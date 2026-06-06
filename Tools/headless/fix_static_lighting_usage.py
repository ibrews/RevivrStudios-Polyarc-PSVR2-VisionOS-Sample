import unreal

# WHY: this session enabled static/baked lighting (r.AllowStaticLighting=True). Materials that
# were created headlessly (M_TriPlanar -> MI_WA_*, M_GlassLookOpaque, the Fab sci-fi cube Default)
# never had bUsedWithStaticLighting=True saved on the asset, so the cook omits the static-lighting
# shader permutation -> on-device the static-lit meshes fall back to the Default Material
# (WorldGrid). The editor sets the flag on-the-fly so it looks fine in-editor only.
# FIX: set used_with_static_lighting=True on every /Game BASE material referenced by either map
# (usage flags live on UMaterial; instances inherit), recompile + save. Also fold in the pending
# hand fix (used_with_skeletal_mesh on M_Wood_Walnut). Does NOT save levels (keeps the bakes).

def L(m):
    unreal.log_warning("STATICFIX >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous(["/Game"], True)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
MEL = unreal.MaterialEditingLibrary

def base_material(m):
    seen = 0
    while m is not None and isinstance(m, unreal.MaterialInstance) and seen < 16:
        m = m.get_editor_property("parent")
        seen += 1
    return m if isinstance(m, unreal.Material) else None

bases = {}      # /Game base path -> UMaterial
engine_bases = set()
for mp in ["/Game/VRTemplate/Maps/VRTemplateMap", "/Game/VRTemplate/Maps/TravelTestMap"]:
    les.load_level(mp)
    L("scan level %s" % mp)
    for a in eas.get_all_level_actors():
        try: comps = a.get_components_by_class(unreal.MeshComponent)
        except Exception: comps = []
        for c in comps:
            try: n = c.get_num_materials()
            except Exception: n = 0
            for i in range(n):
                m = c.get_material(i)
                if m is None:
                    continue
                b = base_material(m)
                if b is None:
                    continue
                p = b.get_path_name()
                if p.startswith("/Game/"):
                    bases[p] = b
                else:
                    engine_bases.add(p)

# safety net: known-fallback + hand bases even if a mesh slot didn't enumerate them
for extra in ["/Game/M_GlassLookOpaque",
              "/Game/VRTemplate/Materials/M_TriPlanar",
              "/Game/Fab/Sci-Fi_Cube_01/sci_fi_cube_01/Materials/Default",
              "/Game/StarterContent/Materials/M_Wood_Walnut"]:
    if extra not in bases:
        mm = unreal.load_asset(extra)
        if isinstance(mm, unreal.Material):
            bases[extra] = mm

for p in sorted(engine_bases):
    L("ENGINE base (cannot resave; reskin mesh if it grids on-device): %s" % p)

L("=== %d unique /Game base materials to check ===" % len(bases))
changed = 0
for p, b in sorted(bases.items()):
    try:
        before = b.get_editor_property("used_with_static_lighting")
    except Exception as e:
        L("  %s : CANNOT READ used_with_static_lighting (%s)" % (p, e))
        continue
    if before:
        L("  %s : already True (skip)" % p)
        continue
    try:
        b.set_editor_property("used_with_static_lighting", True)
        after = b.get_editor_property("used_with_static_lighting")
        MEL.recompile_material(b)
        ok = unreal.EditorAssetLibrary.save_asset(p)
        L("  %s : static_lighting %s -> %s  save=%s" % (p, before, after, ok))
        changed += 1
    except Exception as e:
        L("  %s : SET/SAVE FAILED (%s)" % (p, e))

# pending hand fix: M_Wood_Walnut on the skinned hand needs the skeletal permutation too
ww_path = "/Game/StarterContent/Materials/M_Wood_Walnut"
ww = unreal.load_asset(ww_path)
if isinstance(ww, unreal.Material):
    try:
        if not ww.get_editor_property("used_with_skeletal_mesh"):
            ww.set_editor_property("used_with_skeletal_mesh", True)
            MEL.recompile_material(ww)
            unreal.EditorAssetLibrary.save_asset(ww_path)
            L("M_Wood_Walnut: used_with_skeletal_mesh -> True (saved)")
        else:
            L("M_Wood_Walnut: used_with_skeletal_mesh already True")
    except Exception as e:
        L("M_Wood_Walnut skeletal flag FAILED (%s)" % e)

L("STATICFIX DONE changed=%d engine_unfixable=%d" % (changed, len(engine_bases)))
