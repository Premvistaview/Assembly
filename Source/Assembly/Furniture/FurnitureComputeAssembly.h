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

ASSEMBLY_API FFurnitureComputeResult ComputeFurnitureAssembly(const FString& JsonText);
