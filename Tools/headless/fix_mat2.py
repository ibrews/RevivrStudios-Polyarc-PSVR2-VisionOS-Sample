import unreal
def L(m):
    unreal.log_warning("FIX2 >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

AT  = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MAT_DIR = "/Game/VRTemplate/Materials"
WAF = unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture")
v2 = MAT_DIR + "/M_WorldAligned_v2"

if EAL.does_asset_exist(v2):
    mat = unreal.load_asset(v2); L("v2 exists")
else:
    mat = AT.create_asset("M_WorldAligned_v2", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -700, -100)
    tex.set_editor_property("parameter_name", "BaseTex")
    tex.set_editor_property("texture", unreal.load_asset("/Game/StarterContent/Textures/T_Rock_Sandstone_D"))
    size = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 150)
    size.set_editor_property("parameter_name", "TileSize"); size.set_editor_property("default_value", 256.0)
    mfc = MEL.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, -350, -50)
    mfc.set_material_function(WAF)
    try:
        for o in mfc.get_editor_property("function_outputs"):
            eo = o.get_editor_property("expression_output")
            L("  MF out='%s'" % eo.get_editor_property("output_name"))
    except Exception as e:
        L("  out dump err %s" % e)
    c1 = MEL.connect_material_expressions(tex, "", mfc, "TextureObject")
    c2 = MEL.connect_material_expressions(size, "", mfc, "TextureSize")
    c3 = MEL.connect_material_property(mfc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    L("  connect TexObj=%s TexSize=%s out->BaseColor=%s" % (c1, c2, c3))
    metal = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -350, 250)
    metal.set_editor_property("parameter_name", "Metallic"); metal.set_editor_property("default_value", 0.0)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -350, 350)
    rough.set_editor_property("parameter_name", "Roughness"); rough.set_editor_property("default_value", 0.75)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_asset(v2)
    L("created v2")

# Reparent the existing instances to the corrected master + re-set params.
for nm, tex, tile, me, ro in [("MI_WA_TechPanel","T_Concrete_Panels_D",350,0.2,0.5),
                              ("MI_WA_Steel","T_Metal_Steel_D",300,0.9,0.35),
                              ("MI_WA_Sandstone","T_Rock_Sandstone_D",300,0.0,0.85),
                              ("MI_WA_CutStone","T_Brick_Cut_Stone_D",300,0.0,0.85)]:
    full = MAT_DIR + "/" + nm
    mi = unreal.load_asset(full)
    if not mi:
        L("  MISSING MI %s" % nm); continue
    mi.set_editor_property("parent", mat)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", unreal.load_asset("/Game/StarterContent/Textures/"+tex))
    MEL.set_material_instance_scalar_parameter_value(mi, "TileSize", tile)
    MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", me)
    MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", ro)
    EAL.save_asset(full)
    L("  reparented %s" % nm)
L("DONE")
