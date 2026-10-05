#include "Furniture/FurniturePlacementActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AFurniturePlacementActor::AFurniturePlacementActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
}

FVector AFurniturePlacementActor::PlanFeetToWorld(float EastFeet, float SouthFeet, float PlanWidthFeet, float PlanDepthFeet)
{
	const float CentimetersPerFoot = 30.48f;
	return FVector(
		(EastFeet - PlanWidthFeet * 0.5f) * CentimetersPerFoot,
		(PlanDepthFeet * 0.5f - SouthFeet) * CentimetersPerFoot,
		0.f);
}

void AFurniturePlacementActor::ApplyDetection(
	const FFloorplanDetection& Detection,
	UStaticMesh* Mesh,
	float YawOffsetDegrees,
	float PlanWidthFeet,
	float PlanDepthFeet,
	bool bPlaceholder,
	UMaterialInterface* PlaceholderMaterial)
{
	DetectionId = Detection.Id;
	Kind = Detection.Kind;
	Room = Detection.Room;
	WidthFeet = Detection.WidthFeet;
	DepthFeet = Detection.DepthFeet;

	if (Mesh == nullptr || MeshComponent == nullptr)
	{
		return;
	}

	MeshComponent->SetStaticMesh(Mesh);
	if (bPlaceholder && PlaceholderMaterial != nullptr)
	{
		const int32 SlotCount = FMath::Max(Mesh->GetStaticMaterials().Num(), 1);
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			MeshComponent->SetMaterial(Slot, PlaceholderMaterial);
		}
	}
	else
	{
		MeshComponent->EmptyOverrideMaterials();
	}

	const float Yaw = Detection.YawDegrees + YawOffsetDegrees;
	const bool bSwapAxes = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Yaw))) < 0.5f;
	const float CentimetersPerFoot = 30.48f;
	const float TargetX = (bSwapAxes ? Detection.DepthFeet : Detection.WidthFeet) * CentimetersPerFoot;
	const float TargetY = (bSwapAxes ? Detection.WidthFeet : Detection.DepthFeet) * CentimetersPerFoot;

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float MeshSizeX = FMath::Max(Bounds.BoxExtent.X * 2.f, 1.f);
	const float MeshSizeY = FMath::Max(Bounds.BoxExtent.Y * 2.f, 1.f);

	FVector Scale(TargetX / MeshSizeX, TargetY / MeshSizeY, 1.f);
	Scale.Z = FMath::Min(Scale.X, Scale.Y);

	const FRotator Rotation(0.f, Yaw, 0.f);
	const FVector PlanCenter = PlanFeetToWorld(Detection.EastFeet, Detection.SouthFeet, PlanWidthFeet, PlanDepthFeet);
	const float BottomZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale.Z;
	const FVector CenterOffset(Bounds.Origin.X * Scale.X, Bounds.Origin.Y * Scale.Y, 0.f);
	const FVector Location = FVector(PlanCenter.X, PlanCenter.Y, -BottomZ) - Rotation.RotateVector(CenterOffset);

	SetActorScale3D(Scale);
	SetActorRotation(Rotation);
	SetActorLocation(Location);

#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("%s (%s)"), *Detection.Label, FurnitureRoomLabel(Detection.Room)));
	SetFolderPath(FName(TEXT("Floorplan Furniture")));
#endif
}

void AFurniturePlacementActor::ApplyDetectedObject(
	const FDetectedFurnitureObject& Object,
	UStaticMesh* Mesh,
	float YawOffsetDegrees,
	bool bPlaceholder,
	UMaterialInterface* PlaceholderMaterial)
{
	DetectionId = Object.Id;
	CategoryName = Object.Category;
	if (!TryParseFurnitureKind(Object.Category, Kind))
	{
		Kind = EFurnitureKind::Sofa;
	}

	if (Mesh == nullptr || MeshComponent == nullptr)
	{
		return;
	}

	MeshComponent->SetStaticMesh(Mesh);
	if (bPlaceholder && PlaceholderMaterial != nullptr)
	{
		const int32 SlotCount = FMath::Max(Mesh->GetStaticMaterials().Num(), 1);
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			MeshComponent->SetMaterial(Slot, PlaceholderMaterial);
		}
	}
	else
	{
		MeshComponent->EmptyOverrideMaterials();
	}

	FRotator Rotation = Object.Rotation;
	Rotation.Yaw += YawOffsetDegrees;

	FVector Scale(
		FMath::IsNearlyZero(Object.Scale.X) ? 1.f : Object.Scale.X,
		FMath::IsNearlyZero(Object.Scale.Y) ? 1.f : Object.Scale.Y,
		FMath::IsNearlyZero(Object.Scale.Z) ? 1.f : Object.Scale.Z);

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float MeshX = FMath::Max(Bounds.BoxExtent.X * 2.f, 1.f);
	const float MeshY = FMath::Max(Bounds.BoxExtent.Y * 2.f, 1.f);
	const float MeshZ = FMath::Max(Bounds.BoxExtent.Z * 2.f, 1.f);

	float Uniform = 1.f;
	if (Object.bPlaceAtBoundsCenter)
	{
		const float TargetHeight = FurnitureKindHeightCm(Kind);
		Uniform = TargetHeight / MeshZ;
		if (Object.Size.X > 1.f && Object.Size.Y > 1.f)
		{
			const float FitX = Object.Size.X / (MeshX * Uniform);
			const float FitY = Object.Size.Y / (MeshY * Uniform);
			const float FloorFit = FMath::Min(FitX, FitY);
			if (FloorFit < 1.f)
			{
				Uniform *= FloorFit;
			}
		}
		const float MaxHeight = FMath::Max(TargetHeight, MannequinHeightCm() * 1.15f);
		const float Height = MeshZ * Uniform;
		if (Height > MaxHeight)
		{
			Uniform *= MaxHeight / Height;
		}
	}
	else if (Object.Size.X > 1.f && Object.Size.Y > 1.f)
	{
		Uniform = FMath::Min(Object.Size.X / MeshX, Object.Size.Y / MeshY);
		const float Height = MeshZ * Uniform;
		const float MaxHeight = FMath::Max(FurnitureKindHeightCm(Kind), MannequinHeightCm() * 1.15f);
		if (Height > MaxHeight)
		{
			Uniform *= MaxHeight / Height;
		}
	}
	Scale *= Uniform;

	const float BottomZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale.Z;
	FVector Location = Object.Location;
	Location.Z -= BottomZ;
	if (Object.bPlaceAtBoundsCenter)
	{
		const FVector BoundsOrigin(Bounds.Origin.X * Scale.X, Bounds.Origin.Y * Scale.Y, 0.f);
		Location -= Rotation.RotateVector(BoundsOrigin);
	}

	SetActorScale3D(Scale);
	SetActorRotation(Rotation);
	SetActorLocation(Location);

#if WITH_EDITOR
	PostEditMove(true);
	const FString Label = Object.Label.IsEmpty() ? Object.Category : Object.Label;
	SetActorLabel(FString::Printf(TEXT("%s (%s)"), *Label, *Object.Category));
	SetFolderPath(FName(TEXT("Furniture")));
#endif
}
