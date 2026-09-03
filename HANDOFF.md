# Lumenwork — handoff, 2026-09-03 ~10:40Z

**Repo:** `/Users/Shared/GH/Lumenwork` @ `main`. Local only — **no GitHub remote by design**;
the account choice (ibrews = Alex Coulombe Presents vs agilelens = Agile Lens) drives README and
LICENSE branding and is Alex's call.

## Exact next command
```bash
E="/Users/Shared/GH/UnrealEngineVisionOS/.claude/worktrees/ue582-xr-main-m5"
"$E/Engine/Binaries/Mac/UnrealEditor-Cmd" "/Users/Shared/GH/Lumenwork/My_Project.uproject" \
  -run=Cook -TargetPlatform=VisionOS -unversioned -unattended -NoLogTimes > /tmp/cook.log 2>&1; echo $?
```

## The live blocker and the untested fix
Cook exited(1) silently after `LogAudio: Registering Engine Module Parameter Interfaces`, with
**zero packages queued** and no error text at any verbosity.

**Found:** `Config/DefaultGame.ini`'s `+MapsToCook` listed only the OLD maps
(`VRTemplate/Maps/VRTemplateMap`, `TravelTestMap`). Alex's swapped-in level
`/Game/XRFramework/Levels/L_XRTemplateMovableTest` — which `GameDefaultMap` points at — was
**never in the cook list**. Added it. **NOT YET VERIFIED** — the window expired before a re-run.

## Verify before claiming anything works
- Cook actually queues packages, and `L_XRTemplateMovableTest` is among them.
- Cook output contains the SM5 depth-fixup global shader **before** installing anything.
- Runtime log: frame counter past `][ 0]`, `sf_metal_sm5_ios`, applied `r.Lumen.FinalGatherMethod=0`,
  and **no `Missing shader permutation`** (that string is what crashed the earlier build in front of Alex).
- **Which GameMode/Pawn actually instantiate.** `GlobalDefaultGameMode` still points at the OLD
  `VRTemplate/VRGameMode` while the new content ships `BP_XRGameMode`/`BP_XRPawn`. Both resolve, so
  it fails silently. Do NOT repoint it without Alex.
- Alex's `BP_XRPawn` sets `xr.SecondaryScreenPercentage.HMDRenderTarget 120` (Pinchwork used 100).
  120% supersampling under Lumen SM5 is a real GPU cost — first knob to try at 100 if frames are slow.

## Already done — do not re-derive
Manual `xcodebuild ... UE_XCODE_BUILD_MODE=PostBuildSync` workaround (both targets, works);
target receipts written; Android cherry-picks `7924fff..ddac32d` applied and renamed to `lumenwork`;
stale engine DDC + Zen store moved aside (rollback, ~11 G); `Content/XRFramework` pulled from Fort.

## Devices
M5 AVP (`00008142-000468E83409401C`) left ACTIVE by Alex — build/install/launch freely.
**Pico Swan + XREAL Aura: do NOT install** — a sibling's immersive APK is on both awaiting Alex's
morning wear test. Android evidence tonight is cook-log only.

## Status, stated plainly
**No successful cook, install, or runtime evidence exists for Lumenwork on any target.** Nothing works yet.
