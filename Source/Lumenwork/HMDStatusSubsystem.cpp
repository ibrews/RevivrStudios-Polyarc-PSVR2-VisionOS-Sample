// Fill out your copyright notice in the Description page of Project Settings.


#include "HMDStatusSubsystem.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Logging/LogMacros.h"
#include "Misc/CoreDelegates.h"

#if PLATFORM_ANDROID
#include "IAndroidXRModule.h"
#include "AndroidXRTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMyProjectHMD, Log, All);

namespace
{
	const TCHAR* GHMDRenderTargetCVarName = TEXT("xr.SecondaryScreenPercentage.HMDRenderTarget");
}

void UHMDStatusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RequestAndroidXRSustainedPerformance();

	// Device profile cvars are pushed during early engine init, well before any GameInstance
	// exists, so the value here is already the real Config/DefaultDeviceProfiles.ini setting.
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(GHMDRenderTargetCVarName))
	{
		DeviceProfileHMDRenderTarget = CVar->GetFloat();
	}
	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UHMDStatusSubsystem::OnWorldPostInit);

	FCoreDelegates::VRHeadsetReconnected.AddUObject(this, &UHMDStatusSubsystem::HmdReconnected);

	// Belt-and-braces: hook every lifecycle signal that visionOS might fire
	// when the user presses the digital crown to exit immersive. We don't
	// know which one fires on this build of UE/OXRVisionOS, so hook all
	// three and log which one wins so we can drop the others later.
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &UHMDStatusSubsystem::OnAppDeactivated);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UHMDStatusSubsystem::OnAppBackgrounded);
	FCoreDelegates::GetApplicationWillTerminateDelegate().AddUObject(this, &UHMDStatusSubsystem::OnAppWillTerminate);
}

void UHMDStatusSubsystem::HmdReconnected()
{
	// Re-enable stereo rendering
	UHeadMountedDisplayFunctionLibrary::EnableHMD(true);
}

void UHMDStatusSubsystem::OnAppDeactivated()
{
	// Crown press fires WillResignActive on iOS-derived OSes BEFORE
	// EnterBackground. Catch the earlier event so we exit before the
	// immersive layer fully tears down.
	UE_LOG(LogMyProjectHMD, Warning, TEXT("[Purgatory-Fix] OnAppDeactivated — calling RequestExit(force=true)"));
	FPlatformMisc::RequestExit(/*Force=*/true);
}

void UHMDStatusSubsystem::OnAppBackgrounded()
{
	UE_LOG(LogMyProjectHMD, Warning, TEXT("[Purgatory-Fix] OnAppBackgrounded — calling RequestExit(force=true)"));
	FPlatformMisc::RequestExit(/*Force=*/true);
}

void UHMDStatusSubsystem::OnAppWillTerminate()
{
	UE_LOG(LogMyProjectHMD, Warning, TEXT("[Purgatory-Fix] OnAppWillTerminate — calling RequestExit(force=true)"));
	FPlatformMisc::RequestExit(/*Force=*/true);
}

void UHMDStatusSubsystem::OnWorldPostInit(UWorld* World, const UWorld::InitializationValues IVS)
{
	// Only game worlds run BP_XRPawn's BeginPlay (editor preview / inactive worlds don't matter
	// here); World->OnWorldBeginPlay broadcasts from UWorld::BeginPlay() AFTER
	// GameMode->StartPlay() has already dispatched BeginPlay to every actor, so this is
	// guaranteed to fire after the pawn's ExecuteConsoleCommand, not before it.
	if (World && World->IsGameWorld())
	{
		World->OnWorldBeginPlay.AddUObject(this, &UHMDStatusSubsystem::ReapplyDeviceProfileHMDRenderTarget);
	}
}

void UHMDStatusSubsystem::ReapplyDeviceProfileHMDRenderTarget()
{
	if (DeviceProfileHMDRenderTarget <= 0.f)
	{
		return;
	}

	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(GHMDRenderTargetCVarName))
	{
		const float StompedValue = CVar->GetFloat();
		CVar->Set(DeviceProfileHMDRenderTarget, ECVF_SetByConsole);
		UE_LOG(LogMyProjectHMD, Log,
			TEXT("[HMDRenderTarget-Fix] BP_XRPawn's BeginPlay left HMDRenderTarget=%.1f (ECVF_SetByConsole); re-applied device-profile value %.1f at the same priority"),
			StompedValue, DeviceProfileHMDRenderTarget);
	}
}

void UHMDStatusSubsystem::RequestAndroidXRSustainedPerformance()
{
#if PLATFORM_ANDROID
	if (IAndroidXRModule::IsAvailable())
	{
		IAndroidXRModule& XR = IAndroidXRModule::Get();
		const bool bCpu = XR.PerfSettingsSetPerformanceLevel(
			EAndroidXRPerfSettingsDomain::CPU, EAndroidXRPerfSettingsLevel::SustainedHigh);
		const bool bGpu = XR.PerfSettingsSetPerformanceLevel(
			EAndroidXRPerfSettingsDomain::GPU, EAndroidXRPerfSettingsLevel::SustainedHigh);
		UE_LOG(LogMyProjectHMD, Log, TEXT("[AndroidXR-Perf] SustainedHigh request cpu=%s gpu=%s"),
			bCpu ? TEXT("ok") : TEXT("REFUSED"), bGpu ? TEXT("ok") : TEXT("REFUSED"));
	}
	else
	{
		UE_LOG(LogMyProjectHMD, Warning, TEXT("[AndroidXR-Perf] AndroidXR module not available — device will stay at POWER_SAVING"));
	}
#endif
}
