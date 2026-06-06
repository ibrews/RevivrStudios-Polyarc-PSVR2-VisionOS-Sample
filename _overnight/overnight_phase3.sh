#!/bin/bash
# Phase 3 (best-effort) — compile + package ACCVR24 for VisionOS on the freshly-rebuilt fork engine,
# using Pinchwork's key VisionOS Config settings. ACCVR24 is a big non-VisionOS VR project (Odin,
# OculusXR, MocopiLiveLink, ARKitFace, HairStrands...) so a VisionOS package may fail on a plugin —
# this attempts it and reports honestly. ACCVR24 is NOT git-tracked -> we back up Config first.
set -u
ENG=/Users/Shared/GH/UnrealEngineVisionOS
ACC=/Users/alex/dev/ACCVR24_Mac56
UPROJ="$ACC/ACCVR24.uproject"
PINCH=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample
LOG=/tmp/overnight_build.log
STATUS=/tmp/overnight_status.txt
say(){ echo "[$(date '+%m-%d %H:%M:%S')] P3: $*" | tee -a "$LOG"; }
setstatus(){ echo "[$(date '+%H:%M:%S')] P3 $*" > "$STATUS"; say "STATUS: $*"; }
freegb(){ df -g /Users/Shared 2>/dev/null | awk 'NR==2{print $4}'; }

say "================ PHASE 3 (ACCVR24) START $(date) free=$(freegb)G ================"
f=$(freegb); if [ "${f:-99}" -lt 9 ]; then setstatus "P3 SKIPPED: disk too low (${f}G) to safely build ACCVR24 (23G project)"; exit 0; fi
[ -f "$UPROJ" ] || { setstatus "P3 SKIPPED: $UPROJ not found"; exit 0; }

# 1. back up ACCVR24 Config (not git-tracked -> this backup is the only undo)
BK="$ACC/Config.bak.$(date +%Y%m%d_%H%M%S)"
cp -R "$ACC/Config" "$BK" && say "backed up ACCVR24 Config -> $BK"

# 2. apply Pinchwork key VisionOS config (additive — ACCVR24 had no Config/VisionOS)
mkdir -p "$ACC/Config/VisionOS"
cp "$PINCH/Config/VisionOS/VisionOSEngine.ini" "$ACC/Config/VisionOS/VisionOSEngine.ini"
cat >> "$ACC/Config/VisionOS/VisionOSEngine.ini" <<'EOF'

[/Script/IOSRuntimeSettings.IOSRuntimeSettings]
MinimumiOSVersion=IOS_17
AdditionalPlistData=<key>NSHandsTrackingUsageDescription</key><string>Track your hands to interact with the application.</string><key>NSAccessoryTrackingUsageDescription</key><string>Track spatial controllers for 6DOF input.</string><key>NSWorldSensingUsageDescription</key><string>Track your head position for 6DOF immersive experience.</string><key>UIApplicationSceneManifest</key><dict><key>UIApplicationPreferredDefaultSceneSessionRole</key><string>CPSceneSessionRoleImmersiveSpaceApplication</string><key>UIApplicationSupportsMultipleScenes</key><true/><key>UISceneConfigurations</key><dict><key>CPSceneSessionRoleImmersiveSpaceApplication</key><array><dict><key>UISceneInitialImmersionStyle</key><string>UIImmersionStyleMixed</string></dict></array></dict></dict><key>GCSupportsControllerUserInteraction</key><true/><key>GCSupportedGameControllers</key><array><dict><key>ProfileName</key><string>ExtendedGamepad</string></dict><dict><key>ProfileName</key><string>SpatialGamepad</string></dict></array>
EOF
say "applied Pinchwork VisionOS config (render/immersion CVars + IOS_17 + UIImmersionStyleMixed manifest)"

# 3. compile + cook + package on the fork engine (best effort; -build does editor+game compile)
setstatus "P3 ACCVR24 BuildCookRun VisionOS (-build, best-effort)"
"$ENG/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$UPROJ" -platform=VisionOS -build -cook -stage -pak -package -archive -archivedirectory="$ACC/Archive" -clientconfig=Development >> "$LOG" 2>&1
RC=$?; say "ACCVR24 BuildCookRun exit=$RC free=$(freegb)G"
APP=$(ls -dt "$ACC"/Archive/VisionOS/*.app 2>/dev/null | head -1)
if [ $RC -eq 0 ] && [ -n "$APP" ]; then
  setstatus "ALL DONE: ACCVR24 packaged -> $APP  (Config backup: $BK)"
else
  setstatus "ALL DONE: ACCVR24 BuildCookRun FAILED rc=$RC — likely a non-VisionOS plugin (OculusXR/Odin/ARKitFace/Mocopi). Original Config safe at $BK. See log."
fi
say "================ PHASE 3 COMPLETE rc=$RC ================"
