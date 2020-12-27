#include "CoreMinimal.h"
#include "RHIUtilities.h"

class FInstancedStaticMeshSceneProxy;
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
	constexpr SIZE_T MaxStaticMeshLods = 0x8;
};

struct FMeshEntity {
	FMeshEntity(FInstancedStaticMeshSceneProxy* InInstanceSceneProxy);
	~FMeshEntity() {}

	UStaticMesh* GetStaticMesh() const;
	FBoxSphereBounds GetClusterBounds(int32 ClusterRenderIndex) const;
	FBoxSphereBounds GetClusterBounds(int32 ClusterRenderIndex, const FBoxSphereBounds& MeshBounds) const; //just used for test

	uint32 NumLod;
	uint32 NumRenderCluster;
	uint32 NumDrawElement;
	//uint32 
	FInstancedStaticMeshSceneProxy* InstanceSceneProxy;

	TArray<uint32> NumSectionPerLod; //Mesh每级Lod的Section数量
	TArray<uint32> SectionIndexCount;
	TArray<uint32> SectionFirstIndex;
	TArray<float> ScreenLODs;
	/*TArray<FClusterNode> ClusterNodes*/ //Cluster结构
};

struct FMobileGPUDrivenSystem {
	FMobileGPUDrivenSystem();
	~FMobileGPUDrivenSystem();
	void UpdateGlobalGPUBuffer();

	static TMap<UWorld*, FMobileGPUDrivenSystem*> GlobalGPUDrivenSystemMap;
	static void RegisterEntity(UWorld* World, FInstancedStaticMeshSceneProxy* InstanceSceneProxy);
	static void UnRegisterEntity(UWorld* World, FInstancedStaticMeshSceneProxy* InstanceSceneProxy);
	static int32 GetIndirectDrawStartIndex(UWorld* World, int32 EntityIndex);
	static bool IsGPUDrivenWorld(UWorld* World);
	
	//[Resources AutoRelease]
	bool bGPUDataDirty;
	TArray<FMeshEntity> Entities;

	//[Resources Manager]
	FRWBuffer IndirectDrawCommandBuffer_GPU;
	FRWBufferStructured ClusterMappingAndBoundBuffer_GPU;
	FRWBufferStructured LodBuffer_GPU;
};
