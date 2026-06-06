import unreal

def L(m):
    unreal.log_warning("PREP >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

# --- SAFEGUARD: never SAVE a map unless the game C++ module loaded. If the editor opened the
# project with a stale/mismatched game module (BuildId mismatch after an engine rebuild), the
# game-class actors are 'missing' and saving the map would DROP them (the 599 lines of uncommitted
# feature work). Material edits (stock assets) are always safe; only map saves are guarded. ---
GAME_CLASSES = ["HandTrackingComponent", "HandSkeletalDriverComponent", "GamepadInputSetup"]
GAME_OK = any(hasattr(unreal, c) for c in GAME_CLASSES)
L("game module loaded = %s (checked %s)" % (GAME_OK, GAME_CLASSES))

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous(["/Game"], True)

# ---------- 1. PENDING-BATCH hand fix: M_Wood_Walnut used_with_skeletal_mesh (stock, always safe) ----------
try:
    wp = "/Game/StarterContent/Materials/M_Wood_Walnut"
    wm = unreal.load_asset(wp)
    if wm:
        b = wm.get_editor_property("used_with_skeletal_mesh")
        wm.set_editor_property("used_with_skeletal_mesh", True)
        MEL.recompile_material(wm)
        EAL.save_asset(wp)
        L("hand fix: M_Wood_Walnut used_with_skeletal_mesh %s -> True, saved" % b)
    else:
        L("hand fix: M_Wood_Walnut MISSING")
except Exception as e:
    L("hand fix FAILED: %s" % e)

# ---------- 2. opaque reflective glass-look material (stock, always safe) ----------
PATH = "/Game/M_GlassLookOpaque"
glass = None
try:
    if EAL.does_asset_exist(PATH): EAL.delete_asset(PATH)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset("M_GlassLookOpaque", "/Game", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    bc = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -520, -220)
    bc.set_editor_property("constant", unreal.LinearColor(0.02, 0.06, 0.08, 1.0))
    MEL.connect_material_property(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -520, 0)
    rough.set_editor_property("r", 0.06); MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -520, 120)
    spec.set_editor_property("r", 1.0); MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    try:
        fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -760, 280)
        try: fres.set_editor_property("exponent", 4.0); fres.set_editor_property("base_reflect_fraction", 0.04)
        except Exception: pass
        rim = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -760, 430)
        rim.set_editor_property("constant", unreal.LinearColor(0.25, 0.70, 1.0, 1.0))
        mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -360, 330)
        MEL.connect_material_expressions(fres, "", mul, "A"); MEL.connect_material_expressions(rim, "", mul, "B")
        MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    except Exception as e: L("fresnel rim skip: %s" % e)
    MEL.recompile_material(mat); EAL.save_asset(PATH)
    glass = unreal.load_asset(PATH)
    L("created %s" % PATH)
except Exception as e:
    L("glass material FAILED: %s" % e)

# ---------- 3. assign glass to the statue + save TravelTestMap (GUARDED) ----------
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if GAME_OK and glass:
    les.load_level("/Game/VRTemplate/Maps/TravelTestMap")
    n = 0
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.StaticMeshActor):
            lab = a.get_actor_label(); smc = a.static_mesh_component; mesh = smc.static_mesh
            mn = mesh.get_name() if mesh else ""
            if "statue" in lab.lower() or "statue" in mn.lower():
                for i in range(max(1, smc.get_num_materials())): smc.set_material(i, glass)
                L("assigned M_GlassLookOpaque to %s" % lab); n += 1
    les.save_current_level()
    L("glass assigned to %d statue(s); TravelTestMap SAVED" % n)
else:
    L("SKIP statue glass-assign+save (GAME_OK=%s glass=%s) — protecting map from missing-class drop" % (GAME_OK, glass is not None))

# ---------- 4. material audit (read-only loads, no save) ----------
SUSPECT = ("DefaultMaterial", "WorldGridMaterial", "DefaultDeferredDecal", "Default__")
for mp in ["/Game/VRTemplate/Maps/TravelTestMap", "/Game/VRTemplate/Maps/VRTemplateMap"]:
    les.load_level(mp)
    bad = 0
    for a in eas.get_all_level_actors():
        try: comps = a.get_components_by_class(unreal.MeshComponent)
        except Exception: comps = []
        for c in comps:
            try: nm = c.get_num_materials()
            except Exception: nm = 0
            for i in range(nm):
                m = c.get_material(i); name = m.get_name() if m else "NULL"
                if (m is None) or any(s in name for s in SUSPECT):
                    bad += 1; L("  BROKEN? %s.%s slot%d = %s" % (a.get_actor_label(), c.get_name(), i, name))
    L("AUDIT %s: %d suspect slots" % (mp, bad))
L("CONTENT_PREP DONE (GAME_OK=%s)" % GAME_OK)
