#include "PlaceAssemblyCommandlet.h"

#include "Furniture/FurnitureAssemblyLoad.h"
#include "Furniture/FurnitureCatalog.h"

#include "Components/LightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlaceAssembly, Log, All);

namespace
{
	const TCHAR* FirstPersonGameMode = TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C");
	const TCHAR* FurnitureCatalogPath = TEXT("/Game/Furniture/DA_FurnitureCatalog.DA_FurnitureCatalog");

	bool HasDirectionalLight(UWorld* World)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			return true;
		}
		return false;
	}

	void AddLevelLighting(UWorld* World)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-50.f, -30.f, 0.f), SpawnParams))
		{
			if (Sun->GetLightComponent() != nullptr)
			{
				Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
				Sun->GetLightComponent()->SetIntensity(10.f);
			}
			Sun->SetActorLabel(TEXT("Sun"));
		}

		if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
		{
			if (Sky->GetLightComponent() != nullptr)
			{
				Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
				Sky->GetLightComponent()->SetIntensity(1.f);
				if (USkyLightComponent* SkyComponent = Cast<USkyLightComponent>(Sky->GetLightComponent()))
				{
					SkyComponent->bRealTimeCapture = true;
					SkyComponent->SourceType = SLS_CapturedScene;
					SkyComponent->bLowerHemisphereIsBlack = false;
				}
			}
			Sky->SetActorLabel(TEXT("SkyLight"));
		}

		if (ASkyAtmosphere* Atmosphere = World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
		{
			Atmosphere->SetActorLabel(TEXT("SkyAtmosphere"));
		}
		if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
		{
			Fog->SetActorLabel(TEXT("HeightFog"));
		}

		if (APostProcessVolume* Post = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
		{
			Post->bUnbound = true;
			Post->Settings.bOverride_AutoExposureMethod = true;
			Post->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			Post->Settings.bOverride_AutoExposureBias = true;
			Post->Settings.AutoExposureBias = 1.f;
			Post->SetActorLabel(TEXT("Exposure"));
		}
	}

	void EnsurePlayerStart(UWorld* World)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			return;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>(FVector(0.f, 0.f, 120.f), FRotator::ZeroRotator, SpawnParams))
		{
			PlayerStart->SetActorLabel(TEXT("PlayerStart"));
		}
	}
}

UPlaceAssemblyCommandlet::UPlaceAssemblyCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UPlaceAssemblyCommandlet::Main(const FString& Params)
{
	FString JsonPath;
	if (!FParse::Value(*Params, TEXT("Json="), JsonPath))
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("Pass the floorplan file with -Json=\"C:/path/file.json\"."));
		return 1;
	}
	JsonPath = JsonPath.TrimQuotes();
	if (!FPaths::FileExists(JsonPath))
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("JSON file was not found: %s"), *JsonPath);
		return 1;
	}

	FString MapPackage;
	if (!FParse::Value(*Params, TEXT("Map="), MapPackage))
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("Pass the level to open in the packaged game with -Map=/Game/YourLevel."));
		return 1;
	}

	if (GEditor == nullptr)
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("Place Assembly must run inside Unreal Editor."));
		return 1;
	}

	UFurnitureCatalog* Catalog = LoadObject<UFurnitureCatalog>(nullptr, FurnitureCatalogPath);
	if (Catalog == nullptr)
	{
		Catalog = LoadObject<UFurnitureCatalog>(nullptr, TEXT("/Game/Furniture/DA_FurnitureCatalog"));
	}
	if (Catalog == nullptr)
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("The furniture catalog is missing. Assign category meshes in the editor first."));
		return 1;
	}

	UE_LOG(LogPlaceAssembly, Display, TEXT("Reading %s"), *JsonPath);
	const FFurnitureAssemblyLoadResult Loaded = LoadFurnitureComputeAssemblyFile(JsonPath, Catalog);
	UE_LOG(LogPlaceAssembly, Display, TEXT("Compute Assembly. %s"), *Loaded.Message);
	if (!Loaded.bSuccess || Loaded.Objects.Num() == 0)
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("Compute Assembly did not produce any objects to place."));
		return 1;
	}

	const FString MapFilename = FPackageName::LongPackageNameToFilename(MapPackage, FPackageName::GetMapPackageExtension());
	if (!FPaths::FileExists(MapFilename))
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("The level was not found: %s"), *MapFilename);
		return 1;
	}

	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
	if (World == nullptr)
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("Could not open %s."), *MapPackage);
		return 1;
	}
	UE_LOG(LogPlaceAssembly, Display, TEXT("Opened %s for furniture placement."), *MapPackage);

	const FFurniturePlacementReport Placement = PlaceDetectedFurniture(World, Catalog, Loaded.Objects);
	UE_LOG(LogPlaceAssembly, Display, TEXT("%s"), *Placement.Message);
	if (Placement.Placed == 0)
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("No furniture actors were spawned."));
		return 1;
	}

	if (UClass* GameModeClass = LoadClass<AGameModeBase>(nullptr, FirstPersonGameMode))
	{
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->DefaultGameMode = GameModeClass;
		}
	}
	else
	{
		UE_LOG(LogPlaceAssembly, Warning, TEXT("Could not load the first person game mode at %s."), FirstPersonGameMode);
	}

	EnsurePlayerStart(World);

	if (!HasDirectionalLight(World))
	{
		AddLevelLighting(World);
	}

	if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPackage))
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("Could not save the level %s."), *MapPackage);
		return 1;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(MapPackage);
	const FString MapObject = MapPackage + TEXT(".") + AssetName;
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlaceAssembly"), TEXT("build.txt"));
	const FString Report = FString::Printf(
		TEXT("MapPackage=%s\nMapObject=%s\nPlaced=%d\nAssembly=Compute\nJson=%s\n"),
		*MapPackage,
		*MapObject,
		Placement.Placed,
		*JsonPath);
	if (!FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogPlaceAssembly, Error, TEXT("The level was saved, but the build report could not be written to %s."), *ReportPath);
		return 1;
	}

	UE_LOG(LogPlaceAssembly, Display, TEXT("PlaceAssembly saved %s"), *MapObject);
	return 0;
}
