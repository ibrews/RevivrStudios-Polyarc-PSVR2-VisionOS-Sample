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

DEFINE_LOG_CATEGORY_STATIC(LogPinchworkShowcase, Log, All);

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
		int32 PropagateAlpha; // r.Mobile.PropagateAlpha
	};

	// On-device verification (M5/Lumen, 2026-09-02): modes 0, 2, and 4 all render everything
	// semi-transparent -- wrong. Only modes 1 and 3 look correct. Mode 1 (Epic stock pass) is now
	// the shipping default below, and the ring-thumb-pinch cycler alternates 1<->3 only -- it never
	// lands on 0, 2, or 4. All 5 entries and their exact Name strings stay intact: other
	// tooling/logs reference them by index and name -- this only changes which ones are reachable.
	static const FAlphaMode GAlphaModes[] = {
		{ TEXT("0 inline=ON  stock=off alpha=ON  (current default)"), 1, 0, 1 },
		{ TEXT("1 inline=off stock=ON  alpha=ON  (Epic stock pass)"), 0, 1, 1 },
		{ TEXT("2 inline=off stock=off alpha=ON  (NO inversion)"),    0, 0, 1 },
		{ TEXT("3 inline=ON  stock=ON  alpha=ON  (both - expect double-invert)"), 1, 1, 1 },
		{ TEXT("4 inline=off stock=off alpha=off (no passthrough blend)"), 0, 0, 0 },
	};
	static constexpr int32 GNumAlphaModes = UE_ARRAY_COUNT(GAlphaModes);

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
		int32 AntiAliasing;   // r.Mobile.AntiAliasing (0=off,1=FXAA,2=TAA,3=MSAA,4=TSR,5=SMAA)
	};

	// Mode 0 matches the shipping default so the app starts in its normal state.
	static const FQualityMode GQualityModes[] = {
		{ TEXT("0 shadow=5 nanite=ON  aa=FXAA (current default)"), 5, 1, 1 },
		{ TEXT("1 shadow=1 nanite=ON  aa=FXAA (low shadow)"),      1, 1, 1 },
		{ TEXT("2 shadow=0 nanite=ON  aa=FXAA (shadows OFF)"),     0, 1, 1 },
		{ TEXT("3 shadow=5 nanite=off aa=FXAA (Nanite OFF)"),      5, 0, 1 },
		{ TEXT("4 shadow=5 nanite=ON  aa=MSAA"),                   5, 1, 3 },
		{ TEXT("5 shadow=5 nanite=ON  aa=TAA"),                    5, 1, 2 },
		{ TEXT("6 shadow=3 nanite=ON  aa=FXAA (medium shadow)"),   3, 1, 1 },
	};
	static constexpr int32 GNumQualityModes = UE_ARRAY_COUNT(GQualityModes);
}

void UPinchworkShowcaseSubsystem::ApplyAlphaMode(int32 Mode)
{
	const FAlphaMode& M = GAlphaModes[FMath::Clamp(Mode, 0, GNumAlphaModes - 1)];
	SetCVarInt(TEXT("r.Mobile.VisionOS.InlineAlphaInvert"), M.InlineInvert);
	SetCVarInt(TEXT("r.AlphaInvertPass"), M.StockPass);
	SetCVarInt(TEXT("r.Mobile.PropagateAlpha"), M.PropagateAlpha);
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
		// On-device verification showed only modes 1 (Epic stock pass) and 3 (both -- double-invert)
		// render correctly; 0, 2, and 4 all put everything in semitransparency. Toggle between just
		// those two rather than stepping the full GNumAlphaModes range.
		AlphaMode = (AlphaMode == 1) ? 3 : 1;
		ApplyAlphaMode(AlphaMode);
	}
	bPrevRingPinch = bRing;
}

void UPinchworkShowcaseSubsystem::ApplyQualityMode(int32 Mode)
{
	const FQualityMode& M = GQualityModes[FMath::Clamp(Mode, 0, GNumQualityModes - 1)];
	SetCVarInt(TEXT("r.ShadowQuality"), M.ShadowQuality);
	SetCVarInt(TEXT("r.Nanite"), M.Nanite);
	SetCVarInt(TEXT("r.Mobile.AntiAliasing"), M.AntiAliasing);
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

bool UPinchworkShowcaseSubsystem::TryWire()
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) { return false; } // VR pawn spawns after world begin — retry next tick

	TArray<UHandTrackingComponent*> Hands;
	Pawn->GetComponents<UHandTrackingComponent>(Hands);
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

	// Alpha-mode cycler readout, on its own key so it renders as a separate, brighter line.
	// Yellow because it is debug state, not normal app state - it should look temporary.
	GEngine->AddOnScreenDebugMessage(9102, 0.f, FColor::Yellow,
		FString::Printf(TEXT("ALPHA MODE (ring-thumb pinch to cycle): %s"), GAlphaModes[AlphaMode].Name));

	// Quality-mode cycler readout, distinct key so it renders as its own line.
	GEngine->AddOnScreenDebugMessage(9103, 0.f, FColor::Green,
		FString::Printf(TEXT("QUALITY MODE (pinky-pinch to cycle): %s"), GQualityModes[QualityMode].Name));

	// Render-config diagnostic: what's actually running, without a log pull. Alex asked on-device
	// "are we still in deferred rendering, is SM6 on" -- this answers that live.
	{
		const FStaticShaderPlatform Platform = GMaxRHIShaderPlatform;
		const FString PlatformName = FDataDrivenShaderPlatformInfo::GetName(Platform).ToString();
		const bool bForward = IsForwardShadingEnabled(Platform);
		const bool bSM6 = PlatformName.Contains(TEXT("SM6"));
		GEngine->AddOnScreenDebugMessage(9104, 0.f, FColor::Orange,
			FString::Printf(TEXT("RENDER: platform=%s  %s  SM6=%s  GPU=%s"),
				*PlatformName,
				bForward ? TEXT("FORWARD") : TEXT("DEFERRED"),
				bSM6 ? TEXT("ON") : TEXT("off"),
				*UVisionProGPUDetection::GetGPUTierDisplayString()));
	}
}
