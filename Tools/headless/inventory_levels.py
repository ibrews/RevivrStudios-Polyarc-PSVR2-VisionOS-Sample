import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def dump(map_path):
    unreal.log_warning("INV >>> ===== %s =====" % map_path)
    les.load_level(map_path)
    actors = eas.get_all_level_actors()
    unreal.log_warning("INV >>> actor_count=%d" % len(actors))
    for a in actors:
        try:
            cls = a.get_class().get_name()
            label = a.get_actor_label()
            loc = a.get_actor_location()
            rot = a.get_actor_rotation()
            scl = a.get_actor_scale3d()
            line = "INV| cls=%s | label=%s | loc=(%.1f,%.1f,%.1f) | rot=(%.1f,%.1f,%.1f) | scl=(%.2f,%.2f,%.2f)" % (
                cls, label, loc.x, loc.y, loc.z, rot.roll, rot.pitch, rot.yaw, scl.x, scl.y, scl.z)
            smc = a.get_component_by_class(unreal.StaticMeshComponent)
            if smc:
                mesh = smc.static_mesh
                meshp = mesh.get_path_name() if mesh else "None"
                mats = []
                try:
                    n = smc.get_num_materials()
                    for i in range(n):
                        m = smc.get_material(i)
                        mats.append(m.get_path_name() if m else "None")
                except Exception as e:
                    mats = ["<materr:%s>" % e]
                line += " | mesh=%s | mats=%s" % (meshp, mats)
            # lights
            for (lc_cls, lname) in [(unreal.DirectionalLightComponent,"Dir"),(unreal.SkyLightComponent,"Sky"),(unreal.PointLightComponent,"Point"),(unreal.SpotLightComponent,"Spot"),(unreal.RectLightComponent,"Rect")]:
                lc = a.get_component_by_class(lc_cls)
                if lc:
                    try:
                        col = lc.get_editor_property("light_color")
                        inten = lc.intensity
                        line += " | LIGHT=%s color=(%d,%d,%d) intensity=%.2f" % (lname, col.r, col.g, col.b, inten)
                    except Exception as e:
                        line += " | LIGHT=%s <err:%s>" % (lname, e)
            unreal.log_warning(line)
        except Exception as e:
            unreal.log_warning("INV| <actor err: %s>" % e)
    unreal.log_warning("INV >>> ----- end %s -----" % map_path)

dump("/Game/VRTemplate/Maps/VRTemplateMap")
dump("/Game/VRTemplate/Maps/TravelTestMap")
unreal.log_warning("INV >>> ALL DONE")
