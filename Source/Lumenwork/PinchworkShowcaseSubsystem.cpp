// Copyright (c) 2026 Alex Coulombe. MIT License.

#include "PinchworkShowcaseSubsystem.h"

#include "HandTrackingComponent.h"   // UHandTrackingComponent, EHandGesture, EHandKeypoint
#include "PinchworkUE.h"             // FVector/FQuat <-> PinchworkCore conversions
#include "Pinchwork/PinchworkGestures.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"   // alpha-mode cycler sets render cvars at runtime
#include "RHIShaderPlatform.h"     // GMaxRHIShaderPlatform -- render-config HUD diagnostic
#include "VisionProAutoCycler.h"   // timer-driven mode stepping with measured per-mode stats
#include "VisionProAlphaProbe.h"   // numeric alpha/premultiplication readback — D1
#include "DataDrivenShaderPlatformInfo.h"  // FDataDrivenShaderPlatformInfo::GetName
#include "RenderUtils.h"           // IsForwardShadingEnabled
#include "VisionProGPUDetection.h" // runtime M2-vs-M5 GPU tier (Apple8 vs Apple9+)
#include "Engine/PostProcessVolume.h" // runtime Lumen on/off override -- content-level, not console-variable-gated
#include "Kismet/GameplayStatics.h" // SpawnSound2D for the mode-change cue
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h" // handle to force-stop/cap the mode-change cue's length

DEFINE_LOG_CATEGORY_STATIC(LogPinchworkShowcase, Log, All);

// 2026-09-05: VRPawn_HandWorldControl's scripted hand demo (5c3716b) calls Showcase_OnPinkyPinch
// every tick its own touch-check reports true, relying on the 0.5s debounce below to turn that
// into one mode step per 0.5s -- great for an unattended showcase, but it means a build launched
// with -csvGpuStats captures the GPU cost of continuously cycling render modes, not a stable
// config, and it makes manual on-device A/B testing (e.g. comparing two AA methods by eye)
// impossible to hold still. Defaulted to 1 (disabled) 2026-09-05 after `-ExecCmds=
// "r.VisionOS.Showcase.DisableScriptedInput 1"` proved unreliable to deliver via `devicectl device
// process launch` (several quoting/multi-flag variants all failed to actually change the running
// cvar) -- baking the default in source sidesteps that delivery problem entirely. Set to 0 on the
// command line for the old unattended-showcase behavior if that's ever needed again.
static TAutoConsoleVariable<int32> CVarShowcaseDisableScriptedInput(
	TEXT("r.VisionOS.Showcase.DisableScriptedInput"),
	1,
	TEXT("1 disables Showcase_OnPinkyPinch/OnRingPinch mode-stepping (quality + alpha cyclers).\n")
	TEXT("Use during automated GPU profiling captures so a scripted hand demo's pinch doesn't\n")
	TEXT("confound the measurement with continuous mode cycling. 0 = normal showcase behavior."),
	ECVF_Default);

TStatId UPinchworkShowcaseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPinchworkShowcaseSubsystem, STATGROUP_Tickables);
}

bool UPinchworkShowcaseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPinchworkShowcaseSubsystem::Tick(float DeltaTime)
{
	// Timer-driven auto-cycler. Deliberately ticked BEFORE the wiring checks below: those return
	// early when hand tracking has not resolved, and the entire point of the auto-cycler is that the
	// wearer does nothing at all -- no pinch, no gesture, hands possibly not even in view. Gating it
	// behind hand wiring would make a headset-on-the-desk or hands-down session silently produce no
	// data, which is exactly the failure it exists to prevent.
	UpdateAutoCycler(DeltaTime);

	// Same reasoning as the auto-cycler above: the alpha readback must not be gated behind hand
	// wiring. D1 is about what the compositor receives, which has nothing to do with whether hand
	// tracking resolved.
	UpdateAlphaProbe(DeltaTime);
	UpdateGPUTrace(DeltaTime);
	UpdateLumenOverride();

	// 2026-09-04 FIX: DrawHud() used to be called only past the hand-wiring gate below, so if the
	// wearer's hands weren't in view (or TryWire() hadn't succeeded yet), NOTHING showed on screen
	// -- including the QUALITY MODE line, which Alex needed to read during an auto-cycle run and
	// couldn't. Split into an always-visible part (mode/cycler status -- none of it needs hand
	// data) and the hand-dependent part below.
	DrawStatusHud();

	if (!bWired)
	{
		if (!TryWire()) { return; }
	}
	// Hands can go stale across a level load — re-wire if so.
	if (!LeftHand.IsValid() || !RightHand.IsValid())
	{
		bWired = false;
		return;
	}

	UpdateTwoHand(DeltaTime);
	UpdateMacro();
	UpdateAlphaModeCycler();
	UpdateQualityModeCycler();
	DrawHud();
}

