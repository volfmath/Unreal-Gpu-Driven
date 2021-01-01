#include "MobileGPUDriven.h"
#include "InstancedStaticMesh.h"
#include "StaticMeshResources.h"

//--------------------------------Static Function--------------------------------
TMap<uint32, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::GlobalWorldIndexToSystemMap;

//以下容器仅引用资源
TMap<uint32, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::GlobalUniqueIdToSystemMap_GameThread;
TMap<uint32, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::GlobalUniqueIdToSystemMap_RenderThread;

//GameThread Add System, RenderThread remove
void FMobileGPUDrivenSystem::RegisterEntity(UInstancedStaticMeshComponent* InstanceComponent) {
	//Only GameThread
	uint32 UniqueObjectIndex = InstanceComponent->GetUniqueID();
	uint32 UniqueWorldIndex = InstanceComponent->GetWorld()->GetUniqueID();

	FMobileGPUDrivenSystem* FoundSystem = GlobalWorldIndexToSystemMap.FindOrAdd(UniqueWorldIndex, new FMobileGPUDrivenSystem{});
	check(!GlobalUniqueIdToSystemMap_GameThread.Contains(UniqueObjectIndex));//注册时不能存在
	GlobalUniqueIdToSystemMap_GameThread.Emplace(UniqueObjectIndex, FoundSystem);

	uint32 EntityIndex = FoundSystem->EntitiesComponents.Emplace(UniqueObjectIndex, InstanceComponent);
	FoundSystem->ComponentToIndexMap_GameThread.Emplace(UniqueObjectIndex, EntityIndex);

	//#TODO: Remove the parameter of PerInstanceRenderData, directly use InstanceComponent member
	FMeshEntity SubmitToRenderThreadMeshEntity = FMeshEntity::CreateMeshEntity(InstanceComponent, InstanceComponent->PerInstanceRenderData);
	ENQUEUE_RENDER_COMMAND(FRegisterEntityToGpuDriven)(
		[RightValueMeshEntity{ MoveTemp(SubmitToRenderThreadMeshEntity) }, FoundSystem](FRHICommandList& RHICmdList)
		{
			FMobileGPUDrivenSystem::RegisterEntity_RenderThread(MoveTemp(const_cast<FMeshEntity&>(RightValueMeshEntity)), FoundSystem);
		}
	);

	//MarkDirty
	FoundSystem->MarkAllComponentsDirty();
}

//Return the index of the removed element
void FMobileGPUDrivenSystem::UnRegisterEntity(uint32 UniqueObjectIndex) {

	//Only GameThread
	auto FoundSystem = GlobalUniqueIdToSystemMap_GameThread.FindChecked(UniqueObjectIndex);
	uint32 EntityIndex_GameThread = FoundSystem->ComponentToIndexMap_GameThread.FindChecked(UniqueObjectIndex);

	//Set the last element index
	auto& ToSwapComponent = FoundSystem->EntitiesComponents.Last();
	uint32& ToSwapEntityIndex_GameThread = FoundSystem->ComponentToIndexMap_GameThread.FindChecked(ToSwapComponent.UniqueObjectId);
	ToSwapEntityIndex_GameThread = EntityIndex_GameThread;

	//Remove element
	FoundSystem->EntitiesComponents.RemoveAtSwap(EntityIndex_GameThread);
	FoundSystem->ComponentToIndexMap_GameThread.Remove(UniqueObjectIndex);
	GlobalUniqueIdToSystemMap_GameThread.Remove(UniqueObjectIndex);

	ENQUEUE_RENDER_COMMAND(FUnRegisterEntityToGpuDriven)(
		[UniqueObjectIndex](FRHICommandList& RHICmdList)
		{
			FMobileGPUDrivenSystem::UnRegisterEntity_RenderThread(UniqueObjectIndex);
		}
	);

	//MarkDirty
	FoundSystem->MarkAllComponentsDirty();
}

