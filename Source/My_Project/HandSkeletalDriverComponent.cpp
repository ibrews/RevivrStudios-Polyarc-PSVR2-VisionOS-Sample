#include "HandSkeletalDriverComponent.h"

#include "Components/ChildActorComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// V26: distal joints are the last bones before a Tip in each finger
	// chain. Only these get explicit Z scaling (see DrivePoseableWithStrategy
	// for why). Adding new keypoint enum values? Update this list too.
	bool IsDistalKeypoint(EHandKeypoint Kp)
	{
		return Kp == EHandKeypoint::ThumbDistal
			|| Kp == EHandKeypoint::IndexDistal
			|| Kp == EHandKeypoint::MiddleDistal
			|| Kp == EHandKeypoint::RingDistal
			|| Kp == EHandKeypoint::LittleDistal;
	}

	// V27: metacarpal joints are the first bone in each finger chain (the
	// "palm" bones). World-driving them from live OpenXR keypoints distorts
	// the palm geometry and breaks thenar webbing. Default behavior is to
	// skip them entirely so they inherit bind-relative transform from the
	// wrist via the skeleton hierarchy.
	bool IsMetacarpalKeypoint(EHandKeypoint Kp)
	{
		return Kp == EHandKeypoint::ThumbMetacarpal
			|| Kp == EHandKeypoint::IndexMetacarpal
			|| Kp == EHandKeypoint::MiddleMetacarpal
			|| Kp == EHandKeypoint::RingMetacarpal
			|| Kp == EHandKeypoint::LittleMetacarpal;
	}

	// V28: thumb chain keypoints (metacarpal + proximal + distal + tip).
	// Used by the thumb test rig to apply per-strategy overrides only to
	// thumb bones, leaving the other fingers driven normally.
	bool IsThumbKeypoint(EHandKeypoint Kp)
	{
		return Kp == EHandKeypoint::ThumbMetacarpal
			|| Kp == EHandKeypoint::ThumbProximal
			|| Kp == EHandKeypoint::ThumbDistal
			|| Kp == EHandKeypoint::ThumbTip;
	}

	// V28: per-ghost thumb-strategy slot table (the A-F cycle order).
	EThumbStrategyTest GetThumbStrategyForGhost(int32 GhostIndex)
	{
		switch (GhostIndex)
		{
			case 0: return EThumbStrategyTest::SkipThumb01;
			case 1: return EThumbStrategyTest::DriveAllNormal;
			case 2: return EThumbStrategyTest::ScaleDown;
			case 3: return EThumbStrategyTest::PullTowardWrist15;
			case 4: return EThumbStrategyTest::PullTowardWrist30;
			case 5: return EThumbStrategyTest::SkipAllThumb;
		}
		return EThumbStrategyTest::SkipThumb01;
	}

	// V28: short label for on-device readout.
	const TCHAR* GetThumbStrategyLetter(EThumbStrategyTest S)
	{
		switch (S)
		{
			case EThumbStrategyTest::SkipThumb01:        return TEXT("A: skip thumb_01");
			case EThumbStrategyTest::DriveAllNormal:     return TEXT("B: drive all");
			case EThumbStrategyTest::ScaleDown:          return TEXT("C: 0.8x scale");
			case EThumbStrategyTest::PullTowardWrist15:  return TEXT("D: pull 1.5cm");
			case EThumbStrategyTest::PullTowardWrist30:  return TEXT("E: pull 3cm");
			case EThumbStrategyTest::SkipAllThumb:       return TEXT("F: skip all");
		}
		return TEXT("?");
	}

	IHandTracker* FindSkeletalHandTracker()
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
			if (Tracker)
			{
				return Tracker;
			}
		}
		return nullptr;
	}
}

