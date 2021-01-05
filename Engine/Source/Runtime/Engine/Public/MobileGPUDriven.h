#include "CoreMinimal.h"
#include "RHIUtilities.h"

class FInstancedStaticMeshSceneProxy;
class UInstancedStaticMeshComponent;
class UStaticMesh;

extern ENGINE_API TAutoConsoleVariable<int32> CVarMobileEnableGPUDriven;

/**----------------------Gpu Struct Layout----------------------*/
struct FDrawIndirectCommandArgs_CPU {
	uint32    IndexCount;
	uint32    InstanceCount;
	uint32    FirstIndex;
	int32     VertexOffset;
	uint32    FirstInstance;
};

//ÿ��Cluster����,ֻ�ǵ�ǰCluster��һ��Instance
struct FClusterData_CPU {
	uint32 FirstRenderIndex;
	uint32 LodBufferStartIndex;
	uint32 InstanceCount;
	FVector BoundCenter;
	FVector BoundExtent;
};

struct FEntityLodBuffer_CPU {
	float CurLodScreenSize;
};

struct FSparseClusterBuffer_CPU {
	uint32 FirstRenderIndex;
};

struct FEntityLodCountBuffer_CPU {
	uint32 CurLodRenderCount;
};

namespace SLGPUDrivenParameter {
	constexpr SIZE_T IndirectCommandSize = sizeof(FDrawIndirectCommandArgs_CPU);
	constexpr SIZE_T IndirectBufferElementSize = 0x5;
};


/**���ڴ洢UserData�Ľṹ, Ϊ�˱���ÿ֡�ظ�д�뽫�����FMeshEntity��*/
struct FGpuDrivenInstancingUserData
{
	FGpuDrivenInstancingUserData() = default;
	~FGpuDrivenInstancingUserData() = default;

	FGpuDrivenInstancingUserData(UInstancedStaticMeshComponent* InstanceComponent)
		: bRenderSelected(true)
		, bRenderUnselected(true)
		, StartCullDistance(InstanceComponent->InstanceStartCullDistance)
		, EndCullDistance(InstanceComponent->InstanceEndCullDistance)
	{

	}

	bool bRenderSelected;
	bool bRenderUnselected;

	int32 StartCullDistance;
	int32 EndCullDistance;

	uint32 SparseClusterBufferIndex;
	uint32 ClusterInstanceCount;
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
		TSharedPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> InPerInstanceRenderData,
		const FGpuDrivenInstancingUserData& UserData
	);
	FMeshEntity(const FMeshEntity& CopyFMeshEntity);
	FMeshEntity(FMeshEntity&& CopyMeshEntity);

	static FMeshEntity CreateMeshEntity(UInstancedStaticMeshComponent* InstanceComponent);

	~FMeshEntity() {}

	FBoxSphereBounds GetClusterBounds(int32 ClusterRenderIndex) const;
	FBoxSphereBounds GetClusterBounds(int32 ClusterRenderIndex, const FBoxSphereBounds& MeshBounds) const; //just used for test

	uint32 NumLod;
	uint32 NumDrawElement;
	uint32 NumRenderCluster; //#TODO��ʹ��TArray
	uint32 UniqueObjectId;
	uint32 UniqueWorldId; //���ڼ�¼ע��ʱ��WorldId

	FBoxSphereBounds MeshBound;
	TArray<uint32> SectionIndexCount;
	TArray<uint32> SectionFirstIndex;
	TArray<float> ScreenLODs;
	TWeakPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> PerInstanceRenderData;
	//TArray<FInstanceClusterNode> ClusterNodes; //Cluster�ṹ

	//[Global Cache Data]
	uint32 IndirectArgsStartIndex;
	FGpuDrivenInstancingUserData GpuDriven_UserData;
};

struct FMeshEntityGameThread {
	FMeshEntityGameThread(uint32 InUniqueObjectId, UInstancedStaticMeshComponent* InEntityComponent);
	~FMeshEntityGameThread() {}

	uint32 UniqueObjectId;
	TWeakObjectPtr<UInstancedStaticMeshComponent> EntityComponent;
};

struct FMobileGPUDrivenSystem {
	FMobileGPUDrivenSystem();
	~FMobileGPUDrivenSystem();

	//[export function]
	ENGINE_API static FMobileGPUDrivenSystem* GetGPUDrivenSystem_RenderThreadOrTask(uint32 UniqueObjectIndex);


	//[Public Call Function]
	//#TODO: Update Function
	//void UpdateEntity()
	//void UpdateEntity_RenderThread()
	const FMeshEntity& GetMeshEntityByUniqueId(uint32 UniqueObjectIndex) const;
	static void RegisterEntity(UInstancedStaticMeshComponent* InstanceComponent);
	static void UnRegisterEntity(uint32 UniqueObjectIndex);
	static void RegisterEntity_RenderThread(FMeshEntity&& MeshEntity, FMobileGPUDrivenSystem* SceneSystemPtr);
	static void UnRegisterEntity_RenderThread(uint32 UniqueObjectIndex);
	static bool IsGPUDrivenWorld(UWorld* World);
	static FMobileGPUDrivenSystem* GetGPUDrivenSystem_GameThread(uint32 UniqueObjectIndex);
	

	//[Inner Call Function]
	void MarkAllComponentsDirty(); //#TODO: Remove
	void UpdateAllGPUBuffer();

	//[Thread Shared]
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalWorldIndexToSystemMap;

	//[RenderThread Only]
	uint32 ClusterCount;
	TArray<FMeshEntity> Entities;
	TMap<uint32, uint32> UniqueIdToEntityIndex_RenderThread;
	FRWBuffer IndirectDrawCommandBuffer_GPU;
	FRWBufferStructured ClusterData_GPU;
	FRWBufferStructured LodBuffer_GPU;
	FRWBufferStructured SparseClusterBuffer_GPU;
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalUniqueIdToSystemMap_RenderThread;

	//#TODO: Remove EntitiesComponents
	//[GameThread Only]
	TArray<FMeshEntityGameThread> EntitiesComponents; 
	TMap<uint32, uint32> UniqueIdToEntityIndex_GameThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalUniqueIdToSystemMap_GameThread; 
};
