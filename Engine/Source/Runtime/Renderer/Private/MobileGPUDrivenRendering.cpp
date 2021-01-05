#include "MobileGPUDrivenRendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "MobileGPUDriven.h"


class MobileGpuCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(MobileGpuCullingCS);

public:
	static constexpr uint32 ThreadCount = 64;

	MobileGpuCullingCS() : FGlobalShader() {}

	MobileGpuCullingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ClusterData_SRV.Bind(Initializer.ParameterMap, TEXT("ClusterDataBuffer"));
		SparseEntityBuffer_UAV.Bind(Initializer.ParameterMap, TEXT("SparseEntityBuffer"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	void BindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterData_SRV, GpuDrivenSystem->ClusterData_GPU.SRV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), SparseEntityBuffer_UAV, /*GpuDrivenSystem->*/)
	}

	void UnBindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetUAVParameter(SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), SparseEntityBuffer_UAV, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ClusterData_SRV);
	LAYOUT_FIELD(FShaderResourceParameter, SparseEntityBuffer_UAV);
};

IMPLEMENT_SHADER_TYPE(, MobileGpuCullingCS, TEXT("/Plugins/Shaders/Private/MobileGpuDriven.usf"), TEXT("MobileGpuCulling"), SF_Compute)


void FMobileSceneRenderer::MobileGPUCulling(FRHICommandListImmediate& RHICmdList) {

	if (CVarMobileEnableGPUDriven.GetValueOnAnyThread() != 0) {
		SCOPED_DRAW_EVENT(RHICmdList, MobileGPUDrivenCulling);

		FMobileGPUDrivenSystem* GpuDrivenSystem = FMobileGPUDrivenSystem::GetGPUDrivenSystem_RenderThreadOrTask(Scene->GetWorld()->GetUniqueID());
		ensure(GpuDrivenSystem != nullptr);
		if (GpuDrivenSystem) {

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GpuDrivenSystem->ClusterData_GPU);

			const uint32 ThreadGroups = FMath::DivideAndRoundUp(GpuDrivenSystem->ClusterCount, MobileGpuCullingCS::ThreadCount);
			TShaderMapRef<MobileGpuCullingCS> MobileGpuCullingShader(GetGlobalShaderMap(FeatureLevel));
			RHICmdList.SetComputeShader(MobileGpuCullingShader.GetComputeShader());
			MobileGpuCullingShader->BindParameters(RHICmdList, Views[0], GpuDrivenSystem);
			RHICmdList.DispatchComputeShader(ThreadGroups, 1, 1);
			MobileGpuCullingShader->UnBindParameters(RHICmdList, Views[0], GpuDrivenSystem);
		}
	}
}
