import unreal
def L(m):
    unreal.log_warning("ASSET >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

AT  = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# ============================================================
# PART A: gesture Input Actions (duplicate an existing digital bool IA)
# ============================================================
L("===== PART A: Input Actions =====")
IA_DIR = "/Game/VRTemplate/Input/Actions/Hands"
SRC_IA = "/Game/VRTemplate/Input/Actions/IA_Grab_Right"   # digital bool grab action = good template
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
    ["/Game/VRTemplate/Input/Actions"], True)
for base in ["IA_IndexThumbPinch", "IA_MiddleThumbPinch", "IA_PinkyThumbPinch"]:
    for side in ["Left", "Right"]:
        name = "%s_%s" % (base, side)
        full = "%s/%s" % (IA_DIR, name)
        if EAL.does_asset_exist(full):
            L("  exists %s" % name); continue
        ok = EAL.duplicate_asset(SRC_IA, full)
        if ok:
            EAL.save_asset(full); L("  created %s" % name)
        else:
            L("  FAILED %s" % name)

# ============================================================
# PART B: world-aligned (triplanar) master material — kills UV stretch on the
# big SM_Cube floor/walls (their 0-1 UVs smear textures when scaled 20x).
# ============================================================
L("===== PART B: world-aligned material =====")
MAT_DIR = "/Game/VRTemplate/Materials"
master_path = MAT_DIR + "/M_WorldAligned"
WAF = unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture")
L("  WorldAlignedTexture MF = %s" % (WAF is not None))

# Delete any prior (possibly broken) master + instances so we always rebuild clean.
for p in [master_path, MAT_DIR+"/MI_WA_TechPanel", MAT_DIR+"/MI_WA_Steel",
          MAT_DIR+"/MI_WA_Sandstone", MAT_DIR+"/MI_WA_CutStone"]:
    if EAL.does_asset_exist(p):
        EAL.delete_asset(p); L("  deleted prior %s" % p.split('/')[-1])

if False:
    pass
else:
    mat = AT.create_asset("M_WorldAligned", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -700, -100)
    tex.set_editor_property("parameter_name", "BaseTex")
    tex.set_editor_property("texture", unreal.load_asset("/Game/StarterContent/Textures/T_Rock_Sandstone_D"))
    size = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 150)
    size.set_editor_property("parameter_name", "TileSize")
    size.set_editor_property("default_value", 256.0)
    mfc = MEL.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, -350, -50)
    try:
        okfn = mfc.set_material_function(WAF)
        L("  set_material_function -> %s" % okfn)
    except Exception as e:
        mfc.set_editor_property("material_function", WAF); L("  set_material_function via property (%s)" % e)
    # log the function's real output names so we connect the right one
    try:
        for o in mfc.get_editor_property("function_outputs"):
            eo = o.get_editor_property("expression_output")
            L("  MF output name='%s'" % eo.get_editor_property("output_name"))
    except Exception as e:
        L("  output name dump err: %s" % e)
    c1 = MEL.connect_material_expressions(tex, "", mfc, "TextureObject")
    c2 = MEL.connect_material_expressions(size, "", mfc, "TextureSize")
    # First output (empty name) = the XYZ triplanar texture result.
    c3 = MEL.connect_material_property(mfc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if not c3:
        for cand in ["XYZ Texture", "XYZTexture", "RGB", "Result"]:
            c3 = MEL.connect_material_property(mfc, cand, unreal.MaterialProperty.MP_BASE_COLOR)
            if c3: L("  base color via '%s'" % cand); break
    L("  connect TextureObject=%s TextureSize=%s out->BaseColor=%s" % (c1, c2, c3))
    metal = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -350, 250)
    metal.set_editor_property("parameter_name", "Metallic"); metal.set_editor_property("default_value", 0.0)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -350, 350)
    rough.set_editor_property("parameter_name", "Roughness"); rough.set_editor_property("default_value", 0.75)
    cm = MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    cr = MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    L("  connect Metallic=%s Roughness=%s" % (cm, cr))
    MEL.recompile_material(mat)
    EAL.save_asset(master_path)
    L("  created master M_WorldAligned")

# ---- instances ----
def make_mi(name, tex, tile, metallic, roughness):
    full = MAT_DIR + "/" + name
    if EAL.does_asset_exist(full):
        mi = unreal.load_asset(full)
    else:
        mi = AT.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", mat)
    t = unreal.load_asset("/Game/StarterContent/Textures/" + tex)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", t)
    MEL.set_material_instance_scalar_parameter_value(mi, "TileSize", tile)
    MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    EAL.save_asset(full)
    L("  MI %s tex=%s tile=%.0f" % (name, tex, tile))
    return mi

mi_tech  = make_mi("MI_WA_TechPanel", "T_Concrete_Panels_D", 350, 0.2, 0.5)
mi_steel = make_mi("MI_WA_Steel",     "T_Metal_Steel_D",     300, 0.9, 0.35)
mi_sand  = make_mi("MI_WA_Sandstone", "T_Rock_Sandstone_D",  300, 0.0, 0.85)
mi_stone = make_mi("MI_WA_CutStone",  "T_Brick_Cut_Stone_D", 300, 0.0, 0.85)

# ============================================================
# PART C: assign world-aligned instances to the big floor/wall pieces per level
# ============================================================
L("===== PART C: assign floor/walls =====")
def assign(map_path, floor_mi, wall_mi):
    les.load_level(map_path)
    floors = {"Floor", "Floor2"}
    walls = {"Wall_0","Wall_1","Wall_2","Wall_3","Wall_4","Wall_5"}
    nf=nw=0
    for a in eas.get_all_level_actors():
        c = a.get_component_by_class(unreal.StaticMeshComponent)
        if not c: continue
        lbl = a.get_actor_label()
        if lbl in floors:
            for i in range(max(1, c.get_num_materials())): c.set_material(i, floor_mi)
            nf+=1
        elif lbl in walls:
            for i in range(max(1, c.get_num_materials())): c.set_material(i, wall_mi)
            nw+=1
    les.save_current_level()
    L("  %s floors=%d walls=%d" % (map_path.split('/')[-1], nf, nw))

assign("/Game/VRTemplate/Maps/VRTemplateMap", mi_tech, mi_steel)
assign("/Game/VRTemplate/Maps/TravelTestMap", mi_sand, mi_stone)
L("===== DONE =====")
