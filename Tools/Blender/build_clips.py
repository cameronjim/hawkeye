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

Every clip is keyed poses solved per frame (clipkit.py): the idle is the base, the hips carry the weight, limbs are IK
to keyed targets with poles, hands and feet take keyed turns, and both ends meet the game's poses. The run fails on a
foot sliding while planted, a joint turning faster than a body can, a hand inside the torso or a joint under the floor.
claude-docs/animation.md, "Authored clips", says what plays where.
"""

import argparse
import json
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

CLIPS = {
    "ue4": [getup_back, getup_front] + HITS,
    "uefn": list(HITS) + BOW,
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
            problems, notes = ck.check_clip(rig, clip, frames)
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
