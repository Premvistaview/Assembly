#include "AssemblyEditor.h"

#include "FloorplanFurnitureWindow.h"

#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "AssemblyEditor"

void FAssemblyEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssemblyEditorModule::RegisterMenus));
}

void FAssemblyEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (TSharedPtr<SWindow> Window = FurnitureWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

void FAssemblyEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	FToolMenuSection& Section = Menu->AddSection("Floorplan", LOCTEXT("FloorplanSection", "Floorplan"));
	Section.AddMenuEntry(
		"OpenFloorplanFurniture",
		LOCTEXT("OpenFloorplanFurniture", "Furniture Placement"),
		LOCTEXT("OpenFloorplanFurnitureTip", "Import detection JSON, assign one premodel mesh per category, and place the objects."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FAssemblyEditorModule::OpenFurnitureWindow)));
}

void FAssemblyEditorModule::OpenFurnitureWindow()
{
	if (TSharedPtr<SWindow> Existing = FurnitureWindow.Pin())
	{
		Existing->BringToFront(true);
		return;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("FurnitureWindowTitle", "Furniture Placement"))
		.ClientSize(FVector2D(1180.f, 820.f))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			SNew(SFloorplanFurnitureWindow)
		];

	FurnitureWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssemblyEditorModule, AssemblyEditor);
