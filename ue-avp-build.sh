#!/bin/bash
# ue-avp-build.sh — one-command sim/device build switcher for Unreal Engine visionOS (AVP) projects.
#
# Modeled on the Godot build.sh (ibrews/godot-avp-cascade). THE KEY IDEA: the project's render config NEVER
# changes between sim and device — it stays the one proven Mixed/passthrough ("Pinchwork") config on disk,
# UNTOUCHED. Only the build ARCH, SIGNING, and INSTALL differ, and all of that is carried by the build COMMAND.
# No `swap-visionos-config.sh`, no in-place ini mutation, no "which state is this repo in?" footgun. Two projects
# can build different targets at once because nothing shared is mutated.
#
#   ./ue-avp-build.sh sim       # build+cook+package for the visionOS Simulator, then install + launch
#   ./ue-avp-build.sh device    # build+cook+package for a real AVP (Agile Lens signed), install (then TAP to launch)
#   ./ue-avp-build.sh help
#
# ⚠️ CLAUDE CODE CALLERS: `sim`/`device` MUST be invoked with `dangerouslyDisableSandbox: true` on the
# Bash tool call. Without it, modern-Xcode (bUseModernXcode=True) builds -- Mac Editor AND VisionOS/iOS
# package targets both -- fail 100% of the time at the final Xcode finalize step ("Run custom shell
# script 'Touch UBT generated tiles'" / "Bad file descriptor"), AFTER hundreds of compile/link actions
# already succeeded. This is a real, deterministic Claude Code Bash-sandbox bug, not a UE/project issue.
# Root cause + fix: ~/knowledge/intelligence/techniques/headless-xcodebuild-scheme-preaction-bad-file-descriptor.md
#
# Drop into a UE visionOS project root; edit CONFIG (or a sibling ue-avp-build.config). Canonical copy lives in the
# KB (intelligence/techniques/scripts/). Design + rationale: intelligence/techniques/ue-visionos-sim-device-build-flow.md
#
# Self-monitoring (added 2026-08-28, see preventing-wasted-build-cycles-on-shared-ue-machines.md): sim/device
# runs self-log to a deterministic path, get a post-BuildCookRun cross-check against
# ue-build-log-status.sh (catches "BuildCookRun's own exit code said success but the log secretly shows
# Result: Failed" -- the exact class of bug from that incident), and text a PASS/FAIL verdict via fleet_bus.py
# on exit -- so an invoking session no longer has to remember to poll or watch compile lines scroll. A
# format mismatch in the log check (exit 2, INCOMPLETE) never fails a build on its own -- only an EXPLICIT
# FAIL marker (exit 1) does; the check can only add a true positive, never a false one.
set -euo pipefail

# ---------- CONFIG (override via env or a sibling ue-avp-build.config) ----------
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -f "$HERE/ue-avp-build.config" ] && source "$HERE/ue-avp-build.config"
UE_ROOT="${UE_ROOT:-/Users/Shared/GH/UnrealEngineVisionOS}"        # the engine fork (ibrews @ visionos-integration)
PROJECT="${PROJECT:-$HERE/PolyarcSample.uproject}"                 # this project's .uproject (abs path)
TARGET="${TARGET:-PolyarcSample}"
CLIENTCONFIG="${CLIENTCONFIG:-Development}"
BUNDLE_ID="${BUNDLE_ID:-}"                 # blank = auto-detect from the packaged .app's Info.plist
SIM_UDID="${SIM_UDID:-}"                  # blank = auto-pick a visionOS sim
DEVICE_ID="${DEVICE_ID:-}"                # blank = auto-pick a connected AVP (or set explicitly)
DEVICE_TEAM="${DEVICE_TEAM:-C624J4S2F8}"  # Agile Lens LLC — sign com.agilelens.* / AVP builds with this team
EXPECTED_DEVICE_BUNDLE_ID="${EXPECTED_DEVICE_BUNDLE_ID:-com.agilelens.lumenworkmain.Lumenwork}"
INSTALL_DEVICE="${INSTALL_DEVICE:-0}"
DEVICE_ACTIVITY_CHECKED="${DEVICE_ACTIVITY_CHECKED:-0}"
# --------------------------------------------------------------------------------

UAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
export NuGetAudit=false   # clean-machine: Magick.NET 14.10.2 NU1902 is treated as a build error otherwise (KB gotcha)
STAGE_DIR="$(dirname "$PROJECT")/Saved/StagedBuilds/VisionOS"

