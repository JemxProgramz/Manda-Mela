// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MandaMela : ModuleRules
{
	public MandaMela(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate",
			"SlateCore",
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "NavigationSystem" });

		PublicIncludePaths.AddRange(new string[] {
			"MandaMela",
			"MandaMela/Variant_Platforming",
			"MandaMela/Variant_Platforming/Animation",
			"MandaMela/Variant_Combat",
			"MandaMela/Variant_Combat/AI",
			"MandaMela/Variant_Combat/Animation",
			"MandaMela/Variant_Combat/Gameplay",
			"MandaMela/Variant_Combat/Interfaces",
			"MandaMela/Variant_Combat/UI",
			"MandaMela/Variant_SideScrolling",
			"MandaMela/Variant_SideScrolling/AI",
			"MandaMela/Variant_SideScrolling/Gameplay",
			"MandaMela/Variant_SideScrolling/Interfaces",
			"MandaMela/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
