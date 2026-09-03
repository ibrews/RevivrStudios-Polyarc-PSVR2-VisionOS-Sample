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

---

## HANDOFF — 2026-09-03 ~09:5x EDT — BUDGET CRITICAL, stopped clean mid-AVP-fix

**Session:** `alex-mbp-lumenwork-avp-pico-aura`. Weekly window hit 95%, resetting Thu 11:00 EDT — stopped per instruction rather than risk a truncated mid-fix state.

### Where things stand, exactly

**M5 AVP re-cook (priority 1) — IN PROGRESS, NOT DONE:**
- Re-cook was launched (`ue-avp-build.sh device`, PID 75175) against complete content @ `2d4af78`. It hit the KNOWN `ApplePostBuildSync`/"Touch UBT generated tiles" flake on **both** targets (Mac Editor + VisionOS) — same as prior sessions.
- **Mac Editor target: FIXED.** Ran the exact `xcodebuild ... UE_XCODE_BUILD_MODE=PostBuildSync` command by hand (copied from `params:` in `~/Library/Logs/Unreal Engine/LocalBuildLogs/UBA-My_ProjectEditor-Mac-Development.txt`) → `** BUILD SUCCEEDED **`. `Binaries/Mac/My_ProjectEditor.target` and `Binaries/Mac/UnrealEditor.modules` both show a **fresh** timestamp (09:40) — UBT's own WriteMetadata phase for this target completed inside the original run before the VisionOS target's failure killed the overall session, so **no manual receipt reconstruction was needed for the Mac Editor target.**
- **VisionOS target: xcodebuild finalize fixed, but the .target RECEIPT IS STILL STALE.** Ran the VisionOS `xcodebuild` finalize command by hand → `** BUILD SUCCEEDED **`. Codesign verified correct: `TeamIdentifier=C624J4S2F8` (Agile Lens LLC — the "Apple Development: Joshua Dachs (GJRH2D635T)" Authority line is a personal-named cert issued *under* the Agile Lens team, confirmed via `codesign -dvvv`, matches the precedent noted in today's earlier daily log — NOT a signing-policy violation). **BUT** `Binaries/VisionOS/My_Project.target` still has mtime `07:22` (stale, predates this run) — UBT's WriteMetadata step for the VisionOS target never ran (its ApplePostBuildSync failed with exit 65 before reaching that phase).

### Exact next command (resume here)

1. Reconstruct the VisionOS target receipt from UBT's own computed data (same pattern as the Mac Editor doc, script already proven):
   ```
   python3 ~/knowledge/intelligence/techniques/scripts/ue-write-target-receipt.py \
     /Users/Shared/GH/Lumenwork/My_Project.uproject VisionOS Development \
     --engine "/Users/Shared/GH/UnrealEngineVisionOS/.claude/worktrees/ue582-xr-main-m5/Engine"
   ```
   (TargetMetadata.json confirmed present at `Intermediate/Build/VisionOS/arm64/My_Project/Development/TargetMetadata.json`.)
2. Check `Plugins/*/Binaries/VisionOS/*.modules` and `Plugins/*/Binaries/Mac/UnrealEditor.modules` too — none were found via `find ... -iname "*.modules"` under `Plugins/` at handoff time, which may itself be a problem (the 2026-09-03 KB doc warns per-plugin manifests can independently be missing, e.g. `Pinchwork`/`AndroidXR`). Verify before trusting a cook.
3. Re-run the SAME command that failed, now with `-skipbuild` (build outputs are all valid on disk, just needed manual finalize + receipt fix):
   ```
   cd /Users/Shared/GH/Lumenwork && DEVICE_ID=00008142-000468E83409401C \
   "/Users/Shared/GH/UnrealEngineVisionOS/.claude/worktrees/ue582-xr-main-m5/Engine/Build/BatchFiles/RunUAT.sh" \
     BuildCookRun -project="/Users/Shared/GH/Lumenwork/My_Project.uproject" -target=My_Project -platform=VisionOS \
     -clientconfig=Development -skipbuild -cook -stage -pak -package -nop4 -unattended -nopause -utf8output \
     -ini:Engine:[/Script/MacTargetPlatform.XcodeProjectSettings]:CodeSigningTeam=C624J4S2F8
   ```
   **Before installing, prove in the cook output** (per the original brief): `FVisionOSTranslucentDepthFixupPS` in the shader stats CSV, and grep the cook log for `BP_Picture`, `VRPawn_HandWorldControl`, `SM_SportsCar`, and the `Hands` InputActions — if absent, the cook dropped them and installing is pointless.
4. Install: `xcrun devicectl device install app --device 00008142-000468E83409401C "$(ls -dt /Users/Shared/GH/Lumenwork/Saved/StagedBuilds/VisionOS/*.app | head -1)"`, then wear+tap, pull the runtime log, and confirm (verbatim, per the brief): frame counter progressing, **Pawn = VRPawn_HandWorldControl** (never confirmed in any prior session — do this explicitly), GameMode = `BP_XRGameMode_C`, zero `LogUObjectGlobals: Error` for the 6 `IA_*ThumbPinch_*` InputActions, no `Missing shader permutation`, `sf_metal_sm5_ios`.

### Android (Pico Swan + XREAL Aura) — NOT STARTED building, prep done

**Course correction received mid-task (from the coordinator): build Android on FORT via the proven PowerShell driver, not on this Mac (no NDK here).** Do NOT try to stand up an Android toolchain on alex-mbp.

- Driver: `H:\GH\fort-android-lumen.ps1` on Fort (copy: `~/knowledge/projects/pinchwork/scripts/fort-android-lumen.ps1`). Modes in order: `prepare` (skip the git-clone part — see below) → `disable-visionos-plugin` → `sdk-ini` → `build` → `status`.
- **Lumenwork has no GitHub remote** (local-only repo by design), so the driver's `prepare` mode's `git clone` won't work — Lumenwork must be transferred to Fort as a file tree, not cloned.
- **Tarball IS DONE**: `/tmp/lumenwork_android.tar.gz` on alex-mbp, 2.46 GB, confirmed complete at handoff time. Not yet copied to Fort — next step is the `scp` below, nothing before it needed.
  ```
  cd /Users/Shared/GH/Lumenwork && tar czf /tmp/lumenwork_android.tar.gz \
    --exclude='.git' --exclude='Saved' --exclude='Intermediate' --exclude='Binaries' \
    --exclude='DerivedDataCache' --exclude='.claude' .
  ```
  Then `scp /tmp/lumenwork_android.tar.gz fort:H:/GH/lumenwork_android.tar.gz` and on Fort: `ssh fort 'powershell -NoProfile -Command "New-Item -ItemType Directory -Force H:\GH\Lumenwork-android; tar -xzf H:\GH\lumenwork_android.tar.gz -C H:\GH\Lumenwork-android"'`.
- Edit `fort-android-lumen.ps1`'s `$Repo` to `H:\GH\Lumenwork-android`, `$Log` to a Lumenwork-specific path — do NOT overwrite the sibling's `Revivr-android-lumen` paths (different project, still may be in use).
- After `prepare`-equivalent steps, still must: set `EngineAssociation` to `"5.8"` in `My_Project.uproject`, confirm `Config/Android/AndroidEngine.ini` present (it is, verified in Lumenwork already — `bSupportsVulkanSM5=True`/`bSupportsVulkan=False` confirmed in `Config/DefaultEngine.ini` at handoff time, package `com.agilelens.lumenwork.lumen`), confirm `Plugins/AndroidXR` present (it is).
- **Gate before installing anything:** grep Fort's build log for `Shader Library 'Global' (SF_VULKAN_SM5_ANDROID` — `SF_VULKAN_ES31_ANDROID` is a full stop.
- Devices confirmed connected via adb on alex-mbp at task start: Pico Swan (`PB324XJGL2090013G`), XREAL Aura (`v1l466811861db`). Both free — no live session held them (sibling released its claim per the coordinator's message). Deploy script: `~/knowledge/projects/pinchwork/scripts/deploy-android-lumen.sh` (verify it works with Lumenwork's paths — same `My_Project` project name, so its hardcoded UE log path may just work).

### Fort liveness check before touching anything there

At handoff time, Fort had ONE stray process: `UnrealEditor-Cmd.exe` PID 75728, running a **different, unrelated** project (`D:\GitHub\unreal-visionos-basics\vr58.uproject`, `-nullrhi -unattended -ExecCmds=quit`) — looked like a self-terminating recheck, not a live editor holding Live Coding. Re-check `Get-Process UnrealEditor,UnrealEditor-Cmd` on Fort before starting the Android build; if a live editor is open, only `build-android-only` (game target, not editor) is safe per the known Live-Coding-blocks-editor-target trap.

### Board / cleanup

Session board entry `Alexs-MacBook-Pro-3-alex-mbp-lumenwork-avp-pico-aura` is checked in with `ue-build-engine,avp-device(M5)` — **release/update this claim on resume** so a fresh session doesn't think the singleton is still held by a live process (it isn't — this session is stopping).
