#include "MobileGPUDriven.h"
#include "InstancedStaticMesh.h"
#include "StaticMeshResources.h"

//--------------------------------Static Function--------------------------------
TMap<UWorld*, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::GlobalGPUDrivenSystemMap;

void FMobileGPUDrivenSystem::RegisterEntity(UWorld* World, FInstancedStaticMeshSceneProxy* InstanceSceneProxy) {
	check(World);
	if (IsGPUDrivenWorld(World)) {

		auto FoundSystemPtr = GlobalGPUDrivenSystemMap.Find(World);
		if(!FoundSystemPtr){
			*FoundSystemPtr = GlobalGPUDrivenSystemMap.Emplace(World, new FMobileGPUDrivenSystem{});
		}
		FMobileGPUDrivenSystem* FoundSystem = *FoundSystemPtr;
		int32 EntityIndex = FoundSystem->Entities.Emplace(InstanceSceneProxy);
		InstanceSceneProxy->SetEntityIndex(EntityIndex);

		//Mark Dirty
		FoundSystem->bGPUDataDirty = true;
	}
}

void FMobileGPUDrivenSystem::UnRegisterEntity(UWorld* World, FInstancedStaticMeshSceneProxy* InstanceSceneProxy) {
	check(World);
	if (IsGPUDrivenWorld(World)) {
		auto FoundSystem = GlobalGPUDrivenSystemMap.FindChecked(World);
		uint32 EntityIndex = InstanceSceneProxy->GetEntityIndex();

		//Set the index of last element
		FClusterEntity& ToSwapElement = FoundSystem->Entities.Last();
		ToSwapElement.InstanceSceneProxy->SetEntityIndex(EntityIndex);

		//Swap the position of the current element and the last element
		FoundSystem->Entities.RemoveAtSwap(EntityIndex);

		//Mark Dirty
		FoundSystem->bGPUDataDirty = true;

		if (FoundSystem->Entities.Num() == 0) {
			delete FoundSystem;
		}
	}
}

bool FMobileGPUDrivenSystem::IsGPUDrivenWorld(UWorld* World) {
	return World->WorldType == EWorldType::Type::Game ||
		World->WorldType == EWorldType::Type::Editor ||
		World->WorldType == EWorldType::Type::PIE;
}
//--------------------------------Static Function--------------------------------

FClusterEntity::FClusterEntity(FInstancedStaticMeshSceneProxy* InInstanceSceneProxy)
	: NumLod(InInstanceSceneProxy->StaticMesh->GetNumLODs())
	, NumRenderCluster(InInstanceSceneProxy->InstancedRenderData.PerInstanceRenderData->InstanceBuffer.GetNumInstances())
	, NumDrawElement(0)
	, InstanceSceneProxy(InInstanceSceneProxy)
{
	NumSectionPerLod.Reserve(NumLod);
	ScreenLODs.Reserve(NumLod);
	for (int32 Lods = 0; Lods < NumLod; ++Lods) {
		FStaticMeshSceneProxy::FLODInfo& LODInfo = InInstanceSceneProxy->LODs[Lods];
		uint32 NumSection = LODInfo.Sections.Num();
		NumSectionPerLod.Emplace(NumSection);

		for (int32 SectionIndex = 0; SectionIndex < NumSection; ++SectionIndex) {
			//Make sure there is no PreCulled
			check(LODInfo.Sections[SectionIndex].NumPreCulledTriangles == 0xFFFFFFFF);
			const FStaticMeshLODResources& LODModel = InInstanceSceneProxy->RenderData->LODResources[Lods];
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			SectionIndexCount.Emplace(Section.MaxVertexIndex - Section.MinVertexIndex);
			SectionFirstIndex.Emplace(Section.FirstIndex);
		}
		ScreenLODs.Emplace(InstanceSceneProxy->GetScreenSize(Lods));
		NumDrawElement += NumSection;
	}
}

UStaticMesh* FClusterEntity::GetStaticMesh() const {
	return InstanceSceneProxy->StaticMesh;
}

FBoxSphereBounds FClusterEntity::GetClusterBounds(int32 ClusterRenderIndex) const {
	return FBoxSphereBounds();
}

FBoxSphereBounds FClusterEntity::GetClusterBounds(int32 ClusterRenderIndex, const FBoxSphereBounds& MeshBounds) const {
	const auto& MeshInstanceBuffer = InstanceSceneProxy->InstancedRenderData.PerInstanceRenderData->InstanceBuffer;
	FMatrix LocalToWorld;
	MeshInstanceBuffer.GetInstanceTransform(ClusterRenderIndex, LocalToWorld);
	return MeshBounds.TransformBy(LocalToWorld);
}