void FMobileGPUDrivenSystem::RegisterEntity_RenderThread(FMeshEntity&& MeshEntity, FMobileGPUDrivenSystem* SceneSystemPtr)
{
	//Only RenderThread
	check(!GlobalUniqueIdToSystemMap_RenderThread.Contains(MeshEntity.UniqueObjectId));//注册时不能存在
	GlobalUniqueIdToSystemMap_RenderThread.Emplace(MeshEntity.UniqueObjectId, SceneSystemPtr);

	FMobileGPUDrivenSystem* FoundSystem = SceneSystemPtr;
	uint32 EntityIndex_RenderThread = FoundSystem->Entities.Emplace(MoveTemp(MeshEntity));
	FoundSystem->ComponentToIndexMap_RenderThread.Emplace(MeshEntity.UniqueObjectId, EntityIndex_RenderThread);

	//注册就更新, 因为AddStaticMesh是多线程的, 或者多线程锁?
	FoundSystem->UpdateIndirectDrawCommandBuffer();
}

void FMobileGPUDrivenSystem::UnRegisterEntity_RenderThread(uint32 UniqueObjectIndex) {

	//Only RenderThread
	auto FoundSystem = GlobalUniqueIdToSystemMap_RenderThread.FindChecked(UniqueObjectIndex);
	uint32 EntityIndex_RenderThread = FoundSystem->ComponentToIndexMap_RenderThread.FindChecked(UniqueObjectIndex);
	uint32 UniqueWorldId = FoundSystem->Entities[EntityIndex_RenderThread].UniqueWorldId;

	//Set the ID of the last element
	auto& ToSwapEntity = FoundSystem->Entities.Last();
	uint32& ToSwapEntityIndex_RenderThread = FoundSystem->ComponentToIndexMap_RenderThread.FindChecked(ToSwapEntity.UniqueObjectId);
	ToSwapEntityIndex_RenderThread = EntityIndex_RenderThread;

	//remove element
	FoundSystem->Entities.RemoveAtSwap(EntityIndex_RenderThread);
	FoundSystem->ComponentToIndexMap_RenderThread.Remove(UniqueObjectIndex);
	GlobalUniqueIdToSystemMap_RenderThread.Remove(UniqueObjectIndex);

	FoundSystem->UpdateIndirectDrawCommandBuffer();

	if (FoundSystem->Entities.Num() == 0) {
		GlobalWorldIndexToSystemMap.Remove(UniqueWorldId); //World容器生命周期终结处
		delete FoundSystem;
	}	
}

bool FMobileGPUDrivenSystem::IsGPUDrivenWorld(UWorld* World) {

	check(IsInGameThread());
	check(World);

	return World->WorldType == EWorldType::Type::Game ||
		World->WorldType == EWorldType::Type::Editor ||
		World->WorldType == EWorldType::Type::PIE;
}

FMobileGPUDrivenSystem* FMobileGPUDrivenSystem::GetGPUDrivenSystem_GameThread(uint32 UniqueObjectIndex){

	check(IsInGameThread());

	auto FoundSystem = GlobalUniqueIdToSystemMap_GameThread.Find(UniqueObjectIndex);
	if (FoundSystem) {
		return *FoundSystem;
	}
	else {
		return nullptr;
	}
}

FMobileGPUDrivenSystem* FMobileGPUDrivenSystem::GetGPUDrivenSystem_RenderThreadOrTask(uint32 UniqueObjectIndex) {
	
	auto FoundSystem = GlobalUniqueIdToSystemMap_RenderThread.Find(UniqueObjectIndex);
	if (FoundSystem) {
		return *FoundSystem;
	}
	else {
		return nullptr;
	}
}
//--------------------------------Static Function End--------------------------------

FMeshEntity::FMeshEntity
(
	uint32 InNumLod,
	uint32 InNumDrawElement,
	uint32 InNumRenderCluster,
	uint32 InUniqueObjectId,
	uint32 InUniqueWorldId,
	FBoxSphereBounds InMeshBound,
	TArray<uint32>&& InSectionIndexCount,
	TArray<uint32>&& InSectionFirstIndex,
	TArray<float>&& InScreenLODs,
	TSharedPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> InPerInstanceRenderData
)
	: NumLod(InNumLod)
	, NumDrawElement(InNumDrawElement)
	, NumRenderCluster(InNumRenderCluster)
	, UniqueObjectId(InUniqueObjectId)
	, UniqueWorldId(InUniqueWorldId)
	, MeshBound(InMeshBound)
	, SectionIndexCount(MoveTemp(InSectionIndexCount))
	, SectionFirstIndex(MoveTemp(InSectionFirstIndex))
	, ScreenLODs(MoveTemp(InScreenLODs))
	, PerInstanceRenderData(InPerInstanceRenderData)
{
	check(PerInstanceRenderData.IsValid());
}

