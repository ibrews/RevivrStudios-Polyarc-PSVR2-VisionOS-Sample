// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HMDStatusSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class MY_PROJECT_API UHMDStatusSubsystem : public UGameInstanceSubsystem
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
};
