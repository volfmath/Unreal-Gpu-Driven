#include "MobileGPUDriven.h"
#include "InstancedStaticMesh.h"
#include "StaticMeshResources.h"

//--------------------------------Static Function--------------------------------
TMap<UWorld*, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::GlobalGPUDrivenSystemMap;

void FMobileGPUDrivenSystem::RegisterEntity(UWorld* World, FInstancedStaticMeshSceneProxy* InstanceSceneProxy) {
	//Only RenderThread
	check(World);
	auto FoundSystemPtr = GlobalGPUDrivenSystemMap.Find(World);
	if (!FoundSystemPtr) {
		*FoundSystemPtr = GlobalGPUDrivenSystemMap.Emplace(World, new FMobileGPUDrivenSystem{});
	}
	FMobileGPUDrivenSystem* FoundSystem = *FoundSystemPtr;
	int32 EntityIndex = FoundSystem->Entities.Emplace(InstanceSceneProxy);
	InstanceSceneProxy->SetEntityIndex(EntityIndex);

	//Mark Dirty
	FoundSystem->bGPUDataDirty = true;
}

void FMobileGPUDrivenSystem::UnRegisterEntity(UWorld* World, FInstancedStaticMeshSceneProxy* InstanceSceneProxy) {

	//Only RenderThread
	check(World);
	auto FoundSystem = GlobalGPUDrivenSystemMap.FindChecked(World);
	uint32 EntityIndex = InstanceSceneProxy->GetEntityIndex();

	//Set the index of last element
	FMeshEntity& ToSwapElement = FoundSystem->Entities.Last();
	ToSwapElement.InstanceSceneProxy->SetEntityIndex(EntityIndex);

	//Swap the position of the current element and the last element
	FoundSystem->Entities.RemoveAtSwap(EntityIndex);

	//Mark Dirty
	FoundSystem->bGPUDataDirty = true;

	if (FoundSystem->Entities.Num() == 0) {
		GlobalGPUDrivenSystemMap.Remove(World);
		delete FoundSystem;
	}
}

bool FMobileGPUDrivenSystem::IsGPUDrivenWorld(UWorld* World) {
	check(World);
	return World->WorldType == EWorldType::Type::Game ||
		World->WorldType == EWorldType::Type::Editor ||
		World->WorldType == EWorldType::Type::PIE;
}

int32 FMobileGPUDrivenSystem::GetIndirectDrawStartIndex(UWorld* World, int32 EntityIndex) {

	auto FoundSystem = GlobalGPUDrivenSystemMap.FindChecked(World);
	const auto& Entities = FoundSystem->Entities;
	int32 DrawStartIndex = 0;
	for (int32 i = 0; i < EntityIndex; ++i) {
		DrawStartIndex += Entities[i].NumDrawElement;
	}
	return DrawStartIndex;
}
//--------------------------------Static Function--------------------------------

