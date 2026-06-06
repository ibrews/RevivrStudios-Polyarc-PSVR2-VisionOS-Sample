import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary

VR = "/Game/VRTemplate/Maps/VRTemplateMap"
TT = "/Game/VRTemplate/Maps/TravelTestMap"
GEN = "LVLGEN_"
M = "/Game/StarterContent/Materials/"
P = "/Game/StarterContent/Props/"
ARC = "/Game/StarterContent/Architecture/"

def L(m):
    unreal.log_warning("DESIGN >>> " + m)
    try: unreal.log_flush()
    except Exception: pass

def mat(p):
    a = unreal.load_asset(M + p)
    if not a: L("  !! MISSING MAT %s" % p)
    return a

def smc(actor):
    return actor.get_component_by_class(unreal.StaticMeshComponent)

def set_mats(actor, material):
    c = smc(actor)
    if not c or not material: return False
    for i in range(max(1, c.get_num_materials())):
        c.set_material(i, material)
    return True

def find(label):
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None

def clear_generated():
    removed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            if a.get_actor_label().startswith(GEN):
                eas.destroy_actor(a); removed += 1
        except Exception as e:
            L("  clear err %s" % e)
    L("  cleared %d prior generated actors" % removed)

def spawn(mesh_path, loc, rot, scale, material, label):
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        L("  !! MISSING MESH %s" % mesh_path); return None
    act = eas.spawn_actor_from_class(unreal.StaticMeshActor,
                                     unreal.Vector(loc[0], loc[1], loc[2]),
                                     unreal.Rotator(rot[0], rot[1], rot[2]))
    if not act:
        L("  !! SPAWN FAIL %s" % label); return None
    c = act.static_mesh_component
    c.set_static_mesh(mesh)
    act.set_actor_scale3d(unreal.Vector(scale[0], scale[1], scale[2]))
    act.set_actor_label(GEN + label)
    if material:
        for i in range(max(1, c.get_num_materials())):
            c.set_material(i, material)
    return act

def reskin(floor_mat, wall_mat, table_mat, accent_mat):
    floors = {"Floor", "Floor2"}
    walls = {"Wall_0","Wall_1","Wall_2","Wall_3","Wall_4","Wall_5"}
    nf=nw=nt=na=0
    for a in eas.get_all_level_actors():
        c = smc(a)
        if not c: continue
        lbl = a.get_actor_label()
        is_accent = False
        try:
            for i in range(c.get_num_materials()):
                cm = c.get_material(i)
                if cm and "MI_Grid_Accent" in cm.get_path_name():
                    is_accent = True; break
        except Exception: pass
        if lbl in floors:   set_mats(a, floor_mat); nf+=1
        elif lbl in walls:  set_mats(a, wall_mat); nw+=1
        elif lbl == "Table":set_mats(a, table_mat); nt+=1
        elif is_accent:     set_mats(a, accent_mat); na+=1
    L("  reskin floors=%d walls=%d table=%d accents=%d" % (nf,nw,nt,na))

def set_lights(dir_color, dir_int, sky_color):
    nd=ns=0
    for a in eas.get_all_level_actors():
        cls = a.get_class().get_name()
        if cls == "DirectionalLight":
            c = a.get_component_by_class(unreal.DirectionalLightComponent)
            if c: c.set_light_color(dir_color); c.set_intensity(dir_int); nd+=1
        elif cls == "SkyLight":
            c = a.get_component_by_class(unreal.SkyLightComponent)
            if c: c.set_light_color(sky_color); ns+=1
    L("  lights dir=%d sky=%d" % (nd,ns))

def reskin_grabcubes(material):
    n=0
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() == "Grabbable_SmallCube_C":
            if set_mats(a, material): n+=1
    L("  grabcube reskin=%d" % n)

COOL = unreal.LinearColor(0.66, 0.76, 1.0, 1.0)
WARM = unreal.LinearColor(1.0, 0.74, 0.50, 1.0)

# ===== STEP 1: re-duplicate TravelTestMap (re-key MapBuildDataId) =====
L("===== STEP 1: re-duplicate TravelTestMap =====")
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/VRTemplate/Maps"], True)
les.load_level(VR)
if EAL.does_asset_exist(TT):
    L("  delete TT -> %s" % EAL.delete_asset(TT))
L("  duplicate VR->TT -> %s" % (EAL.duplicate_asset(VR, TT) is not None))
L("  save TT -> %s" % EAL.save_asset(TT))

# ===== STEP 2: LEVEL A = Cobalt Lab (cool metal/tech) =====
L("===== STEP 2: LEVEL A (Cobalt Lab) =====")
les.load_level(VR)
clear_generated()
reskin(mat("M_Tech_Panel"), mat("M_Metal_Brushed_Nickel"), mat("M_Metal_Brushed_Nickel"), mat("M_Metal_Chrome"))
set_lights(COOL, 2.5, COOL)
nickel = mat("M_Metal_Brushed_Nickel"); chrome = mat("M_Metal_Chrome")
spawn(ARC + "Pillar_50x500", (180, -260, 0), (0,0,0),   (1,1,1),       nickel, "Pillar_L")
spawn(ARC + "Pillar_50x500", (180,  260, 0), (0,0,0),   (1,1,1),       nickel, "Pillar_R")
spawn(P + "SM_Statue",       (360,   0, 0), (0,0,180),  (1.5,1.5,1.5), chrome, "Statue_Center")
L("  saving A -> %s" % les.save_current_level())

# ===== STEP 3: LEVEL B = Stone Courtyard (warm stone/wood/greenery) =====
L("===== STEP 3: LEVEL B (Stone Courtyard) =====")
les.load_level(TT)
clear_generated()
reskin(mat("M_Rock_Sandstone"), mat("M_Brick_Cut_Stone"), mat("M_Wood_Walnut"), mat("M_Wood_Walnut"))
set_lights(WARM, 3.0, WARM)
reskin_grabcubes(mat("M_Wood_Walnut"))
sf = find("sci_fi_cube_01")
if sf: eas.destroy_actor(sf); L("  removed sci_fi_cube_01")
spawn(P + "SM_Statue",     (350,   0, 0), (0,0,180), (1.5,1.5,1.5), None, "Statue_Center")
spawn(P + "SM_Rock",       (250,-380, 0), (0,0,15),  (1.8,1.8,1.6), None, "Rock_1")
spawn(P + "SM_Rock",       (330,-410, 0), (0,0,140), (1.1,1.1,1.0), None, "Rock_2")
spawn(P + "SM_Rock",       (185,-420, 0), (0,0,250), (0.9,0.9,0.8), None, "Rock_3")
spawn(P + "SM_Bush",       (-320,-300, 0), (0,0,0),  (1.3,1.3,1.3), None, "Bush_1")
spawn(P + "SM_Bush",       (-360, 220, 0), (0,0,60), (1.1,1.1,1.1), None, "Bush_2")
spawn(P + "SM_Bush",       ( 120, 380, 0), (0,0,120),(1.4,1.4,1.4), None, "Bush_3")
spawn(P + "SM_Bush",       ( 360, 340, 0), (0,0,200),(1.2,1.2,1.2), None, "Bush_4")
spawn(P + "SM_TableRound", (-120, 300, 0), (0,0,0),   (1,1,1), None, "TableRound")
spawn(P + "SM_Chair",      (-120, 380, 0), (0,0,270), (1,1,1), None, "Chair")
L("  saving B -> %s" % les.save_current_level())

L("===== DESIGN DONE =====")
