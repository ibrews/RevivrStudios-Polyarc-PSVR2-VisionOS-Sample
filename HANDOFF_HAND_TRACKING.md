# Handoff — OpenXR hand tracking on Apple Vision Pro (PSVR2 sample)

**For:** A fresh Claude Code session picking up this work.
**Updated:** 2026-05-23, v24. Replaces the 2026-05-21 and 2026-05-23/v20 handoffs (rebased onto current code state).

## TL;DR — solved

OpenXR hand tracking is now driving `SKM_MannyXR_left/_right` skeletal-mesh
hand bones correctly on Apple Vision Pro through the Polyarc UE 5.6 fork +
the `OpenXRVisionOS` plugin. Open palm, fist, pinches, and per-finger
gestures all render anatomically. The PSVR2 controller-attached mannequins
are hidden so only the tracking-driven hands are visible.

The empirical bone-driving formula (after 24 iterations) is documented in
`~/knowledge/intelligence/techniques/openxr-to-manny-xr-hand-conversion.md`.
Short version:

```cpp
// Finger bones:
FRotationMatrix::MakeFromZX(-AimDirection, WristXform.GetRotation().GetForwardVector())

// Wrist bone correction (apply before driving children):
WristAdjustRotation = FRotator(0, 0, 90)   // Roll +90 around bone-along axis
```

## Components (all wired into `/Game/VRTemplate/Blueprints/VRPawn`)

| Component | Source file | Status |
|---|---|---|
| `UHandTrackingComponent` (×2, L+R) | `Source/My_Project/HandTrackingComponent.{h,cpp}` | Works. Calibration, gesture detection, 52 sphere markers, billboarded debug label all functional. |
| `UHandSkeletalDriverComponent` (×2, L+R) | `Source/My_Project/HandSkeletalDriverComponent.{h,cpp}` | Works. Single Poseable Manny-XR mesh per side, bones driven from OpenXR keypoints, anatomically correct. PSVR2 mannequin hiding works. |
| `UHMDStatusSubsystem` | `Source/My_Project/HMDStatusSubsystem.{h,cpp}` | Works. Hooks app-lifecycle events and force-exits on background to fix the visionOS "purgatory" zombie-process issue. |

### What's wired into VRPawn

Via ECABridge edits to `/Game/VRTemplate/Blueprints/VRPawn`:

```
VRPawn
├── (existing) Camera, MotionControllers, WidgetInteractions, TeleportTraceNiagaraSystem
├── (existing) HandLeft, HandRight  → B_MannequinsXR_C, controller-attached (hidden at runtime)
├── (added)    HandTrackingLeft         → UHandTrackingComponent, bIsRight=false
├── (added)    HandTrackingRight        → UHandTrackingComponent, bIsRight=true
├── (added)    HandSkeletalDriverLeft   → UHandSkeletalDriverComponent, bIsRight=false
└── (added)    HandSkeletalDriverRight  → UHandSkeletalDriverComponent, bIsRight=true
```

## Iteration arc (passes 18 → 24)

| Pass | Finding | Locked in |
|---|---|---|
| v18–v19 | Direct OXR joint quat, plus `MakeFromX/XY/XZ` variants — all visibly broken | — |
| v20 | Cycled MakeFromX vs Y vs Z; **strategy E (`MakeFromZ(aim)`) won** → bone-along is +Z, not +X | Z is the bone axis |
| v21 | Cycled `+aim` vs `-aim`; **B (`MakeFromZ(-aim)`) won** → bone expects joint→parent direction | Direction is reversed |
| v22 | Added two-sided wireframe debug material; cycled 5 twist-lock variants on `-aim` baseline; **B (`MakeFromZX(-aim, wristForward)`) won** | Finger formula locked |
| v23 | Cycled 6 wrist 90° corrections with fingers locked; **F (`Roll +90`) won** — wrist's cylindrical cuff then pointed toward the elbow | Wrist correction locked |
| v24 | Defaults updated, test rig disabled, single mesh per side | Ship |

## How to test on device

```bash
cd /Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample
xcrun devicectl list devices | grep "Agile Alex"
# Should report: available (paired) or connected

# Confirm latest archive
ls -la Archive/VisionOS/My_Project.app/My_Project

# Package + install (ECABridge auto-skipped via SupportedTargetPlatforms in .uproject)
/Users/Shared/GH/UnrealEngineVisionOS/Engine/Build/BatchFiles/RunUAT.sh \
    BuildCookRun \
    -project=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/My_Project.uproject \
    -platform=VisionOS -build -cook -stage -pak -package -archive \
    -archivedirectory=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/Archive \
    -clientconfig=Development

xcrun devicectl device install app \
    --device 2642855C-6B73-5D5B-9387-6B110E7A7CF3 \
    /Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/Archive/VisionOS/My_Project.app
```

On launch, the calibration prompt reads `[v24] CALIBRATE: MAKE A TIGHT FIST`.
The `[vN]` prefix is bumped each pass to verify a fresh bundle loaded (visionOS
sometimes serves cached bundles).

## Build / install pipeline reference

- **ECABridge: no more toggle dance.** `My_Project.uproject` now has
  `SupportedTargetPlatforms: [Mac, Win64, Linux]` on the ECABridge plugin
  entry, so VisionOS packaging silently skips the plugin. The Mac editor
  still loads it for content authoring.