// Candidate alpha-inversion configurations, stepped by a ring-thumb pinch.
// visionOS mixed immersion needs UE's alpha in the "1 = opaque" convention that CompositorServices
// expects; UE renders the opposite, so something must invert it. There are two independent
// implementations of that inversion plus the option of none/both, and which is right on device has
// not been obvious from source. Rather than guess again, cycle them live and let the headset answer.
namespace
{
	struct FAlphaMode
	{
		const TCHAR* Name;
		int32 InlineInvert;   // r.Mobile.VisionOS.InlineAlphaInvert
		int32 StockPass;      // r.AlphaInvertPass
		// r.Mobile.PropagateAlpha -- NOTE (2026-09-05): confirmed ECVF_ReadOnly in
		// MobileBasePassRendering.cpp. Calling Set() on it at runtime is a silent no-op (UE logs a
		// warning, does not apply the value) -- it has never actually done anything in this cycler.
		// Harmless here only because every reachable mode (1, 3) already shares the same value (1);
		// don't add a mode that relies on this field differing between reachable entries, it won't
		// take effect. Changing PropagateAlpha for real needs a cook-time/.ini change, not a cycle.
		int32 PropagateAlpha;
		// r.BackbufferQuantizationDitheringOverride -- EXPERIMENTAL, added 2026-09-05 for Alex's
		// "stepped alpha artifacts" report. 0 = engine default (auto-detects bitdepth from the
		// backbuffer format). Verified in PostProcessTonemap.cpp: ECVF_RenderThreadSafe (genuinely
		// runtime-cyclable) -- but ALSO verified in PostProcessTonemap.usf that this dithering is
		// applied to OutColor.rgb ONLY, never .a. So this is NOT a confirmed fix for alpha-channel
		// stepping specifically -- it's here as a cheap, safe thing to try live (RGB dithering can
		// still change how banding READS perceptually in a passthrough-composited view even if it
		// never touches the alpha value itself). If cycling through modes 5/6 below doesn't visibly
		// help, the real fix is more likely the XR swapchain's own alpha bit-depth/format, which
		// lives outside this file (OpenXRVisionOS plugin / compositor swapchain setup) -- not yet
		// investigated.
		int32 DitherOverride;
	};

	// On-device verification (M5/Lumen, 2026-09-02): modes 0, 2, and 4 all render everything
	// semi-transparent -- wrong. Only modes 1 and 3 look correct. Mode 1 (Epic stock pass) is now
	// the shipping default below. All 5 original entries and their exact Name strings stay intact:
	// other tooling/logs reference them by index and name. Modes 5/6 (2026-09-05) are NEW, added to
	// the reachable cycle alongside 1/3 -- see DitherOverride comment above for what they actually
	// test and why they're not a guaranteed fix.
	static const FAlphaMode GAlphaModes[] = {
		{ TEXT("0 inline=ON  stock=off alpha=ON  (current default)"), 1, 0, 1, 0 },
		{ TEXT("1 inline=off stock=ON  alpha=ON  (Epic stock pass)"), 0, 1, 1, 0 },
		{ TEXT("2 inline=off stock=off alpha=ON  (NO inversion)"),    0, 0, 1, 0 },
		{ TEXT("3 inline=ON  stock=ON  alpha=ON  (both - expect double-invert)"), 1, 1, 1, 0 },
		{ TEXT("4 inline=off stock=off alpha=off (no passthrough blend)"), 0, 0, 0, 0 },
		{ TEXT("5 = mode 1 + dither override 8-bit (EXPERIMENTAL, RGB-only, see comment)"), 0, 1, 1, 8 },
		{ TEXT("6 = mode 1 + dither override 6-bit, stronger (EXPERIMENTAL, RGB-only)"),    0, 1, 1, 6 },
	};
	static constexpr int32 GNumAlphaModes = UE_ARRAY_COUNT(GAlphaModes);
	// Ring-pinch cycles through exactly these, in this order -- never lands on the confirmed-wrong
	// 0/2/4. Extend this array (not the struct layout) to add more reachable candidates later.
	static const int32 GReachableAlphaModes[] = { 1, 3, 5, 6 };
	static constexpr int32 GNumReachableAlphaModes = UE_ARRAY_COUNT(GReachableAlphaModes);

	// Shared by the tick-based ring-pinch path and Showcase_OnRingPinch() (Blueprint entry point)
	// so both step through the exact same reachable set in the exact same order.
	static int32 NextReachableAlphaMode(int32 Current)
	{
		for (int32 i = 0; i < GNumReachableAlphaModes; ++i)
		{
			if (GReachableAlphaModes[i] == Current)
			{
				return GReachableAlphaModes[(i + 1) % GNumReachableAlphaModes];
			}
		}
		return GReachableAlphaModes[0]; // Current wasn't a reachable mode (e.g. still at the 0 default) -- start the cycle.
	}

	static void SetCVarInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			// ECVF_SetByConsole outranks SetByProjectSetting, so this beats the .ini values.
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	struct FQualityMode
	{
		const TCHAR* Name;
		int32 ShadowQuality;  // r.ShadowQuality (0=off .. 5=max)
		int32 Nanite;         // r.Nanite (0/1)
		int32 AntiAliasing;   // r.Mobile.AntiAliasing (0=off,1=FXAA,2=TAA,3=MSAA,4=TSR,5=SMAA) -- inert on M5 SM5, kept only for the M2/ES3_1 path
		int32 AntiAliasingMethod; // r.AntiAliasingMethod, -1=leave as shipped (default). THIS is the cvar that
		                          // actually matters on M5 SM5 (0=None,1=FXAA,2=TAA,3=MSAA,4=TSR,5=SMAA per
		                          // SceneUtils.h). Confirmed ECVF_RenderThreadSafe (SceneView.cpp) -- genuinely
		                          // runtime-cyclable like the others, NOT cook-time-gated as earlier tonight's
		                          // notes assumed. TAA (2) deliberately NOT offered here: confirmed broken on
		                          // this fork tonight (blank right eye, reproduced on-device, reverted) --
		                          // that's a rendering-logic bug independent of how the cvar gets set, so a
		                          // live toggle would hit the same bug, not avoid it.
		int32 ScreenProbeDownsample; // r.Lumen.ScreenProbeGather.DownsampleFactor (default 16; higher = fewer/coarser probes = cheaper)
		int32 ScreenTraces;          // r.Lumen.ScreenProbeGather.ScreenTraces (default 1; 0 skips the screen-space trace pass)
		int32 RadianceCache;         // r.Lumen.ScreenProbeGather.RadianceCache (default 1; persistent world-space cache)
		int32 TraceMeshSDFs;         // r.Lumen.TraceMeshSDFs (default 0 on this project already)
		int32 LumenState;            // -1=leave as shipped (default), 0=force Lumen OFF, 1=force Lumen ON
	};

