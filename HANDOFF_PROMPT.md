> 🟢 **CURRENT WORK (2026-06-03) is NOT this file.** The live handoff — "Pinchwork" rename, two
> themed travel levels, pinch-grab v2, keep-in-RAM, forest-hand walnut, app icon, passthrough
> translucency, and the **uncommitted working tree + pending cook batch + arm64-Lightmass engine
> state** — is in **[`claude-progress.md`](./claude-progress.md)** → read the
> "⛔ CURRENT STATE & PENDING BATCH" section FIRST. The OpenXR hand-rig task below is an older,
> separate, RESOLVED work stream (kept for reference).

---

# Session Handoff — OpenXR hand tracking on AVP — RESOLVED

**Session date:** 2026-05-23 (v24)
**Repo:** `/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample`

## Read this first

**Full context lives in [HANDOFF_HAND_TRACKING.md](./HANDOFF_HAND_TRACKING.md)**
in this same directory. That file has: the solved bone formula, the iteration
arc, gotchas, and build/install pipeline.

This file (`HANDOFF_PROMPT.md`) is just the session-resume capsule.

## State

OpenXR hand tracking on Apple Vision Pro is **working end-to-end**. Both
`SKM_MannyXR_left` and `SKM_MannyXR_right` skeletal meshes now deform from
real finger motion. The solution was found empirically across passes
20–24 by cycling bone-rotation formulas and wrist corrections side-by-side
in a 6-ghost test rig.

The final conversion is:

```cpp
// Finger bones:
FRotationMatrix::MakeFromZX(-AimDirection, WristXform.GetRotation().GetForwardVector())

// Wrist bone correction:
WristAdjustRotation = FRotator(0, 0, 90)   // Roll +90 around bone-along axis
```

Why these choices: see
`~/knowledge/intelligence/techniques/openxr-to-manny-xr-hand-conversion.md`.

## What's running on device (v24)

* Calibration prompt: `[v24] CALIBRATE: MAKE A TIGHT FIST`
* After calibration: one Manny-XR mesh per hand (wireframe two-sided
  material by default for diagnostics), correctly oriented and bending
* PSVR2 controller mannequins are hidden
* Gesture detection: OpenPalm, Fist, ThumbsUp, Peace, FingerGuns, RockOn,
  CallMe, IndexPinch, MiddlePinch, PinkyPinch — all working

## What's next (in priority order, if a new session continues)

1. **Decide the long-term material.** Wireframe is great for diagnostics,
   noisy for shipping. Three options: (a) flip `bUseDebugMaterial=false`
   to use the default Manny-XR material as-is, (b) create a project asset
   `Content/HandTracking/M_HandDebug_TwoSided` via ECABridge while the
   editor's open, set bIsTwoSided=true, point `DebugMaterialOverride` at
   it, (c) modify the existing M_MannyXR material's bIsTwoSided flag
   directly via ECABridge.
2. **Remove the `[vN]` version-marker prefix** from the calibration
   prompt. No longer needed.
3. **Wire gesture detections to gameplay input.** The enum signals are
   firing — they just need consumers.
4. **Validate on different hand sizes** without re-calibration drift.

## Verify build + install pipeline

```bash
cd /Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample

# Confirm AVP is reachable
xcrun devicectl list devices | grep "Agile Alex"

# Re-package (~90 sec). ECABridge auto-skipped via SupportedTargetPlatforms;
# no toggle dance needed.
/Users/Shared/GH/UnrealEngineVisionOS/Engine/Build/BatchFiles/RunUAT.sh \
    BuildCookRun \
    -project=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/My_Project.uproject \
    -platform=VisionOS -build -cook -stage -pak -package -archive \
    -archivedirectory=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/Archive \
    -clientconfig=Development

# Verify (three-check rule — see
# ~/knowledge/intelligence/techniques/verify-build-before-installing.md)
grep -E "AutomationTool exiting with ExitCode" /tmp/uat-*.log | tail -1
grep -E "(BUILD SUCCESSFUL|BUILD FAILED|Cook failed)" /tmp/uat-*.log | tail -3
ls -la Archive/VisionOS/My_Project.app/My_Project   # timestamp must be new

# Install
xcrun devicectl device install app \
    --device 2642855C-6B73-5D5B-9387-6B110E7A7CF3 \
    /Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/Archive/VisionOS/My_Project.app
```

## Verify on device

After launching from the AVP home grid:

* Calibration prompt reads `[v24] ...` (confirms fresh bundle, not cached)
* After calibration: hands curl with your fingers, palms face the right
  direction, wrist cuff points toward your elbow not the camera
* Gesture label appears above each hand: gesture name + per-finger curl
  ratios
* No PSVR2 controller mannequins visible

## Recalibrating for a different skeletal mesh

If you swap `SKM_MannyXR_*` for a different rig (MetaHuman, Mixamo, custom):

1. Set `WristAdjustRotation = FRotator::ZeroRotator` on
   `HandSkeletalDriverComponent`
2. Set `bMultiStrategyTest = true`
3. Build + install
4. In headset, watch the cycling `WRIST <name>` labels and identify which
   90° correction is right for the new rig
5. Set that value as the new `WristAdjustRotation` default
6. Set `bMultiStrategyTest = false`

The finger formula `MakeFromZX(-aim, wristForward)` may or may not still be
right for a different rig — if curling looks wrong, refactor the enum to
re-test the v20–v22 finger candidates too. KB:
`~/knowledge/intelligence/techniques/openxr-to-manny-xr-hand-conversion.md`
explains the search space.
