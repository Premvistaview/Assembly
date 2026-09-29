#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SWindow;

class FAssemblyEditorModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

	void RegisterMenus();
	void OpenFurnitureWindow();

	TWeakPtr<SWindow> FurnitureWindow;
};
