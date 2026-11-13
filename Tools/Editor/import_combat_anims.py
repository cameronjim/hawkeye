"""Turn downloaded combat clips into game animations (claude-docs/animation.md).

Reads Tools/Data/Anims/manifest.json. For every source it can find:

    mixamo_fbx       FBX files in a folder. The source's mesh_file (a clip downloaded With Skin)
                     imports as skeletal mesh plus its animation and defines the source skeleton;
                     every other file imports as an animation onto it. Under /Game/AnimSources/<Source>.
    uasset_project   .uasset clips in another project's Content folder (Paragon Sparrow in the Game
                     Animation Sample's). The clips, the source mesh and their hard-reference closure
                     are copied to the same /Game/ paths here (Tools/Editor/_packages.py reads UE4 and
                     UE5 packages), never overwriting.

then, per source:

    IK_<Source>                      an IK Rig on the source mesh with the standard chains (root,
                                     spine, neck, head, clavicles, arms, legs, toes, fingers when the
                                     bones exist) from the source's chain map in the manifest
    RTG_<Source>_To_<Target>         an IK Retargeter from it to each target rig: the engine's default
                                     op stack (pelvis, FK chains, IK chains, IK solve, root motion),
                                     chains mapped by name, the target's retarget pose auto-aligned to
                                     the source's, root motion copied from the source root (generated
                                     from the pelvis for Mixamo, which has no root bone: the root goes
                                     under the pelvis on the ground, horizontal translation only, so the
                                     pelvis keeps its height when the game locks or extracts the root)

and per clip, per target its characters need (uefn: Kate and Clint; thug: thugs and archers):

    <target output>/A_<Role>_<Variant>    the batch-retargeted sequence, root motion on for strike
                                          roles on the UEFN target, root locked everywhere else
    <target output>/AM_<Role>_<Variant>   its montage: slot, ANS_HitWindow, ANS_ComboWindow, a motion
                                          warping window, loop or hold, by role (ROLE_DEFAULTS),
                                          overridable per clip in the manifest

Targets: IK_Kate_UEFN on SKM_UEFN_Mannequin and IK_Thug_Mannequin on the UE4 mannequin the thugs
wear, both built here from the manifest's chain maps. (Epic's IK_UEFN_Mannequin from the sample was
tried first and dropped: its stored skeleton lacks this project's "attach" bone, and every retargeted
track landed one bone down, the pelvis at the origin; the self-test's raw-bone checks catch that.)

Idempotent: every asset that exists is kept unless --force; only what changed is saved; one log
line per clip. A missing source folder, file or asset is reported and skipped, never fatal.

Run headless (Tools\\import-anims.ps1 wraps this):

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript "-script=Tools\\Editor\\import_combat_anims.py --force"

Flags (in the -script string, or HAWKEYE_ANIM_ARGS):
    --force           re-import, rebuild rigs and retargeters, re-retarget and rebuild montages
    --only NAME       only this source
    --selftest        run the manifest's selftest block instead and assert on the results
    --keep            with --selftest, keep the SelfTest outputs (default: deleted afterwards)
"""

import hashlib
import json
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _packages  # noqa: E402

SCRIPT_BUILD = "combat-anims-3"   # bump when the rig, retargeter or montage layout below changes
BUILD_TAG = "HawkeyeBuild"
CHARACTERS_TAG = "HawkeyeCharacters"
SOURCE_TAG = "HawkeyeSource"

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MANIFEST = os.path.join(REPO, "Tools", "Data", "Anims", "manifest.json")
SOURCES_ROOT = "/Game/AnimSources"

ALL_CHARACTERS = ("kate", "clint", "thug", "archer")

# Per-role layout. Windows are fractions of the clip; warp runs from 0 to warp_end.
_STRIKE = {"slot": "DefaultSlot", "hit": [0.25, 0.45], "combo": [0.45, 0.8], "warp_end": 0.25, "root_motion": True}
ROLE_DEFAULTS = {
    "Light1": _STRIKE, "Light2": _STRIKE, "Light3": _STRIKE, "Kick": _STRIKE,
    "Heavy": {"slot": "DefaultSlot", "hit": [0.4, 0.55], "combo": [0.6, 0.85], "warp_end": 0.4, "root_motion": True},
    "Parry": {"slot": "DefaultSlot", "root_motion": False},
    "DodgeForward": {"slot": "DefaultSlot"}, "DodgeBack": {"slot": "DefaultSlot"},
    "DodgeLeft": {"slot": "DefaultSlot"}, "DodgeRight": {"slot": "DefaultSlot"},
    "HitFront": {"slot": "DefaultSlot"}, "HitBack": {"slot": "DefaultSlot"},
    "HitLeft": {"slot": "DefaultSlot"}, "HitRight": {"slot": "DefaultSlot"},
    "Knockdown": {"slot": "DefaultSlot", "hold": True},
    "GetUp": {"slot": "DefaultSlot"},
    "FinisherAttacker": {"slot": "DefaultSlot"}, "FinisherVictim": {"slot": "DefaultSlot"},
    "FinisherBow": {"slot": "DefaultSlot"},
    # The bow's clips over locomotion: 0.15 s in, 0.25 s out. The draw holds its last frame (the anchor)
    # until the aim clip takes over, so it never blends out toward locomotion on the way.
    "BowDraw": {"slot": "UpperBody", "hold": True, "blend_in": 0.15, "blend_out": 0.25},
    "BowAimIdle": {"slot": "UpperBody", "loop": True, "blend_in": 0.15, "blend_out": 0.25},
    "BowFire": {"slot": "UpperBody", "blend_in": 0.15, "blend_out": 0.25},
    "BowNock": {"slot": "UpperBody", "blend_in": 0.15, "blend_out": 0.25},
}

# Single-frame aim poses (BowAimOffset_CC, _CU, ...): retargeted, then made mesh-space additive and laid
# into the manifest's aim_offsets blend spaces. No montage.
AIM_POSE_PREFIX = "BowAimOffset_"
FROM_TAG = "HawkeyeFrom"

_MIXAMO_PREFIX = re.compile(r"^mixamorig\d*[:_]", re.IGNORECASE)

FORCE = False


# --------------------------------------------------------------------------------------------------
# arguments and manifest
# --------------------------------------------------------------------------------------------------


def parse_args(argv):
    args = {"force": False, "only": None, "selftest": False, "keep": False}
    words = list(argv) + os.environ.get("HAWKEYE_ANIM_ARGS", "").split()
    i = 0
    while i < len(words):
        word = words[i]
        if word == "--force":
            args["force"] = True
        elif word == "--selftest":
            args["selftest"] = True
        elif word == "--keep":
            args["keep"] = True
        elif word == "--only" and i + 1 < len(words):
            args["only"] = words[i + 1]
            i += 1
        i += 1
    return args


