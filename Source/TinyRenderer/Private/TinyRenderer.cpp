#include "TinyRenderer.h"

#include "MeshMaterialShader.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "RenderGraphBuilder.h"
#include "SimpleMeshDrawCommandPass.h"
#include "Materials/MaterialRenderProxy.h"
#include "ShaderParameterStruct.h"
#include "MaterialDomain.h"
#include "PrimitiveSceneShaderData.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "StaticMeshResources.h"
#include "SystemTextures.h"
#include "TRRenderingMeshData.h"
#include "Engine/StaticMesh.h"
#include "UnrealClient.h"
#include "Runtime/Renderer/Private/SceneRendering.h"

DEFINE_LOG_CATEGORY_STATIC(LogTinyRenderer, Log, All);

/* TinyRenderer の VS/PS 共通で利用する機能を定義 */
namespace TinyRendererShader
{
	/* シェーダーのコンパイル環境を変更 */
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		/* GPUScene は利用するが、InstanceCulling は不要 */
		OutEnvironment.SetDefine(TEXT("USE_INSTANCE_CULLING_DATA"), 0);
		OutEnvironment.SetDefine(TEXT("USE_INSTANCE_CULLING"), 0);
	}

	/* 任意の ShaderPermutation に対してコンパイルを行うかどうかを判定 */
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static FName NAME_LocalVertexFactory(TEXT("FLocalVertexFactory"));
		const FMaterialShaderParameters& MaterialParameters = Parameters.MaterialParameters;
		return MaterialParameters.MaterialDomain == MD_Surface &&
			MaterialParameters.BlendMode == BLEND_Opaque &&
			Parameters.VertexFactoryType == FindVertexFactoryType(NAME_LocalVertexFactory);
	}
}

/* TinyRenderer の頂点シェーダー C++ 定義 */
class FTinyRendererShaderVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FTinyRendererShaderVS, MeshMaterial);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		/* 親の環境定義を引き継ぎつつ、TinyRenderer 用の環境定義を追加 */
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		TinyRendererShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return TinyRendererShader::ShouldCompilePermutation(Parameters);
	}
};

/* TinyRenderer のピクセルシェーダー C++ 定義 */
class FTinyRendererShaderPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FTinyRendererShaderPS, MeshMaterial);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters,
	                                         FShaderCompilerEnvironment& OutEnvironment)
	{
		/* 親の環境定義を引き継ぎつつ、TinyRenderer 用の環境定義を追加 */
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		TinyRendererShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		/* PixelShader 特有の環境定義を追加 */
		/* ライトベイクは行わないので、ライトマップ系機能は不要 */
		OutEnvironment.SetDefine(TEXT("NEEDS_LIGHTMAP_COORDINATE"), 0);
		/* 独自の出力を行うため、通常の SceneTexture は不要 */
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1);
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return TinyRendererShader::ShouldCompilePermutation(Parameters);
	}
};

/* TinyRenderer の頂点シェーダーとピクセルシェーダーの実装 (.usf ファイル) と C++ 定義を関連付けて登録 */
IMPLEMENT_MATERIAL_SHADER_TYPE(, FTinyRendererShaderVS,
                                 TEXT("/TinyRenderer/Private/TinyRendererShader.usf"),
                                 TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FTinyRendererShaderPS,
                                 TEXT("/TinyRenderer/Private/TinyRendererShader.usf"),
                                 TEXT("MainPS"), SF_Pixel);

