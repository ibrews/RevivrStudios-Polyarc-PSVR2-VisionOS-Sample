// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class My_Project : ModuleRules
{
	public My_Project(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "XRBase", "EnhancedInput", "HeadMountedDisplay" });

		// RHI + RenderCore: on-screen render-config diagnostic (shader platform / forward-vs-
		// deferred / SM6 status) in PinchworkShowcaseSubsystem -- so this is visible directly on
		// device instead of needing a log pull each time. See GMaxRHIShaderPlatform (RHI) and
		// IsForwardShadingEnabled (RenderCore).
		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "RenderCore" });

		// Metal: VisionProGPUDetection.mm queries MTLDevice supportsFamily directly for runtime
		// GPU-tier detection (M2/Apple8 vs M5/Apple9+), matching the engine's own SM6 hardware gate
		// at MetalRHI.cpp:258-265. Capability-based rather than a device-model lookup table, so it
		// stays correct for hardware released after this code was written.
		if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.VisionOS
			|| Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicWeakFrameworks.Add("Metal");
		}

		// Hand-tracking gameplay was extracted into the Pinchwork plugin (own repo,
		// sold standalone). The VRPawn adds Pinchwork's components, so the project
		// depends on the plugin module. PinchworkCore is the engine-agnostic math
		// the PinchworkShowcaseSubsystem drives directly.
		PrivateDependencyModuleNames.Add("Pinchwork");
		PrivateDependencyModuleNames.Add("PinchworkCore");

		// SpatialAccessoryTracking (PSVR2 Sense controller thumbstick input via Swift bridge) —
		// disabled 2026-08-14 (Alex: "we're not using it at all"). Its Source uses
		// FXRMotionControllerData / IXRTrackingSystem::GetMotionControllerData /
		// RebaseObjectOrientationAndPosition, all removed from IXRTrackingSystem.h in UE 5.8 -
		// this dependency alone was enough to break every VisionOS build against 5.8 regardless
		// of the plugin's own Enabled state in My_Project.uproject, since it's added here
		// unconditionally for the platform. Re-enable (and update SpatialXRTrackingWrapper.h for
		// the new IXRTrackingSystem API) if PSVR2 Sense support is wanted again.
		// if (Target.Platform == UnrealTargetPlatform.VisionOS)
		// {
		// 	PrivateDependencyModuleNames.Add("SpatialAccessoryTracking");
		// }

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
