// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HMDStatusSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class LUMENWORK_API UHMDStatusSubsystem : public UGameInstanceSubsystem
{
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	GENERATED_BODY()
	
private:
	void HmdReconnected();

	// visionOS crown-exit clean shutdown: when the OS backgrounds us (crown
	// press, app switch), force-exit so the next launch doesn't land in
	// purgatory holding the previous immersive layer. The standard CVar
	// `xr.OpenXRExitAppOnRuntimeDrivenSessionExit` does not fire on the
	// OXRVisionOS plugin because it goes Running → cleanup without the
	// intermediate STATE_EXITING that the CVar's listener watches for.
	void OnAppDeactivated();
	void OnAppBackgrounded();
	void OnAppWillTerminate();

	// Android XR starts every app throttled at POWER_SAVING and never raises it unless asked;
	// UE's OpenXR plugin has no XR_EXT_performance_settings support, so we ask directly through
	// Google's AndroidXR plugin. No-op (guarded) on every non-Android platform.
	void RequestAndroidXRSustainedPerformance();

	// BP_XRPawn's BeginPlay runs "Execute Console Command" with
	// "xr.SecondaryScreenPercentage.HMDRenderTarget 120", which sets the cvar at
	// ECVF_SetByConsole -- the HIGHEST cvar priority in the engine, above
	// ECVF_SetByDeviceProfile. That silently stomps whatever
	// Config/DefaultDeviceProfiles.ini set (VisionOS/VisionProM5/Android_Adreno_Vulkan_SM5 all
	// set HMDRenderTarget=70) back to 120 on every level start, with no error or log line
	// calling attention to it. Snapshot the device-profile value here (before any pawn has had
	// a chance to run BeginPlay) and re-apply it once, right after UWorld::BeginPlay finishes
	// dispatching every actor's BeginPlay (confirmed in engine source: GameMode->StartPlay()
	// runs before OnWorldBeginPlay broadcasts), at the same ECVF_SetByConsole priority so
	// last-write-wins in our favor.
	void OnWorldPostInit(UWorld* World, const UWorld::InitializationValues IVS);
	void ReapplyDeviceProfileHMDRenderTarget();

	float DeviceProfileHMDRenderTarget = 0.f;
};