def load_manifest(path=MANIFEST):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def clip_characters(clip):
    wanted = clip.get("characters", "all")
    if isinstance(wanted, str):
        wanted = [wanted]
    if "all" in wanted:
        return list(ALL_CHARACTERS)
    return [w for w in wanted if w in ALL_CHARACTERS]


def targets_for(clip, targets):
    """(target name, characters on it) for every target one of the clip's characters uses."""
    chars = clip_characters(clip)
    out = []
    for name, target in targets.items():
        config = target.get("config", target)
        on = [ch for ch in chars if ch in config["characters"]]
        if on:
            out.append((name, on))
    return out


def clip_layout(clip):
    layout = dict(ROLE_DEFAULTS.get(clip["role"], {"slot": "DefaultSlot"}))
    layout["_explicit"] = sorted(k for k in clip if k in ("hit", "combo", "warp_end"))
    for key in ("slot", "hit", "combo", "warp_end", "loop", "hold", "root_motion", "blend_in", "blend_out",
                "start_s", "end_s", "rate", "hit_s", "combo_s", "warp_end_s"):
        if key in clip:
            layout[key] = clip[key]
    return layout


def asset_names(clip):
    base = "{0}_{1}".format(clip["role"], re.sub(r"[^A-Za-z0-9]", "", clip.get("variant", "Clip")) or "Clip")
    return "A_" + base, "AM_" + base


def fingerprint(*parts):
    return hashlib.sha1(json.dumps(parts, sort_keys=True).encode("utf-8")).hexdigest()[:12]


# --------------------------------------------------------------------------------------------------
# small engine helpers
# --------------------------------------------------------------------------------------------------


def tag_of(asset, tag=BUILD_TAG):
    try:
        return unreal.EditorAssetLibrary.get_metadata_tag(asset, tag) or ""
    except Exception:  # noqa: BLE001
        return ""


def set_tag(asset, value, tag=BUILD_TAG):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, tag, value)


def delete_if_exists(path):
    if c.exists(path):
        unreal.EditorAssetLibrary.delete_asset(path)


def skeleton_bones(skeleton):
    """Bone names of a skeleton, from its reference pose."""
    pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    return [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(pose)]


def mesh_skeleton(mesh):
    return mesh.get_editor_property("skeleton") if mesh is not None else None


def resolve_bone(name, bones, lower_map):
    """name, or the bone that is name after a Mixamo prefix, or a case-insensitive match. None if absent."""
    if not name:
        return None
    if name in bones:
        return name
    low = name.lower()
    if low in lower_map:
        return lower_map[low]
    for bone in bones:
        if _MIXAMO_PREFIX.sub("", bone).lower() == low:
            return bone
    return None


def load_path(path):
    """The asset at path, through the registry, or straight from its package when the registry has
    not caught up with a file copied in this session."""
    asset = c.load_or_none(path)
    if asset is None:
        try:
            asset = unreal.load_asset(path)
        except Exception:  # noqa: BLE001 - a missing package is expected here
            asset = None
    return asset


def scan(paths):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(sorted(set(paths)), True)


# --------------------------------------------------------------------------------------------------
# IK rigs
# --------------------------------------------------------------------------------------------------


def ensure_ik_rig(path, mesh, chain_map_name, chain_maps):
    """An IK Rig at path on mesh with chain_map's chains. "auto" uses the engine's auto definition."""
    chain_map = chain_maps.get(chain_map_name) if chain_map_name != "auto" else None
    stamp = fingerprint(SCRIPT_BUILD, chain_map_name, chain_map, mesh.get_path_name())
    existing = c.load_or_none(path)
    if existing is not None and not FORCE and tag_of(existing) == stamp:
        c.log("exists", path)
        return existing
    folder, name = path.rsplit("/", 1)
    if existing is None:
        rig, _created = c.create_asset(name, folder, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory(), quiet=True)
    else:
        rig = existing
    if rig is None:
        c.log("FAILED", path, "could not create the IK Rig")
        return None
    ctl = unreal.IKRigController.get_controller(rig)
    ctl.set_skeletal_mesh(mesh)
    for chain in list(ctl.get_retarget_chains()):
        ctl.remove_retarget_chain(chain.get_editor_property("chain_name"))
    if chain_map is None:
        ok = ctl.apply_auto_generated_retarget_definition()
        made = [str(ch.get_editor_property("chain_name")) for ch in ctl.get_retarget_chains()]
        c.log("updated" if existing is not None else "created", path,
              "auto definition {0}: {1} chains, root {2}".format("ok" if ok else "FAILED", len(made), ctl.get_retarget_root()))
    else:
        bones = skeleton_bones(mesh_skeleton(mesh))
        lower = dict((b.lower(), b) for b in bones)
        root = resolve_bone(chain_map.get("retarget_root", "pelvis"), bones, lower)
        if root:
            ctl.set_retarget_root(root)
        made, skipped = [], []
        for chain_name, (start, end) in chain_map["chains"].items():
            start_bone = resolve_bone(start, bones, lower)
            end_bone = resolve_bone(end, bones, lower)
            if start_bone and end_bone:
                ctl.add_retarget_chain(chain_name, start_bone, end_bone, "")
                made.append(chain_name)
            else:
                skipped.append(chain_name)
        c.log("updated" if existing is not None else "created", path,
              "root {0}, {1} chains{2}".format(root, len(made), ", no bones for " + ", ".join(skipped) if skipped else ""))
    set_tag(rig, stamp)
    c.save(rig)
    return rig


def ensure_target(name, target, chain_maps):
    mesh = c.load_or_none(target["skeletal_mesh"])
    if mesh is None:
        c.log("skipped", "target " + name, "no " + target["skeletal_mesh"])
        return None
    rig = ensure_ik_rig(target["ik_rig"], mesh, target["chains"], chain_maps)
    if rig is None:
        return None
    return {"name": name, "mesh": mesh, "rig": rig, "config": target}


# --------------------------------------------------------------------------------------------------
# retargeters
# --------------------------------------------------------------------------------------------------


def _configure_root_motion(ctl, chain_map, target_mesh):
    """Root motion from the source root, or generated from the target pelvis when the source has none.
    Every root motion op in the stack gets the same settings; the note says what was done."""
    op_class = getattr(unreal, "IKRetargetRootMotionController", None)
    if op_class is None:
        return "no root motion op controller"
    source_root = (chain_map or {}).get("root_bone", "root")
    target_bones = skeleton_bones(mesh_skeleton(target_mesh))
    notes = []
    for index in range(ctl.get_num_retarget_ops()):
        op = ctl.get_op_controller(index)
        if isinstance(op, op_class):
            notes.append(_configure_root_motion_op(op, source_root, target_bones))
    if not notes:
        return "no root motion op"
    return notes[0] if len(set(notes)) == 1 else "; ".join(notes)