/* TinyRenderer のシェーダーが利用するパラメータ構造体を定義 */
BEGIN_SHADER_PARAMETER_STRUCT(FTinyRendererShaderParameters,)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FTinyRendererBasePassMeshProcessor : public FMeshPassProcessor
{
public:
	FTinyRendererBasePassMeshProcessor(const FSceneView* InView,
	                                   FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(nullptr, InView->GetFeatureLevel(), InView, InDrawListContext),
		  FeatureLevel(InView->GetFeatureLevel())
	{
		/* メッシュ描画時の RenderState を設定。パイプラインの挙動を制御することになる */
		PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
		PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<>::GetRHI());
	}

	/* この MeshPassProcessor を通じて指定された MeshBatch のメッシュ描画コマンドをコマンドリストに追加する処理 */
	bool TryAddMeshBatch(const FMeshBatch& MeshBatch,
	                     const uint64 BatchElementMask,
	                     const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	                     const FMaterial& MaterialResource,
	                     const FMaterialRenderProxy& MaterialRenderProxy,
	                     const int32 StaticMeshId)
	{
		/* MeshBatch が利用する VertexFactory を取得 */
		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
		TMeshProcessorShaders<FTinyRendererShaderVS, FTinyRendererShaderPS> TinyRenderPassShaders;

		/* MeshBatch が利用する ShaderType を登録 */
		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<FTinyRendererShaderVS>();
		ShaderTypes.AddShaderType<FTinyRendererShaderPS>();

		/* 上で登録した ShaderType と MeshBatch に割り当てられたマテリアルをもとに、実際に利用するシェーダーコードたちを取得 */
		FMaterialShaders Shaders;
		if (const FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
			!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			return false;
		}

		/* 頂点シェーダーとピクセルシェーダーを取得 */
		Shaders.TryGetVertexShader(TinyRenderPassShaders.VertexShader);
		Shaders.TryGetPixelShader(TinyRenderPassShaders.PixelShader);

		/* メッシュ描画時の RenderState を設定 */
		const FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

		FMeshMaterialShaderElementData ShaderElementData;
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy,
		                                             MeshBatch, StaticMeshId, true);

		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(
			TinyRenderPassShaders.VertexShader, TinyRenderPassShaders.PixelShader);

		/* ラスタライザの FillMode と CullMode を設定 */
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MaterialResource, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MaterialResource, OverrideSettings);

		/* MeshBatch に対応する描画コマンドを作成し、内部でコマンドリストに追加 */
		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			DrawRenderState,
			TinyRenderPassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			ShaderElementData);
		return true;
	}

	virtual void AddMeshBatch(const FMeshBatch& MeshBatch,
	                          const uint64 BatchElementMask,
	                          const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	                          const int32 StaticMeshId = -1) override
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;

		while (MaterialRenderProxy)
		{
			if (const FMaterial* MaterialResource = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
				MaterialResource && TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy,
				                                    *MaterialResource, *MaterialRenderProxy, StaticMeshId))
			{
				break;
			}
			/* 最初に取得したマテリアルが利用できなかったりコマンドの作成に失敗した場合は、Fallback のマテリアルを試す。
			   Fallback のマテリアルがない場合には nullptr が返るので、ループを抜ける */
			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}
	}

private:
	FMeshPassProcessorRenderState PassDrawRenderState;
	ERHIFeatureLevel::Type FeatureLevel;
};

/**
 * @param InMeshBatches 作成した MeshBatch を格納する配列
 * @param RequiredFeatures MeshBatch が描画時に必要とする機能
 * @return MeshBatch が作成できた場合は true、それ以外は false
 */
