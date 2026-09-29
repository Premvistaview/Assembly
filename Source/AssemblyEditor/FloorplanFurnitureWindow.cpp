#include "FloorplanFurnitureWindow.h"

#include "Furniture/FurniturePlacementActor.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	UStaticMesh* LoadPlaceholderMesh()
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_Cube.SM_Cube"));
		if (Mesh == nullptr)
		{
			Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}
		return Mesh;
	}

	UMaterialInterface* LoadPlaceholderMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	}

	FString FormatVector(const FVector& Value)
	{
		return FString::Printf(TEXT("%.1f, %.1f, %.1f"), Value.X, Value.Y, Value.Z);
	}

	FString FormatRotator(const FRotator& Value)
	{
		return FString::Printf(TEXT("P %.0f  Y %.0f  R %.0f"), Value.Pitch, Value.Yaw, Value.Roll);
	}
}

void SFloorplanFurnitureWindow::Construct(const FArguments& InArgs)
{
	LoadOrCreateCatalog();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Furniture placement")))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(TEXT("Import a JSON file of detected objects. Each object must include a category, location, rotation, and scale. Assign one premodel mesh to each category, then place the objects. Detection and placement are not edited by hand.")))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SButton).OnClicked(this, &SFloorplanFurnitureWindow::ImportJsonClicked)
					[ SNew(STextBlock).Text(FText::FromString(TEXT("Import JSON"))) ]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SFloorplanFurnitureWindow::PlaceObjectsClicked)
					.IsEnabled_Lambda([this]() { return CanPlace(); })
					[ SNew(STextBlock).Text(FText::FromString(TEXT("Place Objects"))) ]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Detected objects")))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(280.f)
						[
							SAssignNew(ListView, SListView<TSharedPtr<FImportedFurnitureItem>>)
							.ListItemsSource(&Objects)
							.SelectionMode(ESelectionMode::None)
							.OnGenerateRow(this, &SFloorplanFurnitureWindow::OnGenerateRow)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)
					[ SNew(SSeparator) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Mesh categories")))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 4.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(FText::FromString(TEXT("Every object in a category uses the same premodel mesh. Set the mesh once for Toilet, and every toilet in the JSON uses it.")))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(CategoryList, SVerticalBox)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
					[
						SAssignNew(PickerHost, SBox)
						.Visibility(EVisibility::Collapsed)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SFloorplanFurnitureWindow::GetStatusText)
			]
		]
	];

	SetStatus(TEXT("Import a JSON file. The file is accepted only when it contains object detection data."));
	RebuildCategories();
}

TSharedRef<ITableRow> SFloorplanFurnitureWindow::OnGenerateRow(TSharedPtr<FImportedFurnitureItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 Index = Objects.IndexOfByKey(Item);
	const bool bShowCategory = Item.IsValid() && (Index <= 0 || !Objects[Index - 1].IsValid() || !Objects[Index - 1]->Object.Category.Equals(Item->Object.Category, ESearchCase::IgnoreCase));
	const FString Line = Item.IsValid()
		? FString::Printf(TEXT("%s    loc %s    rot %s    scale %s    mesh %s"),
			*Item->Object.Label,
			*FormatVector(Item->Object.Location),
			*FormatRotator(Item->Object.Rotation),
			*FormatVector(Item->Object.Scale),
			*CategoryMeshLabel(Item->Object.Category))
		: FString();

	return SNew(STableRow<TSharedPtr<FImportedFurnitureItem>>, OwnerTable)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, bShowCategory ? 6.f : 0.f, 4.f, 0.f)
			[
				SNew(STextBlock)
				.Visibility(bShowCategory ? EVisibility::Visible : EVisibility::Collapsed)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.Text(FText::FromString(Item.IsValid() && bShowCategory ? Item->Object.Category : TEXT("")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(12.f, 1.f, 4.f, 1.f)
			[
				SNew(STextBlock).Text(FText::FromString(Line))
			]
		];
}

FReply SFloorplanFurnitureWindow::ImportJsonClicked()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (Desktop == nullptr)
	{
		return FReply::Handled();
	}

	TArray<FString> Files;
	const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const FString DefaultPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Furniture/Detections"));
	const bool bOpened = Desktop->OpenFileDialog(
		Parent,
		TEXT("Import Detection JSON"),
		DefaultPath,
		TEXT(""),
		TEXT("Detection JSON|*.json"),
		0,
		Files);
	if (bOpened && Files.Num() > 0)
	{
		ImportJsonFile(Files[0]);
	}
	return FReply::Handled();
}

