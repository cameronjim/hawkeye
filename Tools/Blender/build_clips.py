"""Author the game's get-up, hit reaction and bow clips by script, on the driver mannequins, and export them as FBX.

    blender --background --factory-startup --python-exit-code 1 --python Tools\\Blender\\build_clips.py -- [--only a,b]
        [--no-render] [--no-export] [--assets-root DIR] [--out DIR] [--renders DIR]

Needs the rigs Tools/Editor/export_clip_rigs.py writes (<assets root>/built/clips/rigs/A_Thug_Idle.fbx and
A_Kate_Idle.fbx: the thugs' UE4 mannequin and Kate's UEFN mannequin, each in its idle). Writes, per rig:

    <out>/<rig>/Skeleton.fbx          the rig at rest with a stand-in body of capsules bound to it: the import defines
                                      the source skeleton and its reference pose from it
    <out>/<rig>/<Clip>.fbx            the clip on the rig's own skeleton (its armature object is the root bone)
    <out>/<rig>/clips.json            per clip: length, the checks' measures
    <renders>/<rig>_<Clip>.png        side-on strip of the clip at fixed times (red bars along the top: tenths of s)

The weld clips (2026-11-23): six hit reactions per rig by zone and weight (HitHead, HitGut, HitLegs; Light, Heavy) that
start on their damaged pose, and Kate's jab, cross, hook and front kick (uefn), each built around the frame its hit window
opens (a hold at full extension) at rate 1.0 with an early step-in. Their extras carry hit_peak_s, knockback_cm, contact_cm,
contact_height_cm and wide into clips.json, and create_combat_anims.py bakes them into the anim sets.

Every clip is keyed poses solved per frame (clipkit.py): the idle is the base, the hips carry the weight, limbs are IK
to keyed targets with poles, hands and feet take keyed turns, and both ends meet the game's poses. The run fails on a
foot sliding while planted, a joint turning faster than a body can, a hand inside the torso or a joint under the floor.
claude-docs/animation.md, "Authored clips", says what plays where.
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clipkit as ck  # noqa: E402
from clipkit import Track as T  # noqa: E402

DEFAULT_ASSETS_ROOT = r"C:\Users\camer\code\hawkeye-assets"
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RIG_FILES = {"ue4": "A_Thug_Idle.fbx", "uefn": "A_Kate_Idle.fbx"}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets-root", default="")
    parser.add_argument("--out", default="")
    parser.add_argument("--renders", default="")
    parser.add_argument("--only", default="")
    parser.add_argument("--no-render", action="store_true")
    parser.add_argument("--no-export", action="store_true")
    args = parser.parse_args(argv)
    root = args.assets_root or os.environ.get("HAWKEYE_ASSETS_ROOT", "")
    if not root:
        local = os.path.join(REPO, "Tools", "local.json")
        if os.path.isfile(local):
            with open(local, encoding="utf-8") as f:
                root = json.load(f).get("assetsRoot", "")
    args.assets_root = root or DEFAULT_ASSETS_ROOT
    args.out = args.out or os.path.join(args.assets_root, "built", "clips")
    args.renders = args.renders or os.path.join(REPO, "Saved", "Screenshots", "Blender", "clips")
    args.only = set(n.strip() for n in args.only.split(",") if n.strip())
    return args


def V(*xyz):
    return tuple(float(v) for v in xyz)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


# --------------------------------------------------------------------------------------------------
# get-ups (the thugs: UE4 mannequin)
# --------------------------------------------------------------------------------------------------


def getup_back(rig):
    """Lying on his back, feet toward where he will face: chin tucks, he sits up with a hand pushing behind,
    draws his feet in to where he will stand, rocks his hips forward over them into a crouch and rises, a little
    tall and back, then settles into the idle. Feet planted from the moment they are drawn in."""
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    hl = tuple(rig.idle_hand("l"))
    hr = tuple(rig.idle_hand("r"))
    a = rig.ankle_height()
    lie_y = 60.0
    tracks = {
        "pelvis_pos": T([(0.00, V(0, lie_y, 11)), (0.15, V(0, lie_y + 1, 11)), (0.42, V(0, lie_y - 2, 13)),
                         (0.58, V(0, lie_y - 6, 15)), (0.74, V(0, 38, 27)), (0.90, V(0, 22, 47)),
                         (1.10, V(P[0], 14, 74)), (1.28, add(P, V(0, -1.5, 0.6))), (1.42, add(P, V(0, 0.5, -1.0))),
                         (1.60, P)]),
        "pelvis_rot": T([(0.00, V(-86, 0, 4)), (0.15, V(-84, 0, 4)), (0.42, V(-62, 0, 2)), (0.58, V(-42, 0, 0)),
                         (0.74, V(-18, 0, 0)), (0.90, V(8, 0, 0)), (1.10, V(6, 0, 0)), (1.28, V(-3, 0, 0)),
                         (1.42, V(1, 0, 0)), (1.60, V(0, 0, 0))]),
        # The chest curls up off the floor (relative to the hips), leads forward over the feet, then stacks.
        "chest": T([(0.00, V(2, 0, 0)), (0.15, V(14, 0, 0)), (0.42, V(52, 4, 0)), (0.58, V(62, 6, 0)),
                    (0.74, V(54, 4, 0)), (0.90, V(30, 0, 0)), (1.10, V(10, 0, 0)), (1.28, V(-5, 0, 0)),
                    (1.42, V(1.5, 0, 0)), (1.60, V(0, 0, 0))]),
        "neck": T([(0.00, V(8, 0, 0)), (0.15, V(30, 0, 0)), (0.42, V(22, 0, 0)), (0.58, V(0, 0, 0)),
                   (0.74, V(-18, 0, 0)), (0.90, V(-22, 0, 0)), (1.10, V(-8, 0, 0)), (1.28, V(3, 0, 0)),
                   (1.60, V(0, 0, 0))]),
        # Legs: out straight, heels drawn in along the floor, then the feet planted where he will stand.
        "foot_l": T([(0.00, V(15, lie_y - 78, a - 1)), (0.12, V(15, lie_y - 78, a - 1)), (0.32, V(17, 18, a + 3)),
                     (0.48, fl), (1.60, fl)]),
        "foot_r": T([(0.00, V(-13, lie_y - 77, a - 1)), (0.10, V(-13, lie_y - 77, a - 1)), (0.30, V(-14, 34, a + 4)),
                     (0.46, fr), (1.60, fr)]),
        "foot_l_rot": T([(0.00, V(-72, 18, 0)), (0.12, V(-72, 18, 0)), (0.36, V(-12, 6, 0)), (0.48, V(0, 0, 0)),
                         (1.60, V(0, 0, 0))]),
        "foot_r_rot": T([(0.00, V(-70, -16, 0)), (0.10, V(-70, -16, 0)), (0.34, V(-10, -4, 0)), (0.46, V(0, 0, 0)),
                         (1.60, V(0, 0, 0))]),
        # Hands: on the floor by his hips, the right plants behind and pushes, the left reaches over the knees;
        # both come up into his guard in arcs.
        "hand_l": T([(0.00, V(30, lie_y + 6, 4)), (0.15, V(31, lie_y + 4, 5)), (0.42, V(28, 22, 42)),
                     (0.58, V(24, 4, 50)), (0.74, V(24, 0, 58)), (0.90, V(30, -6, 78)), (1.10, add(hl, V(-2, -6, 8))),
                     (1.28, add(hl, V(0, -2, 2))), (1.60, hl)]),
        "hand_r": T([(0.00, V(-30, lie_y + 6, 4)), (0.15, V(-31, lie_y + 12, 4)), (0.30, V(-32, lie_y + 22, 3)),
                     (0.66, V(-32, lie_y + 22, 3)), (0.80, V(-40, 44, 30)), (0.95, V(-38, 18, 70)),
                     (1.12, add(hr, V(0, -6, 8))), (1.28, add(hr, V(0, -2, 2))), (1.60, hr)]),
        "hand_r_rot": T([(0.00, V(0, 0, 0)), (0.30, V(60, 0, -20)), (0.66, V(60, 0, -20)), (0.85, V(0, 0, 0)),
                         (1.60, V(0, 0, 0))]),
        "pole_hand_r": T([(0.0, V(-0.6, 0.2, 0.8)), (0.66, V(-0.6, 0.8, 0.2)), (0.95, V(-0.6, 0.5, -0.8)),
                          (1.6, V(-0.6, 0.4, -0.9))]),
        "fingers_l": T([(0.0, 0.6), (0.9, 1.0), (1.6, 1.0)]),
        "fingers_r": T([(0.0, 0.6), (0.3, 0.2), (0.66, 0.2), (0.95, 1.0), (1.6, 1.0)]),
    }
    return ck.Clip("GetUp_Back", 1.6, tracks, fade_out=0.18,
                   planted=[("l", 0.48, 1.60, "flat"), ("r", 0.46, 1.60, "flat"), ],
                   strip_times=[0.0, 0.3, 0.55, 0.8, 1.0, 1.2, 1.4, 1.6], torso_clear=False,
                   extra={"lying": "back", "lie_pelvis": [0.0, lie_y, 11.0]})


def getup_front(rig):
    """Face down, head toward where he will face: hands slide under the shoulders, he pushes his chest up, rocks
    back onto his knees, steps the left foot through, rises off it with the right toes pushing, then steps the right
    foot up into the idle stance. Hands, knee and feet stay put while they bear weight."""
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    hl = tuple(rig.idle_hand("l"))
    hr = tuple(rig.idle_hand("r"))
    a = rig.ankle_height()
    lie_y = 14.0
    knee_ball_r = V(-13, 90, 2.5)
    kneel_ankle = tuple(rig.foot_from_ball("r", knee_ball_r, (60, 0, 0)))
    tracks = {
        "pelvis_pos": T([(0.00, V(0, lie_y, 12)), (0.18, V(0, lie_y, 12)), (0.45, V(0, lie_y + 2, 15)),
                         (0.72, V(0, 34, 46)), (0.86, V(0, 32, 58)), (0.98, V(0, 26, 58)), (1.12, V(2, 21, 62)), (1.30, V(1, 14, 76)),
                         (1.52, add(P, V(0, -1.0, 0.5))), (1.66, add(P, V(0, 0.5, -0.8))), (1.80, P)]),
        "pelvis_rot": T([(0.00, V(86, 0, 0)), (0.18, V(86, 0, 0)), (0.45, V(80, 0, 0)), (0.72, V(62, 0, 0)),
                         (0.92, V(42, 0, 0)), (1.08, V(28, 4, 0)), (1.30, V(12, 2, 0)), (1.52, V(-2, 0, 0)),
                         (1.66, V(1, 0, 0)), (1.80, V(0, 0, 0))]),
        "chest": T([(0.00, V(-4, 0, 0)), (0.18, V(-8, 0, 0)), (0.45, V(-42, 0, 0)), (0.72, V(-12, 0, 0)),
                    (0.92, V(4, 0, 0)), (1.08, V(14, 0, 0)), (1.30, V(10, 0, 0)), (1.52, V(-4, 0, 0)),
                    (1.66, V(1, 0, 0)), (1.80, V(0, 0, 0))]),
        "neck": T([(0.00, V(-10, 32, 0)), (0.18, V(-25, 10, 0)), (0.45, V(-18, 0, 0)), (0.72, V(-30, 0, 0)),
                   (0.92, V(-30, 0, 0)), (1.08, V(-20, 0, 0)), (1.30, V(-6, 0, 0)), (1.52, V(3, 0, 0)),
                   (1.80, V(0, 0, 0))]),
        # Hands: under the shoulders, palms down, while he pushes up and rocks back; then up into the guard.
        "hand_l": T([(0.00, V(30, -36, 5)), (0.18, V(24, -42, 3)), (0.86, V(24, -42, 3)), (1.06, V(28, -26, 40)),
                     (1.30, add(hl, V(-2, -8, 10))), (1.52, add(hl, V(0, -2, 2))), (1.80, hl)]),
        "hand_r": T([(0.00, V(-30, -36, 5)), (0.18, V(-24, -42, 3)), (0.92, V(-24, -42, 3)), (1.08, V(-30, -22, 42)),
                     (1.34, add(hr, V(0, -8, 10))), (1.52, add(hr, V(0, -2, 2))), (1.80, hr)]),
        "hand_l_rot": T([(0.0, V(0, 70, 0)), (0.18, V(0, 90, 0)), (0.86, V(0, 90, 0)), (1.20, V(0, 0, 0)),
                         (1.80, V(0, 0, 0))]),
        "hand_r_rot": T([(0.0, V(0, -70, 0)), (0.18, V(0, -90, 0)), (0.92, V(0, -90, 0)), (1.25, V(0, 0, 0)),
                         (1.80, V(0, 0, 0))]),
        "fingers_l": T([(0.0, 0.5), (0.18, 0.15), (0.86, 0.15), (1.2, 1.0), (1.8, 1.0)]),
        "fingers_r": T([(0.0, 0.5), (0.18, 0.15), (0.92, 0.15), (1.25, 1.0), (1.8, 1.0)]),
        "pole_hand_l": V(0.4, 0.6, 0.2),
        "pole_hand_r": V(-0.4, 0.6, 0.2),
        # Left leg: the knee comes under the hips on the floor, then the foot steps through to where he will stand.
        "foot_l": T([(0.00, V(14, lie_y + 86, a + 3)), (0.40, V(14, lie_y + 86, a + 3)), (0.54, V(14, 90, 26)), (0.63, V(14, 89, 30)),
                     (0.72, V(14, 90, 14)), (0.82, V(15, 70, 20)), (0.94, V(17, 30, 22)), (1.08, add(fl, V(0, -2, 6))),
                     (1.15, fl),
                     (1.80, fl)]),
        "foot_l_rot": T([(0.00, V(105, 10, 0)), (0.45, V(105, 10, 0)), (0.72, V(105, 0, 0)), (0.90, V(30, 0, 0)),
                         (1.07, V(-10, 0, 0)), (1.15, V(0, 0, 0)), (1.80, V(0, 0, 0))]),
        "pole_foot_l": T([(0.0, V(0.1, 0.6, -1.0)), (0.72, V(0.1, 0.3, -1.0)), (0.84, V(0.15, -1.0, -0.3)),
                          (1.00, V(0.15, -1.0, 0.2)), (1.12, V(0.1, -1.0, 0.0)), (1.8, V(0.1, -1.0, 0.0))]),
        # Right leg: onto the knee with the toes tucked (the ball stays put), heel up as he rises, then the step.
        "foot_r": T([(0.00, V(-13, lie_y + 86, a + 3)), (0.40, V(-13, lie_y + 86, a + 3)), (0.54, V(-13, 90, 26)), (0.62, V(-13, 89, 28)),
                     (1.14, kneel_ankle), (1.30, add(kneel_ankle, V(0, -14, 8))), (1.40, V(-14, 46, 22)),
                     (1.52, add(fr, V(0, 0, 2))), (1.58, fr), (1.80, fr)]),
        "ball_r": knee_ball_r,
        "pivot_r": T([(0.0, 0.0), (0.66, 0.0), (0.72, 1.0), (1.14, 1.0), (1.20, 0.0), (1.8, 0.0)]),
        "foot_r_rot": T([(0.00, V(105, -10, 0)), (0.45, V(105, -10, 0)), (0.72, V(78, 0, 0)), (1.14, V(60, 0, 0)),
                         (1.30, V(30, 0, 0)), (1.42, V(-8, 0, 0)), (1.52, V(-4, 0, 0)), (1.58, V(0, 0, 0)),
                         (1.80, V(0, 0, 0))]),
        "toe_r": T([(0.0, 0.0), (0.58, 0.0), (0.80, -50.0), (1.14, -40.0), (1.36, 0.0), (1.8, 0.0)]),
        "pole_foot_r": T([(0.0, V(-0.1, 0.6, -1.0)), (0.72, V(-0.1, 0.6, -1.0)), (1.14, V(-0.1, -0.6, -1.0)),
                          (1.30, V(-0.1, -1.0, -0.2)), (1.8, V(-0.1, -1.0, 0.0))]),
    }
    return ck.Clip("GetUp_Front", 1.8, tracks, fade_out=0.16,
                   planted=[("l", 1.17, 1.80, "flat"), ("r", 0.74, 1.14, "ball"), ("r", 1.58, 1.80, "flat")],
                   strip_times=[0.0, 0.3, 0.55, 0.8, 1.0, 1.2, 1.4, 1.8], torso_clear=False,
                   extra={"lying": "front", "lie_pelvis": [0.0, lie_y, 12.0]})


# --------------------------------------------------------------------------------------------------
# hit reactions (both rigs)
# --------------------------------------------------------------------------------------------------

# The game shoves whoever is hit (250 cm/s on the capsule: about 10 cm; a heavy blow 500 cm/s, about 28 cm). A clip is
# in place, so each starts with the whole body that far toward the blow and steps back onto the capsule: in the world
# the feet stay where they were while the capsule slides under them.
LIGHT_SHOVE = 10.0
HEAVY_SHOVE = 28.0


def _step(start, end, t0, t1, lift):
    """Keys for one step: up off the heel, over in an arc, heel down a touch early, settled."""
    mid = tuple(s + (e - s) * 0.55 for s, e in zip(start, end))
    return [(t0, start), (t0 + (t1 - t0) * 0.45, add(mid, V(0, 0, lift))), (t1, end)]


def _step_rot(t0, t1, pitch_up=-14.0):
    """The foot's turn over a step: toes up as it swings, flat as it lands."""
    return [(t0, V(0, 0, 0)), (t0 + (t1 - t0) * 0.4, V(pitch_up * 0.4, 0, 0)), (t0 + (t1 - t0) * 0.8, V(pitch_up, 0, 0)),
            (t1, V(0, 0, 0))]