UHandSkeletalDriverComponent::UHandSkeletalDriverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	// Resolve project-shipped Manny-XR hand meshes here (ConstructorHelpers
	// is only legal inside constructors). The PSVR2 sample already
	// references these via the controller-attached HandLeft/HandRight, so
	// they're guaranteed to be in the cooked package.
	static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> LeftFinder(
		TEXT("/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_left.SKM_MannyXR_left"));
	DefaultLeftMesh = LeftFinder.Get();

	static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> RightFinder(
		TEXT("/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_right.SKM_MannyXR_right"));
	DefaultRightMesh = RightFinder.Get();

	// Engine WireframeMaterial is intrinsically two-sided (wireframe has no
	// winding), so it lets us see the back of the hand even if the mesh is
	// rendered inside-out from a coordinate-frame flip. It's also extremely
	// diagnostic — you can see every triangle and bone clearly. Once
	// rotation math is settled and we want a solid look, replace this with
	// a custom unlit two-sided material asset (set bIsTwoSided=true on a
	// new UMaterial in the editor or via ECABridge).
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> DebugMatFinder(
		TEXT("/Engine/EngineDebugMaterials/WireframeMaterial.WireframeMaterial"));
	DebugMaterialOverride = DebugMatFinder.Get();

	// Walnut skin for the hands in the wooded level (CDO ref → always cooked).
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> WalnutFinder(
		TEXT("/Game/StarterContent/Materials/M_Wood_Walnut.M_Wood_Walnut"));
	WoodedLevelHandMaterial = WalnutFinder.Get();
}

void UHandSkeletalDriverComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsurePoseablesInitialized();

	if (bHidePsvr2ControllerHandsAtStart)
	{
		HidePsvr2ControllerHands();
	}
}

int32 UHandSkeletalDriverComponent::NumTestStrategies()
{
	return 6;
}

void UHandSkeletalDriverComponent::SetHandMeshHidden(bool bHidden)
{
	bForceHidden = bHidden;
	for (UPoseableMeshComponent* P : Poseables)
	{
		if (P) { P->SetVisibility(!bHidden); }
	}
}

bool UHandSkeletalDriverComponent::IsWoodedLevel() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FString MapName = World->GetMapName();
	MapName.RemoveFromStart(World->StreamingLevelsPrefix);
	return MapName.Equals(WoodedLevelName, ESearchCase::IgnoreCase);
}

EBoneRotationStrategy UHandSkeletalDriverComponent::GetStrategyForGhost(int32 GhostIndex)
{
	// V23: finger strategy is locked across all ghosts. The cycling now
	// varies the wrist adjustment (see GetWristAdjustForGhost) so we can
	// isolate the wrist's 90°-correction question without disturbing the
	// proven finger formula.
	return EBoneRotationStrategy::AimZX_Rev_WristFwd;
}

const TCHAR* UHandSkeletalDriverComponent::GetStrategyLetter(EBoneRotationStrategy S)
{
	return TEXT("B-locked");
}

// V23 wrist-adjust test rig: the cycling slot index now selects a
// per-ghost wrist correction. Match the order to the enum so labels
// stay readable.
static EWristAdjustTest GetWristAdjustForGhost(int32 GhostIndex)
{
	switch (GhostIndex)
	{
		case 0: return EWristAdjustTest::None;
		case 1: return EWristAdjustTest::PitchPlus;
		case 2: return EWristAdjustTest::PitchMinus;
		case 3: return EWristAdjustTest::YawPlus;
		case 4: return EWristAdjustTest::YawMinus;
		case 5: return EWristAdjustTest::RollPlus;
	}
	return EWristAdjustTest::None;
}

static const TCHAR* GetWristAdjustLetter(EWristAdjustTest W)
{
	switch (W)
	{
		case EWristAdjustTest::None:       return TEXT("A: raw");
		case EWristAdjustTest::PitchPlus:  return TEXT("B: pitch+90");
		case EWristAdjustTest::PitchMinus: return TEXT("C: pitch-90");
		case EWristAdjustTest::YawPlus:    return TEXT("D: yaw+90");
		case EWristAdjustTest::YawMinus:   return TEXT("E: yaw-90");
		case EWristAdjustTest::RollPlus:   return TEXT("F: roll+90");
	}
	return TEXT("?");
}

