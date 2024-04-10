#include "TinyRendererBP.h"

#include "EngineModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphEvent.h"
#include "TextureResource.h"
#include "TinyRenderer.h"
#include "Camera/CameraTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"

UTinyRenderer::UTinyRenderer()
{
}

UTinyRenderer* UTinyRenderer::CreateTinyRenderer(UObject* WorldContextObject,
                                                 UTextureRenderTarget2D* RenderTarget)
{
	if (!WorldContextObject || !RenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTinyRenderer::CreateTinyRenderer: Invalid parameters"));
		return nullptr;
	}
	UTinyRenderer* TinyRenderer = NewObject<UTinyRenderer>(WorldContextObject);
	TinyRenderer->RenderTarget = RenderTarget;

	return TinyRenderer;
}

void UTinyRenderer::SetStaticMesh(UStaticMesh* InStaticMesh, const int32 InLODIndex)
{
	StaticMesh = InStaticMesh;
	LODIndex = InLODIndex;

	OverrideMaterials.Empty();
	OverrideMaterials.Reserve(StaticMesh->GetStaticMaterials().Num());
	for (int32 MaterialIndex = 0; MaterialIndex < StaticMesh->GetStaticMaterials().Num(); ++MaterialIndex)
	{
		OverrideMaterials.Add(StaticMesh->GetMaterial(MaterialIndex));
	}	
}

void UTinyRenderer::SetTransform(const FTransform& InTransform)
{
	Transform = InTransform;
}

void UTinyRenderer::SetOverrideMaterial(UMaterialInterface* InMaterial, int32 InMaterialIndex)
{
	if (!StaticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTinyRenderer::SetOverrideMaterial: Invalid parameters"));
		return;
	}

	if (InMaterialIndex >= StaticMesh->GetStaticMaterials().Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("UTinyRenderer::SetOverrideMaterial: Invalid parameters"));
		return;
	}

	if (OverrideMaterials.Num() <= InMaterialIndex)
	{
		OverrideMaterials.AddZeroed(InMaterialIndex - OverrideMaterials.Num() + 1);
	}

	if (InMaterial)
	{
		OverrideMaterials[InMaterialIndex] = InMaterial;
	}
}

UMaterialInstanceDynamic* UTinyRenderer::CreateAndSetMaterialInstanceDynamic(UMaterialInterface* SourceMaterial,
	const int32 MaterialIndex)
{
	UMaterialInterface* ParentMaterial = SourceMaterial ? SourceMaterial : OverrideMaterials[MaterialIndex].Get();

	if (!ParentMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTinyRenderer::CreateAndSetMaterialInstanceDynamic: Invalid parameters"));
		return nullptr;
	}

	UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(ParentMaterial, this);
	SetOverrideMaterial(MaterialInstance, MaterialIndex);

	return MaterialInstance;
}

void UTinyRenderer::Render()
{
	SCOPED_NAMED_EVENT(UTinyRenderer_Render, FColor::Green);

	if (!StaticMesh || !RenderTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("UStaticMeshRenderBP::RenderStaticMesh: Invalid parameters"));
		return;
	}

	/* RenderTaget から 描画リソースを取得 */
	const FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();


	/* ViewFamily オブジェクトの作成 */
	FSceneViewFamily::ConstructionValues
		ConstructionValues(RenderTargetResource, nullptr, FEngineShowFlags(ESFIM_Game));
	ConstructionValues.SetTime(FGameTime::GetTimeSinceAppStart());
	TUniquePtr<FSceneViewFamilyContext> ViewFamily = MakeUnique<FSceneViewFamilyContext>(ConstructionValues);

	/* ScreenPercentage の無効化 */
	ViewFamily->EngineShowFlags.ScreenPercentage = false;
	ViewFamily->SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(*ViewFamily, 1.0f));

	/* MinimalViewInfo から ViewInitOptions を作成 */
	const FIntRect ViewRect(0, 0, RenderTarget->SizeX, RenderTarget->SizeY);
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.SetViewRectangle(ViewRect);
	ViewInitOptions.ViewFamily = ViewFamily.Get();
	ViewInitOptions.ViewOrigin = ViewInfo.Location;
	ViewInitOptions.ViewRotationMatrix = FMatrix(
		{0, 0, 1, 0},
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 0, 1});
	ViewInitOptions.FOV = ViewInfo.FOV;
	ViewInitOptions.DesiredFOV = ViewInfo.FOV;
	/* 投影行列を計算し、ViewInitOptions に設定 */
	FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(ViewInfo,
	                                                              AspectRatio_MaintainYFOV,
	                                                              ViewRect,
	                                                              ViewInitOptions);

	ENQUEUE_RENDER_COMMAND(FStaticMeshRenderCommand)(
		[this, ViewFamily = MoveTemp(ViewFamily), ViewInitOptions](
		FRHICommandListImmediate& RHICmdList) mutable
		{
			SCOPED_NAMED_EVENT(FStaticMeshRenderCommand_Render, FColor::Green);

			/* TinyRenderer オブジェクトの作成 */
			FTinyRenderer Renderer(*ViewFamily);
			/* RenderThread で ViewFamily の初期化を完了 */
			GetRendererModule().CreateAndInitSingleView(RHICmdList, ViewFamily.Get(), &ViewInitOptions);

			/* RDGBuilder の作成 */
			FRDGBuilder GraphBuilder(RHICmdList,
			                         RDG_EVENT_NAME("StaticMeshRender"),
			                         ERDGBuilderFlags::AllowParallelExecute);

			/* StaticMesh の設定 */
			Renderer.SetStaticMeshData(StaticMesh, LODIndex, Transform.ToMatrixWithScale(), OverrideMaterials);

			/* 作成したレンダラによる描画処理の登録 */
			Renderer.Render(GraphBuilder);

			/* RDGBuilder による RHI コマンドの発行と実行 */
			GraphBuilder.Execute();
		});
}
