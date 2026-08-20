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
#include "VisionProAutoCycler.h"   // FVisionProAutoCycler is a by-value member below
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

	// --- Quality-mode cycler (D3 TestFlight A/B: shadow quality / Nanite / AA method) ---
	// Pinky-pinch on either hand steps through render-quality presets, distinct from the ring-pinch
	// alpha cycler above so both can be used independently by a remote M2/M5 tester. Every cvar here
	// was verified NOT ECVF_ReadOnly before being added (see KB
	// visionos-ue58-runtime-cyclable-vs-cooktime-cvars.md) -- SM6/Lumen are ECVF_ReadOnly and
	// cannot be cycled this way; they need a separate build.
	void UpdateQualityModeCycler();
	void ApplyQualityMode(int32 Mode);

	// --- Timer-driven auto-cycler (no gesture required) ---
	// The pinch cyclers above need a wearer who knows the gesture and reports what they saw. This
	// one steps the SAME quality modes on a timer and writes measured per-mode statistics to the log
	// ([AUTOCYCLE] blocks: frame ms avg/min/max/p95, fps, game/render/GPU ms, draw calls, primitives,
	// thermal state). Off by default; enable with r.VisionOS.AutoCycle 1. Ticked before the hand
	// wiring so it runs with hands down or out of view.
	void UpdateAutoCycler(float DeltaTime);

	FVisionProAutoCycler AutoCycler;
	bool bAutoCyclerInitialized = false;

	int32 QualityMode = 0;
	bool bPrevPinkyPinch = false;
	double LastQualityModeChangeTime = -1000.0;

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
