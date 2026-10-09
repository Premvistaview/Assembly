#include "Furniture/FurnitureJsonImport.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <initializer_list>

namespace
{
	bool TryNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double& OutValue)
	{
		return Object.IsValid() && Object->TryGetNumberField(Name, OutValue);
	}

	bool ReadVectorValue(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
			if (Values.Num() < 2)
			{
				return false;
			}
			OutVector = FVector(
				Values[0]->AsNumber(),
				Values[1]->AsNumber(),
				Values.Num() > 2 ? Values[2]->AsNumber() : 0.0);
			return true;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			const bool bX = TryNumber(Object, TEXT("x"), X) || TryNumber(Object, TEXT("X"), X);
			const bool bY = TryNumber(Object, TEXT("y"), Y) || TryNumber(Object, TEXT("Y"), Y);
			TryNumber(Object, TEXT("z"), Z) || TryNumber(Object, TEXT("Z"), Z);
			if (!bX || !bY)
			{
				return false;
			}
			OutVector = FVector(X, Y, Z);
			return true;
		}

		return false;
	}

	bool ReadScaleValue(const TSharedPtr<FJsonValue>& Value, FVector& OutScale)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Number)
		{
			const double Uniform = Value->AsNumber();
			if (FMath::IsNearlyZero(Uniform))
			{
				return false;
			}
			OutScale = FVector(Uniform);
			return true;
		}

		return ReadVectorValue(Value, OutScale) && !OutScale.IsNearlyZero();
	}

	bool ReadRotationValue(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotation)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Number)
		{
			OutRotation = FRotator(0.0, Value->AsNumber(), 0.0);
			return true;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
			if (Values.Num() == 1)
			{
				OutRotation = FRotator(0.0, Values[0]->AsNumber(), 0.0);
				return true;
			}
			if (Values.Num() >= 3)
			{
				OutRotation = FRotator(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
				return true;
			}
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			double Pitch = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			const bool bYaw = TryNumber(Object, TEXT("yaw"), Yaw) || TryNumber(Object, TEXT("Yaw"), Yaw);
			TryNumber(Object, TEXT("pitch"), Pitch) || TryNumber(Object, TEXT("Pitch"), Pitch);
			TryNumber(Object, TEXT("roll"), Roll) || TryNumber(Object, TEXT("Roll"), Roll);
			if (!bYaw)
			{
				return false;
			}
			OutRotation = FRotator(Pitch, Yaw, Roll);
			return true;
		}

		return false;
	}

	TSharedPtr<FJsonValue> FindField(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Names)
	{
		for (const TCHAR* Name : Names)
		{
			if (TSharedPtr<FJsonValue> Field = Object->TryGetField(Name))
			{
				return Field;
			}
		}
		return nullptr;
	}

	bool ReadStringField(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Names, FString& OutValue)
	{
		for (const TCHAR* Name : Names)
		{
			if (Object->TryGetStringField(Name, OutValue) && !OutValue.IsEmpty())
			{
				return true;
			}
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

	const TArray<TSharedPtr<FJsonValue>>* DetectionArrayIfValid(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->Type != EJson::Array || Value->AsArray().Num() == 0)
		{
			return nullptr;
		}
		const TSharedPtr<FJsonValue>& First = Value->AsArray()[0];
		if (!First.IsValid() || First->Type != EJson::Object)
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject> FirstObject = First->AsObject();
		const bool bLooksLikeDetection =
			FirstObject->HasField(TEXT("type")) || FirstObject->HasField(TEXT("category")) || FirstObject->HasField(TEXT("kind"));
		const bool bHasPlace =
			FirstObject->HasField(TEXT("position")) || FirstObject->HasField(TEXT("location")) || FirstObject->HasField(TEXT("translation"));
		if (!bLooksLikeDetection || !bHasPlace)
		{
			return nullptr;
		}
		return &Value->AsArray();
	}

	const TArray<TSharedPtr<FJsonValue>>* FindObjectArray(const TSharedPtr<FJsonValue>& Root)
	{
		if (!Root.IsValid())
		{
			return nullptr;
		}
		if (Root->Type == EJson::Array)
		{
			return DetectionArrayIfValid(Root);
		}
		if (Root->Type != EJson::Object)
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject> Object = Root->AsObject();
		if (const TArray<TSharedPtr<FJsonValue>>* Objects = DetectionArrayIfValid(Object->TryGetField(TEXT("objects"))))
		{
			return Objects;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			if (Field.Key.Equals(TEXT("objects"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (const TArray<TSharedPtr<FJsonValue>>* Objects = DetectionArrayIfValid(Field.Value))
			{
				return Objects;
			}
		}
		return nullptr;
	}

	FString ReadUnits(const TSharedPtr<FJsonValue>& Root)
	{
		if (!Root.IsValid() || Root->Type != EJson::Object)
		{
			return TEXT("cm");
		}
		FString Units;
		if (Root->AsObject()->TryGetStringField(TEXT("units"), Units))
		{
			return Units;
		}
		return TEXT("cm");
	}
}

FFurnitureJsonImportResult ImportFurnitureDetectionJson(const FString& JsonText)
{
	FFurnitureJsonImportResult Result;
	TSharedPtr<FJsonValue> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.Message = TEXT("This file is not valid JSON.");
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* Objects = FindObjectArray(Root);
	if (Objects == nullptr || Objects->Num() == 0)
	{
		Result.Message = TEXT("This JSON does not contain object detection data. Expected a list of objects with a type, position, and rotation.");
		return Result;
	}

	const double CentimetersPerUnit = UnitToCentimeters(ReadUnits(Root));
	bool bFromCompute = false;
	if (Root->Type == EJson::Object)
	{
		FString Source;
		if (Root->AsObject()->TryGetStringField(TEXT("source"), Source) && Source.Equals(TEXT("compute"), ESearchCase::IgnoreCase))
		{
			bFromCompute = true;
		}
	}
	int32 MissingFields = 0;

	for (int32 Index = 0; Index < Objects->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& Value = (*Objects)[Index];
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			++MissingFields;
			continue;
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		FString Category;
		const bool bHasCategory = ReadStringField(Object, { TEXT("category"), TEXT("kind"), TEXT("type"), TEXT("class"), TEXT("className") }, Category);
		FString Label;
		ReadStringField(Object, { TEXT("label"), TEXT("name") }, Label);
		if (!bHasCategory)
		{
			Category = Label;
		}
		if (Category.IsEmpty())
		{
			++MissingFields;
			continue;
		}

		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Scale = FVector::OneVector;
		FVector Size = FVector::ZeroVector;
		const bool bHasLocation = ReadVectorValue(FindField(Object, { TEXT("location"), TEXT("position"), TEXT("translation") }), Location);
		bool bHasRotation = ReadRotationValue(FindField(Object, { TEXT("rotation"), TEXT("rotator"), TEXT("yaw"), TEXT("angle") }), Rotation);
		if (!bHasRotation)
		{
			double Yaw = 0.0;
			if (TryNumber(Object, TEXT("yaw"), Yaw) || TryNumber(Object, TEXT("angle"), Yaw))
			{
				Rotation = FRotator(0.0, Yaw, 0.0);
				bHasRotation = true;
			}
		}
		const bool bHasScale = ReadScaleValue(FindField(Object, { TEXT("scale") }), Scale);
		ReadVectorValue(FindField(Object, { TEXT("size"), TEXT("dimensions") }), Size);
		if (!bHasLocation || !bHasRotation)
		{
			++MissingFields;
			continue;
		}
		if (!bHasScale)
		{
			Scale = FVector::OneVector;
		}

		FDetectedFurnitureObject Detected;
		FString Id;
		if (!ReadStringField(Object, { TEXT("id"), TEXT("name") }, Id) || Id.Equals(Category, ESearchCase::IgnoreCase))
		{
			Id = FString::Printf(TEXT("%s_%d"), *Category, Index + 1);
		}
		Detected.Id = FName(*Id);
		Detected.Category = Category;
		Detected.Label = Label.IsEmpty() ? Category : Label;
		FString ObjectSource;
		bool bObjectFromCompute = bFromCompute;
		if (Object->TryGetStringField(TEXT("source"), ObjectSource) && ObjectSource.Equals(TEXT("compute"), ESearchCase::IgnoreCase))
		{
			bObjectFromCompute = true;
		}

		Detected.Location = Location * CentimetersPerUnit;
		Detected.Rotation = Rotation;
		Detected.Scale = Scale;
		Detected.Size = Size * CentimetersPerUnit;
		Detected.bPlaceAtBoundsCenter = bObjectFromCompute;
		Result.Objects.Add(Detected);
	}

	Result.SkippedCount = MissingFields;
	if (Result.Objects.Num() == 0)
	{
		Result.bIsDetectionData = false;
		Result.Message = TEXT("This JSON does not contain object detection data. Each object needs a category and a position, plus a rotation.");
		return Result;
	}

	TSet<FString> Categories;
	for (const FDetectedFurnitureObject& Object : Result.Objects)
	{
		bool bAlready = false;
		for (const FString& Existing : Categories)
		{
			if (Existing.Equals(Object.Category, ESearchCase::IgnoreCase))
			{
				bAlready = true;
				break;
			}
		}
		if (!bAlready)
		{
			Categories.Add(Object.Category);
		}
	}

	Result.bIsDetectionData = true;
	Result.Message = FString::Printf(
		TEXT("Detection data is valid. %d objects in %d categories."),
		Result.Objects.Num(),
		Categories.Num());
	if (MissingFields > 0)
	{
		Result.Message += FString::Printf(TEXT(" %d entries were skipped because category, position, or rotation was missing."), MissingFields);
	}
	return Result;
}
