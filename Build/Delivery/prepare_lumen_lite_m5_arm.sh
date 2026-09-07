#!/bin/bash
# Clone, configure, sign, and optionally install one evidence arm from the
# immutable M5 baseline candidate produced by package_lumen_lite_m5.sh.
set -euo pipefail

SOURCE_APP="${SOURCE_APP:?set SOURCE_APP to the preserved baseline .app}"
ARM="${1:?usage: prepare_lumen_lite_m5_arm.sh A|B|C70|C50|D|E}"
OUT_ROOT="${OUT_ROOT:-$(dirname "$SOURCE_APP")/arms}"
DEVICE_ID="${DEVICE_ID:-00008142-000468E83409401C}"
CERTIFICATE="${CERTIFICATE:-AAAD77542C2299171B729CDCC518F46FF9646F73}"
ENTITLEMENTS="${ENTITLEMENTS:-/private/tmp/lumenwork-myproject.entitlements}"
INSTALL_DEVICE="${INSTALL_DEVICE:-0}"
DEVICE_ACTIVITY_CHECKED="${DEVICE_ACTIVITY_CHECKED:-0}"
BUNDLE_ID="com.agilelens.lumenwork.MyProject"

case "$ARM" in
  A)   dpcvars='r.Lumen.FinalGatherMethod=1,xr.SecondaryScreenPercentage.HMDRenderTarget=100' ;;
  B)   dpcvars='r.Lumen.FinalGatherMethod=0,xr.SecondaryScreenPercentage.HMDRenderTarget=100' ;;
  C70) dpcvars='r.Lumen.FinalGatherMethod=0,r.ScreenPercentage=70,xr.SecondaryScreenPercentage.HMDRenderTarget=100' ;;
  C50) dpcvars='r.Lumen.FinalGatherMethod=0,r.ScreenPercentage=50,xr.SecondaryScreenPercentage.HMDRenderTarget=100' ;;
  D)   dpcvars="r.Lumen.FinalGatherMethod=0,r.ScreenPercentage=${SAFE_SCREEN_PERCENTAGE:-70},r.Lumen.Reflections.DownsampleFactor=2,xr.SecondaryScreenPercentage.HMDRenderTarget=100" ;;
  E)   dpcvars="r.Lumen.FinalGatherMethod=0,r.ScreenPercentage=${SAFE_SCREEN_PERCENTAGE:-70},r.Lumen.Reflections.DownsampleFactor=2,r.Lumen.IrradianceFieldGather.NumProbesToTraceBudget=50,xr.SecondaryScreenPercentage.HMDRenderTarget=100" ;;
  *) echo "unknown arm: $ARM" >&2; exit 2 ;;
esac

test -d "$SOURCE_APP"
test -f "$ENTITLEMENTS"
mkdir -p "$OUT_ROOT"
variant="$OUT_ROOT/Lumenwork-M5-$ARM.app"
test ! -e "$variant"
/usr/bin/ditto "$SOURCE_APP" "$variant"
printf '%s\n' "-metalmobilesm5 -csvGpuStats -csvCaptureFrames=1800 -dpcvars=$dpcvars" > "$variant/uecommandline.txt"
codesign --force --sign "$CERTIFICATE" --entitlements "$ENTITLEMENTS" \
  --generate-entitlement-der --timestamp=none "$variant"
codesign --verify --deep --strict --verbose=2 "$variant"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$variant/Info.plist")" = "$BUNDLE_ID"
echo "ARM=$ARM"
echo "DPCVARS=$dpcvars"
echo "APP=$variant"
echo "APP_EXEC_SHA256=$(shasum -a 256 "$variant/Lumenwork" | awk '{print $1}')"

if [ "$INSTALL_DEVICE" = "1" ]; then
  test "$DEVICE_ACTIVITY_CHECKED" = "1" || {
    echo "REFUSING_INSTALL: set DEVICE_ACTIVITY_CHECKED=1 only after current wearer/capture activity is checked" >&2
    exit 3
  }
  xcrun devicectl device install app --device "$DEVICE_ID" "$variant"
  echo "INSTALLED_DEVICE=$DEVICE_ID"
  echo "Launch the immersive app for this arm, then preserve its console/device CSV before preparing the next arm."
else
  echo "PREPARED_ONLY=1"
fi
