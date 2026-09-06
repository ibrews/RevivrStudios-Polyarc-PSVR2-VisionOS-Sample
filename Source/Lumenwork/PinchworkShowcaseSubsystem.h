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

UCLASS(BlueprintType)
class LUMENWORK_API UPinchworkShowcaseSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// Alternate entry points into the SAME mode-cycling logic the pinky/ring pinch normally drives,
	// for a pawn (e.g. VRPawn_HandWorldControl) that has no UHandTrackingComponent -- TryWire() can
	// never succeed there (confirmed via [WIREDIAG] on-device, 2026-09-05: hand_components=0), since
	// that pawn does its own hand interaction via HandBonesTouchCheck/EHandKeypoint instead. Call
	// these from that Blueprint's own rising-edge gesture check ("Get World Subsystem" node ->
	// Pinchwork Showcase Subsystem -> Showcase On Pinky/Ring Pinch). Debounced internally with the
	// same 0.5s window as the native pinch path, so it's safe to call every tick while touching.
	UFUNCTION(BlueprintCallable, Category = "Pinchwork Showcase")
	void Showcase_OnPinkyPinch();

	UFUNCTION(BlueprintCallable, Category = "Pinchwork Showcase")
	void Showcase_OnRingPinch();

private:
	// Find the player pawn's two hand components + spawn the demo cube. Retried
	// each tick until the VR pawn exists (it spawns after world begin play).
	bool TryWire();
	bool bPawnRecoveryAttempted = false;

	void UpdateTwoHand(float DeltaTime);
	void UpdateMacro();
	void DrawHud();
	// Hand-independent HUD lines (mode/cycler status) -- split out 2026-09-04 so they're visible
	// even when hand tracking hasn't wired yet (see the Tick() comment for why this mattered).
	void DrawStatusHud();

	// --- Alpha-mode cycler (mixed-immersion debugging) ---
	// A ring-thumb pinch on either hand steps through the candidate alpha-inversion configurations
	// below and applies them as runtime cvars. visionOS mixed immersion composites UE's alpha against
	// passthrough, and getting that wrong has produced two different failures on device already (whole
	// frame semi-transparent; then opaque geometry entirely invisible). Each rebuild-and-look cycle is
	// ~4 minutes, so cycling live and reporting what each mode looks like is far faster than guessing.
	// Same idiom the component already uses to step r.Gun.OrientIndex on-device.
	void UpdateAlphaModeCycler();
	void ApplyAlphaMode(int32 Mode);

	int32 AlphaMode = 1;
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

	// --- Numeric alpha/premultiplication readback (D1) ---
	// Drives FVisionProAlphaProbe::CaptureAndLog(). This exists because the probe was previously
	// compiled into the binary with ZERO call sites -- it could never emit anything, while being
	// described as the thing that settles D1. Instrumentation that is never invoked is worse than
	// no instrumentation, because it reads as coverage.
	// Off by default: r.VisionOS.AlphaProbe.Interval (seconds) or r.VisionOS.AlphaProbe.Once.
	void UpdateAlphaProbe(float DeltaTime);
	double AlphaProbeElapsed = 0.0;

	int32 QualityMode = 0;
	bool bPrevPinkyPinch = false;
	double LastQualityModeChangeTime = -1000.0;

	// --- One-shot GPU trace (DumpGPU) ---
	// 2026-09-04: Alex wants a RANKED per-pass GPU cost breakdown instead of more blind cvar A/B.
	// Fires ONE `DumpGPU` capture r.VisionOS.GPUTrace.Delay seconds after startup (0 = disabled).
	void UpdateGPUTrace(float DeltaTime);
	double GPUTraceElapsed = 0.0;
	bool bGPUTraceFired = false;

	// --- Runtime Lumen on/off override (r.VisionOS.LumenOverride: -1=leave as shipped [default],
	// 0=force off, 1=force on) ---
	// A post-process volume's GI/Reflection method override is a per-frame BLENDED CONTENT value
	// (FPostProcessSettings::DynamicGlobalIlluminationMethod), read fresh every frame from
	// View.FinalPostProcessSettings -- NOT gated by the r.DynamicGlobalIlluminationMethod console
	// variable's ECVF_ReadOnly flag (verified against DeferredShadingRenderer.cpp/
	// IndirectLightRendering.cpp before adding this). So unlike the console-variable path, this CAN
	// toggle Lumen off at runtime in one binary -- spawns one unbound, high-priority
	// APostProcessVolume on first use and flips its override each time the cvar changes.
	void UpdateLumenOverride();
	void ApplyLumenState(int32 Wanted); // -1=shipped default, 0=off, 1=on -- shared with ApplyQualityMode
	TWeakObjectPtr<class APostProcessVolume> LumenOverrideVolume;
	int32 LastAppliedLumenOverride = -2; // sentinel distinct from the cvar's own -1/0/1 range

	// Plays once per mode change (pinky-pinch step or auto-cycle step) so a wearer/tester notices
	// the change even if they're not staring at the HUD text. 2026-09-04, Alex's request for the
	// TestFlight showcase build -- "so people know to look for a change."
	void PlayModeChangeSound();
	TWeakObjectPtr<class USoundBase> ModeChangeSound;
	TWeakObjectPtr<class UAudioComponent> ActiveModeChangeSound; // so a new mode change can stop the previous cue before it naturally ends
	bool bModeChangeSoundLoadAttempted = false;

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
