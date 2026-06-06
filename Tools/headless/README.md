# Tools/headless — UE 5.6.1 commandlet scripts (Pinchwork)

Reusable Python scripts driven through the headless editor (no interactive UI —
the editor + ECABridge are flaky on this Polyarc fork). Mirrored in the KB at
`~/knowledge/projects/pinchwork/scripts/`.

## How to run

```bash
ENGINE=/Users/Shared/GH/UnrealEngineVisionOS
UPROJECT=/Users/Shared/GH/RevivrStudios-Polyarc-PSVR2-VisionOS-Sample/My_Project.uproject

# design / reskin / fix (NO rendering flag):
"$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$UPROJECT" \
  -run=pythonscript -script="$(pwd)/design2.py" \
  -stdout -unattended -nopause -nosplash -NoLogTimes

# bake (ADD -AllowCommandletRendering):
"$ENGINE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$UPROJECT" \
  -run=pythonscript -script="$(pwd)/static_bake2.py" \
  -AllowCommandletRendering -stdout -unattended -nopause -nosplash -NoLogTimes
```

Gotchas (full list: KB `intelligence/techniques/headless-ue-python-commandlet-gotchas.md`):
- `spawn_actor_from_object` **crashes** natively → use `spawn_actor_from_class(StaticMeshActor)` + `set_static_mesh`.
- `does_asset_exist` is False until `AssetRegistryHelpers...scan_paths_synchronous([p], True)`.
- Bake needs `-AllowCommandletRendering`; spawning/reskin does not.
- `unreal.log_flush()` after each line → the last printed line pinpoints a native crash.
- Pythonscript commandlets sometimes get SIGTERM'd ~1 min in (env flakiness) — just re-run.

## Scripts

| Script | Purpose |
|---|---|
| `inventory_levels.py` | Dump every actor + material in both maps (read-only audit). |
| `verify.py` | Quick post-edit inventory sanity check. |
| `design2.py` | Duplicate + theme Level A (Cobalt Lab) / B (Stone Courtyard): spawn `LVLGEN_*` props, reskin floor/grid, place lights. Supersedes the crashed `design_levels.py` v1. |
| `assets_and_mats.py` | Build the triplanar master `M_TriPlanar` + reparent `MI_WA_*` instances (de-stretch floors/walls). |
| `fix_mat2.py` | Material-instance reparent/parameter fixups. |
| `fix_hand_material.py` | **PENDING — run before next cook.** Sets `used_with_skeletal_mesh=True` on `M_Wood_Walnut` so the forest hand stops falling back to WorldGridMaterial. |
| `glasslook.py` | Build interim opaque "glass-look" material `M_GlassLookOpaque` for the statue (writes depth → no passthrough blocky-alpha artifact). |
| `bake.py` | Bake both maps (legacy; superseded by `static_bake2.py`). |
| `static_bake2.py` | Set WorldSettings `force_no_precomputed_lighting=False` + Stationary lights + `build_light_maps` MEDIUM. **Only succeeds with a native-arm64 UnrealLightmass** (see KB `apple-silicon-ue-lightmass-arm64.md`). |
| `gen_icon.py` | Generate the 3-layer (front/middle/back) visionOS parallax app icon art. |