bool FTinyRenderer::CreateMeshBatch(TArray<FMeshBatch>& InMeshBatches,
                                    FMeshBatchesRequiredFeatures& RequiredFeatures) const
{
	SCOPED_NAMED_EVENT_F(TEXT("FTinyRenderer::CreateMeshBatch - %s"), FColor::Emerald, *StaticMesh->GetName());

	// StaticMesh がコンパイル中の場合は MeshBatch を作成しない
	// Editor 用チェックであり、非 Editor ビルドでは定数化するので、最適化で消える
	if (StaticMesh->IsCompiling())
	{
		return false;
	}

	// StaticMesh から RenderData を取得。ここに StaticMesh のメッシュデータが格納されている
	FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();

	const int32 LODResourceIndex = FMath::Min(LODIndex, RenderData->LODResources.Num() - 1);
	if (LODResourceIndex < 0)
	{
		return false;
	}

	// 以下、LODResources から指定の LODIndex のメッシュデータを読み出し、MeshBatch を作成する
	const FStaticMeshSectionArray& Sections = RenderData->LODResources[LODResourceIndex].Sections;

	// セクションの数だけ MeshBatch を作成。一般的に、StaticMesh に割り当てられているマテリアルの数と対応している
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); SectionIndex++)
	{
		const FStaticMeshSection& Section = Sections[SectionIndex];
		if (Section.NumTriangles == 0)
		{
			continue;
		}

		const FStaticMeshLODResources& LODResource = RenderData->LODResources[LODResourceIndex];

		// データを MeshBatch に格納していく
		FMeshBatch MeshBatch;
		MeshBatch.VertexFactory = &RenderData->LODVertexFactories[LODResourceIndex].VertexFactory;
		MeshBatch.Type = PT_TriangleList;

		// MeshBatch の Element に IndexBuffer などを格納。MeshBatch は複数の Element を持つことができるが、エンジンでも多くの場合は 1 つの Element しか使われていない
		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
		const FLocalVertexFactory* VertexFactory = static_cast<const FLocalVertexFactory*>(MeshBatch.VertexFactory);
		BatchElement.VertexFactoryUserData = VertexFactory->GetUniformBuffer();

		BatchElement.IndexBuffer = &LODResource.IndexBuffer;
		BatchElement.FirstIndex = Section.FirstIndex;
		BatchElement.NumPrimitives = Section.NumTriangles;
		BatchElement.MinVertexIndex = Section.MinVertexIndex;
		BatchElement.MaxVertexIndex = Section.MaxVertexIndex;
		BatchElement.PrimitiveIdMode = PrimID_DynamicPrimitiveShaderData;

		MeshBatch.LODIndex = LODResourceIndex;
		MeshBatch.SegmentIndex = SectionIndex;
		MeshBatch.CastShadow = false;

		const UMaterialInterface* OverrideMaterial = OverrideMaterials.IsValidIndex(Section.MaterialIndex)
			                                             ? OverrideMaterials[Section.MaterialIndex].Get()
			                                             : nullptr;

		const UMaterialInterface* MaterialInterface = OverrideMaterial
			                                              ? OverrideMaterial
			                                              : StaticMesh->GetMaterial(Section.MaterialIndex);

		// マテリアルを取得
		if (BatchElement.NumPrimitives > 0 && MaterialInterface)
		{
			const auto MaterialProxy = MaterialInterface->GetRenderProxy();
			// マテリアルのレンダースレッド表現である MaterialRenderProxy を MeshBatch に MaterialRenderProxy を格納
			MeshBatch.MaterialRenderProxy = MaterialProxy;
			InMeshBatches.Add(MeshBatch);

			const FMaterialRelevance& MaterialRelevance = MaterialInterface->GetRelevance_Concurrent(FeatureLevel);
			// マテリアルが利用を要求しているレンダリング機能を RequiredFeatures に格納
			if (MaterialRelevance.bUsesWorldPositionOffset)
			{
				RequiredFeatures.bWorldPositionOffset = true;
			}
		}
	}

	if (InMeshBatches.IsEmpty())
	{
		return false;
	}

	return true;
}

/**
 * @param GraphBuilder RDGBuilder
 * @param RequiredFeatures 対象の MeshBatch の描画時に必要とする機能
 * @return GPUScene のためのパラメータ
 */
