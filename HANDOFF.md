# Lumenwork — handoff, 2026-09-03 ~07:30 EDT (11:30Z)

**Repo:** `/Users/Shared/GH/Lumenwork` @ `main`. Local only — **no GitHub remote by design**;
the account choice (ibrews = Alex Coulombe Presents vs agilelens = Agile Lens) drives README and
LICENSE branding and is Alex's call.

## Status: cook, build, install, and launch all verified working end to end for the first time

Full detail + all evidence: `~/knowledge/daily/2026-09-03-alex-mbp.md` (search "Lumenwork: FULL
install + launch"). Summary:

- Cook: 805/805 packages, 0 errors, SM5 depth-fixup global shader (`FVisionOSTranslucentDepthFixupPS`)
  confirmed compiled for `METAL_SM5_IOS`.
- Build/stage/pak/package: succeeded via `ue-avp-build.sh device` (after two by-hand Xcode
  PostBuildSync finalize fixes — see gotchas below) and a direct `RunUAT.sh BuildCookRun
  -skipbuild -skipcook -stage -pak -package` follow-up.
- Installed + launched on `Alex's M5 Apple Vision Pro` (`00008142-000468E83409401C`). Did **not**
  hang on a headset-worn gate this time (earlier Pinchwork session tonight did).
- Runtime log confirms: frame counter reached at least 793 with real render activity, no `Missing
  shader permutation` crash, `sf_metal_sm5_ios` shader library loaded, `BP_XRGameMode_C`
  confirmed as the actual runtime GameMode (not the old VRGameMode).

## Two things NOT fixed — left for Alex, both real and confirmed from the runtime log

1. **`r.Lumen.FinalGatherMethod` applies as `1`, not the expected `0`.** Two `SetByScalability`
   attempts to change it are logged as ignored ("lower priority than the previous
   'SetByProjectSetting'. Value remains '1'"). Not investigated — where the ProjectSetting value
   of 1 is coming from, and whether 0 is actually still wanted, needs a decision.
2. **Hand-pinch gesture input is likely broken.** Runtime log:
   `LogUObjectGlobals: Error: CDO Constructor (HandTrackingComponent): Failed to find` for all 6
   `IA_*ThumbPinch_*` InputActions, still under the OLD `/Game/VRTemplate/Input/Actions/Hands/`
   path — not in this project's cook list. A `Config/DefaultGame.ini` comment nearby already notes
   these "must be force-cooked or they won't be packaged" — they weren't, for this project. Fix
   needs a decision: force-cook the old VRTemplate input actions, or migrate
   `HandTrackingComponent`'s references to whatever the new XRFramework equivalents are.
3. **Pawn class not confirmed at runtime** — the pulled log (truncated by a `devicectl copy`
   socket-close mid-transfer) didn't capture a Pawn-spawn line. `BP_XRPawn` vs the old
   `VRTemplate/VRPawn` is still only confirmed by config intent, not runtime evidence, unlike
   GameMode which IS confirmed (`BP_XRGameMode_C`).

## Two build-system gotchas hit and fixed this session (both documented in the KB, both reusable)

- **`Binaries/Mac/UnrealEditor.modules` (and each project plugin's own copy) can go missing from
  the SAME Xcode `PostBuildSync` "Touch UBT generated tiles" flake that already breaks the
  `.target` receipt** — this is what caused last night's *actual* silent cook exit (NOT the
  `MapsToCook` theory from the prior handoff, which was a real but unrelated fix — the cook never
  got far enough to consult `MapsToCook`). Full root cause + fix:
  `~/knowledge/intelligence/techniques/ue58-xcode-finalize-flake-also-drops-modules-manifest.md`.
- **Reconstructing a `.target` receipt from `TargetMetadata.json` for a `Game`/device target must
  keep the `Project` field** (a path relative to the receipt's own directory) — dropping it (as
  the original doc's table said) leaves `Receipt.ProjectDir` null, which silently breaks every
  `$(ProjectDir)`-relative RuntimeDependency (almost always includes every plugin's `.uplugin`) and
  produces a `DirectoryNotFoundException` deep in UAT's staging step. Fixed
  `scripts/ue-write-target-receipt.py` (now writes `Project` unconditionally) and addended
  `ue58-installed-engine-visionos-package-receipt-workaround.md`.

## Devices

M5 AVP (`00008142-000468E83409401C`) — app installed and was running live at session end.
**Pico Swan + XREAL Aura: not touched this session** (a sibling's immersive APK is on both,
awaiting Alex's morning wear test — unrelated to Lumenwork).

## Already done — do not re-derive

Manual `xcodebuild ... UE_XCODE_BUILD_MODE=PostBuildSync` workaround (both Mac Editor and VisionOS
targets, works every time it's been tried); target receipts + module manifests reconstructed
correctly (with the `Project` field fix); Android cherry-picks `7924fff..ddac32d` applied and
renamed to `lumenwork`; stale engine DDC + Zen store moved aside (rollback, ~11 G);
`Content/XRFramework` pulled from Fort.
