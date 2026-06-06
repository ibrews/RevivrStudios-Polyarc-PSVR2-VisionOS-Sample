import unreal

def log(m):
    unreal.log("GLASSLOOK: " + m)
    unreal.log_flush()

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous(["/Game/StarterContent", "/Game/VRTemplate", "/Game"], True)

MEL = unreal.MaterialEditingLibrary
PATH = "/Game/M_GlassLookOpaque"

if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    unreal.EditorAssetLibrary.delete_asset(PATH)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset("M_GlassLookOpaque", "/Game", unreal.Material, unreal.MaterialFactoryNew())

# OPAQUE default-lit -> writes scene depth -> no visionOS blocky-alpha artifact.
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)

# dark tinted crystal base color
bc = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -520, -220)
bc.set_editor_property("constant", unreal.LinearColor(0.02, 0.06, 0.08, 1.0))
MEL.connect_material_property(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)

# very low roughness -> mirror-like (reflects sky/reflection captures = glassy)
rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -520, 0)
rough.set_editor_property("r", 0.06)
MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

spec = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -520, 120)
spec.set_editor_property("r", 1.0)
MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

# Fresnel-driven emissive rim -> reads as a glass/crystal edge highlight
try:
    fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -760, 280)
    try:
        fres.set_editor_property("exponent", 4.0)
        fres.set_editor_property("base_reflect_fraction", 0.04)
    except Exception as e:
        log("fresnel prop set skipped: %s" % e)
    rim = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -760, 430)
    rim.set_editor_property("constant", unreal.LinearColor(0.25, 0.70, 1.0, 1.0))
    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -360, 330)
    MEL.connect_material_expressions(fres, "", mul, "A")
    MEL.connect_material_expressions(rim, "", mul, "B")
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
except Exception as e:
    log("fresnel rim skipped: %s" % e)

MEL.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(PATH)
log("material created: %s blend=%s" % (PATH, str(mat.get_editor_property("blend_mode"))))

mat_asset = unreal.load_asset(PATH)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level("/Game/VRTemplate/Maps/TravelTestMap")

n = 0
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.StaticMeshActor):
        lab = a.get_actor_label()
        smc = a.static_mesh_component
        mesh = smc.static_mesh
        mn = mesh.get_name() if mesh else ""
        if "statue" in lab.lower() or "statue" in mn.lower():
            for i in range(max(1, smc.get_num_materials())):
                smc.set_material(i, mat_asset)
            log("assigned M_GlassLookOpaque to %s" % lab)
            n += 1

les.save_current_level()
log("DONE assigned=%d" % n)
