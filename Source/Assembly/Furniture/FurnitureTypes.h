#pragma once

#include "CoreMinimal.h"
#include "FurnitureTypes.generated.h"

/** Room that owns a detected symbol on the floor plan. */
UENUM(BlueprintType)
enum class EFurnitureRoom : uint8
{
	Bedroom UMETA(DisplayName = "Bedroom"),
	Toilet UMETA(DisplayName = "Toilet"),
	Utility UMETA(DisplayName = "Utility"),
	Kitchen UMETA(DisplayName = "Kitchen"),
	Dining UMETA(DisplayName = "Dining"),
	Living UMETA(DisplayName = "Living"),
	Parking UMETA(DisplayName = "Parking")
};

/** Kind of furniture or fixture a mesh can be assigned to. */
UENUM(BlueprintType)
enum class EFurnitureKind : uint8
{
	Bed UMETA(DisplayName = "Bed"),
	Wardrobe UMETA(DisplayName = "Wardrobe"),
	Toilet UMETA(DisplayName = "Toilet"),
	WashBasin UMETA(DisplayName = "Wash Basin"),
	UtilitySink UMETA(DisplayName = "Utility Sink"),
	KitchenSink UMETA(DisplayName = "Kitchen Sink"),
	Stove UMETA(DisplayName = "Stove"),
	Fridge UMETA(DisplayName = "Fridge"),
	DiningTable UMETA(DisplayName = "Dining Table"),
	DiningChair UMETA(DisplayName = "Dining Chair"),
	Sofa UMETA(DisplayName = "Sofa"),
	CoffeeTable UMETA(DisplayName = "Coffee Table"),
	Television UMETA(DisplayName = "Television"),
	Plant UMETA(DisplayName = "Plant"),
	Car UMETA(DisplayName = "Car"),
	BedLamp UMETA(DisplayName = "Bed Lamp")
};

/**
 * One symbol found on a floor plan.
 * EastFeet / SouthFeet are the symbol center, in feet, from the plan's northwest corner.
 * WidthFeet is the east-west size. DepthFeet is the north-south size.
 * YawDegrees is the direction the front of the object faces: 0 east, 90 north, -90 south, 180 west.
 */
USTRUCT(BlueprintType)
struct ASSEMBLY_API FFloorplanDetection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	EFurnitureKind Kind = EFurnitureKind::Sofa;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	EFurnitureRoom Room = EFurnitureRoom::Living;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	float EastFeet = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	float SouthFeet = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	float WidthFeet = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	float DepthFeet = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	float YawDegrees = 0.f;
};

