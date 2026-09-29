#include "Furniture/FloorplanDetector.h"

namespace
{
	FFloorplanDetection MakeDetection(
		const TCHAR* Id,
		EFurnitureKind Kind,
		const TCHAR* Label,
		float EastFeet,
		float SouthFeet,
		float WidthFeet,
		float DepthFeet,
		float YawDegrees)
	{
		FFloorplanDetection Item;
		Item.Id = Id;
		Item.Kind = Kind;
		Item.Room = FurnitureKindRoom(Kind);
		Item.Label = Label;
		Item.EastFeet = EastFeet;
		Item.SouthFeet = SouthFeet;
		Item.WidthFeet = WidthFeet;
		Item.DepthFeet = DepthFeet;
		Item.YawDegrees = YawDegrees;
		return Item;
	}
}

FFloorplanDetectionResult UFloorplanDetector::Detect_Implementation(UTexture2D* FloorplanImage)
{
	FFloorplanDetectionResult Empty;
	Empty.PlanName = TEXT("Floor plan");
	return Empty;
}

FFloorplanDetectionResult USampleBHKFloorplanDetector::Detect_Implementation(UTexture2D* FloorplanImage)
{
	// Positions are measured from the bundled 1 BHK drawing.
	// The building outline on that 975x1024 image is pixels (130, 59) to (877, 867).
	FFloorplanDetectionResult Result;
	Result.PlanName = TEXT("1 BHK");
	Result.PlanWidthFeet = 25.f;
	Result.PlanDepthFeet = 30.f;
	Result.PlanUVMin = FVector2D(130.f / 975.f, 59.f / 1024.f);
	Result.PlanUVMax = FVector2D(877.f / 975.f, 867.f / 1024.f);

	TArray<FFloorplanDetection>& Items = Result.Items;

	Items.Add(MakeDetection(TEXT("Bed"), EFurnitureKind::Bed, TEXT("Bed"), 4.16f, 5.33f, 6.33f, 5.46f, -90.f));
	Items.Add(MakeDetection(TEXT("Wardrobe"), EFurnitureKind::Wardrobe, TEXT("Wardrobe"), 3.55f, 12.20f, 4.55f, 1.30f, 90.f));

	Items.Add(MakeDetection(TEXT("Toilet"), EFurnitureKind::Toilet, TEXT("Toilet"), 12.40f, 1.80f, 1.60f, 2.40f, -90.f));
	Items.Add(MakeDetection(TEXT("WashBasin"), EFurnitureKind::WashBasin, TEXT("Wash Basin"), 15.80f, 1.50f, 1.60f, 1.40f, -90.f));

	Items.Add(MakeDetection(TEXT("UtilitySink"), EFurnitureKind::UtilitySink, TEXT("Utility Sink"), 23.50f, 2.00f, 1.60f, 2.20f, 180.f));

	Items.Add(MakeDetection(TEXT("KitchenSink"), EFurnitureKind::KitchenSink, TEXT("Kitchen Sink"), 21.30f, 5.60f, 2.80f, 1.60f, -90.f));
	Items.Add(MakeDetection(TEXT("Stove"), EFurnitureKind::Stove, TEXT("Stove"), 23.70f, 8.60f, 1.60f, 2.20f, 180.f));
	Items.Add(MakeDetection(TEXT("Fridge"), EFurnitureKind::Fridge, TEXT("Fridge"), 23.20f, 12.00f, 2.20f, 2.20f, 180.f));

	Items.Add(MakeDetection(TEXT("DiningTable"), EFurnitureKind::DiningTable, TEXT("Dining Table"), 14.80f, 9.40f, 4.20f, 2.80f, 0.f));
	Items.Add(MakeDetection(TEXT("ChairNorthA"), EFurnitureKind::DiningChair, TEXT("Dining Chair"), 14.00f, 7.20f, 1.50f, 1.50f, -90.f));
	Items.Add(MakeDetection(TEXT("ChairNorthB"), EFurnitureKind::DiningChair, TEXT("Dining Chair"), 15.60f, 7.20f, 1.50f, 1.50f, -90.f));
	Items.Add(MakeDetection(TEXT("ChairWest"), EFurnitureKind::DiningChair, TEXT("Dining Chair"), 11.60f, 9.40f, 1.50f, 1.50f, 0.f));
	Items.Add(MakeDetection(TEXT("ChairEast"), EFurnitureKind::DiningChair, TEXT("Dining Chair"), 17.60f, 9.40f, 1.50f, 1.50f, 180.f));

	Items.Add(MakeDetection(TEXT("Sofa"), EFurnitureKind::Sofa, TEXT("Sofa"), 16.00f, 21.20f, 7.20f, 5.50f, 0.f));
	Items.Add(MakeDetection(TEXT("CoffeeTable"), EFurnitureKind::CoffeeTable, TEXT("Coffee Table"), 16.80f, 23.60f, 2.00f, 3.20f, 0.f));
	Items.Add(MakeDetection(TEXT("Television"), EFurnitureKind::Television, TEXT("Television"), 23.80f, 21.00f, 0.80f, 3.80f, 180.f));
	Items.Add(MakeDetection(TEXT("Plant"), EFurnitureKind::Plant, TEXT("Plant"), 22.80f, 27.50f, 1.30f, 1.30f, 0.f));

	Items.Add(MakeDetection(TEXT("Car"), EFurnitureKind::Car, TEXT("Car"), 5.50f, 21.00f, 5.80f, 12.00f, -90.f));

	return Result;
}
