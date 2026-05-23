# openxr-handtracking

OpenXR hand tracking driving skeletal-mesh hands in real time on Apple Vision Pro — and, eventually, other OpenXR-capable headsets. Built on the Polyarc UE 5.6 fork + the `OpenXRVisionOS` plugin, ships as a hand-tracking layer over the Polyarc PSVR2 sample.

> [!NOTE]
> Forked from [RevivrStudios/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample](https://github.com/RevivrStudios/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample). The PSVR2 sample is the upstream; this fork adds OpenXR hand-tracking driving SKM_MannyXR_left/_right skeletal meshes with real finger motion. Multi-headset support is on the roadmap (currently AVP-only).

## What it does

Three Unreal components wire into the project's `VRPawn` and produce a finger-tracked hand experience:

| Component | What |
|---|---|
| `UHandTrackingComponent` | Captures 26 OpenXR keypoints per hand, runs through a calibration flow (FIST → OPEN HAND), and emits gesture enums for OpenPalm, Fist, ThumbsUp, Peace, FingerGuns, RockOn, CallMe, IndexPinch, MiddlePinch, PinkyPinch. Also renders 52 debug sphere markers and a billboarded gesture label per hand. |
| `UHandSkeletalDriverComponent` | Drives Manny-XR skeletal-mesh hand bones from the OpenXR keypoint stream. Includes per-distal-bone hand-size stretch so rendered fingertips reach the user's actual fingertips, plus a switchable two-sided wireframe debug material for diagnosing UV/winding issues. |
| `UHMDStatusSubsystem` | Hooks visionOS app-lifecycle events to force-exit on background — works around a Polyarc OXRVisionOS quirk where the standard `xr.OpenXRExitAppOnRuntimeDrivenSessionExit` CVar doesn't fire, leaving the app in zombie "crown-exit purgatory." |

The OpenXR → Manny-XR coordinate-frame conversion was empirically derived across 26+ iteration passes. The TL;DR:

```cpp
// Finger bones:
FRotationMatrix::MakeFromZX(-AimDirection, WristXform.GetRotation().GetForwardVector())

// Wrist correction:
WristAdjustRotation = FRotator(0, 0, 90)   // Roll +90 around bone-along axis

// Per-distal-bone hand-size stretch:
scale_z = (live OpenXR distal-to-tip distance) / (bind distal length × DistalTipMeshFactor)
```

Why each piece: see [`HANDOFF_HAND_TRACKING.md`](./HANDOFF_HAND_TRACKING.md).

## Quickstart

Requires a Mac with Xcode 26+, an Apple Vision Pro paired via developer mode, and the [Polyarc UE 5.6 fork](https://polyarcgames.github.io) built locally (used by this project as its engine — you must have your GitHub account linked to your Epic Games account to access the Polyarc UE fork).

```bash
# 1. Clone (no Git LFS needed — assets are stored as regular blobs)
git clone https://github.com/ibrews/openxr-handtracking.git
cd openxr-handtracking

# 2. Install the ECABridge editor plugin (this repo doesn't bundle it; 3.2GB external)
#    See https://github.com/ibrews/ECABridge for install instructions
#    Place at Plugins/ECABridge/

# 3. Package for VisionOS (~90s incremental)
/path/to/UnrealEngineVisionOS/Engine/Build/BatchFiles/RunUAT.sh \
    BuildCookRun \
    -project="$(pwd)/My_Project.uproject" \
    -platform=VisionOS -build -cook -stage -pak -package -archive \
    -archivedirectory="$(pwd)/Archive" \
    -clientconfig=Development

# 4. Install on AVP
xcrun devicectl device install app \
    --device <your-AVP-uuid> \
    Archive/VisionOS/My_Project.app
```

## Things to Try

1. **Test calibration on device.** Launch from the AVP home grid. You'll see `[v26] CALIBRATE: MAKE A TIGHT FIST`. Make a fist, then open your hand fully when prompted. Calibration captures your per-finger curl ratios; gestures fire correctly after.
2. **Watch the per-finger ratio readout.** A debug label above each hand shows live ratios like `OPEN PALM T99/92 I98/92 M99/92 R99/92 L99/92` — Thumb / Index / Middle / Ring / Little curl, normalized against the calibration threshold. Make finger-guns or rock-on to see them shift in real time.
3. **Dial in fingertip reach for your hand.** If your pinkies don't quite touch in headset, edit `UHandSkeletalDriverComponent` → `DistalTipMeshFactor` in the Details panel. Lower (0.25) = longer fingertips; higher (1.0) = shorter. Default is 0.5, empirical for Manny-XR + an average hand.
4. **Recalibrate for a different skeletal mesh.** Edit `UHandSkeletalDriverComponent` in the editor: set `WristAdjustRotation = (0,0,0)`, flip `bMultiStrategyTest = true`, package + install. The on-device test rig cycles through six 90° wrist corrections so you can pick the right one for whatever skeletal mesh you swap in.
5. **Diagnose UV / winding issues with wireframe material.** Flip `bUseDebugMaterial = true` on `UHandSkeletalDriverComponent`. Engine `WireframeMaterial` overrides every mesh slot; it's intrinsically two-sided, so back-facing or winding-flipped geometry stays visible. Great for figuring out whether a mesh issue is rotation- or geometry-related.

## What's working

- Five fingers driven independently with anatomically correct curl
- Wrist correctly oriented (cuff points back toward elbow, not at camera)
- Per-distal-bone scaling so fingertips reach what your real fingertips reach
- Solid Manny material (back-of-hand renders normally — no winding issues)
- PSVR2 controller mannequins hidden so only the tracking-driven hands show
- visionOS app-lifecycle / crown-exit fix
- ECABridge auto-skipped during VisionOS packaging (no toggle dance)
- Gesture detection (10 gestures) wired and stable

## What's not (yet)

- **Multi-headset support.** Tested only on Apple Vision Pro via the Polyarc UE 5.6 fork. The bone conversion formula was empirically derived against Apple's OpenXR runtime; Meta/Quest and other runtimes may apply different joint-pose post-processing. Recalibration via the `bMultiStrategyTest` rig should make this tractable for other headsets, but is unverified.
- **Non-Manny skeletons.** The wrist Roll+90 correction and `MakeFromZX(-aim, wristForward)` finger formula were empirically derived for `SKM_MannyXR_*`. Swapping to MetaHuman, Mixamo, or custom rigs likely needs recalibration. The test rig is gated behind `bMultiStrategyTest=true` to support this.
- **Gameplay wiring.** Gesture enums fire reliably but aren't bound to any actual gameplay input yet.
- **`HANDOFF_HAND_TRACKING.md`** has a "Next directions" section listing other deferred polish items.

## Related repos

- [ibrews/ECABridge](https://github.com/ibrews/ECABridge) — UE editor automation plugin used during this project's development (asset creation, component wiring, BP compilation via JSON-RPC over HTTP). Install separately; not bundled here due to size.
- [RevivrStudios/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample](https://github.com/RevivrStudios/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample) — the vendor PSVR2 sample this is forked from. PSVR2 Sense-controller support is inherited from this upstream.
- [PolyarcGames/UnrealEngineVisionOS](https://polyarcgames.github.io) — UE 5.6 fork with `OpenXRVisionOS` plugin (required engine).

## License

Inherits the upstream RevivrStudios/Polyarc sample's MIT License — see [LICENSE](./LICENSE).

## Disclaimer (inherited from upstream)

This project is an unofficial community sample. It is not affiliated with, endorsed by, or associated with Apple Inc., Sony Interactive Entertainment, Epic Games, or Polyarc. PlayStation and PSVR2 are trademarks of Sony Interactive Entertainment. Apple Vision Pro is a trademark of Apple Inc.