/** Everything a detector returns for one floor plan image. */
USTRUCT(BlueprintType)
struct ASSEMBLY_API FFloorplanDetectionResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	FString PlanName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	float PlanWidthFeet = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	float PlanDepthFeet = 30.f;

	/** Normalized rectangle of the building outline inside the source image. (0,0) is the image top-left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	FVector2D PlanUVMin = FVector2D(0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	FVector2D PlanUVMax = FVector2D(1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floorplan")
	TArray<FFloorplanDetection> Items;
};

/** One detected object imported from JSON. Location is centimeters. Rotation is degrees. Scale is the mesh scale. */
USTRUCT(BlueprintType)
struct ASSEMBLY_API FDetectedFurnitureObject
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString Category;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FVector Scale = FVector::OneVector;

	/** Maximum footprint in centimeters. Placement fits the mesh inside it with one scale. Zero means the JSON did not include a size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	FVector Size = FVector::ZeroVector;

	/** When set, Location is the footprint center and the mesh pivot is shifted from the mesh bounds origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Furniture")
	bool bPlaceAtBoundsCenter = false;
};

inline const EFurnitureKind* AllFurnitureKinds(int32& OutCount)
{
	static const EFurnitureKind Kinds[] =
	{
		EFurnitureKind::Bed,
		EFurnitureKind::Wardrobe,
		EFurnitureKind::Toilet,
		EFurnitureKind::WashBasin,
		EFurnitureKind::UtilitySink,
		EFurnitureKind::KitchenSink,
		EFurnitureKind::Stove,
		EFurnitureKind::Fridge,
		EFurnitureKind::DiningTable,
		EFurnitureKind::DiningChair,
		EFurnitureKind::Sofa,
		EFurnitureKind::CoffeeTable,
		EFurnitureKind::Television,
		EFurnitureKind::Plant,
		EFurnitureKind::Car,
		EFurnitureKind::BedLamp
	};
	OutCount = UE_ARRAY_COUNT(Kinds);
	return Kinds;
}

inline const EFurnitureRoom* AllFurnitureRooms(int32& OutCount)
{
	static const EFurnitureRoom Rooms[] =
	{
		EFurnitureRoom::Bedroom,
		EFurnitureRoom::Toilet,
		EFurnitureRoom::Utility,
		EFurnitureRoom::Kitchen,
		EFurnitureRoom::Dining,
		EFurnitureRoom::Living,
		EFurnitureRoom::Parking
	};
	OutCount = UE_ARRAY_COUNT(Rooms);
	return Rooms;
}

inline const TCHAR* FurnitureKindLabel(EFurnitureKind Kind)
{
	switch (Kind)
	{
	case EFurnitureKind::Bed: return TEXT("Bed");
	case EFurnitureKind::Wardrobe: return TEXT("Wardrobe");
	case EFurnitureKind::Toilet: return TEXT("Toilet");
	case EFurnitureKind::WashBasin: return TEXT("Wash Basin");
	case EFurnitureKind::UtilitySink: return TEXT("Utility Sink");
	case EFurnitureKind::KitchenSink: return TEXT("Kitchen Sink");
	case EFurnitureKind::Stove: return TEXT("Stove");
	case EFurnitureKind::Fridge: return TEXT("Fridge");
	case EFurnitureKind::DiningTable: return TEXT("Dining Table");
	case EFurnitureKind::DiningChair: return TEXT("Dining Chair");
	case EFurnitureKind::Sofa: return TEXT("Sofa");
	case EFurnitureKind::CoffeeTable: return TEXT("Coffee Table");
	case EFurnitureKind::Television: return TEXT("Television");
	case EFurnitureKind::Plant: return TEXT("Plant");
	case EFurnitureKind::Car: return TEXT("Car");
	case EFurnitureKind::BedLamp: return TEXT("Bed Lamp");
	default: return TEXT("Furniture");
	}
}

inline bool TryParseFurnitureKind(const FString& Name, EFurnitureKind& OutKind)
{
	int32 Count = 0;
	const EFurnitureKind* Kinds = AllFurnitureKinds(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Name.Equals(FurnitureKindLabel(Kinds[Index]), ESearchCase::IgnoreCase))
		{
			OutKind = Kinds[Index];
			return true;
		}
	}
	return false;
}

inline const TCHAR* FurnitureRoomLabel(EFurnitureRoom Room)
{
	switch (Room)
	{
	case EFurnitureRoom::Bedroom: return TEXT("Bedroom");
	case EFurnitureRoom::Toilet: return TEXT("Toilet");
	case EFurnitureRoom::Utility: return TEXT("Utility");
	case EFurnitureRoom::Kitchen: return TEXT("Kitchen");
	case EFurnitureRoom::Dining: return TEXT("Dining");
	case EFurnitureRoom::Living: return TEXT("Living");
	case EFurnitureRoom::Parking: return TEXT("Parking");
	default: return TEXT("Room");
	}
}

inline EFurnitureRoom FurnitureKindRoom(EFurnitureKind Kind)
{
	switch (Kind)
	{
	case EFurnitureKind::Bed:
	case EFurnitureKind::BedLamp:
	case EFurnitureKind::Wardrobe:
		return EFurnitureRoom::Bedroom;
	case EFurnitureKind::Toilet:
	case EFurnitureKind::WashBasin:
		return EFurnitureRoom::Toilet;
	case EFurnitureKind::UtilitySink:
		return EFurnitureRoom::Utility;
	case EFurnitureKind::KitchenSink:
	case EFurnitureKind::Stove:
	case EFurnitureKind::Fridge:
		return EFurnitureRoom::Kitchen;
	case EFurnitureKind::DiningTable:
	case EFurnitureKind::DiningChair:
		return EFurnitureRoom::Dining;
	case EFurnitureKind::Car:
		return EFurnitureRoom::Parking;
	case EFurnitureKind::Sofa:
	case EFurnitureKind::CoffeeTable:
	case EFurnitureKind::Television:
	case EFurnitureKind::Plant:
	default:
		return EFurnitureRoom::Living;
	}
}

/** Axis-aligned default footprint, in feet, used when the user adds an object by hand. */
inline void FurnitureKindDefaultSize(EFurnitureKind Kind, float& OutWidthFeet, float& OutDepthFeet, float& OutYawDegrees)
{
	OutWidthFeet = 2.f;
	OutDepthFeet = 2.f;
	OutYawDegrees = 0.f;

	switch (Kind)
	{
	case EFurnitureKind::Bed: OutWidthFeet = 6.3f; OutDepthFeet = 5.4f; OutYawDegrees = -90.f; break;
	case EFurnitureKind::BedLamp: OutWidthFeet = 1.2f; OutDepthFeet = 1.2f; OutYawDegrees = -90.f; break;
	case EFurnitureKind::Wardrobe: OutWidthFeet = 4.6f; OutDepthFeet = 1.4f; OutYawDegrees = 90.f; break;
	case EFurnitureKind::Toilet: OutWidthFeet = 1.6f; OutDepthFeet = 2.4f; OutYawDegrees = -90.f; break;
	case EFurnitureKind::WashBasin: OutWidthFeet = 1.6f; OutDepthFeet = 1.4f; OutYawDegrees = -90.f; break;
	case EFurnitureKind::UtilitySink: OutWidthFeet = 1.6f; OutDepthFeet = 2.2f; OutYawDegrees = 180.f; break;
	case EFurnitureKind::KitchenSink: OutWidthFeet = 2.8f; OutDepthFeet = 1.6f; OutYawDegrees = -90.f; break;
	case EFurnitureKind::Stove: OutWidthFeet = 1.6f; OutDepthFeet = 2.2f; OutYawDegrees = 180.f; break;
	case EFurnitureKind::Fridge: OutWidthFeet = 2.2f; OutDepthFeet = 2.2f; OutYawDegrees = 180.f; break;
	case EFurnitureKind::DiningTable: OutWidthFeet = 4.2f; OutDepthFeet = 2.8f; OutYawDegrees = 0.f; break;
	case EFurnitureKind::DiningChair: OutWidthFeet = 1.5f; OutDepthFeet = 1.5f; OutYawDegrees = -90.f; break;
	case EFurnitureKind::Sofa: OutWidthFeet = 7.2f; OutDepthFeet = 5.5f; OutYawDegrees = 0.f; break;
	case EFurnitureKind::CoffeeTable: OutWidthFeet = 2.f; OutDepthFeet = 3.2f; OutYawDegrees = 0.f; break;
	case EFurnitureKind::Television: OutWidthFeet = 0.8f; OutDepthFeet = 3.8f; OutYawDegrees = 180.f; break;
	case EFurnitureKind::Plant: OutWidthFeet = 1.3f; OutDepthFeet = 1.3f; OutYawDegrees = 0.f; break;
	case EFurnitureKind::Car: OutWidthFeet = 5.8f; OutDepthFeet = 12.f; OutYawDegrees = -90.f; break;
	default: break;
	}
}