// FRotator components: Pitch is rotation around Y (right), Yaw around Z (up),
// Roll around X (forward). For a hand whose mesh is "twisted onto itself,"
// Roll variants are the most physically plausible candidates — they spin the
// wrist around its own bone-along axis.
static FQuat ComputeWristAdjustQuat(EWristAdjustTest W)
{
	switch (W)
	{
		case EWristAdjustTest::None:       return FQuat::Identity;
		case EWristAdjustTest::PitchPlus:  return FRotator( 90.0f,   0.0f,   0.0f).Quaternion();
		case EWristAdjustTest::PitchMinus: return FRotator(-90.0f,   0.0f,   0.0f).Quaternion();
		case EWristAdjustTest::YawPlus:    return FRotator(  0.0f,  90.0f,   0.0f).Quaternion();
		case EWristAdjustTest::YawMinus:   return FRotator(  0.0f, -90.0f,   0.0f).Quaternion();
		case EWristAdjustTest::RollPlus:   return FRotator(  0.0f,   0.0f,  90.0f).Quaternion();
	}
	return FQuat::Identity;
}

void UHandSkeletalDriverComponent::CacheBindLengths(UPoseableMeshComponent* P)
{
	// Walk every hand keypoint, resolve to a bone, and record the bind-pose
	// parent-relative offset magnitude. This is the bone's "length" in the
	// canonical sense — the distance from its parent in the reference pose.
	// At runtime we'll compare the live OpenXR joint-to-joint distance
	// against this to derive a per-bone stretch factor.
	if (!P)
	{
		return;
	}
	BindKeypointLengths.Empty();
	const TArray<FTransform>& BoneSpaceXforms = P->GetBoneSpaceTransforms();
	for (int32 i = 0; i < EHandKeypointCount; ++i)
	{
		const EHandKeypoint Kp = static_cast<EHandKeypoint>(i);
		const FName BoneName = ResolveBoneName(Kp);
		if (BoneName.IsNone())
		{
			continue;
		}
		const int32 BoneIdx = P->GetBoneIndex(BoneName);
		if (BoneIdx == INDEX_NONE || BoneIdx >= BoneSpaceXforms.Num())
		{
			continue;
		}
		// Parent-relative offset magnitude = bind-pose distance from parent.
		// For a finger chain this is the bone-along bind length.
		const float BindLen = BoneSpaceXforms[BoneIdx].GetLocation().Size();
		BindKeypointLengths.Add(Kp, BindLen);
	}
}

void UHandSkeletalDriverComponent::HidePsvr2ControllerHands()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The PSVR2 sample's VRPawn ships with `HandLeft` / `HandRight`
	// ChildActorComponents that spawn the B_MannequinsXR_C actor — those
	// produce the "extra static hands at the controller default position"
	// we want gone. Hide them aggressively: hide the component itself,
	// then walk into the spawned child actor and hide every scene
	// component inside. Also use SetHiddenInGame which sticks across
	// ticks (SetVisibility can be re-overridden by component logic).
	TArray<UActorComponent*> AllComps;
	Owner->GetComponents(AllComps);
	for (UActorComponent* Comp : AllComps)
	{
		if (!Comp) { continue; }

		// Skip our own components or the OpenXR-driver hand tracking ones.
		const FString ClassName = Comp->GetClass()->GetName();
		if (ClassName.Contains(TEXT("HandSkeletalDriverComponent"))
			|| ClassName.Contains(TEXT("HandTrackingComponent")))
		{
			continue;
		}

		const FString CompName = Comp->GetName();
		const bool bIsHandComponent =
			CompName.Contains(TEXT("Hand")) &&
			(CompName.Contains(TEXT("Left")) || CompName.Contains(TEXT("Right")));
		if (!bIsHandComponent)
		{
			continue;
		}

		if (USceneComponent* SC = Cast<USceneComponent>(Comp))
		{
			SC->SetVisibility(false, /*bPropagateToChildren=*/true);
			SC->SetHiddenInGame(true, /*bPropagateToChildren=*/true);
		}

		// ChildActorComponent owns a spawned actor — that actor's
		// components don't inherit our SetVisibility/Hidden directly. Drill
		// in and hide each USceneComponent on the spawned actor too.
		if (UChildActorComponent* CAC = Cast<UChildActorComponent>(Comp))
		{
			if (AActor* Child = CAC->GetChildActor())
			{
				Child->SetActorHiddenInGame(true);
				TArray<USceneComponent*> ChildSceneComps;
				Child->GetComponents<USceneComponent>(ChildSceneComps);
				for (USceneComponent* ChildSC : ChildSceneComps)
				{
					if (!ChildSC) { continue; }
					ChildSC->SetVisibility(false, /*bPropagateToChildren=*/true);
					ChildSC->SetHiddenInGame(true, /*bPropagateToChildren=*/true);
				}
			}
		}
	}
}