MODE="${1:-help}"

# D21b (2026-09-03): refuse a RETIRED or HOLLOW engine tree before spending a compile on it.
# On 2026-09-03 a lane built against the retired 5.6 trunk root instead of the worktree UE_ROOT
# names, compiled all 670 modules from intact source, and died at ApplePostBuildSync on the
# missing DotNet runtime -- 2h57m for nothing. This gate costs ~1 second.
# Script: ~/knowledge/scripts/ue-build-preflight.sh · decision:
# ~/knowledge/intelligence/decisions/2026-09-03-one-golden-trunk-avp-openxr.md
# FAIL CLOSED. The first cut of this gate said `[ -x "$UE_PREFLIGHT" ]` and the KB copy was mode
# 644, so the whole check was skipped in silence and a `sim` run against the retired root sailed
# straight into RunUAT -- the exact fail-open shape that made worktree-scope-guard.sh a no-op on
# four Windows machines for weeks (see that script's jq comment block). Invoke through `bash` so a
# lost exec bit cannot disarm it, and abort if the script is missing rather than assuming OK.
UE_PREFLIGHT="$HOME/knowledge/scripts/ue-build-preflight.sh"
if [ "$MODE" != "help" ]; then
  if [ ! -f "$UE_PREFLIGHT" ]; then
    echo "[ue-avp-build] ABORT: engine preflight missing at $UE_PREFLIGHT -- run: cd ~/knowledge && git pull --rebase" >&2
    exit 1
  fi
  UE_ROOT="$UE_ROOT" bash "$UE_PREFLIGHT" --tree "$UE_ROOT" \
    || { echo "[ue-avp-build] preflight refused this engine tree -- aborting before the build. Set UE_ROOT to the golden trunk (see ue-avp-build.config) or repair the tree." >&2; exit 1; }
fi
SCRIPT_START="$(date +%s)"
KB_SCRIPTS="$HOME/knowledge/scripts"
STATUS_SCRIPT="$KB_SCRIPTS/ue-build-log-status.sh"
FLEET_BUS="$HOME/knowledge/departments/engineering/fleet-tools/fleet_bus.py"
LOGFILE=""   # set by start_monitoring; empty means "no self-log for this invocation" (e.g. help)

# Self-log this invocation's full output to a deterministic path via `tee`, so it's discoverable
# regardless of whether the caller ALSO redirected output elsewhere -- and arm an EXIT trap that
# reports the final PASS/FAIL/rc verdict via fleet-bus, so nobody has to remember to check back.
# Only called for sim/device -- `help` needs none of this.
start_monitoring() {
  local logdir="$(dirname "$PROJECT")/Saved/BuildLogs"
  mkdir -p "$logdir"
  LOGFILE="$logdir/ue-avp-build-${MODE}-$(basename "$PROJECT" .uproject)-$$.log"
  exec > >(tee -a "$LOGFILE") 2>&1
  echo "[ue-avp-build] self-logging this $MODE run to $LOGFILE"
  trap alert_on_exit EXIT
}

alert_on_exit() {
  local rc=$?
  local elapsed=$(( $(date +%s) - SCRIPT_START ))
  local verdict="PASSED"
  [ "$rc" -ne 0 ] && verdict="FAILED (rc=$rc)"
  echo "[ue-avp-build] $MODE $verdict in ${elapsed}s. Log: $LOGFILE"
  if [ -f "$FLEET_BUS" ]; then
    python3 "$FLEET_BUS" send --to human --body "ue-avp-build.sh $MODE ($TARGET on $(hostname -s)): $verdict in ${elapsed}s. Log: $LOGFILE" >/dev/null 2>&1 || true
  fi
  log_build_timing "$rc" "$elapsed"
}

