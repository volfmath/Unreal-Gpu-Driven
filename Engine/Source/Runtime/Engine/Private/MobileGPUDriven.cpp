#include "MobileGPUDriven.h"
#include "InstancedStaticMesh.h"
#include "StaticMeshResources.h"

ENGINE_API TAutoConsoleVariable<int32> CVarMobileEnableGPUDriven(
	TEXT("r.Mobile.GpuDriven"),
	1,
	TEXT("Whether to allow gpudriven.\n"),
	ECVF_Scalability
);


//--------------------------------Static Function--------------------------------
TMap<uint32, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::WorldIndexToSystemMap_GameThread; //逻辑线程只负责创建
TMap<uint32, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::WorldIndexToSystemMap_RenderThread; //渲染线程负责释放内存

//以下容器仅引用资源
TMap<uint32, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::GlobalUniqueIdToSystemMap_GameThread;
TMap<uint32, FMobileGPUDrivenSystem*> FMobileGPUDrivenSystem::GlobalUniqueIdToSystemMap_RenderThread;

//GameThread Add System, RenderThread remove
void FMobileGPUDrivenSystem::RegisterEntity(UInstancedStaticMeshComponent* InstanceComponent) {
	//Only GameThread
	check(IsInGameThread());
	uint32 UniqueObjectIndex = InstanceComponent->GetUniqueID();
	uint32 UniqueWorldIndex = InstanceComponent->GetWorld()->GetUniqueID();

	FMobileGPUDrivenSystem* FoundSystem = WorldIndexToSystemMap_GameThread.FindOrAdd(UniqueWorldIndex, new FMobileGPUDrivenSystem{}); ////World容器生命周期创建处
	check(!GlobalUniqueIdToSystemMap_GameThread.Contains(UniqueObjectIndex));//注册时不能存在
	GlobalUniqueIdToSystemMap_GameThread.Emplace(UniqueObjectIndex, FoundSystem);
	FoundSystem->WorldEntityCount_GameThread += 1;
	/*uint32 EntityIndex = FoundSystem->EntitiesComponents.Emplace(UniqueObjectIndex, InstanceComponent);
	FoundSystem->UniqueIdToEntityIndex_GameThread.Emplace(UniqueObjectIndex, EntityIndex);*/

	FMeshEntity SubmitToRenderThreadMeshEntity = FMeshEntity::CreateMeshEntity(InstanceComponent);
	ENQUEUE_RENDER_COMMAND(FRegisterEntityToGpuDriven)(
		[RightValueMeshEntity{ MoveTemp(SubmitToRenderThreadMeshEntity) }, FoundSystem](FRHICommandList& RHICmdList)
		{
			FMobileGPUDrivenSystem::RegisterEntity_RenderThread(MoveTemp(const_cast<FMeshEntity&>(RightValueMeshEntity)), FoundSystem);
		}
	);

	//MarkDirty
	//FoundSystem->MarkAllComponentsDirty();
}

//Return the index of the removed element
void FMobileGPUDrivenSystem::UnRegisterEntity(uint32 UniqueObjectIndex, uint32 UniqueWorldId) {

	//Only GameThread
	check(IsInGameThread());
	auto FoundSystem = GlobalUniqueIdToSystemMap_GameThread.FindChecked(UniqueObjectIndex);
	FoundSystem->WorldEntityCount_GameThread -= 1;
	//uint32 EntityIndex_GameThread = FoundSystem->UniqueIdToEntityIndex_GameThread.FindChecked(UniqueObjectIndex);

	////Set the last element index
	//auto& ToSwapComponent = FoundSystem->EntitiesComponents.Last();
	//uint32& ToSwapEntityIndex_GameThread = FoundSystem->UniqueIdToEntityIndex_GameThread.FindChecked(ToSwapComponent.UniqueObjectId);
	//ToSwapEntityIndex_GameThread = EntityIndex_GameThread;

	////Remove element
	//FoundSystem->EntitiesComponents.RemoveAtSwap(EntityIndex_GameThread);
	//FoundSystem->UniqueIdToEntityIndex_GameThread.Remove(UniqueObjectIndex);
	GlobalUniqueIdToSystemMap_GameThread.Remove(UniqueObjectIndex);
	
	if (FoundSystem->WorldEntityCount_GameThread == 0) {
		WorldIndexToSystemMap_GameThread.Remove(UniqueWorldId); //如果UniqueWorldId存在问题,需要UniqueIdToEntityIndex_GameThread容器来寻找Entity
	}

	ENQUEUE_RENDER_COMMAND(FUnRegisterEntityToGpuDriven)(
		[UniqueObjectIndex](FRHICommandList& RHICmdList)
		{
			FMobileGPUDrivenSystem::UnRegisterEntity_RenderThread(UniqueObjectIndex);
		}
	);

	//MarkDirty
	//FoundSystem->MarkAllComponentsDirty();
}

