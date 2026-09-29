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

/** Reads detection JSON. A valid object has a category, location, rotation, and scale. */
ASSEMBLY_API FFurnitureJsonImportResult ImportFurnitureDetectionJson(const FString& JsonText);