	// Mode 0 matches the shipping default so the app starts in its normal state.
	//
	// 2026-09-04: added 5 Lumen GPU-cost modes (7-11). The CSV profiler showed this build is
	// GPU-bound at ~49ms/frame (~20fps) with GameThreadTime only 3.7ms -- almost certainly Lumen GI
	// cost, not AA method. All four new-field cvars verified ECVF_Scalability|ECVF_RenderThreadSafe
	// (genuinely runtime-safe, no shader permutation dependency) directly against ue582-xr-main-m5
	// source before adding here -- see LumenScreenProbeGather.cpp / LumenScreenProbeTracing.cpp /
	// LumenDiffuseIndirect.cpp. Each Lumen mode changes EXACTLY ONE field vs mode 7 (the Lumen
	// baseline, itself identical to mode 0's shadow/nanite/aa) -- keeping every other knob fixed so
	// a measured/visual difference has one unambiguous cause, not several confounded together.
	// Mode 9 deliberately makes probes FINER (more expensive) as a sanity check on the measurement
	// itself -- if mode 9 doesn't come back slower than mode 7, trust the methodology less, not
	// just the knob.
	// 2026-09-04 (2nd pass, Alex's TestFlight showcase request): added mode 12, LUMEN OFF -- a
	// deliberate, dramatic before/after so testers pinky-pinching through modes see WHY this
	// project exists. LumenState=-1 on every other mode leaves Lumen exactly as shipped (no
	// override applied at all -- see ApplyLumenState's -1 branch), so none of the diagnostic modes
	// above accidentally touch it.
	// Field order: Name, ShadowQuality, Nanite, AntiAliasing(inert r.Mobile.AntiAliasing),
	// AntiAliasingMethod(real r.AntiAliasingMethod, -1=leave as shipped), ScreenProbeDownsample,
	// ScreenTraces, RadianceCache, TraceMeshSDFs, LumenState.
	static const FQualityMode GQualityModes[] = {
		{ TEXT("0 shadow=5 aa=FXAA (current default)"), 5, 1, 1, -1, 16, 1, 1, 0, -1 },
		{ TEXT("1 shadow=1 aa=FXAA (low shadow)"),      1, 1, 1, -1, 16, 1, 1, 0, -1 },
		{ TEXT("2 shadow=0 aa=FXAA (shadows OFF)"),     0, 1, 1, -1, 16, 1, 1, 0, -1 },
		{ TEXT("3 shadow=5 aa=FXAA (Nanite toggle removed from label -- confirmed inert/unsupported on METAL_SM5_IOS)"),      5, 0, 1, -1, 16, 1, 1, 0, -1 },
		{ TEXT("4 shadow=5 aa=MSAA"),                   5, 1, 3, -1, 16, 1, 1, 0, -1 },
		{ TEXT("5 shadow=5 aa=TAA (r.Mobile only, inert on M5)"), 5, 1, 2, -1, 16, 1, 1, 0, -1 },
		{ TEXT("6 shadow=3 aa=FXAA (medium shadow)"),   3, 1, 1, -1, 16, 1, 1, 0, -1 },
		{ TEXT("7 LUMEN BASELINE (everything else = mode 0's values, untouched Lumen defaults)"), 5, 1, 1, -1, 16, 1, 1, 0, -1 },
		{ TEXT("8 LUMEN probes 2x coarser (DownsampleFactor 32)"), 5, 1, 1, -1, 32, 1, 1, 0, -1 },
		{ TEXT("9 LUMEN probes 2x FINER -- control, should be WORSE not better"), 5, 1, 1, -1, 8, 1, 1, 0, -1 },
		{ TEXT("10 LUMEN screen traces OFF"),                      5, 1, 1, -1, 16, 0, 1, 0, -1 },
		{ TEXT("11 LUMEN radiance cache OFF"),                     5, 1, 1, -1, 16, 1, 0, 0, -1 },
		{ TEXT("12 LUMEN GI OFF -- see the difference!"),          5, 1, 1, -1, 16, 1, 1, 0,  0 },
		{ TEXT("13 AA METHOD: TSR (real r.AntiAliasingMethod=4)"), 5, 1, 1,  4, 16, 1, 1, 0, -1 },
		{ TEXT("14 AA METHOD: None (real r.AntiAliasingMethod=0)"), 5, 1, 1, 0, 16, 1, 1, 0, -1 },
	};
	static constexpr int32 GNumQualityModes = UE_ARRAY_COUNT(GQualityModes);
}

void UPinchworkShowcaseSubsystem::ApplyAlphaMode(int32 Mode)
{
	const FAlphaMode& M = GAlphaModes[FMath::Clamp(Mode, 0, GNumAlphaModes - 1)];
	SetCVarInt(TEXT("r.Mobile.VisionOS.InlineAlphaInvert"), M.InlineInvert);
	SetCVarInt(TEXT("r.AlphaInvertPass"), M.StockPass);
	SetCVarInt(TEXT("r.Mobile.PropagateAlpha"), M.PropagateAlpha); // no-op, ECVF_ReadOnly -- see FAlphaMode comment
	SetCVarInt(TEXT("r.BackbufferQuantizationDitheringOverride"), M.DitherOverride);
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[ALPHAMODE] -> %s"), M.Name);
	if (GLog) { GLog->Flush(); }   // forced flush: this log is the record of what was on screen
}

