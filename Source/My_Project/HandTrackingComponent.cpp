#include "HandTrackingComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// IHandTracker reports keypoints in world space already, so converting
	// from meters→centimeters isn't needed — UE's FTransform is in UU (cm).
	constexpr float ScaleToCmFactor = 1.0f;

	// Shared calibration state across every UHandTrackingComponent instance
	// in the process. Both left and right hand components read from and
	// write to these globals so the calibration phase stays synchronized
	// (the user does the same pose with both hands simultaneously) and the
	// resulting per-finger thresholds are shared — left and right hand
	// anatomy is symmetric enough that one calibration covers both.
	EHandCalibrationState GSharedCalibrationPhase = EHandCalibrationState::Uncalibrated;
	TArray<float> GSharedFistRatios;       // size 5 once captured
	TArray<float> GSharedOpenRatios;       // size 5 once captured
	TArray<float> GSharedCalibratedThresholds;  // size 5, zeroed before calibration

	void ResetSharedCalibration()
	{
		GSharedCalibrationPhase = EHandCalibrationState::AwaitingFist;
		GSharedFistRatios.Reset();
		GSharedOpenRatios.Reset();
		GSharedCalibratedThresholds.Reset();
	}

	// Atomic-ish phase advance. Pass the phase the caller observed; if the
	// shared state has already moved on (the other hand captured first this
	// same frame), the report is dropped. Returns true on successful advance.
	bool TryAdvanceCalibrationPhase(EHandCalibrationState ObservedPhase, const TArray<float>& Ratios, float MinFingerSeparation, float GlobalFallback)
	{
		if (GSharedCalibrationPhase != ObservedPhase || Ratios.Num() != 5)
		{
			return false;
		}

		if (ObservedPhase == EHandCalibrationState::AwaitingFist)
		{
			GSharedFistRatios = Ratios;
			GSharedCalibrationPhase = EHandCalibrationState::AwaitingOpen;
			return true;
		}
		if (ObservedPhase == EHandCalibrationState::AwaitingOpen)
		{
			GSharedOpenRatios = Ratios;
			// Compute per-finger thresholds at the midpoint. Any finger
			// whose Open ratio isn't meaningfully higher than its Fist
			// ratio falls back to the global threshold — a degenerate
			// capture for that finger (e.g., thumb that can't straighten)
			// shouldn't poison its classification.
			GSharedCalibratedThresholds.SetNum(5);
			for (int32 i = 0; i < 5; ++i)
			{
				const float Fist = GSharedFistRatios.IsValidIndex(i) ? GSharedFistRatios[i] : 0.0f;
				const float Open = GSharedOpenRatios.IsValidIndex(i) ? GSharedOpenRatios[i] : 0.0f;
				if (Open > Fist + MinFingerSeparation)
				{
					GSharedCalibratedThresholds[i] = (Fist + Open) * 0.5f;
				}
				else
				{
					GSharedCalibratedThresholds[i] = GlobalFallback;
				}
			}
			GSharedCalibrationPhase = EHandCalibrationState::Calibrated;
			return true;
		}
		return false;
	}

	IHandTracker* FindHandTracker()
	{
		IModularFeatures& Features = IModularFeatures::Get();
		const FName FeatureName = IHandTracker::GetModularFeatureName();
		if (!Features.IsModularFeatureAvailable(FeatureName))
		{
			return nullptr;
		}
		TArray<IHandTracker*> Trackers = Features.GetModularFeatureImplementations<IHandTracker>(FeatureName);
		for (IHandTracker* Tracker : Trackers)
		{
			if (Tracker != nullptr)
			{
				return Tracker;
			}
		}
		return nullptr;
	}
}

UHandTrackingComponent::UHandTrackingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bWantsInitializeComponent = true;
	CachedKeypoints.SetNum(EHandKeypointCount);

	// Five-slot arrays — one entry per finger (thumb, index, middle, ring, little).
	CalibratedThresholds.Init(0.0f, 5);
	CalibrationStabilityWindow.SetNum(5);

	// FObjectFinder* internally calls CheckIfIsInConstructor which fatal-asserts
	// (LogClass: ConstructorHelpers used outside of constructor) if invoked
	// from runtime code. Resolving here — the only legal site — caches the
	// engine sphere mesh for use at BeginPlay.
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> EngineSphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	DefaultJointMesh = EngineSphereFinder.Get();
}

void UHandTrackingComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureInstancesInitialized();

	if (bAutoCalibrateOnStart && CalibrationState == EHandCalibrationState::Uncalibrated)
	{
		StartCalibration();
	}
}

void UHandTrackingComponent::EnsureInstancesInitialized()
{
	if (JointInstances)
	{
		return;
	}

	JointInstances = NewObject<UInstancedStaticMeshComponent>(GetOwner(), UInstancedStaticMeshComponent::StaticClass(), NAME_None, RF_Transient);
	JointInstances->SetupAttachment(this);
	JointInstances->SetMobility(EComponentMobility::Movable);
	JointInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JointInstances->SetCastShadow(false);
	JointInstances->SetReceivesDecals(false);
	JointInstances->bSelectable = false;
	JointInstances->SetGenerateOverlapEvents(false);
	JointInstances->RegisterComponent();

	if (UStaticMesh* Mesh = ResolveJointMesh())
	{
		JointInstances->SetStaticMesh(Mesh);
	}

	const FTransform HiddenInstance(FRotator::ZeroRotator, FVector::ZeroVector, FVector(KINDA_SMALL_NUMBER));
	for (int32 i = 0; i < EHandKeypointCount; ++i)
	{
		JointInstances->AddInstance(HiddenInstance, /*bWorldSpace=*/true);
	}
}

