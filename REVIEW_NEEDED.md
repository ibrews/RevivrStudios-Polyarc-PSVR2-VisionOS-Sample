# REVIEW_NEEDED — Pinchwork 2.0 (overnight, Stream 2)

Branch: `spinoff/pinchwork-plugin` · 2026-06-18 · chip session (Alex at Unreal Fest).

## TL;DR
The Pinchwork **2.0 core is done and verified off-headset** (`./Plugins/Pinchwork/Tests/run_tests.sh`
→ 80 checks, 0 failures). The new **UE wrapper components are written but NOT compiled** —
a full engine/editor build was out of scope overnight (the shared UE fork build
mutex was held by a sibling `ECABridge` build). Two things need Alex:
**(1) one UE build to compile the new modules, (2) on-device gesture verification.**

---

## ⚠️ BLOCKED / not verified this session

### 1. The new UE code has not been compiled
New, written but un-built (no UBT run — mutex + multi-hour build):
- Module `PinchworkCore` (`Plugins/Pinchwork/Source/PinchworkCore/`)
- `UTwoHandManipulatorComponent`, `UGestureSequenceComponent`, `PinchworkUE.h`
  (`Plugins/Pinchwork/Source/Pinchwork/`)
- `Pinchwork.Build.cs` (+`PinchworkCore` dep) and `Pinchwork.uplugin` (2 modules, v2.0)

What IS verified: the pure-C++ core those wrappers delegate to compiles clean
under `clang++ -std=c++17 -Werror -Wall -Wextra` and passes all 80 asserts.
The wrappers are deliberately thin (FVector↔FVec3 conversion + UPROPERTYs), so
the risk surface is UHT/reflection boilerplate, not algorithm logic.

**To compile (when the UE build mutex is free — gate first):**
```bash
# 1. Confirm no other UE build is running (collision rule):
ps aux | grep -iE 'UnrealBuildTool|Build.sh|RunUAT|clang' | grep -v grep
# 2. Regenerate project files so the new PinchworkCore module is picked up, then
#    build the editor target against the visionOS fork at
#    /Users/Shared/GH/UnrealEngineVisionOS (same flow as a normal editor build
#    for this project). Watch for:
#      - UHT parse of the two new component headers (.generated.h include is last)
#      - the EKeypoint/EHandKeypoint + EGesture/EHandGesture static_asserts
#        (they FAIL the build loudly if an enum ever drifts — that's intended)
```
Once it compiles, the on-device checklist below applies.

### 2. Gestures cannot be verified in the simulator
The visionOS sim has **no hand tracking** (ARKit hand provider gated off). The
mock-joint harness is the off-headset proof; real-hand behavior must be checked
on the AVP. **On-device verify checklist** (bundle `com.agilelens.uf.pinchwork2`,
never overwrite an existing app):
- [ ] Existing 1.0 still works: per-finger pinches + curl gestures classify, the
      floating label reads correctly, calibration (fist→open) completes.
- [ ] **Two-hand transform:** add `UTwoHandManipulatorComponent` to the pawn,
      set a test cube as `Target Actor`, wire both hands' index pinches to
      Begin/Update/End Grab. Verify: hands apart → cube scales up; twist →
      cube rotates; slide both hands → cube translates; scale clamps at
      Min/Max; `Yaw Only Rotation` restricts to turntable spin.
- [ ] **Gesture macro:** add `UGestureSequenceComponent`, register
      `fist → open palm → finger guns` (1.5 s budget), wire `OnGestureStarted`
      → `FeedGesture`, bind `OnSequenceCompleted`. Verify it fires only on the
      correct ordered combo and not on partial/timed-out/wrong-order attempts.

---

## Follow-ups / tech debt (not blocking)

- **De-dup the math.** `UHandTrackingComponent` still carries its own copy of
  the recognition math (extension ratio, fingerprint classify, calibration,
  thumb-up split). 2.0's `PinchworkCore` mirrors it value-for-value but the
  component was left untouched (couldn't build to verify a refactor). Next:
  have the component fill a `Pinchwork::FHandPose` (via `PinchworkUE::FillHandPose`)
  and call `RecognizeGesture`, deleting the duplicated methods. Pure cleanup,
  behavior-preserving, but needs a build to confirm.
- **Project-specific code in a "self-contained" plugin.** `HandTrackingComponent.cpp`
  contains Polyarc-sample-specific test features that don't belong in the MIT
  plugin: gun fire/grip-orientation CVars (`r.Gun.*`), Superman two-fist fly
  (`r.Fly.*`), pinky-pinch level travel (`LevelAPath`/`LevelBPath`), and the
  visionOS translucent-depth-fix A/B cycler (`r.Mobile.VisionOS.*`). These should
  move to the sample project (e.g. a subclass or the pawn BP) so Pinchwork ships
  clean. Flagged, not touched — out of scope + unverifiable overnight.
- **On-device HUD upgrade** (live per-finger + gesture + confidence, two-hand
  scale/rotation readout) was deferred: it needs UMG/Slate + the engine and
  isn't sim-verifiable. The core already exposes everything a HUD would show.
