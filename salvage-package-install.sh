#!/bin/bash
# salvage-package-install.sh — Lumenwork: finish a build that died on the Xcode
# "Touch UBT generated tiles" finalize flake, WITHOUT recompiling.
#
# WHY THIS EXISTS: ue-avp-build.sh's own SKIP_BUILD path DROPS -package (see its
# comments ~line 181), so it only ever stages — you get no installable .app.
# This runs -skipbuild WITH -package, the same shape as the FSLA theater project's
# fallback-cook-package-install.sh, which has salvaged this identical flake 3x.
#
# The device binary is already linked when this runs; only Xcode's finalize failed.
set -uo pipefail
W=/Users/Shared/GH/Lumenwork
E=/Users/Shared/GH/UnrealEngineVisionOS/.claude/worktrees/ue582-xr-main-m5
LEASE=/Users/alex/.claude/hooks/ue-build-lane.sh
SESSION="${1:-87542a0c-3524-4a16-aa45-1e690a4b8c7b}"
LOG="$W/Saved/BuildLogs/lumenwork-salvage-$(date +%s).log"
mkdir -p "$W/Saved/BuildLogs"
ts(){ date "+%Y-%m-%d %H:%M:%S %Z"; }

echo "[$(ts)] salvage starting; binary: $(ls -la "$W/Binaries/VisionOS/Lumenwork" 2>/dev/null | awk '{print $5, $6, $7, $8}')"

bash "$LEASE" run --owner-session "$SESSION" --owner-task "lumenwork-salvage-package" \
  --project "$W" --engine "$E" \
  --purpose "skipbuild+package salvage of the Touch UBT generated tiles finalize flake" \
  --reserve-gib 5 -- \
  bash "$E/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
    -project="$W/Lumenwork.uproject" -target=Lumenwork -platform=VisionOS \
    -clientconfig=Development -cook -stage -pak -package -skipbuild -IgnoreCookErrors \
    -nop4 -unattended -nopause -utf8output -WaitForUATMutex \
    "-ini:Engine:[/Script/MacTargetPlatform.XcodeProjectSettings]:CodeSigningTeam=C624J4S2F8" \
  >> "$LOG" 2>&1
RC=$?
echo "[$(ts)] UAT rc=$RC log=$LOG"

APP="$W/Binaries/VisionOS/Lumenwork.app"
if [ $RC -eq 0 ]; then
  # 2026-09-17: HARD GATE. This script once packaged a Sep 13 Zen oplog into a Sep 17 binary and
  # installed it -- valid, signed, and four days stale. -skipbuild/-package does NOT guarantee a
  # cook ran. Refuse rather than ship content older than the assets it claims to contain.
  if ! bash ~/knowledge/scripts/check-cook-freshness.sh "$W" VisionOS; then
    echo "[$(ts)] REFUSING TO INSTALL: stale cooked data (see above). Force a real cook first."
    exit 93
  fi
  SZ=$(du -sk "$APP" 2>/dev/null | awk '{print $1}')
  echo "[$(ts)] .app size: ${SZ} KB"
  if [ "${SZ:-0}" -lt 50000 ]; then echo "[$(ts)] REFUSING: .app under 50MB, not a real bundle"; exit 91; fi
  file "$APP/Lumenwork" | grep -q "Mach-O" || { echo "[$(ts)] REFUSING: executable is not Mach-O"; exit 92; }
  echo "[$(ts)] installing to AVP"
  # NOTE: capture devicectl's OWN status, not the pipeline's. Piping to `tail` makes $? the
  # tail exit code, which is 0 even when the install failed -- this script reported rc=0 on a
  # real CoreDeviceError 4016 failure on 2026-09-17 because of exactly that.
  INSTALL_OUT="$W/Saved/BuildLogs/install-$(date +%s).log"
  xcrun devicectl device install app --device 00008142-000468E83409401C "$APP" > "$INSTALL_OUT" 2>&1
  IRC=$?
  tail -8 "$INSTALL_OUT"
  echo "[$(ts)] devicectl rc=$IRC"
  [ $IRC -ne 0 ] && { echo "[$(ts)] INSTALL FAILED -- do not report success"; exit $IRC; }
  echo "[$(ts)] INSTALLED"
else
  echo "[$(ts)] FAILED rc=$RC — real error lines:"
  grep -aiE "^ERROR|BUILD FAILED|Touch UBT" "$LOG" 2>/dev/null | tail -5
fi
