#pragma once

struct FTRRenderingMeshData
{
	TWeakObjectPtr<UStaticMesh> StaticMesh;
	int32 LODIndex;
	FMatrix Transform;
	TArray<TWeakObjectPtr<UMaterialInterface>> OverrideMaterials;
};
