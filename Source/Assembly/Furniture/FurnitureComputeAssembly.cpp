#include "Furniture/FurnitureComputeAssembly.h"

#include "Furniture/FurnitureTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogFurnitureCompute, Log, All);

namespace
{
	constexpr float CellCm = 10.f;
	constexpr int32 MaxCells = 2000;

	enum class EPlanWall : uint8
	{
		North,
		East,
		South,
		West
	};

	enum class ECell : uint8
	{
		Free,
		Wall,
		Swing,
		Occupied
	};

	enum class EPropAnchor : uint8
	{
		Wall,
		Center,
		Beside,
		InFront,
		Opposite,
		Around,
		Corner,
		LongAxis
	};

	struct FPropDef
	{
		EFurnitureKind Kind = EFurnitureKind::Sofa;
		EPropAnchor Anchor = EPropAnchor::Wall;
	};

	struct FDoorSpec
	{
		EPlanWall Wall = EPlanWall::North;
		float OffsetCm = 0.f;
		float WidthCm = 90.f;
		float SwingCm = 90.f;
	};

	struct FSpaceSpec
	{
		FString Name;
		int32 Index = 0;
		bool bHall = false;
		bool bKnownRoom = false;
		EFurnitureRoom Room = EFurnitureRoom::Living;
		float OriginX = 0.f;
		float OriginY = 0.f;
		float SizeX = 0.f;
		float SizeY = 0.f;
		TArray<FDoorSpec> Doors;
	};

	struct FGridPose
	{
		int32 MinX = 0;
		int32 MinY = 0;
		int32 SizeX = 0;
		int32 SizeY = 0;
		float Yaw = 0.f;
		float LocalXCm = 0.f;
		float LocalYCm = 0.f;
		EPlanWall Wall = EPlanWall::North;
		bool bValid = false;
	};

	struct FRoomGrid
	{
		float OriginX = 0.f;
		float OriginY = 0.f;
		int32 CellsX = 0;
		int32 CellsY = 0;
		TArray<ECell> Cells;
		TArray<FDoorSpec> Doors;

		bool InBounds(int32 X, int32 Y) const
		{
			return X >= 0 && Y >= 0 && X < CellsX && Y < CellsY;
		}

		ECell Get(int32 X, int32 Y) const
		{
			if (!InBounds(X, Y))
			{
				return ECell::Wall;
			}
			return Cells[X + Y * CellsX];
		}

		void SetIfFree(int32 X, int32 Y, ECell Value)
		{
			if (InBounds(X, Y) && Cells[X + Y * CellsX] == ECell::Free)
			{
				Cells[X + Y * CellsX] = Value;
			}
		}

		bool Fits(const FGridPose& Pose) const
		{
			if (Pose.SizeX < 1 || Pose.SizeY < 1)
			{
				return false;
			}
			for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
			{
				for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
				{
					if (Get(X, Y) != ECell::Free)
					{
						return false;
					}
				}
			}
			return true;
		}

		void Occupy(const FGridPose& Pose)
		{
			constexpr int32 Margin = 1;
			for (int32 Y = Pose.MinY - Margin; Y < Pose.MinY + Pose.SizeY + Margin; ++Y)
			{
				for (int32 X = Pose.MinX - Margin; X < Pose.MinX + Pose.SizeX + Margin; ++X)
				{
					SetIfFree(X, Y, ECell::Occupied);
				}
			}
		}
	};

	struct FExportedObject
	{
		FString Id;
		FString Category;
		FString Label;
		float X = 0.f;
		float Y = 0.f;
		float Yaw = 0.f;
		float SizeX = 0.f;
		float SizeY = 0.f;
	};

	int32 CellsForCm(float Cm)
	{
		return FMath::Max(1, FMath::CeilToInt(Cm / CellCm - 0.001f));
	}

	EPlanWall OppositeWall(EPlanWall Wall)
	{
		switch (Wall)
		{
		case EPlanWall::North: return EPlanWall::South;
		case EPlanWall::South: return EPlanWall::North;
		case EPlanWall::East: return EPlanWall::West;
		case EPlanWall::West: return EPlanWall::East;
		default: return EPlanWall::North;
		}
	}

	float YawIntoRoom(EPlanWall BackWall)
	{
		switch (BackWall)
		{
		case EPlanWall::North: return -90.f;
		case EPlanWall::South: return 90.f;
		case EPlanWall::East: return 180.f;
		case EPlanWall::West: return 0.f;
		default: return 0.f;
		}
	}

	FString KindToken(EFurnitureKind Kind)
	{
		switch (Kind)
		{
		case EFurnitureKind::Bed: return TEXT("Bed");
		case EFurnitureKind::Wardrobe: return TEXT("Wardrobe");
		case EFurnitureKind::Toilet: return TEXT("Toilet");
		case EFurnitureKind::WashBasin: return TEXT("WashBasin");
		case EFurnitureKind::UtilitySink: return TEXT("UtilitySink");
		case EFurnitureKind::KitchenSink: return TEXT("KitchenSink");
		case EFurnitureKind::Stove: return TEXT("Stove");
		case EFurnitureKind::Fridge: return TEXT("Fridge");
		case EFurnitureKind::DiningTable: return TEXT("DiningTable");
		case EFurnitureKind::DiningChair: return TEXT("DiningChair");
		case EFurnitureKind::Sofa: return TEXT("Sofa");
		case EFurnitureKind::CoffeeTable: return TEXT("CoffeeTable");
		case EFurnitureKind::Television: return TEXT("Television");
		case EFurnitureKind::Plant: return TEXT("Plant");
		case EFurnitureKind::Car: return TEXT("Car");
		default: return TEXT("Furniture");
		}
	}