EHandKeypoint UHandSkeletalDriverComponent::NextKeypointInChain(EHandKeypoint Keypoint) const
{
	// Walk the OpenXR finger chains. Each call returns the next joint
	// outward from the palm; the tips return themselves (no successor).
	switch (Keypoint)
	{
		case EHandKeypoint::ThumbMetacarpal:    return EHandKeypoint::ThumbProximal;
		case EHandKeypoint::ThumbProximal:      return EHandKeypoint::ThumbDistal;
		case EHandKeypoint::ThumbDistal:        return EHandKeypoint::ThumbTip;
		case EHandKeypoint::IndexMetacarpal:    return EHandKeypoint::IndexProximal;
		case EHandKeypoint::IndexProximal:      return EHandKeypoint::IndexIntermediate;
		case EHandKeypoint::IndexIntermediate:  return EHandKeypoint::IndexDistal;
		case EHandKeypoint::IndexDistal:        return EHandKeypoint::IndexTip;
		case EHandKeypoint::MiddleMetacarpal:   return EHandKeypoint::MiddleProximal;
		case EHandKeypoint::MiddleProximal:     return EHandKeypoint::MiddleIntermediate;
		case EHandKeypoint::MiddleIntermediate: return EHandKeypoint::MiddleDistal;
		case EHandKeypoint::MiddleDistal:       return EHandKeypoint::MiddleTip;
		case EHandKeypoint::RingMetacarpal:     return EHandKeypoint::RingProximal;
		case EHandKeypoint::RingProximal:       return EHandKeypoint::RingIntermediate;
		case EHandKeypoint::RingIntermediate:   return EHandKeypoint::RingDistal;
		case EHandKeypoint::RingDistal:         return EHandKeypoint::RingTip;
		case EHandKeypoint::LittleMetacarpal:   return EHandKeypoint::LittleProximal;
		case EHandKeypoint::LittleProximal:     return EHandKeypoint::LittleIntermediate;
		case EHandKeypoint::LittleIntermediate: return EHandKeypoint::LittleDistal;
		case EHandKeypoint::LittleDistal:       return EHandKeypoint::LittleTip;
		default: return Keypoint; // Wrist, Palm, all Tips — no successor
	}
}

