#!/bin/bash
# Overnight: clean editor rebuild -> verify -> content -> bake both maps -> cook -> install to AVP.
# Launched detached (nohup) so it survives session resets. Poll /tmp/overnight_status.txt + /tmp/overnight_build.log
set -u
ENG=/Users/Shared/GH/UnrealEngineVisionOS
PROJ=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample
UPROJ="$PROJ/My_Project.uproject"
EDCMD="$ENG/Engine/Binaries/Mac/UnrealEditor-Cmd"
DID=2642855C-6B73-5D5B-9387-6B110E7A7CF3
LOG=/tmp/overnight_build.log
STATUS=/tmp/overnight_status.txt
DISK_MIN_GB=8

say(){ echo "[$(date '+%m-%d %H:%M:%S')] $*" | tee -a "$LOG"; }
setstatus(){ echo "[$(date '+%H:%M:%S')] $*" > "$STATUS"; say "STATUS: $*"; }
freegb(){ df -g /Users/Shared 2>/dev/null | awk 'NR==2{print $4}'; }
checkdisk(){ local f; f=$(freegb); if [ "${f:-99}" -lt "$DISK_MIN_GB" ]; then setstatus "ABORTED stageX: disk low (${f}G<${DISK_MIN_GB}G)"; exit 90; fi; }

cd "$ENG" || { setstatus "FAILED: cd $ENG"; exit 1; }
say "================ OVERNIGHT BUILD START $(date) ================"
say "branch=$(git rev-parse --abbrev-ref HEAD)  free=$(freegb)G"

# ---- Stage 1: CLEAN editor rebuild (NOT incremental) ----
setstatus "STAGE1 editor clean rebuild (rm + Build.sh -MaxParallelActions=6, ~1-3h)"
rm -rf Engine/Intermediate/Build/Mac/arm64/UnrealEditor
rm -f  Engine/Binaries/Mac/UnrealEditor*.dylib Engine/Binaries/Mac/UnrealEditor Engine/Binaries/Mac/UnrealEditor-Cmd
say "removed stale intermediate+binaries; free now=$(freegb)G"
checkdisk
say "starting Build.sh UnrealEditor ..."
Engine/Build/BatchFiles/Mac/Build.sh UnrealEditor Mac Development -Architecture=arm64 -MaxParallelActions=6 >> "$LOG" 2>&1
RC=$?
say "editor Build.sh exit=$RC  free=$(freegb)G"
if [ $RC -ne 0 ] || [ ! -x "$EDCMD" ]; then setstatus "FAILED: editor build rc=$RC (binary present=$([ -x "$EDCMD" ] && echo yes || echo no))"; exit 1; fi

# ---- Stage 2: VERIFY editor (gate — do not bake/cook a broken editor) ----
setstatus "STAGE2 verify editor (LogPython)"
echo 'import unreal' > /tmp/ping.py
echo 'unreal.log_warning("PING_OK_PYTHON_WORKS")' >> /tmp/ping.py
"$EDCMD" "$UPROJ" -run=pythonscript -script=/tmp/ping.py -unattended -nopause -nosplash -stdout > /tmp/ping.log 2>&1
if grep -q "PING_OK_PYTHON_WORKS" /tmp/ping.log || grep -qi "LogPython" /tmp/ping.log; then
  say "editor VERIFIED — Python/commandlets work again"
else
  setstatus "FAILED: editor still broken after rebuild (no LogPython in /tmp/ping.log)"; exit 2
fi

# ---- Stage 3: content (glass-look material + material audit) ----
setstatus "STAGE3 content prep (glass-look + material audit)"
"$EDCMD" "$UPROJ" -run=pythonscript -script=/tmp/content_prep.py -stdout -unattended -nopause -nosplash -NoLogTimes >> "$LOG" 2>&1
say "content_prep exit=$?"
checkdisk

# ---- Stage 4: bake BOTH maps (Slate guard + Swarm loopback workaround) ----
setstatus "STAGE4 bake both maps (build_light_maps MEDIUM)"
"$EDCMD" "$UPROJ" -run=pythonscript -script=/tmp/bake_both.py \
  -Messaging -UDPMESSAGING_TRANSPORT_UNICAST=0.0.0.0:6677 \
  -AllowCommandletRendering -stdout -unattended -nopause -nosplash -NoLogTimes >> "$LOG" 2>&1
say "bake exit=$?  free=$(freegb)G"
say "TravelTestMap_BuiltData md5=$(md5 -q "$PROJ/Content/VRTemplate/Maps/TravelTestMap_BuiltData.uasset" 2>/dev/null) (baseline 6196ba7f...)"
say "VRTemplateMap_BuiltData md5=$(md5 -q "$PROJ/Content/VRTemplate/Maps/VRTemplateMap_BuiltData.uasset" 2>/dev/null)"
checkdisk

# ---- Stage 5: cook + package VisionOS (AllowStaticLighting=True -> slow shader recompile) ----
setstatus "STAGE5 cook+package VisionOS (BuildCookRun -build)"
Engine/Build/BatchFiles/RunUAT.sh BuildCookRun -project="$UPROJ" -platform=VisionOS -build -cook -stage -pak -package -archive -archivedirectory="$PROJ/Archive" -clientconfig=Development >> "$LOG" 2>&1
RC=$?
say "cook BuildCookRun exit=$RC  free=$(freegb)G"
if [ $RC -ne 0 ]; then setstatus "FAILED: cook rc=$RC (see log)"; exit 5; fi

# ---- Stage 6: install to AVP (retry on transient 3002) ----
setstatus "STAGE6 install to AVP"
INSTALLED=no
for a in 1 2 3; do
  if xcrun devicectl device install app --device "$DID" "$PROJ/Archive/VisionOS/My_Project.app" >> "$LOG" 2>&1; then
    say "installed to AVP (attempt $a)"; INSTALLED=yes; break
  fi
  say "install attempt $a failed; retry in 8s"; sleep 8
done

setstatus "DONE $(date) (installed=$INSTALLED, free=$(freegb)G)"
say "================ OVERNIGHT BUILD COMPLETE (installed=$INSTALLED) ================"