UStaticMesh* UHandTrackingComponent::ResolveJointMesh() const
{
	if (JointMesh)
	{
		return JointMesh;
	}
	// DefaultJointMesh was resolved in the constructor (the only legal site
	// for ConstructorHelpers). Engine BasicShape Sphere is shipped with every
	// project, so this should virtually never be null in a real build, but
	// caller paths guard for nullptr regardless.
	return DefaultJointMesh;
}

EControllerHand UHandTrackingComponent::ResolveControllerHand() const
{
	return bIsRight ? EControllerHand::Right : EControllerHand::Left;
}

void UHandTrackingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	EnsureInstancesInitialized();

	IHandTracker* Tracker = FindHandTracker();
	const bool bTrackerValid = Tracker && Tracker->IsHandTrackingStateValid();
	bIsTracking = bTrackerValid;

	if (!bTrackerValid)
	{
		if ((bHideMarkersWhenUntracked || !bShowJointMarkers) && JointInstances)
		{
			JointInstances->SetVisibility(false);
		}
		// Release any in-progress gesture state if we lose tracking mid-pose
		// so the label hides and downstream listeners get a clean ended event.
		if (bIsPinching)
		{
			bIsPinching = false;
			OnPinchEnded.Broadcast(GetSide());
		}
		if (bIsMiddlePinching)
		{
			bIsMiddlePinching = false;
			OnMiddlePinchEnded.Broadcast(GetSide());
		}
		if (bIsPinkyPinching)
		{
			bIsPinkyPinching = false;
			OnPinkyPinchEnded.Broadcast(GetSide());
		}
		if (ActiveGesture != EHandGesture::None)
		{
			const EHandGesture EndedGesture = ActiveGesture;
			ActiveGesture = EHandGesture::None;
			PendingGesture = EHandGesture::None;
			PendingGestureStableFor = 0.0f;
			OnGestureEnded.Broadcast(EndedGesture, GetSide());
		}
		// DrawDebugString is per-frame so silence is automatic when we don't call it.
		return;
	}

	const EControllerHand Hand = ResolveControllerHand();
	const FVector MarkerScale(JointMeshScale);

	// Keep the instanced markers' visibility in sync with the user toggle.
	// Per-instance transform writes are skipped below when markers are
	// off, but the visibility itself also needs to flip on/off explicitly.
	if (JointInstances)
	{
		JointInstances->SetVisibility(bShowJointMarkers);
	}

	FTransform ThumbTipWorld = FTransform::Identity;
	FTransform IndexTipWorld = FTransform::Identity;
	FTransform MiddleTipWorld = FTransform::Identity;
	FTransform LittleTipWorld = FTransform::Identity;
	bool bHaveThumbTip = false;
	bool bHaveIndexTip = false;
	bool bHaveMiddleTip = false;
	bool bHaveLittleTip = false;

	for (int32 i = 0; i < EHandKeypointCount; ++i)
	{
		const EHandKeypoint Keypoint = static_cast<EHandKeypoint>(i);
		FTransform KeypointTransform;
		float Radius = 0.0f;
		const bool bGot = Tracker->GetKeypointState(Hand, Keypoint, KeypointTransform, Radius);
		if (!bGot)
		{
			CachedKeypoints[i] = FTransform::Identity;
			if (JointInstances)
			{
				const FTransform HiddenInstance(FRotator::ZeroRotator, FVector::ZeroVector, FVector(KINDA_SMALL_NUMBER));
				JointInstances->UpdateInstanceTransform(i, HiddenInstance, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
			}
			continue;
		}

		KeypointTransform.SetScale3D(KeypointTransform.GetScale3D() * ScaleToCmFactor);
		CachedKeypoints[i] = KeypointTransform;

		if (JointInstances && bShowJointMarkers)
		{
			FTransform InstanceTransform = KeypointTransform;
			InstanceTransform.SetScale3D(MarkerScale);
			JointInstances->UpdateInstanceTransform(i, InstanceTransform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
		}

		if (Keypoint == EHandKeypoint::ThumbTip)
		{
			ThumbTipWorld = KeypointTransform;
			bHaveThumbTip = true;
		}
		else if (Keypoint == EHandKeypoint::IndexTip)
		{
			IndexTipWorld = KeypointTransform;
			bHaveIndexTip = true;
		}
		else if (Keypoint == EHandKeypoint::MiddleTip)
		{
			MiddleTipWorld = KeypointTransform;
			bHaveMiddleTip = true;
		}
		else if (Keypoint == EHandKeypoint::LittleTip)
		{
			LittleTipWorld = KeypointTransform;
			bHaveLittleTip = true;
		}
	}

	if (JointInstances && bShowJointMarkers)
	{
		JointInstances->MarkRenderStateDirty();
	}

	if (bHaveThumbTip && bHaveIndexTip)
	{
		UpdatePinchState(ThumbTipWorld, IndexTipWorld);
	}
	if (bHaveThumbTip && bHaveMiddleTip)
	{
		UpdateMiddlePinchState(ThumbTipWorld, MiddleTipWorld);
	}
	if (bHaveThumbTip && bHaveLittleTip)
	{
		UpdatePinkyPinchState(ThumbTipWorld, LittleTipWorld);
	}
	// Calibration runs every tick while not yet completed so it can finish
	// the moment the user holds a stable pose, even if it's mid-Tick.
	if (CalibrationState != EHandCalibrationState::Calibrated)
	{
		UpdateCalibrationState(DeltaTime);
	}

	if (bDetectCurlGestures)
	{
		UpdateGestureState(DeltaTime);
	}

	if (bShowGestureLabel)
	{
		UpdateGestureLabel();
	}
}

void UHandTrackingComponent::UpdatePinchState(const FTransform& ThumbTipWorld, const FTransform& IndexTipWorld)
{
	const float DistanceCm = FVector::Dist(ThumbTipWorld.GetLocation(), IndexTipWorld.GetLocation());
	const float EnterCm = PinchThresholdCm;
	const float ExitCm = PinchThresholdCm + PinchReleaseHysteresisCm;

	if (!bIsPinching && DistanceCm <= EnterCm)
	{
		bIsPinching = true;
		OnPinchStarted.Broadcast(GetSide());
		if (bSpawnDotOnIndexPinch)
		{
			const FVector Midpoint = (ThumbTipWorld.GetLocation() + IndexTipWorld.GetLocation()) * 0.5f;
			SpawnTransientSphere(Midpoint, IndexPinchDotRadiusCm, IndexPinchDotDurationSec);
		}
	}
	else if (bIsPinching && DistanceCm >= ExitCm)
	{
		bIsPinching = false;
		OnPinchEnded.Broadcast(GetSide());
	}
}

void UHandTrackingComponent::UpdatePinkyPinchState(const FTransform& ThumbTipWorld, const FTransform& PinkyTipWorld)
{
	const float DistanceCm = FVector::Dist(ThumbTipWorld.GetLocation(), PinkyTipWorld.GetLocation());
	const float EnterCm = PinkyPinchThresholdCm;
	const float ExitCm = PinkyPinchThresholdCm + PinkyPinchReleaseHysteresisCm;

	if (!bIsPinkyPinching && DistanceCm <= EnterCm)
	{
		bIsPinkyPinching = true;
		OnPinkyPinchStarted.Broadcast(GetSide());
		if (bSpawnExplosionOnPinkyPinch)
		{
			const FVector Midpoint = (ThumbTipWorld.GetLocation() + PinkyTipWorld.GetLocation()) * 0.5f;
			SpawnExplosion(Midpoint);
		}
	}
	else if (bIsPinkyPinching && DistanceCm >= ExitCm)
	{
		bIsPinkyPinching = false;
		OnPinkyPinchEnded.Broadcast(GetSide());
	}
}

void UHandTrackingComponent::UpdateMiddlePinchState(const FTransform& ThumbTipWorld, const FTransform& MiddleTipWorld)
{
	const float DistanceCm = FVector::Dist(ThumbTipWorld.GetLocation(), MiddleTipWorld.GetLocation());
	const float EnterCm = MiddlePinchThresholdCm;
	const float ExitCm = MiddlePinchThresholdCm + MiddlePinchReleaseHysteresisCm;

	if (!bIsMiddlePinching && DistanceCm <= EnterCm)
	{
		bIsMiddlePinching = true;
		OnMiddlePinchStarted.Broadcast(GetSide());
	}
	else if (bIsMiddlePinching && DistanceCm >= ExitCm)
	{
		bIsMiddlePinching = false;
		OnMiddlePinchEnded.Broadcast(GetSide());
	}
}

void UHandTrackingComponent::SpawnExplosion(const FVector& WorldLocation)
{
	SpawnTransientSphere(WorldLocation, ExplosionRadiusCm, ExplosionDurationSec);
}

void UHandTrackingComponent::SpawnTransientSphere(const FVector& WorldLocation, float RadiusCm, float DurationSec)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UStaticMesh* MeshToUse = ExplosionMesh ? ExplosionMesh.Get() : DefaultJointMesh.Get();
	if (!MeshToUse)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* SphereActor = World->SpawnActor<AStaticMeshActor>(WorldLocation, FRotator::ZeroRotator, Params);
	if (!SphereActor)
	{
		return;
	}

	SphereActor->SetMobility(EComponentMobility::Movable);
	if (UStaticMeshComponent* MeshComp = SphereActor->GetStaticMeshComponent())
	{
		MeshComp->SetStaticMesh(MeshToUse);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCastShadow(false);
		MeshComp->SetReceivesDecals(false);
		// Engine BasicShapes sphere is 100 cm diameter (50 cm radius) at scale 1.
		const float Scale = FMath::Max(RadiusCm, 0.1f) / 50.0f;
		MeshComp->SetWorldScale3D(FVector(Scale));
	}

	// Auto-destroy after the configured lifetime. Weak-lambda guards against
	// a stale timer poking a GC'd actor.
	FTimerHandle DestroyHandle;
	World->GetTimerManager().SetTimer(
		DestroyHandle,
		FTimerDelegate::CreateWeakLambda(SphereActor, [SphereActor]()
		{
			if (IsValid(SphereActor))
			{
				SphereActor->Destroy();
			}
		}),
		FMath::Max(DurationSec, 0.05f),
		false);
}