def hit_light(rig, side):
    """A light blow from the front, his left or his right: the head and chest snap away from it, the hips follow, the
    far foot steps out to catch him and the near foot follows; he comes back past upright and settles."""
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    # Which way the blow pushes him (armature axes: forward is -Y, his left +X).
    push = {"Front": V(0, 1, 0), "Left": V(-1, 0, 0), "Right": V(1, 0, 0)}[side]
    s = LIGHT_SHOVE
    off = tuple(-v * s for v in push)       # the start: everything toward the blow by the shove

    def at(base, k):
        return add(base, tuple(o * k for o in off))

    # Away from the blow: back for the front, a roll and turn away for a side.
    sign = {"Front": 0.0, "Left": -1.0, "Right": 1.0}[side]
    snap_chest = V(-11 if side == "Front" else -4, 10 * sign, 9 * sign)
    snap_neck = V(-24 if side == "Front" else -6, 22 * sign, 14 * sign)
    # The stepping foot: the rear one for a blow from the front, the far one for a side blow.
    first, second = {"Front": ("r", "l"), "Left": ("r", "l"), "Right": ("l", "r")}[side]
    feet = {"l": fl, "r": fr}
    t = 0.55
    tracks = {
        "pelvis_pos": T([(0.00, at(P, 1)), (0.10, add(at(P, 0.45), V(0, 0, -2.0))), (0.22, add(at(P, 0.1), V(0, 0, -2.5))),
                         (0.34, add(at(P, -0.15), V(0, 0, -1.0))), (0.44, add(P, V(0, 0, -0.3))), (t, P)]),
        "pelvis_rot": T([(0.00, V(0, 0, 0)), (0.10, V(-3 if side == "Front" else 0, 4 * sign, 3 * sign)),
                         (0.30, V(1, 1 * sign, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.00, V(0, 0, 0)), (0.06, tuple(v * 0.8 for v in snap_chest)), (0.11, snap_chest),
                    (0.24, tuple(v * 0.35 for v in snap_chest)), (0.34, tuple(-v * 0.25 for v in snap_chest)),
                    (0.44, tuple(v * 0.06 for v in snap_chest)), (t, V(0, 0, 0))]),
        "neck": T([(0.00, V(0, 0, 0)), (0.075, snap_neck), (0.12, tuple(v * 0.9 for v in snap_neck)),
                   (0.22, tuple(v * 0.3 for v in snap_neck)), (0.32, tuple(-v * 0.22 for v in snap_neck)),
                   (0.43, tuple(v * 0.05 for v in snap_neck)), (t, V(0, 0, 0))]),
        "foot_" + first: T(_step(at(feet[first], 1), feet[first], 0.05, 0.24, 6.0) + [(t, feet[first])]),
        "foot_" + first + "_rot": T(_step_rot(0.05, 0.24) + [(t, V(0, 0, 0))]),
        "foot_" + second: T([(0.0, at(feet[second], 1)), (0.24, at(feet[second], 1))]
                            + _step(at(feet[second], 1), feet[second], 0.24, 0.40, 3.0)[1:] + [(t, feet[second])]),
        "foot_" + second + "_rot": T([(0.0, V(0, 0, 0))] + _step_rot(0.24, 0.40, -8.0) + [(t, V(0, 0, 0))]),
    }
    return ck.Clip("Hit" + side + "_Light", t, tracks, fade_in=0.0, fade_out=0.12,
                   planted=[(second, 0.0, 0.24, "flat"), (first, 0.24, t, "flat"), (second, 0.40, t, "flat")],
                   strip_times=[0.0, 0.05, 0.1, 0.17, 0.25, 0.34, 0.44, t],
                   extra={"start_offset": list(off), "span": (-110.0, 110.0)})


def hit_front(rig):
    return hit_light(rig, "Front")


def hit_left(rig):
    return hit_light(rig, "Left")


def hit_right(rig):
    return hit_light(rig, "Right")


def hit_heavy(rig):
    """A heavy blow from the front: head and chest thrown back, arms flung up, knees buckle; two steps back, rear foot
    then front, the hips behind the feet until the second lands; he comes forward past upright and settles."""
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    hl = tuple(rig.idle_hand("l"))
    hr = tuple(rig.idle_hand("r"))
    s = HEAVY_SHOVE
    t = 0.75

    def fwd(base, k):
        return add(base, V(0, -s * k, 0))

    tracks = {
        "pelvis_pos": T([(0.00, fwd(P, 1)), (0.10, add(fwd(P, 0.7), V(0, 0, -2))), (0.22, add(fwd(P, 0.55), V(0, 0, -4))),
                         (0.36, add(fwd(P, 0.25), V(0, 0, -4))), (0.50, add(fwd(P, -0.03), V(0, 0, -2.5))),
                         (0.62, add(P, V(0, 0, -0.5))), (t, P)]),
        "pelvis_rot": T([(0.00, V(0, 0, 0)), (0.12, V(-8, 3, 2)), (0.30, V(-4, 5, 0)), (0.48, V(3, 1, 0)),
                         (0.62, V(-0.5, 0, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.00, V(0, 0, 0)), (0.07, V(-16, 3, 0)), (0.13, V(-24, 6, 2)), (0.28, V(-14, 8, 0)),
                    (0.44, V(9, 2, 0)), (0.56, V(-2.5, 0, 0)), (0.66, V(0.6, 0, 0)), (t, V(0, 0, 0))]),
        "neck": T([(0.00, V(0, 0, 0)), (0.09, V(-21, 5, 0)), (0.15, V(-17, 8, 0)), (0.28, V(-8, 4, 0)),
                   (0.42, V(10, 0, 0)), (0.55, V(-3, 0, 0)), (0.66, V(0.6, 0, 0)), (t, V(0, 0, 0))]),
        # Shoulders flung up, back down as he catches himself (the arms ride the chest).
        "clav_l": T([(0.0, V(0, 0, 0)), (0.10, V(0, 0, -14)), (0.30, V(0, 0, -6)), (0.50, V(0, 0, 1)), (t, V(0, 0, 0))]),
        "clav_r": T([(0.0, V(0, 0, 0)), (0.10, V(0, 0, 14)), (0.30, V(0, 0, 6)), (0.50, V(0, 0, -1)), (t, V(0, 0, 0))]),
        "foot_r": T(_step(fwd(fr, 1), fr, 0.05, 0.27, 7.0) + [(t, fr)]),
        "foot_r_rot": T(_step_rot(0.05, 0.27, -16.0) + [(t, V(0, 0, 0))]),
        "foot_l": T([(0.0, fwd(fl, 1)), (0.20, fwd(fl, 1))] + _step(fwd(fl, 1), fl, 0.20, 0.44, 6.0)[1:] + [(t, fl)]),
        "foot_l_rot": T([(0.0, V(0, 0, 0))] + _step_rot(0.20, 0.44, -14.0) + [(t, V(0, 0, 0))]),
    }
    return ck.Clip("HitHeavy_Stagger", t, tracks, fade_out=0.12,
                   planted=[("l", 0.0, 0.20, "flat"), ("r", 0.27, t, "flat"), ("l", 0.44, t, "flat")],
                   strip_times=[0.0, 0.06, 0.13, 0.22, 0.32, 0.44, 0.58, t],
                   extra={"start_offset": [0.0, -s, 0.0], "span": (-110.0, 110.0)})


HITS = [hit_front, hit_left, hit_right, hit_heavy]


# --------------------------------------------------------------------------------------------------
# the bow (Kate: UEFN mannequin, upper body)
# --------------------------------------------------------------------------------------------------

# The game's bow rides in the clip's left hand and its aim offset is laid out on the drawn pose Paragon's Sparrow holds
# (claude-docs/animation.md, "The bow clips"), so the hands' places and turns at the anchor and at the nock are taken from
# those poses (exported by Tools/Editor/export_clip_rigs.py); the motion between them, its timing, arcs, lead and settle
# are keyed here.
# Sparrow's draw is the trimmed 0.43-0.93 s of her whole shot (manifest): the nock is its 0.43 s.
BOW_REFS = {"anchor": ("A_Kate_BowAim.fbx", 0.0), "nock": ("A_Kate_BowDraw.fbx", 0.43)}


def _bow_refs(rig):
    if getattr(rig, "bow_refs", None) is None:
        rigs = os.path.join(rig.assets_root, "built", "clips", "rigs")
        rig.bow_refs = {}
        for key, (name, at) in BOW_REFS.items():
            rig.bow_refs[key] = rig.load_pose(os.path.join(rigs, name), at)
        rig.take_fingers(rig.bow_refs["anchor"], "l")
        rig.take_fingers(rig.bow_refs["anchor"], "r")
    return rig.bow_refs


def _ref_bits(rig, pose):
    pelvis, chest, neck = rig.body_angles(pose)
    bits = {"pelvis_rot": pelvis, "chest": chest, "neck": neck}
    for side in ("l", "r"):
        bits["hand_" + side] = tuple(pose["hand_" + side].translation)
        bits["hand_" + side + "_q"] = rig.quat_of(pose, "hand_" + side)
        upper = pose["upperarm_" + side].translation
        mid = (upper + pose["hand_" + side].translation) * 0.5
        bits["pole_hand_" + side] = tuple((pose["lowerarm_" + side].translation - mid).normalized())
    return bits


def _same_hemisphere(q, ref):
    return tuple(-v for v in q) if sum(a * b for a, b in zip(q, ref)) < 0 else q


def _lerp(a, b, f):
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def _scale(v, f):
    return tuple(x * f for x in v)


def _unit(v):
    n = sum(x * x for x in v) ** 0.5
    return tuple(x / n for x in v)


def _bow_keys(rig, keys):
    """Tracks from keyed body states: keys is [(t, {param: value})], every key naming every param."""
    names = keys[0][1].keys()
    tracks = {}
    for name in names:
        values = [(t, k[name]) for t, k in keys]
        if name.endswith("_q"):
            fixed = [values[0]]
            for t, q in values[1:]:
                fixed.append((t, _same_hemisphere(q, fixed[-1][1])))
            values = fixed
        tracks[name] = T(values)
    return tracks


def bow_draw(rig):
    """From the nock (the arrow on the string, the bow up) to the anchor in 0.36 s: the string hand eases forward onto
    the string, then pulls back along an arc that lifts and swings out past the shoulder to the jaw, the chest turning
    side-on ahead of it; the hand overshoots the anchor by a little and settles, the bow arm pushing out against it."""
    refs = _bow_refs(rig)
    n = _ref_bits(rig, refs["nock"])
    a = _ref_bits(rig, refs["anchor"])
    pull = _unit(tuple(x - y for x, y in zip(a["hand_r"], n["hand_r"])))
    target = _unit(tuple(x - y for x, y in zip(a["hand_l"], a["hand_r"])))

    def state(base, **over):
        out = dict(base)
        out.update(over)
        return out

    keys = [
        (0.00, n),
        (0.05, state(n, hand_r=add(n["hand_r"], _scale(pull, -1.5)), chest=add(n["chest"], V(0, 1.0, 0)))),
        (0.16, state(_mix(n, a, 0.45), hand_r=add(_lerp(n["hand_r"], a["hand_r"], 0.5), V(0, 0, 3.5)))),
        (0.27, state(a, hand_r=add(a["hand_r"], _scale(pull, 2.5)), hand_l=add(a["hand_l"], _scale(target, 1.2)),
                     chest=add(a["chest"], V(0, -2.5, 0)), neck=add(a["neck"], V(0, 1.5, 0)))),
        (0.36, a),
    ]
    tracks = _bow_keys(rig, keys)
    tracks["fingers_r"] = T([(0.0, 1.0), (0.36, 1.0)])
    # A real draw is fast: the string elbow swings about 50 cm from in front of the chest to behind the shoulder in
    # about 0.2 s, so its upper arm turns up to 40 degrees a frame at the peak (smoothly: no frame jumps on its own).
    return ck.Clip("BowDraw", 0.36, tracks, strip_times=[0.0, 0.05, 0.12, 0.19, 0.27, 0.36], torso_clear=False,
                   extra={"span": (-110.0, 110.0), "pop_degrees": 45.0})


def _mix(a, b, f):
    out = {}
    for k in a:
        if k.endswith("_q"):
            q = _same_hemisphere(b[k], a[k])
            out[k] = _lerp(a[k], q, f)
        else:
            out[k] = _lerp(a[k], b[k], f)
    return out


def bow_aim(rig):
    """The held draw, a 2 s loop: the chest rises and falls with a breath, the string hand and the bow hand drift a few
    millimetres with it; it ends exactly where it starts."""
    a = _ref_bits(rig, _bow_refs(rig)["anchor"])

    def breath(f, hand):
        return dict(a, chest=add(a["chest"], V(-0.7 * f, 0.2 * f, 0)), hand_r=add(a["hand_r"], V(0, 0, 0.4 * f)),
                    hand_l=add(a["hand_l"], V(0, 0, 0.25 * hand)))

    keys = [(0.0, a), (0.5, breath(1.0, 0.6)), (1.0, breath(0.2, 1.0)), (1.5, breath(-0.6, 0.2)), (2.0, a)]
    return ck.Clip("BowAimIdle", 2.0, _bow_keys(rig, keys), strip_times=[0.0, 0.5, 1.0, 1.5, 2.0], torso_clear=False,
                   extra={"span": (-110.0, 110.0)})


def bow_fire(rig):
    """The release: the string hand springs back past the ear and out, fingers open, the bow arm pushes on toward the
    target and dips as the bow kicks; held a beat, then the string hand swings down and forward in an arc to the nock,
    where the next draw starts."""
    refs = _bow_refs(rig)
    n = _ref_bits(rig, refs["nock"])
    a = _ref_bits(rig, refs["anchor"])
    back = _unit(tuple(x - y for x, y in zip(a["hand_r"], a["hand_l"])))
    chest = tuple(refs["anchor"][rig.chest_bone].translation)
    side = _unit(tuple(x - y for x, y in zip(a["hand_r"], chest)))
    out = _unit((side[0] - back[0] * sum(s * b for s, b in zip(side, back)),
                 side[1] - back[1] * sum(s * b for s, b in zip(side, back)), 0.0))
    fwd = _scale(back, -1.0)

    def state(base, **over):
        res = dict(base)
        res.update(over)
        return res

    keys = [
        (0.00, a),
        (0.05, state(a, hand_r=add(add(add(a["hand_r"], _scale(back, 13)), _scale(out, 4)), V(0, 0, 2)),
                     hand_l=add(add(a["hand_l"], _scale(fwd, 2.5)), V(0, 0, -1)))),
        (0.16, state(a, hand_r=add(add(add(a["hand_r"], _scale(back, 16)), _scale(out, 7)), V(0, 0, -1)),
                     hand_l=add(add(a["hand_l"], _scale(fwd, 1.0)), V(0, 0, -3)), chest=add(a["chest"], V(0, -2.0, 0)))),
        (0.32, state(_mix(a, n, 0.5), hand_r=add(add(_lerp(a["hand_r"], n["hand_r"], 0.5), _scale(out, 6)), V(0, 0, -8)))),
        (0.50, n),
    ]
    tracks = _bow_keys(rig, keys)
    tracks["fingers_r"] = T([(0.0, 1.0), (0.03, 0.25), (0.20, 0.3), (0.42, 1.0), (0.5, 1.0)])
    return ck.Clip("BowFire", 0.5, tracks, strip_times=[0.0, 0.04, 0.1, 0.16, 0.25, 0.35, 0.5], torso_clear=False,
                   extra={"span": (-110.0, 110.0), "pop_degrees": 45.0})


BOW = [bow_draw, bow_aim, bow_fire]


# --------------------------------------------------------------------------------------------------
# the stealth takedown: a pair (the thug on the UE4 mannequin, Kate on the UEFN one)
# --------------------------------------------------------------------------------------------------

# Both clips are authored in one frame: Kate's capsule at the origin facing -Y, the victim's TAKEDOWN_GAP cm ahead of her
# facing the same way (the game puts them there, gameplay-semantics.md, "Takedown"). Kate's driver is drawn at 0.93 scale,
# so a place on him is divided by KATE_SCALE to be the same place in her rig. The victim is solved first (ue4 before
# uefn) and Kate's hands follow his shoulders frame by frame.
TAKEDOWN_GAP = 55.0
KATE_SCALE = 0.93
TAKEDOWN_SECONDS = 1.75
# How far Kate's root travels back as she drags him down (her rig's cm: the game moves her capsule by it).
TAKEDOWN_RETREAT = 95.0
SOLVED = {}


def takedown_victim(rig):
    """Taken from behind: a jolt as her arm closes across his chest, his hands fly up to it, his head goes back; pulled
    back off balance his heels go out from under him, she drags him back and down onto his back and he goes limp. The
    last frame holds (the game hands it to the ragdoll from there)."""
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    hl = tuple(rig.idle_hand("l"))
    hr = tuple(rig.idle_hand("r"))
    a = rig.ankle_height()
    t = TAKEDOWN_SECONDS
    body = {
        "pelvis_pos": T([(0.0, P), (0.12, add(P, V(0, 3, -1))), (0.45, add(P, V(0, 14, -8))), (0.80, V(0, 26, 52)),
                         (1.10, V(0, 32, 26)), (1.40, V(0, 32, 12)), (t, V(0, 32, 11))]),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.12, V(-6, 0, 0)), (0.45, V(-14, 0, 0)), (0.80, V(-30, 0, 0)),
                         (1.10, V(-56, 0, 2)), (1.40, V(-84, 0, 4)), (t, V(-86, 0, 4))]),
        "chest": T([(0.0, V(0, 0, 0)), (0.12, V(-14, 0, 0)), (0.45, V(-18, 4, 0)), (0.80, V(-12, 2, 0)), (1.10, V(-6, 0, 0)),
                    (1.40, V(0, 0, 0)), (t, V(1, 0, 0))]),
        "neck": T([(0.0, V(0, 0, 0)), (0.10, V(-22, 0, 0)), (0.45, V(-25, 4, 0)), (0.80, V(-15, 4, 0)), (1.10, V(-4, 6, 0)),
                   (1.40, V(10, 18, 4)), (t, V(10, 20, 4))]),
        # Planted while he fights it, then the heels go out from under him and the legs end out straight.
        "foot_l": T([(0.0, fl), (0.50, fl), (0.80, V(18, -10, a + 1)), (1.10, V(17, -26, a + 1)), (1.40, V(15, -44, a - 1)),
                     (t, V(15, -45, a - 1))]),
        "foot_r": T([(0.0, fr), (0.55, fr), (0.85, V(-14, 4, a + 1)), (1.10, V(-14, -22, a + 1)), (1.40, V(-13, -42, a - 1)),
                     (t, V(-13, -43, a - 1))]),
        "foot_l_rot": T([(0.0, V(0, 0, 0)), (0.50, V(0, 0, 0)), (0.80, V(-8, 4, 0)), (1.10, V(-35, 10, 0)),
                         (1.40, V(-70, 18, 0)), (t, V(-72, 18, 0))]),
        "foot_r_rot": T([(0.0, V(0, 0, 0)), (0.55, V(0, 0, 0)), (0.85, V(-8, -4, 0)), (1.10, V(-35, -10, 0)),
                         (1.40, V(-68, -16, 0)), (t, V(-70, -16, 0))]),
    }
    # His hands go to where her arm is (his own chest, wherever it has gone): solve the body first, then key the hands on it.
    probe = ck.Clip("probe", t, body)
    hand_l = [(0.0, hl)]
    hand_r = [(0.0, hr)]
    for k in (0.27, 0.37, 0.48, 0.62, 0.80, 0.95):
        pose = rig.solve(probe.params(k))
        cl, cr = pose["clavicle_l"].translation, pose["clavicle_r"].translation
        # Gripping her forearm across his upper chest: just in front of and below each collarbone, pulling at it.
        hand_l.append((k, add(tuple(cl), V(-2, -14, -10))))
        hand_r.append((k, add(tuple(cr), V(2, -14, -6))))
    hand_l += [(1.20, V(30, 22, 30)), (1.45, V(31, 40, 4)), (t, V(31, 41, 4))]
    hand_r += [(1.22, V(-30, 24, 28)), (1.45, V(-30, 38, 4)), (t, V(-30, 39, 4))]
    body["hand_l"] = T(hand_l)
    body["hand_r"] = T(hand_r)
    body["fingers_l"] = T([(0.0, 1.0), (0.15, 1.8), (0.95, 1.6), (1.3, 0.6), (t, 0.6)])
    body["fingers_r"] = T([(0.0, 1.0), (0.15, 1.8), (0.95, 1.6), (1.3, 0.6), (t, 0.6)])
    return ck.Clip("Takedown_Victim", t, body, planted=[("l", 0.0, 0.50, "flat"), ("r", 0.0, 0.55, "flat")],
                   strip_times=[0.0, 0.15, 0.4, 0.65, 0.9, 1.15, 1.4, t], torso_clear=False,
                   extra={"pair": "victim", "gap": TAKEDOWN_GAP, "span": (-130.0, 130.0), "pop_degrees": 30.0})


