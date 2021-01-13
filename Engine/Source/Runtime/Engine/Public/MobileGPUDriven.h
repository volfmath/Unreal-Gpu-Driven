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

//因为每个数据在Thread中都会使用,所以无所谓做16字节对齐
struct FClusterInputData_CPU {
	uint32 FirstRenderIndex;
	uint32 LodBufferStartIndex;
	uint32 ClusterInstanceCount;
	uint32 InstanceBufferStartIndex;

	float CullDistance; //注意对齐16字节
	FVector BoundCenter;

	uint32 MeshLodCount;
	FVector BoundExtent;
};

struct FClusterOutputData_CPU {
	uint32 FirstRenderIndex;
	uint32 LodBufferStartIndex;
	uint32 ClusterInstanceCountAndLodIndex; //压缩到16字节
	uint32 InstanceBufferStartIndexAddLodCount; //压缩到16字节
};

namespace SLGPUDrivenParameter {
	constexpr SIZE_T IndirectCommandSize = sizeof(FDrawIndirectCommandArgs_CPU);
	constexpr SIZE_T IndirectBufferElementSize = 0x5;
};


/**用于存储UserData的结构, 为了避免每帧重复写入将其放入FMeshEntity中*/
struct FGpuDrivenInstancingUserData
{
	FGpuDrivenInstancingUserData() = default;
	~FGpuDrivenInstancingUserData() = default;

	FGpuDrivenInstancingUserData(UInstancedStaticMeshComponent* InstanceComponent)
		: bRenderSelected(true)
		, bRenderUnselected(true)
		, bIsShadow(true)
		, StartCullDistance(InstanceComponent->InstanceStartCullDistance)
		, EndCullDistance(InstanceComponent->InstanceEndCullDistance)
		, InstanceToRenderStartIndex(0xFFFFFFFF)
		, InstanceToRenderIndexBufferSRV(nullptr)
	{

	}

	bool bRenderSelected;
	bool bRenderUnselected;
	bool bIsShadow;

	int32 StartCullDistance;
	int32 EndCullDistance;
	uint32 InstanceToRenderStartIndex;
	FRHIShaderResourceView* InstanceToRenderIndexBufferSRV;
};

struct FMeshEntity {
	FMeshEntity(
		uint32 NumLod,
		uint32 NumDrawElement,
		uint32 UniqueObjectId,
		uint32 UniqueWorldId,
		float CullDistance,
		TArray<uint32>&& SectionIndexCount,
		TArray<uint32>&& SectionFirstIndex,
		TArray<uint32>&& PerLodSectionCount,
		TArray<float>&& ScreenLODs,
		TSharedPtr<TArray<FGpuDrivenCluster>, ESPMode::ThreadSafe> GpuDrivenCluster,
		const FMatrix& ComponentLocalToWorld,
		const FGpuDrivenInstancingUserData& UserData
	);
	FMeshEntity(const FMeshEntity& CopyFMeshEntity);
	FMeshEntity(FMeshEntity&& CopyMeshEntity);
	~FMeshEntity() {}

	static FMeshEntity CreateMeshEntity(UInstancedStaticMeshComponent* InstanceComponent);
	
	uint32 NumLod;
	uint32 NumDrawElement;
	uint32 UniqueObjectId;
	uint32 UniqueWorldId; //用于记录注册时的WorldId
	float CullDistance;
	TArray<uint32> SectionIndexCount;
	TArray<uint32> SectionFirstIndex;
	TArray<uint32> PerLodSectionCount;
	TArray<float> ScreenLODs;
	TSharedPtr<TArray<FGpuDrivenCluster>, ESPMode::ThreadSafe> GpuDrivenCluster;
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
	ENGINE_API static FMobileGPUDrivenSystem* GetGPUDrivenSystem_RenderThreadByWorldId(uint32 UniqueWorldIndex);

	//[Public Call Function]
	//#TODO: Update Function
	//void UpdateEntity()
	//void UpdateEntity_RenderThread()
	const FMeshEntity& GetMeshEntityByUniqueId(uint32 UniqueObjectIndex) const;
	static void RegisterEntity(UInstancedStaticMeshComponent* InstanceComponent);
	static void UnRegisterEntity(uint32 UniqueObjectIndex, uint32 UniqueWorldId);
	static void RegisterEntity_RenderThread(FMeshEntity&& MeshEntity, FMobileGPUDrivenSystem* SceneSystemPtr);
	static void UnRegisterEntity_RenderThread(uint32 UniqueObjectIndex);
	static bool IsGPUDrivenWorld(UWorld* World);
	static FMobileGPUDrivenSystem* GetGPUDrivenSystem_GameThread(uint32 UniqueObjectIndex);
	static FMobileGPUDrivenSystem* GetGPUDrivenSystem_RenderThreadOrTask(uint32 UniqueObjectIndex);

	//[Inner Call Function]
	void UpdateAllGPUBuffer();

	//[GameThread Only]
	//TMap<uint32, uint32> UniqueIdToEntityIndex_GameThread;
	uint32 WorldEntityCount_GameThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> WorldIndexToSystemMap_GameThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalUniqueIdToSystemMap_GameThread;

	//[RenderThread Only]
	uint32 CurTotalClusterCount;
	uint32 CurTotalLodCount;
	uint32 CurTotalIndirectDrawCount;
	TArray<FMeshEntity> Entities;
	TMap<uint32, uint32> UniqueIdToEntityIndex_RenderThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> WorldIndexToSystemMap_RenderThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalUniqueIdToSystemMap_RenderThread;

	//[Read Resources]
	FReadBuffer EntityLodScreenBuffer_GPU;
	FReadBuffer IndirectDrawToLodIndexBuffer_GPU;
	FRWBufferStructured ClusterInputData_GPU; //#TODO: Remove UAV
	
	//[Output Resources]
	FRWBuffer IndirectDrawCommandBuffer_GPU;
	FRWBuffer EntityLodBufferCount_GPU;
	FRWBufferStructured ClusterOutputData_GPU;
	FRWBufferStructured InstanceToRenderIndexBuffer_GPU;
	


};
