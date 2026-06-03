#!/usr/bin/env bash
# make_solidimagestack.sh — assemble a visionOS layered (parallax) app icon
# asset catalog from three 1024x1024 PNG layers.
#
# visionOS home-screen icons are NOT flat: they are a 3-layer "solidimagestack"
# (Front / Middle / Back) that the system parallaxes on gaze/hover. This script
# emits the exact catalog structure Xcode's own visionOS template uses, so
# `actool --platform xros` compiles it into a proper layered AppIcon.
#
# Usage:
#   make_solidimagestack.sh <out_Assets.xcassets> <back.png> <middle.png> <front.png>
#
# Layer order (Apple convention): Front is closest to the viewer, Back is farthest.
set -euo pipefail

OUT="${1:?output Assets.xcassets path required}"
BACK="${2:?back.png required}"
MID="${3:?middle.png required}"
FRONT="${4:?front.png required}"

STACK="$OUT/AppIcon.solidimagestack"
rm -rf "$STACK"
mkdir -p "$OUT"

# Catalog root
cat > "$OUT/Contents.json" <<'JSON'
{
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
JSON

# solidimagestack root: lists the layers front-to-back
mkdir -p "$STACK"
cat > "$STACK/Contents.json" <<'JSON'
{
  "info" : {
    "author" : "xcode",
    "version" : 1
  },
  "layers" : [
    { "filename" : "Front.solidimagestacklayer" },
    { "filename" : "Middle.solidimagestacklayer" },
    { "filename" : "Back.solidimagestacklayer" }
  ]
}
JSON

# Emit one layer: $1=LayerName  $2=source png  $3=basename in imageset
emit_layer() {
  local name="$1" src="$2" base="$3"
  local layer="$STACK/${name}.solidimagestacklayer"
  local iset="$layer/Content.imageset"
  mkdir -p "$iset"
  cat > "$layer/Contents.json" <<'JSON'
{
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
JSON
  cp -f "$src" "$iset/$base"
  cat > "$iset/Contents.json" <<JSON
{
  "images" : [
    {
      "filename" : "$base",
      "idiom" : "vision",
      "scale" : "2x"
    }
  ],
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
JSON
}

emit_layer Back   "$BACK"  back.png
emit_layer Middle "$MID"   middle.png
emit_layer Front  "$FRONT" front.png

echo "Wrote layered visionOS app icon -> $STACK"
find "$OUT" -type f | sort