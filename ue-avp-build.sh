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
# Drop into a UE visionOS project root; edit CONFIG (or a sibling ue-avp-build.config). Canonical copy lives in the
# KB (intelligence/techniques/scripts/). Design + rationale: intelligence/techniques/ue-visionos-sim-device-build-flow.md
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
# --------------------------------------------------------------------------------

UAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
export NuGetAudit=false   # clean-machine: Magick.NET 14.10.2 NU1902 is treated as a build error otherwise (KB gotcha)
STAGE_DIR="$(dirname "$PROJECT")/Saved/StagedBuilds/VisionOS"

bcr() {  # shared BuildCookRun — render config comes from the project's committed ini (the Mixed/Pinchwork config), UNTOUCHED
  "$UAT" BuildCookRun -project="$PROJECT" -target="$TARGET" -platform=VisionOS \
    -clientconfig="$CLIENTCONFIG" -build -cook -stage -pak -package -nop4 -unattended -nopause -utf8output "$@"
}
newest_app() { ls -dt "$STAGE_DIR"/*.app 2>/dev/null | head -1; }
pick_sim() { [ -n "$SIM_UDID" ] && { echo "$SIM_UDID"; return; }
  xcrun simctl list devices visionOS available | grep -oE '\(([0-9A-F-]{36})\)' | tr -d '()' | head -1; }

case "${1:-help}" in
  sim)
    # Sim deltas (NONE are render config): arch=iossimulator + cook METAL_SIM shaders. The sim-shader flags are
    # injected via -ini: AT BUILD TIME (not a file edit) so the committed config can stay lean for device cooks.
    bcr -clientarchitecture=iossimulator \
      "-ini:Engine:[/Script/IOSRuntimeSettings.IOSRuntimeSettings]:bEnableSimulatorSupport=True" \
      "-ini:Engine:[/Script/IOSRuntimeSettings.IOSRuntimeSettings]:bSupportAppleA8=True"
    APP="$(newest_app)"; DEV="$(pick_sim)"; [ -z "$DEV" ] && { echo "no visionOS sim found"; exit 1; }
    BUNDLE_ID="${BUNDLE_ID:-$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Info.plist" 2>/dev/null)}"
    xcrun simctl boot "$DEV" 2>/dev/null || true; open -a Simulator >/dev/null 2>&1 || true
    xcrun simctl terminate "$DEV" "$BUNDLE_ID" 2>/dev/null || true
    xcrun simctl install "$DEV" "$APP"
    xcrun simctl launch "$DEV" "$BUNDLE_ID"
    echo "→ launched $BUNDLE_ID in sim $DEV (Mixed/passthrough — same render config as device)" ;;
  device)
    # Device = arm64 (no -clientarchitecture). Agile Lens signing. Immersive apps can't be remote-launched → tap.
    bcr "-ini:Engine:[/Script/MacTargetPlatform.XcodeProjectSettings]:CodeSigningTeam=$DEVICE_TEAM"
    APP="$(newest_app)"
    DEV_ID="${DEVICE_ID:-$(xcrun devicectl list devices 2>/dev/null | awk 'tolower($0)~/vision/{print $1}' | head -1)}"
    echo "built (Agile Lens $DEVICE_TEAM): $APP"
    if [ -n "$DEV_ID" ]; then xcrun devicectl device install app --device "$DEV_ID" "$APP"
    else echo "set DEVICE_ID, then: xcrun devicectl device install app --device <id> '$APP'"; fi
    echo "→ wear the AVP and TAP the app icon (no remote launch for immersive apps)" ;;
  help|*)
    echo "usage: $0 sim|device   — identical render config for both; only arch/signing/install differ" ;;
esac