FReply SFloorplanFurnitureWindow::PlaceObjectsClicked()
{
	if (!JsonPath.IsEmpty())
	{
		ImportJsonFile(JsonPath);
	}

	if (!CanPlace())
	{
		SetStatus(TEXT("Import detection JSON before placing objects."));
		return FReply::Handled();
	}
	if (GEditor == nullptr || GEditor->PlayWorld != nullptr)
	{
		SetStatus(TEXT("Stop Play In Editor, then place the objects into the open level."));
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UEditorActorSubsystem* Actors = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (World == nullptr || Actors == nullptr)
	{
		SetStatus(TEXT("Open a level before placing objects."));
		return FReply::Handled();
	}

	UStaticMesh* PlaceholderMesh = LoadPlaceholderMesh();
	UMaterialInterface* PlaceholderMaterial = LoadPlaceholderMaterial();
	FScopedTransaction Transaction(NSLOCTEXT("FurniturePlacement", "PlaceObjects", "Place Detected Furniture"));
	GEditor->SelectNone(false, true, false);

	TSet<FString> MissingMeshes;
	int32 Placed = 0;
	for (const TSharedPtr<FImportedFurnitureItem>& Item : Objects)
	{
		if (!Item.IsValid())
		{
			continue;
		}

		UStaticMesh* Mesh = nullptr;
		float YawOffset = 0.f;
		bool bPlaceholder = true;
		if (Catalog.IsValid())
		{
			if (const FFurnitureMeshOption* Option = Catalog->GetNamedCategoryMesh(Item->Object.Category))
			{
				Mesh = LoadObject<UStaticMesh>(nullptr, *Option->Mesh.ToSoftObjectPath().ToString());
				if (Mesh != nullptr)
				{
					bPlaceholder = false;
					YawOffset = Option->YawOffsetDegrees;
				}
			}
		}
		if (Mesh == nullptr)
		{
			Mesh = PlaceholderMesh;
			bPlaceholder = true;
			MissingMeshes.Add(Item->Object.Category);
		}
		if (Mesh == nullptr)
		{
			continue;
		}

		AFurniturePlacementActor* Existing = nullptr;
		for (TActorIterator<AFurniturePlacementActor> It(World); It; ++It)
		{
			if (It->DetectionId == Item->Object.Id)
			{
				Existing = *It;
				break;
			}
		}

		if (Existing != nullptr)
		{
			Actors->DestroyActor(Existing);
			Existing = nullptr;
		}

		FRotator SpawnRotation = Item->Object.Rotation;
		SpawnRotation.Yaw += YawOffset;
		Existing = Cast<AFurniturePlacementActor>(Actors->SpawnActorFromClass(
			AFurniturePlacementActor::StaticClass(),
			Item->Object.Location,
			SpawnRotation));
		if (Existing == nullptr)
		{
			continue;
		}

		Existing->ApplyDetectedObject(Item->Object, Mesh, YawOffset, bPlaceholder, PlaceholderMaterial);
		GEditor->SelectActor(Existing, true, false, true);
		++Placed;
	}

	GEditor->NoteSelectionChange();
	GEditor->RedrawLevelEditingViewports();

	FString Message = FString::Printf(TEXT("Placed %d objects. Each yaw was read from that object's JSON rotation."), Placed);
	if (MissingMeshes.Num() > 0)
	{
		Message += TEXT(" Categories still using a block: ");
		bool bFirst = true;
		for (const FString& Category : MissingMeshes)
		{
			if (!bFirst)
			{
				Message += TEXT(", ");
			}
			Message += Category;
			bFirst = false;
		}
		Message += TEXT(".");
	}
	SetStatus(Message);
	RefreshObjects();
	return FReply::Handled();
}

FReply SFloorplanFurnitureWindow::OpenCategoryMeshPicker(FString Category)
{
	if (!PickerHost.IsValid())
	{
		return FReply::Handled();
	}

	PickerCategory = Category;

	FAssetPickerConfig Config;
	Config.SelectionMode = ESelectionMode::Single;
	Config.InitialAssetViewType = EAssetViewType::List;
	Config.bFocusSearchBoxWhenOpened = true;
	Config.bAllowDragging = false;
	Config.Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	Config.Filter.bRecursiveClasses = true;
	Config.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SFloorplanFurnitureWindow::HandleMeshPicked);

	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	PickerHost->SetContent(
		SNew(SBox)
		.HeightOverride(260.f)
		[
			ContentBrowser.Get().CreateAssetPicker(Config)
		]);
	PickerHost->SetVisibility(EVisibility::Visible);
	SetStatus(FString::Printf(TEXT("Choose the premodel mesh for %s. Every %s in the JSON will use it."), *Category, *Category));
	return FReply::Handled();
}

