#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Furniture/FurnitureTypes.h"
#include "FurnitureCatalog.generated.h"

class UStaticMesh;

/** The one static mesh assigned to a furniture category. */
USTRUCT(BlueprintType)
struct ASSEMBLY_API FFurnitureMeshOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Added to the detected yaw so this mesh's front matches the plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	float YawOffsetDegrees = 0.f;
};

/** One category. Every object of this kind uses the same mesh. */
USTRUCT(BlueprintType)
struct ASSEMBLY_API FFurnitureKindCatalog
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	EFurnitureKind Kind = EFurnitureKind::Sofa;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bHasMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FFurnitureMeshOption Mesh;
};

/** One category discovered from a detection JSON file. Every object in the category uses the same mesh. */
USTRUCT(BlueprintType)
struct ASSEMBLY_API FFurnitureNamedCategory
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bHasMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FFurnitureMeshOption Mesh;
};

/** Saved mesh for each furniture category. Asset path: /Game/Furniture/DA_FurnitureCatalog. */
UCLASS(BlueprintType)
class ASSEMBLY_API UFurnitureCatalog : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	TArray<FFurnitureKindCatalog> Kinds;

	/** Categories named by the imported JSON. Each one stores a single premodel mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	TArray<FFurnitureNamedCategory> NamedCategories;

	void EnsureAllKinds();
	void EnsureNamedCategory(const FString& Name);

	FFurnitureKindCatalog* FindKind(EFurnitureKind Kind);
	const FFurnitureKindCatalog* FindKind(EFurnitureKind Kind) const;

	/** The mesh shared by every object in this category, or null when none is assigned. */
	FFurnitureMeshOption* GetCategoryMesh(EFurnitureKind Kind);
	const FFurnitureMeshOption* GetCategoryMesh(EFurnitureKind Kind) const;

	void SetCategoryMesh(EFurnitureKind Kind, const FString& DisplayName, const FSoftObjectPath& MeshPath);
	void ClearCategoryMesh(EFurnitureKind Kind);

	FFurnitureNamedCategory* FindNamedCategory(const FString& Name);
	const FFurnitureNamedCategory* FindNamedCategory(const FString& Name) const;

	FFurnitureMeshOption* GetNamedCategoryMesh(const FString& Name);
	const FFurnitureMeshOption* GetNamedCategoryMesh(const FString& Name) const;

	void SetNamedCategoryMesh(const FString& Name, const FString& DisplayName, const FSoftObjectPath& MeshPath);
	void ClearNamedCategoryMesh(const FString& Name);
};
