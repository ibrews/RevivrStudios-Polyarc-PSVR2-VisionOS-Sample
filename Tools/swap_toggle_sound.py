#!/usr/bin/env python
"""
swap_toggle_sound.py -- retarget the ring-to-thumb toggle SFX in VRPawn_HandWorldControl.

Alex, 2026-09-23 (in-headset): "my sound effect is terrible -- would be great to swap that
with ANYTHING else."

The toggle currently plays /Game/StarterContent/Audio/Collapse01 (and /Collapse02) -- UE
StarterContent rubble/debris crash samples, meant for a collapsing building. Target is
/Engine/VREditor/Sounds/UI/Flashlight_Switch, a crisp UI switch click that
VRPawn_HandWorldControl ALREADY references, so it adds no new cook dependency.

RUN HEADLESS:
    UnrealEditor-Cmd <project>.uproject -run=pythonscript -script="<abs path to this file>"
    # add -DryRun anywhere on the command line to report without saving

IDEMPOTENT: re-running after a successful swap reports "already retargeted" and changes
nothing. Safe to leave in a build pipeline.

--------------------------------------------------------------------------------------
KNOWN LIMITATION -- READ BEFORE RELYING ON THIS IN A PIPELINE
--------------------------------------------------------------------------------------
Stock UE Python CANNOT edit a Blueprint graph node's pin default. `UEdGraph.nodes` is
exposed (UPROPERTY), but `UEdGraphPin` is a plain struct rather than a UObject, so its
`DefaultObject` is not reachable from Python in UE 5.8.

So this script can retarget a sound that lives in:
  * an SCS AudioComponent template's `sound` property, or
  * a Blueprint variable default of type SoundBase (on the CDO)

and it CANNOT retarget a sound that is a literal pin default on a PlaySound2D /
SpawnSoundAttached node. If that is where the reference lives, this script exits NON-ZERO
with the exact node names it found, so a pipeline fails loudly instead of silently shipping
the old sound. In that case the swap is two clicks in the editor, or needs a small
EditorUtilityBlueprint / C++ helper.
"""

import sys
import unreal

BP_PATH = "/Game/AVPTemplate/WorldControl/Blueprints/VRPawn_HandWorldControl"
OLD_SOUNDS = (
    "/Game/StarterContent/Audio/Collapse01",
    "/Game/StarterContent/Audio/Collapse02",
)
NEW_SOUND = "/Engine/VREditor/Sounds/UI/Flashlight_Switch"

DRY_RUN = any(a.lower() == "-dryrun" for a in sys.argv)


def log(msg):
    unreal.log("[swap_toggle_sound] {}".format(msg))


def warn(msg):
    unreal.log_warning("[swap_toggle_sound] {}".format(msg))


def path_of(obj):
    if obj is None:
        return None
    try:
        return obj.get_path_name().split(".")[0]
    except Exception:
        return str(obj)


def is_old(obj):
    return path_of(obj) in OLD_SOUNDS


def is_new(obj):
    return path_of(obj) == NEW_SOUND


def main():
    log("DRY RUN -- nothing will be saved" if DRY_RUN else "APPLY mode")

    new_sound = unreal.EditorAssetLibrary.load_asset(NEW_SOUND)
    if new_sound is None:
        warn("target sound could not be loaded: {}".format(NEW_SOUND))
        return 2
    log("target sound OK: {}".format(NEW_SOUND))

    bp = unreal.EditorAssetLibrary.load_asset(BP_PATH)
    if bp is None:
        warn("blueprint could not be loaded: {}".format(BP_PATH))
        return 2

    changed = 0
    already = 0
    found_any_old = False

    # ---- 1. SCS AudioComponent templates -------------------------------------------
    try:
        subsystem = unreal.get_editor_subsystem(unreal.SubobjectDataSubsystem)
        handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)
        for h in handles:
            data = subsystem.k2_find_subobject_data_from_handle(h)
            obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
            if not isinstance(obj, unreal.AudioComponent):
                continue
            cur = obj.get_editor_property("sound")
            log("  AudioComponent '{}' sound BEFORE: {}".format(obj.get_name(), path_of(cur)))
            if is_new(cur):
                already += 1
                log("    -> already retargeted, leaving alone")
            elif is_old(cur):
                found_any_old = True
                if not DRY_RUN:
                    obj.set_editor_property("sound", new_sound)
                log("    -> AFTER: {}".format(NEW_SOUND))
                changed += 1
    except Exception as exc:  # subsystem shape differs across engine versions
        warn("SCS component scan unavailable ({}); continuing".format(exc))

    # ---- 2. Blueprint variable defaults of type SoundBase ---------------------------
    try:
        cdo = unreal.get_default_object(bp.generated_class())
        for var in bp.get_editor_property("new_variables"):
            name = str(var.get_editor_property("var_name"))
            try:
                cur = cdo.get_editor_property(name)
            except Exception:
                continue
            if not isinstance(cur, unreal.SoundBase):
                continue
            log("  variable '{}' sound BEFORE: {}".format(name, path_of(cur)))
            if is_new(cur):
                already += 1
                log("    -> already retargeted, leaving alone")
            elif is_old(cur):
                found_any_old = True
                if not DRY_RUN:
                    cdo.set_editor_property(name, new_sound)
                log("    -> AFTER: {}".format(NEW_SOUND))
                changed += 1
    except Exception as exc:
        warn("variable-default scan unavailable ({}); continuing".format(exc))

    # ---- 3. Detect graph pin defaults we CANNOT edit ---------------------------------
    sound_nodes = []
    try:
        for graph in unreal.BlueprintEditorLibrary.list_graphs(bp):
            for node in graph.get_editor_property("nodes"):
                title = node.get_name()
                if any(k in title for k in ("PlaySound", "SpawnSound", "CallFunction")):
                    sound_nodes.append("{} :: {}".format(graph.get_name(), title))
    except Exception as exc:
        warn("graph scan unavailable ({})".format(exc))

    # ---- verdict ---------------------------------------------------------------------
    if changed and not DRY_RUN:
        unreal.EditorAssetLibrary.save_asset(BP_PATH, only_if_is_dirty=False)
        log("SAVED {} ({} reference(s) retargeted)".format(BP_PATH, changed))
        return 0
    if changed and DRY_RUN:
        log("DRY RUN: would retarget {} reference(s)".format(changed))
        return 0
    if already and not found_any_old:
        log("already retargeted -- nothing to do (idempotent no-op)")
        return 0

    # Nothing reachable was changed. Do NOT report success.
    warn("FAILED: found no reachable {} reference on {}".format(OLD_SOUNDS, BP_PATH))
    warn("The sound is most likely a PIN DEFAULT on a graph node, which stock UE Python")
    warn("cannot edit (UEdGraphPin is a struct, not a UObject).")
    if sound_nodes:
        warn("Candidate nodes to fix by hand ({}):".format(len(sound_nodes)))
        for n in sound_nodes[:25]:
            warn("    {}".format(n))
    warn("Fix in-editor: open {}, select the Play Sound node, set Sound = {}".format(BP_PATH, NEW_SOUND))
    return 1


if __name__ == "__main__":
    code = main()
    log("exit {}".format(code))
    sys.exit(code)
