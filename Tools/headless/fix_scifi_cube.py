import unreal

# Sci-fi cube fix (VRTemplateMap / lab):
#  1. material -> M_Metal_Chrome (mobile-safe, already has bUsedWithStaticLighting)
#  2. mobility -> Movable  (Movable meshes don't use static lighting -> the un-resaveable glTF
#     base material /InterchangeAssets/gltf/M_Default can no longer cause a WorldGrid fallback)
#  3. add RotatingMovementComponent (yaw spin) so it reads as a hero prop
# Saves VRTemplateMap. Verifies by reloading the level afterward.

def L(m):
    unreal.log_warning("CUBEFIX >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

MAP = "/Game/VRTemplate/Maps/VRTemplateMap"
CHROME = "/Game/StarterContent/Materials/M_Metal_Chrome"
MATCH = ("sci-fi_cube", "sci_fi_cube")  # match on the MESH path (Fab asset), not the label

ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous(["/Game"], True)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

chrome = unreal.load_asset(CHROME)
L("chrome loaded: %s" % (chrome.get_path_name() if chrome else "NULL"))

def find_cube_meshcomps():
    hits = []
    for a in eas.get_all_level_actors():
        try: comps = a.get_components_by_class(unreal.StaticMeshComponent)
        except Exception: comps = []
        for c in comps:
            mesh = c.get_editor_property("static_mesh") if c else None
            mp = mesh.get_path_name().lower() if mesh else ""
            if any(tok in mp for tok in MATCH):
                hits.append((a, c, mesh.get_path_name()))
    return hits

def add_rotator(actor, rate):
    # Preferred: SubobjectDataSubsystem (editor-persistent component add)
    try:
        sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = sds.k2_gather_subobject_data_for_instance(actor)
        root = handles[0]
        try:
            params = unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.RotatingMovementComponent)
        except Exception:
            params = unreal.AddNewSubobjectParams()
            params.set_editor_property("parent_handle", root)
            params.set_editor_property("new_class", unreal.RotatingMovementComponent)
        new_handle, fail = sds.add_new_subobject(params)
        if fail and str(fail) not in ("", "None", "0"):
            L("  [rotator] add_new_subobject reported: %s" % fail)
    except Exception as e:
        L("  [rotator] SubobjectDataSubsystem path errored: %s" % e)
    # Fallback: add_component_by_class + register as instance component
    comp = None
    for c in actor.get_components_by_class(unreal.RotatingMovementComponent):
        comp = c; break
    if comp is None:
        try:
            comp = actor.add_component_by_class(unreal.RotatingMovementComponent, False, unreal.Transform(), False)
            try: actor.add_instance_component(comp)
            except Exception as e: L("  [rotator] add_instance_component skipped: %s" % e)
        except Exception as e:
            L("  [rotator] add_component_by_class failed: %s" % e)
    if comp:
        try:
            comp.set_editor_property("rotation_rate", rate)
            comp.set_editor_property("rotation_in_local_space", True)
        except Exception as e:
            L("  [rotator] set rotation_rate failed: %s" % e)
    return comp

les.load_level(MAP)
hits = find_cube_meshcomps()
L("matched %d sci-fi-cube mesh component(s)" % len(hits))
n = 0
for actor, smc, meshpath in hits:
    L("cube actor='%s' mesh=%s" % (actor.get_actor_label(), meshpath))
    # 1. material
    for i in range(max(1, smc.get_num_materials())):
        smc.set_material(i, chrome)
    # 2. mobility -> Movable
    smc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    # 3. rotator (yaw 30 deg/s)
    comp = add_rotator(actor, unreal.Rotator(roll=0.0, pitch=0.0, yaw=30.0))
    L("  applied: mat=chrome mobility=%s rotator=%s"
      % (smc.get_editor_property("mobility"), ("YES" if comp else "NO")))
    n += 1

if n == 0:
    L("NO sci-fi cube found by mesh-path match — NOTHING saved. (Check the mesh path tokens.)")
else:
    les.save_current_level()
    L("saved %s" % MAP)
    # verify by reloading from disk
    les.load_level(MAP)
    for actor, smc, meshpath in find_cube_meshcomps():
        m0 = smc.get_material(0)
        rot = actor.get_components_by_class(unreal.RotatingMovementComponent)
        L("VERIFY '%s': mat0=%s mobility=%s rotators=%d"
          % (actor.get_actor_label(),
             (m0.get_path_name() if m0 else "NULL"),
             smc.get_editor_property("mobility"),
             len(rot)))
L("CUBEFIX DONE applied=%d" % n)
