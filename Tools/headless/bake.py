import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
def L(m):
    unreal.log_warning("BAKE >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

def bake(mp):
    L("loading %s" % mp)
    les.load_level(mp)
    L("building lighting (PREVIEW)...")
    try:
        les.build_light_maps(unreal.LightingBuildQuality.QUALITY_PREVIEW, True)
        L("build done %s" % mp)
    except Exception as e:
        L("build FAILED %s: %s" % (mp, e))
    L("save -> %s" % les.save_current_level())

bake("/Game/VRTemplate/Maps/VRTemplateMap")
bake("/Game/VRTemplate/Maps/TravelTestMap")
L("ALL BAKE DONE")