def takedown_attacker(rig):
    """Kate behind him: steps in on her left foot as her right arm closes across his chest and her left hand takes his
    side, sits her hips down and turns to pull him back off his feet, then drags him back and down in two steps
    each way (her root travels back with her: the game moves her capsule by it), lets go over him and stands. Her hands
    ride his collarbones (from his solved frames) while she holds him."""
    victim = SOLVED.get("Takedown_Victim")
    if victim is None:
        raise RuntimeError("Takedown_Attacker needs Takedown_Victim built first (ue4 before uefn)")
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    hl = tuple(rig.idle_hand("l"))
    hr = tuple(rig.idle_hand("r"))
    t = TAKEDOWN_SECONDS
    back = TAKEDOWN_RETREAT

    def on_him(frame, bone, offset):
        p = victim[frame][bone]
        return tuple(v / KATE_SCALE for v in (p[0] + offset[0], p[1] - TAKEDOWN_GAP + offset[1], p[2] + offset[2]))

    def behind(v, d):
        return add(v, V(0, d, 0))

    first = int(round(0.30 * ck.FPS))
    reach_l = on_him(first, "spine_02", (20, -8, -12))
    reach_r = on_him(first, "clavicle_l", (-2, -12, -4))
    # The arms come up in an arc: out and up from her sides, then in over his shoulders.
    hand_l = [(0.0, hl), (0.10, add(hl, V(6, -22, 14))), (0.20, add(_lerp(hl, reach_l, 0.6), V(10, -16, -4)))]
    hand_r = [(0.0, hr), (0.10, add(hr, V(-4, -22, 16))), (0.20, add(_lerp(hr, reach_r, 0.6), V(-6, -22, -8)))]
    for k in (0.30, 0.4, 0.5, 0.6, 0.75, 0.9, 1.0, 1.1):
        f = int(round(k * ck.FPS))
        hand_r.append((k, on_him(f, "clavicle_l", (-2, -12, -4))))
        hand_l.append((k, on_him(f, "spine_02", (20, -8, -12))))
    hand_l += [(1.30, behind(add(hl, V(8, -24, 10)), back - 20)), (1.48, behind(add(hl, V(2, -10, 5)), back)),
               (1.62, behind(add(hl, V(0, -3, 1)), back)), (t, behind(hl, back))]
    hand_r += [(1.30, behind(add(hr, V(-10, -30, 10)), back - 20)), (1.48, behind(add(hr, V(-4, -12, 5)), back)),
               (1.62, behind(add(hr, V(0, -3, 1)), back)), (t, behind(hr, back))]
    # Feet (world, where she started): the left steps in; then right, left, right, left back, two each.
    l_in = add(fl, V(-2, -22, 0))
    r1 = behind(fr, 30)
    l1 = behind(fl, 34)
    r2 = behind(fr, back)
    l2 = behind(fl, back)
    tracks = {
        "root_pos": T([(0.0, V(0, 0, 0)), (0.45, V(0, 0, 0)), (0.80, V(0, 30, 0)), (1.10, V(0, 64, 0)), (1.45, V(0, back - 3, 0)),
                       (1.60, V(0, back, 0)), (t, V(0, back, 0))]),
        "pelvis_pos": T([(0.0, P), (0.15, add(P, V(0, -10, -4))), (0.45, add(P, V(-2, -2, -10))), (0.80, behind(add(P, V(0, -2, -18)), 30)),
                         (1.10, behind(add(P, V(0, -6, -26)), 64)), (1.35, behind(add(P, V(0, -4, -12)), back - 4)),
                         (1.58, behind(P, back)), (t, behind(P, back))]),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.15, V(4, -4, 0)), (0.45, V(-2, -12, 0)), (0.80, V(8, -6, 0)),
                         (1.10, V(16, 0, 0)), (1.35, V(6, 0, 0)), (1.58, V(-1, 0, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(0, 0, 0)), (0.15, V(8, -4, 0)), (0.45, V(-6, -14, 0)), (0.80, V(10, -6, 0)), (1.10, V(26, 0, 0)),
                    (1.35, V(10, 0, 0)), (1.58, V(-2, 0, 0)), (t, V(0, 0, 0))]),
        "neck": T([(0.0, V(0, 0, 0)), (0.15, V(6, 4, 0)), (0.45, V(4, 8, 0)), (0.80, V(16, 4, 0)), (1.10, V(24, 0, 0)),
                   (1.35, V(14, 0, 0)), (1.58, V(-2, 0, 0)), (t, V(0, 0, 0))]),
        "foot_l": T(_step(fl, l_in, 0.03, 0.20, 5.0) + [(0.66, l_in)] + _step(l_in, l1, 0.66, 0.90, 6.0)[1:]
                    + [(1.18, l1)] + _step(l1, l2, 1.18, 1.42, 5.0)[1:] + [(t, l2)]),
        "foot_l_rot": T(_step_rot(0.03, 0.20, -10.0) + [(0.66, V(0, 0, 0))] + _step_rot(0.66, 0.90, -10.0)[1:]
                        + [(1.18, V(0, 0, 0))] + _step_rot(1.18, 1.42, -8.0)[1:] + [(t, V(0, 0, 0))]),
        "foot_r": T([(0.0, fr), (0.46, fr)] + _step(fr, r1, 0.46, 0.68, 6.0)[1:] + [(0.95, r1)]
                    + _step(r1, r2, 0.95, 1.20, 6.0)[1:] + [(t, r2)]),
        "foot_r_rot": T([(0.0, V(0, 0, 0))] + _step_rot(0.46, 0.68, -10.0) + [(0.95, V(0, 0, 0))]
                        + _step_rot(0.95, 1.20, -10.0)[1:] + [(t, V(0, 0, 0))]),
        "hand_l": T(hand_l),
        "hand_r": T(hand_r),
        "pole_hand_r": T([(0.0, V(-1.0, 0.0, -0.1)), (1.12, V(-1.0, 0.0, -0.1)), (1.45, V(-0.8, 0.4, -0.4)),
                          (t, V(-0.6, 0.5, -0.6))]),
        "pole_hand_l": T([(0.0, V(1.0, 0.0, -0.1)), (1.12, V(1.0, 0.0, -0.1)), (1.45, V(0.9, 0.4, -0.4)),
                          (t, V(0.9, 0.4, -0.4))]),
        "fingers_l": T([(0.0, 1.0), (0.15, 1.7), (1.1, 1.7), (1.3, 1.0), (t, 1.0)]),
        "fingers_r": T([(0.0, 1.0), (0.15, 1.5), (1.1, 1.5), (1.3, 1.0), (t, 1.0)]),
    }
    return ck.Clip("Takedown_Attacker", t, tracks, fade_out=0.15,
                   planted=[("l", 0.20, 0.66, "flat"), ("r", 0.0, 0.46, "flat"), ("r", 0.70, 0.95, "flat"),
                            ("l", 0.92, 1.18, "flat"), ("r", 1.22, t, "flat"), ("l", 1.46, t, "flat")],
                   strip_times=[0.0, 0.15, 0.4, 0.65, 0.9, 1.15, 1.4, t], torso_clear=False,
                   extra={"pair": "attacker", "gap": TAKEDOWN_GAP, "span": (-100.0, 160.0), "retreat": back,
                          # The grab is fast on purpose: the arm comes from her side to across his chest in 0.3 s.
                          "pop_degrees": 36.0})