float UHandTrackingComponent::ComputeFingerExtensionRatio(EHandKeypoint TipKey) const
{
	// Resolve the matching Metacarpal joint for this finger.
	int32 MetacarpalIdx = INDEX_NONE;
	switch (TipKey)
	{
		case EHandKeypoint::ThumbTip:  MetacarpalIdx = static_cast<int32>(EHandKeypoint::ThumbMetacarpal);  break;
		case EHandKeypoint::IndexTip:  MetacarpalIdx = static_cast<int32>(EHandKeypoint::IndexMetacarpal);  break;
		case EHandKeypoint::MiddleTip: MetacarpalIdx = static_cast<int32>(EHandKeypoint::MiddleMetacarpal); break;
		case EHandKeypoint::RingTip:   MetacarpalIdx = static_cast<int32>(EHandKeypoint::RingMetacarpal);   break;
		case EHandKeypoint::LittleTip: MetacarpalIdx = static_cast<int32>(EHandKeypoint::LittleMetacarpal); break;
		default: return 0.0f;
	}

	const int32 TipIdx = static_cast<int32>(TipKey);
	if (!CachedKeypoints.IsValidIndex(TipIdx) || !CachedKeypoints.IsValidIndex(MetacarpalIdx))
	{
		return 0.0f;
	}

	// Sum the joint-to-joint distances along the chain (Metacarpal → … → Tip).
	float ChainLen = 0.0f;
	for (int32 i = MetacarpalIdx; i < TipIdx; ++i)
	{
		ChainLen += FVector::Dist(CachedKeypoints[i].GetLocation(), CachedKeypoints[i + 1].GetLocation());
	}
	if (ChainLen < 1.0f)
	{
		return 0.0f;
	}

	const float StraightDist = FVector::Dist(
		CachedKeypoints[TipIdx].GetLocation(),
		CachedKeypoints[MetacarpalIdx].GetLocation());
	return FMath::Clamp(StraightDist / ChainLen, 0.0f, 1.5f);
}

