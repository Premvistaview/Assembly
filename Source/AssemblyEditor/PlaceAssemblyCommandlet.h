#pragma once

#include "Commandlets/Commandlet.h"
#include "PlaceAssemblyCommandlet.generated.h"

/** Creates a furnished level from a JSON file and saves it for packaging. */
UCLASS()
class UPlaceAssemblyCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:

	UPlaceAssemblyCommandlet();

	virtual int32 Main(const FString& Params) override;
};