void UPinchworkShowcaseSubsystem::UpdateAlphaModeCycler()
{
	// Either hand. Rising edge only, with a debounce so one physical pinch steps exactly one mode.
	const bool bRing =
		(LeftHand.IsValid()  && LeftHand->bIsRingPinching) ||
		(RightHand.IsValid() && RightHand->bIsRingPinching);

	const double Now = FPlatformTime::Seconds();
	if (bRing && !bPrevRingPinch && (Now - LastAlphaModeChangeTime) > 0.5)
	{
		LastAlphaModeChangeTime = Now;
		// Cycles GReachableAlphaModes (currently 1, 3, 5, 6) -- see that array's comment. Never
		// lands on the confirmed-wrong 0/2/4.
		AlphaMode = NextReachableAlphaMode(AlphaMode);
		ApplyAlphaMode(AlphaMode);
	}
	bPrevRingPinch = bRing;
}

void UPinchworkShowcaseSubsystem::ApplyQualityMode(int32 Mode)
{
	const FQualityMode& M = GQualityModes[FMath::Clamp(Mode, 0, GNumQualityModes - 1)];
	SetCVarInt(TEXT("r.ShadowQuality"), M.ShadowQuality);
	SetCVarInt(TEXT("r.Nanite"), M.Nanite);
	// -1 resolves to the actual shipped default (1=FXAA), not "leave whatever a prior mode set" --
	// same predictable-revert convention as LumenState below, so cycling away from an AA-method
	// mode always lands back on the known-good shipped state, not a stale value.
	SetCVarInt(TEXT("r.AntiAliasingMethod"), M.AntiAliasingMethod == -1 ? 1 : M.AntiAliasingMethod);
	SetCVarInt(TEXT("r.Mobile.AntiAliasing"), M.AntiAliasing);
	SetCVarInt(TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"), M.ScreenProbeDownsample);
	SetCVarInt(TEXT("r.Lumen.ScreenProbeGather.ScreenTraces"), M.ScreenTraces);
	SetCVarInt(TEXT("r.Lumen.ScreenProbeGather.RadianceCache"), M.RadianceCache);
	SetCVarInt(TEXT("r.Lumen.TraceMeshSDFs"), M.TraceMeshSDFs);
	ApplyLumenState(M.LumenState);
	PlayModeChangeSound();
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[QUALITYMODE] -> %s"), M.Name);
	if (GLog) { GLog->Flush(); }   // forced flush: this log is the record of what was on screen
}

// Alpha probe cadence. Off by default because CaptureAndLog() does a full-frame readback with an
// RHI flush -- fine for a diagnostic build, not something to run unconditionally.
//   r.VisionOS.AlphaProbe.Interval  seconds between captures (0 = off)
//   r.VisionOS.AlphaProbe.Once      set to 1 to fire a single capture, auto-clears
static TAutoConsoleVariable<float> CVarAlphaProbeInterval(
	TEXT("r.VisionOS.AlphaProbe.Interval"),
	0.0f,
	TEXT("Seconds between [ALPHAPROBE] captures. 0 disables. Each capture stalls the GPU."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAlphaProbeOnce(
	TEXT("r.VisionOS.AlphaProbe.Once"),
	0,
	TEXT("Set to 1 to fire one [ALPHAPROBE] capture. Auto-clears back to 0."),
	ECVF_Default);

// One-shot GPU trace trigger. 2026-09-04: Alex wants a RANKED breakdown of per-pass GPU cost,
// not more blind cvar A/B -- `DumpGPU` is UE's own built-in profiler for exactly that (every
// pass/draw, ranked by ms, written to Saved/Profiling/GPU as a browsable report). Fires ONCE,
// automatically, `r.VisionOS.GPUTrace.Delay` seconds after this subsystem starts ticking (0 =
// disabled, the default) -- long enough for the scene to load and settle so the captured frame is
// representative, not a loading-screen frame. Deliberately does NOT check r.VisionOS.AutoCycle:
// if both are enabled the capture may land mid-mode-transition, which is a caller error, not
// something to guess around here.
static TAutoConsoleVariable<float> CVarGPUTraceDelay(
	TEXT("r.VisionOS.GPUTrace.Delay"),
	0.0f,
	TEXT("Seconds after startup to fire ONE DumpGPU capture. 0 disables (default)."),
	ECVF_Default);

void UPinchworkShowcaseSubsystem::UpdateGPUTrace(float DeltaTime)
{
	if (bGPUTraceFired)
	{
		return;
	}

	const float Delay = CVarGPUTraceDelay.GetValueOnGameThread();
	if (Delay <= 0.0f)
	{
		return;
	}

	GPUTraceElapsed += DeltaTime;
	if (GPUTraceElapsed < Delay)
	{
		return;
	}

	bGPUTraceFired = true;
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[GPUTRACE] firing DumpGPU at elapsed=%.1fs"), GPUTraceElapsed);
	if (GLog) { GLog->Flush(); }
	GEngine->Exec(GetWorld(), TEXT("DumpGPU"));
}

static TAutoConsoleVariable<int32> CVarLumenOverride(
	TEXT("r.VisionOS.LumenOverride"),
	-1,
	TEXT("Force Lumen GI+Reflections on/off at runtime via a spawned unbound post-process volume.\n")
	TEXT("-1 = leave as shipped (default), 0 = force OFF, 1 = force ON."),
	ECVF_Default);

void UPinchworkShowcaseSubsystem::UpdateLumenOverride()
{
	ApplyLumenState(CVarLumenOverride.GetValueOnGameThread());
}

void UPinchworkShowcaseSubsystem::PlayModeChangeSound()
{
	// Load once, lazily -- ConstructorHelpers::FObjectFinder can't be used here (this isn't a
	// constructor), so LoadObject follows the same convention TryWire() already uses for the demo
	// cube's mesh/material. Reusing StarterContent/Audio/Light01_Cue (already force-cooked into
	// this project per Config/DefaultGame.ini's DirectoriesToAlwaysCook) rather than adding a new
	// asset -- also a fitting cue thematically (a "light" sound for a Lumen toggle).
	if (!bModeChangeSoundLoadAttempted)
	{
		bModeChangeSoundLoadAttempted = true;
		ModeChangeSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/StarterContent/Audio/Light01_Cue.Light01_Cue"));
		if (!ModeChangeSound.IsValid())
		{
			UE_LOG(LogPinchworkShowcase, Warning, TEXT("[QUALITYMODE] mode-change sound failed to load -- continuing silently"));
		}
		else
		{
			// 2026-09-05 diagnostic: Alex reported no audible sound despite no load failure. Log the
			// actual duration/volume so the next device log confirms whether PlaySound2D is even
			// being reached with a sane asset, rather than guessing at audio routing blind.
			UE_LOG(LogPinchworkShowcase, Warning, TEXT("[QUALITYMODE] mode-change sound loaded: duration=%.2fs"),
				ModeChangeSound->GetDuration());
		}
	}

	if (USoundBase* Sound = ModeChangeSound.Get())
	{
		if (UWorld* World = GetWorld())
		{
			// 2026-09-05 fix: Alex correctly diagnosed this as "long or looping, so there's no
			// noticeable change" -- StarterContent/Audio's ambient cues (Light/Fire/Steam/etc.) run
			// several seconds each, per their file sizes (~300KB+ raw wave, not a short click). Fire-
			// and-forget PlaySound2D on every mode change stacks overlapping multi-second instances
			// into one continuous wash instead of a discrete per-change blip. Fix: stop any prior
			// instance first, then force-cap this one to a short blip via a timer, regardless of the
			// asset's real length -- makes ANY StarterContent cue work as a UI cue, not just a
			// coincidentally-short one.
			if (UAudioComponent* Prev = ActiveModeChangeSound.Get())
			{
				Prev->Stop();
			}
			UAudioComponent* AC = UGameplayStatics::SpawnSound2D(World, Sound);
			ActiveModeChangeSound = AC;
			if (AC)
			{
				// 0.15s + 0.05s fade = 0.20s total, well under the 0.5s pinch-debounce cadence
				// (LastQualityModeChangeTime / LastAlphaModeChangeTime). The prior 0.25s+0.08s=0.33s
				// cap was too close to that 0.5s window -- on-device evidence from tonight's scripted
				// demo shows successive mode changes landing 0.47-0.50s apart, leaving only ~170ms of
				// margin, audible as a blend under any timer jitter.
				TWeakObjectPtr<UAudioComponent> WeakAC = AC;
				FTimerHandle StopTimer;
				World->GetTimerManager().SetTimer(StopTimer, FTimerDelegate::CreateLambda([WeakAC]()
				{
					if (UAudioComponent* Comp = WeakAC.Get())
					{
						Comp->FadeOut(0.05f, 0.0f);
					}
				}), 0.15f, false);
			}
		}
	}
}

// Shared by the debug cvar path (UpdateLumenOverride, above) and the showcase quality-mode cycler
// (ApplyQualityMode, below) -- both need "force Lumen on/off/shipped-default", just triggered
// differently (a console var for dev use, a pinky-pinch mode step for testers).
void UPinchworkShowcaseSubsystem::ApplyLumenState(int32 Wanted)
{
	if (Wanted == LastAppliedLumenOverride)
	{
		return; // no change since last call -- avoid respawning/rewriting every frame
	}

	UWorld* World = GetWorld();
	if (!World) { return; }

	if (Wanted == -1)
	{
		// Returning to "shipped default" means removing our override entirely, not setting it to
		// Lumen explicitly -- the project's own DefaultEngine.ini value should win once we're not
		// forcing anything.
		if (APostProcessVolume* Vol = LumenOverrideVolume.Get())
		{
			Vol->Destroy();
			LumenOverrideVolume = nullptr;
		}
		LastAppliedLumenOverride = -1;
		UE_LOG(LogPinchworkShowcase, Warning, TEXT("[LUMENOVERRIDE] -> shipped default (override removed)"));
		if (GLog) { GLog->Flush(); }
		return;
	}

	APostProcessVolume* Vol = LumenOverrideVolume.Get();
	if (!Vol)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Vol = World->SpawnActor<APostProcessVolume>(Params);
		if (!Vol) { return; }
		Vol->bUnbound = true;   // applies globally, not just inside a bounded volume shape
		Vol->BlendWeight = 1.0f;
		Vol->Priority = 100.0f; // outrank any level-placed volume so this test is unambiguous
		LumenOverrideVolume = Vol;
	}

	const bool bLumenOn = (Wanted == 1);
	Vol->Settings.bOverride_DynamicGlobalIlluminationMethod = true;
	Vol->Settings.DynamicGlobalIlluminationMethod = bLumenOn ? EDynamicGlobalIlluminationMethod::Lumen : EDynamicGlobalIlluminationMethod::None;
	Vol->Settings.bOverride_ReflectionMethod = true;
	Vol->Settings.ReflectionMethod = bLumenOn ? EReflectionMethod::Lumen : EReflectionMethod::None;

	LastAppliedLumenOverride = Wanted;
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[LUMENOVERRIDE] -> FORCED %s (GI+Reflections)"), bLumenOn ? TEXT("ON") : TEXT("OFF"));
	if (GLog) { GLog->Flush(); }
}

void UPinchworkShowcaseSubsystem::UpdateAlphaProbe(float DeltaTime)
{
	// A one-shot request always wins, and clears itself so a stuck cvar cannot pin the GPU in a
	// readback stall every frame.
	if (CVarAlphaProbeOnce.GetValueOnGameThread() != 0)
	{
		CVarAlphaProbeOnce->Set(0, ECVF_SetByCode);
		FVisionProAlphaProbe::CaptureAndLog();
		AlphaProbeElapsed = 0.0;
		return;
	}

	const float Interval = CVarAlphaProbeInterval.GetValueOnGameThread();
	if (Interval <= 0.0f)
	{
		AlphaProbeElapsed = 0.0;
		return;
	}

	// Skip the first few seconds: the readback is only meaningful once the scene is actually
	// rendering, and a capture during startup would measure a cleared or partially-composed frame
	// and report it as if it were representative.
	AlphaProbeElapsed += DeltaTime;
	if (AlphaProbeElapsed >= Interval)
	{
		AlphaProbeElapsed = 0.0;
		FVisionProAlphaProbe::CaptureAndLog();
	}
}

void UPinchworkShowcaseSubsystem::UpdateAutoCycler(float DeltaTime)
{
	// Bind once, lazily. Drives the SAME GQualityModes table the pinch cycler uses, so the two can
	// never disagree about what "mode 3" means -- a mismatch there would silently mislabel every
	// measurement in the pulled log.
	if (!bAutoCyclerInitialized)
	{
		bAutoCyclerInitialized = true;
		AutoCycler.Initialize(TEXT("quality"), GNumQualityModes,
			FVisionProAutoCycler::FApplyModeDelegate::CreateLambda(
				[this](int32 ModeIndex, FString& OutModeName)
				{
					ApplyQualityMode(ModeIndex);
					OutModeName = GQualityModes[FMath::Clamp(ModeIndex, 0, GNumQualityModes - 1)].Name;
					// Keep the pinch cycler's index in sync so a wearer who pinches mid-run steps
					// from where the auto-cycler actually is, not from a stale index.
					QualityMode = ModeIndex;
				}));
	}

	AutoCycler.Tick(DeltaTime);
}

void UPinchworkShowcaseSubsystem::UpdateQualityModeCycler()
{
	const bool bPinky =
		(LeftHand.IsValid()  && LeftHand->bIsPinkyPinching) ||
		(RightHand.IsValid() && RightHand->bIsPinkyPinching);

	const double Now = FPlatformTime::Seconds();
	if (bPinky && !bPrevPinkyPinch && (Now - LastQualityModeChangeTime) > 0.5)
	{
		LastQualityModeChangeTime = Now;
		QualityMode = (QualityMode + 1) % GNumQualityModes;
		ApplyQualityMode(QualityMode);
	}
	bPrevPinkyPinch = bPinky;
}

void UPinchworkShowcaseSubsystem::Showcase_OnPinkyPinch()
{
	if (CVarShowcaseDisableScriptedInput.GetValueOnGameThread() != 0) { return; }

	// Same debounce window as UpdateQualityModeCycler() above -- callers are expected to invoke this
	// on every tick a Blueprint's own touch-check reports true, not just on the rising edge, so the
	// debounce here is what turns a held pinch into exactly one mode step.
	const double Now = FPlatformTime::Seconds();
	if ((Now - LastQualityModeChangeTime) <= 0.5) { return; }
	LastQualityModeChangeTime = Now;
	QualityMode = (QualityMode + 1) % GNumQualityModes;
	ApplyQualityMode(QualityMode);
	// Greppable confirmation line -- Showcase_OnPinkyPinch/OnRingPinch previously had no direct log
	// output of their own (only the downstream QUALITYMODE/ALPHAMODE effect logged), so a device-log
	// grep for "Showcase_On.*Pinch" (the on-device verification recipe for e0c110f) could never match
	// anything. This also can't distinguish a real pinch from VRPawn_HandWorldControl's scripted hand
	// demo (5c3716b), which calls this same BlueprintCallable entry point -- run with
	// -ExecCmds="r.VisionOS.Showcase.DisableScriptedInput 1" to rule the scripted demo out first.
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[GESTURE] Showcase_OnPinkyPinch fired -> QualityMode=%d"), QualityMode);
}

void UPinchworkShowcaseSubsystem::Showcase_OnRingPinch()
{
	if (CVarShowcaseDisableScriptedInput.GetValueOnGameThread() != 0) { return; }

	// Same debounce window and mode cycling as UpdateAlphaModeCycler() above.
	const double Now = FPlatformTime::Seconds();
	if ((Now - LastAlphaModeChangeTime) <= 0.5) { return; }
	LastAlphaModeChangeTime = Now;
	AlphaMode = NextReachableAlphaMode(AlphaMode);
	ApplyAlphaMode(AlphaMode);
	// See the matching comment in Showcase_OnPinkyPinch() above.
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[GESTURE] Showcase_OnRingPinch fired -> AlphaMode=%d"), AlphaMode);
}

bool UPinchworkShowcaseSubsystem::TryWire()
{
	// 2026-09-05 diagnostic (throttled to ~1/sec -- this is called every tick until it succeeds):
	// Alex reported pinky/ring pinch not working and disputed the (wrong -- see commit) theory that
	// BP_XRPawn is the active pawn. Rather than guess again, log the ACTUAL pawn class and
	// UHandTrackingComponent count so the next device log pull answers this directly.
	static double LastWireDiagLogTime = -1000.0;
	const double NowDiag = FPlatformTime::Seconds();
	const bool bShouldLogDiag = (NowDiag - LastWireDiagLogTime) > 1.0;

	UWorld* World = GetWorld();
	if (!World) { return false; }
	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		if (bShouldLogDiag)
		{
			LastWireDiagLogTime = NowDiag;
			UE_LOG(LogPinchworkShowcase, Warning, TEXT("[WIREDIAG] no pawn yet (PC=%s)"), PC ? TEXT("valid") : TEXT("null"));
			if (GLog) { GLog->Flush(); }
		}
		return false; // VR pawn spawns after world begin — retry next tick
	}

	TArray<UHandTrackingComponent*> Hands;
	Pawn->GetComponents<UHandTrackingComponent>(Hands);
	if (bShouldLogDiag)
	{
		LastWireDiagLogTime = NowDiag;
		UE_LOG(LogPinchworkShowcase, Warning, TEXT("[WIREDIAG] pawn=%s class=%s hand_components=%d"),
			*Pawn->GetName(), *Pawn->GetClass()->GetName(), Hands.Num());
		if (GLog) { GLog->Flush(); }
	}
	if (Hands.Num() < 2) { return false; }
	for (UHandTrackingComponent* H : Hands)
	{
		if (!H) { continue; }
		if (H->bIsRight) { RightHand = H; } else { LeftHand = H; }
	}
	if (!LeftHand.IsValid() || !RightHand.IsValid()) { return false; }

	// Spawn the demo cube ~60 cm in front of the player at roughly hand height.
	FVector SpawnLoc = Pawn->GetActorLocation() + FVector(60.f, 0.f, 0.f);
	if (PC && PC->PlayerCameraManager)
	{
		const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
		const FVector Fwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
		SpawnLoc = Cam + Fwd * 60.f - FVector(0.f, 0.f, 15.f);
	}

	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AStaticMeshActor* Cube = World->SpawnActor<AStaticMeshActor>(SpawnLoc, FRotator::ZeroRotator, Params))
		{
			Cube->SetMobility(EComponentMobility::Movable);
			if (UStaticMeshComponent* MC = Cube->GetStaticMeshComponent())
			{
				MC->SetStaticMesh(CubeMesh);
				MC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				MC->SetCastShadow(false);
				if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr,
					TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
				{
					MC->SetMaterial(0, Mat);
				}
			}
			Cube->SetActorScale3D(FVector(0.15f)); // engine cube is 100 cm -> ~15 cm
			TargetCube = Cube;
		}
	}

	// Register the demo macro: Fist -> Open Palm -> Finger Guns (2 s budget).
	MacroId = Sequences.AddSequence(Pinchwork::FGestureSequence(
		"unlock",
		{ Pinchwork::EGesture::Fist, Pinchwork::EGesture::OpenPalm, Pinchwork::EGesture::FingerGuns },
		2.0f));

	bWired = true;
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[PinchworkShowcase] wired: 2 hands + demo cube + 'unlock' macro."));
	return true;
}