bool UHandTrackingComponent::IsFingerExtended(EHandKeypoint TipKey) const
{
	const float Ratio = ComputeFingerExtensionRatio(TipKey);
	const int32 FingerIdx = FingerIndexFromTipKey(TipKey);

	if (GSharedCalibrationPhase == EHandCalibrationState::Calibrated
		&& GSharedCalibratedThresholds.IsValidIndex(FingerIdx)
		&& GSharedCalibratedThresholds[FingerIdx] > 0.0f)
	{
		return Ratio >= GSharedCalibratedThresholds[FingerIdx];
	}
	return Ratio >= FingerExtendedRatioThreshold;
}

int32 UHandTrackingComponent::FingerIndexFromTipKey(EHandKeypoint TipKey) const
{
	switch (TipKey)
	{
		case EHandKeypoint::ThumbTip:  return 0;
		case EHandKeypoint::IndexTip:  return 1;
		case EHandKeypoint::MiddleTip: return 2;
		case EHandKeypoint::RingTip:   return 3;
		case EHandKeypoint::LittleTip: return 4;
		default: return INDEX_NONE;
	}
}

void UHandTrackingComponent::StartCalibration()
{
	// Resets the SHARED calibration state — every UHandTrackingComponent
	// instance in the process is now in AwaitingFist.
	ResetSharedCalibration();
	CalibrationState = GSharedCalibrationPhase;
	CalibrationCurrentStableSec = 0.0f;
	for (TArray<float>& Window : CalibrationStabilityWindow)
	{
		Window.Reset();
	}
	for (float& V : CalibratedThresholds) { V = 0.0f; }
}

void UHandTrackingComponent::CaptureCurrentRatios(TArray<float>& OutRatios) const
{
	OutRatios.SetNum(5);
	OutRatios[0] = ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip);
	OutRatios[1] = ComputeFingerExtensionRatio(EHandKeypoint::IndexTip);
	OutRatios[2] = ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip);
	OutRatios[3] = ComputeFingerExtensionRatio(EHandKeypoint::RingTip);
	OutRatios[4] = ComputeFingerExtensionRatio(EHandKeypoint::LittleTip);
}

bool UHandTrackingComponent::AreRatiosStable() const
{
	for (const TArray<float>& Window : CalibrationStabilityWindow)
	{
		if (Window.Num() < CalibrationStabilityWindowSize)
		{
			return false;
		}
		float MinVal = TNumericLimits<float>::Max();
		float MaxVal = TNumericLimits<float>::Lowest();
		for (float V : Window)
		{
			MinVal = FMath::Min(MinVal, V);
			MaxVal = FMath::Max(MaxVal, V);
		}
		if ((MaxVal - MinVal) > CalibrationStabilityMaxRange)
		{
			return false;
		}
	}
	return true;
}

