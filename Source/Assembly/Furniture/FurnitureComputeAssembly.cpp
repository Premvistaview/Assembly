#include "Furniture/FurnitureComputeAssembly.h"

#include "Furniture/FurnitureCatalog.h"
#include "Furniture/FurnitureTypes.h"
#include "Engine/StaticMesh.h"
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
		Window,
		Clearance,
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
		LongAxis,
		/** Television plus its stand and the pieces that sit on the stand. */
		TvUnit,
		/** Centered dining table with chairs spaced around it. */
		DiningSet
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
		/** Bottom of this opening above the floor. Doors sit on the ground. Windows default higher. */
		float SillCm = 0.f;
		float HeightCm = 210.f;
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
		float TotalSizeX = 0.f;
		float TotalSizeY = 0.f;
		bool bHasTotalSize = false;
		/** 0 picks automatically, 1 forces a wall-mounted TV, 2 prefers a TV stand. */
		int32 TvMode = 0;
		TArray<FDoorSpec> Doors;
		TArray<FDoorSpec> Windows;
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
		float OriginZ = 0.f;
		/** Usable east-west and north-south size after swing, in centimeters. Zero uses the wall grid. */
		float UsableEastWest = 0.f;
		float UsableNorthSouth = 0.f;
		int32 CellsX = 0;
		int32 CellsY = 0;
		int32 CellsZ = 1;
		TArray<ECell> Cells;
		TArray<FDoorSpec> Doors;
		TArray<FDoorSpec> Windows;
		bool bRelaxedClearance = false;

		/** Raised placement. When active, Fits also needs the cubes between Z0 and Z1 to be free. */
		struct FLevel
		{
			bool bActive = false;
			bool bFloor = true;
			int32 Z0 = 0;
			int32 Z1 = 0;
		};
		FLevel Level;

		int32 IndexOf(int32 X, int32 Y, int32 Z) const
		{
			return X + Y * CellsX + Z * CellsX * CellsY;
		}

		bool InBounds(int32 X, int32 Y) const
		{
			return X >= 0 && Y >= 0 && X < CellsX && Y < CellsY;
		}

		bool InBounds(int32 X, int32 Y, int32 Z) const
		{
			return InBounds(X, Y) && Z >= 0 && Z < CellsZ;
		}

		/** Ground layer. Props are anchored on z = 0. */
		ECell Get(int32 X, int32 Y) const
		{
			return Get(X, Y, 0);
		}

		ECell Get(int32 X, int32 Y, int32 Z) const
		{
			if (!InBounds(X, Y, Z))
			{
				return ECell::Wall;
			}
			return Cells[IndexOf(X, Y, Z)];
		}

		void SetIfFree(int32 X, int32 Y, ECell Value)
		{
			SetIfFree(X, Y, 0, Value);
		}

		void SetIfFree(int32 X, int32 Y, int32 Z, ECell Value)
		{
			if (!InBounds(X, Y, Z))
			{
				return;
			}
			ECell& Cell = Cells[IndexOf(X, Y, Z)];
			if (Cell == ECell::Free)
			{
				Cell = Value;
			}
		}

		bool Fits(const FGridPose& Pose) const
		{
			if (Pose.SizeX < 1 || Pose.SizeY < 1)
			{
				return false;
			}
			if (!Level.bActive || Level.bFloor)
			{
				for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
				{
					for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
					{
						const ECell Cell = Get(X, Y);
						if (Cell == ECell::Free || (bRelaxedClearance && Cell == ECell::Clearance))
						{
							continue;
						}
						return false;
					}
				}
			}
			return !Level.bActive || FitsRange(Pose, Level.Z0, Level.Z1);
		}

		/** True when every cube of the footprint between Z0 and Z1 is free of windows, swings, and other mounted pieces. */
		bool FitsRange(const FGridPose& Pose, int32 Z0, int32 Z1) const
		{
			for (int32 Z = Z0; Z < Z1; ++Z)
			{
				for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
				{
					for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
					{
						if (Get(X, Y, Z) != ECell::Free)
						{
							return false;
						}
					}
				}
			}
			return true;
		}

		void OccupyRange(const FGridPose& Pose, int32 Z0, int32 Z1)
		{
			for (int32 Z = Z0; Z < Z1; ++Z)
			{
				for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
				{
					for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
					{
						if (InBounds(X, Y, Z))
						{
							Cells[IndexOf(X, Y, Z)] = ECell::Occupied;
						}
					}
				}
			}
		}

		void Occupy(const FGridPose& Pose)
		{
			for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
			{
				for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
				{
					if (!InBounds(X, Y))
					{
						continue;
					}
					ECell& Cell = Cells[IndexOf(X, Y, 0)];
					if (Cell == ECell::Free || Cell == ECell::Clearance)
					{
						Cell = ECell::Occupied;
					}
				}
			}

			constexpr int32 Margin = 1;
			for (int32 Y = Pose.MinY - Margin; Y < Pose.MinY + Pose.SizeY + Margin; ++Y)
			{
				for (int32 X = Pose.MinX - Margin; X < Pose.MinX + Pose.SizeX + Margin; ++X)
				{
					SetIfFree(X, Y, ECell::Clearance);
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
		float Z = 0.f;
		float Yaw = 0.f;
		float SizeX = 0.f;
		float SizeY = 0.f;
		float SizeZ = 0.f;
		float UniformScale = 1.f;
	};

	/** Unscaled catalog mesh. X is depth, Y is width, Z is height. */
	struct FResolvedMesh
	{
		bool bValid = false;
		float X = 0.f;
		float Y = 0.f;
		float Z = 0.f;
	};

	const TArray<FFurnitureCatalogMeshSize>* ActiveMeshSizes = nullptr;

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
		case EPlanWall::North: return 90.f;
		case EPlanWall::South: return -90.f;
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
		case EFurnitureKind::BedLamp: return TEXT("BedLamp");
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
		case EFurnitureKind::TvStand: return TEXT("TvStand");
		case EFurnitureKind::Speaker: return TEXT("Speaker");
		case EFurnitureKind::DvdPlayer: return TEXT("DvdPlayer");
		case EFurnitureKind::GameConsole: return TEXT("GameConsole");
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
			OutProps.Add({ EFurnitureKind::BedLamp, EPropAnchor::Beside });
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
			OutProps.Add({ EFurnitureKind::DiningTable, EPropAnchor::DiningSet });
			break;
		case EFurnitureRoom::Living:
			OutProps.Add({ EFurnitureKind::Sofa, EPropAnchor::Wall });
			OutProps.Add({ EFurnitureKind::CoffeeTable, EPropAnchor::InFront });
			OutProps.Add({ EFurnitureKind::Television, EPropAnchor::TvUnit });
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

	bool BuildGrid(const FSpaceSpec& Space, float PlanOriginX, float PlanOriginY, float PlanOriginZ, FRoomGrid& Grid, FString& OutError)
	{
		Grid = FRoomGrid();
		Grid.OriginX = PlanOriginX + Space.OriginX;
		Grid.OriginY = PlanOriginY + Space.OriginY;
		Grid.OriginZ = PlanOriginZ;
		Grid.UsableEastWest = Space.bHasTotalSize ? Space.TotalSizeX : 0.f;
		Grid.UsableNorthSouth = Space.bHasTotalSize ? Space.TotalSizeY : 0.f;
		Grid.Doors = Space.Doors;
		Grid.Windows = Space.Windows;
		Grid.CellsX = CellsForCm(Space.SizeX);
		Grid.CellsY = CellsForCm(Space.SizeY);
		Grid.CellsZ = FMath::Clamp(CellsForCm(300.f), 3, 36);
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
		const int64 Volume = static_cast<int64>(Grid.CellsX) * Grid.CellsY * Grid.CellsZ;
		if (Volume > 1500000)
		{
			OutError = FString::Printf(TEXT("%s is too large for a 10 cm cube grid"), *Space.Name);
			return false;
		}

		Grid.Cells.SetNum(static_cast<int32>(Volume));
		for (ECell& Cell : Grid.Cells)
		{
			Cell = ECell::Free;
		}
		for (int32 Z = 0; Z < Grid.CellsZ; ++Z)
		{
			for (int32 X = 0; X < Grid.CellsX; ++X)
			{
				Grid.Cells[Grid.IndexOf(X, 0, Z)] = ECell::Wall;
				Grid.Cells[Grid.IndexOf(X, Grid.CellsY - 1, Z)] = ECell::Wall;
			}
			for (int32 Y = 0; Y < Grid.CellsY; ++Y)
			{
				Grid.Cells[Grid.IndexOf(0, Y, Z)] = ECell::Wall;
				Grid.Cells[Grid.IndexOf(Grid.CellsX - 1, Y, Z)] = ECell::Wall;
			}
		}

		auto MarkVolume = [&Grid](EPlanWall Wall, int32 Start, int32 End, int32 DepthCells, int32 Z0, int32 Z1, ECell Blocked)
		{
			Start = FMath::Clamp(Start, 0, (Wall == EPlanWall::North || Wall == EPlanWall::South) ? Grid.CellsX : Grid.CellsY);
			End = FMath::Clamp(End, Start, (Wall == EPlanWall::North || Wall == EPlanWall::South) ? Grid.CellsX : Grid.CellsY);
			Z0 = FMath::Clamp(Z0, 0, Grid.CellsZ);
			Z1 = FMath::Clamp(Z1, Z0, Grid.CellsZ);
			for (int32 Z = Z0; Z < Z1; ++Z)
			{
				for (int32 Along = Start; Along < End; ++Along)
				{
					for (int32 Step = 1; Step <= DepthCells; ++Step)
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
						Grid.SetIfFree(X, Y, Z, Blocked);
					}
				}
			}
		};

		for (const FDoorSpec& Door : Grid.Doors)
		{
			const int32 Start = FMath::FloorToInt(Door.OffsetCm / CellCm);
			const int32 End = FMath::CeilToInt((Door.OffsetCm + Door.WidthCm) / CellCm - 0.001f);
			const int32 SwingCells = CellsForCm(Door.SwingCm);
			const int32 HeightCells = FMath::Max(1, CellsForCm(Door.HeightCm));
			MarkVolume(Door.Wall, Start, End, SwingCells, 0, HeightCells, ECell::Swing);
		}
		for (const FDoorSpec& Window : Grid.Windows)
		{
			const int32 Start = FMath::FloorToInt(Window.OffsetCm / CellCm);
			const int32 End = FMath::CeilToInt((Window.OffsetCm + Window.WidthCm) / CellCm - 0.001f);
			const int32 DepthCells = CellsForCm(Window.SwingCm);
			const int32 SillCells = FMath::Max(0, FMath::FloorToInt(Window.SillCm / CellCm));
			const int32 HeightCells = FMath::Max(1, CellsForCm(Window.HeightCm));
			MarkVolume(Window.Wall, Start, End, DepthCells, SillCells, SillCells + HeightCells, ECell::Window);
		}
		return true;
	}

	bool FindBestOnWall(const FRoomGrid& Grid, EPlanWall Wall, int32 AlongCells, int32 IntoCells, float AlongCm, float IntoCm, int32 DesiredAlong, FGridPose& OutPose, TFunction<bool(const FGridPose&)> Accept = nullptr)
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
			if (Accept && !Accept(Pose))
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

	const TCHAR* WallLabel(EPlanWall Wall)
	{
		switch (Wall)
		{
		case EPlanWall::North: return TEXT("north");
		case EPlanWall::East: return TEXT("east");
		case EPlanWall::South: return TEXT("south");
		case EPlanWall::West: return TEXT("west");
		default: return TEXT("north");
		}
	}

	/** Viewing wall first, then the nearer perpendicular wall, then the other perpendicular wall, then the far wall. */
	TArray<EPlanWall> TvWallOrder(const FRoomGrid& Grid, const FGridPose& Sofa, EPlanWall Preferred)
	{
		const bool bAlongX = Preferred == EPlanWall::North || Preferred == EPlanWall::South;
		const int32 SofaCenter = bAlongX ? Sofa.MinX + Sofa.SizeX / 2 : Sofa.MinY + Sofa.SizeY / 2;
		const int32 RoomCenter = bAlongX ? Grid.CellsX / 2 : Grid.CellsY / 2;
		const bool bLowSide = SofaCenter < RoomCenter;
		EPlanWall NearPerp = EPlanWall::East;
		EPlanWall FarPerp = EPlanWall::West;
		if (bAlongX)
		{
			NearPerp = bLowSide ? EPlanWall::West : EPlanWall::East;
			FarPerp = bLowSide ? EPlanWall::East : EPlanWall::West;
		}
		else
		{
			NearPerp = bLowSide ? EPlanWall::North : EPlanWall::South;
			FarPerp = bLowSide ? EPlanWall::South : EPlanWall::North;
		}
		TArray<EPlanWall> Order;
		Order.Add(Preferred);
		Order.Add(NearPerp);
		Order.Add(FarPerp);
		Order.Add(OppositeWall(Preferred));
		return Order;
	}

	/** True when this pose's wall span crosses a window or door in the television's height band. */
	bool TvSpanHitsOpening(const FRoomGrid& Grid, const FGridPose& Pose, float BottomCm, float TopCm)
	{
		const bool bAlongX = Pose.Wall == EPlanWall::North || Pose.Wall == EPlanWall::South;
		const float Along0 = (bAlongX ? Pose.MinX : Pose.MinY) * CellCm;
		const float Along1 = Along0 + (bAlongX ? Pose.SizeX : Pose.SizeY) * CellCm;
		auto Hits = [&](const TArray<FDoorSpec>& Openings)
		{
			for (const FDoorSpec& Opening : Openings)
			{
				if (Opening.Wall != Pose.Wall)
				{
					continue;
				}
				const float Open0 = Opening.OffsetCm;
				const float Open1 = Opening.OffsetCm + Opening.WidthCm;
				const float OpenBottom = Opening.SillCm;
				const float OpenTop = Opening.SillCm + Opening.HeightCm;
				if (Along0 < Open1 && Along1 > Open0 && BottomCm < OpenTop && TopCm > OpenBottom)
				{
					return true;
				}
			}
			return false;
		};
		return Hits(Grid.Windows) || Hits(Grid.Doors);
	}

	FResolvedMesh ResolveCatalogMesh(const TArray<FFurnitureCatalogMeshSize>& MeshSizes, std::initializer_list<const TCHAR*> Names)
	{
		for (const FFurnitureCatalogMeshSize& Size : MeshSizes)
		{
			if (Size.ExtentCm.X <= 1.f || Size.ExtentCm.Y <= 1.f || Size.ExtentCm.Z <= 1.f)
			{
				continue;
			}
			for (const TCHAR* Name : Names)
			{
				if (Size.Category.Equals(Name, ESearchCase::IgnoreCase))
				{
					FResolvedMesh Mesh;
					Mesh.bValid = true;
					Mesh.X = Size.ExtentCm.X;
					Mesh.Y = Size.ExtentCm.Y;
					Mesh.Z = Size.ExtentCm.Z;
					return Mesh;
				}
			}
		}
		return FResolvedMesh();
	}

	FResolvedMesh MeshForKind(EFurnitureKind Kind)
	{
		if (ActiveMeshSizes == nullptr)
		{
			return FResolvedMesh();
		}
		return ResolveCatalogMesh(*ActiveMeshSizes, { FurnitureKindLabel(Kind) });
	}

	float KindHeightCm(EFurnitureKind Kind)
	{
		const FResolvedMesh Mesh = MeshForKind(Kind);
		return Mesh.bValid ? Mesh.Z : FurnitureKindHeightCm(Kind);
	}

	EPlanWall PreferredPrimaryWall(const FRoomGrid& Grid)
	{
		if (Grid.Doors.Num() > 0)
		{
			return OppositeWall(Grid.Doors[0].Wall);
		}
		return EPlanWall::North;
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

	float InteriorWallCm(const FRoomGrid& Grid, bool bEastWest)
	{
		if (bEastWest && Grid.UsableEastWest > CellCm)
		{
			return Grid.UsableEastWest;
		}
		if (!bEastWest && Grid.UsableNorthSouth > CellCm)
		{
			return Grid.UsableNorthSouth;
		}
		const int32 Cells = bEastWest ? Grid.CellsX : Grid.CellsY;
		return static_cast<float>(FMath::Max(1, Cells - 2)) * CellCm;
	}

	int32 LongestOpenRun(const FRoomGrid& Grid, EPlanWall Wall)
	{
		int32 Best = 0;
		int32 Run = 0;
		const bool bHorizontal = Wall == EPlanWall::North || Wall == EPlanWall::South;
		const int32 Limit = bHorizontal ? Grid.CellsX - 1 : Grid.CellsY - 1;
		for (int32 Along = 1; Along < Limit; ++Along)
		{
			int32 X = 0;
			int32 Y = 0;
			switch (Wall)
			{
			case EPlanWall::North: X = Along; Y = 1; break;
			case EPlanWall::South: X = Along; Y = FMath::Max(1, Grid.CellsY - 2); break;
			case EPlanWall::West: X = 1; Y = Along; break;
			case EPlanWall::East: X = FMath::Max(1, Grid.CellsX - 2); Y = Along; break;
			default: break;
			}
			const ECell Cell = Grid.Get(X, Y);
			if (Cell == ECell::Free || Cell == ECell::Clearance)
			{
				++Run;
				Best = FMath::Max(Best, Run);
			}
			else
			{
				Run = 0;
			}
		}
		return Best;
	}

	void CategoryWallShare(EFurnitureKind Kind, float& OutAlongShare, float& OutIntoShare)
	{
		OutAlongShare = 0.40f;
		OutIntoShare = 0.30f;
		switch (Kind)
		{
		case EFurnitureKind::Bed: OutAlongShare = 0.65f; OutIntoShare = 0.52f; break;
		case EFurnitureKind::BedLamp: OutAlongShare = 0.14f; OutIntoShare = 0.14f; break;
		case EFurnitureKind::Wardrobe: OutAlongShare = 0.42f; OutIntoShare = 0.22f; break;
		case EFurnitureKind::Toilet: OutAlongShare = 0.38f; OutIntoShare = 0.55f; break;
		case EFurnitureKind::WashBasin: OutAlongShare = 0.30f; OutIntoShare = 0.34f; break;
		case EFurnitureKind::UtilitySink: OutAlongShare = 0.42f; OutIntoShare = 0.45f; break;
		case EFurnitureKind::KitchenSink: OutAlongShare = 0.42f; OutIntoShare = 0.28f; break;
		case EFurnitureKind::Stove: OutAlongShare = 0.32f; OutIntoShare = 0.32f; break;
		case EFurnitureKind::Fridge: OutAlongShare = 0.30f; OutIntoShare = 0.32f; break;
		case EFurnitureKind::DiningTable: OutAlongShare = 0.62f; OutIntoShare = 0.50f; break;
		case EFurnitureKind::DiningChair: OutAlongShare = 0.22f; OutIntoShare = 0.22f; break;
		case EFurnitureKind::Sofa: OutAlongShare = 0.65f; OutIntoShare = 0.48f; break;
		case EFurnitureKind::CoffeeTable: OutAlongShare = 0.42f; OutIntoShare = 0.28f; break;
		case EFurnitureKind::Television: OutAlongShare = 0.30f; OutIntoShare = 0.05f; break;
		case EFurnitureKind::TvStand: OutAlongShare = 0.48f; OutIntoShare = 0.12f; break;
		case EFurnitureKind::Plant: OutAlongShare = 0.16f; OutIntoShare = 0.16f; break;
		case EFurnitureKind::Car: OutAlongShare = 0.86f; OutIntoShare = 0.90f; break;
		default: break;
		}
	}

	float ClampToWall(float RequestedCm, float WallCm)
	{
		const float Minimum = CellCm * 2.f;
		const float Maximum = FMath::Max(Minimum, WallCm - CellCm);
		return FMath::Clamp(RequestedCm, Minimum, Maximum);
	}

	/** Along-wall and into-room sizes. A catalog mesh uses its own width and depth, clamped to the open wall. */
	void SizeFromWallDistance(const FRoomGrid& Grid, EFurnitureKind Kind, EPlanWall Wall, float& OutAlongCm, float& OutIntoCm)
	{
		const bool bAlongEastWest = Wall == EPlanWall::North || Wall == EPlanWall::South;
		const float FullAlong = InteriorWallCm(Grid, bAlongEastWest);
		const float FullAcross = InteriorWallCm(Grid, !bAlongEastWest);
		const int32 OpenCells = LongestOpenRun(Grid, Wall);
		const float OpenAlong = OpenCells > 0 ? OpenCells * CellCm : FullAlong;

		const FResolvedMesh Mesh = MeshForKind(Kind);
		if (Mesh.bValid)
		{
			OutAlongCm = ClampToWall(Mesh.Y, OpenAlong + CellCm);
			OutIntoCm = ClampToWall(Mesh.X, FullAcross);
			return;
		}

		float AlongShare = 0.40f;
		float IntoShare = 0.30f;
		CategoryWallShare(Kind, AlongShare, IntoShare);
		OutAlongCm = ClampToWall(FullAlong * AlongShare, OpenAlong + CellCm);
		OutIntoCm = ClampToWall(FullAcross * IntoShare, FullAcross);
	}

	/** East-west and north-south sizes. A catalog mesh uses its own width and depth, clamped to the room. */
	void FootprintFromWallDistance(const FRoomGrid& Grid, EFurnitureKind Kind, float& OutWidthCm, float& OutDepthCm)
	{
		const float WidthWall = InteriorWallCm(Grid, true);
		const float DepthWall = InteriorWallCm(Grid, false);
		const FResolvedMesh Mesh = MeshForKind(Kind);
		if (Mesh.bValid)
		{
			if (Kind == EFurnitureKind::DiningChair || Kind == EFurnitureKind::Plant || Kind == EFurnitureKind::BedLamp)
			{
				const float Side = ClampToWall(FMath::Max(Mesh.X, Mesh.Y), FMath::Min(WidthWall, DepthWall));
				OutWidthCm = Side;
				OutDepthCm = Side;
				return;
			}
			OutWidthCm = ClampToWall(Mesh.Y, WidthWall);
			OutDepthCm = ClampToWall(Mesh.X, DepthWall);
			return;
		}

		float WidthShare = 0.40f;
		float DepthShare = 0.30f;
		CategoryWallShare(Kind, WidthShare, DepthShare);
		if (Kind == EFurnitureKind::DiningChair || Kind == EFurnitureKind::Plant || Kind == EFurnitureKind::BedLamp)
		{
			const float Side = ClampToWall(FMath::Min(WidthWall, DepthWall) * WidthShare, FMath::Min(WidthWall, DepthWall));
			OutWidthCm = Side;
			OutDepthCm = Side;
			return;
		}
		OutWidthCm = ClampToWall(WidthWall * WidthShare, WidthWall);
		OutDepthCm = ClampToWall(DepthWall * DepthShare, DepthWall);
	}

	bool PlaceKindOnWalls(const FRoomGrid& Grid, EFurnitureKind Kind, const TArray<EPlanWall>& Order, int32 DesiredAlong, bool bUseDesiredOnFirstWall, FGridPose& OutPose)
	{
		for (int32 Index = 0; Index < Order.Num(); ++Index)
		{
			float AlongCm = 0.f;
			float IntoCm = 0.f;
			SizeFromWallDistance(Grid, Kind, Order[Index], AlongCm, IntoCm);
			const int32 Desired = (Index == 0 && bUseDesiredOnFirstWall) ? DesiredAlong : 1;
			if (FindBestOnWall(Grid, Order[Index], CellsForCm(AlongCm), CellsForCm(IntoCm), AlongCm, IntoCm, Desired, OutPose))
			{
				return true;
			}
		}
		return false;
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


	FString SpaceLabel(const FSpaceSpec& Space)
	{
		return FString::Printf(TEXT("%s %d"), *Space.Name, Space.Index + 1);
	}

	/** Z is the height of the mesh bottom above the room floor. SizeZ is an optional target height. */
	void EmitAt(const FSpaceSpec& Space, const FRoomGrid& Grid, EFurnitureKind Kind, int32 Serial, float X, float Y, float BottomCm, float Yaw, float LocalXCm, float LocalYCm, TArray<FExportedObject>& OutObjects, float SizeZ = 0.f, float UniformScale = 1.f)
	{
		FExportedObject Object;
		Object.Id = FString::Printf(TEXT("%s_%d"), *KindToken(Kind), Space.Index + 1);
		if (Serial > 0)
		{
			Object.Id += FString::Printf(TEXT("_%d"), Serial);
		}
		Object.Category = FurnitureKindLabel(Kind);
		Object.Label = FurnitureKindLabel(Kind);
		Object.X = X;
		Object.Y = Y;
		Object.Z = Grid.OriginZ + BottomCm;
		Object.Yaw = Yaw;
		Object.SizeX = LocalXCm;
		Object.SizeY = LocalYCm;
		Object.SizeZ = SizeZ;
		Object.UniformScale = UniformScale;
		OutObjects.Add(Object);
	}

	void EmitPose(const FSpaceSpec& Space, const FRoomGrid& Grid, const FGridPose& Pose, EFurnitureKind Kind, int32 Serial, TArray<FExportedObject>& OutObjects, float BottomCm = 0.f, float SizeZ = 0.f, float UniformScale = 1.f)
	{
		EmitAt(Space, Grid, Kind, Serial,
			Grid.OriginX + (Pose.MinX + Pose.SizeX * 0.5f) * CellCm,
			Grid.OriginY + (Pose.MinY + Pose.SizeY * 0.5f) * CellCm,
			BottomCm, Pose.Yaw, Pose.LocalXCm, Pose.LocalYCm, OutObjects, SizeZ, UniformScale);
	}

	/** Small pose centered along the wall on a stand, flush with the wall side. */
	FGridPose PoseOnStand(const FGridPose& Stand, int32 AlongCells, int32 IntoCells, float AlongCm, float IntoCm)
	{
		FGridPose Pose = Stand;
		const bool bAlongX = Stand.Wall == EPlanWall::North || Stand.Wall == EPlanWall::South;
		const int32 StandAlong = bAlongX ? Stand.SizeX : Stand.SizeY;
		const int32 StandInto = bAlongX ? Stand.SizeY : Stand.SizeX;
		AlongCells = FMath::Clamp(AlongCells, 1, StandAlong);
		IntoCells = FMath::Clamp(IntoCells, 1, StandInto);
		const int32 AlongStart = (StandAlong - AlongCells) / 2;
		if (bAlongX)
		{
			Pose.MinX = Stand.MinX + AlongStart;
			Pose.SizeX = AlongCells;
			Pose.SizeY = IntoCells;
			Pose.MinY = Stand.Wall == EPlanWall::North ? Stand.MinY : Stand.MinY + StandInto - IntoCells;
		}
		else
		{
			Pose.MinY = Stand.MinY + AlongStart;
			Pose.SizeY = AlongCells;
			Pose.SizeX = IntoCells;
			Pose.MinX = Stand.Wall == EPlanWall::West ? Stand.MinX : Stand.MinX + StandInto - IntoCells;
		}
		Pose.LocalXCm = IntoCm;
		Pose.LocalYCm = AlongCm;
		return Pose;
	}

	/** Stand centered on a television that is already against the wall. The television stays centered along the stand. */
	FGridPose StandAroundTelevision(const FGridPose& Tv, int32 AlongCells, int32 IntoCells, float AlongCm, float IntoCm)
	{
		FGridPose Pose = Tv;
		const bool bAlongX = Tv.Wall == EPlanWall::North || Tv.Wall == EPlanWall::South;
		const int32 TvAlong = bAlongX ? Tv.SizeX : Tv.SizeY;
		const int32 TvInto = bAlongX ? Tv.SizeY : Tv.SizeX;
		AlongCells = FMath::Max(AlongCells, TvAlong);
		IntoCells = FMath::Max(IntoCells, TvInto);
		if (bAlongX)
		{
			const int32 Center = Tv.MinX + Tv.SizeX / 2;
			Pose.SizeX = AlongCells;
			Pose.SizeY = IntoCells;
			Pose.MinX = Center - AlongCells / 2;
			Pose.MinY = Tv.Wall == EPlanWall::North ? Tv.MinY : Tv.MinY + Tv.SizeY - IntoCells;
		}
		else
		{
			const int32 Center = Tv.MinY + Tv.SizeY / 2;
			Pose.SizeY = AlongCells;
			Pose.SizeX = IntoCells;
			Pose.MinY = Center - AlongCells / 2;
			Pose.MinX = Tv.Wall == EPlanWall::West ? Tv.MinX : Tv.MinX + Tv.SizeX - IntoCells;
		}
		Pose.LocalXCm = IntoCm;
		Pose.LocalYCm = AlongCm;
		Pose.Yaw = Tv.Yaw;
		Pose.Wall = Tv.Wall;
		return Pose;
	}

	/**
	 * Television unit. The mesh is enlarged before a wall is chosen, and the screen bottom is 50 cm above the floor.
	 * A window or door in that height band rejects the spot. The table is then sized under the television so they meet.
	 * Speakers and the other accessories take the same wall and rotation.
	 */
	bool PlaceTvUnit(const FSpaceSpec& Space, FRoomGrid& Grid, const FGridPose& SofaPose, const FResolvedMesh& TvMesh, TArray<FExportedObject>& OutObjects, TArray<FString>& OutNotes, int32& OutPlaced)
	{
		const float Grow = TelevisionMeshScale();
		const float TvBottomCm = TelevisionBottomCm();
		const float WallBottom = FurnitureKindMountBottomCm(EFurnitureKind::Television);
		float TvIntoCm = 0.f;
		float TvAlongCm = 0.f;
		float TvHeightCm = 0.f;
		if (TvMesh.bValid)
		{
			TvIntoCm = TvMesh.X * Grow;
			TvAlongCm = TvMesh.Y * Grow;
			TvHeightCm = TvMesh.Z * Grow;
		}
		else
		{
			float WidthFeet = 0.f;
			float DepthFeet = 0.f;
			float IgnoredYaw = 0.f;
			FurnitureKindDefaultSize(EFurnitureKind::Television, WidthFeet, DepthFeet, IgnoredYaw);
			constexpr float CentimetersPerFoot = 30.48f;
			TvIntoCm = WidthFeet * CentimetersPerFoot * Grow;
			TvAlongCm = DepthFeet * CentimetersPerFoot * Grow;
			TvHeightCm = FurnitureKindHeightCm(EFurnitureKind::Television) * Grow;
		}
		const float TvTopCm = TvBottomCm + TvHeightCm;

		const float SpeakerCm = 20.f;
		const float SpeakerGap = 6.f;
		const float DesiredStandAlong = TvAlongCm + 2.f * (SpeakerCm + SpeakerGap);
		const float DesiredStandInto = FMath::Max(TvIntoCm + 10.f, 40.f);
		const int32 TvAlongCells = CellsForCm(TvAlongCm);
		const int32 TvIntoCells = CellsForCm(TvIntoCm);

		const bool bSofaAlongX = SofaPose.Wall == EPlanWall::North || SofaPose.Wall == EPlanWall::South;
		const int32 SofaCenter = bSofaAlongX ? SofaPose.MinX + SofaPose.SizeX / 2 : SofaPose.MinY + SofaPose.SizeY / 2;
		const EPlanWall Preferred = OppositeWall(SofaPose.Wall);
		const TArray<EPlanWall> Order = TvWallOrder(Grid, SofaPose, Preferred);

		auto NoteMove = [&](EPlanWall Wall)
		{
			if (Wall == Preferred)
			{
				return;
			}
			const TCHAR* Relation = Wall == OppositeWall(Preferred) ? TEXT("perpendicular") : TEXT("far");
			OutNotes.Add(FString::Printf(
				TEXT("%s Television moved to the %s %s wall because a window or door crosses the screen. The TV table, speakers, and accessories turned with it."),
				*SpaceLabel(Space), Relation, WallLabel(Wall)));
		};

		auto FrontClear = [](const FRoomGrid& Room, const FGridPose& Block, float AisleCm)
		{
			const int32 Steps = FMath::Max(1, FMath::FloorToInt(AisleCm / CellCm));
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				auto Open = [&Room](int32 X, int32 Y)
				{
					if (!Room.InBounds(X, Y))
					{
						return false;
					}
					const ECell Cell = Room.Get(X, Y);
					return Cell == ECell::Free || Cell == ECell::Clearance;
				};
				if (Block.Wall == EPlanWall::North || Block.Wall == EPlanWall::South)
				{
					const int32 Y = Block.Wall == EPlanWall::North ? Block.MinY + Block.SizeY - 1 + Step : Block.MinY - Step;
					for (int32 X = Block.MinX; X < Block.MinX + Block.SizeX; ++X)
					{
						if (!Open(X, Y))
						{
							return false;
						}
					}
				}
				else
				{
					const int32 X = Block.Wall == EPlanWall::West ? Block.MinX + Block.SizeX - 1 + Step : Block.MinX - Step;
					for (int32 Y = Block.MinY; Y < Block.MinY + Block.SizeY; ++Y)
					{
						if (!Open(X, Y))
						{
							return false;
						}
					}
				}
			}
			return true;
		};

		const bool bForceWall = Space.TvMode == 1;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			Grid.bRelaxedClearance = Pass == 1;
			const bool bRequireAisle = Pass == 0;
			for (EPlanWall Wall : Order)
			{
				const bool bAlongEastWest = Wall == EPlanWall::North || Wall == EPlanWall::South;
				const int32 OpenCells = LongestOpenRun(Grid, Wall);
				const float OpenAlong = OpenCells > 0 ? OpenCells * CellCm : InteriorWallCm(Grid, bAlongEastWest);
				if (OpenAlong + 1.f < TvAlongCm)
				{
					continue;
				}

				if (!bForceWall)
				{
					const int32 Z0 = FMath::FloorToInt(TvBottomCm / CellCm);
					const int32 Z1 = FMath::Min(Grid.CellsZ, Z0 + CellsForCm(TvHeightCm));
					const int32 NarrowAlongCells = CellsForCm(TvAlongCm + 20.f);
					const int32 WideAlongCells = CellsForCm(DesiredStandAlong);
					const int32 StandIntoCells = CellsForCm(DesiredStandInto);
					auto StandFor = [&](const FGridPose& Tv, int32 AlongCells)
					{
						return StandAroundTelevision(Tv, AlongCells, StandIntoCells, AlongCells * CellCm, StandIntoCells * CellCm);
					};

					FGridPose TvPose;
					const int32 Desired = SofaCenter - TvAlongCells / 2;
					const bool bFoundTv = FindBestOnWall(Grid, Wall, TvAlongCells, TvIntoCells, TvAlongCm, TvIntoCm, Desired, TvPose,
						[&](const FGridPose& Candidate)
						{
							if (TvSpanHitsOpening(Grid, Candidate, TvBottomCm, TvTopCm) || !Grid.FitsRange(Candidate, Z0, Z1))
							{
								return false;
							}
							const bool bWide = Grid.Fits(StandFor(Candidate, WideAlongCells));
							const bool bNarrow = Grid.Fits(StandFor(Candidate, NarrowAlongCells));
							if (!bWide && !bNarrow)
							{
								return false;
							}
							if (bRequireAisle && !FrontClear(Grid, bWide ? StandFor(Candidate, WideAlongCells) : StandFor(Candidate, NarrowAlongCells), 90.f))
							{
								return false;
							}
							return true;
						});
					if (!bFoundTv)
					{
						continue;
					}

					const bool bWide = Grid.Fits(StandFor(TvPose, WideAlongCells));
					const FGridPose StandPose = bWide ? StandFor(TvPose, WideAlongCells) : StandFor(TvPose, NarrowAlongCells);

					Grid.Level = FRoomGrid::FLevel();
					Grid.Occupy(StandPose);
					EmitPose(Space, Grid, StandPose, EFurnitureKind::TvStand, 0, OutObjects, 0.f, TvBottomCm, 1.f);
					++OutPlaced;

					Grid.OccupyRange(TvPose, Z0, Z1);
					EmitPose(Space, Grid, TvPose, EFurnitureKind::Television, 0, OutObjects, TvBottomCm, TvHeightCm, Grow);
					++OutPlaced;

						// Accessories sit on the stand top, which meets the television bottom. They share the stand's yaw.
						const bool bAlongX = Wall == EPlanWall::North || Wall == EPlanWall::South;
						const float AlongDirX = bAlongX ? 1.f : 0.f;
						const float AlongDirY = bAlongX ? 0.f : 1.f;
						float IntoDirX = 0.f;
						float IntoDirY = 0.f;
						switch (Wall)
						{
						case EPlanWall::North: IntoDirY = 1.f; break;
						case EPlanWall::South: IntoDirY = -1.f; break;
						case EPlanWall::West: IntoDirX = 1.f; break;
						default: IntoDirX = -1.f; break;
						}
						const float BoxAlong = (bAlongX ? StandPose.SizeX : StandPose.SizeY) * CellCm;
						const float BoxInto = (bAlongX ? StandPose.SizeY : StandPose.SizeX) * CellCm;
						const float CenterX = Grid.OriginX + (StandPose.MinX + StandPose.SizeX * 0.5f) * CellCm;
						const float CenterY = Grid.OriginY + (StandPose.MinY + StandPose.SizeY * 0.5f) * CellCm;

						auto PlaceOnStand = [&](EFurnitureKind Kind, int32 Serial, float AlongOffset, float IntoOffset, float WidthCm, float DepthCm)
						{
							EmitAt(Space, Grid, Kind, Serial,
								CenterX + AlongDirX * AlongOffset + IntoDirX * IntoOffset,
								CenterY + AlongDirY * AlongOffset + IntoDirY * IntoOffset,
								TvBottomCm, StandPose.Yaw, DepthCm, WidthCm, OutObjects);
							++OutPlaced;
						};

						const float TvHalf = TvAlongCm * 0.5f;
						const float BackInto = -BoxInto * 0.5f;
						float SpeakerOffset = bWide ? TvHalf + SpeakerCm * 0.5f + SpeakerGap : -1.f;
						if (SpeakerOffset + SpeakerCm * 0.5f > BoxAlong * 0.5f)
						{
							SpeakerOffset = -1.f;
						}
						if (SpeakerOffset > 0.f)
						{
							PlaceOnStand(EFurnitureKind::Speaker, 1, -SpeakerOffset, BackInto + SpeakerCm * 0.5f + 4.f, SpeakerCm, SpeakerCm);
							PlaceOnStand(EFurnitureKind::Speaker, 2, SpeakerOffset, BackInto + SpeakerCm * 0.5f + 4.f, SpeakerCm, SpeakerCm);
						}
						else
						{
							OutNotes.Add(FString::Printf(TEXT("%s speakers skipped because the TV stand is too narrow"), *SpaceLabel(Space)));
						}

						const float FrontDepth = 25.f;
						if (BoxInto >= TvIntoCm + FrontDepth + 6.f && BoxAlong >= 100.f)
						{
							const float FrontInto = BoxInto * 0.5f - FrontDepth * 0.5f - 3.f;
							PlaceOnStand(EFurnitureKind::DvdPlayer, 0, -BoxAlong * 0.18f, FrontInto, 35.f, FrontDepth);
							PlaceOnStand(EFurnitureKind::GameConsole, 0, BoxAlong * 0.18f, FrontInto, 30.f, FrontDepth);
						}
						else
						{
							OutNotes.Add(FString::Printf(TEXT("%s DVD player and console skipped because the TV stand is too small"), *SpaceLabel(Space)));
						}
						Grid.bRelaxedClearance = false;
						if (!bRequireAisle)
						{
							OutNotes.Add(FString::Printf(TEXT("%s TV group placed with less than 90 cm of circulation in front"), *SpaceLabel(Space)));
						}
						NoteMove(Wall);
						OutNotes.Add(FString::Printf(TEXT("%s Television scaled to 180%% with its bottom at %.0f cm and the table meeting the screen"), *SpaceLabel(Space), TvBottomCm));
						return true;
					}

					if (!bForceWall)
					{
						continue;
					}

					const int32 Z0 = FMath::FloorToInt(WallBottom / CellCm);
					const int32 Z1 = FMath::Min(Grid.CellsZ, Z0 + CellsForCm(TvHeightCm));
					const float MountTop = WallBottom + TvHeightCm;
					Grid.Level.bActive = true;
					Grid.Level.bFloor = false;
					Grid.Level.Z0 = Z0;
					Grid.Level.Z1 = Z1;
					FGridPose TvPose;
					const bool bFound = FindBestOnWall(Grid, Wall, TvAlongCells, TvIntoCells, TvAlongCm, TvIntoCm, SofaCenter - TvAlongCells / 2, TvPose,
						[&](const FGridPose& Candidate)
						{
							if (TvSpanHitsOpening(Grid, Candidate, WallBottom, MountTop))
							{
								return false;
							}
							return Grid.FitsRange(Candidate, Z0, Z1);
						});
					Grid.Level = FRoomGrid::FLevel();
					if (!bFound)
					{
						continue;
					}
					Grid.OccupyRange(TvPose, Z0, Z1);
					EmitPose(Space, Grid, TvPose, EFurnitureKind::Television, 0, OutObjects, WallBottom, TvHeightCm, Grow);
					++OutPlaced;
					Grid.bRelaxedClearance = false;
					OutNotes.Add(FString::Printf(TEXT("%s Television wall-mounted at %.0f cm"), *SpaceLabel(Space), WallBottom));
					NoteMove(Wall);
					return true;
				}
			}
		Grid.bRelaxedClearance = false;
		Grid.Level = FRoomGrid::FLevel();
		return false;
	}

	/**
	 * Dining set. The table is a real size, centered on the open floor.
	 * Chairs sit 10 cm off the edge, with 80 cm behind each chair, and an open side keeps a 90 cm aisle.
	 */
	bool PlaceDiningSet(const FSpaceSpec& Space, FRoomGrid& Grid, TArray<FExportedObject>& OutObjects, TArray<FString>& OutNotes, int32& OutPlaced)
	{
		float ChairCm = 50.f;
		const FResolvedMesh ChairMesh = MeshForKind(EFurnitureKind::DiningChair);
		if (ChairMesh.bValid)
		{
			ChairCm = FMath::Max(ChairMesh.X, ChairMesh.Y);
		}
		const float TuckCm = 10.f;
		const float PullCm = 80.f;
		const float AisleCm = 90.f;
		const float ChairBandCm = TuckCm + ChairCm + PullCm;

		auto Walkable = [](const FRoomGrid& Room, int32 X, int32 Y)
		{
			if (!Room.InBounds(X, Y))
			{
				return false;
			}
			const ECell Cell = Room.Get(X, Y);
			return Cell == ECell::Free || Cell == ECell::Clearance;
		};

		auto FreeBeyond = [&Walkable](const FRoomGrid& Room, const FGridPose& Block, EPlanWall Side)
		{
			int32 Steps = 0;
			for (;;)
			{
				++Steps;
				bool bOpen = true;
				if (Side == EPlanWall::North || Side == EPlanWall::South)
				{
					const int32 Y = Side == EPlanWall::North
						? Block.MinY - Steps
						: Block.MinY + Block.SizeY - 1 + Steps;
					for (int32 X = Block.MinX; X < Block.MinX + Block.SizeX && bOpen; ++X)
					{
						bOpen = Walkable(Room, X, Y);
					}
				}
				else
				{
					const int32 X = Side == EPlanWall::West
						? Block.MinX - Steps
						: Block.MinX + Block.SizeX - 1 + Steps;
					for (int32 Y = Block.MinY; Y < Block.MinY + Block.SizeY && bOpen; ++Y)
					{
						bOpen = Walkable(Room, X, Y);
					}
				}
				if (!bOpen)
				{
					return (Steps - 1) * CellCm;
				}
				if (Steps >= 40)
				{
					return Steps * CellCm;
				}
			}
		};

		auto FootprintFree = [](const FRoomGrid& Room, const FGridPose& Pose, float HeightCm)
		{
			if (!Room.Fits(Pose))
			{
				return false;
			}
			const int32 Z1 = FMath::Min(Room.CellsZ, CellsForCm(HeightCm));
			return Z1 <= 1 || Room.FitsRange(Pose, 1, Z1);
		};

		auto MaxEven = [&](float SideCm)
		{
			if (SideCm + 0.1f < ChairCm)
			{
				return 0;
			}
			int32 Count = 1;
			while (Count < 4 && (Count + 1) * ChairCm + Count * 10.f <= SideCm + 0.1f)
			{
				++Count;
			}
			return Count;
		};

		struct FChairPlan
		{
			float X0 = 0.f;
			float Y0 = 0.f;
			float X1 = 0.f;
			float Y1 = 0.f;
			float Yaw = 0.f;
			EPlanWall Back = EPlanWall::North;
		};

		struct FLayout
		{
			bool bValid = false;
			int32 Chairs = 0;
			bool bAisle = false;
			float CenterDist = 0.f;
			FGridPose Table;
			float Yaw = 0.f;
			float LocalX = 0.f;
			float LocalY = 0.f;
			TArray<FChairPlan> Plans;
		};

		double SumX = 0.0;
		double SumY = 0.0;
		int32 FreeCount = 0;
		for (int32 Y = 0; Y < Grid.CellsY; ++Y)
		{
			for (int32 X = 0; X < Grid.CellsX; ++X)
			{
				if (Grid.Get(X, Y) != ECell::Free)
				{
					continue;
				}
				SumX += X + 0.5;
				SumY += Y + 0.5;
				++FreeCount;
			}
		}
		if (FreeCount == 0)
		{
			return false;
		}
		const float CentroidX = static_cast<float>(SumX / FreeCount);
		const float CentroidY = static_cast<float>(SumY / FreeCount);

		auto Better = [](const FLayout& A, const FLayout& B)
		{
			if (A.Chairs != B.Chairs)
			{
				return A.Chairs > B.Chairs;
			}
			if (A.bAisle != B.bAisle)
			{
				return A.bAisle;
			}
			return A.CenterDist < B.CenterDist;
		};

		auto SideChairs = [&](const FGridPose& Table, float Edge0, float Edge1, bool bAlongX, EPlanWall Back, int32 Wanted, TArray<FChairPlan>& OutPlans)
		{
			const float Side = Edge1 - Edge0;
			int32 Count = FMath::Min(Wanted, MaxEven(Side));
			for (; Count >= 1; --Count)
			{
				const float Gap = (Side - Count * ChairCm) / static_cast<float>(Count + 1);
				TArray<FChairPlan> Trial;
				bool bAll = true;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const float Center = Edge0 + Gap * (Index + 1) + ChairCm * Index + ChairCm * 0.5f;
					FChairPlan Plan;
					Plan.Yaw = YawIntoRoom(Back);
					Plan.Back = Back;
					if (bAlongX)
					{
						Plan.X0 = Center - ChairCm * 0.5f;
						Plan.X1 = Center + ChairCm * 0.5f;
						if (Back == EPlanWall::North)
						{
							Plan.Y1 = Table.MinY * CellCm - TuckCm;
							Plan.Y0 = Plan.Y1 - ChairCm;
						}
						else
						{
							Plan.Y0 = (Table.MinY + Table.SizeY) * CellCm + TuckCm;
							Plan.Y1 = Plan.Y0 + ChairCm;
						}
					}
					else
					{
						Plan.Y0 = Center - ChairCm * 0.5f;
						Plan.Y1 = Center + ChairCm * 0.5f;
						if (Back == EPlanWall::West)
						{
							Plan.X1 = Table.MinX * CellCm - TuckCm;
							Plan.X0 = Plan.X1 - ChairCm;
						}
						else
						{
							Plan.X0 = (Table.MinX + Table.SizeX) * CellCm + TuckCm;
							Plan.X1 = Plan.X0 + ChairCm;
						}
					}
					FGridPose Pose;
					Pose.MinX = FMath::FloorToInt(Plan.X0 / CellCm + 0.001f);
					Pose.MinY = FMath::FloorToInt(Plan.Y0 / CellCm + 0.001f);
					Pose.SizeX = FMath::Max(1, FMath::CeilToInt(Plan.X1 / CellCm - 0.001f) - Pose.MinX);
					Pose.SizeY = FMath::Max(1, FMath::CeilToInt(Plan.Y1 / CellCm - 0.001f) - Pose.MinY);
					if (!FootprintFree(Grid, Pose, KindHeightCm(EFurnitureKind::DiningChair)) || FreeBeyond(Grid, Pose, Back) + 0.1f < PullCm)
					{
						bAll = false;
						break;
					}
					Trial.Add(Plan);
				}
				if (bAll)
				{
					OutPlans.Append(Trial);
					return Count;
				}
			}
			return 0;
		};

		struct FPreset
		{
			float LongCm;
			float ShortCm;
			int32 OnLong;
			int32 OnShort;
		};
		TArray<FPreset> Presets;
		const FResolvedMesh TableMesh = MeshForKind(EFurnitureKind::DiningTable);
		if (TableMesh.bValid)
		{
			FPreset MeshPreset;
			MeshPreset.LongCm = FMath::Max(TableMesh.X, TableMesh.Y);
			MeshPreset.ShortCm = FMath::Min(TableMesh.X, TableMesh.Y);
			MeshPreset.OnLong = MeshPreset.LongCm >= 160.f ? 2 : 1;
			MeshPreset.OnShort = MeshPreset.ShortCm >= 80.f ? 1 : 0;
			Presets.Add(MeshPreset);
		}
		const FPreset FallbackPresets[] =
		{
			{ 220.f, 100.f, 3, 1 },
			{ 180.f, 90.f, 3, 1 },
			{ 180.f, 90.f, 2, 1 },
			{ 160.f, 90.f, 2, 1 },
			{ 140.f, 80.f, 2, 0 },
			{ 120.f, 80.f, 2, 0 },
			{ 100.f, 100.f, 1, 1 },
			{ 90.f, 90.f, 1, 1 },
			{ 80.f, 80.f, 1, 0 }
		};
		Presets.Append(FallbackPresets, UE_ARRAY_COUNT(FallbackPresets));

		const FRoomGrid::FLevel SavedLevel = Grid.Level;
		Grid.Level = FRoomGrid::FLevel();
		FLayout Best;
		for (const FPreset& Preset : Presets)
		{
			for (int32 Turn = 0; Turn < 2; ++Turn)
			{
				const bool bLongOnX = Turn == 0;
				const float WorldX = bLongOnX ? Preset.LongCm : Preset.ShortCm;
				const float WorldY = bLongOnX ? Preset.ShortCm : Preset.LongCm;
				const int32 WantNorth = bLongOnX ? Preset.OnLong : Preset.OnShort;
				const int32 WantEast = bLongOnX ? Preset.OnShort : Preset.OnLong;
				const int32 CellsX = CellsForCm(WorldX);
				const int32 CellsY = CellsForCm(WorldY);
				if (CellsX + 2 >= Grid.CellsX || CellsY + 2 >= Grid.CellsY)
				{
					continue;
				}

				for (int32 Y = 1; Y + CellsY < Grid.CellsY; ++Y)
				{
					for (int32 X = 1; X + CellsX < Grid.CellsX; ++X)
					{
						FGridPose Table;
						Table.MinX = X;
						Table.MinY = Y;
						Table.SizeX = CellsX;
						Table.SizeY = CellsY;
						if (!FootprintFree(Grid, Table, KindHeightCm(EFurnitureKind::DiningTable)))
						{
							continue;
						}
						const float TableDist = FMath::Abs((X + CellsX * 0.5f) - CentroidX) + FMath::Abs((Y + CellsY * 0.5f) - CentroidY);

						const float TableX0 = Table.MinX * CellCm;
						const float TableX1 = (Table.MinX + Table.SizeX) * CellCm;
						const float TableY0 = Table.MinY * CellCm;
						const float TableY1 = (Table.MinY + Table.SizeY) * CellCm;
						auto PairSides = [&](float Edge0, float Edge1, bool bAlongX, EPlanWall First, EPlanWall Second, int32 Wanted, TArray<FChairPlan>& OutPlans)
						{
							TArray<FChairPlan> A;
							TArray<FChairPlan> B;
							int32 Count = FMath::Min(SideChairs(Table, Edge0, Edge1, bAlongX, First, Wanted, A), SideChairs(Table, Edge0, Edge1, bAlongX, Second, Wanted, B));
							while (Count >= 1)
							{
								A.Reset();
								B.Reset();
								const int32 GotA = SideChairs(Table, Edge0, Edge1, bAlongX, First, Count, A);
								const int32 GotB = SideChairs(Table, Edge0, Edge1, bAlongX, Second, Count, B);
								if (GotA == Count && GotB == Count)
								{
									OutPlans.Append(A);
									OutPlans.Append(B);
									return Count;
								}
								--Count;
							}
							return 0;
						};
						TArray<FChairPlan> Plans;
						const int32 UsedNS = PairSides(TableX0, TableX1, true, EPlanWall::North, EPlanWall::South, WantNorth, Plans);
						const int32 UsedEW = PairSides(TableY0, TableY1, false, EPlanWall::East, EPlanWall::West, WantEast, Plans);
						auto Walking = [&](EPlanWall Side, bool bHasChairs)
						{
							const float Clear = FreeBeyond(Grid, Table, Side);
							return bHasChairs ? Clear - ChairBandCm : Clear;
						};
						const bool bPathY = Walking(EPlanWall::North, UsedNS > 0) + 0.1f >= AisleCm
							&& Walking(EPlanWall::South, UsedNS > 0) + 0.1f >= AisleCm;
						const bool bPathX = Walking(EPlanWall::East, UsedEW > 0) + 0.1f >= AisleCm
							&& Walking(EPlanWall::West, UsedEW > 0) + 0.1f >= AisleCm;

						FLayout Layout;
						Layout.bValid = true;
						Layout.Chairs = Plans.Num();
						Layout.bAisle = bPathX || bPathY;
						Layout.CenterDist = TableDist;
						Layout.Table = Table;
						Layout.Yaw = bLongOnX ? 0.f : 90.f;
						Layout.LocalX = Preset.LongCm;
						Layout.LocalY = Preset.ShortCm;
						Layout.Plans = MoveTemp(Plans);
						if (!Best.bValid || Better(Layout, Best))
						{
							Best = MoveTemp(Layout);
						}
					}
				}
			}
		}

		if (!Best.bValid)
		{
			Grid.Level = SavedLevel;
			return false;
		}

		auto OccupyFootprint = [](FRoomGrid& Room, const FGridPose& Pose)
		{
			for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
			{
				for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
				{
					if (!Room.InBounds(X, Y))
					{
						continue;
					}
					ECell& Cell = Room.Cells[Room.IndexOf(X, Y, 0)];
					if (Cell == ECell::Free || Cell == ECell::Clearance)
					{
						Cell = ECell::Occupied;
					}
				}
			}
		};

		OccupyFootprint(Grid, Best.Table);
		const float TableCenterX = Grid.OriginX + (Best.Table.MinX + Best.Table.SizeX * 0.5f) * CellCm;
		const float TableCenterY = Grid.OriginY + (Best.Table.MinY + Best.Table.SizeY * 0.5f) * CellCm;
		EmitAt(Space, Grid, EFurnitureKind::DiningTable, 0, TableCenterX, TableCenterY, 0.f, Best.Yaw, Best.LocalX, Best.LocalY, OutObjects);
		++OutPlaced;

		int32 Serial = 1;
		for (const FChairPlan& Plan : Best.Plans)
		{
			FGridPose Pose;
			Pose.MinX = FMath::FloorToInt(Plan.X0 / CellCm + 0.001f);
			Pose.MinY = FMath::FloorToInt(Plan.Y0 / CellCm + 0.001f);
			Pose.SizeX = FMath::Max(1, FMath::CeilToInt(Plan.X1 / CellCm - 0.001f) - Pose.MinX);
			Pose.SizeY = FMath::Max(1, FMath::CeilToInt(Plan.Y1 / CellCm - 0.001f) - Pose.MinY);
			OccupyFootprint(Grid, Pose);
			for (int32 Step = 1; Step <= CellsForCm(PullCm); ++Step)
			{
				if (Plan.Back == EPlanWall::North || Plan.Back == EPlanWall::South)
				{
					const int32 Y = Plan.Back == EPlanWall::North ? Pose.MinY - Step : Pose.MinY + Pose.SizeY - 1 + Step;
					for (int32 X = Pose.MinX; X < Pose.MinX + Pose.SizeX; ++X)
					{
						Grid.SetIfFree(X, Y, ECell::Clearance);
					}
				}
				else
				{
					const int32 X = Plan.Back == EPlanWall::West ? Pose.MinX - Step : Pose.MinX + Pose.SizeX - 1 + Step;
					for (int32 Y = Pose.MinY; Y < Pose.MinY + Pose.SizeY; ++Y)
					{
						Grid.SetIfFree(X, Y, ECell::Clearance);
					}
				}
			}
			EmitAt(Space, Grid, EFurnitureKind::DiningChair, Serial,
				Grid.OriginX + (Plan.X0 + Plan.X1) * 0.5f,
				Grid.OriginY + (Plan.Y0 + Plan.Y1) * 0.5f,
				0.f, Plan.Yaw, ChairCm, ChairCm, OutObjects);
			++Serial;
			++OutPlaced;
		}

		if (Best.Chairs > 0)
		{
			OutNotes.Add(FString::Printf(
				TEXT("%s dining table centered with %d chairs, 10 cm from the table and 80 cm behind each chair%s"),
				*SpaceLabel(Space),
				Best.Chairs,
				Best.bAisle ? TEXT("") : TEXT("; the room leaves less than 90 cm of walking space beside the set")));
		}
		else
		{
			OutNotes.Add(FString::Printf(
				TEXT("%s dining table centered, but no chair kept 80 cm clear behind it"),
				*SpaceLabel(Space)));
		}
		Grid.Level = SavedLevel;
		return true;
	}

	void PlaceSpace(const FSpaceSpec& Space, FRoomGrid& Grid, const FResolvedMesh& TvMesh, TArray<FExportedObject>& OutObjects, TArray<FString>& OutNotes, int32& OutPlaced)
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

			// Every piece is tested up to its own height. A spot is rejected when a window or door swing
			// crosses the cubes the piece would fill, so tall pieces move to another wall.
			Grid.Level = FRoomGrid::FLevel();
			if (Prop.Anchor != EPropAnchor::TvUnit && FurnitureKindMountBottomCm(Prop.Kind) < 0.f)
			{
				Grid.Level.bActive = true;
				Grid.Level.bFloor = true;
				Grid.Level.Z0 = 1;
				Grid.Level.Z1 = FMath::Min(Grid.CellsZ, CellsForCm(KindHeightCm(Prop.Kind)));
			}

			if (Prop.Anchor == EPropAnchor::Around)
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				FootprintFromWallDistance(Grid, Prop.Kind, WidthCm, DepthCm);
				TArray<FGridPose> Chairs;
				Grid.bRelaxedClearance = false;
				bool bChairsFit = PlaceAround(Grid, PrimaryPose, WidthCm, Chairs);
				bool bReduced = false;
				if (!bChairsFit)
				{
					Grid.bRelaxedClearance = true;
					bChairsFit = PlaceAround(Grid, PrimaryPose, WidthCm, Chairs);
					bReduced = bChairsFit;
					Grid.bRelaxedClearance = false;
				}
				if (!bChairsFit)
				{
					OutNotes.Add(FString::Printf(TEXT("%s %s does not fit"), *SpaceLabel(Space), *PropName));
					Grid.Level = FRoomGrid::FLevel();
					continue;
				}
				Grid.Level = FRoomGrid::FLevel();
				for (int32 ChairIndex = 0; ChairIndex < Chairs.Num(); ++ChairIndex)
				{
					EmitPose(Space, Grid, Chairs[ChairIndex], Prop.Kind, ChairIndex + 1, OutObjects);
					++OutPlaced;
				}
				if (bReduced)
				{
					OutNotes.Add(FString::Printf(TEXT("%s %s placed with reduced clearance"), *SpaceLabel(Space), *PropName));
				}
				continue;
			}

			if (Prop.Anchor == EPropAnchor::DiningSet)
			{
				Grid.Level = FRoomGrid::FLevel();
				if (!PlaceDiningSet(Space, Grid, OutObjects, OutNotes, OutPlaced))
				{
					OutNotes.Add(FString::Printf(TEXT("%s %s does not fit"), *SpaceLabel(Space), *PropName));
				}
				else
				{
					bPrimaryPlaced = true;
				}
				continue;
			}

			if (Prop.Anchor == EPropAnchor::TvUnit)
			{
				if (!PlaceTvUnit(Space, Grid, PrimaryPose, TvMesh, OutObjects, OutNotes, OutPlaced))
				{
					OutNotes.Add(FString::Printf(TEXT("%s %s does not fit"), *SpaceLabel(Space), *PropName));
				}
				continue;
			}

			// Wall-mounted fixtures (sinks, wash basin) are tested at their mounting height.
			const float MountBottom = FurnitureKindMountBottomCm(Prop.Kind);
			const bool bMounted = MountBottom > 0.f;
			const int32 MountZ0 = bMounted ? FMath::FloorToInt(MountBottom / CellCm) : 0;
			const int32 MountZ1 = bMounted ? MountZ0 + CellsForCm(KindHeightCm(Prop.Kind)) : 0;
			if (bMounted)
			{
				Grid.Level.bActive = true;
				Grid.Level.bFloor = FurnitureKindReservesFloorBelow(Prop.Kind);
				Grid.Level.Z0 = MountZ0;
				Grid.Level.Z1 = MountZ1;
			}

			FGridPose Pose;
			bool bPlaced = false;
			bool bReduced = false;
			for (int32 Pass = 0; Pass < 2 && !bPlaced; ++Pass)
			{
				Grid.bRelaxedClearance = Pass == 1;
				switch (Prop.Anchor)
			{
			case EPropAnchor::Wall:
			{
				const TArray<EPlanWall> Order = WallOrder(Grid, PreferredPrimaryWall(Grid));
				bPlaced = PlaceKindOnWalls(Grid, Prop.Kind, Order, 1, false, Pose);
				break;
			}
			case EPropAnchor::Beside:
			{
				const int32 EndAlong = (ChainPose.Wall == EPlanWall::North || ChainPose.Wall == EPlanWall::South)
					? ChainPose.MinX + ChainPose.SizeX
					: ChainPose.MinY + ChainPose.SizeY;
				const TArray<EPlanWall> Order = WallOrder(Grid, ChainPose.Wall);
				bPlaced = PlaceKindOnWalls(Grid, Prop.Kind, Order, EndAlong, true, Pose);
				break;
			}
			case EPropAnchor::InFront:
			{
				float ParallelCm = 0.f;
				float AwayCm = 0.f;
				SizeFromWallDistance(Grid, Prop.Kind, PrimaryPose.Wall, ParallelCm, AwayCm);
				bPlaced = PlaceInFront(Grid, PrimaryPose, CellsForCm(ParallelCm), CellsForCm(AwayCm), ParallelCm, AwayCm, Pose);
				break;
			}
			case EPropAnchor::Opposite:
			{
				float AlongCm = 0.f;
				float IntoCm = 0.f;
				const EPlanWall Wall = OppositeWall(PrimaryPose.Wall);
				SizeFromWallDistance(Grid, Prop.Kind, Wall, AlongCm, IntoCm);
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
				FootprintFromWallDistance(Grid, Prop.Kind, WidthCm, DepthCm);
				bPlaced = PlaceCorner(Grid, CellsForCm(WidthCm), WidthCm, Pose);
				break;
			}
			case EPropAnchor::Center:
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				FootprintFromWallDistance(Grid, Prop.Kind, WidthCm, DepthCm);
				bPlaced = FindCentered(Grid, CellsForCm(WidthCm), CellsForCm(DepthCm), 0.f, WidthCm, DepthCm, Pose);
				break;
			}
			case EPropAnchor::DiningSet:
				break;
			case EPropAnchor::LongAxis:
			{
				float WidthCm = 0.f;
				float DepthCm = 0.f;
				FootprintFromWallDistance(Grid, Prop.Kind, WidthCm, DepthCm);
				const float LongCm = FMath::Max(WidthCm, DepthCm);
				const float ShortCm = FMath::Min(WidthCm, DepthCm);
				const bool bAlongY = Grid.CellsY >= Grid.CellsX;
				if (bAlongY)
				{
					bPlaced = FindCentered(Grid, CellsForCm(ShortCm), CellsForCm(LongCm), 90.f, LongCm, ShortCm, Pose);
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
				if (bPlaced && Pass == 1)
				{
					bReduced = true;
				}
			}
			Grid.bRelaxedClearance = false;
			Grid.Level = FRoomGrid::FLevel();

			if (!bPlaced)
			{
				OutNotes.Add(FString::Printf(TEXT("%s %s does not fit"), *SpaceLabel(Space), *PropName));
				continue;
			}

			if (bMounted)
			{
				Grid.OccupyRange(Pose, MountZ0, MountZ1);
				if (FurnitureKindReservesFloorBelow(Prop.Kind))
				{
					Grid.Occupy(Pose);
				}
			}
			else
			{
				Grid.Occupy(Pose);
			}
			EmitPose(Space, Grid, Pose, Prop.Kind, 0, OutObjects, bMounted ? MountBottom : 0.f);
			++OutPlaced;
			if (bReduced)
			{
				OutNotes.Add(FString::Printf(TEXT("%s %s placed with reduced clearance"), *SpaceLabel(Space), *PropName));
			}
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
		OutDoor.SillCm = 0.f;
		OutDoor.HeightCm = 210.f;
		double Height = 0.0;
		if (TryNumber(Object, TEXT("height"), Height) && Height > 0.0)
		{
			OutDoor.HeightCm = static_cast<float>(Height * UnitScale);
		}
		return OutDoor.SwingCm > 0.f;
	}

	bool ReadWindow(const TSharedPtr<FJsonObject>& Object, double UnitScale, FDoorSpec& OutWindow)
	{
		FString WallText;
		if (!ReadStringField(Object, { TEXT("wall"), TEXT("side") }, WallText) || !ParseWall(WallText, OutWindow.Wall))
		{
			return false;
		}
		double Offset = 0.0;
		double Width = 0.0;
		double Depth = 40.0 / UnitScale;
		TryNumber(Object, TEXT("offset"), Offset) || TryNumber(Object, TEXT("along"), Offset) || TryNumber(Object, TEXT("start"), Offset);
		if (!TryNumber(Object, TEXT("width"), Width) && !TryNumber(Object, TEXT("span"), Width))
		{
			return false;
		}
		TryNumber(Object, TEXT("depth"), Depth) || TryNumber(Object, TEXT("clearance"), Depth);
		if (Width <= 0.0 || Depth <= 0.0)
		{
			return false;
		}
		OutWindow.OffsetCm = static_cast<float>(Offset * UnitScale);
		OutWindow.WidthCm = static_cast<float>(Width * UnitScale);
		OutWindow.SwingCm = static_cast<float>(Depth * UnitScale);
		double Sill = 0.0;
		double Height = 0.0;
		const bool bHasSill = TryNumber(Object, TEXT("sill"), Sill) || TryNumber(Object, TEXT("bottom"), Sill) || TryNumber(Object, TEXT("z"), Sill);
		const bool bHasHeight = TryNumber(Object, TEXT("height"), Height);
		OutWindow.SillCm = bHasSill && Sill >= 0.0 ? static_cast<float>(Sill * UnitScale) : 90.f;
		OutWindow.HeightCm = bHasHeight && Height > 0.0 ? static_cast<float>(Height * UnitScale) : 120.f;
		return true;
	}

	bool ReadSpace(const TSharedPtr<FJsonObject>& Object, double UnitScale, bool bForceHall, int32 Index, FSpaceSpec& OutSpace, FString& OutError)
	{
		OutSpace = FSpaceSpec();
		OutSpace.Index = Index;
		FString TvText;
		if (ReadStringField(Object, { TEXT("tv"), TEXT("tvMount") }, TvText))
		{
			OutSpace.TvMode = TvText.Contains(TEXT("wall")) ? 1 : (TvText.Contains(TEXT("stand")) ? 2 : 0);
		}
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

		float TotalX = 0.f;
		float TotalY = 0.f;
		if (ReadVector2(FindField(Object, { TEXT("totalSize") }), TotalX, TotalY) && TotalX > 1.f && TotalY > 1.f)
		{
			OutSpace.TotalSizeX = TotalX * static_cast<float>(UnitScale);
			OutSpace.TotalSizeY = TotalY * static_cast<float>(UnitScale);
			OutSpace.bHasTotalSize = true;
		}

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
		if (TSharedPtr<FJsonValue> WindowsValue = FindField(Object, { TEXT("windows") }))
		{
			TArray<TSharedPtr<FJsonValue>> WindowValues;
			if (WindowsValue->Type == EJson::Array)
			{
				WindowValues = WindowsValue->AsArray();
			}
			else if (WindowsValue->Type == EJson::Object)
			{
				WindowValues.Add(WindowsValue);
			}
			for (const TSharedPtr<FJsonValue>& WindowValue : WindowValues)
			{
				if (!WindowValue.IsValid() || WindowValue->Type != EJson::Object)
				{
					continue;
				}
				FDoorSpec Window;
				if (ReadWindow(WindowValue->AsObject(), UnitScale, Window))
				{
					OutSpace.Windows.Add(Window);
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
		(void)Notes;

		TArray<TSharedPtr<FJsonValue>> Items;
		Items.Reserve(Objects.Num());
		for (const FExportedObject& Object : Objects)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("type"), Object.Category);
			Item->SetStringField(TEXT("id"), Object.Id);
			Item->SetStringField(TEXT("label"), Object.Label);
			Item->SetStringField(TEXT("source"), TEXT("compute"));

			TArray<TSharedPtr<FJsonValue>> Position;
			Position.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.X)));
			Position.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.Y)));
			Position.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.Z)));
			Item->SetArrayField(TEXT("position"), Position);

			TSharedRef<FJsonObject> Rotation = MakeShared<FJsonObject>();
			Rotation->SetNumberField(TEXT("pitch"), 0.0);
			Rotation->SetNumberField(TEXT("yaw"), static_cast<double>(Object.Yaw));
			Rotation->SetNumberField(TEXT("roll"), 0.0);
			Item->SetObjectField(TEXT("rotation"), Rotation);
			Item->SetNumberField(TEXT("scale"), static_cast<double>(Object.UniformScale));

			TArray<TSharedPtr<FJsonValue>> Size;
			Size.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.SizeX)));
			Size.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.SizeY)));
			if (Object.SizeZ > 1.f)
			{
				Size.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Object.SizeZ)));
			}
			Item->SetArrayField(TEXT("size"), Size);
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("units"), TEXT("cm"));
		Root->SetStringField(TEXT("source"), TEXT("compute"));
		Root->SetArrayField(TEXT("objects"), Items);

		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	void WriteNumberPair(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, float A, float B)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(static_cast<double>(A)));
		Values.Add(MakeShared<FJsonValueNumber>(static_cast<double>(B)));
		Object->SetArrayField(Name, Values);
	}

	float RoundRoomMeasure(float Value)
	{
		return FMath::RoundToFloat(Value * 100.f) / 100.f;
	}

	bool MeasureSpaceObject(const TSharedPtr<FJsonObject>& Object, const FString& Units, FString& OutNote)
	{
		float SizeX = 0.f;
		float SizeY = 0.f;
		if (!ReadVector2(FindField(Object, { TEXT("size"), TEXT("dimensions"), TEXT("extent") }), SizeX, SizeY) || SizeX <= 1.f || SizeY <= 1.f)
		{
			return false;
		}

		float SwingWest = 0.f;
		float SwingEast = 0.f;
		float SwingNorth = 0.f;
		float SwingSouth = 0.f;
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
				const TSharedPtr<FJsonObject> Door = DoorValue->AsObject();
				FString WallText;
				EPlanWall Wall = EPlanWall::North;
				if (!ReadStringField(Door, { TEXT("wall"), TEXT("side") }, WallText) || !ParseWall(WallText, Wall))
				{
					continue;
				}
				double Width = 0.0;
				double Swing = 0.0;
				if (!TryNumber(Door, TEXT("width"), Width) && !TryNumber(Door, TEXT("span"), Width))
				{
					continue;
				}
				const bool bHasSwing = TryNumber(Door, TEXT("swing"), Swing) || TryNumber(Door, TEXT("swingDepth"), Swing) || TryNumber(Door, TEXT("sway"), Swing);
				const float SwingRadius = static_cast<float>(bHasSwing && Swing > 0.0 ? Swing : Width);
				switch (Wall)
				{
				case EPlanWall::West: SwingWest = FMath::Max(SwingWest, SwingRadius); break;
				case EPlanWall::East: SwingEast = FMath::Max(SwingEast, SwingRadius); break;
				case EPlanWall::North: SwingNorth = FMath::Max(SwingNorth, SwingRadius); break;
				case EPlanWall::South: SwingSouth = FMath::Max(SwingSouth, SwingRadius); break;
				default: break;
				}
			}
		}

		const float SwingX = SwingWest + SwingEast;
		const float SwingY = SwingNorth + SwingSouth;
		const float TotalX = FMath::Max(SizeX - SwingX, SizeX * 0.25f);
		const float TotalY = FMath::Max(SizeY - SwingY, SizeY * 0.25f);
		WriteNumberPair(Object, TEXT("wallDistance"), RoundRoomMeasure(SizeX), RoundRoomMeasure(SizeY));
		WriteNumberPair(Object, TEXT("swingRadius"), RoundRoomMeasure(SwingX), RoundRoomMeasure(SwingY));
		WriteNumberPair(Object, TEXT("totalSize"), RoundRoomMeasure(TotalX), RoundRoomMeasure(TotalY));

		FString Name;
		ReadStringField(Object, { TEXT("name"), TEXT("room"), TEXT("label"), TEXT("category") }, Name);
		OutNote = FString::Printf(
			TEXT("%s wall %g x %g %s, swing %g x %g, total %g x %g"),
			*Name,
			RoundRoomMeasure(SizeX),
			RoundRoomMeasure(SizeY),
			*Units,
			RoundRoomMeasure(SwingX),
			RoundRoomMeasure(SwingY),
			RoundRoomMeasure(TotalX),
			RoundRoomMeasure(TotalY));
		return true;
	}

	void MeasureSpaceArray(const TSharedPtr<FJsonValue>& Value, const FString& Units, TArray<FString>& Notes)
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
			FString Note;
			if (MeasureSpaceObject(Entry->AsObject(), Units, Note))
			{
				Notes.Add(Note);
			}
		}
	}

	void UseMeshSizes(const TArray<FFurnitureCatalogMeshSize>* Sizes)
	{
		ActiveMeshSizes = Sizes;
	}

	bool HasMeasuredMesh(const TArray<FFurnitureCatalogMeshSize>& Sizes, const FString& Category)
	{
		for (const FFurnitureCatalogMeshSize& Size : Sizes)
		{
			if (Size.Category.Equals(Category, ESearchCase::IgnoreCase)
				&& Size.ExtentCm.X > 1.f
				&& Size.ExtentCm.Y > 1.f
				&& Size.ExtentCm.Z > 1.f)
			{
				return true;
			}
		}
		return false;
	}

	void AliasNames(EFurnitureKind Kind, TArray<const TCHAR*>& OutNames)
	{
		OutNames.Reset();
		switch (Kind)
		{
		case EFurnitureKind::Television:
			OutNames.Add(TEXT("tv"));
			OutNames.Add(TEXT("TV"));
			break;
		case EFurnitureKind::DiningTable:
			OutNames.Add(TEXT("table"));
			break;
		case EFurnitureKind::DiningChair:
			OutNames.Add(TEXT("chair"));
			break;
		case EFurnitureKind::WashBasin:
			OutNames.Add(TEXT("basin"));
			OutNames.Add(TEXT("washbasin"));
			break;
		case EFurnitureKind::CoffeeTable:
			OutNames.Add(TEXT("coffee"));
			break;
		case EFurnitureKind::KitchenSink:
			OutNames.Add(TEXT("kitchensink"));
			break;
		case EFurnitureKind::UtilitySink:
			OutNames.Add(TEXT("utilitysink"));
			break;
		case EFurnitureKind::BedLamp:
			OutNames.Add(TEXT("lamp"));
			OutNames.Add(TEXT("bedlamp"));
			break;
		case EFurnitureKind::TvStand:
			OutNames.Add(TEXT("tv stand"));
			OutNames.Add(TEXT("tvstand"));
			OutNames.Add(TEXT("tv table"));
			OutNames.Add(TEXT("tvtable"));
			break;
		default:
			break;
		}
	}

	const FFurnitureMeshOption* CatalogMeshForKind(UFurnitureCatalog* Catalog, EFurnitureKind Kind)
	{
		if (Catalog == nullptr)
		{
			return nullptr;
		}
		if (const FFurnitureMeshOption* Option = Catalog->GetNamedCategoryMesh(FurnitureKindLabel(Kind)))
		{
			return Option;
		}
		TArray<const TCHAR*> Aliases;
		AliasNames(Kind, Aliases);
		for (const TCHAR* Alias : Aliases)
		{
			if (const FFurnitureMeshOption* Option = Catalog->GetNamedCategoryMesh(Alias))
			{
				return Option;
			}
		}
		return Catalog->GetCategoryMesh(Kind);
	}

	void AppendCatalogMeshSizes(TArray<FFurnitureCatalogMeshSize>& Sizes)
	{
		UFurnitureCatalog* Catalog = LoadObject<UFurnitureCatalog>(nullptr, TEXT("/Game/Furniture/DA_FurnitureCatalog.DA_FurnitureCatalog"));
		if (Catalog == nullptr)
		{
			Catalog = LoadObject<UFurnitureCatalog>(nullptr, TEXT("/Game/Furniture/DA_FurnitureCatalog"));
		}
		if (Catalog == nullptr)
		{
			return;
		}

		int32 KindCount = 0;
		const EFurnitureKind* Kinds = AllFurnitureKinds(KindCount);
		for (int32 Index = 0; Index < KindCount; ++Index)
		{
			const EFurnitureKind Kind = Kinds[Index];
			const FString Category = FurnitureKindLabel(Kind);
			if (HasMeasuredMesh(Sizes, Category))
			{
				continue;
			}
			const FFurnitureMeshOption* Option = CatalogMeshForKind(Catalog, Kind);
			if (Option == nullptr || Option->Mesh.IsNull())
			{
				continue;
			}
			UStaticMesh* Mesh = Option->Mesh.LoadSynchronous();
			if (Mesh == nullptr)
			{
				continue;
			}
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			FFurnitureCatalogMeshSize Size;
			Size.Category = Category;
			Size.ExtentCm = FVector(Bounds.BoxExtent.X * 2.f, Bounds.BoxExtent.Y * 2.f, Bounds.BoxExtent.Z * 2.f);
			if (Size.ExtentCm.X <= 1.f || Size.ExtentCm.Y <= 1.f || Size.ExtentCm.Z <= 1.f)
			{
				continue;
			}
			Sizes.Add(Size);
		}
	}
}

