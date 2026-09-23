// Copyright Alex Coulombe Presents. All Rights Reserved.

using UnrealBuildTool;

public class BlueprintPinTools : ModuleRules
{
	public BlueprintPinTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",      // UEdGraph, UEdGraphNode, UEdGraphSchema::TrySetDefaultObject
			"UnrealEd",    // FBlueprintEditorUtils, FKismetEditorUtilities, FEditorFileUtils
			"Kismet",      // blueprint editor utilities
			"BlueprintGraph",
			"Slate",
			"SlateCore",
		});
	}
}