void UHandTrackingComponent::UpdateCalibrationState(float DeltaTime)
{
	// Always mirror the shared phase into the instance UPROPERTY so BP and
	// the label code see the current global state. If another hand
	// advanced the phase between frames, our stability counter is no
	// longer relevant — reset it so this hand starts measuring stability
	// for the NEW pose.
	if (CalibrationState != GSharedCalibrationPhase)
	{
		CalibrationState = GSharedCalibrationPhase;
		CalibrationCurrentStableSec = 0.0f;
		for (TArray<float>& Window : CalibrationStabilityWindow)
		{
			Window.Reset();
		}
	}
	CalibratedThresholds = GSharedCalibratedThresholds;

	if (CalibrationState == EHandCalibrationState::Uncalibrated
		|| CalibrationState == EHandCalibrationState::Calibrated)
	{
		return;
	}

	// Tracking must be live to capture meaningful ratios.
	if (!bIsTracking)
	{
		CalibrationCurrentStableSec = 0.0f;
		return;
	}

	// Push the latest ratio into each finger's rolling window.
	TArray<float> Current;
	CaptureCurrentRatios(Current);
	for (int32 i = 0; i < 5; ++i)
	{
		TArray<float>& Window = CalibrationStabilityWindow[i];
		Window.Add(Current[i]);
		if (Window.Num() > CalibrationStabilityWindowSize)
		{
			Window.RemoveAt(0, 1, EAllowShrinking::No);
		}
	}

	if (!AreRatiosStable())
	{
		CalibrationCurrentStableSec = 0.0f;
		return;
	}
	CalibrationCurrentStableSec += DeltaTime;
	if (CalibrationCurrentStableSec < CalibrationStabilityRequiredSec)
	{
		return;
	}

	// Stable long enough — try to advance the shared phase. The within-pose
	// sanity gates are gone: we trust whatever stable pose the user holds.
	// Validation happens cross-pose in TryAdvanceCalibrationPhase (per
	// finger: Open - Fist must exceed CalibrationFistMaxRatio's distance,
	// otherwise that finger falls back to the global threshold).
	constexpr float MinFingerSeparation = 0.10f;
	const bool bAdvanced = TryAdvanceCalibrationPhase(CalibrationState, Current, MinFingerSeparation, FingerExtendedRatioThreshold);
	if (bAdvanced)
	{
		CalibrationState = GSharedCalibrationPhase;
		CalibratedThresholds = GSharedCalibratedThresholds;
		CalibrationCurrentStableSec = 0.0f;
		for (TArray<float>& Window : CalibrationStabilityWindow)
		{
			Window.Reset();
		}
	}
}

EHandGesture UHandTrackingComponent::ClassifyGesture(bool bThumb, bool bIndex, bool bMiddle, bool bRing, bool bLittle) const
{
	// Legacy binary classifier — kept for completeness but the confidence
	// path is what UpdateGestureState calls. Not used in the main flow.
	const int32 ExtendedCount = (bThumb?1:0) + (bIndex?1:0) + (bMiddle?1:0) + (bRing?1:0) + (bLittle?1:0);

	if (ExtendedCount == 5) return EHandGesture::OpenPalm;
	if (ExtendedCount == 0) return EHandGesture::Fist;
	if (bThumb && !bIndex && !bMiddle && !bRing && !bLittle) return EHandGesture::ThumbsUp;
	if (bIndex && bMiddle && !bRing && !bLittle && !bThumb)  return EHandGesture::Peace;
	if (bThumb && bIndex && !bMiddle && !bRing && !bLittle) return EHandGesture::FingerGuns;
	if (bThumb && bLittle && !bIndex && !bMiddle && !bRing) return EHandGesture::CallMe;
	if (bIndex && bLittle && !bMiddle && !bRing)             return EHandGesture::RockOn;
	return EHandGesture::None;
}

float UHandTrackingComponent::NormalizeFingerRatio(int32 FingerIdx, float Ratio) const
{
	// Normalize the raw chain ratio into 0–1 where 0 = user's fist pose,
	// 1 = user's open pose. With calibration this is a clean per-finger
	// mapping; without it we approximate via the global threshold.
	if (GSharedCalibrationPhase == EHandCalibrationState::Calibrated
		&& GSharedFistRatios.IsValidIndex(FingerIdx)
		&& GSharedOpenRatios.IsValidIndex(FingerIdx))
	{
		const float Fist = GSharedFistRatios[FingerIdx];
		const float Open = GSharedOpenRatios[FingerIdx];
		const float Range = Open - Fist;
		if (Range > 0.05f)
		{
			return FMath::Clamp((Ratio - Fist) / Range, 0.0f, 1.0f);
		}
	}
	// Uncalibrated fallback: a smooth ramp around the global threshold so
	// classification still has a gradient (no calibration → softer reads).
	const float Pivot = FingerExtendedRatioThreshold;
	const float HalfWidth = 0.12f;
	return FMath::Clamp((Ratio - (Pivot - HalfWidth)) / (2.0f * HalfWidth), 0.0f, 1.0f);
}