	void PropsForRoom(EFurnitureRoom Room, TArray<FPropDef>& OutProps)
	{
		OutProps.Reset();
		switch (Room)
		{
		case EFurnitureRoom::Bedroom:
			OutProps.Add({ EFurnitureKind::Bed, EPropAnchor::Wall });
			OutProps.Add({ EFurnitureKind::Wardrobe, EPropAnchor::Beside });
			break;
		case EFurnitureRoom::Toilet:
			OutProps.Add({ EFurnitureKind::Toilet, EPropAnchor::Wall });
			OutProps.Add({ EFurnitureKind::WashBasin, EPropAnchor::Beside });
			break;
		case EFurnitureRoom::Utility:
			OutProps.Add({ EFurnitureKind::UtilitySink, EPropAnchor::Wall });
			break;
		case EFurnitureRoom::Kitchen:
			OutProps.Add({ EFurnitureKind::Stove, EPropAnchor::Wall });
			OutProps.Add({ EFurnitureKind::KitchenSink, EPropAnchor::Beside });
			OutProps.Add({ EFurnitureKind::Fridge, EPropAnchor::Beside });
			break;
		case EFurnitureRoom::Dining:
			OutProps.Add({ EFurnitureKind::DiningTable, EPropAnchor::Center });
			OutProps.Add({ EFurnitureKind::DiningChair, EPropAnchor::Around });
			break;
		case EFurnitureRoom::Living:
			OutProps.Add({ EFurnitureKind::Sofa, EPropAnchor::Wall });
			OutProps.Add({ EFurnitureKind::CoffeeTable, EPropAnchor::InFront });
			OutProps.Add({ EFurnitureKind::Television, EPropAnchor::Opposite });
			OutProps.Add({ EFurnitureKind::Plant, EPropAnchor::Corner });
			break;
		case EFurnitureRoom::Parking:
			OutProps.Add({ EFurnitureKind::Car, EPropAnchor::LongAxis });
			break;
		default:
			break;
		}
	}

