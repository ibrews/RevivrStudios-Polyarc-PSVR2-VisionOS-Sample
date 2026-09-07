#!/bin/bash
# Produce the unsigned-build/cooked/signed 0.0.13 M5 candidate after the exact
# eb473 recovery receipt exists. The outer invocation acquires and maintains the
# Unreal build-lane lease; the inner invocation owns the protected pipeline.
set -euo pipefail

ENGINE="${ENGINE:-/Users/Shared/GH/UnrealEngineVisionOS/.claude/worktrees/ue582-xr-main-m5}"
PROJECT_ROOT="${PROJECT_ROOT:-/Users/Shared/GH/_deliveries/Lumenwork}"
PROJECT="$PROJECT_ROOT/Lumenwork.uproject"
EXPECTED_ENGINE="eb47362c29cc1cde71afeb6bd885c86ff7e8127a"
RECOVERY_RECEIPT="${RECOVERY_RECEIPT:-$HOME/knowledge/fleet/receipts/engine-recovery-eb473.done}"
LEASE="${LEASE:-$HOME/.claude/hooks/ue-build-lane.sh}"
OWNER_SESSION="${OWNER_SESSION:-01a07a55-22e0-7a81-a1c8-803646e22ed4}"
OWNER_TASK="${OWNER_TASK:-Lumenwork overnight M5 delivery}"
RESERVE_GIB="${RESERVE_GIB:-8}"
ENTITLEMENTS="${ENTITLEMENTS:-/private/tmp/lumenwork-myproject.entitlements}"
CERTIFICATE="${CERTIFICATE:-AAAD77542C2299171B729CDCC518F46FF9646F73}"
EXPECTED_BUNDLE_ID="com.agilelens.lumenwork.MyProject"

require_recovery_receipt() {
  test -f "$RECOVERY_RECEIPT" || { echo "RECOVERY_NOT_RELEASED=$RECOVERY_RECEIPT" >&2; exit 75; }
  receipt_engine="$(awk '$1 == "engine_commit:" { print $2 }' "$RECOVERY_RECEIPT")"
  printf '%s\n' "$receipt_engine" | grep -Eq '^[0-9a-f]{12,40}$' || {
    echo "RECOVERY_RECEIPT_INVALID_ENGINE=$receipt_engine" >&2; exit 76;
  }
  case "$EXPECTED_ENGINE" in
    "$receipt_engine"*) ;;
    *) echo "RECOVERY_RECEIPT_WRONG_ENGINE=$receipt_engine" >&2; exit 76 ;;
  esac
}

if [ "${1:-}" != "--inner" ]; then
  require_recovery_receipt
  test -f "$LEASE"
  python3 "$PROJECT_ROOT/Build/Delivery/guarded_ubt.py" --identity-only
  RUN_ID="${RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
  export ENGINE PROJECT_ROOT RECOVERY_RECEIPT LEASE OWNER_SESSION OWNER_TASK
  export RESERVE_GIB ENTITLEMENTS CERTIFICATE RUN_ID
  exec bash "$LEASE" run \
    --owner-session "$OWNER_SESSION" \
    --owner-task "$OWNER_TASK" \
    --project "$PROJECT_ROOT" \
    --engine "$ENGINE" \
    --purpose "Lumenwork 0.0.13 M5 Lumen Lite cook package sign" \
    --reserve-gib "$RESERVE_GIB" \
    -- bash "$0" --inner
fi

require_recovery_receipt
python3 "$PROJECT_ROOT/Build/Delivery/guarded_ubt.py" --identity-only
test "$(git -C "$ENGINE" rev-parse HEAD)" = "$EXPECTED_ENGINE"
test -x "$ENGINE/Engine/Binaries/Mac/UnrealEditor"
test -f "$PROJECT_ROOT/Binaries/VisionOS/Lumenwork"
test -f "$ENTITLEMENTS"

RUN_ROOT="/Users/Shared/GH/_deliveries/artifacts/lumenwork-m5-$RUN_ID"
LOG_ROOT="/Users/Shared/GH/_deliveries/logs/lumenwork-m5-$RUN_ID"
test ! -e "$RUN_ROOT"
test ! -e "$LOG_ROOT"
mkdir -p "$RUN_ROOT" "$LOG_ROOT"
LOG="$LOG_ROOT/package.log"
exec > >(tee -a "$LOG") 2>&1

# Arm a host-local, descriptor-driven watchdog. It observes this shell and the
# terminal marker without consuming an agent turn.
WATCHDOG="$HOME/knowledge/departments/engineering/fleet-tools/overnight-watchdog.ps1"
if command -v pwsh >/dev/null 2>&1 && [ -f "$WATCHDOG" ]; then
  python3 - "$LOG_ROOT/watchdog.json" "$LOG" "$$" "$RUN_ID" <<'PY'
import json, sys
from datetime import datetime, timezone
path, log, pid, run_id = sys.argv[1:]
data = {
    "Label": f"Lumenwork M5 package {run_id}",
    "LogPath": log,
    "ProcessId": int(pid),
    "DonePattern": "DELIVERY_COMPLETE=",
    "StallMinutes": 45,
    "ExpectedMinMinutes": 20,
    "StartTime": datetime.now(timezone.utc).isoformat(),
}
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2)
PY
  nohup pwsh -NoProfile -ExecutionPolicy Bypass -File "$WATCHDOG" \
    -Descriptor "$LOG_ROOT/watchdog.json" -Watch -PollSeconds 600 \
    > "$LOG_ROOT/watchdog.log" 2>&1 &
  echo "WATCHDOG_PID=$!"