EHandGesture UHandTrackingComponent::ClassifyGestureByConfidence(const float NormalizedRatios[5], float& OutTopConfidence) const
{
	// Fingerprint encoding: 1 = expected extended, 0 = expected curled,
	// -1 = don't care. Each gesture is one row. RockOn doesn't care about
	// the thumb (some people tuck it, some leave it out).
	struct FFingerprint
	{
		EHandGesture Gesture;
		int8 Thumb, Index, Middle, Ring, Little;
	};
	static const FFingerprint Fingerprints[] = {
		{ EHandGesture::OpenPalm,    1,  1,  1,  1,  1 },
		{ EHandGesture::Fist,        0,  0,  0,  0,  0 },
		{ EHandGesture::ThumbsUp,    1,  0,  0,  0,  0 },
		{ EHandGesture::Peace,       0,  1,  1,  0,  0 },
		{ EHandGesture::FingerGuns,  1,  1,  0,  0,  0 },
		{ EHandGesture::CallMe,      1,  0,  0,  0,  1 },
		{ EHandGesture::RockOn,     -1,  1,  0,  0,  1 },
	};

	float TopScore = 0.0f;
	float SecondScore = 0.0f;
	EHandGesture TopGesture = EHandGesture::None;

	for (const FFingerprint& FP : Fingerprints)
	{
		const int8 Expect[5] = { FP.Thumb, FP.Index, FP.Middle, FP.Ring, FP.Little };
		// MIN-based scoring (weakest-link). For ThumbsUp-vs-CallMe the only
		// differentiator is the pinky, so a clearly-extended pinky drops
		// ThumbsUp's score to ~0 while CallMe stays high (or vice versa).
		// Average would let the other 4 matching fingers dominate and
		// leave a tiny margin between similar gestures.
		float MinScore = 1.0f;
		bool bHasContribution = false;
		for (int32 i = 0; i < 5; ++i)
		{
			if (Expect[i] < 0) { continue; }
			const float Want = (Expect[i] == 1) ? NormalizedRatios[i] : (1.0f - NormalizedRatios[i]);
			MinScore = FMath::Min(MinScore, Want);
			bHasContribution = true;
		}
		const float Score = bHasContribution ? MinScore : 0.0f;

		if (Score > TopScore)
		{
			SecondScore = TopScore;
			TopScore = Score;
			TopGesture = FP.Gesture;
		}
		else if (Score > SecondScore)
		{
			SecondScore = Score;
		}
	}

	OutTopConfidence = TopScore;

	// Gate 1: absolute floor — anything below it is "neutral" (no label).
	if (TopScore < MinGestureConfidence)
	{
		return EHandGesture::None;
	}
	// Gate 2: must beat runner-up by the margin. Prevents flicker between
	// nearly-tied gestures (e.g., during a transition).
	if ((TopScore - SecondScore) < MinGestureConfidenceMargin)
	{
		return EHandGesture::None;
	}
	return TopGesture;
}

void UHandTrackingComponent::UpdateGestureState(float DeltaTime)
{
	// Confidence-based classification: each gesture has an expected
	// per-finger fingerprint (extended/curled/don't-care). We score how
	// well the live normalized ratios match each fingerprint, then pick
	// the winner only if it clears the absolute confidence floor AND
	// beats the runner-up by a margin. The binary IsFingerExtended path
	// stays in service for the [TIMRL] debug strip.
	float NormalizedRatios[5];
	NormalizedRatios[0] = NormalizeFingerRatio(0, ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip));
	NormalizedRatios[1] = NormalizeFingerRatio(1, ComputeFingerExtensionRatio(EHandKeypoint::IndexTip));
	NormalizedRatios[2] = NormalizeFingerRatio(2, ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip));
	NormalizedRatios[3] = NormalizeFingerRatio(3, ComputeFingerExtensionRatio(EHandKeypoint::RingTip));
	NormalizedRatios[4] = NormalizeFingerRatio(4, ComputeFingerExtensionRatio(EHandKeypoint::LittleTip));

	float TopConfidence = 0.0f;
	const EHandGesture Detected = ClassifyGestureByConfidence(NormalizedRatios, TopConfidence);

	if (Detected == PendingGesture)
	{
		PendingGestureStableFor += DeltaTime;
	}
	else
	{
		PendingGesture = Detected;
		PendingGestureStableFor = 0.0f;
	}

	// Only commit a transition once the new pose has held long enough.
	if (PendingGestureStableFor >= GestureStabilityWindowSec && PendingGesture != ActiveGesture)
	{
		const EHandGesture OldGesture = ActiveGesture;
		ActiveGesture = PendingGesture;

		if (OldGesture != EHandGesture::None)
		{
			OnGestureEnded.Broadcast(OldGesture, GetSide());
		}
		if (ActiveGesture != EHandGesture::None)
		{
			OnGestureStarted.Broadcast(ActiveGesture, GetSide());
			if (bSpawnShapesOnGestures)
			{
				const FTransform PalmTransform = GetKeypointWorldTransform(EHandKeypoint::Palm);
				SpawnGestureVisual(ActiveGesture, PalmTransform);
			}
		}
	}
}

