#pragma once

#include "CoreMinimal.h"
#include "Furniture/FurnitureTypes.h"

class UFurnitureCatalog;
class UWorld;

/** Detection list produced from a JSON file. A room floorplan is solved first, then read as the same list. */
struct ASSEMBLY_API FFurnitureAssemblyLoadResult
{
	bool bSuccess = false;

	/** True when the file was read. False when the path could not be opened. */
	bool bReadFile = false;

	/** True when the file was a floorplan of rooms and halls, so Compute Assembly ran. */
	bool bComputed = false;

	FString Message;
	TArray<FDetectedFurnitureObject> Objects;
};

/** Objects spawned into a world from a detection list, using the catalog mesh for each category. */
struct ASSEMBLY_API FFurniturePlacementReport
{
	int32 Placed = 0;
	TArray<FString> MissingCategories;
	FString Message;
};

/**
 * Reads a detection JSON file, or solves a floorplan and writes the computed list beside it.
 * Category names are aligned with the catalog, and an alias mesh is copied onto the category Compute Assembly uses.
 */
ASSEMBLY_API FFurnitureAssemblyLoadResult LoadFurnitureAssemblyFile(const FString& Path, UFurnitureCatalog* Catalog);

/** Solves a floorplan of rooms and halls with Compute Assembly. Rejects an object detection list. */
ASSEMBLY_API FFurnitureAssemblyLoadResult LoadFurnitureComputeAssemblyFile(const FString& Path, UFurnitureCatalog* Catalog);

/** Spawns one furniture actor per object. An existing actor with the same detection id is replaced. */
ASSEMBLY_API FFurniturePlacementReport PlaceDetectedFurniture(UWorld* World, UFurnitureCatalog* Catalog, const TArray<FDetectedFurnitureObject>& Objects);