	bool TryNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double& OutValue)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			if (Field.Key.Equals(Name, ESearchCase::IgnoreCase) && Field.Value.IsValid() && Field.Value->Type == EJson::Number)
			{
				OutValue = Field.Value->AsNumber();
				return true;
			}
		}
		return false;
	}

	TSharedPtr<FJsonValue> FindField(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Names)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			for (const TCHAR* Name : Names)
			{
				if (Field.Key.Equals(Name, ESearchCase::IgnoreCase) && Field.Value.IsValid())
				{
					return Field.Value;
				}
			}
		}
		return nullptr;
	}

	bool ReadStringField(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Names, FString& OutValue)
	{
		for (const TCHAR* Name : Names)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
			{
				if (Field.Key.Equals(Name, ESearchCase::IgnoreCase) && Field.Value.IsValid() && Field.Value->Type == EJson::String)
				{
					OutValue = Field.Value->AsString();
					if (!OutValue.IsEmpty())
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	bool ReadVector2(const TSharedPtr<FJsonValue>& Value, float& OutX, float& OutY)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
			if (Values.Num() < 2 || !Values[0].IsValid() || !Values[1].IsValid())
			{
				return false;
			}
			OutX = static_cast<float>(Values[0]->AsNumber());
			OutY = static_cast<float>(Values[1]->AsNumber());
			return true;
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double X = 0.0;
			double Y = 0.0;
			const bool bX = TryNumber(Object, TEXT("x"), X) || TryNumber(Object, TEXT("X"), X) || TryNumber(Object, TEXT("width"), X) || TryNumber(Object, TEXT("east"), X);
			const bool bY = TryNumber(Object, TEXT("y"), Y) || TryNumber(Object, TEXT("Y"), Y) || TryNumber(Object, TEXT("depth"), Y) || TryNumber(Object, TEXT("south"), Y);
			if (!bX || !bY)
			{
				return false;
			}
			OutX = static_cast<float>(X);
			OutY = static_cast<float>(Y);
			return true;
		}
		return false;
	}

	double UnitToCentimeters(const FString& Units)
	{
		if (Units.Equals(TEXT("m"), ESearchCase::IgnoreCase) || Units.Equals(TEXT("meter"), ESearchCase::IgnoreCase) || Units.Equals(TEXT("meters"), ESearchCase::IgnoreCase))
		{
			return 100.0;
		}
		if (Units.Equals(TEXT("ft"), ESearchCase::IgnoreCase) || Units.Equals(TEXT("foot"), ESearchCase::IgnoreCase) || Units.Equals(TEXT("feet"), ESearchCase::IgnoreCase))
		{
			return 30.48;
		}
		return 1.0;
	}

	FString CompactName(const FString& Name)
	{
		FString Normalized = Name;
		Normalized.ReplaceInline(TEXT("_"), TEXT(" "));
		Normalized.ReplaceInline(TEXT("-"), TEXT(" "));
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		return Normalized;
	}

	bool MatchCompact(const FString& Compact, const TCHAR* Phrase)
	{
		const FString Needle(Phrase);
		if (Needle.Len() <= 3)
		{
			return Compact.Equals(Needle, ESearchCase::IgnoreCase);
		}
		return Compact.Contains(Needle, ESearchCase::IgnoreCase);
	}

	bool IsHallName(const FString& Name)
	{
		const FString Compact = CompactName(Name);
		return MatchCompact(Compact, TEXT("hallway"))
			|| MatchCompact(Compact, TEXT("corridor"))
			|| MatchCompact(Compact, TEXT("passage"))
			|| MatchCompact(Compact, TEXT("lobby"))
			|| MatchCompact(Compact, TEXT("foyer"))
			|| MatchCompact(Compact, TEXT("hall"));
	}

	bool ClassifyRoomName(const FString& Name, EFurnitureRoom& OutRoom)
	{
		const FString Compact = CompactName(Name);
		if (MatchCompact(Compact, TEXT("bedroom")))
		{
			OutRoom = EFurnitureRoom::Bedroom;
			return true;
		}
		if (MatchCompact(Compact, TEXT("bathroom")) || MatchCompact(Compact, TEXT("restroom")) || MatchCompact(Compact, TEXT("toilet")) || MatchCompact(Compact, TEXT("wc")))
		{
			OutRoom = EFurnitureRoom::Toilet;
			return true;
		}
		if (MatchCompact(Compact, TEXT("utility")))
		{
			OutRoom = EFurnitureRoom::Utility;
			return true;
		}
		if (MatchCompact(Compact, TEXT("kitchen")))
		{
			OutRoom = EFurnitureRoom::Kitchen;
			return true;
		}
		if (MatchCompact(Compact, TEXT("dining")))
		{
			OutRoom = EFurnitureRoom::Dining;
			return true;
		}
		if (MatchCompact(Compact, TEXT("living")) || MatchCompact(Compact, TEXT("lounge")))
		{
			OutRoom = EFurnitureRoom::Living;
			return true;
		}
		if (MatchCompact(Compact, TEXT("parking")) || MatchCompact(Compact, TEXT("garage")) || MatchCompact(Compact, TEXT("carport")))
		{
			OutRoom = EFurnitureRoom::Parking;
			return true;
		}
		return false;
	}

	bool ParseWall(const FString& Text, EPlanWall& OutWall)
	{
		if (Text.Equals(TEXT("north"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("n"), ESearchCase::IgnoreCase))
		{
			OutWall = EPlanWall::North;
			return true;
		}
		if (Text.Equals(TEXT("east"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("e"), ESearchCase::IgnoreCase))
		{
			OutWall = EPlanWall::East;
			return true;
		}
		if (Text.Equals(TEXT("south"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("s"), ESearchCase::IgnoreCase))
		{
			OutWall = EPlanWall::South;
			return true;
		}
		if (Text.Equals(TEXT("west"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("w"), ESearchCase::IgnoreCase))
		{
			OutWall = EPlanWall::West;
			return true;
		}
		return false;
	}

	bool BuildGrid(const FSpaceSpec& Space, FRoomGrid& Grid, FString& OutError)
	{
		Grid = FRoomGrid();
		Grid.OriginX = Space.OriginX;
		Grid.OriginY = Space.OriginY;
		Grid.Doors = Space.Doors;
		Grid.CellsX = CellsForCm(Space.SizeX);
		Grid.CellsY = CellsForCm(Space.SizeY);
		if (Grid.CellsX < 3 || Grid.CellsY < 3)
		{
			OutError = FString::Printf(TEXT("%s is smaller than the 10 cm wall grid"), *Space.Name);
			return false;
		}
		if (Grid.CellsX > MaxCells || Grid.CellsY > MaxCells)
		{
			OutError = FString::Printf(TEXT("%s is too large for a 10 cm grid"), *Space.Name);
			return false;
		}

		Grid.Cells.SetNum(Grid.CellsX * Grid.CellsY);
		for (ECell& Cell : Grid.Cells)
		{
			Cell = ECell::Free;
		}
		for (int32 X = 0; X < Grid.CellsX; ++X)
		{
			Grid.Cells[X] = ECell::Wall;
			Grid.Cells[X + (Grid.CellsY - 1) * Grid.CellsX] = ECell::Wall;
		}
		for (int32 Y = 0; Y < Grid.CellsY; ++Y)
		{
			Grid.Cells[Y * Grid.CellsX] = ECell::Wall;
			Grid.Cells[(Grid.CellsX - 1) + Y * Grid.CellsX] = ECell::Wall;
		}

		auto MarkSpan = [&Grid](EPlanWall Wall, int32 Start, int32 End, int32 SwingCells)
		{
			Start = FMath::Clamp(Start, 0, (Wall == EPlanWall::North || Wall == EPlanWall::South) ? Grid.CellsX : Grid.CellsY);
			End = FMath::Clamp(End, Start, (Wall == EPlanWall::North || Wall == EPlanWall::South) ? Grid.CellsX : Grid.CellsY);
			for (int32 Along = Start; Along < End; ++Along)
			{
				for (int32 Step = 1; Step <= SwingCells; ++Step)
				{
					int32 X = 0;
					int32 Y = 0;
					switch (Wall)
					{
					case EPlanWall::North: X = Along; Y = Step; break;
					case EPlanWall::South: X = Along; Y = Grid.CellsY - 1 - Step; break;
					case EPlanWall::West: X = Step; Y = Along; break;
					case EPlanWall::East: X = Grid.CellsX - 1 - Step; Y = Along; break;
					default: break;
					}
					Grid.SetIfFree(X, Y, ECell::Swing);
				}
			}
		};

		for (const FDoorSpec& Door : Grid.Doors)
		{
			const int32 Start = FMath::FloorToInt(Door.OffsetCm / CellCm);
			const int32 End = FMath::CeilToInt((Door.OffsetCm + Door.WidthCm) / CellCm - 0.001f);
			const int32 SwingCells = CellsForCm(Door.SwingCm);
			MarkSpan(Door.Wall, Start, End, SwingCells);
		}
		return true;
	}

	bool FindBestOnWall(const FRoomGrid& Grid, EPlanWall Wall, int32 AlongCells, int32 IntoCells, float AlongCm, float IntoCm, int32 DesiredAlong, FGridPose& OutPose)
	{
		const int32 Limit = (Wall == EPlanWall::North || Wall == EPlanWall::South) ? Grid.CellsX - 1 : Grid.CellsY - 1;
		bool bFound = false;
		int32 BestDistance = MAX_int32;
		int32 BestAlong = MAX_int32;
		for (int32 Along = 1; Along + AlongCells <= Limit; ++Along)
		{
			FGridPose Pose;
			Pose.Wall = Wall;
			Pose.Yaw = YawIntoRoom(Wall);
			Pose.LocalXCm = IntoCm;
			Pose.LocalYCm = AlongCm;
			switch (Wall)
			{
			case EPlanWall::North:
				Pose.MinX = Along;
				Pose.MinY = 1;
				Pose.SizeX = AlongCells;
				Pose.SizeY = IntoCells;
				break;
			case EPlanWall::South:
				Pose.MinX = Along;
				Pose.MinY = Grid.CellsY - 1 - IntoCells;
				Pose.SizeX = AlongCells;
				Pose.SizeY = IntoCells;
				break;
			case EPlanWall::West:
				Pose.MinX = 1;
				Pose.MinY = Along;
				Pose.SizeX = IntoCells;
				Pose.SizeY = AlongCells;
				break;
			case EPlanWall::East:
				Pose.MinX = Grid.CellsX - 1 - IntoCells;
				Pose.MinY = Along;
				Pose.SizeX = IntoCells;
				Pose.SizeY = AlongCells;
				break;
			default:
				break;
			}

			if (Pose.MinX < 1 || Pose.MinY < 1 || Pose.MinX + Pose.SizeX >= Grid.CellsX || Pose.MinY + Pose.SizeY >= Grid.CellsY)
			{
				continue;
			}
			if (!Grid.Fits(Pose))
			{
				continue;
			}

			bool bTouchesWall = true;
			switch (Wall)
			{
			case EPlanWall::North:
				for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
				{
					bTouchesWall &= Grid.Get(X, Pose.MinY - 1) == ECell::Wall;
				}
				break;
			case EPlanWall::South:
				for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
				{
					bTouchesWall &= Grid.Get(X, Pose.MinY + Pose.SizeY) == ECell::Wall;
				}
				break;
			case EPlanWall::West:
				for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
				{
					bTouchesWall &= Grid.Get(Pose.MinX - 1, Y) == ECell::Wall;
				}
				break;
			case EPlanWall::East:
				for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
				{
					bTouchesWall &= Grid.Get(Pose.MinX + Pose.SizeX, Y) == ECell::Wall;
				}
				break;
			default:
				bTouchesWall = false;
				break;
			}
			if (!bTouchesWall)
			{
				continue;
			}

			const int32 Distance = FMath::Abs(Along - DesiredAlong);
			if (!bFound || Distance < BestDistance || (Distance == BestDistance && Along < BestAlong))
			{
				OutPose = Pose;
				OutPose.bValid = true;
				bFound = true;
				BestDistance = Distance;
				BestAlong = Along;
			}
		}
		return bFound;
	}

	TArray<EPlanWall> WallOrder(const FRoomGrid& Grid, EPlanWall Preferred)
	{
		TArray<EPlanWall> Order;
		Order.Add(Preferred);
		const EPlanWall All[] = { EPlanWall::North, EPlanWall::East, EPlanWall::South, EPlanWall::West };
		for (EPlanWall Wall : All)
		{
			if (Wall != Preferred)
			{
				Order.Add(Wall);
			}
		}
		return Order;
	}

	EPlanWall PreferredPrimaryWall(const FRoomGrid& Grid)
	{
		if (Grid.Doors.Num() > 0)
		{
			return OppositeWall(Grid.Doors[0].Wall);
		}
		return EPlanWall::North;
	}

	bool PlaceOnWalls(const FRoomGrid& Grid, const TArray<EPlanWall>& Order, int32 AlongCells, int32 IntoCells, float AlongCm, float IntoCm, int32 DesiredAlong, bool bUseDesiredOnFirstWall, FGridPose& OutPose)
	{
		for (int32 Index = 0; Index < Order.Num(); ++Index)
		{
			const int32 Desired = (Index == 0 && bUseDesiredOnFirstWall) ? DesiredAlong : 1;
			if (FindBestOnWall(Grid, Order[Index], AlongCells, IntoCells, AlongCm, IntoCm, Desired, OutPose))
			{
				return true;
			}
		}
		return false;
	}

	bool FindCentered(const FRoomGrid& Grid, int32 SizeX, int32 SizeY, float Yaw, float LocalX, float LocalY, FGridPose& OutPose)
	{
		bool bFound = false;
		int32 BestScore = MAX_int32;
		for (int32 Y = 1; Y + SizeY < Grid.CellsY; ++Y)
		{
			for (int32 X = 1; X + SizeX < Grid.CellsX; ++X)
			{
				FGridPose Pose;
				Pose.MinX = X;
				Pose.MinY = Y;
				Pose.SizeX = SizeX;
				Pose.SizeY = SizeY;
				Pose.Yaw = Yaw;
				Pose.LocalXCm = LocalX;
				Pose.LocalYCm = LocalY;
				if (!Grid.Fits(Pose))
				{
					continue;
				}
				const int32 Score = FMath::Abs((X + SizeX / 2) - Grid.CellsX / 2) + FMath::Abs((Y + SizeY / 2) - Grid.CellsY / 2);
				if (!bFound || Score < BestScore || (Score == BestScore && (Y < OutPose.MinY || (Y == OutPose.MinY && X < OutPose.MinX))))
				{
					OutPose = Pose;
					OutPose.bValid = true;
					BestScore = Score;
					bFound = true;
				}
			}
		}
		return bFound;
	}

	void MeasureKind(EFurnitureKind Kind, float& OutWidthCm, float& OutDepthCm)
	{
		float WidthFeet = 2.f;
		float DepthFeet = 2.f;
		float Yaw = 0.f;
		FurnitureKindDefaultSize(Kind, WidthFeet, DepthFeet, Yaw);
		OutWidthCm = WidthFeet * 30.48f;
		OutDepthCm = DepthFeet * 30.48f;
	}

	void WallFootprint(EFurnitureKind Kind, float& OutAlongCm, float& OutIntoCm)
	{
		float WidthCm = 0.f;
		float DepthCm = 0.f;
		MeasureKind(Kind, WidthCm, DepthCm);
		if (Kind == EFurnitureKind::Television)
		{
			OutAlongCm = DepthCm;
			OutIntoCm = WidthCm;
			return;
		}
		OutAlongCm = WidthCm;
		OutIntoCm = DepthCm;
	}

	bool PlaceInFront(const FRoomGrid& Grid, const FGridPose& Primary, int32 ParallelCells, int32 AwayCells, float ParallelCm, float AwayCm, FGridPose& OutPose)
	{
		const int32 SlideLimit = FMath::Max(Grid.CellsX, Grid.CellsY);
		for (int32 Gap = 0; Gap <= 40; ++Gap)
		{
			for (int32 Step = 0; Step <= SlideLimit; ++Step)
			{
				const int32 Slides[] = { 0, Step, -Step };
				const int32 SlideCount = Step == 0 ? 1 : 3;
				for (int32 SlideIndex = 0; SlideIndex < SlideCount; ++SlideIndex)
				{
					if (Step != 0 && SlideIndex == 0)
					{
						continue;
					}
					const int32 Slide = Slides[SlideIndex];
					FGridPose Pose;
					Pose.Yaw = 0.f;
					Pose.Wall = Primary.Wall;
					switch (Primary.Wall)
					{
					case EPlanWall::North:
						Pose.MinX = (Primary.MinX + Primary.SizeX / 2) - ParallelCells / 2 + Slide;
						Pose.MinY = Primary.MinY + Primary.SizeY + Gap;
						Pose.SizeX = ParallelCells;
						Pose.SizeY = AwayCells;
						Pose.LocalXCm = ParallelCm;
						Pose.LocalYCm = AwayCm;
						break;
					case EPlanWall::South:
						Pose.MinX = (Primary.MinX + Primary.SizeX / 2) - ParallelCells / 2 + Slide;
						Pose.MinY = Primary.MinY - Gap - AwayCells;
						Pose.SizeX = ParallelCells;
						Pose.SizeY = AwayCells;
						Pose.LocalXCm = ParallelCm;
						Pose.LocalYCm = AwayCm;
						break;
					case EPlanWall::West:
						Pose.MinX = Primary.MinX + Primary.SizeX + Gap;
						Pose.MinY = (Primary.MinY + Primary.SizeY / 2) - ParallelCells / 2 + Slide;
						Pose.SizeX = AwayCells;
						Pose.SizeY = ParallelCells;
						Pose.LocalXCm = AwayCm;
						Pose.LocalYCm = ParallelCm;
						break;
					case EPlanWall::East:
						Pose.MinX = Primary.MinX - Gap - AwayCells;
						Pose.MinY = (Primary.MinY + Primary.SizeY / 2) - ParallelCells / 2 + Slide;
						Pose.SizeX = AwayCells;
						Pose.SizeY = ParallelCells;
						Pose.LocalXCm = AwayCm;
						Pose.LocalYCm = ParallelCm;
						break;
					default:
						break;
					}
					if (Grid.Fits(Pose))
					{
						OutPose = Pose;
						OutPose.bValid = true;
						return true;
					}
				}
			}
		}
		return false;
	}

	bool PlaceAround(FRoomGrid& Grid, const FGridPose& Table, float ChairCm, TArray<FGridPose>& OutChairs)
	{
		const int32 ChairCells = CellsForCm(ChairCm);
		struct FSide
		{
			EPlanWall Back;
			int32 DirX;
			int32 DirY;
		};
		const FSide Sides[] =
		{
			{ EPlanWall::North, 0, -1 },
			{ EPlanWall::East, 1, 0 },
			{ EPlanWall::South, 0, 1 },
			{ EPlanWall::West, -1, 0 }
		};

		for (const FSide& Side : Sides)
		{
			bool bPlaced = false;
			for (int32 Gap = 0; Gap <= 8 && !bPlaced; ++Gap)
			{
				for (int32 Step = 0; Step <= FMath::Max(Grid.CellsX, Grid.CellsY) && !bPlaced; ++Step)
				{
					const int32 Slides[] = { 0, Step, -Step };
					const int32 SlideCount = Step == 0 ? 1 : 3;
					for (int32 SlideIndex = 0; SlideIndex < SlideCount && !bPlaced; ++SlideIndex)
					{
						if (Step != 0 && SlideIndex == 0)
						{
							continue;
						}
						const int32 Slide = Slides[SlideIndex];
						FGridPose Pose;
						Pose.SizeX = ChairCells;
						Pose.SizeY = ChairCells;
						Pose.Yaw = YawIntoRoom(Side.Back);
						Pose.LocalXCm = ChairCm;
						Pose.LocalYCm = ChairCm;
						Pose.Wall = Side.Back;
						const int32 CenterX = Table.MinX + Table.SizeX / 2;
						const int32 CenterY = Table.MinY + Table.SizeY / 2;
						if (Side.DirY < 0)
						{
							Pose.MinX = CenterX - ChairCells / 2 + Slide;
							Pose.MinY = Table.MinY - Gap - ChairCells;
						}
						else if (Side.DirY > 0)
						{
							Pose.MinX = CenterX - ChairCells / 2 + Slide;
							Pose.MinY = Table.MinY + Table.SizeY + Gap;
						}
						else if (Side.DirX > 0)
						{
							Pose.MinX = Table.MinX + Table.SizeX + Gap;
							Pose.MinY = CenterY - ChairCells / 2 + Slide;
						}
						else
						{
							Pose.MinX = Table.MinX - Gap - ChairCells;
							Pose.MinY = CenterY - ChairCells / 2 + Slide;
						}
						if (!Grid.Fits(Pose))
						{
							continue;
						}
						Pose.bValid = true;
						Grid.Occupy(Pose);
						OutChairs.Add(Pose);
						bPlaced = true;
					}
				}
			}
		}
		return OutChairs.Num() > 0;
	}

	bool PlaceCorner(const FRoomGrid& Grid, int32 Size, float Cm, FGridPose& OutPose)
	{
		const int32 Corners[4][2] =
		{
			{ 1, 1 },
			{ Grid.CellsX - 1 - Size, 1 },
			{ 1, Grid.CellsY - 1 - Size },
			{ Grid.CellsX - 1 - Size, Grid.CellsY - 1 - Size }
		};
		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			FGridPose Pose;
			Pose.MinX = Corners[CornerIndex][0];
			Pose.MinY = Corners[CornerIndex][1];
			Pose.SizeX = Size;
			Pose.SizeY = Size;
			Pose.Yaw = 0.f;
			Pose.LocalXCm = Cm;
			Pose.LocalYCm = Cm;
			if (Pose.MinX < 1 || Pose.MinY < 1 || !Grid.Fits(Pose))
			{
				continue;
			}
			OutPose = Pose;
			OutPose.bValid = true;
			return true;
		}
		return false;
	}

	void EmitPose(const FSpaceSpec& Space, const FRoomGrid& Grid, const FGridPose& Pose, EFurnitureKind Kind, int32 Serial, TArray<FExportedObject>& OutObjects)
	{
		FExportedObject Object;
		Object.Id = FString::Printf(TEXT("%s_%d"), *KindToken(Kind), Space.Index + 1);
		if (Serial > 0)
		{
			Object.Id += FString::Printf(TEXT("_%d"), Serial);
		}
		Object.Category = FurnitureKindLabel(Kind);
		Object.Label = FurnitureKindLabel(Kind);
		Object.X = Grid.OriginX + (Pose.MinX + Pose.SizeX * 0.5f) * CellCm;
		Object.Y = Grid.OriginY + (Pose.MinY + Pose.SizeY * 0.5f) * CellCm;
		Object.Yaw = Pose.Yaw;
		Object.SizeX = Pose.LocalXCm;
		Object.SizeY = Pose.LocalYCm;
		OutObjects.Add(Object);
	}

	FString SpaceLabel(const FSpaceSpec& Space)
	{
		return FString::Printf(TEXT("%s %d"), *Space.Name, Space.Index + 1);
	}

	void PlaceSpace(const FSpaceSpec& Space, FRoomGrid& Grid, TArray<FExportedObject>& OutObjects, TArray<FString>& OutNotes, int32& OutPlaced)
	{
		TArray<FPropDef> Props;
		PropsForRoom(Space.Room, Props);
		bool bPrimaryPlaced = false;
		FGridPose PrimaryPose;
		FGridPose ChainPose;

		for (int32 PropIndex = 0; PropIndex < Props.Num(); ++PropIndex)
		{
			const FPropDef& Prop = Props[PropIndex];
			const bool bIsPrimary = PropIndex == 0;
			const FString PropName = FurnitureKindLabel(Prop.Kind);
			if (!bIsPrimary && !bPrimaryPlaced)
			{
				OutNotes.Add(FString::Printf(TEXT("%s %s skipped because the primary piece was not placed"), *SpaceLabel(Space), *PropName));
				continue;
			}

			if (Prop.Anchor == EPropAnchor::Around)
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				MeasureKind(Prop.Kind, WidthCm, DepthCm);
				TArray<FGridPose> Chairs;
				if (!PlaceAround(Grid, PrimaryPose, WidthCm, Chairs))
				{
					OutNotes.Add(FString::Printf(TEXT("%s %s does not fit"), *SpaceLabel(Space), *PropName));
					continue;
				}
				for (int32 ChairIndex = 0; ChairIndex < Chairs.Num(); ++ChairIndex)
				{
					EmitPose(Space, Grid, Chairs[ChairIndex], Prop.Kind, ChairIndex + 1, OutObjects);
					++OutPlaced;
				}
				continue;
			}

			FGridPose Pose;
			bool bPlaced = false;
			switch (Prop.Anchor)
			{
			case EPropAnchor::Wall:
			{
				float AlongCm = 0.f;
				float IntoCm = 0.f;
				WallFootprint(Prop.Kind, AlongCm, IntoCm);
				const TArray<EPlanWall> Order = WallOrder(Grid, PreferredPrimaryWall(Grid));
				bPlaced = PlaceOnWalls(Grid, Order, CellsForCm(AlongCm), CellsForCm(IntoCm), AlongCm, IntoCm, 1, false, Pose);
				break;
			}
			case EPropAnchor::Beside:
			{
				float AlongCm = 0.f;
				float IntoCm = 0.f;
				WallFootprint(Prop.Kind, AlongCm, IntoCm);
				const int32 EndAlong = (ChainPose.Wall == EPlanWall::North || ChainPose.Wall == EPlanWall::South)
					? ChainPose.MinX + ChainPose.SizeX
					: ChainPose.MinY + ChainPose.SizeY;
				const TArray<EPlanWall> Order = WallOrder(Grid, ChainPose.Wall);
				bPlaced = PlaceOnWalls(Grid, Order, CellsForCm(AlongCm), CellsForCm(IntoCm), AlongCm, IntoCm, EndAlong, true, Pose);
				break;
			}
			case EPropAnchor::InFront:
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				MeasureKind(Prop.Kind, WidthCm, DepthCm);
				const float ParallelCm = FMath::Max(WidthCm, DepthCm);
				const float AwayCm = FMath::Min(WidthCm, DepthCm);
				bPlaced = PlaceInFront(Grid, PrimaryPose, CellsForCm(ParallelCm), CellsForCm(AwayCm), ParallelCm, AwayCm, Pose);
				break;
			}
			case EPropAnchor::Opposite:
			{
				float AlongCm = 0.f;
				float IntoCm = 0.f;
				WallFootprint(Prop.Kind, AlongCm, IntoCm);
				const EPlanWall Wall = OppositeWall(PrimaryPose.Wall);
				const int32 AnchorCenter = (PrimaryPose.Wall == EPlanWall::North || PrimaryPose.Wall == EPlanWall::South)
					? PrimaryPose.MinX + PrimaryPose.SizeX / 2
					: PrimaryPose.MinY + PrimaryPose.SizeY / 2;
				const int32 Desired = AnchorCenter - CellsForCm(AlongCm) / 2;
				bPlaced = FindBestOnWall(Grid, Wall, CellsForCm(AlongCm), CellsForCm(IntoCm), AlongCm, IntoCm, Desired, Pose);
				break;
			}
			case EPropAnchor::Corner:
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				MeasureKind(Prop.Kind, WidthCm, DepthCm);
				const float Cm = FMath::Max(WidthCm, DepthCm);
				bPlaced = PlaceCorner(Grid, CellsForCm(Cm), Cm, Pose);
				break;
			}
			case EPropAnchor::Center:
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				MeasureKind(Prop.Kind, WidthCm, DepthCm);
				bPlaced = FindCentered(Grid, CellsForCm(WidthCm), CellsForCm(DepthCm), 0.f, WidthCm, DepthCm, Pose);
				break;
			}
			case EPropAnchor::LongAxis:
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				MeasureKind(Prop.Kind, WidthCm, DepthCm);
				const float LongCm = FMath::Max(WidthCm, DepthCm);
				const float ShortCm = FMath::Min(WidthCm, DepthCm);
				const bool bAlongY = Grid.CellsY >= Grid.CellsX;
				if (bAlongY)
				{
					bPlaced = FindCentered(Grid, CellsForCm(ShortCm), CellsForCm(LongCm), -90.f, LongCm, ShortCm, Pose);
				}
				else
				{
					bPlaced = FindCentered(Grid, CellsForCm(LongCm), CellsForCm(ShortCm), 0.f, LongCm, ShortCm, Pose);
				}
				break;
			}
			default:
				break;
			}

			if (!bPlaced)
			{
				OutNotes.Add(FString::Printf(TEXT("%s %s does not fit"), *SpaceLabel(Space), *PropName));
				continue;
			}

			Grid.Occupy(Pose);
			EmitPose(Space, Grid, Pose, Prop.Kind, 0, OutObjects);
			++OutPlaced;
			if (bIsPrimary)
			{
				bPrimaryPlaced = true;
				PrimaryPose = Pose;
				ChainPose = Pose;
			}
			else if (Prop.Anchor == EPropAnchor::Beside)
			{
				ChainPose = Pose;
			}
		}
	}

	bool ReadDoor(const TSharedPtr<FJsonObject>& Object, double UnitScale, FDoorSpec& OutDoor)
	{
		FString WallText;
		if (!ReadStringField(Object, { TEXT("wall"), TEXT("side") }, WallText) || !ParseWall(WallText, OutDoor.Wall))
		{
			return false;
		}
		double Offset = 0.0;
		double Width = 90.0;
		double Swing = 0.0;
		TryNumber(Object, TEXT("offset"), Offset) || TryNumber(Object, TEXT("along"), Offset) || TryNumber(Object, TEXT("start"), Offset);
		if (!TryNumber(Object, TEXT("width"), Width) && !TryNumber(Object, TEXT("span"), Width))
		{
			return false;
		}
		const bool bHasSwing = TryNumber(Object, TEXT("swing"), Swing) || TryNumber(Object, TEXT("swingDepth"), Swing);
		if (Width <= 0.0)
		{
			return false;
		}
		OutDoor.OffsetCm = static_cast<float>(Offset * UnitScale);
		OutDoor.WidthCm = static_cast<float>(Width * UnitScale);
		OutDoor.SwingCm = bHasSwing ? static_cast<float>(Swing * UnitScale) : OutDoor.WidthCm;
		return OutDoor.SwingCm > 0.f;
	}

	bool ReadSpace(const TSharedPtr<FJsonObject>& Object, double UnitScale, bool bForceHall, int32 Index, FSpaceSpec& OutSpace, FString& OutError)
	{
		OutSpace = FSpaceSpec();
		OutSpace.Index = Index;
		if (!ReadStringField(Object, { TEXT("name"), TEXT("room"), TEXT("label"), TEXT("category") }, OutSpace.Name))
		{
			OutError = TEXT("A room is missing a name");
			return false;
		}

		FString KindText;
		const bool bHasKind = ReadStringField(Object, { TEXT("kind"), TEXT("space") }, KindText);
		if (bForceHall || (bHasKind && IsHallName(KindText)) || IsHallName(OutSpace.Name))
		{
			OutSpace.bHall = true;
		}
		else if (ClassifyRoomName(OutSpace.Name, OutSpace.Room) || (bHasKind && ClassifyRoomName(KindText, OutSpace.Room)))
		{
			OutSpace.bKnownRoom = true;
		}

		float OriginX = 0.f;
		float OriginY = 0.f;
		float SizeX = 0.f;
		float SizeY = 0.f;
		const bool bHasOrigin = ReadVector2(FindField(Object, { TEXT("origin"), TEXT("position"), TEXT("location"), TEXT("min") }), OriginX, OriginY);
		if (!bHasOrigin)
		{
			double X = 0.0;
			double Y = 0.0;
			if (TryNumber(Object, TEXT("x"), X) && TryNumber(Object, TEXT("y"), Y))
			{
				OriginX = static_cast<float>(X);
				OriginY = static_cast<float>(Y);
			}
		}
		const bool bHasSize = ReadVector2(FindField(Object, { TEXT("size"), TEXT("dimensions"), TEXT("extent") }), SizeX, SizeY);
		if (!bHasSize)
		{
			double Width = 0.0;
			double Depth = 0.0;
			if (TryNumber(Object, TEXT("width"), Width) && (TryNumber(Object, TEXT("depth"), Depth) || TryNumber(Object, TEXT("height"), Depth)))
			{
				SizeX = static_cast<float>(Width);
				SizeY = static_cast<float>(Depth);
			}
		}
		if (SizeX <= 1.f || SizeY <= 1.f)
		{
			OutError = FString::Printf(TEXT("%s is missing a size"), *OutSpace.Name);
			return false;
		}

		OutSpace.OriginX = OriginX * static_cast<float>(UnitScale);
		OutSpace.OriginY = OriginY * static_cast<float>(UnitScale);
		OutSpace.SizeX = SizeX * static_cast<float>(UnitScale);
		OutSpace.SizeY = SizeY * static_cast<float>(UnitScale);

		if (TSharedPtr<FJsonValue> DoorsValue = FindField(Object, { TEXT("doors"), TEXT("openings") }))
		{
			TArray<TSharedPtr<FJsonValue>> DoorValues;
			if (DoorsValue->Type == EJson::Array)
			{
				DoorValues = DoorsValue->AsArray();
			}
			else if (DoorsValue->Type == EJson::Object)
			{
				DoorValues.Add(DoorsValue);
			}
			for (const TSharedPtr<FJsonValue>& DoorValue : DoorValues)
			{
				if (!DoorValue.IsValid() || DoorValue->Type != EJson::Object)
				{
					continue;
				}
				FDoorSpec Door;
				if (ReadDoor(DoorValue->AsObject(), UnitScale, Door))
				{
					OutSpace.Doors.Add(Door);
				}
			}
		}
		return true;
	}

	void ReadSpaceArray(const TSharedPtr<FJsonValue>& Value, double UnitScale, bool bForceHall, TArray<FSpaceSpec>& OutSpaces, TArray<FString>& OutNotes)
	{
		if (!Value.IsValid() || Value->Type != EJson::Array)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			if (!Entry.IsValid() || Entry->Type != EJson::Object)
			{
				continue;
			}
			FSpaceSpec Space;
			FString Error;
			if (!ReadSpace(Entry->AsObject(), UnitScale, bForceHall, OutSpaces.Num(), Space, Error))
			{
				if (!Error.IsEmpty())
				{
					OutNotes.Add(Error);
				}
				continue;
			}
			OutSpaces.Add(Space);
		}
	}

	FString BuildExportJson(const TArray<FExportedObject>& Objects, const TArray<FString>& Notes)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("units"), TEXT("cm"));
		Root->SetStringField(TEXT("source"), TEXT("compute"));

		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FExportedObject& Object : Objects)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("id"), Object.Id);
			Item->SetStringField(TEXT("type"), Object.Category);
			Item->SetStringField(TEXT("label"), Object.Label);

			TArray<TSharedPtr<FJsonValue>> Position;
			Position.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.X)));
			Position.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.Y)));
			Position.Add(MakeShared<FJsonValueNumber>(0.0));
			Item->SetArrayField(TEXT("position"), Position);

			TSharedRef<FJsonObject> Rotation = MakeShared<FJsonObject>();
			Rotation->SetNumberField(TEXT("pitch"), 0.0);
			Rotation->SetNumberField(TEXT("yaw"), static_cast<double>(Object.Yaw));
			Rotation->SetNumberField(TEXT("roll"), 0.0);
			Item->SetObjectField(TEXT("rotation"), Rotation);
			Item->SetNumberField(TEXT("scale"), 1.0);

			TArray<TSharedPtr<FJsonValue>> Size;
			Size.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.SizeX)));
			Size.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.SizeY)));
			Item->SetArrayField(TEXT("size"), Size);
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Root->SetArrayField(TEXT("objects"), Items);

		TArray<TSharedPtr<FJsonValue>> NoteValues;
		for (const FString& Note : Notes)
		{
			NoteValues.Add(MakeShared<FJsonValueString>(Note));
		}
		Root->SetArrayField(TEXT("notes"), NoteValues);

		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}
}