void UHandTrackingComponent::SpawnGestureVisual(EHandGesture Gesture, const FTransform& PalmTransform)
{
	const FVector PalmLoc   = PalmTransform.GetLocation();
	const FVector PalmFwd   = PalmTransform.GetRotation().GetForwardVector();
	const FVector PalmRight = PalmTransform.GetRotation().GetRightVector();
	const FVector PalmUp    = PalmTransform.GetRotation().GetUpVector();

	switch (Gesture)
	{
		case EHandGesture::OpenPalm:
		{
			// 1 huge shield sphere centered on the palm.
			SpawnTransientSphere(PalmLoc, 20.0f, 0.8f);
			break;
		}
		case EHandGesture::Fist:
		{
			// 8 small frag spheres in a tight cluster around the palm.
			const FVector Offsets[] = {
				{  2.0f,  0.0f,  0.0f }, { -2.0f,  0.0f,  0.0f },
				{  0.0f,  2.0f,  0.0f }, {  0.0f, -2.0f,  0.0f },
				{  0.0f,  0.0f,  2.0f }, {  0.0f,  0.0f, -2.0f },
				{  1.5f,  1.5f,  1.5f }, { -1.5f, -1.5f, -1.5f }
			};
			for (const FVector& Off : Offsets)
			{
				SpawnTransientSphere(PalmLoc + Off, 2.0f, 0.5f);
			}
			break;
		}
		case EHandGesture::Peace:
		{
			// V shape: 2 medium spheres in front of and offset from the palm.
			SpawnTransientSphere(PalmLoc + PalmFwd * 8.0f + PalmRight * -3.0f, 3.5f, 0.6f);
			SpawnTransientSphere(PalmLoc + PalmFwd * 8.0f + PalmRight *  3.0f, 3.5f, 0.6f);
			break;
		}
		case EHandGesture::FingerGuns:
		{
			// 5 spheres firing along palm-forward, decreasing radius (bullet trail).
			for (int32 i = 0; i < 5; ++i)
			{
				const float DistFromPalm = 5.0f + 4.0f * static_cast<float>(i);
				const float Radius = FMath::Max(1.0f, 5.0f - 0.8f * static_cast<float>(i));
				SpawnTransientSphere(PalmLoc + PalmFwd * DistFromPalm, Radius, 0.7f);
			}
			break;
		}
		case EHandGesture::RockOn:
		{
			// 4 spheres: two "horns" up-left and up-right of the palm, plus
			// a stacked centerline (base + neck) for the goat silhouette.
			SpawnTransientSphere(PalmLoc + PalmUp * 7.0f + PalmRight * -4.0f, 2.5f, 0.6f);
			SpawnTransientSphere(PalmLoc + PalmUp * 7.0f + PalmRight *  4.0f, 2.5f, 0.6f);
			SpawnTransientSphere(PalmLoc + PalmUp * 3.0f,                      3.0f, 0.6f);
			SpawnTransientSphere(PalmLoc,                                      3.5f, 0.6f);
			break;
		}
		case EHandGesture::CallMe:
		{
			// 6-sphere ring (halo) in the palm-up/right plane.
			constexpr int32 N = 6;
			const float Radius = 8.0f;
			for (int32 i = 0; i < N; ++i)
			{
				const float Theta = (2.0f * PI * static_cast<float>(i)) / static_cast<float>(N);
				const FVector Offset = PalmRight * (Radius * FMath::Cos(Theta))
				                     + PalmUp    * (Radius * FMath::Sin(Theta));
				SpawnTransientSphere(PalmLoc + Offset, 2.5f, 0.7f);
			}
			break;
		}
		default:
			break;
	}
}

FTransform UHandTrackingComponent::GetKeypointWorldTransform(EHandKeypoint Keypoint) const
{
	const int32 Index = static_cast<int32>(Keypoint);
	if (CachedKeypoints.IsValidIndex(Index))
	{
		return CachedKeypoints[Index];
	}
	return FTransform::Identity;
}

void UHandTrackingComponent::EnsureLabelInitialized()
{
	// No-op — DrawDebugString is per-frame and needs no persistent component.
	// Kept as a member to preserve the .h signature in case we switch back
	// to a component-based renderer.
}