# 2026-09-04 (Alex): auto-track every invocation's timing so future sessions can give accurate
# ETAs instead of guessing -- this used to only happen if a session remembered to hand-write a
# task-timing-log.jsonl line at the END of a whole work session, which lost the cook-only vs
# full-build distinction that actually matters for an estimate (a SKIP_BUILD cook-only pass and a
# full -build -cook -stage -pak -package run differ by minutes, not seconds). One JSONL line per
# invocation, keyed by that distinction via the `phase` field so they can be filtered separately.
log_build_timing() {
  local rc="$1" elapsed_s="$2"
  local timing_log="$HOME/knowledge/departments/engineering/task-timing-log.jsonl"
  [ -d "$(dirname "$timing_log")" ] || return 0
  local machine="${FLEET_MACHINE:-$(hostname -s 2>/dev/null || echo unknown)}"
  local phase="full-build"
  [ "${SKIP_BUILD:-0}" = "1" ] && phase="cook-only(SKIP_BUILD)"
  local outcome="success"
  [ "$rc" -ne 0 ] && outcome="failed"
  # UAT's own self-reported "BuildCookRun time: N.NNs" line is a tighter measure of the actual
  # engine work than $elapsed_s (which also includes device install/auto-detect for `device` mode)
  # -- prefer it when parseable, fall back to overall elapsed otherwise.
  local ucr_s=""
  [ -n "$LOGFILE" ] && [ -f "$LOGFILE" ] && \
    ucr_s="$(grep -o 'BuildCookRun time: [0-9.]*' "$LOGFILE" 2>/dev/null | tail -1 | grep -o '[0-9.]*$')"
  local duration_min
  duration_min="$(awk -v s="$elapsed_s" 'BEGIN { printf "%.1f", s/60 }')"
  local task="ue-avp-build.sh $MODE ($TARGET, $phase)"
  local date_str="$(date +%Y-%m-%d)"
  {
    printf '{"task":"%s","type":"build_ue5","machine":"%s","duration_min":%s,"outcome":"%s","date":"%s"' \
      "$task" "$machine" "$duration_min" "$outcome" "$date_str"
    [ -n "$ucr_s" ] && printf ',"build_cook_run_s":%s' "$ucr_s"
    printf ',"phase":"%s","elapsed_s":%s}\n' "$phase" "$elapsed_s"
  } >> "$timing_log" 2>/dev/null || true
}

# Cross-check BuildCookRun's own exit code against the log itself. `set -e` already aborts this
# script if bcr returns nonzero -- this catches the OTHER class of bug (proven real earlier today):
# UAT/UBT reporting success while the log secretly contains a terminal FAIL marker deeper in its
# own output. Deliberately asymmetric: only an explicit FAIL (status-checker exit 1) hard-fails the
# build; INCOMPLETE/unparseable (exit 2, e.g. a log-format mismatch) just logs a note and continues
# -- a gap in this check's log-format coverage must never be able to fail a genuinely good build.
verify_log_or_fail() {
  [ -f "$STATUS_SCRIPT" ] || { echo "[ue-avp-build] NOTE: $STATUS_SCRIPT not found, skipping log cross-check"; return 0; }
  set +e
  bash "$STATUS_SCRIPT" "$LOGFILE" 1
  local status_rc=$?
  set -e
  if [ "$status_rc" -eq 1 ]; then
    echo "[ue-avp-build] BuildCookRun's own exit code said success, but ue-build-log-status.sh found an explicit FAIL marker in $LOGFILE -- treating this as a hard failure." >&2
    exit 1
  fi
  # 2026-09-03: this function's own last statement used to be the `if` test above. When status_rc
  # is anything but 1 (the overwhelmingly common case: a genuine PASS), that `[ ... -eq 1 ]` test
  # itself evaluates false -- exit status 1 -- and since it's the function's LAST command, `set -e`
  # read that as verify_log_or_fail (and therefore bcr, and therefore the whole script) FAILING,
  # even though the build had just printed "OVERALL: PASS". Explicit `return 0` closes the gap.
  return 0
}