FFurnitureComputeResult ComputeFurnitureAssembly(const FString& JsonText)
{
	FFurnitureComputeResult Result;
	TSharedPtr<FJsonValue> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || Root->Type != EJson::Object)
	{
		Result.Message = TEXT("This JSON does not contain object detection data or a floorplan of rooms and halls.");
		return Result;
	}

	const TSharedPtr<FJsonObject> RootObject = Root->AsObject();
	const TSharedPtr<FJsonValue> RoomsValue = FindField(RootObject, { TEXT("rooms") });
	const TSharedPtr<FJsonValue> HallsValue = FindField(RootObject, { TEXT("halls") });
	const TSharedPtr<FJsonValue> SpacesValue = FindField(RootObject, { TEXT("spaces") });
	if (!RoomsValue.IsValid() && !HallsValue.IsValid() && !SpacesValue.IsValid())
	{
		Result.Message = TEXT("This JSON does not contain object detection data. Expected a list of objects with a type, position, and rotation.");
		return Result;
	}

	Result.bIsFloorplan = true;
	FString Units = TEXT("cm");
	if (TSharedPtr<FJsonValue> UnitsValue = FindField(RootObject, { TEXT("units") }))
	{
		if (UnitsValue->Type == EJson::String)
		{
			Units = UnitsValue->AsString();
		}
	}
	const double UnitScale = UnitToCentimeters(Units);

	TArray<FString> Notes;
	TArray<FSpaceSpec> Spaces;
	ReadSpaceArray(RoomsValue, UnitScale, false, Spaces, Notes);
	ReadSpaceArray(HallsValue, UnitScale, true, Spaces, Notes);
	ReadSpaceArray(SpacesValue, UnitScale, false, Spaces, Notes);
	if (Spaces.Num() == 0)
	{
		Result.Message = Notes.Num() > 0
			? FString::Printf(TEXT("Compute Assembly found a floorplan, but no room could be read. %s."), *FString::Join(Notes, TEXT("; ")))
			: TEXT("Compute Assembly found a floorplan, but it has no rooms or halls.");
		UE_LOG(LogFurnitureCompute, Log, TEXT("%s"), *Result.Message);
		return Result;
	}

	TArray<FExportedObject> Objects;
	int32 Placed = 0;
	int32 RoomCount = 0;
	int32 HallCount = 0;
	for (const FSpaceSpec& Space : Spaces)
	{
		if (Space.bHall)
		{
			++HallCount;
		}
		else
		{
			++RoomCount;
		}

		FRoomGrid Grid;
		FString GridError;
		if (!BuildGrid(Space, Grid, GridError))
		{
			Notes.Add(GridError);
			continue;
		}
		if (Space.bHall)
		{
			continue;
		}
		if (!Space.bKnownRoom)
		{
			Notes.Add(FString::Printf(TEXT("%s is an unknown room, so no props were chosen"), *SpaceLabel(Space)));
			continue;
		}
		PlaceSpace(Space, Grid, Objects, Notes, Placed);
	}

	Result.Message = FString::Printf(TEXT("Read %d rooms and %d halls. Placed %d objects."), RoomCount, HallCount, Placed);
	if (Notes.Num() > 0)
	{
		Result.Message += FString::Printf(TEXT(" Skipped: %s."), *FString::Join(Notes, TEXT("; ")));
	}
	if (Objects.Num() > 0)
	{
		Result.ExportedJson = BuildExportJson(Objects, Notes);
	}

	UE_LOG(LogFurnitureCompute, Log, TEXT("%s"), *Result.Message);
	for (const FString& Note : Notes)
	{
		UE_LOG(LogFurnitureCompute, Log, TEXT("Skip: %s"), *Note);
	}
	return Result;
}