# --------------------------------------------------------------------------------------------------
# the weld (2026-11-23): reactions that start on their damaged pose, and Kate's strikes built around the hit frame
# --------------------------------------------------------------------------------------------------

# claude-docs/gameplay-semantics.md, "The weld": a landed blow freezes both fighters on the hit pose, so a reaction clip's
# FIRST frame is already the damaged pose (the game starts it there, HitPeakTime 0, with no blend), and a strike's clip has
# a hold at full extension on the frame its hit window opens. Rates are 1.0 (nothing sped up).

# How far a rig's centimetres are from the game's: Kate's driver is drawn at 0.93.
RIG_SCALE = {"ue4": 1.0, "uefn": KATE_SCALE}
# From a hand's or foot's bone to what meets him (knuckles past the wrist, toes past the ankle), and from his surface to the
# bone the warp aims at (his ribs, his skull's centre): the clip's contact distance is the bone's flat reach plus both.
REACH = {"hand": 9.0, "foot": 14.0}
BODY_DEPTH = 16.0
# The knock-back the game gives a reaction (UHitReactionComponent's weld tuning): a light 10 cm, a heavy 28, over 0.3 s
# eased like (1 - t) ^ 2 in speed: a distance of 1 - (1 - t / 0.3) ^ 3 of it by t.
KNOCK_SECONDS = 0.3
# The share of the slide a planted foot holds in the world. All of it overstretches the leg (the slide is fastest at the
# start: a heavy's 28 cm goes at 280 cm/s), so a planted foot skids with the capsule for the rest: 45% of the slide.
PLANT_HOLD = 0.55


def knock_share(t):
    """The share of the knock-back done by t seconds: the integral of the eased speed."""
    u = min(max(t / KNOCK_SECONDS, 0.0), 1.0)
    return 1.0 - (1.0 - u) ** 3


def dense(fn, t0, t1):
    """Keys for fn(t) at every frame from t0 to t1 (the feet's drift against the knock-back is a curve of its own)."""
    n0, n1 = int(round(t0 * ck.FPS)), int(round(t1 * ck.FPS))
    return [(f / ck.FPS, fn(f / ck.FPS)) for f in range(n0, n1 + 1)]


def foot_with_knock(base, shove, steps, seconds, lift=6.0):
    """A foot's keys in the capsule's frame for a body the capsule slides back by `shove` cm (eased). Planted, it drifts
    toward the blow as the capsule leaves it (its place in the world holds); a step [(t0, t1)] lifts it, swings it back and
    sets it down on `base`, where it holds again against what is left of the slide. Ends on base at `seconds`."""
    toward = V(0, -1, 0)

    def planted(t, since):
        k = (knock_share(t) - knock_share(since)) * PLANT_HOLD
        return add(base, tuple(c * shove * k for c in toward))

    keys = []
    cursor = 0.0
    since = 0.0
    for t0, t1 in steps:
        keys += dense(lambda t, s=since: planted(t, s), cursor, t0)
        start = planted(t0, since)
        for f in range(int(round(t0 * ck.FPS)) + 1, int(round(t1 * ck.FPS)) + 1):
            t = f / ck.FPS
            u = ck.smoothstep((t - t0) / (t1 - t0))
            arc = lift * (1.0 - (2.0 * u - 1.0) ** 2)
            keys.append((t, add(tuple(s + (b - s) * u for s, b in zip(start, base)), V(0, 0, arc))))
        cursor = t1
        since = t1
    keys += dense(lambda t, s=since: planted(t, s), cursor, seconds)
    # One key per frame (a step's last frame and the planted run's first are the same).
    unique = {}
    for t, v in keys:
        unique[round(t, 4)] = v
    return sorted(unique.items())


def foot_turn(steps, seconds, pitch=-10.0):
    """The foot's turn over the steps: toes up as it swings, flat as it lands."""
    keys = [(0.0, V(0, 0, 0))]
    for t0, t1 in steps:
        keys += [(t0, V(0, 0, 0)), (t0 + (t1 - t0) * 0.45, V(pitch * 0.5, 0, 0)), (t0 + (t1 - t0) * 0.8, V(pitch, 0, 0)),
                 (t1, V(0, 0, 0))]
    keys.append((seconds, V(0, 0, 0)))
    return keys


def hit_zone(rig, zone, heavy):
    """A reaction from a blow in front, by where it landed (the head snaps, the gut folds, the legs buckle), starting on its
    damaged pose: frame 0 is the peak, the body then settles slowly past upright and back. The capsule slides back by the
    knock-back (eased), so a planted foot holds its place in the world (PLANT_HOLD of the slide) and a stepping foot steps
    back with it."""
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    hl = tuple(rig.idle_hand("l"))
    hr = tuple(rig.idle_hand("r"))
    k = 2.4 if heavy else 1.6
    t = 0.8 if heavy else 0.55
    shove = (HEAVY_SHOVE if heavy else LIGHT_SHOVE) / RIG_SCALE[rig.kind]
    steps_r = [(0.04, 0.22)] if heavy else [(0.05, 0.2)]
    steps_l = [(0.2, 0.42)] if heavy else [(0.32, 0.46)]
    if zone == "Legs":
        # The knees go: both feet stay put through the buckle, one shuffles back as he catches himself.
        steps_r = [(0.34, 0.5)] if not heavy else [(0.3, 0.5)]
        steps_l = []
    body = {}
    mid = t * 0.5
    if zone == "Head":
        body["chest"] = T([(0.0, V(-10 * k, 8 * k, 3 * k)), (0.08, V(-6 * k, 5 * k, 2 * k)), (0.2, V(1.5 * k, -1 * k, 0)),
                           (0.32, V(-0.8 * k, 0, 0)), (mid + 0.1, V(0.3, 0, 0)), (t, V(0, 0, 0))])
        body["neck"] = T([(0.0, V(-26 * k, 34 * k, 6 * k)), (0.06, V(-22 * k, 28 * k, 5 * k)), (0.16, V(-8 * k, 10 * k, 2 * k)),
                          (0.28, V(3 * k, -3 * k, 0)), (0.4, V(-1 * k, 0, 0)), (t, V(0, 0, 0))])
        body["pelvis_rot"] = T([(0.0, V(-3 * k, 3 * k, 0)), (0.2, V(0.5, 0, 0)), (t, V(0, 0, 0))])
        body["pelvis_pos"] = T([(0.0, add(P, V(0, 2 * k, -1))), (0.2, add(P, V(0, 1, -0.5))), (t, P)])
        hand_l = [(0.0, add(hl, V(2, -8, 12))), (0.15, add(hl, V(0, -4, 6))), (0.35, add(hl, V(0, -1, 1))), (t, hl)]
        hand_r = [(0.0, add(hr, V(-2, -8, 10))), (0.15, add(hr, V(0, -4, 5))), (0.35, add(hr, V(0, -1, 1))), (t, hr)]
    elif zone == "Gut":
        # A light blow is a flinch (a small hunch, the hips driven back) and must leave his head no nearer the attacker
        # than it began: 0.4 folds the chest about 10 degrees, the hips go back 8 cm. A heavy folds more but ends well
        # behind Kate's fist (1.3: about 34 degrees with the hips back and the 28 cm knock-back).
        k = 0.4 if not heavy else 1.3
        kb = 1.6 if not heavy else 1.3
        body["chest"] = T([(0.0, V(26 * k, 0, 0)), (0.1, V(32 * k, 0, 0)), (0.25, V(22 * k, 0, 0)), (mid + 0.05, V(8 * k, 0, 0)),
                           (t * 0.85, V(-1.5, 0, 0)), (t, V(0, 0, 0))])
        body["neck"] = T([(0.0, V(14 * k, 0, 0)), (0.12, V(18 * k, 0, 0)), (0.3, V(8 * k, 0, 0)), (t * 0.85, V(-2, 0, 0)), (t, V(0, 0, 0))])
        body["pelvis_rot"] = T([(0.0, V(8 * k, 0, 0)), (0.12, V(10 * k, 0, 0)), (0.3, V(4 * k, 0, 0)), (t, V(0, 0, 0))])
        # Hips driven back and down: the weight goes behind the feet.
        body["pelvis_pos"] = T([(0.0, add(P, V(0, 5 * kb, -4 * k))), (0.12, add(P, V(0, 7 * kb, -5 * k))), (0.3, add(P, V(0, 3 * kb, -2 * k))),
                                (t, P)])
        # Both hands to the belly, then out and down.
        belly_l = add(P, V(8, -18, 20))
        belly_r = add(P, V(-8, -18, 20))
        hand_l = [(0.0, belly_l), (0.1, belly_l), (0.2, belly_l), (0.3, add(belly_l, V(2, -2, 2))), (0.55, add(hl, V(0, -4, 4))), (t, hl)]
        hand_r = [(0.0, belly_r), (0.1, belly_r), (0.2, belly_r), (0.3, add(belly_r, V(-2, -2, 2))), (0.55, add(hr, V(0, -4, 4))), (t, hr)]
    else:
        drop = 12.0 * k
        body["pelvis_pos"] = T([(0.0, add(P, V(0, 3 * k, -drop * 0.8))), (0.1, add(P, V(0, 4 * k, -drop))),
                                (0.25, add(P, V(0, 2 * k, -drop * 0.45))), (0.4, add(P, V(0, 0.5, -1.5))), (t, P)])
        body["pelvis_rot"] = T([(0.0, V(6 * k, 0, 0)), (0.12, V(8 * k, 0, 0)), (0.3, V(2 * k, 0, 0)), (t, V(0, 0, 0))])
        body["chest"] = T([(0.0, V(12 * k, 0, 0)), (0.12, V(16 * k, 0, 0)), (0.3, V(6 * k, 0, 0)), (t * 0.85, V(-1, 0, 0)), (t, V(0, 0, 0))])
        body["neck"] = T([(0.0, V(6 * k, 0, 0)), (0.15, V(10 * k, 0, 0)), (0.35, V(2 * k, 0, 0)), (t, V(0, 0, 0))])
        # Arms out for balance.
        hand_l = [(0.0, add(hl, V(10, -10, 6))), (0.2, add(hl, V(8, -6, 8))), (0.4, add(hl, V(2, -2, 2))), (t, hl)]
        hand_r = [(0.0, add(hr, V(-10, -10, 6))), (0.2, add(hr, V(-8, -6, 8))), (0.4, add(hr, V(-2, -2, 2))), (t, hr)]
    body["hand_l"] = T(hand_l)
    body["hand_r"] = T(hand_r)
    body["foot_r"] = T(foot_with_knock(fr, shove, steps_r, t, lift=7.0 if heavy else 5.0))
    body["foot_l"] = T(foot_with_knock(fl, shove, steps_l, t, lift=6.0 if heavy else 4.0))
    body["foot_r_rot"] = T(foot_turn(steps_r, t))
    body["foot_l_rot"] = T(foot_turn(steps_l, t))
    name = "Hit{0}_{1}".format(zone, "Heavy" if heavy else "Light")
    return ck.Clip(name, t, body, fade_in=0.0, fade_out=0.12, torso_clear=False,
                   strip_times=[0.0, 0.04, 0.1, 0.17, 0.25, 0.34, 0.45, t],
                   extra={"span": (-110.0, 110.0), "hit_peak_s": 0.0,
                          "knockback_cm": HEAVY_SHOVE if heavy else LIGHT_SHOVE, "pop_degrees": 64.0 if heavy else 40.0})


def hit_head_light(rig):
    return hit_zone(rig, "Head", False)


