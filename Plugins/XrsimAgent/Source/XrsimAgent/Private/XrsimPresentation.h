#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HeadMountedDisplayTypes.h"
#include "XrsimHandMeshes.h"

class AActor;
class UWorld;
class UTexture;
class USceneCaptureComponent2D;

// Transient presentation only. Never changes the pawn, target, or saved level.
class FXrsimPresentation
{
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<USceneCaptureComponent2D> Camera;
    TWeakObjectPtr<UTexture> PreviousTexture;
    ESpectatorScreenMode PreviousMode = ESpectatorScreenMode::SingleEyeCroppedToFill;
    bool ChangedSpectator = false;
    TSharedPtr<FJsonObject> Settings;
    FXrsimHandMeshes HandMeshes;

  public:
    bool Configure(UWorld *World, const TSharedPtr<FJsonObject> &Config, FString &Error);
    void Reset();
    void Tick(UWorld *World);
    void UpdateHands(IHandTracker *Tracker);
    TSharedPtr<FJsonObject> State() const;
};
