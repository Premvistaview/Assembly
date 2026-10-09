#include "Furniture/FurnitureAssemblyLoad.h"

#include "Furniture/FurnitureCatalog.h"
#include "Furniture/FurnitureComputeAssembly.h"
#include "Furniture/FurnitureJsonImport.h"
#include "Furniture/FurniturePlacementActor.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	UStaticMesh* LoadPlaceholderMesh()
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_Cube.SM_Cube"));
		if (Mesh == nullptr)
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}
		return Mesh;
	}

	UMaterialInterface* LoadPlaceholderMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	}

	void CollectTelevisionMesh(UFurnitureCatalog* Catalog, TArray<FFurnitureCatalogMeshSize>& OutSizes)
	{
		if (Catalog == nullptr)
		{
			return;
		}
		const TCHAR* Names[] = { TEXT("Television"), TEXT("tv"), TEXT("TV") };
		const FFurnitureMeshOption* Option = nullptr;
		for (const TCHAR* Name : Names)
		{
			Option = Catalog->GetNamedCategoryMesh(Name);
			if (Option != nullptr && !Option->Mesh.IsNull())
			{
				break;
			}
			Option = nullptr;
		}
		if (Option == nullptr)
		{
			return;
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Option->Mesh.ToSoftObjectPath().ToString());
		if (Mesh == nullptr)
		{
			return;
		}
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		FFurnitureCatalogMeshSize Size;
		Size.Category = TEXT("Television");
		Size.ExtentCm = FVector(Bounds.BoxExtent.X * 2.f, Bounds.BoxExtent.Y * 2.f, Bounds.BoxExtent.Z * 2.f);
		OutSizes.Add(Size);
	}

	/** Copies a mesh from a detection name such as "tv" onto the catalog category Compute Assembly uses. */
	void AdoptAliasMesh(UFurnitureCatalog* Catalog, const FString& Category)
	{
		if (Catalog == nullptr || Catalog->GetNamedCategoryMesh(Category) != nullptr)
		{
			return;
		}

		TArray<FString> Aliases;
		if (Category.Equals(TEXT("Television"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("tv"));
		}
		else if (Category.Equals(TEXT("Dining Table"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("table"));
		}
		else if (Category.Equals(TEXT("Dining Chair"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("chair"));
		}
		else if (Category.Equals(TEXT("Wash Basin"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("basin"));
			Aliases.Add(TEXT("washbasin"));
		}
		else if (Category.Equals(TEXT("Coffee Table"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("coffee"));
		}
		else if (Category.Equals(TEXT("Kitchen Sink"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("kitchensink"));
		}
		else if (Category.Equals(TEXT("Utility Sink"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("utilitysink"));
		}
		else if (Category.Equals(TEXT("Bed Lamp"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("lamp"));
			Aliases.Add(TEXT("bedlamp"));
		}
		else if (Category.Equals(TEXT("TV Stand"), ESearchCase::IgnoreCase))
		{
			Aliases.Add(TEXT("tv stand"));
			Aliases.Add(TEXT("tvstand"));
			Aliases.Add(TEXT("tv table"));
			Aliases.Add(TEXT("tvtable"));
		}

		for (const FString& Alias : Aliases)
		{
			const FFurnitureMeshOption* Option = Catalog->GetNamedCategoryMesh(Alias);
			if (Option == nullptr)
			{
				continue;
			}
			Catalog->SetNamedCategoryMesh(Category, Option->DisplayName, Option->Mesh.ToSoftObjectPath());
			if (FFurnitureNamedCategory* Entry = Catalog->FindNamedCategory(Category))
			{
				Entry->Mesh.YawOffsetDegrees = Option->YawOffsetDegrees;
			}
			return;
		}
	}

	void FillAssemblyObjects(FFurnitureAssemblyLoadResult& Result, UFurnitureCatalog* Catalog, const FFurnitureJsonImportResult& Imported)
	{
		Result.Message = Imported.Message;
		Result.bSuccess = Imported.bIsDetectionData;
		if (!Imported.bIsDetectionData)
		{
			return;
		}

		Result.Objects.Reserve(Imported.Objects.Num());
		for (const FDetectedFurnitureObject& Object : Imported.Objects)
		{
			FDetectedFurnitureObject Stored = Object;
			if (Catalog != nullptr)
			{
				if (const FFurnitureNamedCategory* Existing = Catalog->FindNamedCategory(Stored.Category))
				{
					Stored.Category = Existing->Name;
				}
				else
				{
					Catalog->EnsureNamedCategory(Stored.Category);
					AdoptAliasMesh(Catalog, Stored.Category);
				}
			}
			Result.Objects.Add(Stored);
		}
	}

	FFurnitureJsonImportResult ComputeAssemblyFromFloorplan(const FString& Path, const FString& JsonText, UFurnitureCatalog* Catalog, bool& bComputed)
	{
		bComputed = false;
		FFurnitureJsonImportResult Imported = ImportFurnitureDetectionJson(JsonText);
		if (Imported.bIsDetectionData)
		{
			return Imported;
		}

		FString FloorplanText = JsonText;
		const FFloorplanRoomSizeResult Sized = PrepareFloorplanRoomSizes(JsonText);
		if (Sized.bIsFloorplan && !Sized.JsonText.IsEmpty())
		{
			FloorplanText = Sized.JsonText;
			FFileHelper::SaveStringToFile(FloorplanText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		TArray<FFurnitureCatalogMeshSize> MeshSizes;
		CollectTelevisionMesh(Catalog, MeshSizes);
		const FFurnitureComputeResult Computed = ComputeFurnitureAssembly(FloorplanText, MeshSizes);
		if (!Computed.bIsFloorplan)
		{
			return Imported;
		}

		bComputed = true;
		if (Computed.ExportedJson.IsEmpty())
		{
			Imported.Message = FString::Printf(TEXT("Compute Assembly. %s"), *Computed.Message);
			return Imported;
		}

		const FString ExportPath = FPaths::Combine(FPaths::GetPath(Path), FPaths::GetBaseFilename(Path) + TEXT(".computed.json"));
		const bool bSaved = FFileHelper::SaveStringToFile(Computed.ExportedJson, *ExportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Imported = ImportFurnitureDetectionJson(Computed.ExportedJson);
		if (Imported.bIsDetectionData)
		{
			const FString SizePrefix = Sized.bIsFloorplan && !Sized.Message.IsEmpty() ? Sized.Message + TEXT(" ") : FString();
			Imported.Message = bSaved
				? FString::Printf(TEXT("%sConverted to object detection. %s Saved %s."), *SizePrefix, *Computed.Message, *FPaths::GetCleanFilename(ExportPath))
				: FString::Printf(TEXT("%sConverted to object detection. %s Could not write %s."), *SizePrefix, *Computed.Message, *FPaths::GetCleanFilename(ExportPath));
		}
		else
		{
			Imported.Message = FString::Printf(TEXT("Compute Assembly built a placement list, but it could not be read back. %s"), *Imported.Message);
		}
		return Imported;
	}
}

FFurnitureAssemblyLoadResult LoadFurnitureAssemblyFile(const FString& Path, UFurnitureCatalog* Catalog)
{
	FFurnitureAssemblyLoadResult Result;

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		Result.Message = FString::Printf(TEXT("Could not read %s."), *Path);
		return Result;
	}
	Result.bReadFile = true;

	FFurnitureJsonImportResult Imported = ImportFurnitureDetectionJson(JsonText);
	if (!Imported.bIsDetectionData)
	{
		bool bComputed = false;
		Imported = ComputeAssemblyFromFloorplan(Path, JsonText, Catalog, bComputed);
		Result.bComputed = bComputed;
	}

	FillAssemblyObjects(Result, Catalog, Imported);
	return Result;
}

FFurnitureAssemblyLoadResult LoadFurnitureComputeAssemblyFile(const FString& Path, UFurnitureCatalog* Catalog)
{
	FFurnitureAssemblyLoadResult Result;

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		Result.Message = FString::Printf(TEXT("Could not read %s."), *Path);
		return Result;
	}
	Result.bReadFile = true;

	const FFurnitureJsonImportResult Probe = ImportFurnitureDetectionJson(JsonText);
	if (Probe.bIsDetectionData)
	{
		Result.Message = TEXT("This JSON is an object list. Choose a floorplan of rooms and halls for Compute Assembly.");
		return Result;
	}

	bool bComputed = false;
	const FFurnitureJsonImportResult Imported = ComputeAssemblyFromFloorplan(Path, JsonText, Catalog, bComputed);
	Result.bComputed = bComputed;
	FillAssemblyObjects(Result, Catalog, Imported);
	return Result;
}

FFurniturePlacementReport PlaceDetectedFurniture(UWorld* World, UFurnitureCatalog* Catalog, const TArray<FDetectedFurnitureObject>& Objects)
{
	FFurniturePlacementReport Report;
	if (World == nullptr)
	{
		Report.Message = TEXT("There is no level to place objects into.");
		return Report;
	}

	UStaticMesh* PlaceholderMesh = LoadPlaceholderMesh();
	UMaterialInterface* PlaceholderMaterial = LoadPlaceholderMaterial();
	TSet<FString> MissingMeshes;

	for (const FDetectedFurnitureObject& Object : Objects)
	{
		UStaticMesh* Mesh = nullptr;
		float YawOffset = 0.f;
		bool bPlaceholder = true;
		if (Catalog != nullptr)
		{
			if (const FFurnitureMeshOption* Option = Catalog->GetNamedCategoryMesh(Object.Category))
			{
				Mesh = LoadObject<UStaticMesh>(nullptr, *Option->Mesh.ToSoftObjectPath().ToString());
				if (Mesh != nullptr)
				{
					bPlaceholder = false;
					YawOffset = Option->YawOffsetDegrees;
				}
			}
		}
		if (Mesh == nullptr)
		{
			Mesh = PlaceholderMesh;
			bPlaceholder = true;
			MissingMeshes.Add(Object.Category);
		}
		if (Mesh == nullptr)
		{
			continue;
		}

		AFurniturePlacementActor* Existing = nullptr;
		for (TActorIterator<AFurniturePlacementActor> It(World); It; ++It)
		{
			if (It->DetectionId == Object.Id)
			{
				Existing = *It;
				break;
			}
		}
		if (Existing != nullptr)
		{
			World->DestroyActor(Existing);
			Existing = nullptr;
		}

		FRotator SpawnRotation = Object.Rotation;
		SpawnRotation.Yaw += YawOffset;
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Existing = World->SpawnActor<AFurniturePlacementActor>(Object.Location, SpawnRotation, SpawnParams);
		if (Existing == nullptr)
		{
			continue;
		}

		Existing->ApplyDetectedObject(Object, Mesh, YawOffset, bPlaceholder, PlaceholderMaterial);
		++Report.Placed;
	}

	Report.MissingCategories = MissingMeshes.Array();
	Report.Message = FString::Printf(TEXT("Placed %d objects. Each yaw was read from that object's JSON rotation."), Report.Placed);
	if (Report.MissingCategories.Num() > 0)
	{
		Report.MissingCategories.Sort();
		Report.Message += TEXT(" Categories still using a block: ");
		Report.Message += FString::Join(Report.MissingCategories, TEXT(", "));
		Report.Message += TEXT(".");
	}
	return Report;
}
