#pragma once
#include "CoreMinimal.h"
#include "RenderGraph.h"

class FViewInfo;
class FRDGBuilder; 

struct FMobileHzbResource : public FRenderResource {
	FMobileHzbResource() { }
	~FMobileHzbResource() { };

	virtual void InitDynamicRHI() override; 
	virtual void ReleaseDynamicRHI() override;
	
	FRWBuffer MobileHzbBuffer;
	TRefCountPtr<IPooledRenderTarget> MobileHZBTexture;
	FIntPoint HzbSize;
};


struct FMobileHzbSystem {

	friend class FHZBOcclusionTester;
	friend class FMobileSceneRenderer;

	static constexpr bool bUseRaster = true;
	static constexpr int32 kMaxMipBatchSize = 1;
	static constexpr int32 kHzbTexWidth = 256;
	static constexpr int32 kHzbTexHeight = 128;
	static constexpr uint8 kHZBMaxMipmap = 8;

	static void InitialResource();

	static void ReduceMips(FRDGTextureSRVRef RDGTexutreMip, FRDGTextureRef RDGFurthestHZBTexture, FViewInfo& View, FRDGBuilder& GraphBuilder, uint32 CurOutHzbMipLevel);
	static void MobileRasterBuildHZB(FRHICommandListImmediate& RHICmdList, FViewInfo& View);

	static void ReduceBuffer(FRDGTextureSRVRef RDGTexutreMip, const FRWBuffer& MobileHzbBuffer, FViewInfo& View, FRDGBuilder& GraphBuilder, uint32 CurOutHzbMipLevel);
	static void MobileComputeBuildHZB(FRHICommandListImmediate& RHICmdList, FViewInfo& View);

private:
	static TGlobalResource<FMobileHzbResource>* MobileHzbResourcesPtr;
};