def hit_head_heavy(rig):
    return hit_zone(rig, "Head", True)


def hit_gut_light(rig):
    return hit_zone(rig, "Gut", False)


def hit_gut_heavy(rig):
    return hit_zone(rig, "Gut", True)


def hit_legs_light(rig):
    return hit_zone(rig, "Legs", False)


def hit_legs_heavy(rig):
    return hit_zone(rig, "Legs", True)


ZONE_HITS = [hit_head_light, hit_head_heavy, hit_gut_light, hit_gut_heavy, hit_legs_light, hit_legs_heavy]


# ---- Kate's strikes ----

# The anatomy these are keyed against, from the rig itself: where the shoulder is when the body has turned and leaned is not
# a number to guess, so each hand is keyed as an offset from the solved shoulder (the body's tracks solved on their own).


def shoulder_keys(rig, body, side, keys):
    """Keys for a hand in the world the clip starts in: the solved shoulder at each key's time plus the key's offset (armature
    axes: forward -Y, his left +X, up +Z). `body` is the body's own tracks, already in that world (the root's travel in
    its pelvis), solved on their own."""
    probe = ck.Clip("probe", max(t for t, _ in keys) + 0.1, dict(body))
    out = []
    for t, offset in keys:
        pose = rig.solve(probe.params(t))
        out.append((t, add(tuple(pose["upperarm_" + side].translation), V(*offset))))
    return out


def hand_track(rig, body, root, side, keys, seconds):
    """A hand's track: the idle's place at both ends (the first and last key are placeholders), the inner keys on the
    shoulder."""
    idle = tuple(rig.idle_hand(side))
    return T([(0.0, idle)] + shoulder_keys(rig, body, side, keys[1:-1]) + [(seconds, add(idle, tuple(root(seconds))))])


def world(keys, root):
    """Body-frame place keys [(t, place)] into the world the clip starts in: plus the root's travel at each time."""
    return [(t, add(place, tuple(root(t)))) for t, place in keys]


def root_step(distance, t0, t1):
    """The capsule's travel, an early step-in (so the warp that scales it has the wind-up to do it in, not the hit's last
    frames): nothing until t0, eased to `distance` forward (-Y) at t1."""
    mid = (t0 + t1) * 0.5
    keys = [(0.0, V(0, 0, 0))]
    if t0 > 0.0:
        keys.append((t0, V(0, 0, 0)))
    keys += [(mid, V(0, -distance * 0.55, 0)), (t1, V(0, -distance, 0)), (t1 + 2.0, V(0, -distance, 0))]
    return T(keys)


def strike_feet(rig, step, lead_steps, rear_steps, seconds):
    """Both feet keyed in the world the clip starts in: the capsule travels `step` cm forward, so each foot is planted in the
    world, stepping forward by `step` to follow it (lead_steps, rear_steps are (t0, t1) windows)."""
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    fwd = V(0, -step, 0)

    def one(base, steps):
        keys = [(0.0, base)]
        cur = base
        for t0, t1 in steps:
            if t0 > keys[-1][0] + 1e-6:
                keys.append((t0, cur))
            target = add(base, fwd)
            keys += [(t0 + (t1 - t0) * 0.45, add(tuple((c + g) * 0.5 for c, g in zip(cur, target)), V(0, 0, 7))), (t1, target)]
            cur = target
        keys.append((seconds, cur))
        return keys

    return one(fl, lead_steps), one(fr, rear_steps)


def _strike_clip(rig, name, seconds, body, root, hands, feet, hit, combo, limb, strip, extra=None):
    tracks = dict(body)
    tracks["root_pos"] = root
    for side, keys in hands.items():
        tracks["hand_" + side] = hand_track(rig, body, root, side, keys, seconds)
    tracks["foot_l"] = T(feet[0])
    tracks["foot_r"] = T(feet[1])
    info = {"span": (-110.0, 150.0), "pop_degrees": 52.0, "hit_s": list(hit), "combo_s": list(combo),
            "contact": [limb, hit[0]], "rate": 1.0}
    info.update(extra or {})
    return ck.Clip(name, seconds, tracks, fade_in=0.0, fade_out=0.15, torso_clear=False, strip_times=strip, extra=info)


GUARD_L = (6, -16, -14)       # the left hand's guard, from the left shoulder: in front of the chin
GUARD_R = (-6, -16, -14)


def strike_jab(rig):
    """A left jab, 0.77 s: a dip and a quick step in on the lead foot (4 frames), the hand snaps out in 4 frames and holds
    at full extension for 3 on the hit, snaps back along the same line to the guard. The hips turn only a little; the
    weight goes onto the front foot."""
    P = tuple(rig.idle_pelvis)
    t = 0.77
    root = root_step(30.0, 0.0, 0.2)
    body = {
        "pelvis_pos": T(world([(0.0, P), (0.13, add(P, V(0, 3, -4))), (0.27, add(P, V(0, -8, -3))), (0.37, add(P, V(0, -9, -3))),
                         (0.5, add(P, V(0, -4, -1.5))), (t, P)], root)),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.13, V(2, -5, 0)), (0.27, V(5, 10, 0)), (0.37, V(5, 11, 0)), (0.5, V(1, 3, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(0, 0, 0)), (0.13, V(3, -4, 0)), (0.27, V(6, 12, 0)), (0.37, V(6, 13, 0)), (0.5, V(2, 3, 0)), (t, V(0, 0, 0))]),
        "neck": T([(0.0, V(0, 0, 0)), (0.13, V(2, 0, 0)), (0.27, V(-2, -8, 0)), (0.37, V(-2, -9, 0)), (0.5, V(0, -2, 0)), (t, V(0, 0, 0))]),
    }
    hands = {
        "l": [(0.0, (0, -8, -30)), (0.13, GUARD_L), (0.27, (4, -64, -2)), (0.37, (4, -66, -2)), (0.45, (4, -52, -4)),
              (0.55, GUARD_L), (t, (0, -8, -30))],
        "r": [(0.0, (0, -8, -30)), (0.13, GUARD_R), (0.27, (-8, -16, -12)), (0.37, (-8, -16, -12)), (0.55, GUARD_R), (t, (0, -8, -30))],
    }
    feet = strike_feet(rig, 30.0, [(0.0, 0.18)], [(0.4, 0.58)], t)
    return _strike_clip(rig, "Strike_Jab", t, body, root, hands, feet, (0.27, 0.37), (0.43, 0.72), "hand_l",
                        [0.0, 0.07, 0.13, 0.2, 0.27, 0.33, 0.45, t])


def strike_cross(rig):
    """A right cross, 0.9 s: the weight goes back and the right shoulder loads (5 frames), the hips drive round and the hand
    comes straight out in 4, holds at full extension for 3 on the hit with the whole body behind it, follows through a
    little past and comes back to the guard while the rear foot squares up."""
    P = tuple(rig.idle_pelvis)
    t = 0.9
    root = root_step(32.0, 0.0, 0.22)
    body = {
        "pelvis_pos": T(world([(0.0, P), (0.17, add(P, V(0, 5, -5))), (0.3, add(P, V(0, -9, -4))), (0.4, add(P, V(0, -10, -4))),
                         (0.55, add(P, V(0, -5, -2))), (t, P)], root)),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.17, V(3, -18, 0)), (0.3, V(6, 24, 0)), (0.4, V(6, 26, 0)), (0.55, V(2, 8, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(0, 0, 0)), (0.17, V(4, -14, 0)), (0.3, V(7, 20, 0)), (0.4, V(7, 22, 0)), (0.55, V(2, 6, 0)), (t, V(0, 0, 0))]),
        "neck": T([(0.0, V(0, 0, 0)), (0.17, V(3, 6, 0)), (0.3, V(-3, -16, 0)), (0.4, V(-3, -18, 0)), (0.55, V(0, -4, 0)), (t, V(0, 0, 0))]),
    }
    hands = {
        "r": [(0.0, (0, -8, -30)), (0.17, (-5, -10, -12)), (0.3, (-4, -68, -4)), (0.4, (-4, -71, -4)), (0.48, (-4, -76, -5)),
              (0.6, GUARD_R), (t, (0, -8, -30))],
        "l": [(0.0, (0, -8, -30)), (0.17, GUARD_L), (0.3, (6, -14, -16)), (0.4, (6, -14, -16)), (0.6, GUARD_L), (t, (0, -8, -30))],
    }
    feet = strike_feet(rig, 32.0, [(0.0, 0.2)], [(0.42, 0.62)], t)
    return _strike_clip(rig, "Strike_Cross", t, body, root, hands, feet, (0.3, 0.4), (0.48, 0.88), "hand_r",
                        [0.0, 0.08, 0.17, 0.24, 0.3, 0.36, 0.5, t])


def strike_hook(rig):
    """A right hook, 0.95 s: a half crouch and the right elbow loads out and back (6 frames), the hips and shoulders whip
    round and the fist swings in a flat arc across the line to him in 4, holds at the end of the arc for 3 on the hit with
    the elbow high, and swings on a little before coming back."""
    P = tuple(rig.idle_pelvis)
    t = 0.95
    root = root_step(30.0, 0.0, 0.24)
    body = {
        "pelvis_pos": T(world([(0.0, P), (0.2, add(P, V(0, 4, -7))), (0.33, add(P, V(0, -8, -4))), (0.43, add(P, V(0, -9, -4))),
                         (0.6, add(P, V(0, -4, -2))), (t, P)], root)),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.2, V(2, -22, 0)), (0.33, V(5, 30, 0)), (0.43, V(5, 33, 0)), (0.6, V(2, 10, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(0, 0, 0)), (0.2, V(4, -18, -4)), (0.33, V(6, 26, 4)), (0.43, V(6, 29, 4)), (0.6, V(2, 8, 0)), (t, V(0, 0, 0))]),
        "neck": T([(0.0, V(0, 0, 0)), (0.2, V(3, 8, 0)), (0.33, V(-3, -22, 0)), (0.43, V(-3, -24, 0)), (0.6, V(0, -6, 0)), (t, V(0, 0, 0))]),
    }
    hands = {
        "r": [(0.0, (0, -8, -30)), (0.2, (-22, -2, -4)), (0.26, (-30, -26, 0)), (0.33, (2, -50, 0)), (0.43, (6, -52, 0)),
              (0.52, (10, -46, 0)), (0.66, GUARD_R), (t, (0, -8, -30))],
        "l": [(0.0, (0, -8, -30)), (0.2, GUARD_L), (0.33, (4, -14, -16)), (0.43, (4, -14, -16)), (0.66, GUARD_L), (t, (0, -8, -30))],
    }
    feet = strike_feet(rig, 30.0, [(0.0, 0.2)], [(0.46, 0.66)], t)
    clip = _strike_clip(rig, "Strike_Hook", t, body, root, hands, feet, (0.33, 0.43), (0.52, 0.92), "hand_r",
                        [0.0, 0.1, 0.2, 0.27, 0.33, 0.4, 0.55, t], extra={"wide": True})
    # The elbow rides out and high through the arc.
    clip.tracks["pole_hand_r"] = T([(0.0, V(-0.6, 0.5, -0.8)), (0.2, V(-1.0, 0.3, 0.2)), (0.33, V(-0.8, 0.1, 0.9)),
                                    (0.43, V(-0.8, 0.1, 0.9)), (0.66, V(-0.6, 0.5, -0.8)), (t, V(-0.6, 0.5, -0.8))])
    return clip


def strike_front_kick(rig):
    """A right front kick, 1.1 s: a step in, the right knee comes up and the hips sit back to load it (6 frames), the shin
    snaps out in 4 and the foot holds at full extension for 3 on the hit with the chest leaning back to balance it, then the
    leg folds and comes back down to plant."""
    P = tuple(rig.idle_pelvis)
    fl = tuple(rig.idle_foot("l"))
    fr = tuple(rig.idle_foot("r"))
    t = 1.1
    step = 28.0
    root = root_step(step, 0.0, 0.2)
    body = {
        "pelvis_pos": T(world([(0.0, P), (0.2, add(P, V(0, 6, -3))), (0.33, add(P, V(0, 2, -1))), (0.43, add(P, V(0, 1, -1))),
                               (0.65, add(P, V(0, -1, -2))), (t, P)], root)),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.2, V(-6, -4, 0)), (0.33, V(-12, 6, 0)), (0.43, V(-13, 6, 0)), (0.65, V(-3, 1, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(0, 0, 0)), (0.2, V(-4, -4, 0)), (0.33, V(-10, 4, 0)), (0.43, V(-11, 4, 0)), (0.65, V(-2, 0, 0)), (t, V(0, 0, 0))]),
        "neck": T([(0.0, V(0, 0, 0)), (0.2, V(2, 0, 0)), (0.33, V(8, 0, 0)), (0.43, V(8, 0, 0)), (0.65, V(1, 0, 0)), (t, V(0, 0, 0))]),
    }
    fwd = add(fr, V(0, -step, 0))
    left_new = add(fl, V(0, -step, 0))
    # The right foot: planted, knee up (chamber), snapped out to his gut, folded back, set down on the new place.
    foot_r = [(0.0, fr), (0.07, add(fr, V(0, -4, 6))), (0.2, add(fwd, V(0, 14, 36))), (0.245, add(fwd, V(0, -10, 52))), (0.29, add(fwd, V(0, -40, 70))),
              (0.33, add(fwd, V(0, -64, 80))), (0.43, add(fwd, V(0, -66, 82))), (0.52, add(fwd, V(0, -40, 60))),
              (0.65, add(fwd, V(0, -10, 24))), (0.8, add(fwd, V(0, 0, 4))), (0.9, fwd), (t, fwd)]
    # The left foot steps in with the capsule (0.02 to 0.12), then bears the whole kick, planted.
    foot_l = [(0.0, fl), (0.02, fl), (0.07, add(tuple((x + y) * 0.5 for x, y in zip(fl, left_new)), V(0, 0, 6))), (0.12, left_new),
              (t, left_new)]
    hands = {
        "r": [(0.0, V(0, 0, 0)), (0.2, (-4, -14, -14)), (0.33, (-10, -22, -4)), (0.43, (-10, -22, -4)), (0.65, GUARD_R), (t, V(0, 0, 0))],
        "l": [(0.0, V(0, 0, 0)), (0.2, GUARD_L), (0.33, (14, -12, 8)), (0.43, (14, -12, 8)), (0.65, GUARD_L), (t, V(0, 0, 0))],
    }
    tracks = dict(body)
    tracks["root_pos"] = root
    for side, keys in hands.items():
        tracks["hand_" + side] = hand_track(rig, body, root, side, keys, t)
    tracks["foot_l"] = T(foot_l)
    tracks["foot_r"] = T(foot_r)
    tracks["foot_r_rot"] = T([(0.0, V(0, 0, 0)), (0.2, V(30, 0, 0)), (0.33, V(-30, 0, 0)), (0.43, V(-30, 0, 0)), (0.65, V(10, 0, 0)),
                              (0.85, V(0, 0, 0)), (t, V(0, 0, 0))])
    return ck.Clip("Strike_FrontKick", t, tracks, fade_in=0.0, fade_out=0.15, torso_clear=False,
                   strip_times=[0.0, 0.1, 0.2, 0.27, 0.33, 0.42, 0.6, t],
                   extra={"span": (-110.0, 150.0), "hit_s": [0.33, 0.43], "combo_s": [0.6, 1.05], "pop_degrees": 70.0,
                          "contact": ["foot_r", 0.33], "rate": 1.0})


