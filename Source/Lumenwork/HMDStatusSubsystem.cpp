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

// 2026-09-05: opt-in, empty by default (no behavior change). See the header comment on
// ReapplyProfilingOverrideCVars() for why this exists -- DeviceProfiles.ini wins against any
// external cvar override for anything it manages, and this is the only way found so far to make
// a specific cvar survive that push for a one-off profiling capture.
static TAutoConsoleVariable<FString> CVarProfilingReapplyCVars(
	TEXT("r.VisionOS.Profiling.ReapplyCVars"),
	TEXT(""),
	TEXT("Semicolon-separated name=value list of cvars to force back to these values, at\n")
	TEXT("ECVF_SetByConsole priority, right after UWorld::BeginPlay (i.e. after DeviceProfile\n")
	TEXT("push has already happened). Empty = no-op. Example:\n")
	TEXT("-ini:Engine:[ConsoleVariables]:r.VisionOS.Profiling.ReapplyCVars=\"r.Lumen.ScreenProbeGather.DownsampleFactor=2\""),
	ECVF_Default);

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
		World->OnWorldBeginPlay.AddUObject(this, &UHMDStatusSubsystem::ReapplyProfilingOverrideCVars);
	}
}

void UHMDStatusSubsystem::ReapplyProfilingOverrideCVars()
{
	static const TCHAR* CVarName = TEXT("r.VisionOS.Profiling.ReapplyCVars");
	FString Pairs;
	if (IConsoleVariable* ListCVar = IConsoleManager::Get().FindConsoleVariable(CVarName))
	{
		Pairs = ListCVar->GetString();
	}
	if (Pairs.IsEmpty())
	{
		return;
	}

	TArray<FString> Entries;
	Pairs.ParseIntoArray(Entries, TEXT(";"), /*InCullEmpty=*/true);
	for (const FString& Entry : Entries)
	{
		FString Name, Value;
		if (!Entry.Split(TEXT("="), &Name, &Value))
		{
			UE_LOG(LogMyProjectHMD, Warning, TEXT("[ProfilingCVarReapply] malformed entry (expected name=value): %s"), *Entry);
			continue;
		}
		Name.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			CVar->Set(*Value, ECVF_SetByConsole);
			UE_LOG(LogMyProjectHMD, Log, TEXT("[ProfilingCVarReapply] forced %s=%s (ECVF_SetByConsole) after DeviceProfile push"), *Name, *Value);
		}
		else
		{
			UE_LOG(LogMyProjectHMD, Warning, TEXT("[ProfilingCVarReapply] cvar not found: %s"), *Name);
		}
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