void SFloorplanFurnitureWindow::HandleMeshPicked(const FAssetData& AssetData)
{
	if (!Catalog.IsValid() || PickerCategory.IsEmpty())
	{
		return;
	}

	Catalog->Modify();
	Catalog->SetNamedCategoryMesh(PickerCategory, AssetData.AssetName.ToString(), AssetData.GetSoftObjectPath());
	SaveCatalog();

	if (PickerHost.IsValid())
	{
		PickerHost->SetVisibility(EVisibility::Collapsed);
		PickerHost->SetContent(SNullWidget::NullWidget);
	}
	RebuildCategories();
	RefreshObjects();
	SetStatus(FString::Printf(TEXT("%s uses %s. Every object in that category keeps this mesh."), *PickerCategory, *AssetData.AssetName.ToString()));
}

void SFloorplanFurnitureWindow::ImportJsonFile(const FString& Path)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		bJsonValid = false;
		Objects.Reset();
		Categories.Reset();
		JsonPath.Reset();
		RefreshObjects();
		RebuildCategories();
		SetStatus(FString::Printf(TEXT("Could not read %s."), *Path));
		return;
	}

	const FFurnitureJsonImportResult Imported = ImportFurnitureDetectionJson(JsonText);
	Objects.Reset();
	Categories.Reset();
	JsonPath = Path;
	bJsonValid = Imported.bIsDetectionData;

	if (!Imported.bIsDetectionData)
	{
		RefreshObjects();
		RebuildCategories();
		SetStatus(Imported.Message);
		return;
	}

	for (const FDetectedFurnitureObject& Object : Imported.Objects)
	{
		TSharedPtr<FImportedFurnitureItem> Item = MakeShared<FImportedFurnitureItem>();
		Item->Object = Object;
		Objects.Add(Item);

		bool bFound = false;
		for (const FString& Category : Categories)
		{
			if (Category.Equals(Object.Category, ESearchCase::IgnoreCase))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			Categories.Add(Object.Category);
			if (Catalog.IsValid())
			{
				Catalog->EnsureNamedCategory(Object.Category);
			}
		}
	}

	Objects.Sort([](const TSharedPtr<FImportedFurnitureItem>& A, const TSharedPtr<FImportedFurnitureItem>& B)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return A.IsValid();
		}
		const int32 CategoryCompare = A->Object.Category.Compare(B->Object.Category, ESearchCase::IgnoreCase);
		if (CategoryCompare != 0)
		{
			return CategoryCompare < 0;
		}
		return A->Object.Label < B->Object.Label;
	});

	RefreshObjects();
	RebuildCategories();
	SetStatus(FString::Printf(TEXT("%s  %s"), *FPaths::GetCleanFilename(Path), *Imported.Message));
}