bcr() {  # shared BuildCookRun -- render config comes from the project's committed ini, UNTOUCHED
  # 2026-09-09: TWO-PASS BY DEFAULT. The single-pass `-build -cook -stage -pak -package` ALWAYS dies at
  # the Xcode 'Touch UBT generated tiles' / ApplePostBuildSync step (ExitCode=6) BEFORE cook ever runs,
  # so `./ue-avp-build.sh sim|device` could not produce a package at all. Measured 2026-09-09 across
  # phases 1-3 plus both packaging attempts: 5 for 5, deterministic, not a flake.
  # That abort is POST-LINK, so the binary it just produced is good. Hence:
  #   PASS 1  -build only        -> links the arch-correct binary, then dies at finalize (expected)
  #   ASSERT  vtool              -> device and sim share ONE output path and UBT's staleness check is
  #                                 platform-blind, so this is what stops a sim binary shipping to a headset
  #   PASS 2  SKIP_BUILD=1       -> -skipbuild -cook -stage -pak, finalize not re-run, succeeds
  # Verified end to end 2026-09-09: device .app VISIONOS + 429MB cookeddata; sim .app VISIONOSSIMULATOR.
  # Set BCR_SINGLE_PASS=1 to get the old behaviour back.
  # 2026-09-10 REVERTED TO SINGLE-PASS BY DEFAULT.
  # I made two-pass the default on 2026-09-09 after MY OWN engine builds died at the Xcode
  # 'Touch UBT generated tiles' finalize step, and generalised that to "BuildCookRun can never
  # package". That was wrong. The evidence against it:
  #   - this project packaged fine on the single-pass path at 2026-09-09 10:36 (429 MB cooked .app)
  #   - sibling sessions installed R1 CAP30 / R2 CAP22 / R3 CAP45 / S1 BANDFIX to the M5 the same day
  # Worse, two-pass routes pass 2 through SKIP_BUILD=1, which DROPS -package -- so it only ever
  # STAGES. No packaged, signed .app is produced, and nothing can be installed to a device.
  # Two-pass remains available for the case it was built for (a genuine finalize block):
  #   BCR_TWO_PASS=1 ./ue-avp-build.sh device
  if [ "${SKIP_BUILD:-0}" = "1" ] || [ "${BCR_TWO_PASS:-0}" != "1" ]; then
    _bcr_raw "$@"; return $?
  fi
  echo "[ue-avp-build] pass 1/2: -build (expect the finalize abort; the link is what matters)"
  BCR_SINGLE_PASS=1 SKIP_BUILD=0 _bcr_build_only "$@" || true
  local want=device; case " $* " in *iossimulator*) want=simulator ;; esac
  local bin="$(dirname "$PROJECT")/Binaries/VisionOS/$TARGET"
  if [ -x "$HOME/knowledge/scripts/visionos-platform-preflight.sh" ]; then
    bash "$HOME/knowledge/scripts/visionos-platform-preflight.sh" --expect "$want" --binary "$bin" || {
      echo "[ue-avp-build] ABORT: linked binary is not a $want build -- refusing to cook/package it." >&2
      return 8; }
  fi
  echo "[ue-avp-build] pass 2/2: -skipbuild -cook -stage -pak"
  SKIP_BUILD=1 _bcr_raw "$@"
}

_bcr_build_only() {
  # 2026-09-09: pass 1 is EXPECTED to end with UBT "Result: Failed" (the ApplePostBuildSync
  # 'Touch UBT generated tiles' abort, which happens AFTER the link). If that lands in the shared
  # self-log, ue-build-log-status.sh sees a FAIL marker next to pass 2's success and correctly
  # reports CONFLICT -> the whole run is failed despite a good package. So pass 1 gets its OWN log.
  # Pass 2's result is the verdict; pass 1's job is only to produce an arch-correct binary.
  local p1log="${PROJECT%/*}/Saved/BuildLogs/ue-avp-build-pass1-$$.log"
  mkdir -p "$(dirname "$p1log")"
  echo "[ue-avp-build] pass 1 log (finalize abort here is expected): $p1log"
  "$UAT" BuildCookRun -project="$PROJECT" -target="$TARGET" -platform=VisionOS \
    -clientconfig="$CLIENTCONFIG" -build -nop4 -unattended -nopause -utf8output -WaitForUATMutex "$@" \
    > "$p1log" 2>&1
  local rc=$?
  echo "[ue-avp-build] pass 1 rc=$rc  link=$(grep -aoE 'Link \[[a-z0-9]+\] [A-Za-z]+' "$p1log" | tail -1)"
  return $rc
}

