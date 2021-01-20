#include "MobileGPUDrivenRendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "MobileGPUDriven.h"
#include "MobileHZB.h"

constexpr uint32 ThreadCount = 64;

class FGpuDrivenClearCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGpuDrivenClearCS)

public:
	FGpuDrivenClearCS() {};

	FGpuDrivenClearCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ClearBuffer.Bind(Initializer.ParameterMap, TEXT("ClearUAV"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Paramers)
	{
		return true;
	}


	void BindParameters(FRHICommandList& RHICmdList, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClearBuffer, GpuDrivenSystem->EntityLodBufferCount_GPU.UAV);
	}

	void UnBindParameters(FRHICommandList& RHICmdList, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClearBuffer, nullptr);
	}

public:
	LAYOUT_FIELD(FShaderResourceParameter, ClearBuffer);
};

class FMobileGpuCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMobileGpuCullingCS);

public:

	FMobileGpuCullingCS() : FGlobalShader() {}

	FMobileGpuCullingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer) {
		TotalClusterCount.Bind(Initializer.ParameterMap, TEXT("TotalClusterCount_Pass_0"));
		ViewOriginPosition.Bind(Initializer.ParameterMap, TEXT("ViewOriginPosition"));
		ViewFrustumPermutedPlanes.Bind(Initializer.ParameterMap, TEXT("ViewFrustumPermutedPlanes"));
		ProjMatrixXY.Bind(Initializer.ParameterMap, TEXT("ProjMatrixXY"));
		PreViewProjectMatrix.Bind(Initializer.ParameterMap, TEXT("LastFrameViewProjectMatrix"));
		HzbResource_SRV.Bind(Initializer.ParameterMap, TEXT("HzbResource"));
		ClusterData_SRV.Bind(Initializer.ParameterMap, TEXT("InputClusterBufferSRV"));
		EntityLodBuffer_SRV.Bind(Initializer.ParameterMap, TEXT("LodDataBuffre"));
		OutputClusterBuffer_UAV.Bind(Initializer.ParameterMap, TEXT("OutputClusterBufferUAV"));
		EntityLodCountBuffer_UAV.Bind(Initializer.ParameterMap, TEXT("EntityLodCountBufferUAV"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return true;
	}

	void BindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FMobileGPUDrivenSystem* GpuDrivenSystem, FRHITexture* InHzbResource) {
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), TotalClusterCount, GpuDrivenSystem->CurTotalClusterCount);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), ViewOriginPosition, View.ViewMatrices.GetViewOrigin());
		SetShaderValueArray(RHICmdList, RHICmdList.GetBoundComputeShader(), ViewFrustumPermutedPlanes, View.ViewFrustum.PermutedPlanes.GetData(), View.ViewFrustum.PermutedPlanes.Num());//#TODO: 去掉远近平面? 

		{
			const auto& ProjMatrix = View.ViewMatrices.GetProjectionMatrix();
			FVector2D ProjMatrixValue = FVector2D(ProjMatrix.M[0][0], ProjMatrix.M[1][1]);
			SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), ProjMatrixXY, ProjMatrixValue);
		}

		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), PreViewProjectMatrix, View.PrevViewInfo.ViewMatrices.GetViewProjectionMatrix()); //Last Frame

		SetTextureParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), HzbResource_SRV, InHzbResource);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), EntityLodBuffer_SRV, GpuDrivenSystem->EntityLodScreenBuffer_GPU.SRV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterData_SRV, GpuDrivenSystem->ClusterInputData_GPU.SRV);

		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), OutputClusterBuffer_UAV, GpuDrivenSystem->ClusterOutputData_GPU.UAV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), EntityLodCountBuffer_UAV, GpuDrivenSystem->EntityLodBufferCount_GPU.UAV);

	}

	void UnBindParameters(FRHICommandList& RHICmdList, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), OutputClusterBuffer_UAV, nullptr);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), EntityLodCountBuffer_UAV, nullptr);
	}

private:
	//#TODO: Batch?
	LAYOUT_FIELD(FShaderParameter, TotalClusterCount);
	LAYOUT_FIELD(FShaderParameter, ViewOriginPosition);
	LAYOUT_FIELD(FShaderParameter, ViewFrustumPermutedPlanes);
	LAYOUT_FIELD(FShaderParameter, ProjMatrixXY);
	LAYOUT_FIELD(FShaderParameter, PreViewProjectMatrix);
	LAYOUT_FIELD(FShaderResourceParameter, HzbResource_SRV)
	LAYOUT_FIELD(FShaderResourceParameter, ClusterData_SRV);
	LAYOUT_FIELD(FShaderResourceParameter, EntityLodBuffer_SRV);
	LAYOUT_FIELD(FShaderResourceParameter, OutputClusterBuffer_UAV);
	LAYOUT_FIELD(FShaderResourceParameter, EntityLodCountBuffer_UAV);
	
};

class FMobileInstanceIndexBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMobileInstanceIndexBufferCS);

public:

	FMobileInstanceIndexBufferCS() : FGlobalShader() {}

	FMobileInstanceIndexBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer) {
		TotalClusterCount.Bind(Initializer.ParameterMap, TEXT("TotalClusterCount_Pass_1"));
		OutputClusterBuffer_SRV.Bind(Initializer.ParameterMap, TEXT("OutputClusterBufferSRV"));
		EntityLodCountBuffer_SRV.Bind(Initializer.ParameterMap, TEXT("LodCountBufferSRV_0"));
		InstanceToRenderIndexBuffer_UAV.Bind(Initializer.ParameterMap, TEXT("InstanceToRenderIndexBufferUAV"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return true;
	}

	void BindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), TotalClusterCount, GpuDrivenSystem->CurTotalClusterCount);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), OutputClusterBuffer_SRV, GpuDrivenSystem->ClusterOutputData_GPU.SRV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), EntityLodCountBuffer_SRV, GpuDrivenSystem->EntityLodBufferCount_GPU.SRV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), InstanceToRenderIndexBuffer_UAV, GpuDrivenSystem->InstanceToRenderIndexBuffer_GPU.UAV);
	}

	void UnBindParameters(FRHICommandList& RHICmdList, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), InstanceToRenderIndexBuffer_UAV, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, TotalClusterCount);
	LAYOUT_FIELD(FShaderResourceParameter, OutputClusterBuffer_SRV);
	LAYOUT_FIELD(FShaderResourceParameter, EntityLodCountBuffer_SRV);
	LAYOUT_FIELD(FShaderResourceParameter, InstanceToRenderIndexBuffer_UAV);

};

class FMobileUpdateDrawBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMobileUpdateDrawBufferCS);

public:

	FMobileUpdateDrawBufferCS() : FGlobalShader() {}

	FMobileUpdateDrawBufferCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer) {
		TotalClusterCount.Bind(Initializer.ParameterMap, TEXT("TotalDrawCount_Pass_2"));
		IndirectDrawToLodIndexBuffer_SRV.Bind(Initializer.ParameterMap, TEXT("IndirectDrawToLodIndexBufferSRV"));
		EntityLodCountBuffer_SRV.Bind(Initializer.ParameterMap, TEXT("LodCountBufferSRV_1"));
		IndirectDrawCommandBuffer_UAV.Bind(Initializer.ParameterMap, TEXT("IndirectDrawCommandBufferUAV"));
		FirstInstanceIndexBuffer_UAV.Bind(Initializer.ParameterMap, TEXT("FirstInstanceIndexBufferUAV"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return true;
	}

	void BindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), TotalClusterCount, GpuDrivenSystem->CurTotalIndirectDrawCount);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), IndirectDrawToLodIndexBuffer_SRV, GpuDrivenSystem->IndirectDrawToLodIndexBuffer_GPU.SRV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), EntityLodCountBuffer_SRV, GpuDrivenSystem->EntityLodBufferCount_GPU.SRV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), IndirectDrawCommandBuffer_UAV, GpuDrivenSystem->IndirectDrawCommandBuffer_GPU.UAV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), FirstInstanceIndexBuffer_UAV, GpuDrivenSystem->IndirectDrawFirstInstanceIndex_GPU.UAV);
	}

	void UnBindParameters(FRHICommandList& RHICmdList, FMobileGPUDrivenSystem* GpuDrivenSystem) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), IndirectDrawCommandBuffer_UAV, nullptr);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), FirstInstanceIndexBuffer_UAV, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, TotalClusterCount);
	LAYOUT_FIELD(FShaderResourceParameter, IndirectDrawToLodIndexBuffer_SRV);
	LAYOUT_FIELD(FShaderResourceParameter, EntityLodCountBuffer_SRV);
	LAYOUT_FIELD(FShaderResourceParameter, IndirectDrawCommandBuffer_UAV);
	LAYOUT_FIELD(FShaderResourceParameter, FirstInstanceIndexBuffer_UAV);
};

