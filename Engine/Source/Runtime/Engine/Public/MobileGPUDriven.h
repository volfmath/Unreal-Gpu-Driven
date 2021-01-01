#include "CoreMinimal.h"
#include "RHIUtilities.h"

class FInstancedStaticMeshSceneProxy;
class UInstancedStaticMeshComponent;
class UStaticMesh;

struct FDrawIndirectCommandArgs_CPU {
	uint32    IndexCount;
	uint32    InstanceCount;
	uint32    FirstIndex;
	int32     VertexOffset;
	uint32    FirstInstance;
};

//每个Cluster数据,只是当前Cluster是一个Instance
struct FClusterMappingAndBound_CPU {
	uint32 FirstRenderIndex;
	uint32 LodBufferStartIndex;
	FVector BoundCenter;
	FVector BoundExtent;
};

struct FLodBuffer_CPU {
	float CurLodScreenSize;
};

namespace SLGPUDrivenParameter {
	constexpr SIZE_T IndirectCommandSize = sizeof(FDrawIndirectCommandArgs_CPU);
	constexpr SIZE_T IndirectBufferElementSize = 0x5;
};

struct FMeshEntity {
	FMeshEntity(
		uint32 NumLod,
		uint32 NumDrawElement,
		uint32 NumRenderCluster,
		uint32 UniqueObjectId,
		uint32 UniqueWorldId,
		FBoxSphereBounds MeshBound,
		TArray<uint32>&& SectionIndexCount,
		TArray<uint32>&& SectionFirstIndex,
		TArray<float>&& ScreenLODs,
		TSharedPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> InPerInstanceRenderData
	);
	FMeshEntity(const FMeshEntity& CopyFMeshEntity);
	FMeshEntity(FMeshEntity&& CopyMeshEntity);

	static FMeshEntity CreateMeshEntity(UInstancedStaticMeshComponent* InstanceComponent, TSharedPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> InPerInstanceRenderData);

	~FMeshEntity() {}

	FBoxSphereBounds GetClusterBounds(int32 ClusterRenderIndex) const;
	FBoxSphereBounds GetClusterBounds(int32 ClusterRenderIndex, const FBoxSphereBounds& MeshBounds) const; //just used for test

	uint32 NumLod;
	uint32 NumDrawElement;
	uint32 NumRenderCluster;
	uint32 UniqueObjectId;
	uint32 UniqueWorldId; //用于记录注册时的WorldId

	FBoxSphereBounds MeshBound;
	TArray<uint32> SectionIndexCount;
	TArray<uint32> SectionFirstIndex;
	TArray<float> ScreenLODs;
	TWeakPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> PerInstanceRenderData;
	/*TArray<FClusterNode> ClusterNodes*/ //Cluster结构
};

struct FMeshEntityGameThread {
	FMeshEntityGameThread(uint32 InUniqueObjectId, UInstancedStaticMeshComponent* InEntityComponent);
	~FMeshEntityGameThread() {}

	uint32 UniqueObjectId;
	TWeakObjectPtr<UInstancedStaticMeshComponent> EntityComponent;
};

struct IndirectDrawArgsAndStartIndex {
	uint32 IndirectArgsStartIndex;
	FVertexBufferRHIRef IndirectArgsBuffer;
};

struct FMobileGPUDrivenSystem {
	FMobileGPUDrivenSystem();
	~FMobileGPUDrivenSystem();
	void UpdateGlobalGPUBuffer();

	//[Public Call Function]
	void GetIndirectDrawArgsAndStartIndex(uint32 EntityIndex, IndirectDrawArgsAndStartIndex& IndirectBuffer) const;
	static void RegisterEntity(UInstancedStaticMeshComponent* InstanceComponent);
	static void UnRegisterEntity(uint32 UniqueObjectIndex);
	static void RegisterEntity_RenderThread(FMeshEntity&& MeshEntity, FMobileGPUDrivenSystem* SceneSystemPtr);
	static void UnRegisterEntity_RenderThread(uint32 UniqueObjectIndex);
	static bool IsGPUDrivenWorld(UWorld* World);
	static FMobileGPUDrivenSystem* GetGPUDrivenSystem_GameThread(uint32 UniqueObjectIndex);
	static FMobileGPUDrivenSystem* GetGPUDrivenSystem_RenderThreadOrTask(uint32 UniqueObjectIndex);

	//[Inner Call Function]
	void MarkAllComponentsDirty();
	void UpdateIndirectDrawCommandBuffer();

	//[Thread Shared]
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalWorldIndexToSystemMap;

	//[RenderThread Only]
	TArray<FMeshEntity> Entities;
	TMap<uint32, uint32> ComponentToIndexMap_RenderThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalUniqueIdToSystemMap_RenderThread;
	FRWBuffer IndirectDrawCommandBuffer_GPU;
	FRWBufferStructured ClusterMappingAndBoundBuffer_GPU;
	FRWBufferStructured LodBuffer_GPU;

	//[GameThread Only]
	TArray<FMeshEntityGameThread> EntitiesComponents; 
	TMap<uint32, uint32> ComponentToIndexMap_GameThread; 
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalUniqueIdToSystemMap_GameThread; 
};