void UHandSkeletalDriverComponent::EnsurePoseablesInitialized()
{
	const int32 DesiredCount = bMultiStrategyTest ? NumTestStrategies() : 1;
	if (Poseables.Num() == DesiredCount)
	{
		return;
	}

	// Clean up any prior ones (e.g., bMultiStrategyTest flipped at runtime).
	for (UPoseableMeshComponent* Old : Poseables)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	Poseables.Reset();

	USkeletalMesh* MeshToUse = HandMeshOverride
		? HandMeshOverride.Get()
		: (bIsRight ? DefaultRightMesh.Get() : DefaultLeftMesh.Get());

	for (int32 i = 0; i < DesiredCount; ++i)
	{
		UPoseableMeshComponent* P = NewObject<UPoseableMeshComponent>(GetOwner(), UPoseableMeshComponent::StaticClass(), NAME_None, RF_Transient);
		P->SetupAttachment(this);
		P->SetMobility(EComponentMobility::Movable);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetCastShadow(false);
		P->SetReceivesDecals(false);
		P->bSelectable = false;
		P->SetGenerateOverlapEvents(false);
		if (MeshToUse)
		{
			P->SetSkinnedAssetAndUpdate(MeshToUse, /*bReinitPose=*/true);
			// V25: snapshot bind-pose bone lengths now, while the Poseable's
			// local pose is still the asset's reference pose. Once Tick starts
			// calling SetBoneTransformByName, the local pose reflects our
			// overrides and we lose access to bind-pose values.
			if (BindKeypointLengths.IsEmpty())
			{
				CacheBindLengths(P);
			}
		}
		// Stamp debug material across every slot. Must happen AFTER
		// SetSkinnedAssetAndUpdate (slots are sized from the asset). The
		// material itself is two-sided so back-facing geometry stays
		// visible regardless of winding — critical for diagnosing
		// inverted-mesh cases vs. rotation issues.
		if (bUseDebugMaterial && DebugMaterialOverride)
		{
			const int32 NumMats = P->GetNumMaterials();
			for (int32 SlotIdx = 0; SlotIdx < NumMats; ++SlotIdx)
			{
				P->SetMaterial(SlotIdx, DebugMaterialOverride);
			}
		}
		// Wooded level (Stone Courtyard): skin the hands in walnut wood.
		else if (bSwapHandMaterialInWoodedLevel && WoodedLevelHandMaterial && IsWoodedLevel())
		{
			const int32 NumMats = P->GetNumMaterials();
			for (int32 SlotIdx = 0; SlotIdx < NumMats; ++SlotIdx)
			{
				P->SetMaterial(SlotIdx, WoodedLevelHandMaterial);
			}
		}
		P->RegisterComponent();
		P->SetVisibility(true);
		Poseables.Add(P);
	}
}

FName UHandSkeletalDriverComponent::ResolveBoneName(EHandKeypoint Keypoint) const
{
	// UE5 Manny / ManniXR finger-bone naming convention (uses "pinky" not
	// "little"). Thumb has 3 bones (no Intermediate), other fingers have 4
	// (metacarpal + 3 phalanges).
	const TCHAR* Suffix = bIsRight ? TEXT("_r") : TEXT("_l");

	auto MakeName = [Suffix](const TCHAR* BaseName) -> FName
	{
		return FName(*FString::Printf(TEXT("%s%s"), BaseName, Suffix));
	};

	switch (Keypoint)
	{
		case EHandKeypoint::Wrist:                return MakeName(TEXT("hand"));
		case EHandKeypoint::ThumbMetacarpal:      return MakeName(TEXT("thumb_01"));
		case EHandKeypoint::ThumbProximal:        return MakeName(TEXT("thumb_02"));
		case EHandKeypoint::ThumbDistal:          return MakeName(TEXT("thumb_03"));
		case EHandKeypoint::IndexMetacarpal:      return MakeName(TEXT("index_metacarpal"));
		case EHandKeypoint::IndexProximal:        return MakeName(TEXT("index_01"));
		case EHandKeypoint::IndexIntermediate:    return MakeName(TEXT("index_02"));
		case EHandKeypoint::IndexDistal:          return MakeName(TEXT("index_03"));
		case EHandKeypoint::MiddleMetacarpal:     return MakeName(TEXT("middle_metacarpal"));
		case EHandKeypoint::MiddleProximal:       return MakeName(TEXT("middle_01"));
		case EHandKeypoint::MiddleIntermediate:   return MakeName(TEXT("middle_02"));
		case EHandKeypoint::MiddleDistal:         return MakeName(TEXT("middle_03"));
		case EHandKeypoint::RingMetacarpal:       return MakeName(TEXT("ring_metacarpal"));
		case EHandKeypoint::RingProximal:         return MakeName(TEXT("ring_01"));
		case EHandKeypoint::RingIntermediate:     return MakeName(TEXT("ring_02"));
		case EHandKeypoint::RingDistal:           return MakeName(TEXT("ring_03"));
		case EHandKeypoint::LittleMetacarpal:     return MakeName(TEXT("pinky_metacarpal"));
		case EHandKeypoint::LittleProximal:       return MakeName(TEXT("pinky_01"));
		case EHandKeypoint::LittleIntermediate:   return MakeName(TEXT("pinky_02"));
		case EHandKeypoint::LittleDistal:         return MakeName(TEXT("pinky_03"));
		// Tips and Palm have no bones in the Manny-XR rig — they're virtual
		// endpoints. Skipping them is harmless because the Distal bone
		// already determines the fingertip orientation.
		default: return NAME_None;
	}
}

void UHandSkeletalDriverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	EnsurePoseablesInitialized();
	if (bHidePsvr2ControllerHandsEveryTick)
	{
		HidePsvr2ControllerHands();
	}

	IHandTracker* Tracker = FindSkeletalHandTracker();
	const bool bValid = Tracker && Tracker->IsHandTrackingStateValid();

	if (!bValid)
	{
		if (bHideWhenUntracked)
		{
			for (UPoseableMeshComponent* P : Poseables)
			{
				if (P) { P->SetVisibility(false); }
			}
		}
		return;
	}

	// Hidden while this hand is grabbing (set via SetHandMeshHidden) — keep the mesh
	// hidden and skip driving the pose until release.
	if (bForceHidden)
	{
		for (UPoseableMeshComponent* P : Poseables)
		{
			if (P) { P->SetVisibility(false); }
		}
		return;
	}

	const EControllerHand Hand = bIsRight ? EControllerHand::Right : EControllerHand::Left;
	const FQuat WristAdjust = WristAdjustRotation.Quaternion();

	FTransform WristXform;
	float Radius = 0.0f;
	const bool bHaveWrist = Tracker->GetKeypointState(Hand, EHandKeypoint::Wrist, WristXform, Radius);
	if (bHaveWrist && !WristAdjust.IsIdentity())
	{
		WristXform.SetRotation(WristXform.GetRotation() * WristAdjust);
	}

	UWorld* World = GetWorld();

	if (bMultiStrategyTest)
	{
		// Cycle one strategy at a time, world-clock synced so left and
		// right hand components show the same strategy in lock-step. The
		// active ghost overlays the user's actual wrist so they can
		// compare the driven pose directly against their real hand.
		const float WorldTime = World ? World->GetTimeSeconds() : 0.0f;
		const float CycleSec = FMath::Max(MultiStrategyCycleSec, 0.1f);
		const int32 NumStrats = Poseables.Num();
		const int32 ActiveIdx = NumStrats > 0
			? static_cast<int32>(FMath::FloorToInt(WorldTime / CycleSec)) % NumStrats
			: 0;
		const float TimeIntoSlot = FMath::Fmod(WorldTime, CycleSec);
		const float TimeRemaining = CycleSec - TimeIntoSlot;

		for (int32 i = 0; i < Poseables.Num(); ++i)
		{
			UPoseableMeshComponent* P = Poseables[i];
			if (!P) { continue; }

			if (i != ActiveIdx)
			{
				P->SetVisibility(false);
				continue;
			}

			// V28: wrist correction is locked via WristAdjustRotation (applied
			// to WristXform earlier in Tick); the cycling variable is now the
			// per-ghost thumb strategy. Finger formula remains locked too.
			const EBoneRotationStrategy Strat = GetStrategyForGhost(i);
			const EThumbStrategyTest ThumbStrat = GetThumbStrategyForGhost(i);

			if (bHaveWrist)
			{
				P->SetWorldLocationAndRotation(WristXform.GetLocation(), WristXform.GetRotation());
				const FName WristBone = ResolveBoneName(EHandKeypoint::Wrist);
				if (!WristBone.IsNone())
				{
					P->SetBoneTransformByName(WristBone, WristXform, EBoneSpaces::WorldSpace);
				}
			}
			P->SetVisibility(true);
			DrivePoseableWithStrategy(P, Strat, ThumbStrat, WristXform, Tracker, Hand);

#if ENABLE_DRAW_DEBUG
			if (World && bHaveWrist)
			{
				const FVector LabelPos = WristXform.GetLocation() + FVector(0.0f, 0.0f, TestRigHeightAboveWristCm);
				const FString LabelText = FString::Printf(
					TEXT("THUMB %s [%s] %.1fs"),
					GetThumbStrategyLetter(ThumbStrat),
					bIsRight ? TEXT("R") : TEXT("L"),
					TimeRemaining);
				DrawDebugString(World, LabelPos, LabelText, nullptr, FColor::Yellow, 0.0f, true, 2.0f);
			}
#endif
		}
	}
	else
	{
		// Normal mode: single mesh anchored at the wrist, active strategy.
		if (Poseables.Num() == 0) { return; }
		UPoseableMeshComponent* P = Poseables[0];
		if (!P) { return; }

		if (bHaveWrist)
		{
			P->SetWorldLocationAndRotation(WristXform.GetLocation(), WristXform.GetRotation());
			const FName WristBone = ResolveBoneName(EHandKeypoint::Wrist);
			if (!WristBone.IsNone())
			{
				P->SetBoneTransformByName(WristBone, WristXform, EBoneSpaces::WorldSpace);
			}
		}
		P->SetVisibility(true);
		// V29: single-mesh mode uses the configurable thumb strategy. Default
		// is B (DriveAllNormal) per v28 testing — accurate spatial tracking
		// at the cost of "thick thumb" visual, which the thenar_extend morph
		// target compensates for once authored.
		DrivePoseableWithStrategy(P, ActiveStrategy, ActiveThumbStrategy, WristXform, Tracker, Hand);
	}
}