bool UPinchworkShowcaseSubsystem::GetHandPinch(const UHandTrackingComponent* Hand, FVector& OutPoint) const
{
	if (!Hand) { return false; }
	const FVector Thumb = Hand->GetKeypointWorldTransform(EHandKeypoint::ThumbTip).GetLocation();
	const FVector Index = Hand->GetKeypointWorldTransform(EHandKeypoint::IndexTip).GetLocation();
	if (Thumb.IsNearlyZero() || Index.IsNearlyZero()) { return false; } // untracked this frame
	OutPoint = (Thumb + Index) * 0.5f;
	return true;
}

void UPinchworkShowcaseSubsystem::UpdateTwoHand(float DeltaTime)
{
	AActor* Cube = TargetCube.Get();
	if (!Cube) { return; }

	FVector L, R;
	const bool bL = LeftHand->bIsPinching && GetHandPinch(LeftHand.Get(), L);
	const bool bR = RightHand->bIsPinching && GetHandPinch(RightHand.Get(), R);
	const bool bBoth = bL && bR;

	if (bBoth && !bTwoHandActive)
	{
		Manipulator.Begin(PinchworkUE::ToCore(L), PinchworkUE::ToCore(R));
		const FTransform X = Cube->GetActorTransform();
		GrabBasis.Position = PinchworkUE::ToCore(X.GetLocation());
		GrabBasis.Rotation = PinchworkUE::ToCore(X.GetRotation());
		GrabBasis.Scale = (float)X.GetScale3D().GetMax();
		bTwoHandActive = true;
	}
	else if (bBoth && bTwoHandActive)
	{
		const Pinchwork::FManipulationDelta D = Manipulator.Update(PinchworkUE::ToCore(L), PinchworkUE::ToCore(R));
		LastScale = D.Scale;
		Pinchwork::FObjectTransform Res = Manipulator.ApplyDelta(GrabBasis, D);
		const float ClampedScale = FMath::Clamp(Res.Scale, 0.03f, 1.5f);
		Cube->SetActorLocationAndRotation(PinchworkUE::ToUE(Res.Position), PinchworkUE::ToUE(Res.Rotation));
		Cube->SetActorScale3D(FVector(ClampedScale));
	}
	else if (!bBoth && bTwoHandActive)
	{
		Manipulator.End();
		bTwoHandActive = false;
	}
}

