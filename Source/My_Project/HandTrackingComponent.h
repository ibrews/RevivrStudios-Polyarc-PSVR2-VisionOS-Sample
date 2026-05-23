// Phase 1 OpenXR hand-tracking visualizer.
//
// Why a SceneComponent (not USkeletalMeshComponent): Phase 1 visualizes the 26
// per-hand OpenXR keypoints as instanced static-mesh spheres so the result is
// usable without a hand skeletal mesh asset. Phase 2 will add a rigged-mesh
// driver as a separate component (or sibling subclass) once SK_Hand_left
// import is resolved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HeadMountedDisplayTypes.h"
#include "InputCoreTypes.h"
#include "HandTrackingComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EHandTrackingSide : uint8
{
	Left  UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right")
};

// Curl-pattern gestures. Each gesture maps to a specific (thumb, index,
// middle, ring, little) extended/curled signature. Detection runs after the
// per-frame keypoint sweep and is debounced for 100 ms of pose stability.
UENUM(BlueprintType)
enum class EHandGesture : uint8
{
	None       UMETA(DisplayName = "None"),
	OpenPalm   UMETA(DisplayName = "Open Palm"),
	Fist       UMETA(DisplayName = "Fist"),
	ThumbsUp   UMETA(DisplayName = "Thumbs Up"),
	Peace      UMETA(DisplayName = "Peace"),
	FingerGuns UMETA(DisplayName = "Finger Guns"),
	RockOn     UMETA(DisplayName = "Rock On"),
	CallMe     UMETA(DisplayName = "Call Me")
};

// Per-user calibration walks the user through two reference poses and
// captures the per-finger extension ratio at each, then sets a per-finger
// classification threshold at the midpoint. Adapts to hand size and
// individual joint flexibility without an absolute hand-coded threshold.
UENUM(BlueprintType)
enum class EHandCalibrationState : uint8
{
	Uncalibrated UMETA(DisplayName = "Uncalibrated"),
	AwaitingFist UMETA(DisplayName = "Awaiting Fist"),
	AwaitingOpen UMETA(DisplayName = "Awaiting Open Hand"),
	Calibrated   UMETA(DisplayName = "Calibrated")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHandTrackingPinchEvent, EHandTrackingSide, Side);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHandTrackingGestureEvent, EHandGesture, Gesture, EHandTrackingSide, Side);

// NOTE: We expose handedness as a bool (`bIsRight`) rather than an
// `EHandTrackingSide` UPROPERTY because the editor automation surface we use
// to wire components into Blueprints can mutate bools generically but cannot
// set EnumProperty values. The derived `GetSide()` keeps the enum-based API
// for delegate consumers and Blueprint readability.

