// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Assembly : ModuleRules
{
	public Assembly(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Assembly",
			"Assembly/Variant_Platforming",
			"Assembly/Variant_Platforming/Animation",
			"Assembly/Variant_Combat",
			"Assembly/Variant_Combat/AI",
			"Assembly/Variant_Combat/Animation",
			"Assembly/Variant_Combat/Gameplay",
			"Assembly/Variant_Combat/Interfaces",
			"Assembly/Variant_Combat/UI",
			"Assembly/Variant_SideScrolling",
			"Assembly/Variant_SideScrolling/AI",
			"Assembly/Variant_SideScrolling/Gameplay",
			"Assembly/Variant_SideScrolling/Interfaces",
			"Assembly/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
