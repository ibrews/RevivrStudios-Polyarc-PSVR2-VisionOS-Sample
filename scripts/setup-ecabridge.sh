#!/bin/bash
# Per-machine ECABridge setup (macOS/Linux). ECABridge is an Editor-only developer/MCP bridge --
# a TOOL, not project content -- so the project does not version it and Plugins/ECABridge is
# gitignored. Each machine links its own checkout here.
#
# Why not a committed symlink: it used to be one, pointing at /Users/Shared/GH/ECABridge. That
# broke macOS (dyld resolves the symlink before @loader_path, so UBT's baked relative rpaths
# walked into /Users/ and the module failed to load -- a warning in the editor, FATAL in a cook)
# and Windows (git checked it out as a 26-byte text stub: "Unable to find plugin 'ECABridge'").
# See intelligence/decisions/2026-09-10-no-committed-absolute-path-symlinks.md
set -euo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${ECABRIDGE_SRC:-/Users/Shared/GH/ECABridge}"
DST="$PROJ/Plugins/ECABridge"
[ -d "$SRC" ] || { echo "ECABridge checkout not found at $SRC. Clone it, or set ECABRIDGE_SRC." >&2; exit 3; }
[ -e "$DST" ] && { echo "Plugins/ECABridge already exists -- leaving it alone."; exit 0; }
ln -s "$SRC" "$DST"
echo "linked $DST -> $SRC"
echo
echo "NOTE: a symlink still makes dyld resolve rpaths from the REAL path. If the module fails to"
echo "load, run ~/knowledge/scripts/fix-ecabridge-rpaths.sh (re-run after every ECABridge rebuild)."
echo "A real directory (git worktree of the ECABridge repo) avoids that entirely:"
echo "  git -C \"$SRC\" worktree add \"$DST\" <branch>"
echo
echo "Then enable it locally (do NOT commit this):"
echo "  set ECABridge Enabled=true in Lumenwork.uproject"