fi

echo "DELIVERY_BEGIN=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "RUN_ID=$RUN_ID"
echo "ENGINE_HEAD=$EXPECTED_ENGINE"
echo "SOURCE_HEAD=$(git -C "$PROJECT_ROOT" rev-parse HEAD)"

engine_manifest="$ENGINE/Engine/Binaries/Mac/UnrealEditor.modules"
project_manifest="$PROJECT_ROOT/Binaries/Mac/UnrealEditor.modules"
engine_build_id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["BuildId"])' "$engine_manifest")"
project_build_id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["BuildId"])' "$project_manifest")"
test "$engine_build_id" = "$project_build_id"
echo "EDITOR_BUILD_ID=$engine_build_id"

stats="$PROJECT_ROOT/Saved/Cooked/VisionOS/Lumenwork/Metadata/ShaderTypeStats-METAL_SM5_IOS.csv"
start_epoch="$(date +%s)"
if [ -f "$stats" ]; then
  cp "$stats" "$LOG_ROOT/ShaderTypeStats-METAL_SM5_IOS.pre-cook.csv"
fi

export NuGetAudit=false
"$ENGINE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$PROJECT" -target=Lumenwork -platform=VisionOS \
  -clientconfig=Development -skipbuild -cook -stage -pak \
  -stagingdirectory="$RUN_ROOT/stage" \
  -unrealexe="$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  -nocompileeditor -skipbuildeditor -nop4 -unattended -nopause -utf8output \
  -WaitForUATMutex -NoUBA -ubtargs=-NoUBA \
  -additionalcookeroptions=-DisablePlugins=ECABridge

test -f "$stats"
test "$(stat -f %m "$stats")" -ge "$start_epoch"
python3 "$PROJECT_ROOT/Build/Delivery/verify_lumen_lite_cook.py" "$stats"
cp "$stats" "$LOG_ROOT/ShaderTypeStats-METAL_SM5_IOS.csv"
echo "SHADER_STATS_SHA256=$(shasum -a 256 "$stats" | awk '{print $1}')"

utoc="$(find "$RUN_ROOT/stage" -type f -name '*.utoc' | head -n 1)"
test -n "$utoc"
"$ENGINE/Engine/Binaries/Mac/UnrealPak" "$utoc" -List > "$LOG_ROOT/iostore-list.txt"
grep -qi 'L_XRTemplateMovableTest' "$LOG_ROOT/iostore-list.txt"
grep -qi 'VRPawn_HandWorldControl' "$LOG_ROOT/iostore-list.txt"
echo "IOSTORE_UTOC_SHA256=$(shasum -a 256 "$utoc" | awk '{print $1}')"

workspace="$PROJECT_ROOT/Intermediate/ProjectFiles/Lumenwork_VisionOS_Lumenwork.xcworkspace"
UBT_NO_POST_DEPLOY=true /Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild build \
  -workspace "$workspace" -scheme Lumenwork -configuration Development \
  -destination generic/platform=visionOS \
  CODE_SIGN_ALLOW_ENTITLEMENTS_MODIFICATION=YES \
  ENABLE_USER_SCRIPT_SANDBOXING=NO CODE_SIGNING_ALLOWED=NO \
  CODE_SIGNING_REQUIRED=NO UE_XCODE_BUILD_MODE=PostBuildSync

app="$PROJECT_ROOT/Binaries/VisionOS/Lumenwork.app"
candidate="$RUN_ROOT/Lumenwork-0.0.13-M5-eb473.app"
artifact="$RUN_ROOT/Lumenwork-0.0.13-M5-eb473.zip"
test -f "$app/Lumenwork"
test -d "$app/cookeddata"
/usr/bin/ditto "$app" "$candidate"

plist="$candidate/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleShortVersionString 0.0.13' "$plist" 2>/dev/null || \
  /usr/libexec/PlistBuddy -c 'Add :CFBundleShortVersionString string 0.0.13' "$plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleVersion 13' "$plist" 2>/dev/null || \
  /usr/libexec/PlistBuddy -c 'Add :CFBundleVersion string 13' "$plist"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" = "$EXPECTED_BUNDLE_ID"
printf '%s\n' '-metalmobilesm5 -csvGpuStats -csvCaptureFrames=1800' > "$candidate/uecommandline.txt"

codesign --force --sign "$CERTIFICATE" --entitlements "$ENTITLEMENTS" \
  --generate-entitlement-der --timestamp=none "$candidate"
codesign --verify --deep --strict --verbose=2 "$candidate"
codesign -d --entitlements :- "$candidate" 2>&1 | grep -q "C624J4S2F8.$EXPECTED_BUNDLE_ID"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$candidate" "$artifact"

echo "CANDIDATE=$candidate"
echo "CANDIDATE_EXEC_SHA256=$(shasum -a 256 "$candidate/Lumenwork" | awk '{print $1}')"
echo "ARTIFACT=$artifact"
echo "ARTIFACT_BYTES=$(stat -f %z "$artifact")"
echo "ARTIFACT_SHA256=$(shasum -a 256 "$artifact" | awk '{print $1}')"
df -h /Users/Shared
echo "DELIVERY_COMPLETE=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
