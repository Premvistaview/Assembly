#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Furniture/FurnitureTypes.h"
#include "FurniturePlacementActor.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * One piece of furniture placed from a floor plan.
 * The mesh is scaled so its footprint matches the detected size, and it sits on Z = 0.
 */
UCLASS(Blueprintable)
class ASSEMBLY_API AFurniturePlacementActor : public AActor
{
	GENERATED_BODY()

public:

	AFurniturePlacementActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Furniture")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	FName DetectionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	FString CategoryName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	EFurnitureKind Kind = EFurnitureKind::Sofa;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	EFurnitureRoom Room = EFurnitureRoom::Living;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	float WidthFeet = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Furniture")
	float DepthFeet = 2.f;

	/**
	 * Moves this actor onto the plan.
	 * PlanWidthFeet and PlanDepthFeet center the building on the world origin.
	 * +X is east, +Y is north. YawOffsetDegrees corrects the mesh's authored front.
	 */
	void ApplyDetection(
		const FFloorplanDetection& Detection,
		UStaticMesh* Mesh,
		float YawOffsetDegrees,
		float PlanWidthFeet,
		float PlanDepthFeet,
		bool bPlaceholder,
		UMaterialInterface* PlaceholderMaterial);

	/** World location of a plan point. The plan center is the world origin, north is +Y. */
	static FVector PlanFeetToWorld(float EastFeet, float SouthFeet, float PlanWidthFeet, float PlanDepthFeet);

	/** Places the actor with the location, rotation, and scale taken from detection JSON. */
	void ApplyDetectedObject(
		const FDetectedFurnitureObject& Object,
		UStaticMesh* Mesh,
		float YawOffsetDegrees,
		bool bPlaceholder,
		UMaterialInterface* PlaceholderMaterial);
};
