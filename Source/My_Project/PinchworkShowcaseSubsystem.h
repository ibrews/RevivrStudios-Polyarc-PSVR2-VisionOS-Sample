// Copyright (c) 2026 Alex Coulombe. MIT License.
//
// Drives Pinchwork 2.0's new features on-device with ZERO Blueprint wiring.
//
// The VRPawn already carries two UHandTrackingComponents (added via the plugin).
// Rather than edit that Blueprint to add the new 2.0 components, this world
// subsystem auto-discovers the live pawn's hand components on play and drives
// the new features directly against the unit-tested PinchworkCore:
//   - TWO-HAND TRANSFORM: pinch with both hands, then move / rotate / spread
//     them to translate / rotate / scale a demo cube.
//   - GESTURE MACRO: perform Fist -> Open Palm -> Finger Guns (either hand) to
//     fire a named "unlock" event (cube pops + on-screen banner).
//   - LIVE HUD: on-screen readout of per-hand gesture, two-hand scale, and
//     macro progress.
//
// This is project/demo glue, so it lives in the game module — the Pinchwork
// plugin stays clean (see the repo REVIEW_NEEDED.md note about keeping
// project-specific code out of the MIT plugin).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Pinchwork/PinchworkTwoHand.h"
#include "Pinchwork/PinchworkSequence.h"
#include "PinchworkShowcaseSubsystem.generated.h"

class UHandTrackingComponent;

UCLASS()
class MY_PROJECT_API UPinchworkShowcaseSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	// Find the player pawn's two hand components + spawn the demo cube. Retried
	// each tick until the VR pawn exists (it spawns after world begin play).
	bool TryWire();

	void UpdateTwoHand(float DeltaTime);
	void UpdateMacro();
	void DrawHud();

	// --- Alpha-mode cycler (mixed-immersion debugging) ---
	// A ring-thumb pinch on either hand steps through the candidate alpha-inversion configurations
	// below and applies them as runtime cvars. visionOS mixed immersion composites UE's alpha against
	// passthrough, and getting that wrong has produced two different failures on device already (whole
	// frame semi-transparent; then opaque geometry entirely invisible). Each rebuild-and-look cycle is
	// ~4 minutes, so cycling live and reporting what each mode looks like is far faster than guessing.
	// Same idiom the component already uses to step r.Gun.OrientIndex on-device.
	void UpdateAlphaModeCycler();
	void ApplyAlphaMode(int32 Mode);

	int32 AlphaMode = 0;
	bool bPrevRingPinch = false;
	double LastAlphaModeChangeTime = -1000.0;

	// Per-hand committed pinch point (thumb-index midpoint) in world space, and
	// whether that hand is currently pinching. Returns false if untracked.
	bool GetHandPinch(const UHandTrackingComponent* Hand, FVector& OutPoint) const;

	void OnMacroCompleted(const FString& Name);
	void SpawnBurst(const FVector& Center);

	TWeakObjectPtr<UHandTrackingComponent> LeftHand;
	TWeakObjectPtr<UHandTrackingComponent> RightHand;
	TWeakObjectPtr<AActor> TargetCube;
	bool bWired = false;

	// Two-hand manipulation state (PinchworkCore — the same math the harness tests).
	Pinchwork::FTwoHandManipulator Manipulator;
	Pinchwork::FObjectTransform GrabBasis;
	bool bTwoHandActive = false;
	float LastScale = 1.0f;

	// Gesture-macro state.
	Pinchwork::FGestureSequenceRecognizer Sequences;
	int32 MacroId = -1;
	uint8 LastFedGesture[2] = { 0, 0 }; // [0]=left [1]=right; raw EHandGesture value
	FString LastMacroFired;
	double LastMacroFiredTime = -1000.0;
};