def _configure_root_motion_op(op, source_root, target_bones):
    op.set_target_root_bone("root" if "root" in target_bones else target_bones[0])
    op.set_target_pelvis_bone("pelvis" if "pelvis" in target_bones else target_bones[1])
    if source_root:
        op.set_source_root_bone(source_root)
        return "root motion copied from " + source_root
    settings = op.get_settings()
    source_enum = getattr(unreal, "RootMotionSource", None)
    height_enum = getattr(unreal, "RootMotionHeightSource", None)
    if source_enum is None or not hasattr(source_enum, "GENERATE_FROM_TARGET_PELVIS") or height_enum is None:
        return "FAILED: root motion enums not exposed; the pelvis height would land on the root"
    # The op's defaults copy the root's height "from the source root", which on a source with no
    # root bone is its first bone, the hips: the root track then carries the hips' 90 cm and the
    # pelvis sits at the root's feet. The game locks or extracts the root, so the character knelt.
    # Instead: the root straight under the pelvis (no ref-pose offset, which the pelvis's tilt
    # swung about), on the ground, keeping its reference rotation. Only the hips' horizontal
    # travel becomes root motion; everything else stays on the pelvis.
    applied = c.set_props(settings, [("root_motion_source", source_enum.GENERATE_FROM_TARGET_PELVIS),
                                     ("root_height_source", height_enum.SNAP_TO_GROUND),
                                     ("maintain_offset_from_pelvis", False), ("rotate_with_pelvis", False)],
                          "root motion op")
    if len(applied) != 4:
        return "FAILED: root motion op took only " + ", ".join(applied)
    op.set_settings(settings)
    return "root motion generated from the target pelvis: ground height, horizontal only"


# The target's IK bones and the bones they follow. The Game Animation Sample's AnimBP runs leg IK toward
# ik_foot_l and ik_foot_r; a retarget leaves them at their reference pose unless something moves them,
# and every Mixamo clip then had both feet pulled back to the reference stance in game (a kick never
# left the ground). Epic's own clips carry them on the feet and hands; the Pin Bones op does the same.
IK_BONE_PINS = (("foot_l", "ik_foot_l"), ("foot_r", "ik_foot_r"), ("hand_l", "ik_hand_l"), ("hand_r", "ik_hand_r"),
                ("hand_r", "ik_hand_gun"))


def _add_ik_bone_pins(ctl, target_mesh):
    """A Pin Bones op at the bottom of the stack that puts each IK bone exactly on the bone it follows
    (they coincide in both mannequins' reference poses). Not "maintain offset": the op measures that
    offset in the retarget pose, where the aligned legs have moved off the IK bones, and it came out
    3 to 8 cm. Returns a note for the log."""
    bones = skeleton_bones(mesh_skeleton(target_mesh))
    pairs = [(frm, to) for frm, to in IK_BONE_PINS if frm in bones and to in bones]
    if not pairs:
        return "no IK bones to pin"
    index = ctl.add_retarget_op("/Script/IKRig.IKRetargetPinBoneOp")
    op = ctl.get_op_controller(index) if index is not None and index >= 0 else None
    if op is None or not hasattr(op, "set_bone_pair"):
        return "FAILED: no Pin Bones op; the IK bones stay at their reference pose"
    for frm, to in pairs:
        op.set_bone_pair(frm, to)
    settings = op.get_settings()
    translation = getattr(unreal, "PinBoneTranslationMode", None)
    rotation = getattr(unreal, "PinBoneRotationMode", None)
    if translation is None or rotation is None:
        return "FAILED: the Pin Bones modes are not exposed to Python"
    applied = c.set_props(settings, [
        ("skeleton_to_copy_from", unreal.RetargetSourceOrTarget.TARGET),
        ("translation_mode", translation.COPY_GLOBAL_POSITION),
        ("rotation_mode", rotation.COPY_GLOBAL_ROTATION)], "pin bones op")
    if len(applied) != 3:
        return "FAILED: pin bones op took only " + ", ".join(applied)
    op.set_settings(settings)
    return "{0} IK bones pinned".format(len(op.get_all_bone_pairs()))