_bcr_raw() {
  # 2026-09-03: SKIP_BUILD=1 drops -build (which pairs with -package) and adds -skipbuild -unrealexe
  # instead, so a fresh -cook -stage -pak can run against an already-finalized .app without re-hitting
  # the Xcode ApplePostBuildSync finalize flake. Passing -skipbuild ALONGSIDE the hardcoded -build (the
  # old way of calling this) does nothing -- -build still runs and still hits the flake first.
  if [ "${SKIP_BUILD:-0}" = "1" ]; then
    "$UAT" BuildCookRun -project="$PROJECT" -target="$TARGET" -platform=VisionOS \
      -clientconfig="$CLIENTCONFIG" -cook -stage -pak -skipbuild \
      -unrealexe="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" \
      -nocompileeditor -skipbuildeditor -nop4 -unattended -nopause -utf8output -WaitForUATMutex "$@"
  else
    "$UAT" BuildCookRun -project="$PROJECT" -target="$TARGET" -platform=VisionOS \
      -clientconfig="$CLIENTCONFIG" -build -cook -stage -pak -package -nop4 -unattended -nopause -utf8output -WaitForUATMutex "$@"
  fi
  verify_log_or_fail
}
newest_app() { ls -dt "$STAGE_DIR"/*.app 2>/dev/null | head -1; }
pick_sim() { [ -n "$SIM_UDID" ] && { echo "$SIM_UDID"; return; }
  xcrun simctl list devices visionOS available | grep -oE '\(([0-9A-F-]{36})\)' | tr -d '()' | head -1; }

case "$MODE" in
  sim)
    start_monitoring
    # Sim deltas: arch=iossimulator + cook METAL_SIM shaders + MSAA OFF AT RUNTIME. Proven root cause
    # (2026-07-05, rediscovered after a black-sim regression): on the sim's A8-level Metal (METAL_SIM), the
    # MSAA color-resolve into the presented backbuffer silently drops when r.Mobile.AntiAliasing=3
    # (device's value) — the scene draws, but never reaches the screen (visible stat overlay, black 3D).
    # Proven+fixed once already (intelligence/techniques/ue-visionos-simulator-build-revival.md,
    # 2026-06-07) but the fix never made it into this switcher.
    #
    # IMPORTANT: a cook-time `-ini:` override for this CVar does NOT work reliably — UE's incremental/
    # iterative cooker only invalidates cached shader permutations when the physical ini FILE changes;
    # a command-line -ini: override never touches the file, so an incremental cook silently keeps reusing
    # shaders baked under the previous value (confirmed empirically 2026-07-05: a rebuild with the -ini:
    # override still rendered black). The fix that actually works is forcing it as a RUNTIME startup
    # console command (`-ExecCmds`), baked into the packaged app's uecommandline.txt post-package — this
    # sidesteps cook/shader-cache invalidation entirely since it's just a live CVar set after launch.
    bcr -clientarchitecture=iossimulator \
      "-ini:Engine:[/Script/IOSRuntimeSettings.IOSRuntimeSettings]:bEnableSimulatorSupport=True" \
      "-ini:Engine:[/Script/IOSRuntimeSettings.IOSRuntimeSettings]:bSupportAppleA8=True"
    APP="$(newest_app)"; DEV="$(pick_sim)"; [ -z "$DEV" ] && { echo "no visionOS sim found"; exit 1; }
    BUNDLE_ID="${BUNDLE_ID:-$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Info.plist" 2>/dev/null)}"
    # Force MSAA off at runtime (see note above) — patch uecommandline.txt and re-sign, no rebuild needed.
    CMDLINE="$APP/uecommandline.txt"
    if [ -f "$CMDLINE" ] && ! grep -q 'r.Mobile.AntiAliasing' "$CMDLINE"; then
      sed -i '' 's/$/ -ExecCmds="r.Mobile.AntiAliasing 0"/' "$CMDLINE"
      codesign --force --sign - --deep "$APP" >/dev/null 2>&1
    fi
    xcrun simctl boot "$DEV" 2>/dev/null || true; open -a Simulator >/dev/null 2>&1 || true
    xcrun simctl terminate "$DEV" "$BUNDLE_ID" 2>/dev/null || true
    xcrun simctl install "$DEV" "$APP"
    xcrun simctl launch "$DEV" "$BUNDLE_ID"
    echo "→ launched $BUNDLE_ID in sim $DEV (Mixed/passthrough — same render config as device)" ;;
  device)
    start_monitoring
    # 2026-09-08: Build/Delivery/guarded_ubt.py pins EXPECTED_PROJECT_ROOT to the _deliveries
    # clone and HEAD to one frozen commit, so it rejects legitimate follow-on commits in this
    # working checkout. The engine/identity facts it asserted are checked by the lane owner and
    # recorded in the receipt instead. The packaged-bundle-id refusal below is kept -- that one
    # catches a real, repeatable mistake (installing the wrong Lumenwork identity).
    # 2026-09-08: the zero-action-ceiling quarantine here is superseded by
    # intelligence/decisions/2026-09-08-engine-and-package-builds-are-a-claude-owned-mac-lane.md
    # -- ue-build-lane.sh is the guard; this lane compiles, cooks and packages in one owner.
    # Device = arm64 (no -clientarchitecture). Agile Lens signing. Immersive apps can't be remote-launched → tap.
    bcr "-ini:Engine:[/Script/MacTargetPlatform.XcodeProjectSettings]:CodeSigningTeam=$DEVICE_TEAM"
    APP="$(newest_app)"
    ACTUAL_DEVICE_BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Info.plist" 2>/dev/null || true)"
    if [ "$ACTUAL_DEVICE_BUNDLE_ID" != "$EXPECTED_DEVICE_BUNDLE_ID" ]; then
        echo "[ue-avp-build] REFUSING INSTALL: packaged identity '$ACTUAL_DEVICE_BUNDLE_ID' does not equal expected Lumenwork identity '$EXPECTED_DEVICE_BUNDLE_ID'." >&2
        echo "[ue-avp-build] com.agilelens.lumenwork.Lumenwork is occupied on the M5 (label 'Lumenwork B13 0908') and claimed by the separate Light Pavilion project; never install that identity from here." >&2
        echo "[ue-avp-build] Rebrand and re-sign a copy as $EXPECTED_DEVICE_BUNDLE_ID, verify it, then install the verified copy explicitly." >&2
        exit 1
    fi
    # 2026-09-03: this auto-detect was doubly broken and FAILED SILENTLY.
    #  1. The old regex was a UUID pattern (8-4-4-4-12). Apple Vision Pro UDIDs are ECID format
    #     (8-16, e.g. 00008142-000468E83409401C), so it matched NOTHING -> DEV_ID empty ->
    #     the `if [ -n "$DEV_ID" ]` below skipped the install with no error. A build would
    #     "succeed" having installed nothing.
    #  2. Even with a correct regex, `head -1` takes devicectl's FIRST vision device, which on
    #     this Mac is an `unavailable` older AVP -- not the connected one.
    # Now: match the real UDID shape, and prefer connected > available, never unavailable.
    if [ -z "${DEVICE_ID:-}" ]; then
        # 2026-09-03: each of these pipelines legitimately finds ZERO matches when no device is
        # awake/connected -- that's the expected "not found yet" case the code below handles. But
        # under `set -e -o pipefail`, a no-match grep's nonzero exit propagates through the pipe and
        # aborts the WHOLE SCRIPT right here (silently, skipping the "available" fallback AND the
        # explicit error message meant to handle exactly this) instead of falling through to them.
        # `|| true` on each assignment keeps the intended fallback logic reachable.
        _devs=$(xcrun devicectl list devices 2>/dev/null | grep -i "vision" | grep -i "physical") || true
        DEV_ID=$(printf '%s\n' "$_devs" | grep -i "connected"          | grep -oE '[0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}' | head -1) || true
        [ -z "$DEV_ID" ] && { DEV_ID=$(printf '%s\n' "$_devs" | grep -i "available" | grep -vi "unavailable" | grep -oE '[0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}' | head -1) || true; }
        if [ -z "$DEV_ID" ]; then
            echo "[ue-avp-build] ERROR: no connected/available visionOS device found. Pass DEVICE_ID=<udid> explicitly." >&2
            echo "[ue-avp-build] devices seen:" >&2; printf '%s\n' "$_devs" >&2
        else
            echo "[ue-avp-build] auto-detected device: $DEV_ID"
        fi
    else
        DEV_ID="$DEVICE_ID"
    fi
    echo "built (Agile Lens $DEVICE_TEAM): $APP"
    if [ "$INSTALL_DEVICE" = "1" ]; then
        if [ "$DEVICE_ACTIVITY_CHECKED" != "1" ]; then
            echo "[ue-avp-build] REFUSING INSTALL: set DEVICE_ACTIVITY_CHECKED=1 only after checking current wearer/capture activity." >&2
            exit 1
        fi
        [ -n "$DEV_ID" ] || { echo "[ue-avp-build] ERROR: install requested but no device is available." >&2; exit 1; }
        xcrun devicectl device install app --device "$DEV_ID" "$APP"
        echo "→ installed; wear the AVP and TAP the app icon (no remote launch for immersive apps)"
    else
        echo "→ packaged only; install requires INSTALL_DEVICE=1 DEVICE_ACTIVITY_CHECKED=1"
    fi ;;
  help|*)
    echo "usage: $0 sim|device   — identical render config for both; only arch/signing/install differ" ;;
esac