STRIKES = [strike_jab, strike_cross, strike_hook, strike_front_kick]


def measure_contact(kind, clip, frames):
    """The clip's contact distance and height at its hit frame, in the game's units, into its extra (and so clips.json, which
    create_combat_anims.py reads): the striking bone's flat reach from the capsule, past the bone to what meets him, and past
    his surface to the bone the warp aims at; and its height over the floor."""
    spec = clip.extra.get("contact")
    if not spec:
        return
    bone, t = spec
    f = min(int(round(t * ck.FPS)), len(frames) - 1)
    p = frames[f][bone].translation
    reach = REACH["foot" if bone.startswith("foot") else "hand"]
    scale = RIG_SCALE[kind]
    clip.extra["contact_cm"] = round((math.hypot(p.x, p.y) + reach + BODY_DEPTH) * scale, 1)
    clip.extra["contact_height_cm"] = round(p.z * scale, 1)


# --------------------------------------------------------------------------------------------------
# the ledge clips (2026-11-25): a roof dismount, the catch, the hang, the let-go, the climb up, a vault's plant
# --------------------------------------------------------------------------------------------------

# claude-docs/gameplay-semantics.md, "Ledge body". Numbers in game centimetres (HANG_*, PARAPET ...) are the game's and are
# divided by the driver's 0.93 scale into this rig's. The clips are keyed in the capsule's own frame (the rig's origin is the
# capsule's bottom, she faces -Y, her left is +X) and the capsule's path is the game's (UParkourComponent: the hang is 145 cm
# under the ledge top and 36 cm out from its edge, a dismount is a hop over a 90 cm parapet that turns her round in the first
# 45% of 0.5 s, a climb goes up first and then forward), so the ledge moves past her here exactly as it does in the game.
# The visible body's IK (Player/LedgeIKNode.h) finishes the contact against the real edge: a hand here only has to be close.
# Clips are in place (no root motion). Feet may go below the capsule's bottom in a hang (the arms are straighter than the
# 145 cm hang strictly allows with her feet on the floor level), so these clips set extra floor_cm.
HANG_BACK = 36.0
HANG_BELOW = 145.0
HAND_HALF_GAP = 19.0
WRIST_UP = 2.5
SHOULDER_OUT = 17.0
ARM_STRAIGHT = 0.94
BALL_FROM_WALL = 5.0
PARAPET = 90.0
START_BACK = 50.0
DROP_HOP = PARAPET + 12.0
DROP_SECONDS = 0.5
DROP_TURN = 0.45
CLIMB_SECONDS = 1.0
CLIMB_FORWARD = 46.0
PUSH_BACK = 22.0
LEDGE_FLOOR = -45.0


def _r(v):
    """Game centimetres to this rig's."""
    return v / KATE_SCALE


def _lerp3(a, b, f):
    return a + (b - a) * f


def hang_pose(rig):
    """The hang itself, from the rig's own proportions: the wrists on the edge a shoulder width apart, the shoulders hung
    ARM_STRAIGHT of an arm's length under them, the feet braced on the wall."""
    wall_y = -_r(HANG_BACK)
    ledge_z = _r(HANG_BELOW)
    wrist_z = ledge_z + _r(WRIST_UP)
    wrist_y = wall_y + _r(1.0)
    hx = _r(HAND_HALF_GAP)
    sh = tuple(rig.idle_pos["upperarm_l"] - rig.idle_pelvis)
    arm = ARM_STRAIGHT * (rig.upper_arm + rig.fore_arm)
    sh_y = wall_y + _r(SHOULDER_OUT)
    horiz = abs(sh_y - wrist_y)
    vert = math.sqrt(max(arm * arm - horiz * horiz, 1.0))
    pelvis = V(0.0, sh_y - sh[1], wrist_z - vert - sh[2])
    ball_y = wall_y + _r(BALL_FROM_WALL)
    feet = {}
    for side in ("l", "r"):
        idle = tuple(rig.idle_foot(side))
        rel_z = idle[2] - rig.idle_pelvis[2]
        off = tuple(rig.ball_offset(side))
        feet[side] = V(idle[0] * 0.85, ball_y - off[1], pelvis[2] + rel_z * 0.97)
    return {"wall_y": wall_y, "ledge_z": ledge_z, "wrist_y": wrist_y, "wrist_z": wrist_z, "hx": hx, "sh": sh, "arm": arm,
            "pelvis": pelvis, "hand_l": V(hx, wrist_y, wrist_z), "hand_r": V(-hx, wrist_y, wrist_z),
            "foot_l": feet["l"], "foot_r": feet["r"]}


def _reach_limited(shoulder, target, length):
    """target, or the point on the line from the shoulder toward it that an arm of this length reaches (the arm points at what
    it cannot yet touch)."""
    d = tuple(t - s for t, s in zip(target, shoulder))
    dist = math.sqrt(sum(c * c for c in d))
    if dist <= length or dist < 1e-6:
        return target
    return tuple(s + c * length / dist for s, c in zip(shoulder, d))


def _smoothed(keys, passes=2):
    """Per-frame keys [(t, place)] with each place averaged with its neighbours (1-2-1), `passes` times, the ends held: a hand
    that follows an edge sweeping past her does not flick when its target comes into reach."""
    for _ in range(passes):
        out = [keys[0]]
        for i in range(1, len(keys) - 1):
            a, b, c = keys[i - 1][1], keys[i][1], keys[i + 1][1]
            out.append((keys[i][0], tuple(0.25 * x + 0.5 * y + 0.25 * z for x, y, z in zip(a, b, c))))
        out.append(keys[-1])
        keys = out
    return keys


def _arm_pole(side, down=-0.3):
    """The elbow out to its side, behind her (away from the wall) and a little down."""
    return V(0.9 if side == "l" else -0.9, 0.4, down)


def ledge_hang_idle(rig):
    """Hanging on both hands, nearly straight arms, toes on the brick, a slow sway that settles; the first and last frames
    are the same pose (it loops, and the catch and the dismount both end on it)."""
    hp = hang_pose(rig)
    P = hp["pelvis"]
    t = 1.6
    tracks = {
        "pelvis_pos": T([(0.0, P), (0.4, add(P, V(0.3, 0.9, 0.5))), (0.8, add(P, V(0.0, 1.5, 0.0))),
                         (1.2, add(P, V(-0.3, 0.7, -0.5))), (t, P)]),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.4, V(1.5, 1.0, 0.8)), (0.8, V(2.5, 0, 0)), (1.2, V(1.5, -1.0, -0.8)),
                         (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(-4, 0, 0)), (0.8, V(-2, 0, 0)), (t, V(-4, 0, 0))]),
        "neck": T([(0.0, V(-18, 0, 0)), (0.8, V(-14, 3, 0)), (t, V(-18, 0, 0))]),
        "hand_l": hp["hand_l"], "hand_r": hp["hand_r"],
        "pole_hand_l": _arm_pole("l"), "pole_hand_r": _arm_pole("r"),
        "foot_l": hp["foot_l"], "foot_r": hp["foot_r"],
        "fingers_l": 1.35, "fingers_r": 1.35,
    }
    return ck.Clip("Ledge_HangIdle", t, tracks, planted=[("l", 0.0, t, "flat"), ("r", 0.0, t, "flat")],
                   strip_times=[0.0, 0.4, 0.8, 1.2, 1.6], torso_clear=False,
                   extra={"loop": True, "floor_cm": LEDGE_FLOOR, "span": (-120.0, 120.0), "pop_degrees": 30.0})


