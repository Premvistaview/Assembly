#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Furniture/FurnitureCatalog.h"
#include "Furniture/FurnitureJsonImport.h"
#include "Furniture/FurnitureTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/SListView.h"

struct FImportedFurnitureItem
{
	FDetectedFurnitureObject Object;
};

/** Imports detection JSON, assigns one premodel mesh per category, and places the objects. */
class SFloorplanFurnitureWindow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SFloorplanFurnitureWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FImportedFurnitureItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	FReply ImportJsonClicked();
	FReply PlaceObjectsClicked();
	FReply OpenCategoryMeshPicker(FString Category);

	void HandleMeshPicked(const struct FAssetData& AssetData);
	void ImportJsonFile(const FString& Path);
	void RefreshObjects();
	void RebuildCategories();
	void LoadOrCreateCatalog();
	void SaveCatalog();
	void SetStatus(const FString& InStatus);

	FString CategoryMeshLabel(const FString& Category) const;
	int32 CountInCategory(const FString& Category) const;
	FText GetStatusText() const;
	bool CanPlace() const;

	TStrongObjectPtr<UFurnitureCatalog> Catalog;
	FString JsonPath;
	FText Status;
	bool bJsonValid = false;
	FString PickerCategory;

	TArray<TSharedPtr<FImportedFurnitureItem>> Objects;
	TArray<FString> Categories;

	TSharedPtr<SListView<TSharedPtr<FImportedFurnitureItem>>> ListView;
	TSharedPtr<SVerticalBox> CategoryList;
	TSharedPtr<SBox> PickerHost;
};