UCLASS(ClassGroup = (VR), meta = (BlueprintSpawnableComponent), DisplayName = "Hand Tracking (OpenXR)")
class MY_PROJECT_API UHandTrackingComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UHandTrackingComponent();

	// Which hand this component represents. False = left, true = right.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking", meta = (DisplayName = "Is Right Hand"))
	bool bIsRight = false;

	UFUNCTION(BlueprintPure, Category = "Hand Tracking")
	EHandTrackingSide GetSide() const { return bIsRight ? EHandTrackingSide::Right : EHandTrackingSide::Left; }

	// Sphere/marker mesh used to visualize each of the 26 OpenXR keypoints.
	// Leave null to fall back to the engine's BasicShape Sphere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Visualization")
	TObjectPtr<UStaticMesh> JointMesh = nullptr;

	// Uniform scale applied to each joint marker. The default 0.012 (1.2 cm)
	// reads as a small fingertip dot on the visionOS passthrough mix.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Visualization", meta = (ClampMin = "0.001"))
	float JointMeshScale = 0.012f;

	// Hide the markers when tracking is invalid (recommended) instead of
	// freezing the last-known pose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Visualization")
	bool bHideMarkersWhenUntracked = true;

	// Show the 26-per-hand joint sphere markers at all. Defaults off now
	// that the skeletal-mesh driver provides the primary visualization.
	// Keypoint sampling still runs (gesture detection needs it).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Visualization")
	bool bShowJointMarkers = false;

	// Pinch threshold in centimeters between ThumbTip and IndexTip. The
	// OpenXR ecosystem typically lands pinch around 2–3 cm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float PinchThresholdCm = 3.0f;

	// Small hysteresis: once pinching, fingertips must separate by this much
	// beyond the threshold to release. Prevents jittery pinch on/off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PinchReleaseHysteresisCm = 1.0f;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingPinchEvent OnPinchStarted;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingPinchEvent OnPinchEnded;

	// True between an OnPinchStarted and the matching OnPinchEnded.
	UPROPERTY(BlueprintReadOnly, Category = "Hand Tracking|Gestures")
	bool bIsPinching = false;

	// Spawn a tiny dot at the pinch midpoint when thumb+index meet.
	// Disabled by default — the gesture label above the hand is the primary
	// feedback channel now.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures")
	bool bSpawnDotOnIndexPinch = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.2", ClampMax = "10.0"))
	float IndexPinchDotRadiusCm = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float IndexPinchDotDurationSec = 0.35f;

	// --- Middle-thumb gesture ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MiddlePinchThresholdCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float MiddlePinchReleaseHysteresisCm = 1.0f;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingPinchEvent OnMiddlePinchStarted;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingPinchEvent OnMiddlePinchEnded;

	UPROPERTY(BlueprintReadOnly, Category = "Hand Tracking|Gestures")
	bool bIsMiddlePinching = false;

	// --- Pinky-thumb gesture ---

	// Distance in cm between ThumbTip and LittleTip that fires a pinky-pinch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float PinkyPinchThresholdCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float PinkyPinchReleaseHysteresisCm = 1.0f;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingPinchEvent OnPinkyPinchStarted;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingPinchEvent OnPinkyPinchEnded;

	UPROPERTY(BlueprintReadOnly, Category = "Hand Tracking|Gestures")
	bool bIsPinkyPinching = false;

	// --- Built-in explosion on pinky pinch ---

	// When true, the component spawns a transient sphere actor at the
	// midpoint between thumb tip and pinky tip on OnPinkyPinchStarted.
	// Disabled by default — see the gesture label.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Explosion")
	bool bSpawnExplosionOnPinkyPinch = false;

	// Radius of the spawned explosion sphere in centimeters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Explosion", meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float ExplosionRadiusCm = 5.0f;

	// How long the explosion sphere lingers before it auto-destroys.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Explosion", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float ExplosionDurationSec = 0.5f;

	// Optional override mesh for the explosion. If null, falls back to the
	// engine BasicShape sphere used for joint markers.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Explosion")
	TObjectPtr<UStaticMesh> ExplosionMesh = nullptr;

	// --- Curl-pattern gestures (open palm / fist / peace / finger guns / rock on / call me) ---

	// Master toggle for the gesture-detection pass. Turn off if you only
	// care about thumb-index / thumb-pinky pinches.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures")
	bool bDetectCurlGestures = true;

	// Fallback ratio threshold used when no per-user calibration has been
	// captured. Once Calibrate() succeeds, per-finger calibrated thresholds
	// override this value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.5", ClampMax = "0.99"))
	float FingerExtendedRatioThreshold = 0.92f;

	// --- Calibration ---

	// On BeginPlay, immediately enter AwaitingFist so the first thing the
	// user does is calibrate. Turn off if you prefer to call StartCalibration()
	// from BP at a later moment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Calibration")
	bool bAutoCalibrateOnStart = true;

	// User must hold the requested pose with stable per-finger ratios for at
	// least this many seconds before the capture advances. Combined with the
	// stability max-range check, this prevents capturing during transitions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Calibration", meta = (ClampMin = "0.2", ClampMax = "5.0"))
	float CalibrationStabilityRequiredSec = 1.0f;

	// All 5 finger ratios must vary by less than this range across the
	// rolling window for "stable" to be true. 0.05 ≈ 5 percentage points.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Calibration", meta = (ClampMin = "0.01", ClampMax = "0.3"))
	float CalibrationStabilityMaxRange = 0.05f;

	// Sanity gate for Fist capture: every finger ratio must be at or below
	// this value (i.e., genuinely curled). Stops a "weak fist" from poisoning
	// the calibration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Calibration", meta = (ClampMin = "0.5", ClampMax = "0.95"))
	float CalibrationFistMaxRatio = 0.85f;

	// Sanity gate for Open Hand: at least this many fingers must be above
	// CalibrationOpenMinRatio. The thumb often can't straighten as fully as
	// the four fingers, so we don't require all 5.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Calibration", meta = (ClampMin = "3", ClampMax = "5"))
	int32 CalibrationOpenMinFingerCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Calibration", meta = (ClampMin = "0.6", ClampMax = "0.99"))
	float CalibrationOpenMinRatio = 0.78f;

	// Current calibration phase (BP read-only).
	UPROPERTY(BlueprintReadOnly, Category = "Hand Tracking|Calibration")
	EHandCalibrationState CalibrationState = EHandCalibrationState::Uncalibrated;

	// Per-finger thresholds populated after successful calibration. Index 0..4
	// maps to thumb / index / middle / ring / little.
	UPROPERTY(BlueprintReadOnly, Category = "Hand Tracking|Calibration")
	TArray<float> CalibratedThresholds;

	// Restart calibration from scratch (clears captured fist/open data).
	UFUNCTION(BlueprintCallable, Category = "Hand Tracking|Calibration")
	void StartCalibration();

	// A detected gesture must hold this long before it commits (filters
	// out transitions like fist→peace where you briefly look like None).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float GestureStabilityWindowSec = 0.15f;

	// Minimum match score (0–1) a gesture's fingerprint must reach before
	// it can be classified. Lower = more permissive (gestures fire even
	// when ratios are noisy), higher = stricter (need a textbook pose).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinGestureConfidence = 0.40f;

	// The winning gesture must beat the runner-up by at least this margin.
	// Prevents flashing between two gestures whose confidences are close.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float MinGestureConfidenceMargin = 0.05f;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingGestureEvent OnGestureStarted;

	UPROPERTY(BlueprintAssignable, Category = "Hand Tracking|Gestures")
	FHandTrackingGestureEvent OnGestureEnded;

	UPROPERTY(BlueprintReadOnly, Category = "Hand Tracking|Gestures")
	EHandGesture ActiveGesture = EHandGesture::None;

	// When true the component spawns a distinct multi-sphere visual at the
	// palm location on each new gesture. Disabled by default — labels above
	// the hand replace the shapes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Gestures")
	bool bSpawnShapesOnGestures = false;

	// --- Floating gesture label (billboarded text above the hand) ---

	// Render a text label above the hand showing the currently-detected
	// gesture / pinch state. The label faces the player's camera.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Label")
	bool bShowGestureLabel = true;

	// Centimeters above the palm to position the label.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Label", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float LabelHeightAbovePalmCm = 12.0f;

	// World-space size of the label glyphs in centimeters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Label", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float LabelWorldSizeCm = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Label")
	FColor LabelColor = FColor(255, 255, 255, 255);

	// When true, append a compact "[TIMRL]" finger-extension strip below the
	// gesture name (uppercase letter = extended, dot = curled). Useful for
	// dialing FingerExtendedRatioThreshold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Tracking|Label")
	bool bDebugShowFingerStates = true;

	// Last known world-space transform for the named keypoint. Returns
	// identity if tracking is invalid or the keypoint isn't available.
	UFUNCTION(BlueprintCallable, Category = "Hand Tracking")
	FTransform GetKeypointWorldTransform(EHandKeypoint Keypoint) const;

	// True when the underlying IHandTracker reports a valid state this frame.
	UFUNCTION(BlueprintCallable, Category = "Hand Tracking")
	bool IsTracking() const { return bIsTracking; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> JointInstances = nullptr;

	// Resolved once in the constructor — ConstructorHelpers::FObjectFinder is
	// the only context where it is legal to call. Holding a strong reference
	// here keeps the lookup off the runtime hot path.
	UPROPERTY()
	TObjectPtr<UStaticMesh> DefaultJointMesh = nullptr;


	bool bIsTracking = false;

	// Cached world-space transforms keyed by EHandKeypoint index.
	TArray<FTransform> CachedKeypoints;

	void EnsureInstancesInitialized();
	UStaticMesh* ResolveJointMesh() const;
	EControllerHand ResolveControllerHand() const;
	void UpdatePinchState(const FTransform& ThumbTipWorld, const FTransform& IndexTipWorld);
	void UpdateMiddlePinchState(const FTransform& ThumbTipWorld, const FTransform& MiddleTipWorld);
	void UpdatePinkyPinchState(const FTransform& ThumbTipWorld, const FTransform& PinkyTipWorld);
	void SpawnExplosion(const FVector& WorldLocation);

	// Gesture detection
	float ComputeFingerExtensionRatio(EHandKeypoint TipKey) const;
	float NormalizeFingerRatio(int32 FingerIdx, float Ratio) const;
	bool IsFingerExtended(EHandKeypoint TipKey) const;
	EHandGesture ClassifyGesture(bool bThumb, bool bIndex, bool bMiddle, bool bRing, bool bLittle) const;
	EHandGesture ClassifyGestureByConfidence(const float NormalizedRatios[5], float& OutTopConfidence) const;
	void UpdateGestureState(float DeltaTime);
	void SpawnGestureVisual(EHandGesture Gesture, const FTransform& PalmTransform);
	void SpawnTransientSphere(const FVector& WorldLocation, float RadiusCm, float DurationSec);

	// Label management
	void EnsureLabelInitialized();
	void UpdateGestureLabel();
	FString BuildLabelText() const;

	// Calibration
	void UpdateCalibrationState(float DeltaTime);
	bool AreRatiosStable() const;
	void CaptureCurrentRatios(TArray<float>& OutRatios) const;
	int32 FingerIndexFromTipKey(EHandKeypoint TipKey) const;

	// Gesture stability tracking
	EHandGesture PendingGesture = EHandGesture::None;
	float PendingGestureStableFor = 0.0f;

	// Per-instance calibration tracking. The phase + captured ratios are
	// stored at file-scope in the .cpp so both hands stay in sync. Each
	// component keeps its own stability ring buffer (independent finger
	// motion can stabilize at different times per hand).
	TArray<TArray<float>> CalibrationStabilityWindow; // [finger][frame] last N samples
	float CalibrationCurrentStableSec = 0.0f;
	static constexpr int32 CalibrationStabilityWindowSize = 30;
};