- **GameFeatureData asset manager rule** is in `Config/DefaultGame.ini`.
  Don't remove — required because ECABridge transitively enables
  GameFeatures, which logs a fatal Error during cook without the rule.
  See `~/knowledge/intelligence/techniques/ecabridge-gamefeatures-cook-error.md`.
- **ECABridge port for this project: 3020.** Use `eca --list` to confirm
  before issuing commands; port 3000 is the Spellrot project.
- **Build time:** ~90s incremental for VisionOS Development archive.
- **Install time:** ~30s via devicectl over USB-C / Wi-Fi tunnel.

## Hard gotchas — don't re-discover these

1. **`xr.OpenXRExitAppOnRuntimeDrivenSessionExit` doesn't fix crown-exit
   purgatory on visionOS.** The lifecycle hook in `HMDStatusSubsystem` is
   the working fix. Leave it alone.
2. **`UTextRenderComponent` doesn't render on visionOS** with the default
   text material. Use `DrawDebugString` instead.
3. **`UPoseableMeshComponent::SetSkeletalMesh` is deprecated in UE 5.6** —
   use `SetSkinnedAssetAndUpdate` (already done in our code).
4. **`ConstructorHelpers::FObjectFinder` is constructor-only.** Using it in
   a runtime function triggers `CheckIfIsInConstructor` and fatal-aborts.
   See `~/knowledge/intelligence/techniques/ue-constructor-helpers-runtime-crash.md`
5. **UAT BuildCookRun: always pass absolute `-project=` path.** Don't use
   `$(pwd)/...` — shell cwd may have drifted from earlier `cd` calls. Bit
   us in the v22 cycle. See `~/knowledge/intelligence/techniques/uat-absolute-project-path.md`.
6. **Verify UAT exit code from the LOG, not the wrapper's `echo $?`.** The
   wrapper exits 0 even when UAT fails. Three-check verification (exit code
   + BUILD marker + archive freshness) before any install. See
   `~/knowledge/intelligence/techniques/verify-build-before-installing.md`.
7. **Polyarc UE 5.6 + Xcode 26.5 needs the MacToolChain patch.** Already
   applied. See `~/knowledge/intelligence/techniques/polyarc-ue56-xcode-265-compile.md`.
8. **`gtimeout` doesn't exist on macOS by default.** Don't rely on it in
   scripts; use shell `& sleep; kill` patterns.
9. **visionOS Info.plist privacy strings already present**:
   `NSHandsTrackingUsageDescription`, `NSWorldSensingUsageDescription`,
   `NSAccessoryTrackingUsageDescription`. Don't second-guess permission
   issues without checking these first.
10. **`xcrun devicectl device install` can fail with "Connection
    interrupted"** mid-install if the AVP sleeps. Retry usually works.
    Wait a few seconds for the tunnel to re-establish before retrying.

## Next directions (post-v24)

The bone rig works. Reasonable next moves, in increasing scope:

1. **Clean up debug visualization.** Wireframe two-sided material is on by
   default (`bUseDebugMaterial=true`). Flip to false to use the regular
   Manny-XR material, or ship a custom unlit two-sided material in
   `Content/HandTracking/M_HandDebug_TwoSided` via ECABridge once the
   editor's open.
2. **Remove the `[vN]` calibration prompt prefix.** Was for stale-bundle
   detection during iteration. No longer needed.
3. **Remove the test rig code** (or keep it). It's well-isolated behind
   `bMultiStrategyTest=false`. If you swap meshes (MetaHuman, Mixamo,
   custom), flip it on and the EWristAdjustTest cycler will help you find
   the new rig's correction.
4. **Validate gestures with real input mappings.** Gesture detection in
   `HandTrackingComponent` produces ENUMS for OpenPalm, Fist, ThumbsUp,
   Peace, FingerGuns, RockOn, CallMe, IndexPinch, MiddlePinch, PinkyPinch.
   Wire these to actual gameplay input.
5. **Validate on different hand sizes.** Calibration captures ratios per
   user; verify it works on small hands and large hands without
   recalibration drift.

## Files added / modified across the whole effort

```
Source/My_Project/
├── HandTrackingComponent.{h,cpp}             (new)
├── HandSkeletalDriverComponent.{h,cpp}       (new)
├── HMDStatusSubsystem.{h,cpp}                (modified — added app-lifecycle hooks)
└── My_Project.Build.cs                       (modified — added HeadMountedDisplay dep)

Config/
├── DefaultGame.ini                           (modified — added GameFeatureData asset manager rule)
└── VisionOS/VisionOSEngine.ini               (modified — flipped xr.OpenXRExitApp CVar)

Content/VRTemplate/Blueprints/VRPawn.uasset   (added 4 components: 2 HandTracking + 2 HandSkeletalDriver)

Plugins/ECABridge/Source/ECABridge/Private/Commands/
└── ECAGameplayTagsCommands.cpp               (LOCAL: 5.7→5.6 RenameTagInINI 2-arg compat;
                                                upstream main has a better version-gated fix)

My_Project.uproject                           (modified — ECABridge SupportedTargetPlatforms: [Mac, Win64, Linux])
```

## KB entries written across this work

- `intelligence/techniques/openxr-to-manny-xr-hand-conversion.md` — the full solution
- `intelligence/techniques/ue-constructor-helpers-runtime-crash.md`
- `intelligence/techniques/ue-visionos-crown-exit-purgatory.md`
- `intelligence/techniques/uat-absolute-project-path.md`
- `intelligence/techniques/ecabridge-gamefeatures-cook-error.md`
- `intelligence/techniques/verify-build-before-installing.md`
