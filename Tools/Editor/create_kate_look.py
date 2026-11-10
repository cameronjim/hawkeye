"""Kate's look: Paragon's Sparrow drawn over the motion-matched mannequin (claude-docs/animation.md, "Kate's
visible mesh").

The UEFN mannequin on BP_Kate's Mesh keeps driving everything (the sample's AnimBP, the montages, the bow and
hang IK, root motion, notifies, collision, sockets) but is hidden; a child skeletal mesh shows Sparrow, posed
from it every frame by a Retarget Pose From Mesh AnimBP, as the Game Animation Sample shows its retargeted
characters. With Sparrow absent (a fresh clone: Paragon is git-ignored) this logs one line and changes nothing,
and BP_Kate keeps the mannequin at runtime.

    /Game/Characters/Kate/Look/IK_Kate_Sparrow        IK Rig on Sparrow: the manifest's ue4 chains, a goal on
                                                      each hand and foot, one limb solver per limb
    /Game/Characters/Kate/Look/RTG_Kate_To_Sparrow    IK Retargeter IK_Kate_UEFN -> IK_Kate_Sparrow: the default
                                                      op stack, chains by name, the target's retarget pose aligned
                                                      to the mannequin's; the hands blend fully to the driver's
                                                      (place and turn), so the bow, the string and a ledge are in
                                                      them; the feet keep the retarget's own placement
    /Game/Characters/Kate/Look/ABP_Kate_Sparrow       its AnimBP (UHawkeyeRetargetGraphBuilder): the retarget from
                                                      the parent mesh, nothing else
    /Game/Characters/Kate/Look/MI_Kate_Sparrow_*      purple instances of Sparrow's cloth materials; Paragon's own
                                                      assets are never edited
    BP_Kate.BodyLook                                  the soft references (AHawkeyeCharacter::BodyLook), and the
                                                      bones to hide: Sparrow's own bow and nocked arrow (Kate holds
                                                      the game's bow, whose string the bow IK draws)

The Look folder is git-ignored: everything in it is built from Paragon's content and rebuilt by this script.
Idempotent: every asset carries a build tag and is rebuilt only when it changes; BP_Kate is saved only when its
BodyLook differs.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _materials as m  # noqa: E402
import import_combat_anims as ica  # noqa: E402

LOOK_BUILD = "kate-look-3"   # bump when the rig, the retargeter or the AnimBP below changes
LOOK_PATH = "/Game/Characters/Kate/Look"
SPARROW_ROOT = "/Game/ParagonSparrow/Characters/Heroes/Sparrow"
SPARROW_MESH = SPARROW_ROOT + "/Meshes/Sparrow"
RIG = LOOK_PATH + "/IK_Kate_Sparrow"
RTG = LOOK_PATH + "/RTG_Kate_To_Sparrow"
ABP = LOOK_PATH + "/ABP_Kate_Sparrow"
KATE = "/Game/Blueprints/Player/BP_Kate"

# Chain -> (goal, the bone it is on, the limb solver's start bone).
LIMB_GOALS = {
    "LeftArm": ("LeftHandGoal", "hand_l", "upperarm_l"),
    "RightArm": ("RightHandGoal", "hand_r", "upperarm_r"),
    "LeftLeg": ("LeftFootGoal", "foot_l", "thigh_l"),
    "RightLeg": ("RightFootGoal", "foot_r", "thigh_r"),
}
# The hands go exactly where the driver's are, turned as they are: the bow is attached to the driver's hand and
# placed by its IK, a hang's hands are put on the ledge by the driver's IK, and a strike's fist is measured there.
HANDS_TO_DRIVER = ("LeftArm", "RightArm")

# Sparrow's own bow (bow_base and its shaft and string bones, under hand_l) and the arrow in her right hand.
HIDDEN_BONES = ["bow_base", "arrow_nock"]

# Material slot -> (Sparrow material, MI name, vector overrides). Purple cloth on the black and leather she has.
SLOT_TINTS = [
    (1, "M_Sparrow_Torso_Arms", "MI_Kate_Sparrow_TorsoArms", [
        ("Cloth Color 1", (0.16, 0.03, 0.30)),
        ("Cloth Color 2", (0.09, 0.02, 0.18)),
        ("Cloth Color 4", (0.05, 0.015, 0.10)),
    ]),
    (2, "M_ChestandBag", "MI_Kate_Sparrow_ChestBag", [
        ("Cloth Color 1", (0.16, 0.03, 0.30)),
        ("Cloth Color 2", (0.09, 0.02, 0.18)),
        ("Cloth Color 4", (0.05, 0.015, 0.10)),
    ]),
    (0, "M_Legs_Hands", "MI_Kate_Sparrow_LegsHands", [
        ("Cloth Color 2", (0.09, 0.02, 0.18)),
        ("Cloth Color 3", (0.05, 0.015, 0.10)),
        ("Cloth Color 4", (0.05, 0.015, 0.10)),
    ]),
]


def sparrow_present():
    return c.exists(SPARROW_MESH)


def _stamp(*parts):
    return ica.fingerprint(LOOK_BUILD, *parts)


def ensure_rig(mesh, chain_map):
    """IK_Kate_Sparrow: the ue4 chains (the names IK_Kate_UEFN uses), a goal and a limb solver per limb."""
    stamp = _stamp("rig", mesh.get_path_name(), chain_map)
    existing = c.load_or_none(RIG)
    if existing is not None and ica.tag_of(existing) == stamp:
        c.log("exists", RIG)
        return existing
    folder, name = RIG.rsplit("/", 1)
    rig = existing
    if rig is None:
        rig, _created = c.create_asset(name, folder, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory(), quiet=True)
    if rig is None:
        c.log("FAILED", RIG, "could not create the IK Rig")
        return None
    ctl = unreal.IKRigController.get_controller(rig)
    # Rebuilt in place, emptied first: a loaded rig cannot be deleted, and a second set of goals is refused.
    while ctl.get_num_solvers() > 0:
        ctl.remove_solver(0)
    for goal in list(ctl.get_all_goals()):
        ctl.remove_goal(goal.get_editor_property("goal_name"))
    for chain in list(ctl.get_retarget_chains()):
        ctl.remove_retarget_chain(chain.get_editor_property("chain_name"))
    ctl.set_skeletal_mesh(mesh)
    bones = ica.skeleton_bones(ica.mesh_skeleton(mesh))
    lower = dict((b.lower(), b) for b in bones)
    ctl.set_retarget_root(ica.resolve_bone(chain_map.get("retarget_root", "pelvis"), bones, lower))
    made = []
    for chain_name, (start, end) in chain_map["chains"].items():
        start_bone, end_bone = ica.resolve_bone(start, bones, lower), ica.resolve_bone(end, bones, lower)
        if not (start_bone and end_bone):
            continue
        goal = ""
        if chain_name in LIMB_GOALS:
            goal_name, goal_bone, solver_start = LIMB_GOALS[chain_name]
            goal = str(ctl.add_new_goal(goal_name, goal_bone))
            solver = ctl.add_solver("/Script/IKRig.IKRigLimbSolver")
            ctl.set_start_bone(solver_start, solver)
            ctl.connect_goal_to_solver(goal, solver)
        ctl.add_retarget_chain(chain_name, start_bone, end_bone, goal)
        made.append(chain_name)
    ica.set_tag(rig, stamp)
    c.save(rig)
    c.log("updated" if existing is not None else "created", RIG, "{0} chains, {1} goals, {2} solvers".format(len(made), len(ctl.get_all_goals()), ctl.get_num_solvers()))
    return rig


def _hands_to_driver(ctl):
    """Every IK chains op: the hand chains' goals fully on the source's hands, place and turn. Returns a note."""
    op_class = getattr(unreal, "IKRetargetIKChainsController", None)
    if op_class is None:
        return "FAILED: no IK chains op controller"
    done = []
    for index in range(ctl.get_num_retarget_ops()):
        op = ctl.get_op_controller(index)
        if not isinstance(op, op_class):
            continue
        settings = op.get_settings()
        chains = list(settings.get_editor_property("chains_to_retarget"))
        for chain in chains:
            if str(chain.get_editor_property("target_chain_name")) in HANDS_TO_DRIVER:
                c.set_props(chain, [("blend_to_source", 1.0), ("blend_to_source_translation", 1.0),
                                    ("blend_to_source_rotation", 1.0)], "IK chain")
                done.append(str(chain.get_editor_property("target_chain_name")))
        settings.set_editor_property("chains_to_retarget", chains)
        op.set_settings(settings)
    return "hands on the driver's: " + (", ".join(done) if done else "FAILED, no hand chains in the IK op")


def ensure_retargeter(source_rig, source_mesh, target_rig, target_mesh):
    stamp = _stamp("rtg", source_rig.get_path_name(), ica.tag_of(source_rig), ica.tag_of(target_rig))
    existing = c.load_or_none(RTG)
    if existing is not None and ica.tag_of(existing) == stamp:
        c.log("exists", RTG)
        return existing
    ica.delete_if_exists(RTG)
    folder, name = RTG.rsplit("/", 1)
    rtg, _created = c.create_asset(name, folder, unreal.IKRetargeter, unreal.IKRetargetFactory(), quiet=True)
    if rtg is None:
        c.log("FAILED", RTG, "could not create the IK Retargeter")
        return None
    side = unreal.RetargetSourceOrTarget
    ctl = unreal.IKRetargeterController.get_controller(rtg)
    ctl.set_ik_rig(side.SOURCE, source_rig)
    ctl.set_ik_rig(side.TARGET, target_rig)
    ctl.set_preview_mesh(side.SOURCE, source_mesh)
    ctl.set_preview_mesh(side.TARGET, target_mesh)
    ctl.remove_all_ops()
    ctl.add_default_ops()
    # 5.8's default stack has no IK Chains op; without one the goals stay where the FK put them. It runs as a
    # child of Run IK Rig, which hands it the goals the target rig has.
    chains_op = ctl.add_retarget_op("/Script/IKRig.IKRetargetIKChainsOp")
    run_ik = ctl.get_index_of_op_by_name("Run IK Rig")
    if chains_op is not None and chains_op >= 0 and run_ik >= 0:
        ctl.set_parent_op_by_name(ctl.get_op_name(chains_op), "Run IK Rig")
    ctl.assign_ik_rig_to_all_ops(side.SOURCE, source_rig)
    ctl.assign_ik_rig_to_all_ops(side.TARGET, target_rig)
    ctl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    # Sparrow's bind pose against the mannequin's A-pose: line her chains up with the driver's.
    pose = ctl.create_retarget_pose("HawkeyeAligned", side.TARGET)
    ctl.set_current_retarget_pose(pose, side.TARGET)
    ctl.auto_align_all_bones(side.TARGET, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    hands = _hands_to_driver(ctl)
    ica.set_tag(rtg, stamp)
    c.save(rtg)
    c.log("FAILED" if "FAILED" in hands else "created", RTG, "{0} ops, {1}".format(ctl.get_num_retarget_ops(), hands))
    return rtg


def ensure_anim_bp(mesh, rtg):
    stamp = _stamp("abp", ica.tag_of(rtg))
    existing = c.load_or_none(ABP)
    if existing is not None and ica.tag_of(existing) == stamp:
        c.log("exists", ABP)
        return existing
    builder = getattr(unreal, "HawkeyeRetargetGraphBuilder", None)
    if builder is None:
        c.log("FAILED", ABP, "UHawkeyeRetargetGraphBuilder not exposed; build the module")
        return None
    abp = builder.build_retarget_from_parent(ABP, ica.mesh_skeleton(mesh), rtg)
    if abp is None:
        c.log("FAILED", ABP, "did not build or compile; see LogHawkeye")
        return None
    ica.set_tag(abp, stamp)
    c.save(abp)
    c.log("created" if existing is None else "updated", ABP, "retarget from the parent mesh with " + rtg.get_name())
    return abp


def ensure_materials(mesh):
    """Per slot the look overrides: a purple MI of Sparrow's material, else None (the mesh's own)."""
    slots = len(mesh.get_editor_property("materials"))
    out = [None] * slots
    for slot, parent_name, mi_name, vectors in SLOT_TINTS:
        parent = c.load_or_none(SPARROW_ROOT + "/Materials/" + parent_name)
        if parent is None or slot >= slots:
            c.log("skipped", LOOK_PATH + "/" + mi_name, "no " + parent_name)
            continue
        out[slot] = m.ensure_material_instance(LOOK_PATH + "/" + mi_name, parent, vectors=vectors)
    while out and out[-1] is None:
        out.pop()
    return out


def _path_of(asset):
    """Package path of an object, a soft reference (Python reads TSoftObjectPtr as the object, or None while it is
    not loaded) or a path struct."""
    if asset is None:
        return ""
    for getter in ("get_path_name", "export_text"):
        method = getattr(asset, getter, None)
        if method is not None:
            try:
                return str(method())
            except Exception:  # noqa: BLE001
                pass
    return str(asset)


def apply_to_kate(mesh, abp, materials):
    bp = c.load_or_none(KATE)
    cdo = c.blueprint_cdo(bp) if bp is not None else None
    if cdo is None:
        c.log("skipped", KATE + ".BodyLook", "no BP_Kate (run create_blueprints)")
        return False
    look = cdo.get_editor_property("body_look")
    anim_class = abp.generated_class()
    wanted = (_path_of(mesh), _path_of(anim_class), [_path_of(x) for x in materials], list(HIDDEN_BONES))
    have = (
        _path_of(look.get_editor_property("mesh")),
        _path_of(look.get_editor_property("anim_class")),
        [_path_of(x) for x in look.get_editor_property("materials")],
        [str(b) for b in look.get_editor_property("hidden_bones")],
    )
    if have == wanted:
        c.log("exists", KATE + ".BodyLook", "Sparrow, {0} material overrides".format(len(materials)))
        return False
    new_look = unreal.HawkeyeBodyLook()
    new_look.set_editor_property("mesh", mesh)
    new_look.set_editor_property("anim_class", anim_class)
    new_look.set_editor_property("materials", list(materials))
    new_look.set_editor_property("hidden_bones", [unreal.Name(b) for b in HIDDEN_BONES])
    cdo.set_editor_property("body_look", new_look)
    c.compile_blueprint(bp)
    c.save(bp)
    c.log("updated", KATE + ".BodyLook", "Sparrow over the mannequin ({0} material overrides, hidden {1})".format(
        len(materials), ", ".join(HIDDEN_BONES)))
    return True


def run():
    if not sparrow_present():
        c.log("skipped", "Kate's look", "Paragon: Sparrow is not at {0}; BP_Kate keeps the mannequin look".format(SPARROW_MESH))
        return None
    manifest = ica.load_manifest()
    chain_maps = manifest["chain_maps"]
    source = ica.ensure_target("uefn", manifest["targets"]["uefn"], chain_maps)
    if source is None:
        c.log("skipped", "Kate's look", "no mannequin rig (run import_gasp first)")
        return None
    mesh = c.load_or_none(SPARROW_MESH)
    c.ensure_directory(LOOK_PATH)
    rig = ensure_rig(mesh, chain_maps["ue4"])
    rtg = ensure_retargeter(source["rig"], source["mesh"], rig, mesh) if rig is not None else None
    abp = ensure_anim_bp(mesh, rtg) if rtg is not None else None
    if abp is None:
        return None
    materials = ensure_materials(mesh)
    apply_to_kate(mesh, abp, materials)
    return abp


if __name__ == "__main__":
    run()
    c.print_summary("kate look")