void UPinchworkShowcaseSubsystem::UpdateMacro()
{
	if (MacroId < 0) { return; }
	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	UHandTrackingComponent* Hands[2] = { LeftHand.Get(), RightHand.Get() };
	for (int32 i = 0; i < 2; ++i)
	{
		UHandTrackingComponent* H = Hands[i];
		if (!H) { continue; }
		const uint8 G = (uint8)H->ActiveGesture;
		if (G == LastFedGesture[i]) { continue; }      // only feed on a transition
		LastFedGesture[i] = G;
		if (H->ActiveGesture == EHandGesture::None) { continue; }

		const std::vector<int> Done = Sequences.OnGesture(static_cast<Pinchwork::EGesture>(G), Now);
		for (int Id : Done)
		{
			OnMacroCompleted(UTF8_TO_TCHAR(Sequences.Sequence(Id).Name.c_str()));
		}
	}
}

void UPinchworkShowcaseSubsystem::OnMacroCompleted(const FString& Name)
{
	UWorld* World = GetWorld();
	LastMacroFired = Name;
	LastMacroFiredTime = World ? World->GetTimeSeconds() : 0.0;
	UE_LOG(LogPinchworkShowcase, Warning, TEXT("[PinchworkShowcase] MACRO COMPLETE: %s"), *Name);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9100, 3.0f, FColor::Green,
			FString::Printf(TEXT("MACRO COMPLETE: %s"), *Name));
	}
	if (AActor* Cube = TargetCube.Get())
	{
		SpawnBurst(Cube->GetActorLocation());
	}
}

