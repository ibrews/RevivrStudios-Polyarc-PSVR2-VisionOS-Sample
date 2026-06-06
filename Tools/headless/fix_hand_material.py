import unreal
def L(m):
    unreal.log_warning("HANDMAT >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

p = "/Game/StarterContent/Materials/M_Wood_Walnut"
mat = unreal.load_asset(p)
if not mat:
    L("MISSING %s" % p)
else:
    try:
        before = mat.get_editor_property("used_with_skeletal_mesh")
    except Exception as e:
        before = "err:%s" % e
    # Without this flag, applying M_Wood_Walnut to the poseable HAND mesh (skinned)
    # falls back to WorldGridMaterial in the cooked build (the grid the user saw in
    # the forest level). Enabling it cooks the skeletal shader permutation.
    mat.set_editor_property("used_with_skeletal_mesh", True)
    after = mat.get_editor_property("used_with_skeletal_mesh")
    L("used_with_skeletal_mesh: %s -> %s" % (before, after))
    unreal.MaterialEditingLibrary.recompile_material(mat)
    L("save -> %s" % unreal.EditorAssetLibrary.save_asset(p))
L("DONE")