IMPLEMENT_SHADER_TYPE(, FGpuDrivenClearCS, TEXT("/Engine/Private/MobileGpuDriven.usf"), TEXT("ClearComputeFieldCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FMobileGpuCullingCS, TEXT("/Engine/Private/MobileGpuDriven.usf"), TEXT("MobileGpuCulling"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FMobileInstanceIndexBufferCS, TEXT("/Engine/Private/MobileGpuDriven.usf"), TEXT("MobileUpdateInstanceIndexBuffer"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FMobileUpdateDrawBufferCS, TEXT("/Engine/Private/MobileGpuDriven.usf"), TEXT("MobileIndirectDrawUpdate"), SF_Compute)

void FMobileSceneRenderer::MobileGPUCulling(FRHICommandListImmediate& RHICmdList) {

	SCOPED_DRAW_EVENT(RHICmdList, MobileGPUDrivenCulling);

	FMobileGPUDrivenSystem* GpuDrivenSystem = FMobileGPUDrivenSystem::GetGPUDrivenSystem_RenderThreadByWorldId(Scene->GetWorld()->GetUniqueID());
	if (GpuDrivenSystem) {

		//Clear Pass
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, GpuDrivenSystem->EntityLodBufferCount_GPU.UAV);
			const uint32 ThreadGroups = FMath::DivideAndRoundUp(GpuDrivenSystem->CurTotalLodCount, ThreadCount);
			TShaderMapRef<FGpuDrivenClearCS> MobileClearShader(GetGlobalShaderMap(FeatureLevel));
			RHICmdList.SetComputeShader(MobileClearShader.GetComputeShader());
			MobileClearShader->BindParameters(RHICmdList, GpuDrivenSystem);
			RHICmdList.DispatchComputeShader(ThreadGroups, 1, 1);
			MobileClearShader->UnBindParameters(RHICmdList, GpuDrivenSystem);
		}

		//Culling Pass
		{
			FTextureRHIRef MobileHZBTexture = FMobileHzbSystem::MobileHzbResourcesPtr->MobileHZBTexture->GetRenderTargetItem().ShaderResourceTexture;

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToCompute, MobileHZBTexture);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, GpuDrivenSystem->ClusterOutputData_GPU.UAV); // gfx queue to compute queue
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, GpuDrivenSystem->EntityLodBufferCount_GPU.UAV); //Only used for compute queue

			const uint32 ThreadGroups = FMath::DivideAndRoundUp(GpuDrivenSystem->CurTotalClusterCount, ThreadCount);
			TShaderMapRef<FMobileGpuCullingCS> MobileGpuCullingShader(GetGlobalShaderMap(FeatureLevel));
			RHICmdList.SetComputeShader(MobileGpuCullingShader.GetComputeShader());
			MobileGpuCullingShader->BindParameters(RHICmdList, Views[0], GpuDrivenSystem, MobileHZBTexture);
			RHICmdList.DispatchComputeShader(ThreadGroups, 1, 1);
			MobileGpuCullingShader->UnBindParameters(RHICmdList, GpuDrivenSystem);
		}

		//ReMapInstanceBuffer Pass
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GpuDrivenSystem->ClusterOutputData_GPU.UAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GpuDrivenSystem->EntityLodBufferCount_GPU.UAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, GpuDrivenSystem->InstanceToRenderIndexBuffer_GPU.UAV);

			const uint32 ThreadGroups = FMath::DivideAndRoundUp(GpuDrivenSystem->CurTotalClusterCount, ThreadCount);
			TShaderMapRef<FMobileInstanceIndexBufferCS> MobileMapInstanceShader(GetGlobalShaderMap(FeatureLevel));
			RHICmdList.SetComputeShader(MobileMapInstanceShader.GetComputeShader());
			MobileMapInstanceShader->BindParameters(RHICmdList, Views[0], GpuDrivenSystem);
			RHICmdList.DispatchComputeShader(ThreadGroups, 1, 1);
			MobileMapInstanceShader->UnBindParameters(RHICmdList, GpuDrivenSystem);
		}

		//Update IndirectDraw Buffer
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GpuDrivenSystem->EntityLodBufferCount_GPU.UAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, GpuDrivenSystem->IndirectDrawCommandBuffer_GPU.UAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, GpuDrivenSystem->IndirectDrawFirstInstanceIndex_GPU.UAV);

			const uint32 ThreadGroups = FMath::DivideAndRoundUp(GpuDrivenSystem->CurTotalIndirectDrawCount, ThreadCount);
			TShaderMapRef<FMobileUpdateDrawBufferCS> MobileUpdateDrawShader(GetGlobalShaderMap(FeatureLevel));
			RHICmdList.SetComputeShader(MobileUpdateDrawShader.GetComputeShader());
			MobileUpdateDrawShader->BindParameters(RHICmdList, Views[0], GpuDrivenSystem);
			RHICmdList.DispatchComputeShader(ThreadGroups, 1, 1);
			MobileUpdateDrawShader->UnBindParameters(RHICmdList, GpuDrivenSystem);
		}

		//transitionResource for gfx
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, GpuDrivenSystem->IndirectDrawCommandBuffer_GPU.UAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, GpuDrivenSystem->IndirectDrawFirstInstanceIndex_GPU.UAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, GpuDrivenSystem->InstanceToRenderIndexBuffer_GPU.UAV);
		}
	}

}

bool bUseMobileGpuDriven() {
	return CVarMobileEnableGPUDriven.GetValueOnAnyThread() != 0;
}