void UPinchworkShowcaseSubsystem::SpawnBurst(const FVector& Center)
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh) { return; }

	const FVector Offsets[6] = {
		{ 8, 0, 0 }, { -8, 0, 0 }, { 0, 8, 0 }, { 0, -8, 0 }, { 0, 0, 8 }, { 0, 0, -8 } };
	for (const FVector& Off : Offsets)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Center + Off, FRotator::ZeroRotator, Params);
		if (!A) { continue; }
		A->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* MC = A->GetStaticMeshComponent())
		{
			MC->SetStaticMesh(CubeMesh);
			MC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MC->SetCastShadow(false);
		}
		A->SetActorScale3D(FVector(0.03f));
		FTimerHandle Th;
		World->GetTimerManager().SetTimer(Th,
			FTimerDelegate::CreateWeakLambda(A, [A]() { if (IsValid(A)) { A->Destroy(); } }),
			0.6f, false);
	}
}

void UPinchworkShowcaseSubsystem::DrawStatusHud()
{
	if (!GEngine) { return; }

	// Alpha-mode cycler readout, on its own key so it renders as a separate, brighter line.
	// Yellow because it is debug state, not normal app state - it should look temporary.
	GEngine->AddOnScreenDebugMessage(9102, 0.f, FColor::Yellow,
		FString::Printf(TEXT("ALPHA MODE (ring-thumb pinch to cycle): %s"), GAlphaModes[AlphaMode].Name));

	// Quality-mode cycler readout, distinct key so it renders as its own line.
	GEngine->AddOnScreenDebugMessage(9103, 0.f, FColor::Green,
		FString::Printf(TEXT("QUALITY MODE (pinky-pinch to cycle): %s"), GQualityModes[QualityMode].Name));

	// Render-config diagnostic: what's actually running, without a log pull. Dropped the SM6=on/off
	// readout (2026-09-05, Alex's request) -- this project never runs SM6, so showing it at all
	// invited people to wonder when it would turn on. Platform/shading-path/GPU tier are still
	// useful and are never "off" in a way that implies a missing feature.
	{
		const FStaticShaderPlatform Platform = GMaxRHIShaderPlatform;
		const FString PlatformName = FDataDrivenShaderPlatformInfo::GetName(Platform).ToString();
		const bool bForward = IsForwardShadingEnabled(Platform);
		GEngine->AddOnScreenDebugMessage(9104, 0.f, FColor::Orange,
			FString::Printf(TEXT("RENDER: platform=%s  %s  GPU=%s"),
				*PlatformName,
				bForward ? TEXT("FORWARD") : TEXT("DEFERRED"),
				*UVisionProGPUDetection::GetGPUTierDisplayString()));
	}

	// GPU trace / Lumen override readout -- both new 2026-09-04, neither needs hand data.
	GEngine->AddOnScreenDebugMessage(9105, 0.f, FColor::Magenta,
		FString::Printf(TEXT("GPU TRACE: %s   LUMEN OVERRIDE: %s"),
			bGPUTraceFired ? TEXT("fired (see log/Saved/Profiling/GPU)") : TEXT("pending or disabled"),
			LastAppliedLumenOverride == 1 ? TEXT("FORCED ON") :
			LastAppliedLumenOverride == 0 ? TEXT("FORCED OFF") : TEXT("shipped default")));
}

void UPinchworkShowcaseSubsystem::DrawHud()
{
	if (!GEngine) { return; }
	// FString (owning) — NOT const TCHAR*: the UTF8_TO_TCHAR temporary would
	// dangle past this statement otherwise.
	const FString LName = UTF8_TO_TCHAR(Pinchwork::GestureName(static_cast<Pinchwork::EGesture>((uint8)LeftHand->ActiveGesture)));
	const FString RName = UTF8_TO_TCHAR(Pinchwork::GestureName(static_cast<Pinchwork::EGesture>((uint8)RightHand->ActiveGesture)));
	const FString Hud = FString::Printf(
		TEXT("Pinchwork 2.0 showcase\n")
		TEXT("L: %s    R: %s\n")
		TEXT("two-hand: %s   scale x%.2f\n")
		TEXT("macro Fist->Open->FingerGuns  (last: %s)"),
		*LName, *RName,
		bTwoHandActive ? TEXT("ON") : TEXT("off"), LastScale,
		LastMacroFired.IsEmpty() ? TEXT("-") : *LastMacroFired);
	// Key 9101, duration 0 -> refreshed every frame (persistent overlay).
	GEngine->AddOnScreenDebugMessage(9101, 0.f, FColor::Cyan, Hud);
}
