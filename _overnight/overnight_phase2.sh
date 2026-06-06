#!/bin/bash
# Phase 2 — runs AFTER the master's clean UnrealEditor (engine) build completes.
# Builds the GAME editor target (so the project loads with its C++ module), then
# content(hand fix+glass) -> bake both maps -> cook -> install the newest .app (Pinchwork.app).
set -u
ENG=/Users/Shared/GH/UnrealEngineVisionOS
PROJ=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample
UPROJ="$PROJ/My_Project.uproject"
EDCMD="$ENG/Engine/Binaries/Mac/UnrealEditor-Cmd"
DID=2642855C-6B73-5D5B-9387-6B110E7A7CF3
LOG=/tmp/overnight_build.log
STATUS=/tmp/overnight_status.txt
say(){ echo "[$(date '+%m-%d %H:%M:%S')] P2: $*" | tee -a "$LOG"; }
setstatus(){ echo "[$(date '+%H:%M:%S')] P2 $*" > "$STATUS"; say "STATUS: $*"; }
freegb(){ df -g /Users/Shared 2>/dev/null | awk 'NR==2{print $4}'; }
checkdisk(){ local f; f=$(freegb); if [ "${f:-99}" -lt 6 ]; then setstatus "P2 ABORTED: disk low (${f}G)"; exit 90; fi; }

cd "$ENG" || { setstatus "P2 FAILED cd"; exit 1; }
say "================ PHASE 2 START $(date) free=$(freegb)G ================"

# 1. GAME editor target (consistent BuildId w/ freshly-built engine editor; -WaitMutex for safety)
setstatus "P2-1 build My_ProjectEditor (game editor target, -MaxParallelActions=6)"
Engine/Build/BatchFiles/Mac/Build.sh My_ProjectEditor Mac Development -project="$UPROJ" -Architecture=arm64 -MaxParallelActions=6 -WaitMutex >> "$LOG" 2>&1
RC=$?; say "My_ProjectEditor build exit=$RC free=$(freegb)G"
if [ $RC -ne 0 ] || [ ! -x "$EDCMD" ]; then setstatus "P2 FAILED: My_ProjectEditor build rc=$RC"; exit 1; fi
checkdisk

# 2. verify editor + that the game C++ module now loads
setstatus "P2-2 verify editor + game module"
cat > /tmp/ping2.py <<'PYEOF'
import unreal
unreal.log_warning("PING_OK_PYTHON_WORKS")
unreal.log_warning("GAME_OK=%s" % hasattr(unreal, "HandTrackingComponent"))
PYEOF
"$EDCMD" "$UPROJ" -run=pythonscript -script=/tmp/ping2.py -unattended -nopause -nosplash -stdout > /tmp/ping2.log 2>&1
if ! grep -q "PING_OK_PYTHON_WORKS" /tmp/ping2.log; then setstatus "P2 FAILED: editor commandlet still broken (no LogPython)"; exit 2; fi
GOK=$(grep -o 'GAME_OK=[A-Za-z]*' /tmp/ping2.log | head -1)
say "editor verified ($GOK)"
if echo "$GOK" | grep -qi "False"; then setstatus "P2 STOP: game module NOT loaded ($GOK) — will NOT bake/save maps (corruption risk). Investigate My_ProjectEditor build."; exit 3; fi

# 3. content: hand fix (M_Wood_Walnut skeletal flag) + glass-look on statue + material audit
setstatus "P2-3 content (hand fix + glass + audit)"
"$EDCMD" "$UPROJ" -run=pythonscript -script=/tmp/content_prep.py -stdout -unattended -nopause -nosplash -NoLogTimes >> "$LOG" 2>&1
say "content exit=$?"
checkdisk

# 4. bake both maps (Slate guard + Swarm loopback)
setstatus "P2-4 bake both maps"
"$EDCMD" "$UPROJ" -run=pythonscript -script=/tmp/bake_both.py -Messaging -UDPMESSAGING_TRANSPORT_UNICAST=0.0.0.0:6677 -AllowCommandletRendering -stdout -unattended -nopause -nosplash -NoLogTimes >> "$LOG" 2>&1
say "bake exit=$? free=$(freegb)G"
say "TravelTestMap_BuiltData md5=$(md5 -q "$PROJ/Content/VRTemplate/Maps/TravelTestMap_BuiltData.uasset" 2>/dev/null) (baseline 6196ba7f)"
say "VRTemplateMap_BuiltData md5=$(md5 -q "$PROJ/Content/VRTemplate/Maps/VRTemplateMap_BuiltData.uasset" 2>/dev/null)"
checkdisk

# 5. cook + package VisionOS
setstatus "P2-5 cook+package VisionOS (-build)"
Engine/Build/BatchFiles/RunUAT.sh BuildCookRun -project="$UPROJ" -platform=VisionOS -build -cook -stage -pak -package -archive -archivedirectory="$PROJ/Archive" -clientconfig=Development >> "$LOG" 2>&1
RC=$?; say "cook exit=$RC free=$(freegb)G"
if [ $RC -ne 0 ]; then setstatus "P2 FAILED: cook rc=$RC"; exit 5; fi

# 6. install newest .app (Pinchwork.app after the rename)
setstatus "P2-6 install to AVP"
APP=$(ls -dt "$PROJ"/Archive/VisionOS/*.app 2>/dev/null | head -1)
say "installing newest: $APP"
INSTALLED=no
for a in 1 2 3; do
  if xcrun devicectl device install app --device "$DID" "$APP" >> "$LOG" 2>&1; then say "installed $(basename "$APP") (attempt $a)"; INSTALLED=yes; break; fi
  say "install attempt $a failed; retry 8s"; sleep 8
done
setstatus "ALL DONE $(date) installed=$INSTALLED app=$(basename "$APP") free=$(freegb)G"
say "================ PHASE 2 COMPLETE installed=$INSTALLED ================"