FFloorplanRoomSizeResult PrepareFloorplanRoomSizes(const FString& JsonText)
{
	FFloorplanRoomSizeResult Result;
	TSharedPtr<FJsonValue> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || Root->Type != EJson::Object)
	{
		return Result;
	}

	const TSharedPtr<FJsonObject> RootObject = Root->AsObject();
	const TSharedPtr<FJsonValue> RoomsValue = FindField(RootObject, { TEXT("rooms") });
	const TSharedPtr<FJsonValue> HallsValue = FindField(RootObject, { TEXT("halls") });
	const TSharedPtr<FJsonValue> SpacesValue = FindField(RootObject, { TEXT("spaces") });
	if (!RoomsValue.IsValid() && !HallsValue.IsValid() && !SpacesValue.IsValid())
	{
		return Result;
	}

	Result.bIsFloorplan = true;
	FString Units = TEXT("cm");
	if (TSharedPtr<FJsonValue> UnitsValue = FindField(RootObject, { TEXT("units") }))
	{
		if (UnitsValue->Type == EJson::String && !UnitsValue->AsString().IsEmpty())
		{
			Units = UnitsValue->AsString();
		}
	}

	TArray<FString> Notes;
	MeasureSpaceArray(RoomsValue, Units, Notes);
	MeasureSpaceArray(HallsValue, Units, Notes);
	MeasureSpaceArray(SpacesValue, Units, Notes);
	if (Notes.Num() == 0)
	{
		Result.Message = TEXT("The floorplan has rooms, but none had a wall size to measure.");
		Result.JsonText = JsonText;
		return Result;
	}

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	Result.JsonText = Out;
	Result.Message = FString::Printf(TEXT("Measured %d rooms from wall distance and swing. %s."), Notes.Num(), *FString::Join(Notes, TEXT("; ")));
	return Result;
}