FGPUSceneResourceParameters FTinyRenderer::SetupGPUSceneResourceParameters(FRDGBuilder& GraphBuilder,
                                                                           const FMeshBatchesRequiredFeatures&
                                                                           RequiredFeatures) const
{
	/* PrimitiveData として使うパラメータを構築 */
	const FPrimitiveUniformShaderParameters PrimitiveParams = FPrimitiveUniformShaderParametersBuilder{}
	                                                          .Defaults()
	                                                          .LocalToWorld(LocalToWorld)
	                                                          .ActorWorldPosition(LocalToWorld.GetOrigin())
	                                                          .CastShadow(false)
	                                                          .CastContactShadow(false)
	                                                          .EvaluateWorldPositionOffset(
		                                                          RequiredFeatures.bWorldPositionOffset)
	                                                          .Build();
	const FPrimitiveSceneShaderData PrimitiveSceneData(PrimitiveParams);

	FGPUSceneResourceParameters GPUSceneParameters;
	{
		/* Primitive Data のバッファを作成 */
		const FRDGBufferRef RDGPrimitiveSceneDataBuffer = CreateStructuredBuffer(GraphBuilder,
			TEXT("PrimitiveSceneDataBuffer"), TArray{PrimitiveSceneData});
		GPUSceneParameters.GPUScenePrimitiveSceneData = GraphBuilder.CreateSRV(RDGPrimitiveSceneDataBuffer);
		GPUSceneParameters.NumScenePrimitives = 1;
	}
	{
		// FTransform3f TranslateX(FVector3f(100, 0, 0));
		//
		// FInstanceSceneShaderData InstanceSceneData;
		// InstanceSceneData.Build(1, /* PrimitiveId */
		//                         0, /* RelativeId */
		//                         0, /* InstanceFlags */
		//                         INVALID_LAST_UPDATE_FRAME, /* LastUpdateFrame */
		//                         0, /* CustomDataCount */
		//                         0.0f, /* RandomID */
		//                         FRenderTransform::Identity, /* LocalToPrimitive */
		//                         TranslateX.ToMatrixNoScale() * PrimitiveParams.LocalToRelativeWorld /* PrimitiveToWorld */
		// );
		//
		// /* Instance Data のバッファを作成 */
		// TArray<FVector4f> InstanceSceneDataSOA;
		// InstanceSceneDataSOA.AddZeroed(FInstanceSceneShaderData::GetDataStrideInFloat4s());
		// for (uint32 ArrayIndex = 0; ArrayIndex < FInstanceSceneShaderData::GetDataStrideInFloat4s(); ArrayIndex++)
		// {
		// 	InstanceSceneDataSOA[ArrayIndex] = InstanceSceneData.Data[ArrayIndex];
		// }

		TArray<FInstanceSceneShaderData> InstancesSceneData;
		{
			FInstanceSceneShaderData InstanceSceneData;
			InstanceSceneData.Build(0, /* PrimitiveId */
			                        0, /* RelativeId */
			                        0, /* InstanceFlags */
			                        INVALID_LAST_UPDATE_FRAME, /* LastUpdateFrame */
			                        0, /* CustomDataCount */
			                        0.0f, /* RandomID */
			                        FRenderTransform::Identity, /* LocalToPrimitive */
			                        PrimitiveParams.LocalToRelativeWorld
			);
			InstancesSceneData.Add(InstanceSceneData);
		}
		TArray<FVector4f> InstanceSceneDataSOA;
		InstanceSceneDataSOA.Reserve(InstancesSceneData.Num() * FInstanceSceneShaderData::GetDataStrideInFloat4s());
		for (const FInstanceSceneShaderData& InstanceSceneData : InstancesSceneData)
		{
			for (uint32 ArrayIndex = 0; ArrayIndex < FInstanceSceneShaderData::GetDataStrideInFloat4s(); ArrayIndex++)
			{
				InstanceSceneDataSOA.Add(InstanceSceneData.Data[ArrayIndex]);
			}
		}


		const FRDGBufferRef RDGInstanceSceneDataBuffer = CreateStructuredBuffer(GraphBuilder,
			TEXT("InstanceSceneDataBuffer"), InstanceSceneDataSOA);
		GPUSceneParameters.GPUSceneInstanceSceneData = GraphBuilder.CreateSRV(RDGInstanceSceneDataBuffer);
		GPUSceneParameters.InstanceDataSOAStride = InstancesSceneData.Num();
		GPUSceneParameters.NumInstances = InstancesSceneData.Num();
	}
	{
		/* ダミーのバッファで不要なパラメータを埋める */
		const FRDGBufferRef DummyBufferVec4 = GSystemTextures.GetDefaultStructuredBuffer(
			GraphBuilder, sizeof(FVector4f));
		const FRDGBufferRef DummyBufferLight = GSystemTextures.GetDefaultStructuredBuffer(
			GraphBuilder, sizeof(FLightSceneData));

		GPUSceneParameters.GPUSceneInstancePayloadData = GraphBuilder.CreateSRV(DummyBufferVec4);
		GPUSceneParameters.GPUSceneLightmapData = GraphBuilder.CreateSRV(DummyBufferVec4);
		GPUSceneParameters.GPUSceneLightData = GraphBuilder.CreateSRV(DummyBufferLight);
	}

	/* たぶん NRVO が効くのでそのまま返しちゃいましょう */
	return GPUSceneParameters;
}

void FTinyRenderer::SetGPUSceneResourceParameters(const FGPUSceneResourceParameters& Parameters)
{
	SceneUniforms.Set(SceneUB::GPUScene, Parameters);
}


FTinyRenderer::FTinyRenderer(const FSceneViewFamily& InViewFamily)
	: FeatureLevel(InViewFamily.GetFeatureLevel()), ViewFamily(InViewFamily)
{
}

void FTinyRenderer::SetStaticMeshData(UStaticMesh* InStaticMesh, const int32 InLODIndex, const FMatrix& InLocalToWorld,
                                      const TArray<UMaterialInterface*>& InOverrideMaterials)
{
	StaticMesh = InStaticMesh;
	LocalToWorld = InLocalToWorld;
	LODIndex = InLODIndex;
	Algo::Transform(InOverrideMaterials, OverrideMaterials, [](UMaterialInterface* InMaterial) -> TWeakObjectPtr<UMaterialInterface>
	{
		return InMaterial;
	});
}

