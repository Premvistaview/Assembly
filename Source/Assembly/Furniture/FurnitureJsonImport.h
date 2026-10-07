#pragma once

#include "CoreMinimal.h"
#include "Furniture/FurnitureTypes.h"

/** Result of checking a JSON file for object detection data. */
struct FFurnitureJsonImportResult
{
	bool bIsDetectionData = false;
	FString Message;
	int32 SkippedCount = 0;
	TArray<FDetectedFurnitureObject> Objects;
};

/**
 * Reads detection JSON. A valid object has a category, position, and rotation.
 * Positions are plan centimeters (+X east, +Y south from the northwest corner) unless root "units" says otherwise.
 * Root may be an object array or { "units", "objects" }. Import does not remap axes; fix coordinates in the JSON.
 */
ASSEMBLY_API FFurnitureJsonImportResult ImportFurnitureDetectionJson(const FString& JsonText);