FMeshEntity::FMeshEntity(const FMeshEntity& CopyMeshEntity) 
	: NumLod(CopyMeshEntity.NumLod)
	, NumDrawElement(CopyMeshEntity.NumDrawElement)
	, NumRenderCluster(CopyMeshEntity.NumRenderCluster)
	, UniqueObjectId(CopyMeshEntity.UniqueObjectId)
	, UniqueWorldId(CopyMeshEntity.UniqueWorldId)
	, MeshBound(CopyMeshEntity.MeshBound)
	, SectionIndexCount(CopyMeshEntity.SectionIndexCount)
	, SectionFirstIndex(CopyMeshEntity.SectionFirstIndex)
	, ScreenLODs(CopyMeshEntity.ScreenLODs)
	, PerInstanceRenderData(CopyMeshEntity.PerInstanceRenderData)
{
	check(PerInstanceRenderData.IsValid());
}

FMeshEntity::FMeshEntity(FMeshEntity&& CopyMeshEntity)
	: NumLod(CopyMeshEntity.NumLod)
	, NumDrawElement(CopyMeshEntity.NumDrawElement)
	, NumRenderCluster(CopyMeshEntity.NumRenderCluster)
	, UniqueObjectId(CopyMeshEntity.UniqueObjectId)
	, UniqueWorldId(CopyMeshEntity.UniqueWorldId)
	, MeshBound(CopyMeshEntity.MeshBound)
	, SectionIndexCount(MoveTemp(CopyMeshEntity.SectionIndexCount))
	, SectionFirstIndex(MoveTemp(CopyMeshEntity.SectionFirstIndex))
	, ScreenLODs(MoveTemp(CopyMeshEntity.ScreenLODs))
	, PerInstanceRenderData(CopyMeshEntity.PerInstanceRenderData)
{
	check(PerInstanceRenderData.IsValid());
}


FMeshEntity FMeshEntity::CreateMeshEntity(UInstancedStaticMeshComponent* InstanceComponent, TSharedPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> InPerInstanceRenderData) {

	check(IsInGameThread());

	//#TODO: Use Cluster
	//InstanceBuffer存在渲染资源,但像数量或者Instance数据都在FStaticMeshInstanceData结构, 即游戏线程数据中, 所以这里直接使用InPerInstanceRenderData是安全的, 除了渲染资源
	uint32 NumRenderCluster = InPerInstanceRenderData->InstanceBuffer.GetNumInstances();

	UStaticMesh* StaticMesh = InstanceComponent->GetStaticMesh();
	const auto& StaticMeshRenderDta = StaticMesh->RenderData;
	uint32 NumLod = StaticMeshRenderDta->LODResources.Num();
	uint32 NumDrawElement = 0;
	uint32 UniqueObjectId = InstanceComponent->GetUniqueID();
	uint32 UniqueWorldId = InstanceComponent->GetWorld()->GetUniqueID();
	FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
	TArray<uint32> SectionIndexCount;
	TArray<uint32> SectionFirstIndex;
	TArray<float> ScreenLODs;
	ScreenLODs.Reserve(NumLod);

	for (uint32 Lod = 0; Lod < NumLod; ++Lod) {
		const FStaticMeshLODResources& LODModel = StaticMeshRenderDta->LODResources[Lod];
		uint32 NumSection = LODModel.Sections.Num();
		if (InstanceComponent->LODData.Num() > 0) {
			//Make sure there is no PreCulled
			check(InstanceComponent->LODData[Lod].PreCulledSections.Num() == 0);
		}

		for (uint32 SectionIndex = 0; SectionIndex < NumSection; ++SectionIndex) {
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			SectionIndexCount.Emplace(Section.NumTriangles * 0x3);
			SectionFirstIndex.Emplace(Section.FirstIndex);
		}
		float LodScreenSize = StaticMeshRenderDta->ScreenSize[Lod].GetValueForFeatureLevel(ERHIFeatureLevel::Type::ES3_1);
		ScreenLODs.Emplace(LodScreenSize);
		NumDrawElement += NumSection;
	}

	return FMeshEntity(NumLod, NumDrawElement, NumRenderCluster, UniqueObjectId, UniqueWorldId, MeshBounds, MoveTemp(SectionIndexCount), MoveTemp(SectionFirstIndex), MoveTemp(ScreenLODs), InPerInstanceRenderData);
}

