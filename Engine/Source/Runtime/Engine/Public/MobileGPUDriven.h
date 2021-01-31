#include "CoreMinimal.h"
#include "RHIUtilities.h"

class FInstancedStaticMeshSceneProxy;
class UInstancedStaticMeshComponent;
class UStaticMesh;


extern ENGINE_API TAutoConsoleVariable<int32> CVarMobileEnableGPUDriven;
extern ENGINE_API TAutoConsoleVariable<int32> CVarGpuDrivenRenderState;
extern ENGINE_API TAutoConsoleVariable<int32> CVarMobileCS;
extern ENGINE_API TAutoConsoleVariable<int32> CVarGpuDrivenMaualFetchTest;
extern ENGINE_API TAutoConsoleVariable<int32> CVarIndirectDrawTest;
DECLARE_LOG_CATEGORY_EXTERN(MobileGpuDriven, Warning, All);

/**----------------------Gpu Struct Layout----------------------*/
struct FDrawIndirectCommandArgs_CPU {
	uint32    IndexCount;
	uint32    InstanceCount;
	uint32    FirstIndex;
	int32     VertexOffset;
	uint32    FirstInstance;
};

//��Ϊÿ��������Thread�ж���ʹ��,��������ν��16�ֽڶ���
struct FClusterInputData_CPU {
	uint32 FirstRenderIndex;
	uint32 LodBufferStartIndex;
	uint32 ClusterInstanceCountAndLodCount;
	uint32 InstanceBufferStartIndex;

	FVector BoundCenter; //Because the Vec3 start address of gles must be aligned to 16 bytes
	float CullDistance; //Aligned to 16 bytes

	FVector BoundExtent;
	float ScaledBoundSphereRadius;
};

struct FClusterOutputData_CPU {
	uint32 FirstRenderIndex;
	uint32 LodBufferStartIndex;
	uint32 ClusterInstanceCountAndLodIndex; //ѹ����16�ֽ�
	uint32 InstanceBufferStartIndexAddLodCount; //ѹ����16�ֽ�
};

struct FDrawLodParameter_CPU {
	uint16 LodBufferIndex;
	uint16 LodLevel;
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
		, InstanceToRenderStartIndex(0xFFFFFFFF)
		, FirstInstanceIndexBufferSRV(nullptr)
		, InstanceToRenderIndexBufferSRV(nullptr)
	{

	}

	bool bRenderSelected;
	bool bRenderUnselected;
	int32 StartCullDistance;
	int32 EndCullDistance;
	uint32 InstanceToRenderStartIndex;
	FRHIShaderResourceView* FirstInstanceIndexBufferSRV;
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
	uint32 UniqueWorldId; //���ڼ�¼ע��ʱ��WorldId
	float CullDistance;
	TArray<uint32> SectionIndexCount;
	TArray<uint32> SectionFirstIndex;
	TArray<uint32> PerLodSectionCount;
	TArray<float> ScreenLODs;
	TSharedPtr<TArray<FGpuDrivenCluster>, ESPMode::ThreadSafe> GpuDrivenCluster;
	uint32 IndirectDrawStartIndex;
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
	ENGINE_API void UpdateAllGPUBuffer();
	void MarkDirty();

	//[GameThread Only]
	uint32 WorldEntityCount_GameThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> WorldIndexToSystemMap_GameThread;
	static TMap<uint32, FMobileGPUDrivenSystem*> GlobalUniqueIdToSystemMap_GameThread;

	//[RenderThread Only]
	bool bSystemDirty;
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
	FRWBuffer IndirectDrawFirstInstanceIndex_GPU;
	FRWBufferStructured ClusterOutputData_GPU;

	FRWBuffer InstanceToRenderIndexBuffer_GPU;
	//FRWBufferStructured InstanceToRenderIndexBuffer_GPU;

};
