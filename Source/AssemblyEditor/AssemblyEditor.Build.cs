using UnrealBuildTool;

public class AssemblyEditor : ModuleRules
{
	public AssemblyEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Assembly"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ToolMenus",
			"ContentBrowser",
			"AssetRegistry",
			"InputCore",
			"DesktopPlatform",
			"Projects",
			"ApplicationCore"
		});
	}
}
