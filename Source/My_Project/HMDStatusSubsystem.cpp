// Fill out your copyright notice in the Description page of Project Settings.


#include "HMDStatusSubsystem.h"

#include "HAL/PlatformMisc.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Logging/LogMacros.h"
#include "Misc/CoreDelegates.h"

DEFINE_LOG_CATEGORY_STATIC(LogMyProjectHMD, Log, All);

void UHMDStatusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

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
