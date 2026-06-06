import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def L(m):
    unreal.log_warning("SBAKE2 >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

LIGHTS = [
    ("DirectionalLight", unreal.DirectionalLightComponent),
    ("SkyLight",         unreal.SkyLightComponent),
    ("PointLight",       unreal.PointLightComponent),
    ("SpotLight",        unreal.SpotLightComponent),
    ("RectLight",        unreal.RectLightComponent),
]

def get_world_settings():
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.WorldSettings):
            return a
    try:
        w = unreal.EditorLevelLibrary.get_editor_world()
        if w:
            return w.get_world_settings()
    except Exception as e:
        L("  ws fallback err: %s" % e)
    return None

def fix_and_bake(mp):
    L("===== %s =====" % mp)
    les.load_level(mp)
    ws = get_world_settings()
    if ws:
        try:
            before = ws.get_editor_property("force_no_precomputed_lighting")
            ws.set_editor_property("force_no_precomputed_lighting", False)
            L("  force_no_precomputed_lighting: %s -> False (ws=%s)" % (before, ws.get_class().get_name()))
        except Exception as e:
            L("  set force_no_precomputed_lighting err: %s" % e)
    else:
        L("  WorldSettings NOT FOUND")
    n = 0
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        for (nm, cc) in LIGHTS:
            if cls == nm:
                c = a.get_component_by_class(cc)
                if c:
                    try: c.set_mobility(unreal.ComponentMobility.STATIONARY); n += 1
                    except Exception: pass
    L("  lights -> Stationary: %d" % n)
    les.save_current_level()
    L("  building lighting (MEDIUM)...")
    try:
        les.build_light_maps(unreal.LightingBuildQuality.QUALITY_MEDIUM, True)
        L("  build done")
    except Exception as e:
        L("  build FAILED: %s" % e)
    L("  save -> %s" % les.save_current_level())

fix_and_bake("/Game/VRTemplate/Maps/VRTemplateMap")
fix_and_bake("/Game/VRTemplate/Maps/TravelTestMap")
L("ALL DONE")