FMobileGPUDrivenSystem::FMobileGPUDrivenSystem() 
	: bGPUDataDirty(true)
{

}

FMobileGPUDrivenSystem::~FMobileGPUDrivenSystem() {

}

void FMobileGPUDrivenSystem::UpdateGlobalGPUBuffer() {

	check(bGPUDataDirty);

	if (IndirectDrawCommandBuffer_GPU.NumBytes != 0) {
		IndirectDrawCommandBuffer_GPU.Release();
	}
	if (ClusterMappingAndBoundBuffer_GPU.NumBytes != 0) {
		ClusterMappingAndBoundBuffer_GPU.Release();
	}
	if (LodBuffer_GPU.NumBytes != 0) {
		LodBuffer_GPU.Release();
	}

	//#TODO: TaskGraph
	TArray<FDrawIndirectCommandArgs_CPU> IndirectDrawCommandBuffer_CPU;
	TArray<FClusterMappingAndBound_CPU> ClusterMappingAndBoundBuffer_CPU;
	TArray<FLodBuffer_CPU> LodBuffer_CPU;
	uint32 LodBufferOffset = 0;

	for (const auto& ProxyEntity : Entities) {
		auto InstanceSceneProxyPtr = ProxyEntity.InstanceSceneProxy;
		check(ProxyEntity.NumDrawElement == ProxyEntity.SectionFirstIndex.Num() && ProxyEntity.NumDrawElement == ProxyEntity.SectionIndexCount.Num());

		int32 StartDrawCommandIndex = IndirectDrawCommandBuffer_CPU.Num();
		int32 SectionBufferIndex = 0;
		IndirectDrawCommandBuffer_CPU.AddZeroed(ProxyEntity.NumDrawElement);
		for (int32 DrawElementIndex = StartDrawCommandIndex; DrawElementIndex < IndirectDrawCommandBuffer_CPU.Num(); ++DrawElementIndex) {
			auto& DrawCommandBuffer = IndirectDrawCommandBuffer_CPU[DrawElementIndex];
			DrawCommandBuffer.IndexCount = ProxyEntity.SectionIndexCount[SectionBufferIndex];
			DrawCommandBuffer.InstanceCount = ProxyEntity.NumRenderCluster; //#TODO: 0
			DrawCommandBuffer.FirstIndex = ProxyEntity.SectionFirstIndex[SectionBufferIndex];
			DrawCommandBuffer.VertexOffset = 0;
			DrawCommandBuffer.FirstInstance = 0; //GPU Write

			SectionBufferIndex += 1;
		}

		int32 ClusterStartIndex = ClusterMappingAndBoundBuffer_CPU.Num();
		int32 LocalInstanceIndex = 0;
		const FBoxSphereBounds& MeshBound = ProxyEntity.GetStaticMesh()->GetBounds();
		ClusterMappingAndBoundBuffer_CPU.AddZeroed(ProxyEntity.NumRenderCluster);
		for (int32 ClusterIndex = ClusterStartIndex; ClusterIndex < ClusterMappingAndBoundBuffer_CPU.Num(); ++ClusterIndex) {
			int32 FirstClusterRenderIndex = LocalInstanceIndex; //#TODO: ClusterNodes.FirstInstance
			const auto& ClusterBound = ProxyEntity.GetClusterBounds(FirstClusterRenderIndex, MeshBound);

			auto& ClusterMappingAndBoundBuffer = ClusterMappingAndBoundBuffer_CPU[ClusterIndex];
			ClusterMappingAndBoundBuffer.LodBufferStartIndex = LodBufferOffset;
			ClusterMappingAndBoundBuffer.FirstRenderIndex = FirstClusterRenderIndex;
			ClusterMappingAndBoundBuffer.BoundCenter = ClusterBound.Origin;
			ClusterMappingAndBoundBuffer.BoundExtent = ClusterBound.BoxExtent;

			LocalInstanceIndex += 1;
		}

		//#TODO: LodBuffer 


		LodBufferOffset += ProxyEntity.NumLod;
	}


	//Write data to gpu
	{
		IndirectDrawCommandBuffer_GPU.Initialize(sizeof(uint32), IndirectDrawCommandBuffer_CPU.Num() * SLGPUDrivenParameter::IndirectBufferElementSize, PF_R32_UINT, BUF_DrawIndirect | BUF_Static);
		void* GPUDataPtr = RHILockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer, 0, IndirectDrawCommandBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(GPUDataPtr, IndirectDrawCommandBuffer_CPU.GetData(), IndirectDrawCommandBuffer_GPU.NumBytes);
		RHIUnlockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer);


	}

	bGPUDataDirty = false;
}

