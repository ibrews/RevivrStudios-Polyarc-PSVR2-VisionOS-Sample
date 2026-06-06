import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def L(m):
    unreal.log_warning("VERIFY >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

def m0(a):
    c = a.get_component_by_class(unreal.StaticMeshComponent)
    if not c: return None
    x = c.get_material(0)
    return x.get_name() if x else None

def report(mp):
    les.load_level(mp)
    actors = eas.get_all_level_actors()
    gen = sorted([a.get_actor_label() for a in actors if a.get_actor_label().startswith("LVLGEN_")])
    floor=wall=accent=table=None; scifi=False; dirc=None; grab=None
    for a in actors:
        lbl = a.get_actor_label(); cls = a.get_class().get_name()
        if lbl == "sci_fi_cube_01": scifi = True
        if lbl == "Floor": floor = m0(a)
        elif lbl == "Wall_1": wall = m0(a)
        elif lbl == "Cube9": accent = m0(a)
        elif lbl == "Table": table = m0(a)
        if cls == "Grabbable_SmallCube_C" and grab is None: grab = m0(a)
        if cls == "DirectionalLight":
            lc = a.get_component_by_class(unreal.DirectionalLightComponent)
            if lc:
                col = lc.get_editor_property("light_color"); dirc = (col.r, col.g, col.b)
    L("%s: floor=%s wall=%s accent=%s table=%s grabcube=%s scifi=%s dir=%s" %
      (mp.split('/')[-1], floor, wall, accent, table, grab, scifi, dirc))
    L("   +props(%d): %s" % (len(gen), gen))

report("/Game/VRTemplate/Maps/VRTemplateMap")
report("/Game/VRTemplate/Maps/TravelTestMap")
L("DONE")