def ensure_retargeter(path, source_rig, source_mesh, source_chain_map, target):
    stamp = fingerprint(SCRIPT_BUILD, source_rig.get_path_name(), target["rig"].get_path_name(), tag_of(source_rig),
                        tag_of(target["rig"]))
    existing = c.load_or_none(path)
    if existing is not None and not FORCE and tag_of(existing) == stamp:
        c.log("exists", path)
        return existing
    folder, name = path.rsplit("/", 1)
    delete_if_exists(path)
    rtg, _created = c.create_asset(name, folder, unreal.IKRetargeter, unreal.IKRetargetFactory(), quiet=True)
    if rtg is None:
        c.log("FAILED", path, "could not create the IK Retargeter")
        return None
    side = unreal.RetargetSourceOrTarget
    ctl = unreal.IKRetargeterController.get_controller(rtg)
    ctl.set_ik_rig(side.SOURCE, source_rig)
    ctl.set_ik_rig(side.TARGET, target["rig"])
    ctl.set_preview_mesh(side.SOURCE, source_mesh)
    ctl.set_preview_mesh(side.TARGET, target["mesh"])
    # Exactly one default stack. The factory may already have put one in, and a retargeter the delete
    # above could not remove (still loaded) comes back with every earlier run's stack: a second root
    # motion op at its defaults undid the settings made on the first, and the pelvis height went back
    # onto the root.
    ctl.remove_all_ops()
    ctl.add_default_ops()
    pin_note = _add_ik_bone_pins(ctl, target["mesh"])
    ctl.assign_ik_rig_to_all_ops(side.SOURCE, source_rig)
    ctl.assign_ik_rig_to_all_ops(side.TARGET, target["rig"])
    ctl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    root_note = _configure_root_motion(ctl, source_chain_map, target["mesh"])
    # T-pose sources (Mixamo) against the A-pose mannequins: line the target's chains up with the
    # source's in a retarget pose of its own, so the arms are not rotated 45 degrees all clip long.
    pose = ctl.create_retarget_pose("HawkeyeAligned", side.TARGET)
    ctl.set_current_retarget_pose(pose, side.TARGET)
    ctl.auto_align_all_bones(side.TARGET, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    mapped = 0
    for chain in unreal.IKRigController.get_controller(target["rig"]).get_retarget_chains():
        if str(ctl.get_source_chain(chain.get_editor_property("chain_name"))) not in ("", "None"):
            mapped += 1
    set_tag(rtg, stamp)
    c.save(rtg)
    c.log("FAILED" if pin_note.startswith("FAILED") else "created", path, "{0} ops, {1} target chains mapped, {2}, {3}".format(
        ctl.get_num_retarget_ops(), mapped, root_note, pin_note))
    return rtg


# --------------------------------------------------------------------------------------------------
# sources
# --------------------------------------------------------------------------------------------------


def _import_task(filename, destination, name, options):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = destination
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.options = options
    c.asset_tools().import_asset_tasks([task])
    return [unreal.load_asset(p) for p in task.get_editor_property("imported_object_paths")]


def _anim_import_data(options):
    data = options.get_editor_property("anim_sequence_import_data")
    # Mixamo exports at 30 fps; a clip whose length is not a whole number of frames snaps to one
    # rather than failing the import.
    c.set_props(data, [("import_bone_tracks", True), ("remove_redundant_keys", False),
                       ("use_default_sample_rate", False), ("custom_sample_rate", 30),
                       ("snap_to_closest_frame_boundary", True)], "anim import data")
    return data


def _fbx_name(filename):
    return re.sub(r"[^A-Za-z0-9]", "", os.path.splitext(os.path.basename(filename))[0]) or "Clip"


def import_fbx_source(source, clips, folder):
    """Imports the mesh file (once) and each clip file onto its skeleton. {clip index: AnimSequence}."""
    base = SOURCES_ROOT + "/" + source["name"]
    mesh_file = source.get("mesh_file")
    mesh_path = os.path.join(folder, mesh_file) if mesh_file else None
    mesh_asset = base + "/SK_" + _fbx_name(mesh_file or "Mesh")
    mesh = c.load_or_none(mesh_asset)
    if mesh is None or FORCE:
        if not mesh_path or not os.path.isfile(mesh_path):
            c.log("missing", mesh_path or source["name"] + " mesh_file", "download one clip With Skin under this name")
            return None, {}
        options = unreal.FbxImportUI()
        # The mesh alone: one asset name for one asset. Its animation, if it is a clip too, comes in
        # below onto the skeleton like every other clip.
        c.set_props(options, [("import_mesh", True), ("import_as_skeletal", True), ("import_animations", False),
                              ("import_materials", False), ("import_textures", False), ("create_physics_asset", False),
                              ("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)], "fbx mesh import")
        _anim_import_data(options)
        imported = _import_task(mesh_path, base, "SK_" + _fbx_name(mesh_file), options)
        mesh = next((o for o in imported if isinstance(o, unreal.SkeletalMesh)), None)
        if mesh is None:
            c.log("FAILED", mesh_path, "no skeletal mesh in it: download it With Skin")
            return None, {}
        for obj in imported:
            c.save(obj)
        c.log("imported", mesh.get_path_name().split(".")[0], "skeleton {0}, bones: {1}".format(
            mesh_skeleton(mesh).get_name(), ", ".join(skeleton_bones(mesh_skeleton(mesh)))))
    else:
        c.log("exists", mesh_asset)
    skeleton = mesh_skeleton(mesh)

    found = {}
    this_run = {}
    for index, clip in clips:
        filename = os.path.join(folder, clip["file"])
        if clip["file"] in this_run:
            # Two roles from one file (Knocked Out is a knockdown and a finisher's victim): one import.
            found[index] = this_run[clip["file"]]
            continue
        anim_path = base + "/A_Src_" + _fbx_name(clip["file"])
        anim = c.load_or_none(anim_path)
        if anim is not None and not FORCE:
            found[index] = anim
            this_run[clip["file"]] = anim
            continue
        if not os.path.isfile(filename):
            c.log("missing", filename, "not downloaded yet; skipped")
            continue
        options = unreal.FbxImportUI()
        c.set_props(options, [("import_mesh", False), ("import_as_skeletal", True), ("import_animations", True),
                              ("skeleton", skeleton), ("import_materials", False), ("import_textures", False),
                              ("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)], "fbx anim import")
        _anim_import_data(options)
        imported = [o for o in _import_task(filename, base, "A_Src_" + _fbx_name(clip["file"]), options)
                    if isinstance(o, unreal.AnimSequence)]
        if not imported:
            c.log("FAILED", filename, "imported no animation onto " + skeleton.get_name())
            continue
        c.save(imported[0])
        found[index] = imported[0]
        this_run[clip["file"]] = imported[0]
        c.log("imported", imported[0].get_path_name().split(".")[0], "{0:.2f} s".format(
            unreal.AnimationLibrary.get_sequence_length(imported[0])))
    return mesh, found


def import_project_source(source, clips, content):
    """Copies the clips and the mesh (with their hard closure) from another Content folder. {index: anim}."""
    # Plus anything else of this source's the run needs (an aim offset's layout).
    roots = [source["skeletal_mesh"]] + [clip["asset"] for _index, clip in clips] + list(source.get("_extra_roots", []))
    plan = _packages.plan(content, roots)
    for package in plan["missing"]:
        if package in roots:
            c.log("missing", package, "not in " + content)
    copied, present = _packages.copy(plan, os.path.join(c.project_dir(), "Content"))
    if copied:
        scan(set("/".join(p.split("/")[:-1]) for p in copied))

    c.log("copied" if copied else "exists", source["name"] + " packages",
          "{0} copied, {1} already here, closure {2:.0f} MB".format(len(copied), len(present), plan["bytes"] / 1e6))
    mesh = load_path(source["skeletal_mesh"])
    if mesh is None:
        c.log("FAILED", source["skeletal_mesh"], "the source mesh did not load")
        return None, {}
    found = {}
    for index, clip in clips:
        anim = load_path(clip["asset"])
        if isinstance(anim, unreal.AnimSequence):
            found[index] = anim
        elif anim is None:
            c.log("missing", clip["asset"], "copied but did not load" if clip["asset"] not in plan["missing"] else "not in " + content)
        else:
            c.log("skipped", clip["asset"], "a {0}, not an animation sequence".format(type(anim).__name__))
    return mesh, found


# --------------------------------------------------------------------------------------------------
# retarget and montage per clip
# --------------------------------------------------------------------------------------------------


def retarget_clip(anim, source_mesh, rtg, target, out_folder, seq_name):
    seq_path = out_folder + "/" + seq_name
    source_path = anim.get_path_name().split(".")[0]
    if c.exists(seq_path) and not FORCE:
        existing = c.load_or_none(seq_path)
        # Only if retargeted from this clip: the same name from another clip (a manifest line pointed
        # elsewhere), or one from before the tag, is redone.
        if existing is not None and tag_of(existing, FROM_TAG) == source_path:
            return existing, "exists"
    delete_if_exists(seq_path)
    c.ensure_directory(out_folder)
    inputs = unreal.IKRetargetBatchOperationInputs()
    c.set_props(inputs, [
        ("assets_to_retarget", [unreal.EditorAssetLibrary.find_asset_data(anim.get_path_name().split(".")[0])]),
        ("source_mesh", source_mesh), ("target_mesh", target["mesh"]), ("ik_retarget_asset", rtg),
        ("search", anim.get_name()), ("replace", seq_name), ("prefix", ""), ("suffix", ""),
        ("target_path", out_folder), ("use_source_path", False), ("include_referenced_assets", False),
        # Paragon's hit reactions are additive over its idle. Additive off retargets the full pose
        # (idle plus the hit), which is what a full-body slot plays; kept on, the copy would point at
        # Sparrow's idle on Sparrow's skeleton.
        ("overwrite_existing_files", True), ("retain_additive_flags", False)], "batch retarget")
    results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    seq = c.load_or_none(seq_path)
    if seq is None:
        names = [str(r.get_editor_property("package_name")) for r in (results or [])]
        return None, "retarget produced {0}, not {1}".format(names or "nothing", seq_path)
    set_tag(seq, source_path, FROM_TAG)
    return seq, "retargeted"


def apply_root_motion(seq, layout, target):
    wanted = bool(layout.get("root_motion", False)) and bool(target["config"].get("root_motion", False))
    unreal.AnimationLibrary.set_root_motion_enabled(seq, wanted)
    # In place otherwise: the capsule, the dash or the lunge moves the character, not the clip.
    unreal.AnimationLibrary.set_is_root_motion_lock_forced(seq, not wanted)
    return wanted


def montage_windows(layout, length):
    """The montage spec's windows as fractions of the sequence: hit, combo and warp_end from the
    manifest's seconds in the source clip (hit_s, combo_s, warp_end_s) when given, else its fractions
    (hit, combo, warp_end) or the role's. With hit_s and no warp end, the warp runs to the hit."""
    length = max(float(length), 1e-3)

    def window(key):
        if layout.get(key + "_s"):
            return [float(t) / length for t in layout[key + "_s"]]
        return [float(f) for f in (layout.get(key) or [-1.0, -1.0])]

    if layout.get("warp_end_s") is not None:
        warp_end = float(layout["warp_end_s"]) / length
    elif layout.get("hit_s") and "warp_end" not in layout.get("_explicit", ()):
        warp_end = float(layout["hit_s"][0]) / length
    else:
        warp_end = float(layout.get("warp_end", -1.0))
    return window("hit"), window("combo"), warp_end


def build_montage(seq, montage_path, layout, root_motion, characters, source_name, fresh_sequence=False):
    """fresh_sequence: the sequence was (re)made this run, so an existing montage points at the old one."""
    spec = unreal.HawkeyeCombatMontageSpec()
    hit, combo, warp_end = montage_windows(layout, unreal.AnimationLibrary.get_sequence_length(seq))
    c.set_props(spec, [
        ("slot_name", layout.get("slot", "DefaultSlot")),
        ("hit_start", float(hit[0])), ("hit_end", float(hit[1])),
        ("combo_start", float(combo[0])), ("combo_end", float(combo[1])),
        ("clip_start_seconds", float(layout.get("start_s", 0.0))), ("clip_end_seconds", float(layout.get("end_s", -1.0))),
        ("play_rate", float(layout.get("rate", 1.0))),
        ("warp_end", warp_end if root_motion else -1.0),
        ("loop", bool(layout.get("loop", False))), ("hold_last_frame", bool(layout.get("hold", False))),
        ("blend_in_seconds", float(layout.get("blend_in", 0.1))), ("blend_out_seconds", float(layout.get("blend_out", 0.2))),
    ], "montage spec")
    stamp = fingerprint(SCRIPT_BUILD, layout, root_motion, seq.get_path_name())
    existing = c.load_or_none(montage_path)
    if existing is not None and not FORCE and not fresh_sequence and tag_of(existing) == stamp:
        return existing, "exists"
    if existing is not None and (FORCE or fresh_sequence):
        delete_if_exists(montage_path)
    montage = unreal.HawkeyeCombatMontageBuilder.build_combat_montage(seq, montage_path, spec)
    if montage is None:
        return None, "the montage builder refused (see LogHawkeye)"
    set_tag(montage, stamp)
    set_tag(montage, ",".join(characters), CHARACTERS_TAG)
    set_tag(montage, source_name, SOURCE_TAG)
    c.save(montage)
    skeleton = seq.get_editor_property("skeleton")
    if skeleton is not None:
        c.save(skeleton, only_if_dirty=True)
    return montage, "built"


def process_clip(clip, anim, source, source_mesh, retargeters, targets, output_override=None):
    """Retargets one clip to each target it needs and builds the montages. Returns the results."""
    seq_name, montage_name = asset_names(clip)
    layout = clip_layout(clip)
    results = []
    for target_name, characters in targets_for(clip, targets):
        target = targets[target_name]
        rtg = retargeters.get(target_name)
        out_folder = output_override(target) if output_override else target["config"]["output"]
        where = out_folder + "/" + seq_name
        if rtg is None:
            c.log("skipped", where, "no retargeter to " + target_name)
            continue
        seq, how = retarget_clip(anim, source_mesh, rtg, target, out_folder, seq_name)
        if seq is None:
            c.log("FAILED", where, how)
            continue
        root_motion = apply_root_motion(seq, layout, target)
        c.save(seq, only_if_dirty=True)
        if clip["role"].startswith(AIM_POSE_PREFIX):
            # An aim pose: no montage; build_aim_offsets makes it additive and lays it into its blend space.
            c.log("created" if how == "retargeted" else "exists", where, "aim pose {0} for {1}".format(
                clip.get("asset", "").rsplit("/", 1)[-1], ",".join(characters)))
            results.append({"clip": clip, "anim": anim, "target": target_name, "sequence": seq, "montage": None,
                            "fresh": how == "retargeted"})
            continue
        montage, built = build_montage(seq, out_folder + "/" + montage_name, layout, root_motion, characters, source["name"],
                                       fresh_sequence=how == "retargeted")
        if montage is None:
            c.log("FAILED", out_folder + "/" + montage_name, built)
            continue
        c.log("created" if how == "retargeted" or built == "built" else "exists", out_folder + "/" + montage_name,
              "{0} for {1}: {2}".format(clip.get("file") or clip.get("asset", "").rsplit("/", 1)[-1], ",".join(characters),
                                        unreal.HawkeyeCombatMontageBuilder.describe_montage(montage)))
        report_pose(clip, seq, target_name)
        results.append({"clip": clip, "anim": anim, "target": target_name, "sequence": seq, "montage": montage})
    return results


LYING_ROLES = ("Knockdown", "FinisherVictim")
AIRBORNE_OR_LOW_ROLES = ("GetUp", "FinisherAttacker", "FinisherBow")


def report_pose(clip, seq, target_name):
    """One line of raw-bone measures per retargeted clip, flagged "check" when the pose is not what
    the role expects: a standing clip whose pelvis is far from the reference height (a track on the
    wrong bone, a bad scale), or a knockdown that never reaches the floor. Nobody has to open it."""
    try:
        length = unreal.AnimationLibrary.get_sequence_length(seq)
        start, mid, end = (pose_metrics(seq, length * f) for f in (0.0, 0.5, 1.0))
    except Exception as exc:  # noqa: BLE001 - a report, never a failure
        c.log("check", seq.get_path_name().split(".")[0], "could not measure: {0}".format(exc))
        return
    role = clip["role"]
    if role in LYING_ROLES:
        ok = end["pelvis_locked_height"] < 45.0
        expect = "ends on the floor"
    elif role in AIRBORNE_OR_LOW_ROLES:
        ok = True
        expect = "free"
    else:
        ok = all(abs(m["pelvis_vs_reference"]) < 35.0 and abs(m["pelvis_locked_vs_reference"]) < 35.0
                 and m["hip_height"] > 45.0 and 40.0 < m["head_height"] < 95.0 for m in (start, mid, end))
        expect = "stands"
    c.log("pose" if ok else "check", seq.get_path_name().split(".")[0],
          "{0} on {1} ({2}): pelvis {3:.0f}/{4:.0f}/{5:.0f} cm (ref {6:+.0f}), root locked {7:.0f}/{8:.0f}/{9:.0f}, head +{10:.0f},"
          " bow hand fwd {11:.0f}, hands apart {12:.0f}".format(
              role, target_name, expect, start["pelvis_height"], mid["pelvis_height"], end["pelvis_height"],
              mid["pelvis_vs_reference"], start["pelvis_locked_height"], mid["pelvis_locked_height"],
              end["pelvis_locked_height"], mid["head_height"], mid["bow_hand_forward"], mid["hands_apart"]))


# --------------------------------------------------------------------------------------------------
# the run
# --------------------------------------------------------------------------------------------------


def source_folder(source):
    envs = source.get("path_env", [])
    if isinstance(envs, str):
        envs = [envs]
    path = next((os.environ[n] for n in envs if os.environ.get(n)), "") or source["path"]
    # {NAME} in a manifest path is an environment variable; the launcher scripts export HAWKEYE_UE_ROOT and
    # HAWKEYE_GASP_CONTENT from Tools\hawkeye.env.ps1, and the path as written is the fallback.
    path = re.sub(r"\{(\w+)\}", lambda m: os.environ.get(m.group(1), m.group(0)), path)
    if not os.path.isabs(path):
        path = os.path.join(REPO, path.replace("/", os.sep))
    return path


def process_source(source, clips, targets, chain_maps, output_override=None):
    folder = source_folder(source)
    if not os.path.isdir(folder):
        c.log("missing", source["name"], "no folder at {0}; its {1} clip(s) skipped".format(folder, len(clips)))
        return []
    if source["kind"] == "mixamo_fbx":
        present = [clip for _i, clip in clips if os.path.isfile(os.path.join(folder, clip["file"]))]
        if not present:
            c.log("missing", source["name"], "none of its {0} file(s) are in {1} yet".format(len(clips), folder))
            return []
        mesh, anims = import_fbx_source(source, clips, folder)
    elif source["kind"] == "uasset_project":
        mesh, anims = import_project_source(source, clips, folder)
    else:
        c.log("FAILED", source["name"], "unknown kind " + source["kind"])
        return []
    if mesh is None:
        return []
    rig = ensure_ik_rig("{0}/{1}/IK_{1}".format(SOURCES_ROOT, source["name"]), mesh, source["chains"], chain_maps)
    if rig is None:
        return []
    needed = set(name for _i, clip in clips for name, _chars in targets_for(clip, targets))
    retargeters = {}
    for name in sorted(needed):
        retargeters[name] = ensure_retargeter("{0}/{1}/RTG_{1}_To_{2}".format(SOURCES_ROOT, source["name"], name.upper()),
                                              rig, mesh, chain_maps.get(source["chains"]), targets[name])
    results = []
    for index, clip in clips:
        anim = anims.get(index)
        if anim is None:
            continue
        results += process_clip(clip, anim, source, mesh, retargeters, targets, output_override)
    return results


def run(manifest=None, args=None, output_override=None):
    global FORCE
    manifest = manifest or load_manifest()
    args = args or {"force": False, "only": None}
    FORCE = bool(args.get("force"))
    chain_maps = manifest["chain_maps"]
    targets = {}
    for name, config in manifest["targets"].items():
        target = ensure_target(name, config, chain_maps)
        if target is not None:
            targets[name] = target
    sources = dict((s["name"], dict(s)) for s in manifest["sources"])
    for aim in manifest.get("aim_offsets", []):
        if aim.get("source") in sources:
            sources[aim["source"]].setdefault("_extra_roots", []).append(aim["layout"])
    results = []
    for name, source in sources.items():
        if args.get("only") and args["only"] != name:
            continue
        clips = [(i, clip) for i, clip in enumerate(manifest["clips"]) if clip["source"] == name]
        if not clips:
            continue
        try:
            results += process_source(source, clips, targets, chain_maps, output_override)
        except Exception as exc:  # noqa: BLE001 - one broken source must not stop the rest
            c.log_error("source " + name, exc)
    for aim in manifest.get("aim_offsets", []):
        if args.get("only") and args["only"] != aim.get("source"):
            continue
        try:
            build_aim_offsets(aim, results, targets, output_override)
        except Exception as exc:  # noqa: BLE001
            c.log_error("aim offset " + aim.get("name", "?"), exc)
    return results


# --------------------------------------------------------------------------------------------------
# aim offsets
# --------------------------------------------------------------------------------------------------


def build_aim_offsets(aim, results, targets, output_override=None):
    """Per target: the aim poses of aim's source and variant made mesh-space additive on base_role's pose,
    then the AimOffset blend space laid out as aim["layout"] (the source's own aim offset)."""
    layout = load_path(aim["layout"])
    if not isinstance(layout, unreal.BlendSpace):
        c.log("missing", aim["layout"], "no source aim offset to lay {0} out from".format(aim["name"]))
        return
    for target_name in sorted(targets):
        poses = [r for r in results if r["target"] == target_name and r["clip"]["role"].startswith(AIM_POSE_PREFIX)
                 and r["clip"].get("source") == aim["source"] and r["clip"].get("variant") == aim.get("variant")]
        if not poses:
            continue
        base = next((r for r in poses if r["clip"]["role"] == aim["base_role"]), None)
        folder = output_override(targets[target_name]) if output_override else targets[target_name]["config"]["output"]
        path = folder + "/" + aim["name"]
        if base is None:
            c.log("FAILED", path, "no {0} pose retargeted to {1}".format(aim["base_role"], target_name))
            continue
        # The base first: Sparrow's centre pose is additive on itself (a zero offset), like the source's.
        made = []
        for r in [base] + [p for p in poses if p is not base]:
            if unreal.HawkeyeCombatMontageBuilder.make_mesh_space_additive(r["sequence"], base["sequence"]):
                c.save(r["sequence"])
                made.append(r["sequence"].get_name())
        stamp = fingerprint(SCRIPT_BUILD, aim, sorted(r["sequence"].get_path_name() for r in poses))
        existing = c.load_or_none(path)
        fresh = made or any(r.get("fresh") for r in poses)
        if existing is not None and not FORCE and not fresh and tag_of(existing) == stamp:
            c.log("exists", path, unreal.HawkeyeCombatMontageBuilder.describe_blend_space(existing))
            continue
        space = unreal.HawkeyeCombatMontageBuilder.build_aim_offset(path, layout, [r["anim"] for r in poses],
                                                                   [r["sequence"] for r in poses])
        if space is None:
            c.log("FAILED", path, "the aim offset builder refused (see LogHawkeye)")
            continue
        set_tag(space, stamp)
        c.save(space)
        c.log("updated" if existing is not None else "created", path, "{0} on {1}{2}: {3}".format(
            len(poses), target_name, ", made additive: " + ", ".join(made) if made else "",
            unreal.HawkeyeCombatMontageBuilder.describe_blend_space(space)))


# --------------------------------------------------------------------------------------------------
# self-test
# --------------------------------------------------------------------------------------------------


def _bone(anim, name, time, root_locked=False):
    return unreal.HawkeyeCombatMontageBuilder.get_raw_bone_location(anim, name, time, root_locked)


def _ref_bone(skeleton, name):
    ref = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    return unreal.AnimPoseExtensions.get_ref_bone_pose(ref, name, unreal.AnimPoseSpaces.WORLD).translation


def _dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2) ** 0.5


POSE_BONES = ("pelvis", "spine_01", "head", "upperarm_l", "lowerarm_l", "hand_l", "hand_r", "thigh_l", "foot_l")


def pose_metrics(anim, time):
    """Component-space measures of anim at time, from its raw bone tracks (UHawkeyeCombatMontageBuilder::
    GetRawBoneLocation): the pelvis against the skeleton's reference height, the hips and head against
    it, the bow arm's extension, the hands forward of the body (the mannequins and Paragon heroes face +Y)."""
    skeleton = anim.get_editor_property("skeleton")
    at = dict((bone, _bone(anim, bone, time)) for bone in POSE_BONES)
    ref = dict((bone, _ref_bone(skeleton, bone)) for bone in ("pelvis", "upperarm_l", "lowerarm_l", "hand_l"))
    arm = _dist(ref["upperarm_l"], ref["lowerarm_l"]) + _dist(ref["lowerarm_l"], ref["hand_l"])
    pelvis = at["pelvis"]
    # With the root held at its reference pose, as the game holds it (root motion extracted or the
    # root locked): a clip whose root track carries the pelvis's height reads right above and on the
    # floor here.
    locked = _bone(anim, "pelvis", time, True)
    return {
        "pelvis_height": pelvis.z,
        "pelvis_vs_reference": pelvis.z - ref["pelvis"].z,
        "pelvis_locked_height": locked.z,
        "pelvis_locked_vs_reference": locked.z - ref["pelvis"].z,
        "hip_height": at["thigh_l"].z,
        "head_height": at["head"].z - pelvis.z,
        "bow_arm_extension": _dist(at["upperarm_l"], at["hand_l"]) / max(arm, 1.0),
        "bow_hand_forward": at["hand_l"].y - pelvis.y,
        "string_hand_forward": at["hand_r"].y - pelvis.y,
        "bow_hand_height": at["hand_l"].z - pelvis.z,
        "hands_apart": _dist(at["hand_l"], at["hand_r"]),
    }


def check_ik_bones_follow(test, name, anim, length):
    """The IK bones sit on the feet they follow, at their reference-pose offset, on the clip's keys: the
    sample's leg IK pulls the feet to ik_foot_l and ik_foot_r, so a stray one plants a foot. (Between keys
    a foot swinging on its thigh arcs while the IK bone's track goes straight, so only keys are compared.)"""
    skeleton = anim.get_editor_property("skeleton")
    bones = skeleton_bones(skeleton)
    frames = max(int(unreal.AnimationLibrary.get_num_frames(anim)), 1)
    for frm, to in IK_BONE_PINS[:2]:
        if frm not in bones or to not in bones:
            continue
        ref_offset = _ref_bone(skeleton, to) - _ref_bone(skeleton, frm)
        worst = 0.0
        for f in (0.0, 0.25, 0.5, 0.75, 1.0):
            t = length * round(f * frames) / frames
            offset = _bone(anim, to, t) - _bone(anim, frm, t)
            worst = max(worst, _dist(offset, ref_offset))
        test.check(worst < 3.0, "{0}: {1} follows {2} (off by up to {3:.1f} cm)".format(name, to, frm, worst))


def check_standing(test, name, target, anim, length):
    """A clip that should stand (all but the knockdowns) has its pelvis near the reference height, the
    hips on it and the head above it: a track written onto the wrong bone puts the pelvis on the floor."""
    for when in (0.0, 0.5):
        m = pose_metrics(anim, length * when)
        ok = abs(m["pelvis_vs_reference"]) < 35.0 and m["hip_height"] > 50.0 and 45.0 < m["head_height"] < 90.0
        test.check(ok, "{0} on {1} at {2:.0%}: standing (pelvis {3:.0f} cm, {4:+.0f} from reference; hips {5:.0f};"
                   " head {6:.0f} above)".format(name, target, when, m["pelvis_height"], m["pelvis_vs_reference"],
                                                  m["hip_height"], m["head_height"]))
        # The game never plays the root track as a pose: it extracts it as root motion or locks it.
        test.check(abs(m["pelvis_locked_vs_reference"]) < 15.0,
                   "{0} on {1} at {2:.0%}: pelvis {3:.0f} cm with the root locked, {4:+.0f} from the reference"
                   " (the root track must not carry the pelvis's height)".format(
                       name, target, when, m["pelvis_locked_height"], m["pelvis_locked_vs_reference"]))


class SelfTest(object):
    def __init__(self):
        self.failures = []
        self.checks = 0

    def check(self, ok, what):
        self.checks += 1
        c.log("PASS" if ok else "FAIL", "selftest", what)
        if not ok:
            self.failures.append(what)


def selftest_output(target):
    return target["config"]["output"] + "/SelfTest"


def run_selftest(args):
    manifest = load_manifest()
    block = manifest["selftest"]
    chain_maps = dict(manifest["chain_maps"])
    chain_maps.update(block.get("chain_maps", {}))
    test_manifest = {
        "targets": manifest["targets"],
        "chain_maps": chain_maps,
        "sources": block["sources"] + [s for s in manifest["sources"] if s["name"] == "ParagonSparrow"],
        "clips": block["clips"],
    }
    results = run(test_manifest, {"force": True, "only": args.get("only")}, output_override=selftest_output)
    test = SelfTest()
    expected = {}
    for clip in block["clips"]:
        for target_name, _chars in targets_for(clip, manifest["targets"]):
            expected[(asset_names(clip)[0], target_name)] = clip
    got = dict(((r["sequence"].get_name(), r["target"]), r) for r in results)
    for key, clip in sorted(expected.items()):
        source_ok = os.path.isdir(source_folder(next(s for s in test_manifest["sources"] if s["name"] == clip["source"])))
        if not source_ok:
            c.log("skipped", "selftest " + key[0], "no source folder for " + clip["source"])
            continue
        result = got.get(key)
        test.check(result is not None, "{0} on {1} was retargeted and has a montage".format(*key))
        if result is None:
            continue
        seq, anim, montage = result["sequence"], result["anim"], result["montage"]
        target_skeleton = manifest["targets"][key[1]]["skeletal_mesh"]
        skeleton = seq.get_editor_property("skeleton")
        mesh_skel = mesh_skeleton(c.load_or_none(target_skeleton))
        test.check(skeleton == mesh_skel, "{0}: on the target skeleton {1}".format(key[0], mesh_skel.get_name()))
        src_len = unreal.AnimationLibrary.get_sequence_length(anim)
        out_len = unreal.AnimationLibrary.get_sequence_length(seq)
        test.check(abs(src_len - out_len) <= 1.0 / 29.0,
                   "{0}: length {1:.3f} s matches the source's {2:.3f} s".format(key[0], out_len, src_len))
        tracks = [str(t) for t in unreal.AnimationLibrary.get_animation_track_names(seq)]
        bones = skeleton_bones(skeleton)
        wanted = [b for b in ("pelvis", "spine_01", "upperarm_l", "hand_r", "thigh_r") if b in bones]
        # Virtual bones ("vb hand_l_prop_01" on the UEFN mannequin) carry tracks without being in the hierarchy.
        strays = [t for t in tracks if t not in bones and not t.startswith("vb ")]
        test.check(len(tracks) > 0 and not strays, "{0}: {1} bone tracks, all on the target skeleton{2}".format(
            key[0], len(tracks), " (not on it: " + ", ".join(strays[:8]) + ")" if strays else ""))
        test.check(len(wanted) == 5, "{0}: target skeleton has pelvis, spine_01, upperarm_l, hand_r, thigh_r".format(key[0]))
        additive = seq.get_editor_property("additive_anim_type")
        test.check(additive == unreal.AdditiveAnimationType.AAT_NONE,
                   "{0}: a full pose, not additive ({1})".format(key[0], additive))
        described = unreal.HawkeyeCombatMontageBuilder.describe_montage(montage)
        layout = clip_layout(clip)
        if layout.get("hit"):
            test.check("hit=-" not in described, "{0}: montage has its ANS_HitWindow ({1})".format(key[0], described))
        test.check("slot=" + layout.get("slot", "DefaultSlot") in described, "{0}: montage slot {1}".format(key[0], layout.get("slot")))
        if clip["role"] not in ("Knockdown", "GetUp", "FinisherVictim"):
            check_standing(test, key[0], key[1], seq, out_len)
        check_ik_bones_follow(test, key[0], seq, out_len)
        if clip["role"] == "BowDraw":
            for when in (0.5, 1.0):
                src_m = pose_metrics(anim, src_len * when)
                out_m = pose_metrics(seq, out_len * when)
                c.log("info", "selftest bow pose at {0:.0%}".format(when), "source {0} | {1} {2}".format(
                    _fmt(src_m), key[1], _fmt(out_m)))
                test.check(abs(out_m["bow_arm_extension"] - src_m["bow_arm_extension"]) < 0.15,
                           "{0} on {1} at {2:.0%}: bow arm extended as in the source ({3:.2f} vs {4:.2f})".format(
                               key[0], key[1], when, out_m["bow_arm_extension"], src_m["bow_arm_extension"]))
                test.check(out_m["bow_hand_forward"] > 30.0,
                           "{0} on {1} at {2:.0%}: bow hand out in front ({3:.0f} cm; source {4:.0f})".format(
                               key[0], key[1], when, out_m["bow_hand_forward"], src_m["bow_hand_forward"]))
                test.check(out_m["bow_hand_height"] > 0.0, "{0} on {1} at {2:.0%}: bow hand above the hips".format(
                    key[0], key[1], when))
            full_src, full_out = pose_metrics(anim, src_len), pose_metrics(seq, out_len)
            test.check(full_out["string_hand_forward"] < full_out["bow_hand_forward"] - 40.0,
                       "{0} on {1} at full draw: string hand drawn back behind the bow hand ({2:.0f} vs {3:.0f} cm;"
                       " source {4:.0f} vs {5:.0f})".format(key[0], key[1], full_out["string_hand_forward"],
                                                           full_out["bow_hand_forward"], full_src["string_hand_forward"],
                                                           full_src["bow_hand_forward"]))
    if not args.get("keep"):
        for target in manifest["targets"].values():
            folder = target["output"] + "/SelfTest"
            if unreal.EditorAssetLibrary.does_directory_exist(folder):
                unreal.EditorAssetLibrary.delete_directory(folder)
        c.log("deleted", "selftest outputs", "SelfTest folders removed (--keep keeps them)")
    if test.failures:
        unreal.log_error("[Hawkeye] selftest FAILED: {0} of {1} checks".format(len(test.failures), test.checks))
        for failure in test.failures:
            unreal.log_error("[Hawkeye]   " + failure)
    else:
        unreal.log("[Hawkeye] selftest PASSED: {0} checks".format(test.checks))
    return test


def _fmt(metrics):
    return " ".join("{0}={1:.2f}".format(k, v) for k, v in sorted(metrics.items()))


def main(argv):
    args = parse_args(argv)
    c.reset_summary()
    if args["selftest"]:
        run_selftest(args)
        c.print_summary("combat anims selftest")
        return
    run(load_manifest(), args)
    c.print_summary("combat anims")


if __name__ == "__main__":
    main(sys.argv[1:])