def ledge_catch(rig):
    """Arms up for the edge before the hands arrive, the hands close on it, the body's weight comes on (arms stretched, feet
    out behind), it swings in under the hands, the knees bend and the toes meet the wall; ends on the hang pose."""
    hp = hang_pose(rig)
    P, hl, hr, fl, fr = hp["pelvis"], hp["hand_l"], hp["hand_r"], hp["foot_l"], hp["foot_r"]
    t = 0.45
    up = V(0.0, 1.5, 9.0)
    tracks = {
        "pelvis_pos": T([(0.0, add(P, V(0, 16, 8))), (0.10, add(P, V(0, 14, -4))), (0.20, add(P, V(0, 8, -5))),
                         (0.30, add(P, V(0, -2, -2))), (0.40, add(P, V(0, 0.5, 0.5))), (t, P)]),
        "pelvis_rot": T([(0.0, V(8, 0, 0)), (0.10, V(14, 0, 0)), (0.20, V(8, 0, 0)), (0.30, V(-2, 0, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(-8, 0, 0)), (0.10, V(-14, 0, 0)), (0.30, V(-6, 0, 0)), (t, V(-4, 0, 0))]),
        "neck": T([(0.0, V(-22, 0, 0)), (0.30, V(-20, 0, 0)), (t, V(-18, 0, 0))]),
        "hand_l": T([(0.0, add(hl, up)), (0.08, add(hl, V(0, 0, 2.0))), (0.12, hl), (t, hl)]),
        "hand_r": T([(0.0, add(hr, up)), (0.08, add(hr, V(0, 0, 2.0))), (0.12, hr), (t, hr)]),
        "pole_hand_l": _arm_pole("l"), "pole_hand_r": _arm_pole("r"),
        "foot_l": T([(0.0, add(fl, V(0, 24, 4))), (0.10, add(fl, V(0, 26, -2))), (0.22, add(fl, V(0, 12, 0))),
                     (0.34, add(fl, V(0, 2, 1))), (0.40, fl), (t, fl)]),
        "foot_r": T([(0.0, add(fr, V(0, 22, 6))), (0.10, add(fr, V(0, 25, -1))), (0.24, add(fr, V(0, 11, 0))),
                     (0.35, add(fr, V(0, 2, 1))), (0.40, fr), (t, fr)]),
        "fingers_l": T([(0.0, 1.0), (0.10, 1.0), (0.16, 1.35), (t, 1.35)]),
        "fingers_r": T([(0.0, 1.0), (0.10, 1.0), (0.16, 1.35), (t, 1.35)]),
    }
    return ck.Clip("Ledge_Catch", t, tracks, planted=[("l", 0.40, t, "flat"), ("r", 0.40, t, "flat")],
                   strip_times=[0.0, 0.08, 0.14, 0.22, 0.30, 0.38, t], torso_clear=False,
                   extra={"floor_cm": LEDGE_FLOOR, "span": (-120.0, 120.0), "pop_degrees": 34.0})


FALL_SECONDS = 1.0


def _fall_pose(rig, ph):
    """The fall after a let-go at phase ph (turns, 0..1 loops), in the capsule's frame: standing height with the knees soft,
    the torso a little forward and the head down at the landing, the arms forward and up for balance (hands in front of the
    shoulders, elbows down, not out to the side), small continuous motion: the arms circling a few cm against each other, the
    legs pedalling, the torso breathing with it."""
    w = 2.0 * math.pi * ph
    ip = tuple(rig.idle_pelvis)
    sh = tuple(rig.idle_pos["upperarm_l"] - rig.idle_pelvis)
    reach = rig.upper_arm + rig.fore_arm
    lean = 3.0 + 1.0 * math.sin(w)
    chest_lean = 5.0 + 1.2 * math.sin(w + 1.0)
    total = math.radians(lean + chest_lean)
    pelvis = V(0.0, ip[1] + 1.5 + 0.8 * math.sin(w + 0.5), ip[2] - _r(1.6) + 0.5 * math.sin(2.0 * w))
    out = {"pelvis_pos": pelvis, "pelvis_rot": V(lean, 0.0, 4.0 * math.sin(w)),
           "chest": V(5.0 + 1.5 * math.sin(w + 1.0), 2.5 * math.sin(w + 0.4), 3.0 * math.sin(w + 2.0)),
           "neck": V(9.0 + 1.5 * math.sin(w + 2.0), 0.0, 0.0)}
    for side, s_, phase in (("l", 1.0, 0.0), ("r", -1.0, math.pi)):
        shoulder = (pelvis[0] + s_ * abs(sh[0]), pelvis[1] + sh[1] - sh[2] * math.sin(total), pelvis[2] + sh[2] * math.cos(total))
        ring = w + phase
        out["hand_" + side] = V(shoulder[0] + s_ * (9.0 + 7.0 * math.cos(ring)), shoulder[1] - 0.80 * reach + 6.0 * math.sin(ring),
                                shoulder[2] - 0.10 * reach + 8.0 * math.sin(ring + 1.2))
        out["pole_hand_" + side] = V(s_ * 0.45, 0.25, -1.0)
        idle = tuple(rig.idle_foot(side))
        stride = math.sin(w + phase)
        out["foot_" + side] = V(idle[0] * 0.80 + 3.0 * s_ * stride, idle[1] + 2.0 - 8.0 * stride, idle[2] + 0.5 + 2.5 * max(0.0, -stride))
        out["foot_%s_rot" % side] = V(18.0 + 6.0 * stride, 0.0, 0.0)
    return out


def _brace_pose(rig):
    """The last 0.2 s before the ground: the hips drop, the knees come up and forward, the arms come down and out a little
    for the landing, the head still on the ground."""
    ip = tuple(rig.idle_pelvis)
    sh = tuple(rig.idle_pos["upperarm_l"] - rig.idle_pelvis)
    reach = rig.upper_arm + rig.fore_arm
    pelvis = V(0.0, ip[1] + 4.0, ip[2] - _r(16.0))
    total = math.radians(12.0 + 16.0)
    out = {"pelvis_pos": pelvis, "pelvis_rot": V(12.0, 0.0, 0.0), "chest": V(16.0, 0.0, 0.0), "neck": V(12.0, 0.0, 0.0)}
    for side, s_ in (("l", 1.0), ("r", -1.0)):
        shoulder = (pelvis[0] + s_ * abs(sh[0]), pelvis[1] + sh[1] - sh[2] * math.sin(total), pelvis[2] + sh[2] * math.cos(total))
        out["hand_" + side] = V(shoulder[0] + s_ * 14.0, shoulder[1] - 0.55 * reach, shoulder[2] - 0.40 * reach)
        out["pole_hand_" + side] = V(s_ * 0.7, 0.2, -1.0)
        idle = tuple(rig.idle_foot(side))
        out["foot_" + side] = V(idle[0] * 0.85, idle[1] - _r(20.0), idle[2] + _r(13.0))
        out["foot_%s_rot" % side] = V(8.0, 0.0, 0.0)
    return out


def _as_tracks(pose_fn, t0, t1):
    """Tracks for every key of pose_fn(t) from t0 to t1 at the frame rate."""
    out = {}
    for t, pose in dense(pose_fn, t0, t1):
        for k, v in pose.items():
            out.setdefault(k, []).append((t, v))
    return out


def ledge_fall_loop(rig):
    """The loop after the let-go (and an instant drop): see _fall_pose. First and last frames are the same pose."""
    tracks = dict((k, T(v)) for k, v in _as_tracks(lambda t: _fall_pose(rig, t / FALL_SECONDS), 0.0, FALL_SECONDS).items())
    tracks["fingers_l"] = 0.9
    tracks["fingers_r"] = 0.9
    return ck.Clip("Ledge_FallLoop", FALL_SECONDS, tracks, strip_times=[0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875],
                   torso_clear=False, extra={"loop": True, "floor_cm": -5.0, "span": (-120.0, 120.0), "pop_degrees": 20.0})


def ledge_fall_brace(rig):
    """From the fall (whatever its phase: the montage blends in over 0.1 s) to the brace, then held for the landing's clip."""
    start = _fall_pose(rig, 0.0)
    end = _brace_pose(rig)
    t = 0.5
    ramp = 0.22
    tracks = {}
    for k in end:
        a, b = start[k], end[k]
        tracks[k] = T([(0.0, a), (ramp, b), (t, b)])
    tracks["fingers_l"] = 0.9
    tracks["fingers_r"] = 0.9
    return ck.Clip("Ledge_FallBrace", t, tracks, strip_times=[0.0, 0.07, 0.14, 0.22, 0.4], torso_clear=False,
                   extra={"floor_cm": -5.0, "span": (-120.0, 120.0), "pop_degrees": 30.0})


def ledge_hang_drop(rig):
    """From the hang: a small pull in, the hands let go and come off the edge, the body pushes off the wall, the arms come
    forward and up and the legs straighten under her, ending exactly on the fall loop's first frame (the loop takes it from
    there). The fall is not this clip's tail: held, a pose reads as a statue sliding down the wall."""
    hp = hang_pose(rig)
    P, hl, hr, fl, fr = hp["pelvis"], hp["hand_l"], hp["hand_r"], hp["foot_l"], hp["foot_r"]
    fp = _fall_pose(rig, 0.0)
    t = 0.5
    mid = lambda a, b, f: _lerp(a, b, f)  # noqa: E731
    push = add(P, V(0, 10, 6))
    tracks = {
        "pelvis_pos": T([(0.0, P), (0.07, add(P, V(0, 1, 3))), (0.15, push), (0.30, mid(push, fp["pelvis_pos"], 0.7)),
                         (t, fp["pelvis_pos"])]),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.12, V(-6, 0, 0)), (0.30, V(3, 0, 0)), (t, fp["pelvis_rot"])]),
        "chest": T([(0.0, V(-4, 0, 0)), (0.12, V(-10, 0, 0)), (0.30, V(2, 0, 0)), (t, fp["chest"])]),
        "neck": T([(0.0, V(-18, 0, 0)), (0.12, V(-14, 0, 0)), (0.30, V(4, 0, 0)), (t, fp["neck"])]),
        "hand_l": T([(0.0, hl), (0.07, hl), (0.15, add(hl, V(5, 5, 9))), (0.30, add(fp["hand_l"], V(4, 6, -6))), (t, fp["hand_l"])]),
        "hand_r": T([(0.0, hr), (0.07, hr), (0.15, add(hr, V(-5, 5, 9))), (0.30, add(fp["hand_r"], V(-4, 6, -6))), (t, fp["hand_r"])]),
        "pole_hand_l": _arm_pole("l"), "pole_hand_r": _arm_pole("r"),
        "foot_l": T([(0.0, fl), (0.10, fl), (0.20, add(fl, V(0, 8, 3))), (0.34, mid(add(fl, V(0, 8, 3)), fp["foot_l"], 0.8)), (t, fp["foot_l"])]),
        "foot_r": T([(0.0, fr), (0.10, fr), (0.20, add(fr, V(0, 8, 3))), (0.34, mid(add(fr, V(0, 8, 3)), fp["foot_r"], 0.8)), (t, fp["foot_r"])]),
        "foot_l_rot": T([(0.0, V(0, 0, 0)), (0.2, V(0, 0, 0)), (t, fp["foot_l_rot"])]),
        "foot_r_rot": T([(0.0, V(0, 0, 0)), (0.2, V(0, 0, 0)), (t, fp["foot_r_rot"])]),
        "fingers_l": T([(0.0, 1.35), (0.10, 1.35), (0.22, 0.9), (t, 0.9)]),
        "fingers_r": T([(0.0, 1.35), (0.10, 1.35), (0.22, 0.9), (t, 0.9)]),
    }
    return ck.Clip("Ledge_HangDrop", t, tracks, planted=[("l", 0.0, 0.10, "flat"), ("r", 0.0, 0.10, "flat")],
                   strip_times=[0.0, 0.07, 0.15, 0.22, 0.30, 0.38, 0.45, t], torso_clear=False,
                   extra={"floor_cm": LEDGE_FLOOR, "span": (-120.0, 120.0), "pop_degrees": 55.0, "pelvis_cm": 14.0})


def _dismount_edge_local(a, side):
    """The parapet's wrist point for a hand, in her own frame (rig cm) at a (0..1) of the dismount. The capsule's path is the
    game's (ComputeMoveShape): out over the lip in the first 70% with a hop DROP_HOP high, down onto the hang; she turns to face
    the wall by DROP_TURN, so the edge, fixed in the world, swings round her."""
    forward = ck.smoothstep(a / 0.7)
    rise = math.sin(math.pi * min(a / 0.7, 1.0))
    run = HANG_BACK + START_BACK
    y_c = -run + run * forward
    z_start = HANG_BELOW - PARAPET
    z_c = z_start + DROP_HOP * rise - z_start * a
    wrist = (HAND_HALF_GAP * (1.0 if side == "l" else -1.0), -HANG_BACK + 1.0, HANG_BELOW + WRIST_UP)
    rel = (wrist[0], wrist[1] - y_c, wrist[2] - z_c)
    phi = math.radians(180.0 * ck.smoothstep(a / DROP_TURN) - 180.0)
    x = rel[0] * math.cos(phi) - rel[1] * math.sin(phi)
    y = rel[0] * math.sin(phi) + rel[1] * math.cos(phi)
    return (_r(x), _r(y), _r(rel[2]))