void FTinyRenderer::Render(FRDGBuilder& GraphBuilder)
{
	SCOPED_NAMED_EVENT(FTinyRenderer_Render, FColor::Emerald);

	// レンダリング対象の SceneTextures を作成
	const FTinySceneTextures SceneTextures = SetupSceneTextures(GraphBuilder);
	// BasePass をレンダリング
	RenderBasePass(GraphBuilder, SceneTextures);
}

FTinyRenderer::FTinySceneTextures FTinyRenderer::SetupSceneTextures(FRDGBuilder& GraphBuilder) const
{
	// 描画先の FRenderTarget を取得。これが SceneColor の出力先になる
	const FRenderTarget* RenderTarget = ViewFamily.RenderTarget;
	// RDG のリソース管理に外部の RenderTarget を登録し、RDG のテクスチャリソースを表す FRDGTextureRef を取得
	const FRDGTextureRef TinyRendererOutputRef = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("TinyRendererOutput")));

	// SceneDepth 用のテクスチャを作成。今回は外部から参照しないので、ここで作成して利用する。
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(RenderTarget->GetSizeXY(), PF_DepthStencil,
	                                                       FClearValueBinding::DepthFar,
	                                                       TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
	const FRDGTextureRef SceneDepth = GraphBuilder.CreateTexture(Desc, TEXT("SceneDepthZ"));

	return FTinySceneTextures{
		.SceneColorTexture = TinyRendererOutputRef,
		.SceneDepthTexture = SceneDepth
	};
}

void FTinyRenderer::RenderBasePass(FRDGBuilder& GraphBuilder, const FTinySceneTextures& SceneTextures)
{
	SCOPED_NAMED_EVENT(FTinyRenderer_RenderBasePass, FColor::Emerald);

	// レンダリング対象の StaticMesh を取得
	UStaticMesh* Mesh = StaticMesh.IsValid() ? StaticMesh.Get() : nullptr;
	if (!Mesh)
	{
		UE_LOG(LogTinyRenderer, Warning, TEXT("StaticMesh is not valid"));
		return;
	}

	// StaticMesh から MeshBatch を作成
	TArray<FMeshBatch> MeshBatches;
	FMeshBatchesRequiredFeatures RequiredFeatures;
	if (!CreateMeshBatch(MeshBatches, RequiredFeatures))
	{
		UE_LOG(LogTinyRenderer, Warning, TEXT("Failed to create mesh batch"));
		return;
	}

	// GPUScene のためのパラメータをセットアップ
	const FGPUSceneResourceParameters GPUSceneResourceParameters = SetupGPUSceneResourceParameters(
		GraphBuilder, RequiredFeatures);
	SetGPUSceneResourceParameters(GPUSceneResourceParameters);

	// レンダリング対象の View を取得	
	const FViewInfo* View = static_cast<const FViewInfo*>(ViewFamily.Views[0]);

	// レンダリングに利用する Shader のパラメータを構築
	FTinyRendererShaderParameters* PassParameters = GraphBuilder.AllocParameters<FTinyRendererShaderParameters>();
	PassParameters->View = View->ViewUniformBuffer;
	PassParameters->Scene = SceneUniforms.GetBuffer(GraphBuilder);
	// レンダリング結果の出力先を設定
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextures.SceneColorTexture,
	                                                        ERenderTargetLoadAction::EClear);
	// DepthStencil の設定
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneTextures.SceneDepthTexture,
	                                                                  ERenderTargetLoadAction::EClear,
	                                                                  ERenderTargetLoadAction::ELoad,
	                                                                  FExclusiveDepthStencil::DepthWrite_StencilWrite);

	// メッシュ描画用のパスを RDG に登録
	AddSimpleMeshPass(
		GraphBuilder, PassParameters, nullptr, *View, nullptr,
		RDG_EVENT_NAME("TinyRendererBasePass"),
		View->UnscaledViewRect, ERDGPassFlags::Raster,
		[View, MeshBatches](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			for (const FMeshBatch& MeshBatch : MeshBatches)
			{
				// マテリアルから ShaderBinding を取得するために、必要に応じて UniformExpression を更新
				MeshBatch.MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View->GetFeatureLevel());
				// MeshBatch を TinyRenderer 用の BasePassMeshProcessor に追加
				FTinyRendererBasePassMeshProcessor TinyRendererBasePassMeshProcessor(View, DynamicMeshPassContext);
				TinyRendererBasePassMeshProcessor.AddMeshBatch(MeshBatch, ~0ull, nullptr);
			}
		});
}