FFurnitureComputeResult ComputeFurnitureAssembly(const FString& JsonText, const TArray<FFurnitureCatalogMeshSize>& MeshSizes)
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
	float PlanOriginX = 0.f;
	float PlanOriginY = 0.f;
	float PlanOriginZ = 0.f;
	if (TSharedPtr<FJsonValue> OriginValue = FindField(RootObject, { TEXT("origin") }))
	{
		float OriginX = 0.f;
		float OriginY = 0.f;
		if (ReadVector2(OriginValue, OriginX, OriginY))
		{
			PlanOriginX = OriginX * static_cast<float>(UnitScale);
			PlanOriginY = OriginY * static_cast<float>(UnitScale);
			if (OriginValue->Type == EJson::Array && OriginValue->AsArray().Num() > 2 && OriginValue->AsArray()[2].IsValid())
			{
				PlanOriginZ = static_cast<float>(OriginValue->AsArray()[2]->AsNumber() * UnitScale);
			}
		}
	}

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
	TArray<FFurnitureCatalogMeshSize> MeasuredMeshes = MeshSizes;
	AppendCatalogMeshSizes(MeasuredMeshes);
	UseMeshSizes(&MeasuredMeshes);
	const FResolvedMesh TvMesh = ResolveCatalogMesh(MeasuredMeshes, { TEXT("Television"), TEXT("tv"), TEXT("TV") });
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
		if (!BuildGrid(Space, PlanOriginX, PlanOriginY, PlanOriginZ, Grid, GridError))
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
		PlaceSpace(Space, Grid, TvMesh, Objects, Notes, Placed);
	}

	int32 MeasuredCount = 0;
	for (const FFurnitureCatalogMeshSize& Size : MeasuredMeshes)
	{
		if (Size.ExtentCm.X > 1.f && Size.ExtentCm.Y > 1.f && Size.ExtentCm.Z > 1.f)
		{
			++MeasuredCount;
		}
	}
	Result.Message = FString::Printf(TEXT("No object data. 10 cm cubes mark walls, door swings, and windows. Props use the ground cubes in category order. Read %d rooms and %d halls. Placed %d objects from %d catalog meshes."), RoomCount, HallCount, Placed, MeasuredCount);
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
	UseMeshSizes(nullptr);
	return Result;
}