def ledge_drop_to_hang(rig):
    """Over a roof edge: a crouch at the parapet, the hands on its top, hips over it as she turns round to face the wall, then
    lowered on straightening arms to the hang. The hands reach the lip while they can (an arm points at what it cannot touch)
    and take it on the way down; the pose ends on the hang's first frame."""
    hp = hang_pose(rig)
    P = hp["pelvis"]
    sh = hp["sh"]
    reach = 0.97 * (rig.upper_arm + rig.fore_arm)
    t = DROP_SECONDS
    crouch = V(0.0, 0.0, rig.idle_pelvis[2] * 0.80)
    ifl = tuple(rig.idle_foot("l"))
    ifr = tuple(rig.idle_foot("r"))

    def pelvis(time):
        a = time / t
        return _lerp(crouch, P, ck.smoothstep(a))

    def hand(side):
        def at(time):
            a = time / t
            p = pelvis(time)
            shoulder = (p[0] + (sh[0] if side == "l" else -sh[0]), p[1] + sh[1], p[2] + sh[2])
            target = _dismount_edge_local(a, side)
            # Out of reach the hand rests ready in front of the chest, elbow bent (not an arm flung out at the lip); it goes
            # to the lip as the lip comes within reach.
            ready = (shoulder[0] * 0.9, shoulder[1] - 0.45 * reach, shoulder[2] - 0.55 * reach)
            dist = math.sqrt(sum((t_ - s_) ** 2 for t_, s_ in zip(target, shoulder)))
            w = ck.smoothstep((1.12 * reach - dist) / (0.25 * reach))
            return _lerp(ready, _reach_limited(shoulder, target, reach), w)
        return at

    def foot(base, hang_foot):
        def at(time):
            a = time / t
            tuck = math.sin(math.pi * min(a / 0.7, 1.0))
            p = pelvis(time)
            rel_z = base[2] - rig.idle_pelvis[2]
            free = V(base[0] * 0.9, p[1] + (base[1] - rig.idle_pelvis[1]) - 6.0 * tuck, p[2] + rel_z * (1.0 - 0.30 * tuck))
            return _lerp(free, hang_foot, ck.smoothstep((a - 0.7) / 0.3))
        return at

    tracks = {
        "pelvis_pos": T(dense(pelvis, 0.0, t)),
        "pelvis_rot": T([(0.0, V(14, 0, 0)), (0.17, V(22, 0, 0)), (0.30, V(8, 0, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(22, 0, 0)), (0.17, V(16, 0, 0)), (0.30, V(2, 0, 0)), (t, V(-4, 0, 0))]),
        "neck": T([(0.0, V(-6, 0, 0)), (0.25, V(-18, 0, 0)), (t, V(-18, 0, 0))]),
        "hand_l": T(_smoothed(dense(hand("l"), 0.0, t), 5)),
        "hand_r": T(_smoothed(dense(hand("r"), 0.0, t), 5)),
        "pole_hand_l": _arm_pole("l"), "pole_hand_r": _arm_pole("r"),
        "foot_l": T(dense(foot(ifl, hp["foot_l"]), 0.0, t)),
        "foot_r": T(dense(foot(ifr, hp["foot_r"]), 0.0, t)),
        "fingers_l": T([(0.0, 1.2), (0.25, 1.2), (t, 1.35)]),
        "fingers_r": T([(0.0, 1.2), (0.25, 1.2), (t, 1.35)]),
    }
    return ck.Clip("Ledge_DropToHang", t, tracks, strip_times=[0.0, 0.08, 0.16, 0.24, 0.32, 0.40, t], torso_clear=False,
                   extra={"floor_cm": LEDGE_FLOOR, "span": (-130.0, 130.0), "pop_degrees": 56.0})


def _climb_edge_local(a, side, push):
    """The wrist point for a hand, in her own frame (rig cm) at a (0..1) of the climb up: the capsule rises first (smoothstep
    over 0.6) and then goes forward onto the top (smoothstep from 0.35), so the ledge sinks past her and then comes back
    behind her; `push` cm back onto the top, away from the face (ahead, -Y)."""
    up = ck.smoothstep(a / 0.6)
    fwd = ck.smoothstep((a - 0.35) / 0.65)
    run = HANG_BACK + CLIMB_FORWARD
    y_c = -run * fwd
    z_c = HANG_BELOW * up
    x = HAND_HALF_GAP * (1.0 if side == "l" else -1.0)
    return (_r(x), _r(-HANG_BACK + 1.0 - push - y_c), _r(HANG_BELOW + WRIST_UP - z_c))


def ledge_climb_up(rig):
    """From the hang: pull (the arms bend, the chest comes to the edge), the chest over it with the palms pressing back onto
    the top, a knee comes up, she stands (the last quarter second cross-fades into the idle)."""
    hp = hang_pose(rig)
    P = hp["pelvis"]
    sh = hp["sh"]
    t = CLIMB_SECONDS
    vert0 = hp["wrist_z"] - P[2] - sh[2]
    ifl = tuple(rig.idle_foot("l"))
    ifr = tuple(rig.idle_foot("r"))

    def push_at(time):
        return PUSH_BACK * ck.smoothstep((time / t - 0.38) / 0.24)

    def hand(side):
        return lambda time: _climb_edge_local(time / t, side, push_at(time))

    def pelvis(time):
        a = time / t
        l = _climb_edge_local(a, "l", push_at(time))
        r = _climb_edge_local(a, "r", push_at(time))
        mid = tuple((x + y) * 0.5 for x, y in zip(l, r))
        # How far under the hands the shoulders hang: the hang's own at the start, bent to a few cm as the chest comes up.
        vert = (1.0 - ck.smoothstep(a / 0.5)) * (vert0 - 8.0) + 8.0
        lean = ck.smoothstep((a - 0.2) / 0.4) * (1.0 - ck.smoothstep((a - 0.7) / 0.3))
        shoulder = (mid[0], mid[1] + _r(SHOULDER_OUT - 1.0) * (1.0 - lean) + 4.0 * lean, mid[2] - vert)
        y = shoulder[1] - sh[1]
        # The chest comes to the edge and over it with the hands pressing on the top: the hips are still under the top
        # (the hands are at the top's level, the shoulders above them), then she pushes up to stand. The shoulders are
        # never lower than the hands once the chest is over the edge.
        edge_z = mid[2] + _r(26.0) - sh[2]
        z = shoulder[2] - sh[2]
        z = _lerp3(z, edge_z, ck.smoothstep((a - 0.35) / 0.3))
        stand = ck.smoothstep((a - 0.5) / 0.5)
        z = z + (rig.idle_pelvis[2] - z) * stand
        y = y + (rig.idle_pelvis[1] - y) * stand
        return (0.0, y, z)

    def foot(base, hang_foot, side):
        def at(time):
            a = time / t
            p = pelvis(time)
            rel_z = base[2] - rig.idle_pelvis[2]
            hang = _lerp(hang_foot, V(base[0] * 0.85, p[1] + 6.0, p[2] + rel_z * 0.80), ck.smoothstep(a / 0.5))
            if side == "r":
                # The right knee comes up onto the top: the foot on the edge, a little in.
                e = _climb_edge_local(a, "r", 0.0)
                knee = (-_r(HAND_HALF_GAP) * 0.6, e[1] - _r(6.0), e[2] + _r(4.0))
                return _lerp(hang, knee, ck.smoothstep((a - 0.55) / 0.2) * (1.0 - ck.smoothstep((a - 0.84) / 0.16)))
            return hang
        return at

    tracks = {
        "pelvis_pos": T(_smoothed(dense(pelvis, 0.0, t), 3)),
        "pelvis_rot": T([(0.0, V(0, 0, 0)), (0.38, V(10, 0, 0)), (0.62, V(30, 0, 0)), (0.84, V(14, 0, 0)), (t, V(0, 0, 0))]),
        "chest": T([(0.0, V(-4, 0, 0)), (0.38, V(10, 0, 0)), (0.62, V(34, 0, 0)), (0.84, V(10, 0, 0)), (t, V(0, 0, 0))]),
        "neck": T([(0.0, V(-18, 0, 0)), (0.5, V(-4, 0, 0)), (t, V(0, 0, 0))]),
        "hand_l": T(_smoothed(dense(hand("l"), 0.0, 0.72)) + [(t, tuple(rig.idle_hand("l")))]),
        "hand_r": T(_smoothed(dense(hand("r"), 0.0, 0.72)) + [(t, tuple(rig.idle_hand("r")))]),
        "pole_hand_l": _arm_pole("l", -0.1), "pole_hand_r": _arm_pole("r", -0.1),
        "foot_l": T(dense(foot(ifl, hp["foot_l"], "l"), 0.0, 0.84) + [(t, ifl)]),
        "foot_r": T(dense(foot(ifr, hp["foot_r"], "r"), 0.0, 0.84) + [(t, ifr)]),
        "fingers_l": T([(0.0, 1.35), (0.38, 1.2), (0.84, 1.0), (t, 1.0)]),
        "fingers_r": T([(0.0, 1.35), (0.38, 1.2), (0.84, 1.0), (t, 1.0)]),
    }
    return ck.Clip("Ledge_ClimbUp", t, tracks, fade_out=0.25, strip_times=[0.0, 0.2, 0.38, 0.5, 0.62, 0.73, 0.84, t],
                   torso_clear=False, extra={"floor_cm": -90.0, "span": (-130.0, 130.0), "pop_degrees": 55.0, "pelvis_cm": 14.0})


VAULT_LIP_AHEAD = 110.0   # the lip's front edge ahead of where a vault starts, game cm (a probe's reach)
VAULT_RUN = 170.0         # the capsule's travel start to end
VAULT_TOP = 90.0          # the top's height over the floor at the start
VAULT_RISE = VAULT_TOP + 10.0   # the game's path (ComputeMoveShape: Rise = Height + 10): the capsule's bottom just clears the top
VAULT_LAND_UP = 50.0      # the floor beyond is about this much higher than where she started (the path's left-over rise)
VAULT_ABOVE = 27.0        # how far over the top her hips go: the body is lower than the capsule, which passes the parapet at VAULT_RISE


def _vault(rig, name, crouch):
    """A speed vault over a 90 cm top (claude-docs/animation.md, "Ledge clips"): she runs at it, the right hand plants on the
    top ahead while the torso leans over it, the legs swing together to the left beside the hips, the hand lets go and she
    lands running (the stride's lead foot first) or, at a slow entry, in a crouch. Keyed in the capsule's frame, with the capsule
    on the game's path (forward linearly VAULT_RUN, up VAULT_RISE * sin(pi a) plus the left-over rise): the capsule's bottom
    clears the parapet, the body is far under the capsule over the top (hips VAULT_ABOVE over the top, hands on it), so the keys
    are made in world heights and taken back into the frame by the capsule's height."""
    t = 0.6
    reach = 0.97 * (rig.upper_arm + rig.fore_arm)
    P = tuple(rig.idle_pelvis)
    ifl = tuple(rig.idle_foot("l"))
    ifr = tuple(rig.idle_foot("r"))
    ihl = tuple(rig.idle_hand("l"))
    ihr = tuple(rig.idle_hand("r"))
    ss = ck.smoothstep
    times = [i / ck.FPS for i in range(int(round(t * ck.FPS)) + 1)]
    top_z = _r(VAULT_TOP)

    def clip_a(time):
        return time / t

    def zb(a):
        """The capsule's bottom over its start floor at a (rig cm)."""
        return _r(VAULT_RISE) * math.sin(math.pi * a) + _r(VAULT_LAND_UP) * a

    def floor_under(a):
        """The floor she is over: the start's until the lip, the roof's beyond."""
        return _r(VAULT_LAND_UP) * ss((a - 0.55) / 0.45)

    def pitch(a):
        v = 12.0 + 30.0 * ss((a - 0.1) / 0.3) - 30.0 * ss((a - 0.55) / 0.3)
        if crouch:
            v = max(v, 12.0 + 18.0 * ss((a - 0.7) / 0.3))
        return v

    def pelvis(time):
        a = clip_a(time)
        bump = ss((a - 0.04) / 0.36) * (1.0 - ss((a - 0.58) / 0.34))
        land = ss((a - 0.75) / 0.25)
        world = floor_under(a) + P[2] - 4.0 + (top_z + _r(VAULT_ABOVE) - (P[2] - 4.0)) * bump
        if crouch:
            world -= _r(26.0) * land
        return (6.0 * bump, P[1] - _r(9.0) * bump, world - zb(a))

    def body_rot(time):
        x = time
        return (V(0.4 * pitch(x / t), 0.0, 22.0 * ss((x / t - 0.15) / 0.25) * (1.0 - ss((x / t - 0.6) / 0.25))),
                V(0.6 * pitch(x / t), 0.0, 0.0))

    def shoulder(time, side="r"):
        """Her shoulder joint where the torso really is (the rig solved without its arms)."""
        pr, ch = body_rot(time)
        solved = rig.solve({"pelvis_pos": pelvis(time), "pelvis_rot": pr, "chest": ch})
        return tuple(solved["upperarm_" + side].translation)

    def hand_r(time):
        a = clip_a(time)
        y_c = -_r(VAULT_RUN) * a
        top = (-_r(10.0), -_r(VAULT_LIP_AHEAD + 15.0) - y_c, top_z + _r(2.0) - zb(a))
        held = _reach_limited(shoulder(time), top, reach)
        sr = shoulder(time)
        start_off = (-4.0, -0.3 * reach, -0.8 * reach)
        end_off = (-12.0, -0.55 * reach, -0.45 * reach) if crouch else (-0.1 * reach, 0.2 * reach, -0.8 * reach)
        off = _lerp(start_off, end_off, ss((a - 0.55) / 0.3))
        free = (sr[0] + off[0], sr[1] + off[1], sr[2] + off[2])
        w = ss((a - 0.12) / 0.13) * (1.0 - ss((a - 0.5) / 0.14))
        return _lerp(free, held, w)

    def hand_l(time):
        a = clip_a(time)
        sl = shoulder(time, "l")
        start = (sl[0] + 4.0, sl[1] + 0.15 * reach, sl[2] - 0.85 * reach)
        out = (sl[0] + 0.18 * reach, sl[1] - 0.3 * reach, sl[2] - 0.6 * reach)   # tucked in front: an arm out to the side read as flung
        v = _lerp(start, out, ss((a - 0.04) / 0.4))
        if crouch:
            end = (sl[0] + 12.0, sl[1] - 0.55 * reach, sl[2] - 0.45 * reach)
        else:
            end = (sl[0] + 0.1 * reach, sl[1] - 0.45 * reach, sl[2] - 0.7 * reach)
        return _lerp(v, end, ss((a - 0.5) / 0.35))

    def foot(side):
        base = ifl if side == "l" else ifr

        def at(time):
            a = clip_a(time)
            lead = (side == "l")
            # the stride into the vault; then the legs swing together to the left, the feet over the top at hip height
            run0 = V(base[0] * 0.8, base[1] + (-14.0 if not lead else 16.0), base[2] + (0.0 if not lead else 10.0))
            swing_z = top_z + _r(18.0 if lead else 12.0) - zb(a)
            swing = V(_r(34.0 if lead else 24.0), base[1] + (4.0 if lead else 14.0), swing_z)
            if crouch:
                land = V(base[0] * 0.85, base[1] + (-8.0 if not lead else -2.0), base[2])
            else:
                land = V(base[0] * 0.8, base[1] + (-18.0 if not lead else 16.0), base[2] + (0.0 if not lead else 6.0))
            tuck = ss((a - 0.08) / 0.3) * (1.0 - ss((a - 0.58) / 0.3))
            v = _lerp(run0, swing, tuck)
            return _lerp(v, land, ss((a - 0.7) / 0.3))
        return at

    tracks = {
        "pelvis_pos": T(dense(pelvis, 0.0, t)),
        "pelvis_rot": T([(x, body_rot(x)[0]) for x in times]),
        "chest": T([(x, body_rot(x)[1]) for x in times]),
        "neck": T([(0.0, V(0, 0, 0)), (0.2, V(-14, 0, 0)), (0.42, V(-18, 0, 0)), (t, V(-4, 0, 0))]),
        "hand_r": T(_smoothed(dense(hand_r, 0.0, t))),
        "hand_l": T(_smoothed(dense(hand_l, 0.0, t))),
        "pole_hand_r": V(-0.5, 1.0, 0.0),
        "pole_hand_l": V(0.5, 1.0, 0.0),
        "foot_l": T(dense(foot("l"), 0.0, t)),
        "foot_r": T(dense(foot("r"), 0.0, t)),
        "foot_l_rot": T([(0.0, V(0, 0, 0)), (0.2, V(25, 0, 0)), (0.4, V(25, 0, 0)), (t, V(0, 0, 0))]),
        "foot_r_rot": T([(0.0, V(0, 0, 0)), (0.2, V(25, 0, 0)), (0.4, V(25, 0, 0)), (t, V(0, 0, 0))]),
        "fingers_r": T([(0.0, 1.0), (0.12, 1.0), (0.25, 0.7), (0.5, 0.7), (0.6, 1.0)]),
    }
    return ck.Clip(name, t, tracks, fade_in=0.05, fade_out=0.15, strip_times=[0.0, 0.1, 0.2, 0.3, 0.4, 0.5, t],
                   torso_clear=False, extra={"floor_cm": -110.0, "span": (-140.0, 140.0), "pop_degrees": 75.0, "pelvis_cm": 32.0})


def ledge_vault_plant(rig):
    """The vault at a run: see _vault."""
    return _vault(rig, "Vault_Plant", False)


def ledge_vault_crouch(rig):
    """The vault at a slow entry: the same plant and swing, landing in a crouch with both hands ready."""
    return _vault(rig, "Vault_Crouch", True)


LEDGE = [ledge_hang_idle, ledge_catch, ledge_hang_drop, ledge_fall_loop, ledge_fall_brace, ledge_drop_to_hang, ledge_climb_up, ledge_vault_plant, ledge_vault_crouch]

TAKEDOWN_BONES = ("clavicle_l", "clavicle_r", "neck_01", "spine_02", "spine_03", "pelvis", "head")

CLIPS = {
    "ue4": [getup_back, getup_front] + HITS + ZONE_HITS + [takedown_victim],
    "uefn": list(HITS) + ZONE_HITS + STRIKES + BOW + [takedown_attacker] + LEDGE,
}


# --------------------------------------------------------------------------------------------------
# the run
# --------------------------------------------------------------------------------------------------


def main():
    args = parse_args()
    failures = []
    rigs_dir = os.path.join(args.assets_root, "built", "clips", "rigs")
    os.makedirs(args.renders, exist_ok=True)
    for kind, makers in CLIPS.items():
        makers = [m for m in makers if not args.only or m.__name__ in args.only]
        if not makers:
            continue
        idle = os.path.join(rigs_dir, RIG_FILES[kind])
        if not os.path.isfile(idle):
            print("[clips] missing {0}: run Tools/Editor/export_clip_rigs.py first".format(idle))
            failures.append(idle)
            continue
        rig = ck.Rig(kind, idle)
        rig.assets_root = args.assets_root
        body = ck.build_body(rig)
        if not args.no_export:
            # The source skeleton's file: the rig at rest, the stand-in body bound to it (manifest mesh_file).
            skeleton = os.path.join(args.out, kind, "Skeleton.fbx")
            ck.export_skeleton_fbx(rig, body, skeleton)
            print("[clips] exported " + skeleton)
        report = {}
        out_dir = os.path.join(args.out, kind)
        json_path = os.path.join(out_dir, "clips.json")
        if os.path.isfile(json_path):
            with open(json_path, encoding="utf-8") as f:
                report = json.load(f)
        for make in makers:
            clip = make(rig)
            frames = ck.solve_clip(rig, clip)
            measure_contact(kind, clip, frames)
            problems, notes = ck.check_clip(rig, clip, frames)
            if clip.extra.get("pair") == "victim":
                SOLVED[clip.name] = [dict((b, tuple(pose[b].translation)) for b in TAKEDOWN_BONES) for pose in frames]
            for bone in filter(None, os.environ.get("CLIP_TRACE", "").split(",")):
                for f, pose in enumerate(frames):
                    print("[trace] {0} {1} f{2} {3}".format(clip.name, bone, f, tuple(round(v, 1) for v in pose[bone].translation)))
            ck.write_action(rig, clip, frames)
            for line in notes:
                print("[clips] {0} {1}: {2}".format(kind, clip.name, line))
            for line in problems:
                print("[clips] FAIL {0} {1}: {2}".format(kind, clip.name, line))
            if problems:
                failures.append(clip.name)
            if not args.no_render:
                png = os.path.join(args.renders, "{0}_{1}.png".format(kind, clip.name))
                ck.render_strip(rig, clip, frames, png, span=clip.extra.get("span", (-150.0, 150.0)))
                print("[clips] rendered " + png)
            if not args.no_export and not problems:
                path = os.path.join(out_dir, clip.name + ".fbx")
                ck.export_fbx(rig, body, path)
                print("[clips] exported {0} ({1} frames)".format(path, clip.frames()))
            report[clip.name] = {"seconds": clip.seconds, "frames": clip.frames(), "notes": notes,
                                 "problems": problems, "extra": clip.extra}
        os.makedirs(out_dir, exist_ok=True)
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2, sort_keys=True)
    if failures:
        raise RuntimeError("clips failed: " + ", ".join(failures))
    print("[clips] done")


main()