FBoxSphereBounds FMeshEntity::GetClusterBounds(int32 ClusterRenderIndex) const {
	return FBoxSphereBounds();
}

FBoxSphereBounds FMeshEntity::GetClusterBounds(int32 ClusterRenderIndex, const FBoxSphereBounds& MeshBounds) const {
	check(PerInstanceRenderData.IsValid());
	const auto& MeshInstanceBuffer = PerInstanceRenderData.Pin()->InstanceBuffer;
	FMatrix LocalToWorld;
	MeshInstanceBuffer.GetInstanceTransform(ClusterRenderIndex, LocalToWorld);
	return MeshBounds.TransformBy(LocalToWorld);
}

FMeshEntityGameThread::FMeshEntityGameThread(uint32 InUniqueObjectId, UInstancedStaticMeshComponent* InEntityComponent)
	: UniqueObjectId(InUniqueObjectId)
	, EntityComponent(InEntityComponent)
{

}

FMobileGPUDrivenSystem::FMobileGPUDrivenSystem() 
{

}

FMobileGPUDrivenSystem::~FMobileGPUDrivenSystem() {

}

void FMobileGPUDrivenSystem::UpdateGlobalGPUBuffer() {

	//if (!bGPUDataDirty)
	//	return;

	//if (IndirectDrawCommandBuffer_GPU.NumBytes != 0) {
	//	IndirectDrawCommandBuffer_GPU.Release();
	//}
	//if (ClusterMappingAndBoundBuffer_GPU.NumBytes != 0) {
	//	ClusterMappingAndBoundBuffer_GPU.Release();
	//}
	//if (LodBuffer_GPU.NumBytes != 0) {
	//	LodBuffer_GPU.Release();
	//}

	////#TODO: TaskGraph
	//TArray<FDrawIndirectCommandArgs_CPU> IndirectDrawCommandBuffer_CPU;
	//TArray<FClusterMappingAndBound_CPU> ClusterMappingAndBoundBuffer_CPU;
	//TArray<FLodBuffer_CPU> LodBuffer_CPU;
	//uint32 LodBufferOffset = 0;

	//for (const auto& ProxyEntity : Entities) {
	//	


	//	int32 ClusterStartIndex = ClusterMappingAndBoundBuffer_CPU.Num();
	//	int32 LocalRenderIndex = 0;
	//	const FBoxSphereBounds& MeshBound = ProxyEntity.MeshBound;
	//	ClusterMappingAndBoundBuffer_CPU.AddZeroed(ProxyEntity.NumRenderCluster);
	//	for (int32 ClusterIndex = ClusterStartIndex; ClusterIndex < ClusterMappingAndBoundBuffer_CPU.Num(); ++ClusterIndex) {
	//		int32 FirstClusterRenderIndex = LocalRenderIndex; //#TODO: ClusterNodes.FirstInstance
	//		const auto& ClusterBound = ProxyEntity.GetClusterBounds(FirstClusterRenderIndex, MeshBound);

	//		auto& ClusterMappingAndBoundBuffer = ClusterMappingAndBoundBuffer_CPU[ClusterIndex];
	//		ClusterMappingAndBoundBuffer.LodBufferStartIndex = LodBufferOffset;
	//		ClusterMappingAndBoundBuffer.FirstRenderIndex = FirstClusterRenderIndex;
	//		ClusterMappingAndBoundBuffer.BoundCenter = ClusterBound.Origin;
	//		ClusterMappingAndBoundBuffer.BoundExtent = ClusterBound.BoxExtent;

	//		LocalRenderIndex += 1;
	//	}

	//	int32 StartLodBufferIndex = LodBuffer_CPU.Num();
	//	int32 LocalLodBufferIndex = 0;
	//	LodBuffer_CPU.AddZeroed(ProxyEntity.NumLod);
	//	for (int32 LodBufferIndex = StartLodBufferIndex; LodBufferIndex < LodBuffer_CPU.Num(); ++LodBufferIndex) {
	//		auto& ClusterLod = LodBuffer_CPU[LodBufferIndex];
	//		ClusterLod.CurLodScreenSize = ProxyEntity.ScreenLODs[LocalLodBufferIndex];

	//		LocalLodBufferIndex += 1;
	//	}

	//	LodBufferOffset += ProxyEntity.NumLod;
	//}


	////Write data to GPU
	//{
	//	ClusterMappingAndBoundBuffer_GPU.Initialize(sizeof(FClusterMappingAndBound_CPU), ClusterMappingAndBoundBuffer_CPU.Num(), BUF_Static);
	//	void* MappingAndBoundData = RHILockStructuredBuffer(ClusterMappingAndBoundBuffer_GPU.Buffer, 0, ClusterMappingAndBoundBuffer_GPU.NumBytes, RLM_WriteOnly);
	//	FMemory::Memcpy(MappingAndBoundData, ClusterMappingAndBoundBuffer_CPU.GetData(), ClusterMappingAndBoundBuffer_GPU.NumBytes);
	//	RHIUnlockStructuredBuffer(ClusterMappingAndBoundBuffer_GPU.Buffer);

	//	LodBuffer_GPU.Initialize(sizeof(FLodBuffer_CPU), LodBuffer_CPU.Num(), BUF_Static);
	//	void* LodBufferData = RHILockStructuredBuffer(LodBuffer_GPU.Buffer, 0, LodBuffer_GPU.NumBytes, RLM_WriteOnly);
	//	FMemory::Memcpy(LodBufferData, LodBuffer_CPU.GetData(), LodBuffer_GPU.NumBytes);
	//	RHIUnlockStructuredBuffer(LodBuffer_GPU.Buffer);
	//}

	//bGPUDataDirty = false;
}

