#pragma once

#include "CoreMinimal.h"

/**
 * Turns a floorplan of rooms and halls into the object JSON Place Assembly already imports.
 * Runs only when the file has no object list.
 * Each placed prop uses a furniture-catalog category name, so it keeps that category's mesh.
 */
struct FFurnitureComputeResult
{
	/** True when the file describes rooms or halls, even if nothing could be placed. */
	bool bIsFloorplan = false;

	FString Message;

	/** Detection JSON. Empty when the floorplan produced no objects. */
	FString ExportedJson;
};

/**
 * Runs after import and before Compute Assembly.
 * For each room, wall distance is the span between opposite walls.
 * Swing radius is added from the doors on those walls.
 * totalSize is wall distance minus that swing, and is written back onto the JSON.
 */
struct FFloorplanRoomSizeResult
{
	bool bIsFloorplan = false;
	FString Message;
	FString JsonText;
};

ASSEMBLY_API FFloorplanRoomSizeResult PrepareFloorplanRoomSizes(const FString& JsonText);
ASSEMBLY_API FFurnitureComputeResult ComputeFurnitureAssembly(const FString& JsonText);