FString UHandTrackingComponent::BuildLabelText() const
{
	// While calibrating, hijack the label to walk the user through poses.
	if (CalibrationState != EHandCalibrationState::Calibrated)
	{
		// Build version marker bumped each pass to confirm new install
		// actually loaded on device — visionOS sometimes serves a cached
		// bundle, and "looks the same" can mean "stale cache".
		FString CalHeadline = TEXT("[v25] ");
		switch (CalibrationState)
		{
			case EHandCalibrationState::Uncalibrated:
				CalHeadline += TEXT("CALIBRATION PENDING");
				break;
			case EHandCalibrationState::AwaitingFist:
				CalHeadline += TEXT("CALIBRATE: MAKE A TIGHT FIST");
				break;
			case EHandCalibrationState::AwaitingOpen:
				CalHeadline += TEXT("CALIBRATE: OPEN HAND, FINGERS SPREAD");
				break;
			default: break;
		}
		const float HoldProgress = FMath::Clamp(
			CalibrationCurrentStableSec / FMath::Max(CalibrationStabilityRequiredSec, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		const int32 TP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip)  * 100.0f);
		const int32 IP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::IndexTip)  * 100.0f);
		const int32 MP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip) * 100.0f);
		const int32 RP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::RingTip)   * 100.0f);
		const int32 LP = FMath::FloorToInt(ComputeFingerExtensionRatio(EHandKeypoint::LittleTip) * 100.0f);
		return FString::Printf(TEXT("%s\nhold steady %d%%\nT%d I%d M%d R%d L%d"),
			*CalHeadline,
			FMath::FloorToInt(HoldProgress * 100.0f),
			TP, IP, MP, RP, LP);
	}

	// Priority: pinches read first (they're explicit and override curl
	// classification), then curl gesture. RockOn is disambiguated from
	// MiddlePinch because the pinch check beats the curl-pattern check.
	FString Headline;
	if (bIsPinching)
	{
		Headline = TEXT("INDEX PINCH");
	}
	else if (bIsMiddlePinching)
	{
		Headline = TEXT("MIDDLE PINCH");
	}
	else if (bIsPinkyPinching)
	{
		Headline = TEXT("PINKY PINCH");
	}
	else
	{
		switch (ActiveGesture)
		{
			case EHandGesture::OpenPalm:   Headline = TEXT("OPEN PALM");   break;
			case EHandGesture::Fist:       Headline = TEXT("FIST");        break;
			case EHandGesture::ThumbsUp:   Headline = TEXT("THUMBS UP");   break;
			case EHandGesture::Peace:      Headline = TEXT("PEACE");       break;
			case EHandGesture::FingerGuns: Headline = TEXT("FINGER GUNS"); break;
			case EHandGesture::RockOn:     Headline = TEXT("ROCK ON");     break;
			case EHandGesture::CallMe:     Headline = TEXT("CALL ME");     break;
			default: break;
		}
	}

	if (Headline.IsEmpty() && !bDebugShowFingerStates)
	{
		return FString();
	}

	if (Headline.IsEmpty())
	{
		Headline = TEXT("...");
	}

	if (!bDebugShowFingerStates)
	{
		return Headline;
	}

	// Per-finger extension-ratio strip. Each number is the live ratio * 100.
	// If calibration has run, the bracketed number after each letter is that
	// finger's personalized extended threshold; otherwise the trailing
	// (>=N ext) shows the global fallback.
	const float Ratios[5] = {
		ComputeFingerExtensionRatio(EHandKeypoint::ThumbTip),
		ComputeFingerExtensionRatio(EHandKeypoint::IndexTip),
		ComputeFingerExtensionRatio(EHandKeypoint::MiddleTip),
		ComputeFingerExtensionRatio(EHandKeypoint::RingTip),
		ComputeFingerExtensionRatio(EHandKeypoint::LittleTip),
	};
	const TCHAR Letters[5] = { TEXT('T'), TEXT('I'), TEXT('M'), TEXT('R'), TEXT('L') };

	if (CalibratedThresholds.Num() == 5 && CalibratedThresholds[1] > 0.0f)
	{
		// Calibrated: show each finger's live ratio with its personalized threshold.
		FString Strip;
		for (int32 i = 0; i < 5; ++i)
		{
			Strip += FString::Printf(TEXT("%c%d/%d "),
				Letters[i],
				FMath::FloorToInt(Ratios[i] * 100.0f),
				FMath::FloorToInt(CalibratedThresholds[i] * 100.0f));
		}
		return FString::Printf(TEXT("%s\n%s"), *Headline, *Strip.TrimEnd());
	}

	const int32 ThreshP = FMath::FloorToInt(FingerExtendedRatioThreshold * 100.0f);
	return FString::Printf(TEXT("%s\nT%d I%d M%d R%d L%d  (>=%d ext)"),
		*Headline,
		FMath::FloorToInt(Ratios[0] * 100.0f),
		FMath::FloorToInt(Ratios[1] * 100.0f),
		FMath::FloorToInt(Ratios[2] * 100.0f),
		FMath::FloorToInt(Ratios[3] * 100.0f),
		FMath::FloorToInt(Ratios[4] * 100.0f),
		ThreshP);
}

void UHandTrackingComponent::UpdateGestureLabel()
{
#if ENABLE_DRAW_DEBUG
	if (!bIsTracking)
	{
		return;
	}

	const FString Text = BuildLabelText();
	if (Text.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// DrawDebugString draws screen-space text at a world position — auto
	// billboard, auto camera-face, and routes through the debug-primitive
	// pipeline which renders reliably on the visionOS forward path where
	// UTextRenderComponent's static-glyph material did not.
	const FTransform PalmTransform = GetKeypointWorldTransform(EHandKeypoint::Palm);
	const FVector LabelLoc = PalmTransform.GetLocation() + FVector(0.0f, 0.0f, LabelHeightAbovePalmCm);
	const float FontScale = FMath::Max(0.5f, LabelWorldSizeCm / 3.0f);
	DrawDebugString(World, LabelLoc, Text, /*BaseActor=*/nullptr, LabelColor, /*Duration=*/0.0f, /*bDrawShadow=*/true, FontScale);
#endif
}
