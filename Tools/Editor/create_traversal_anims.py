"""The landing and fall clips for a grapple trip (claude-docs/animation.md, "Traversal body pose"): a create_all.py step.

The Game Animation Sample ships landing clips on Kate's own skeleton (the UEFN mannequin), in
/Game/Characters/UEFN_Mannequin/Animations/Jump: M_Neutral_Jump_F_Land_<Stand, Walk, Run, Sprint>_<Light, Heavy>_<Lfoot,
Rfoot>, M_Neutral_Jump_F_Land_Roll_*, M_Neutral_Jump_F_Land_Stumble_* and M_Neutral_Jump_Loop_Fall. The sample's graph does
not play them for a grapple's fall. This copies each into /Game/Characters/UEFN_Mannequin/Animations/Traversal with root
motion off and the root locked (the capsule moves her, never a clip: the same setting the dodge and hit clips use), and builds
an AM_<Role>_<Foot> montage of it in the folder create_combat_anims.py reads, so the next step puts them in DA_AnimSet_Kate
under the roles LandStandLight .. LandStumble and FallLoop (ECombatAnimRole). No retarget: the clips are already on the skeleton.

    AM_LandRunHeavy_Lfoot, AM_LandRunHeavy_Rfoot     the two feet she lands on alternate (the role's variants)

The code picks the role from impact speed and ground speed (FTraversalPoseRules::ClassifyLanding); everything here is only the
clips. Each copy's pelvis is read at the start, middle and end with the root locked, and a clip that would carry the body off
the capsule (more than 40 cm) or leave it on the floor (under 40 cm high) is a warning: the lock did not take.

Idempotent: a copy and a montage carry a build tag and are redone only when it changes. Without the sample in Content (a
fresh clone): one "skipped" line and the game lands the way it did before.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import import_combat_anims as ica  # noqa: E402

BUILD = "traversal-anims-1"   # bump when the clip list or the montage layout below changes
SOURCE_FOLDER = "/Game/Characters/UEFN_Mannequin/Animations/Jump"
SEQUENCE_FOLDER = "/Game/Characters/UEFN_Mannequin/Animations/Traversal"
MONTAGE_FOLDER = "/Game/Characters/UEFN_Mannequin/Animations/Combat"
CHARACTERS = ["kate"]

# (role, the sample's clip without its foot, layout)
_LAND = {"slot": "DefaultSlot", "blend_in": 0.08, "blend_out": 0.2}
_TAKE = {"slot": "DefaultSlot", "blend_in": 0.08, "blend_out": 0.15}
ROLES = [
    ("LandStandLight", "M_Neutral_Jump_F_Land_Stand_Light", _LAND),
    ("LandStandHeavy", "M_Neutral_Jump_F_Land_Stand_Heavy", _LAND),
    ("LandWalkLight", "M_Neutral_Jump_F_Land_Walk_Light", _LAND),
    ("LandWalkHeavy", "M_Neutral_Jump_F_Land_Walk_Heavy", _LAND),
    ("LandRunLight", "M_Neutral_Jump_F_Land_Run_Light", _LAND),
    ("LandRunHeavy", "M_Neutral_Jump_F_Land_Run_Heavy", _LAND),
    ("LandSprintLight", "M_Neutral_Jump_F_Land_Sprint_Light", _LAND),
    ("LandSprintHeavy", "M_Neutral_Jump_F_Land_Sprint_Heavy", _LAND),
    ("LandRoll", "M_Neutral_Jump_F_Land_Roll", _TAKE),
    ("LandStumble", "M_Neutral_Jump_F_Land_Stumble", _TAKE),
]
FEET = ("Lfoot", "Rfoot")
# The fall after letting go: one clip that loops.
FALL = ("FallLoop", "M_Neutral_Jump_Loop_Fall", {"slot": "DefaultSlot", "blend_in": 0.15, "blend_out": 0.15, "loop": True})

# How far the pelvis may be off the capsule (horizontally) and how low it may be, cm, for a clip that works in place.
MAX_DRIFT = 40.0
MIN_PELVIS = 40.0


def clip_list():
    """[(role, variant, source asset name, layout)] in the order the anim set cycles them (left foot first)."""
    out = []
    for role, base, layout in ROLES:
        for foot in FEET:
            out.append((role, foot, "{0}_{1}".format(base, foot), layout))
    out.append((FALL[0], "Neutral", FALL[1], FALL[2]))
    return out


def pelvis_report(seq):
    """(start, middle, end) pelvis locations with the root locked as the game has it, and a problem text or ''."""
    builder = unreal.HawkeyeCombatMontageBuilder
    length = unreal.AnimationLibrary.get_sequence_length(seq)
    pose = [builder.get_raw_bone_location(seq, "pelvis", t, True) for t in (0.0, length * 0.5, max(length - 0.03, 0.0))]
    problems = []
    for where, p in zip(("start", "middle", "end"), pose):
        drift = (p.x ** 2 + p.y ** 2) ** 0.5
        if drift > MAX_DRIFT:
            problems.append("pelvis {0:.0f} cm off the capsule at the {1}".format(drift, where))
        if p.z < MIN_PELVIS:
            problems.append("pelvis {0:.0f} cm high at the {1}".format(p.z, where))
    return pose, "; ".join(problems)


def ensure_clip(role, variant, source_name, layout):
    source_path = "{0}/{1}".format(SOURCE_FOLDER, source_name)
    source = c.load_or_none(source_path)
    if source is None:
        c.log("skipped", "{0}/AM_{1}_{2}".format(MONTAGE_FOLDER, role, variant), "no " + source_path)
        return None
    seq_path = "{0}/A_{1}_{2}".format(SEQUENCE_FOLDER, role, variant)
    montage_path = "{0}/AM_{1}_{2}".format(MONTAGE_FOLDER, role, variant)
    stamp = ica.fingerprint(BUILD, source_path, layout)

    seq = c.load_or_none(seq_path)
    fresh = False
    if seq is None or ica.tag_of(seq) != stamp:
        ica.delete_if_exists(montage_path)
        ica.delete_if_exists(seq_path)
        c.ensure_directory(SEQUENCE_FOLDER)
        seq = unreal.EditorAssetLibrary.duplicate_asset(source_path, seq_path)
        if seq is None:
            c.log("FAILED", seq_path, "could not copy " + source_path)
            return None
        # In place: the capsule moves her, never a clip (what apply_root_motion does for a clip without root motion).
        unreal.AnimationLibrary.set_root_motion_enabled(seq, False)
        unreal.AnimationLibrary.set_is_root_motion_lock_forced(seq, True)
        ica.set_tag(seq, stamp)
        c.save(seq)
        fresh = True
    montage, built = ica.build_montage(seq, montage_path, layout, False, CHARACTERS, "GASP", fresh_sequence=fresh)
    if montage is None:
        c.log("FAILED", montage_path, built)
        return None
    pose, problem = pelvis_report(seq)
    c.log("created" if fresh or built == "built" else "exists", montage_path,
          "{0}: {1}; pelvis {2}{3}".format(source_name, unreal.HawkeyeCombatMontageBuilder.describe_montage(montage),
                                          " -> ".join("({0:.0f}, {1:.0f}, {2:.0f})".format(p.x, p.y, p.z) for p in pose),
                                          "; WARNING " + problem if problem else ""))
    if problem:
        unreal.log_warning("[Hawkeye] {0}: {1}".format(montage_path, problem))
    return montage


def run():
    made = []
    for role, variant, source_name, layout in clip_list():
        try:
            montage = ensure_clip(role, variant, source_name, layout)
        except Exception as exc:  # noqa: BLE001 - one clip must not stop the rest
            c.log_error("traversal clip {0}_{1}".format(role, variant), exc)
            montage = None
        if montage is not None:
            made.append(montage)
    return made


if __name__ == "__main__":
    run()
    c.print_summary("traversal anims")
