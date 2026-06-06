// GamepadInputSetup.h
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GamepadInputSetup.generated.h"

class UInputMappingContext;
class APawn;

UCLASS()
class MY_PROJECT_API UGamepadInputSetup : public UGameInstanceSubsystem {
  GENERATED_BODY()

public:
  virtual void Initialize(FSubsystemCollectionBase &Collection) override;
  virtual void Deinitialize() override;

  // Drive grab/release from a hand-tracking pinch (hold-to-grab: grab on press,
  // drop on release). Called by UHandTrackingComponent on index-thumb pinch
  // start/end. Reuses the same TryGrab/ReleaseGrab path as the gamepad grip.
  void HandlePinchGrab(bool bRightHand, bool bPressed);

  // Drive the gun trigger from a hand gesture (middle-finger curl), called by
  // UHandTrackingComponent. ORs into the same R2/L2 fire path (EnableInput + InjectInputForAction
  // IA_Shoot), so a curl fires the hand-grabbed pistol exactly like the gamepad trigger.
  void SetHandTrigger(bool bRightHand, bool bPressed);

private:
  UPROPERTY()
  UInputMappingContext *GamepadIMC;

  // Held actors — C++ managed, not Blueprint
  UPROPERTY() AActor *HeldActorRight;
  UPROPERTY() AActor *HeldActorLeft;

  // Hard refs to assets shared by both travel levels, kept resident across
  // OpenLevel so textures/materials don't unload+reload (no pop-in on travel).
  UPROPERTY()
  TArray<TObjectPtr<UObject>> KeepAliveAssets;

  bool bSetupDone;
  bool bSnapTurnReady;
  bool bR1WasPressed;
  bool bL1WasPressed;

  // Per-hand fire state for direct ProcessEvent fire (no InjectInputForAction)
  bool bR2DirectFired;
  bool bL2DirectFired;

  // Hand-gesture trigger (middle-curl) per hand — ORed into bR2Active/bL2Active in the fire path.
  bool bHandTriggerRight = false;
  bool bHandTriggerLeft  = false;

  // Per-hand pinch-grab edge state (hold-to-grab via HandlePinchGrab).
  bool bPinchHeldRight;
  bool bPinchHeldLeft;
  bool bLoggedFuncsR;  // true once we've logged all functions on right-held actor
  bool bLoggedFuncsL;  // true once we've logged all functions on left-held actor

  void SetupGamepadMappings();
  void TryGrab(bool bRightHand, APlayerController *PC, APawn *Pawn);
  void ReleaseGrab(bool bRightHand, APawn *Pawn);
  static bool IsGrabbableActor(AActor *Actor);
  void PreloadPersistentAssets();

  bool Tick(float DeltaTime);
  FTSTicker::FDelegateHandle TickHandle;
};