void FMobileGPUDrivenSystem::UpdateIndirectDrawCommandBuffer() {

	if (Entities.Num() == 0) {
		return;
	}

	TArray<FDrawIndirectCommandArgs_CPU> IndirectDrawCommandBuffer_CPU;
	IndirectDrawCommandBuffer_GPU.Release();
	for (const auto& ProxyEntity : Entities) {
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
	}

	IndirectDrawCommandBuffer_GPU.Initialize(sizeof(uint32), IndirectDrawCommandBuffer_CPU.Num() * SLGPUDrivenParameter::IndirectBufferElementSize, PF_R32_UINT, BUF_DrawIndirect | BUF_Static);
	void* IndirectBufferData = RHILockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer, 0, IndirectDrawCommandBuffer_GPU.NumBytes, RLM_WriteOnly);
	FMemory::Memcpy(IndirectBufferData, IndirectDrawCommandBuffer_CPU.GetData(), IndirectDrawCommandBuffer_GPU.NumBytes);
	RHIUnlockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer);
}


//#TODO: GPU Driven统一走Dynamic收集MeshDrawCommand, 可移除
void FMobileGPUDrivenSystem::MarkAllComponentsDirty(){
	check(IsInGameThread());

	for (uint32 i = 0; i < static_cast<uint32>(EntitiesComponents.Num()); ++i) {
		//无效值跳过, 其会被卸载, 跑到渲染线程时不会使用这样的UObject
		//因为GPUBuffer的替换, 所有Component需要重新创建渲染状态
		//如果Componetn已经被标记为Dirty内部会自动跳过
		if (EntitiesComponents[i].EntityComponent.IsValid()) {
			EntitiesComponents[i].EntityComponent->MarkRenderStateDirty();
		}
	}
}


void FMobileGPUDrivenSystem::GetIndirectDrawArgsAndStartIndex(uint32 UniqueObjectId, IndirectDrawArgsAndStartIndex& IndirectBuffer) const{
	//#TODO: Cache IndirectArgsStartIndex
	const uint32 EntityIndex_RenderThread = ComponentToIndexMap_RenderThread.FindChecked(UniqueObjectId);

	uint32 DrawStartIndex = 0;
	for (uint32 i = 0; i < EntityIndex_RenderThread; ++i) {
		DrawStartIndex += Entities[i].NumDrawElement;
	}
	IndirectBuffer.IndirectArgsBuffer = IndirectDrawCommandBuffer_GPU.Buffer;
	IndirectBuffer.IndirectArgsStartIndex = DrawStartIndex * SLGPUDrivenParameter::IndirectCommandSize;
}