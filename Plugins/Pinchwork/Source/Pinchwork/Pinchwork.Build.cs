// Copyright (c) 2026 Alex Coulombe. MIT License.

using UnrealBuildTool;

public class Pinchwork : ModuleRules
{
	public Pinchwork(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		// XRBase: IHandTracker modular feature + HMD keypoint types.
		// EnhancedInput: gesture -> input-action injection.
		// HeadMountedDisplay: EHandKeypoint / HeadMountedDisplayTypes.
		PrivateDependencyModuleNames.AddRange(new string[] { "XRBase", "EnhancedInput", "HeadMountedDisplay" });
	}
}
