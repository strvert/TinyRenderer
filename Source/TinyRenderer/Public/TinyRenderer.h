#pragma once
#include "GPUScene.h"
#include "Runtime/Renderer/Private/SceneUniformBuffer.h"

struct FTRRenderingMeshData;

class TINYRENDERER_API FTinyRenderer
{
public:
	// コンストラクタ。FSceneViewFamilyを受け取る
	explicit FTinyRenderer(const FSceneViewFamily& InViewFamily);
	// StaticMesh およびその変換行列を設定する
	void SetStaticMeshData(UStaticMesh* InStaticMesh, const int32 InLODIndex, const FMatrix& InLocalToWorld,
	                       const TArray<UMaterialInterface*>& InOverrideMaterials);
	// 描画命令を発行する
	void Render(FRDGBuilder& GraphBuilder);

private:
	struct FTinySceneTextures
	{
		FRDGTextureRef SceneColorTexture;
		FRDGTextureRef SceneDepthTexture;
	};

	struct FMeshBatchesRequiredFeatures
	{
		bool bWorldPositionOffset = false;
	};

	FTinySceneTextures SetupSceneTextures(FRDGBuilder& GraphBuilder) const;
	void RenderBasePass(FRDGBuilder& GraphBuilder, const FTinySceneTextures& SceneTextures);

	bool CreateMeshBatch(TArray<FMeshBatch>& InMeshBatches,
	                     FMeshBatchesRequiredFeatures& RequiredFeatures) const;

	FGPUSceneResourceParameters SetupGPUSceneResourceParameters(FRDGBuilder& GraphBuilder,
	                                                            const FMeshBatchesRequiredFeatures& RequiredFeatures)
	const;

	void SetGPUSceneResourceParameters(const FGPUSceneResourceParameters& Parameters);

	ERHIFeatureLevel::Type FeatureLevel;
	const FSceneViewFamily& ViewFamily;
	FSceneUniformBuffer SceneUniforms;

	TWeakObjectPtr<UStaticMesh> StaticMesh;
	FMatrix LocalToWorld;
	int32 LODIndex;
	TArray<TWeakObjectPtr<UMaterialInterface>> OverrideMaterials;
};
