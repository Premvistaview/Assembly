#include "Furniture/FurnitureCatalog.h"

void UFurnitureCatalog::EnsureAllKinds()
{
	int32 Count = 0;
	const EFurnitureKind* KindList = AllFurnitureKinds(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FindKind(KindList[Index]) == nullptr)
		{
			FFurnitureKindCatalog& Created = Kinds.AddDefaulted_GetRef();
			Created.Kind = KindList[Index];
		}
	}
}

FFurnitureKindCatalog* UFurnitureCatalog::FindKind(EFurnitureKind Kind)
{
	for (FFurnitureKindCatalog& Entry : Kinds)
	{
		if (Entry.Kind == Kind)
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FFurnitureKindCatalog* UFurnitureCatalog::FindKind(EFurnitureKind Kind) const
{
	for (const FFurnitureKindCatalog& Entry : Kinds)
	{
		if (Entry.Kind == Kind)
		{
			return &Entry;
		}
	}
	return nullptr;
}

FFurnitureMeshOption* UFurnitureCatalog::GetCategoryMesh(EFurnitureKind Kind)
{
	FFurnitureKindCatalog* Entry = FindKind(Kind);
	if (Entry != nullptr && Entry->bHasMesh && !Entry->Mesh.Mesh.IsNull())
	{
		return &Entry->Mesh;
	}
	return nullptr;
}

const FFurnitureMeshOption* UFurnitureCatalog::GetCategoryMesh(EFurnitureKind Kind) const
{
	const FFurnitureKindCatalog* Entry = FindKind(Kind);
	if (Entry != nullptr && Entry->bHasMesh && !Entry->Mesh.Mesh.IsNull())
	{
		return &Entry->Mesh;
	}
	return nullptr;
}

void UFurnitureCatalog::SetCategoryMesh(EFurnitureKind Kind, const FString& DisplayName, const FSoftObjectPath& MeshPath)
{
	if (!MeshPath.IsValid())
	{
		return;
	}

	FFurnitureKindCatalog* Entry = FindKind(Kind);
	if (Entry == nullptr)
	{
		Entry = &Kinds.AddDefaulted_GetRef();
		Entry->Kind = Kind;
	}

	const float KeptYaw = Entry->bHasMesh ? Entry->Mesh.YawOffsetDegrees : 0.f;
	Entry->bHasMesh = true;
	Entry->Mesh.DisplayName = DisplayName.IsEmpty() ? MeshPath.GetAssetName() : DisplayName;
	Entry->Mesh.Mesh = TSoftObjectPtr<UStaticMesh>(MeshPath);
	Entry->Mesh.YawOffsetDegrees = KeptYaw;
}

void UFurnitureCatalog::ClearCategoryMesh(EFurnitureKind Kind)
{
	if (FFurnitureKindCatalog* Entry = FindKind(Kind))
	{
		Entry->bHasMesh = false;
		Entry->Mesh = FFurnitureMeshOption();
	}
}

FFurnitureNamedCategory* UFurnitureCatalog::FindNamedCategory(const FString& Name)
{
	for (FFurnitureNamedCategory& Entry : NamedCategories)
	{
		if (Entry.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FFurnitureNamedCategory* UFurnitureCatalog::FindNamedCategory(const FString& Name) const
{
	for (const FFurnitureNamedCategory& Entry : NamedCategories)
	{
		if (Entry.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &Entry;
		}
	}
	return nullptr;
}

void UFurnitureCatalog::EnsureNamedCategory(const FString& Name)
{
	if (Name.IsEmpty() || FindNamedCategory(Name) != nullptr)
	{
		return;
	}

	FFurnitureNamedCategory& Created = NamedCategories.AddDefaulted_GetRef();
	Created.Name = Name;

	EFurnitureKind Kind = EFurnitureKind::Sofa;
	if (TryParseFurnitureKind(Name, Kind))
	{
		if (const FFurnitureMeshOption* Existing = GetCategoryMesh(Kind))
		{
			Created.bHasMesh = true;
			Created.Mesh = *Existing;
		}
	}
}

FFurnitureMeshOption* UFurnitureCatalog::GetNamedCategoryMesh(const FString& Name)
{
	FFurnitureNamedCategory* Entry = FindNamedCategory(Name);
	if (Entry != nullptr && Entry->bHasMesh && !Entry->Mesh.Mesh.IsNull())
	{
		return &Entry->Mesh;
	}
	return nullptr;
}

const FFurnitureMeshOption* UFurnitureCatalog::GetNamedCategoryMesh(const FString& Name) const
{
	const FFurnitureNamedCategory* Entry = FindNamedCategory(Name);
	if (Entry != nullptr && Entry->bHasMesh && !Entry->Mesh.Mesh.IsNull())
	{
		return &Entry->Mesh;
	}
	return nullptr;
}

void UFurnitureCatalog::SetNamedCategoryMesh(const FString& Name, const FString& DisplayName, const FSoftObjectPath& MeshPath)
{
	if (Name.IsEmpty() || !MeshPath.IsValid())
	{
		return;
	}

	EnsureNamedCategory(Name);
	FFurnitureNamedCategory* Entry = FindNamedCategory(Name);
	if (Entry == nullptr)
	{
		return;
	}

	const float KeptYaw = Entry->bHasMesh ? Entry->Mesh.YawOffsetDegrees : 0.f;
	Entry->bHasMesh = true;
	Entry->Mesh.DisplayName = DisplayName.IsEmpty() ? MeshPath.GetAssetName() : DisplayName;
	Entry->Mesh.Mesh = TSoftObjectPtr<UStaticMesh>(MeshPath);
	Entry->Mesh.YawOffsetDegrees = KeptYaw;
}

void UFurnitureCatalog::ClearNamedCategoryMesh(const FString& Name)
{
	if (FFurnitureNamedCategory* Entry = FindNamedCategory(Name))
	{
		Entry->bHasMesh = false;
		Entry->Mesh = FFurnitureMeshOption();
	}
}
