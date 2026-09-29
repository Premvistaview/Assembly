#pragma once

#include "CoreMinimal.h"
#include "Furniture/FurnitureTypes.h"
#include "FloorplanDetector.generated.h"

class UTexture2D;

/**
 * Turns a floor plan image into furniture locations.
 * Replace Detect with the team's object detector, or make a Blueprint child of this class.
 * Coordinates are feet from the northwest corner: +East to the right of the plan, +South toward the bottom.
 * Yaw is the direction the front faces: 0 east, 90 north, -90 south, 180 west.
 */
UCLASS(Abstract, Blueprintable)
class ASSEMBLY_API UFloorplanDetector : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintNativeEvent, Category = "Floorplan")
	FFloorplanDetectionResult Detect(UTexture2D* FloorplanImage);
};

/** Layout of the bundled 1 BHK sample plan (25 ft x 30 ft). Used until a custom detector is added. */
UCLASS()
class ASSEMBLY_API USampleBHKFloorplanDetector : public UFloorplanDetector
{
	GENERATED_BODY()

public:

	virtual FFloorplanDetectionResult Detect_Implementation(UTexture2D* FloorplanImage) override;
};