FMeshEntity::FMeshEntity(FInstancedStaticMeshSceneProxy* InInstanceSceneProxy)
	: NumLod(InInstanceSceneProxy->StaticMesh->GetNumLODs())
	, NumRenderCluster(InInstanceSceneProxy->InstancedRenderData.PerInstanceRenderData->InstanceBuffer.GetNumInstances())
	, NumDrawElement(0)
	, InstanceSceneProxy(InInstanceSceneProxy)
{
	NumSectionPerLod.Reserve(NumLod);
	ScreenLODs.Reserve(NumLod);
	for (uint32 Lods = 0; Lods < NumLod; ++Lods) {
		FStaticMeshSceneProxy::FLODInfo& LODInfo = InInstanceSceneProxy->LODs[Lods];
		uint32 NumSection = LODInfo.Sections.Num();
		NumSectionPerLod.Emplace(NumSection);

		for (uint32 SectionIndex = 0; SectionIndex < NumSection; ++SectionIndex) {
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

UStaticMesh* FMeshEntity::GetStaticMesh() const {
	return InstanceSceneProxy->StaticMesh;
}

FBoxSphereBounds FMeshEntity::GetClusterBounds(int32 ClusterRenderIndex) const {
	return FBoxSphereBounds();
}

FBoxSphereBounds FMeshEntity::GetClusterBounds(int32 ClusterRenderIndex, const FBoxSphereBounds& MeshBounds) const {
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

	if (!bGPUDataDirty)
		return;

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
			DrawCommandBuffer.InstanceCount = ProxyEntity.NumRenderCluster;
			DrawCommandBuffer.FirstIndex = ProxyEntity.SectionFirstIndex[SectionBufferIndex];
			DrawCommandBuffer.VertexOffset = 0;
			DrawCommandBuffer.FirstInstance = 0; //GPU Write

			SectionBufferIndex += 1;
		}

		int32 ClusterStartIndex = ClusterMappingAndBoundBuffer_CPU.Num();
		int32 LocalRenderIndex = 0;
		const FBoxSphereBounds& MeshBound = ProxyEntity.GetStaticMesh()->GetBounds();
		ClusterMappingAndBoundBuffer_CPU.AddZeroed(ProxyEntity.NumRenderCluster);
		for (int32 ClusterIndex = ClusterStartIndex; ClusterIndex < ClusterMappingAndBoundBuffer_CPU.Num(); ++ClusterIndex) {
			int32 FirstClusterRenderIndex = LocalRenderIndex; //#TODO: ClusterNodes.FirstInstance
			const auto& ClusterBound = ProxyEntity.GetClusterBounds(FirstClusterRenderIndex, MeshBound);

			auto& ClusterMappingAndBoundBuffer = ClusterMappingAndBoundBuffer_CPU[ClusterIndex];
			ClusterMappingAndBoundBuffer.LodBufferStartIndex = LodBufferOffset;
			ClusterMappingAndBoundBuffer.FirstRenderIndex = FirstClusterRenderIndex;
			ClusterMappingAndBoundBuffer.BoundCenter = ClusterBound.Origin;
			ClusterMappingAndBoundBuffer.BoundExtent = ClusterBound.BoxExtent;

			LocalRenderIndex += 1;
		}

		int32 StartLodBufferIndex = LodBuffer_CPU.Num();
		int32 LocalLodBufferIndex = 0;
		LodBuffer_CPU.AddZeroed(ProxyEntity.NumLod);
		for (int32 LodBufferIndex = StartLodBufferIndex; LodBufferIndex < LodBuffer_CPU.Num(); ++LodBufferIndex) {
			auto& ClusterLod = LodBuffer_CPU[LodBufferIndex];
			ClusterLod.CurLodScreenSize = ProxyEntity.ScreenLODs[LocalLodBufferIndex];

			LocalLodBufferIndex += 1;
		}

		LodBufferOffset += ProxyEntity.NumLod;
	}


	//Write data to GPU
	{
		IndirectDrawCommandBuffer_GPU.Initialize(sizeof(uint32), IndirectDrawCommandBuffer_CPU.Num() * SLGPUDrivenParameter::IndirectBufferElementSize, PF_R32_UINT, BUF_DrawIndirect | BUF_Static);
		void* IndirectBufferData = RHILockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer, 0, IndirectDrawCommandBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(IndirectBufferData, IndirectDrawCommandBuffer_CPU.GetData(), IndirectDrawCommandBuffer_GPU.NumBytes);
		RHIUnlockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer);

		ClusterMappingAndBoundBuffer_GPU.Initialize(sizeof(FClusterMappingAndBound_CPU), ClusterMappingAndBoundBuffer_CPU.Num(), BUF_Static);
		void* MappingAndBoundData = RHILockStructuredBuffer(ClusterMappingAndBoundBuffer_GPU.Buffer, 0, ClusterMappingAndBoundBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(MappingAndBoundData, ClusterMappingAndBoundBuffer_CPU.GetData(), ClusterMappingAndBoundBuffer_GPU.NumBytes);
		RHIUnlockStructuredBuffer(ClusterMappingAndBoundBuffer_GPU.Buffer);

		LodBuffer_GPU.Initialize(sizeof(FLodBuffer_CPU), LodBuffer_CPU.Num(), BUF_Static);
		void* LodBufferData = RHILockStructuredBuffer(LodBuffer_GPU.Buffer, 0, LodBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(LodBufferData, LodBuffer_CPU.GetData(), LodBuffer_GPU.NumBytes);
		RHIUnlockStructuredBuffer(LodBuffer_GPU.Buffer);
	}

	bGPUDataDirty = false;
}