void FMobileGPUDrivenSystem::RegisterEntity_RenderThread(FMeshEntity&& MeshEntity, FMobileGPUDrivenSystem* SceneSystemPtr)
{
	//Only RenderThread
	check(!GlobalUniqueIdToSystemMap_RenderThread.Contains(MeshEntity.UniqueObjectId));//注册时不能存在
	GlobalUniqueIdToSystemMap_RenderThread.Emplace(MeshEntity.UniqueObjectId, SceneSystemPtr);
	WorldIndexToSystemMap_RenderThread.Emplace(MeshEntity.UniqueWorldId, SceneSystemPtr);

	uint32 EntityIndex_RenderThread = SceneSystemPtr->Entities.Emplace(MoveTemp(MeshEntity));
	SceneSystemPtr->UniqueIdToEntityIndex_RenderThread.Emplace(MeshEntity.UniqueObjectId, EntityIndex_RenderThread);


	//注册就更新, 因为AddStaticMesh是多线程的, 或者多线程锁?
	SceneSystemPtr->UpdateAllGPUBuffer();

}

void FMobileGPUDrivenSystem::UnRegisterEntity_RenderThread(uint32 UniqueObjectIndex) {

	//Only RenderThread
	auto FoundSystem = GlobalUniqueIdToSystemMap_RenderThread.FindChecked(UniqueObjectIndex);
	uint32 EntityIndex_RenderThread = FoundSystem->UniqueIdToEntityIndex_RenderThread.FindChecked(UniqueObjectIndex);
	uint32 UniqueWorldId = FoundSystem->Entities[EntityIndex_RenderThread].UniqueWorldId;

	//Set the ID of the last element
	auto& ToSwapEntity = FoundSystem->Entities.Last();
	uint32& ToSwapEntityIndex_RenderThread = FoundSystem->UniqueIdToEntityIndex_RenderThread.FindChecked(ToSwapEntity.UniqueObjectId);
	ToSwapEntityIndex_RenderThread = EntityIndex_RenderThread;

	//remove element
	FoundSystem->Entities.RemoveAtSwap(EntityIndex_RenderThread);
	FoundSystem->UniqueIdToEntityIndex_RenderThread.Remove(UniqueObjectIndex);
	GlobalUniqueIdToSystemMap_RenderThread.Remove(UniqueObjectIndex);

	FoundSystem->UpdateAllGPUBuffer();

	if (FoundSystem->Entities.Num() == 0) {
		WorldIndexToSystemMap_RenderThread.Remove(UniqueWorldId);
		delete FoundSystem;//World容器生命周期终结处
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

FMobileGPUDrivenSystem* FMobileGPUDrivenSystem::GetGPUDrivenSystem_RenderThreadByWorldId(uint32 UniqueWorldIndex) {

	auto FoundSystem = WorldIndexToSystemMap_RenderThread.Find(UniqueWorldIndex);
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
	uint32 InUniqueObjectId,
	uint32 InUniqueWorldId,
	float InCullDistance,
	TArray<uint32>&& InSectionIndexCount,
	TArray<uint32>&& InSectionFirstIndex,
	TArray<uint32>&& InPerLodSectionCount,
	TArray<float>&& InScreenLODs,
	TSharedPtr<TArray<FGpuDrivenCluster>, ESPMode::ThreadSafe> InGpuDrivenCluster,
	const FMatrix& InComponentLocalToWorld,
	const FGpuDrivenInstancingUserData& UserData
)
	: NumLod(InNumLod)
	, NumDrawElement(InNumDrawElement)
	, UniqueObjectId(InUniqueObjectId)
	, UniqueWorldId(InUniqueWorldId)
	, CullDistance(InCullDistance)
	, SectionIndexCount(MoveTemp(InSectionIndexCount))
	, SectionFirstIndex(MoveTemp(InSectionFirstIndex))
	, PerLodSectionCount(MoveTemp(InPerLodSectionCount))
	, ScreenLODs(MoveTemp(InScreenLODs))
	, GpuDrivenCluster(InGpuDrivenCluster)
	, IndirectArgsStartIndex(0)
	, GpuDriven_UserData(UserData)
{
	check(GpuDrivenCluster.IsValid());
}

FMeshEntity::FMeshEntity(const FMeshEntity& CopyMeshEntity) 
	: NumLod(CopyMeshEntity.NumLod)
	, NumDrawElement(CopyMeshEntity.NumDrawElement)
	, UniqueObjectId(CopyMeshEntity.UniqueObjectId)
	, UniqueWorldId(CopyMeshEntity.UniqueWorldId)
	, CullDistance(CopyMeshEntity.CullDistance)
	, SectionIndexCount(CopyMeshEntity.SectionIndexCount)
	, SectionFirstIndex(CopyMeshEntity.SectionFirstIndex)
	, PerLodSectionCount(CopyMeshEntity.PerLodSectionCount)
	, ScreenLODs(CopyMeshEntity.ScreenLODs)
	, GpuDrivenCluster(CopyMeshEntity.GpuDrivenCluster)
	, IndirectArgsStartIndex(CopyMeshEntity.IndirectArgsStartIndex)
	, GpuDriven_UserData(CopyMeshEntity.GpuDriven_UserData)
{
	check(false);
}

FMeshEntity::FMeshEntity(FMeshEntity&& CopyMeshEntity)
	: NumLod(CopyMeshEntity.NumLod)
	, NumDrawElement(CopyMeshEntity.NumDrawElement)
	, UniqueObjectId(CopyMeshEntity.UniqueObjectId)
	, UniqueWorldId(CopyMeshEntity.UniqueWorldId)
	, CullDistance(CopyMeshEntity.CullDistance)
	, SectionIndexCount(MoveTemp(CopyMeshEntity.SectionIndexCount))
	, SectionFirstIndex(MoveTemp(CopyMeshEntity.SectionFirstIndex))
	, PerLodSectionCount(MoveTemp(CopyMeshEntity.PerLodSectionCount))
	, ScreenLODs(MoveTemp(CopyMeshEntity.ScreenLODs))
	, GpuDrivenCluster(CopyMeshEntity.GpuDrivenCluster)
	, IndirectArgsStartIndex(CopyMeshEntity.IndirectArgsStartIndex)
	, GpuDriven_UserData(CopyMeshEntity.GpuDriven_UserData)
{
	check(GpuDrivenCluster.IsValid());
}


FMeshEntity FMeshEntity::CreateMeshEntity(UInstancedStaticMeshComponent* InstanceComponent) {

	check(IsInGameThread());

	UStaticMesh* StaticMesh = InstanceComponent->GetStaticMesh();
	const auto& StaticMeshRenderDta = StaticMesh->RenderData;
	uint32 NumLod = StaticMeshRenderDta->LODResources.Num();
	uint32 NumDrawElement = 0;
	uint32 UniqueObjectId = InstanceComponent->GetUniqueID();
	uint32 UniqueWorldId = InstanceComponent->GetWorld()->GetUniqueID();
	FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
	TArray<uint32> SectionIndexCount;
	TArray<uint32> SectionFirstIndex;
	TArray<uint32> PerLodSectionCount;
	TArray<float> ScreenLODs;

	ScreenLODs.Reserve(NumLod);
	PerLodSectionCount.Reserve(NumLod);

	for (uint32 Lod = 0; Lod < NumLod; ++Lod) {
		const FStaticMeshLODResources& LODModel = StaticMeshRenderDta->LODResources[Lod];
		uint32 NumSection = LODModel.Sections.Num();
		if (InstanceComponent->LODData.Num() > 0) {
			//Make sure there is no PreCulled
			check(InstanceComponent->LODData[Lod].PreCulledSections.Num() == 0);
		}

		PerLodSectionCount.Emplace(NumSection);

		for (uint32 SectionIndex = 0; SectionIndex < NumSection; ++SectionIndex) {
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			SectionIndexCount.Emplace(Section.NumTriangles * 0x3);
			SectionFirstIndex.Emplace(Section.FirstIndex);
		}
		float LodScreenSize = StaticMeshRenderDta->ScreenSize[Lod].GetValueForFeatureLevel(ERHIFeatureLevel::Type::ES3_1);
		ScreenLODs.Emplace(LodScreenSize);
		NumDrawElement += NumSection;
	}

	FGpuDrivenInstancingUserData NewUserData = FGpuDrivenInstancingUserData(InstanceComponent);

	//grass.CullDistanceScale已经在InstanceEndCullDistance其中赋值,所以不处理GGrassCullDistanceScale
	float InstanceCullDis = InstanceComponent->InstanceEndCullDistance * GetCachedScalabilityCVars().ViewDistanceScale;

	return FMeshEntity(
		NumLod, 
		NumDrawElement, 
		UniqueObjectId, 
		UniqueWorldId, 
		InstanceCullDis,
		MoveTemp(SectionIndexCount), 
		MoveTemp(SectionFirstIndex), 
		MoveTemp(PerLodSectionCount),
		MoveTemp(ScreenLODs), 
		InstanceComponent->GpuDrivenCluster, 
		InstanceComponent->GetComponentTransform().ToMatrixWithScale(), 
		NewUserData
	);
}

FMeshEntityGameThread::FMeshEntityGameThread(uint32 InUniqueObjectId, UInstancedStaticMeshComponent* InEntityComponent)
	: UniqueObjectId(InUniqueObjectId)
	, EntityComponent(InEntityComponent)
{

}

FMobileGPUDrivenSystem::FMobileGPUDrivenSystem() 
	: WorldEntityCount_GameThread(0)
{

}

FMobileGPUDrivenSystem::~FMobileGPUDrivenSystem() {

}

void FMobileGPUDrivenSystem::UpdateAllGPUBuffer() {

	//Resources Release
	{
		EntityLodScreenBuffer_GPU.Release();
		IndirectDrawToLodIndexBuffer_GPU.Release();
		ClusterInputData_GPU.Release();
		IndirectDrawCommandBuffer_GPU.Release();
		EntityLodBufferCount_GPU.Release();
		ClusterOutputData_GPU.Release();
		InstanceToRenderIndexBuffer_GPU.Release();
	}

	CurTotalClusterCount = 0;
	CurTotalLodCount = 0;
	CurTotalIndirectDrawCount = 0;

	//#TODO: TaskGraph
	TArray<FDrawIndirectCommandArgs_CPU> IndirectDrawCommandBuffer_CPU;
	TArray<uint32> IndirectDrawToLodIndexBuffer_CPU;
	TArray<FClusterInputData_CPU> ClusterData_CPU;
	TArray<float> EntityLodScreenBuffer_CPU;
	uint32 CurTotalInstanceCount = 0;

	for (auto& ProxyEntity : Entities) {
		const auto& ClusterArray = *ProxyEntity.GpuDrivenCluster;
		uint32 NumRenderCluster = ClusterArray.Num();

		//Update Entity parameters
		{
			ProxyEntity.GpuDriven_UserData.InstanceToRenderStartIndex = CurTotalInstanceCount; 
			ProxyEntity.IndirectArgsStartIndex = IndirectDrawCommandBuffer_CPU.Num() * SLGPUDrivenParameter::IndirectCommandSize; //Set IndirectStartIndex
		}

		//About ClusterBuffer
		{
			int32 ClusterStartIndex = ClusterData_CPU.Num();

			int32 LocalClusterIndex = 0;
			uint32 LocalStartInstanceIndex = CurTotalInstanceCount; //InstnaceMapBufferIndex同一个Component均相同
			ClusterData_CPU.AddZeroed(ClusterArray.Num());
			for (int32 ClusterIndex = ClusterStartIndex; ClusterIndex < ClusterData_CPU.Num(); ++ClusterIndex) {
				const FGpuDrivenCluster& CurCluster = ClusterArray[LocalClusterIndex];

				auto& ClusterDataBuffer = ClusterData_CPU[ClusterIndex];
				ClusterDataBuffer.FirstRenderIndex = CurCluster.FirstRenderIndex;
				ClusterDataBuffer.LodBufferStartIndex = CurTotalLodCount;
				check(CurCluster.ClusterInstanceCount >= 1 && CurCluster.ClusterInstanceCount <= 65535); //确保不为空并且压缩在8位内
				ClusterDataBuffer.ClusterInstanceCount = CurCluster.ClusterInstanceCount;
				ClusterDataBuffer.InstanceBufferStartIndex = LocalStartInstanceIndex;	
				ClusterDataBuffer.CullDistance = ProxyEntity.CullDistance;
				ClusterDataBuffer.BoundCenter = CurCluster.BoundCenter;
				ClusterDataBuffer.MeshLodCount = ProxyEntity.NumLod;
				ClusterDataBuffer.BoundExtent = CurCluster.BoundExtent;
				LocalClusterIndex += 1;
				CurTotalInstanceCount += CurCluster.ClusterInstanceCount;
			}
		}

		//About LodBuffer
		{
			int32 StartLodBufferIndex = EntityLodScreenBuffer_CPU.Num();
			int32 LocalLodBufferIndex = 0;
			EntityLodScreenBuffer_CPU.AddZeroed(ProxyEntity.NumLod);
			for (int32 LodBufferIndex = StartLodBufferIndex; LodBufferIndex < EntityLodScreenBuffer_CPU.Num(); ++LodBufferIndex) {
				EntityLodScreenBuffer_CPU[LodBufferIndex] = ProxyEntity.ScreenLODs[LocalLodBufferIndex];
				LocalLodBufferIndex += 1;
			}
		}

		//About IndirectDrawCommandeBuffer
		{
			checkSlow(ProxyEntity.NumDrawElement == ProxyEntity.SectionFirstIndex.Num() && ProxyEntity.NumDrawElement == ProxyEntity.SectionIndexCount.Num());

			int32 StartDrawCommandIndex = IndirectDrawCommandBuffer_CPU.Num();

			int32 LocalIndirectDrawIndex = 0;
			int32 LocalLodIndex = 0;
			int32 LocalSectionIndex = 0;

			IndirectDrawCommandBuffer_CPU.AddZeroed(ProxyEntity.NumDrawElement);
			IndirectDrawToLodIndexBuffer_CPU.AddZeroed(ProxyEntity.NumDrawElement); 

			for (int32 DrawElementIndex = StartDrawCommandIndex; DrawElementIndex < IndirectDrawCommandBuffer_CPU.Num(); ++DrawElementIndex) {
				auto& DrawCommandBuffer = IndirectDrawCommandBuffer_CPU[DrawElementIndex];
				DrawCommandBuffer.IndexCount = ProxyEntity.SectionIndexCount[LocalIndirectDrawIndex];
				DrawCommandBuffer.InstanceCount = NumRenderCluster;
				DrawCommandBuffer.FirstIndex = ProxyEntity.SectionFirstIndex[LocalIndirectDrawIndex];
				DrawCommandBuffer.VertexOffset = 0;
				DrawCommandBuffer.FirstInstance = 0; //GPU Write
				LocalIndirectDrawIndex += 1;

				auto& IndirectDrawToLodIndex = IndirectDrawToLodIndexBuffer_CPU[DrawElementIndex];
				IndirectDrawToLodIndex = LocalLodIndex + CurTotalLodCount;
				LocalSectionIndex += 1;
				if (ProxyEntity.PerLodSectionCount[LocalLodIndex] == LocalSectionIndex) {
					LocalLodIndex += 1;
					LocalSectionIndex = 0;
				}
			}
		}

		//Total count
		CurTotalClusterCount += NumRenderCluster;
		CurTotalLodCount += ProxyEntity.NumLod;
		CurTotalIndirectDrawCount += ProxyEntity.NumDrawElement;
	}

	//Write data to GPU
	if(Entities.Num() != 0)
	{
		ClusterInputData_GPU.Initialize(sizeof(FClusterInputData_CPU), ClusterData_CPU.Num(), BUF_Static);
		void* MappingAndBoundData = RHILockStructuredBuffer(ClusterInputData_GPU.Buffer, 0, ClusterInputData_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(MappingAndBoundData, ClusterData_CPU.GetData(), ClusterInputData_GPU.NumBytes);
		RHIUnlockStructuredBuffer(ClusterInputData_GPU.Buffer);

		EntityLodScreenBuffer_GPU.Initialize(sizeof(float), EntityLodScreenBuffer_CPU.Num(), PF_R32_FLOAT, BUF_Static); //#TODO: PF_R16_FLOAT
		void* LodBufferData = RHILockVertexBuffer(EntityLodScreenBuffer_GPU.Buffer, 0, EntityLodScreenBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(LodBufferData, EntityLodScreenBuffer_CPU.GetData(), EntityLodScreenBuffer_GPU.NumBytes);
		RHIUnlockVertexBuffer(EntityLodScreenBuffer_GPU.Buffer);

		IndirectDrawCommandBuffer_GPU.Initialize(sizeof(uint32), IndirectDrawCommandBuffer_CPU.Num() * SLGPUDrivenParameter::IndirectBufferElementSize, PF_R32_UINT, BUF_DrawIndirect | BUF_Static);
		void* IndirectBufferData = RHILockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer, 0, IndirectDrawCommandBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(IndirectBufferData, IndirectDrawCommandBuffer_CPU.GetData(), IndirectDrawCommandBuffer_GPU.NumBytes);
		RHIUnlockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer);

		IndirectDrawToLodIndexBuffer_GPU.Initialize(sizeof(uint32), IndirectDrawToLodIndexBuffer_CPU.Num(), PF_R32_UINT, BUF_Static);
		void* IndirectDrawToLodIndexBufferData = RHILockVertexBuffer(IndirectDrawToLodIndexBuffer_GPU.Buffer, 0, IndirectDrawToLodIndexBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(IndirectDrawToLodIndexBufferData, IndirectDrawToLodIndexBuffer_CPU.GetData(), IndirectDrawToLodIndexBuffer_GPU.NumBytes);
		RHIUnlockVertexBuffer(IndirectDrawToLodIndexBuffer_GPU.Buffer);

		ClusterOutputData_GPU.Initialize(sizeof(FClusterOutputData_CPU), CurTotalClusterCount, BUF_Static);
		EntityLodBufferCount_GPU.Initialize(sizeof(uint32), CurTotalLodCount, PF_R32_UINT, BUF_Static);
		InstanceToRenderIndexBuffer_GPU.Initialize(sizeof(uint32), CurTotalInstanceCount, BUF_Static);

		check(CurTotalLodCount == EntityLodScreenBuffer_CPU.Num());
	}

	for (auto& ProxyEntity : Entities) {
		ProxyEntity.GpuDriven_UserData.InstanceToRenderIndexBufferSRV = InstanceToRenderIndexBuffer_GPU.SRV.GetReference();
	}
}

////#TODO: GPU Driven统一走Dynamic收集MeshDrawCommand, 可移除
//void FMobileGPUDrivenSystem::MarkAllComponentsDirty(){
//	check(IsInGameThread());
//
//	for (uint32 i = 0; i < static_cast<uint32>(EntitiesComponents.Num()); ++i) {
//		//无效值跳过, 其会被卸载, 跑到渲染线程时不会使用这样的UObject
//		//因为GPUBuffer的替换, 所有Component需要重新创建渲染状态
//		//如果Componetn已经被标记为Dirty内部会自动跳过
//		if (EntitiesComponents[i].EntityComponent.IsValid()) {
//			EntitiesComponents[i].EntityComponent->MarkRenderStateDirty();
//		}
//	}
//}

const FMeshEntity& FMobileGPUDrivenSystem::GetMeshEntityByUniqueId(uint32 UniqueObjectIndex) const {
	const uint32 EntityIndex_RenderThread = UniqueIdToEntityIndex_RenderThread.FindChecked(UniqueObjectIndex);
	return Entities[EntityIndex_RenderThread];
}
