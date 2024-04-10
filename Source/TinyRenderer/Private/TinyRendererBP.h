#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TinyRendererBP.generated.h"

class UTRPrimitiveReference;

UCLASS(BlueprintType)
class UTinyRenderer : public UObject
{
	GENERATED_BODY()

public:
	UTinyRenderer();

	UFUNCTION(BlueprintCallable, Category = "Tiny Renderer",
		meta = (AutoCreateRefTerm = "BackgroundColor", WorldContext = "WorldContextObject"))
	static UTinyRenderer* CreateTinyRenderer(UObject* WorldContextObject,
	                                         UTextureRenderTarget2D* RenderTarget);

	UFUNCTION(BlueprintCallable, Category = "Static Mesh Renderer")
	void SetStaticMesh(UStaticMesh* InStaticMesh, const int32 LODIndex);

	UFUNCTION(BlueprintCallable, Category = "Static Mesh Renderer", meta = (AutoCreateRefTerm = "InTransform"))
	void SetTransform(const FTransform& InTransform);

	UFUNCTION(BlueprintCallable, Category = "Static Mesh Renderer")
	void SetOverrideMaterial(UMaterialInterface* InMaterial, int32 InMaterialIndex);

	UFUNCTION(BlueprintCallable, Category = "Static Mesh Renderer")
	UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(UMaterialInterface* SourceMaterial, const int32 MaterialIndex);

	UFUNCTION(BlueprintCallable, Category = "Static Mesh Renderer")
	void Render();

	UPROPERTY(BlueprintReadWrite, Category = "Static Mesh Renderer")
	FMinimalViewInfo ViewInfo;

private:
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY()
	TObjectPtr<UStaticMesh> StaticMesh;

	FTransform Transform;
	
	int32 LODIndex;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> OverrideMaterials;
};