/** UE mannequin standing height. Furniture is scaled against this before it is placed. */
inline float MannequinHeightCm()
{
	return 180.f;
}

/** Real height for this kind, in centimeters, compared with the 180 cm mannequin. */
inline float FurnitureKindHeightCm(EFurnitureKind Kind)
{
	switch (Kind)
	{
	case EFurnitureKind::Bed: return 70.f;
	case EFurnitureKind::BedLamp: return 45.f;
	case EFurnitureKind::Wardrobe: return 200.f;
	case EFurnitureKind::Toilet: return 80.f;
	case EFurnitureKind::WashBasin: return 85.f;
	case EFurnitureKind::UtilitySink: return 90.f;
	case EFurnitureKind::KitchenSink: return 90.f;
	case EFurnitureKind::Stove: return 90.f;
	case EFurnitureKind::Fridge: return 175.f;
	case EFurnitureKind::DiningTable: return 75.f;
	case EFurnitureKind::DiningChair: return 95.f;
	case EFurnitureKind::Sofa: return 85.f;
	case EFurnitureKind::CoffeeTable: return 42.f;
	case EFurnitureKind::Television: return 80.f;
	case EFurnitureKind::Plant: return 110.f;
	case EFurnitureKind::Car: return 150.f;
	default: return 90.f;
	}
}
