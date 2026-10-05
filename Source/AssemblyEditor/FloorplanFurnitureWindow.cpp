#include "FloorplanFurnitureWindow.h"

#include "Furniture/FurnitureComputeAssembly.h"
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
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

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

	void AddAlias(TArray<FString>& Aliases, const TCHAR* Name)
	{
		Aliases.Add(Name);
	}

	/** Copies a mesh from a detection name such as "tv" onto the catalog category Compute Assembly uses. */
	void AdoptAliasMesh(UFurnitureCatalog* Catalog, const FString& Category)
	{
		if (Catalog == nullptr || Catalog->GetNamedCategoryMesh(Category) != nullptr)
		{
			return;
		}

		TArray<FString> Aliases;
		if (Category.Equals(TEXT("Television"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("tv"));
		}
		else if (Category.Equals(TEXT("Dining Table"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("table"));
		}
		else if (Category.Equals(TEXT("Dining Chair"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("chair"));
		}
		else if (Category.Equals(TEXT("Wash Basin"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("basin"));
			AddAlias(Aliases, TEXT("washbasin"));
		}
		else if (Category.Equals(TEXT("Coffee Table"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("coffee"));
		}
		else if (Category.Equals(TEXT("Kitchen Sink"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("kitchensink"));
		}
		else if (Category.Equals(TEXT("Utility Sink"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("utilitysink"));
		}
		else if (Category.Equals(TEXT("Bed Lamp"), ESearchCase::IgnoreCase))
		{
			AddAlias(Aliases, TEXT("lamp"));
			AddAlias(Aliases, TEXT("bedlamp"));
		}

		for (const FString& Alias : Aliases)
		{
			const FFurnitureMeshOption* Option = Catalog->GetNamedCategoryMesh(Alias);
			if (Option == nullptr)
			{
				continue;
			}
			Catalog->SetNamedCategoryMesh(Category, Option->DisplayName, Option->Mesh.ToSoftObjectPath());
			if (FFurnitureNamedCategory* Entry = Catalog->FindNamedCategory(Category))
			{
				Entry->Mesh.YawOffsetDegrees = Option->YawOffsetDegrees;
			}
			return;
		}
	}

	class SPlacementOrderRow : public SMultiColumnTableRow<TSharedPtr<FImportedFurnitureItem>>
	{
	public:
		SLATE_BEGIN_ARGS(SPlacementOrderRow) {}
			SLATE_ARGUMENT(TSharedPtr<FImportedFurnitureItem>, Item)
			SLATE_ARGUMENT(int32, Step)
			SLATE_ARGUMENT(FString, MeshLabel)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Item = InArgs._Item;
			Step = InArgs._Step;
			MeshLabel = InArgs._MeshLabel;
			FSuperRowType::Construct(FSuperRowType::FTableRowArgs(), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			FString Text;
			if (Column == FName(TEXT("Step")))
			{
				Text = FString::FromInt(Step);
			}
			else if (Column == FName(TEXT("Object")) && Item.IsValid())
			{
				Text = Item->Object.Label.IsEmpty() ? Item->Object.Category : Item->Object.Label;
				const FString Id = Item->Object.Id.ToString();
				if (!Id.IsEmpty() && !Id.Equals(Text, ESearchCase::IgnoreCase))
				{
					Text = FString::Printf(TEXT("%s  (%s)"), *Text, *Id);
				}
				if (!Item->Object.Category.IsEmpty() && !Item->Object.Category.Equals(Item->Object.Label, ESearchCase::IgnoreCase))
				{
					Text += FString::Printf(TEXT("  ·  %s"), *Item->Object.Category);
				}
			}
			else if (Column == FName(TEXT("Mesh")))
			{
				Text = MeshLabel;
			}
			else if (Column == FName(TEXT("Location")) && Item.IsValid())
			{
				Text = FormatVector(Item->Object.Location);
			}
			else if (Column == FName(TEXT("Rotation")) && Item.IsValid())
			{
				Text = FormatRotator(Item->Object.Rotation);
			}
			else if (Column == FName(TEXT("Scale")) && Item.IsValid())
			{
				Text = FormatVector(Item->Object.Scale);
			}

			return SNew(SBox)
				.Padding(FMargin(6.f, 2.f))
				[
					SNew(STextBlock).Text(FText::FromString(Text))
				];
		}

	private:
		TSharedPtr<FImportedFurnitureItem> Item;
		int32 Step = 0;
		FString MeshLabel;
	};
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
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Furniture placement")))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Text(this, &SFloorplanFurnitureWindow::GetAssemblyPathText)
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.Justification(ETextJustify::Right)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(TEXT("Import a JSON file. Object lists go straight to placement. A floorplan of rooms and halls is solved by Compute Assembly, then placed with the same category meshes. Assign one premodel mesh to each category. Order of placement lists every mesh in the order it is spawned.")))
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
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SSegmentedControl<int32>)
				.Value(this, &SFloorplanFurnitureWindow::GetActiveTab)
				.OnValueChanged(this, &SFloorplanFurnitureWindow::SetActiveTab)
				+ SSegmentedControl<int32>::Slot(0)
				.Text(FText::FromString(TEXT("Objects")))
				+ SSegmentedControl<int32>::Slot(1)
				.Text(FText::FromString(TEXT("Order of placement")))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex(this, &SFloorplanFurnitureWindow::GetActiveTab)
				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Objects")))
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
				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(this, &SFloorplanFurnitureWindow::GetPlacementOrderSummary)
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(OrderListView, SListView<TSharedPtr<FImportedFurnitureItem>>)
						.ListItemsSource(&PlacementOrder)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SFloorplanFurnitureWindow::OnGenerateOrderRow)
						.HeaderRow(
							SNew(SHeaderRow)
							+ SHeaderRow::Column(TEXT("Step")).DefaultLabel(FText::FromString(TEXT("#"))).FixedWidth(44.f)
							+ SHeaderRow::Column(TEXT("Object")).DefaultLabel(FText::FromString(TEXT("Object"))).FillWidth(1.5f)
							+ SHeaderRow::Column(TEXT("Mesh")).DefaultLabel(FText::FromString(TEXT("Mesh"))).FillWidth(1.2f)
							+ SHeaderRow::Column(TEXT("Location")).DefaultLabel(FText::FromString(TEXT("Location"))).FillWidth(1.3f)
							+ SHeaderRow::Column(TEXT("Rotation")).DefaultLabel(FText::FromString(TEXT("Rotation"))).FillWidth(1.1f)
							+ SHeaderRow::Column(TEXT("Scale")).DefaultLabel(FText::FromString(TEXT("Scale"))).FillWidth(1.f)
						)
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

	SetStatus(TEXT("Import a JSON file of detected objects, or a floorplan of rooms and halls."));
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

TSharedRef<ITableRow> SFloorplanFurnitureWindow::OnGenerateOrderRow(TSharedPtr<FImportedFurnitureItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 Index = PlacementOrder.IndexOfByKey(Item);
	const int32 Step = Index == INDEX_NONE ? 0 : Index + 1;
	const FString Mesh = Item.IsValid() ? CategoryMeshLabel(Item->Object.Category) : FString();
	return SNew(SPlacementOrderRow, OwnerTable)
		.Item(Item)
		.Step(Step)
		.MeshLabel(Mesh);
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
		TEXT("Import Furniture JSON"),
		DefaultPath,
		TEXT(""),
		TEXT("Furniture JSON|*.json"),
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
	const TArray<TSharedPtr<FImportedFurnitureItem>>& SpawnOrder = PlacementOrder.Num() > 0 ? PlacementOrder : Objects;
	for (const TSharedPtr<FImportedFurnitureItem>& Item : SpawnOrder)
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
		AssemblyPath = EAssemblyPath::None;
		Objects.Reset();
		PlacementOrder.Reset();
		Categories.Reset();
		JsonPath.Reset();
		RefreshObjects();
		RebuildCategories();
		SetStatus(FString::Printf(TEXT("Could not read %s."), *Path));
		return;
	}

	FFurnitureJsonImportResult Imported = ImportFurnitureDetectionJson(JsonText);
	bool bComputePath = false;
	if (!Imported.bIsDetectionData)
	{
		FString FloorplanText = JsonText;
		const FFloorplanRoomSizeResult Sized = PrepareFloorplanRoomSizes(JsonText);
		if (Sized.bIsFloorplan && !Sized.JsonText.IsEmpty())
		{
			FloorplanText = Sized.JsonText;
			FFileHelper::SaveStringToFile(FloorplanText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		const FFurnitureComputeResult Computed = ComputeFurnitureAssembly(FloorplanText);
		if (Computed.bIsFloorplan)
		{
			bComputePath = true;
			if (!Computed.ExportedJson.IsEmpty())
			{
				const FString ExportPath = FPaths::Combine(FPaths::GetPath(Path), FPaths::GetBaseFilename(Path) + TEXT(".computed.json"));
				const bool bSaved = FFileHelper::SaveStringToFile(Computed.ExportedJson, *ExportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				Imported = ImportFurnitureDetectionJson(Computed.ExportedJson);
				if (Imported.bIsDetectionData)
				{
					const FString SizePrefix = Sized.bIsFloorplan && !Sized.Message.IsEmpty() ? Sized.Message + TEXT(" ") : FString();
					Imported.Message = bSaved
						? FString::Printf(TEXT("%sConverted to object detection. %s Saved %s."), *SizePrefix, *Computed.Message, *FPaths::GetCleanFilename(ExportPath))
						: FString::Printf(TEXT("%sConverted to object detection. %s Could not write %s."), *SizePrefix, *Computed.Message, *FPaths::GetCleanFilename(ExportPath));
				}
				else
				{
					Imported.Message = FString::Printf(TEXT("Compute Assembly built a placement list, but it could not be read back. %s"), *Imported.Message);
				}
			}
			else
			{
				Imported.Message = FString::Printf(TEXT("Compute Assembly. %s"), *Computed.Message);
			}
		}
	}

	Objects.Reset();
	PlacementOrder.Reset();
	Categories.Reset();
	JsonPath = Path;
	bJsonValid = Imported.bIsDetectionData;
	AssemblyPath = !bJsonValid ? (bComputePath ? EAssemblyPath::Compute : EAssemblyPath::None) : (bComputePath ? EAssemblyPath::Compute : EAssemblyPath::Place);

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
		if (Catalog.IsValid())
		{
			if (const FFurnitureNamedCategory* Existing = Catalog->FindNamedCategory(Item->Object.Category))
			{
				Item->Object.Category = Existing->Name;
			}
			else
			{
				Catalog->EnsureNamedCategory(Item->Object.Category);
				AdoptAliasMesh(Catalog.Get(), Item->Object.Category);
			}
		}
		Objects.Add(Item);

		bool bFound = false;
		for (const FString& Category : Categories)
		{
			if (Category.Equals(Item->Object.Category, ESearchCase::IgnoreCase))
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			Categories.Add(Item->Object.Category);
		}
	}

	PlacementOrder = Objects;
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
	if (OrderListView.IsValid())
	{
		OrderListView->RequestListRefresh();
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

FText SFloorplanFurnitureWindow::GetPlacementOrderSummary() const
{
	if (PlacementOrder.Num() == 0)
	{
		return FText::FromString(TEXT("Import JSON to see the order each mesh is placed."));
	}
	return FText::FromString(FString::Printf(
		TEXT("%d meshes, in the order Place Objects spawns them. The number is the placement step."),
		PlacementOrder.Num()));
}

int32 SFloorplanFurnitureWindow::GetActiveTab() const
{
	return ActiveTab;
}

void SFloorplanFurnitureWindow::SetActiveTab(int32 Tab)
{
	ActiveTab = Tab;
}

FText SFloorplanFurnitureWindow::GetAssemblyPathText() const
{
	switch (AssemblyPath)
	{
	case EAssemblyPath::Place:
		return FText::FromString(TEXT("Place Assembly"));
	case EAssemblyPath::Compute:
		return FText::FromString(TEXT("Compute Assembly"));
	default:
		return FText::GetEmpty();
	}
}

bool SFloorplanFurnitureWindow::CanPlace() const
{
	return bJsonValid && Objects.Num() > 0;
}
