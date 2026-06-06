import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def L(m):
    unreal.log_warning("BAKE >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

# SAFEGUARD: baking saves the maps. If the game C++ module didn't load (stale My_ProjectEditor),
# the game-class actors are 'missing' and the save would DROP them. Abort rather than corrupt.
GAME_CLASSES = ["HandTrackingComponent", "HandSkeletalDriverComponent", "GamepadInputSetup"]
if not any(hasattr(unreal, c) for c in GAME_CLASSES):
    L("ABORT: game module not loaded (none of %s) — NOT baking/saving maps to protect feature work." % GAME_CLASSES)
    raise SystemExit

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
        if w: return w.get_world_settings()
    except Exception as e:
        L("ws fallback err: %s" % e)
    return None

def fix_and_bake(mp):
    L("===== %s =====" % mp)
    les.load_level(mp)
    ws = get_world_settings()
    if ws:
        try:
            ws.set_editor_property("force_no_precomputed_lighting", False)
            L("force_no_precomputed_lighting -> False")
        except Exception as e:
            L("set fnpl err: %s" % e)
    else:
        L("WorldSettings NOT FOUND")
    n = 0
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        for (nm, cc) in LIGHTS:
            if cls == nm:
                c = a.get_component_by_class(cc)
                if c:
                    try: c.set_mobility(unreal.ComponentMobility.STATIONARY); n += 1
                    except Exception: pass
    L("lights -> Stationary: %d" % n)
    les.save_current_level()
    L("building lighting (MEDIUM)...")
    try:
        les.build_light_maps(unreal.LightingBuildQuality.QUALITY_MEDIUM, True)
        L("build done")
    except Exception as e:
        L("build FAILED: %s" % e)
    L("final save -> %s" % les.save_current_level())

for mp in ["/Game/VRTemplate/Maps/VRTemplateMap", "/Game/VRTemplate/Maps/TravelTestMap"]:
    fix_and_bake(mp)

L("BAKE_BOTH DONE")