void SFloorplanFurnitureWindow::RefreshObjects()
{
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SFloorplanFurnitureWindow::RebuildCategories()
{
	if (!CategoryList.IsValid())
	{
		return;
	}

	CategoryList->ClearChildren();
	if (Categories.Num() == 0)
	{
		CategoryList->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Import JSON to see categories.")))
		];
		return;
	}

	for (const FString& Category : Categories)
	{
		const bool bHasMesh = Catalog.IsValid() && Catalog->GetNamedCategoryMesh(Category) != nullptr;
		const FString Line = FString::Printf(TEXT("%s  →  %s    (%d)"), *Category, *CategoryMeshLabel(Category), CountInCategory(Category));
		CategoryList->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Line))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.OnClicked_Lambda([this, Category]()
				{
					return OpenCategoryMeshPicker(Category);
				})
				[
					SNew(STextBlock).Text(FText::FromString(bHasMesh ? TEXT("Change") : TEXT("Set Mesh")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Visibility(bHasMesh ? EVisibility::Visible : EVisibility::Collapsed)
				.OnClicked_Lambda([this, Category]()
				{
					if (Catalog.IsValid())
					{
						Catalog->Modify();
						Catalog->ClearNamedCategoryMesh(Category);
						SaveCatalog();
					}
					RebuildCategories();
					RefreshObjects();
					SetStatus(FString::Printf(TEXT("Cleared the premodel mesh for %s."), *Category));
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Clear")))
				]
			]
		];
	}
}

void SFloorplanFurnitureWindow::LoadOrCreateCatalog()
{
	const TCHAR* AssetPath = TEXT("/Game/Furniture/DA_FurnitureCatalog.DA_FurnitureCatalog");
	if (UFurnitureCatalog* Existing = LoadObject<UFurnitureCatalog>(nullptr, AssetPath))
	{
		Catalog.Reset(Existing);
		return;
	}

	UPackage* Package = CreatePackage(TEXT("/Game/Furniture/DA_FurnitureCatalog"));
	UFurnitureCatalog* Created = NewObject<UFurnitureCatalog>(Package, TEXT("DA_FurnitureCatalog"), RF_Public | RF_Standalone);
	Created->EnsureAllKinds();
	FAssetRegistryModule::AssetCreated(Created);
	Catalog.Reset(Created);
	SaveCatalog();
}

void SFloorplanFurnitureWindow::SaveCatalog()
{
	if (!Catalog.IsValid())
	{
		return;
	}

	UPackage* Package = Catalog->GetPackage();
	if (Package == nullptr)
	{
		return;
	}

	Catalog->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Catalog.Get(), *Filename, SaveArgs))
	{
		SetStatus(TEXT("Could not save the mesh catalog. Category meshes stay in memory until the editor closes."));
	}
}

void SFloorplanFurnitureWindow::SetStatus(const FString& InStatus)
{
	Status = FText::FromString(InStatus);
}

FString SFloorplanFurnitureWindow::CategoryMeshLabel(const FString& Category) const
{
	if (Catalog.IsValid())
	{
		if (const FFurnitureMeshOption* Option = Catalog->GetNamedCategoryMesh(Category))
		{
			if (!Option->DisplayName.IsEmpty())
			{
				return Option->DisplayName;
			}
			return FPaths::GetBaseFilename(Option->Mesh.ToSoftObjectPath().ToString());
		}
	}
	return TEXT("no mesh");
}

int32 SFloorplanFurnitureWindow::CountInCategory(const FString& Category) const
{
	int32 Count = 0;
	for (const TSharedPtr<FImportedFurnitureItem>& Item : Objects)
	{
		if (Item.IsValid() && Item->Object.Category.Equals(Category, ESearchCase::IgnoreCase))
		{
			++Count;
		}
	}
	return Count;
}

FText SFloorplanFurnitureWindow::GetStatusText() const
{
	return Status;
}

bool SFloorplanFurnitureWindow::CanPlace() const
{
	return bJsonValid && Objects.Num() > 0;
}