FRotator UHandSkeletalDriverComponent::ComputeBoneRotation(
	EBoneRotationStrategy Strategy,
	const FVector& AimDirection,
	const FTransform& WristXform,
	const FQuat& OXRJointRotation) const
{
	// V23: enum collapsed to the single proven formula (see header). All
	// finger bones use MakeFromZX(-aim, wristForward) — Z is bone-along
	// (joint→parent), X locked to wrist forward for twist. Strategy
	// argument kept in the signature for future extensibility but unused.
	const FVector ReverseAim = -AimDirection;
	const FVector WristForward = WristXform.GetRotation().GetForwardVector();
	return FRotationMatrix::MakeFromZX(ReverseAim, WristForward).Rotator();
}

void UHandSkeletalDriverComponent::DrivePoseableWithStrategy(
	UPoseableMeshComponent* Pose,
	EBoneRotationStrategy Strategy,
	EThumbStrategyTest ThumbStrategy,
	const FTransform& WristXform,
	IHandTracker* Tracker,
	EControllerHand Hand) const
{
	if (!Pose || !Tracker) { return; }

	for (int32 i = 0; i < EHandKeypointCount; ++i)
	{
		const EHandKeypoint Keypoint = static_cast<EHandKeypoint>(i);
		if (Keypoint == EHandKeypoint::Wrist)
		{
			continue;
		}

		const bool bIsThumb = IsThumbKeypoint(Keypoint);

		// V27: skip metacarpals so the palm/thenar-webbing geometry stays
		// at bind shape. V28 carves out the thumb metacarpal — its handling
		// is governed by ThumbStrategy below (the test rig overrides v27's
		// thumb_01 skip per-strategy).
		if (bSkipMetacarpalBones && IsMetacarpalKeypoint(Keypoint) && !bIsThumb)
		{
			continue;
		}

		// V28 thumb test rig: per-strategy skips applied before fetching state.
		if (bIsThumb)
		{
			switch (ThumbStrategy)
			{
				case EThumbStrategyTest::SkipAllThumb:
					continue;
				case EThumbStrategyTest::SkipThumb01:
					if (Keypoint == EHandKeypoint::ThumbMetacarpal) { continue; }
					break;
				default:
					break;  // DriveAllNormal / ScaleDown / Pull* drive every thumb bone
			}
		}

		const FName BoneName = ResolveBoneName(Keypoint);
		if (BoneName.IsNone())
		{
			continue;
		}

		FTransform ThisXform;
		float Radius = 0.0f;
		if (!Tracker->GetKeypointState(Hand, Keypoint, ThisXform, Radius))
		{
			continue;
		}

		// V28 thumb test rig: pull thumb keypoints toward the wrist to reduce
		// abduction (closes the thenar-webbing gap visually). Applied to
		// every thumb bone so the whole chain shifts cohesively.
		if (bIsThumb && (ThumbStrategy == EThumbStrategyTest::PullTowardWrist15
		                || ThumbStrategy == EThumbStrategyTest::PullTowardWrist30))
		{
			const float OffsetCm = (ThumbStrategy == EThumbStrategyTest::PullTowardWrist15) ? 1.5f : 3.0f;
			const FVector ToWrist = (WristXform.GetLocation() - ThisXform.GetLocation()).GetSafeNormal();
			ThisXform.SetLocation(ThisXform.GetLocation() + ToWrist * OffsetCm);
		}

		const EHandKeypoint NextKp = NextKeypointInChain(Keypoint);
		FVector AimDirection = FVector::ForwardVector;
		float ActualSegLen = 0.0f;
		if (NextKp != Keypoint)
		{
			FTransform NextXform;
			if (Tracker->GetKeypointState(Hand, NextKp, NextXform, Radius))
			{
				const FVector Delta = NextXform.GetLocation() - ThisXform.GetLocation();
				ActualSegLen = Delta.Size();
				if (ActualSegLen > KINDA_SMALL_NUMBER)
				{
					AimDirection = Delta / ActualSegLen;
				}
			}
		}

		const FRotator BoneRot = ComputeBoneRotation(Strategy, AimDirection, WristXform, ThisXform.GetRotation());

		// V26: distal-only Z scale. Non-distal bones already render correctly
		// because we WORLD-position every joint — vertices between them
		// interpolate via skinning. Only the mesh PAST the distal bone is
		// skinned ~100% to it and needs an explicit scale to make the
		// rendered fingertip reach the user's actual fingertip. Scaling
		// non-distal bones (v25) pulled thumb thenar-webbing vertices
		// away from the index area and made the thumb look oversized.
		FVector BoneScale = FVector::OneVector;
		if (bScaleBonesToHandSize
			&& ActualSegLen > KINDA_SMALL_NUMBER
			&& IsDistalKeypoint(Keypoint)
			&& BindKeypointLengths.Contains(Keypoint))
		{
			// Bind-pose mesh-past-distal-tip length ≈ distal bone bind length
			// × DistalTipMeshFactor (0.5 default; the mesh tapers past the
			// bone tip and ends about halfway out empirically on Manny-XR).
			const float BindMeshExtension = BindKeypointLengths[Keypoint] * DistalTipMeshFactor;
			if (BindMeshExtension > KINDA_SMALL_NUMBER)
			{
				BoneScale = FVector(1.0f, 1.0f, ActualSegLen / BindMeshExtension);
			}
		}

		// V28 thumb test rig: uniform 0.8x scale on every thumb bone to test
		// "is the thumb visually huge because it's just too big?" Overrides
		// the distal-only Z scale above for thumb keypoints specifically.
		if (bIsThumb && ThumbStrategy == EThumbStrategyTest::ScaleDown)
		{
			BoneScale = FVector(0.8f, 0.8f, 0.8f);
		}

		const FTransform BoneXform(BoneRot, ThisXform.GetLocation(), BoneScale);
		Pose->SetBoneTransformByName(BoneName, BoneXform, EBoneSpaces::WorldSpace);
	}
}
