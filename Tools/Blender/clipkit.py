"""Keyed-pose animation authoring on the game's driver mannequins, for build_clips.py.

A clip is a set of keyed tracks (pelvis place and turn, chest, neck, clavicles, hand and foot targets, elbow and
knee poles, toe bend, finger curl) evaluated every frame at 30 fps. Each frame is solved into bone transforms:

  * the idle pose (frame 0 of the rig's idle clip, exported from the game by Tools/Editor/export_clip_rigs.py) is
    the base every bone starts from, so fingers, twist bones and anything not keyed look like the game's idle;
  * keyed rotations are added on top in the body's own frames (the chest bends relative to the hips, the head
    relative to the chest), spread over the spine and neck bones;
  * arms and legs are two-bone IK to their targets with a pole, then the hand or foot takes its keyed turn;
  * the first and last frames cross-fade into the idle pose exactly, so a clip starts or ends on what the game
    blends from or to.

Everything is in the armature's own space: centimetres, Z up, the character facing -Y, his left +X (that is how
Blender imports the engine's FBX). Positions and turns are given relative to the idle (offsets) or absolute.

Tracks are piecewise cubic through their keys (monotone per component, so they never overshoot between keys:
overshoot and settle are keyed on purpose). Paths through via keys are curves, not straight lines.

The checks (feet planted, no joint popping, hands clear of the torso, nothing under the floor) run on the solved
frames; build_clips.py fails the clip on any of them.
"""

import math
import os

import bpy
from mathutils import Matrix, Quaternion, Vector

FPS = 30
FWD = Vector((0.0, -1.0, 0.0))
LEFT = Vector((1.0, 0.0, 0.0))
UP = Vector((0.0, 0.0, 1.0))


# --------------------------------------------------------------------------------------------------
# tracks
# --------------------------------------------------------------------------------------------------


def _pchip_slopes(ts, ys):
    n = len(ts)
    if n < 2:
        return [0.0] * n
    h = [ts[i + 1] - ts[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] if h[i] > 0 else 0.0 for i in range(n - 1)]
    m = [0.0] * n
    m[0] = d[0]
    m[-1] = d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0.0:
            m[i] = 0.0
        else:
            w1 = 2 * h[i] + h[i - 1]
            w2 = h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    # Ends: flat when the next key holds, so a clip starts and stops still.
    if n > 2:
        m[0] = 0.0 if abs(d[0]) < 1e-6 else m[0] * 0.5
        m[-1] = 0.0 if abs(d[-1]) < 1e-6 else m[-1] * 0.5
    return m


def _hermite(t, t0, t1, y0, y1, m0, m1):
    h = t1 - t0
    if h <= 0:
        return y1
    s = (t - t0) / h
    s2, s3 = s * s, s * s * s
    return ((2 * s3 - 3 * s2 + 1) * y0 + (s3 - 2 * s2 + s) * h * m0 + (-2 * s3 + 3 * s2) * y1 + (s3 - s2) * h * m1)


class Track:
    """Keys (time, value): value a number or a tuple. Smooth, monotone between keys; held past either end."""

    def __init__(self, keys):
        keys = sorted(keys, key=lambda k: k[0])
        self.ts = [float(k[0]) for k in keys]
        first = keys[0][1]
        self.dim = len(first) if isinstance(first, (tuple, list, Vector)) else 0
        self.cols = []
        for i in range(max(self.dim, 1)):
            ys = [float(k[1][i]) if self.dim else float(k[1]) for k in keys]
            self.cols.append((ys, _pchip_slopes(self.ts, ys)))

    def __call__(self, t):
        out = []
        for ys, ms in self.cols:
            if t <= self.ts[0]:
                out.append(ys[0])
                continue
            if t >= self.ts[-1]:
                out.append(ys[-1])
                continue
            i = max(j for j in range(len(self.ts) - 1) if self.ts[j] <= t)
            out.append(_hermite(t, self.ts[i], self.ts[i + 1], ys[i], ys[i + 1], ms[i], ms[i + 1]))
        return tuple(out) if self.dim else out[0]


def euler(pitch=0.0, yaw=0.0, roll=0.0):
    """Body-frame turn, degrees: pitch forward about his left, yaw to his left about up, roll to his left."""
    return (Matrix.Rotation(math.radians(yaw), 3, UP) @ Matrix.Rotation(math.radians(pitch), 3, LEFT)
            @ Matrix.Rotation(math.radians(roll), 3, Vector((0.0, 1.0, 0.0))))


def _scaled(e, f):
    return euler(e[0] * f, e[1] * f, e[2] * f)


def smoothstep(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


# --------------------------------------------------------------------------------------------------
# the rig
# --------------------------------------------------------------------------------------------------


def _rot3(m):
    return m.to_3x3().normalized()


def _with(rot3, pos):
    m = rot3.to_4x4()
    m.translation = pos
    return m


def _min_rot(a, b):
    a = a.normalized()
    b = b.normalized()
    return a.rotation_difference(b).to_matrix()


SPINE_WEIGHTS = {
    # The chest's bend shared down the spine, low to high (sums to 1).
    "ue4": (("spine_01", 0.30), ("spine_02", 0.35), ("spine_03", 0.35)),
    "uefn": (("spine_01", 0.16), ("spine_02", 0.20), ("spine_03", 0.22), ("spine_04", 0.22), ("spine_05", 0.20)),
}
NECK_WEIGHTS = {
    "ue4": (("neck_01", 0.45), ("head", 0.55)),
    "uefn": (("neck_01", 0.30), ("neck_02", 0.25), ("head", 0.45)),
}


class Rig:
    """One driver mannequin loaded from its idle FBX: rest matrices, the idle pose, the solver."""

    def __init__(self, kind, idle_fbx):
        self.kind = kind
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=idle_fbx, automatic_bone_orientation=False)
        self.arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
        for o in list(bpy.data.objects):
            if o is not self.arm:
                bpy.data.objects.remove(o, do_unlink=True)
        self.bones = self.arm.data.bones
        self.order = [b.name for b in self.bones]  # parents first
        self.parent = {b.name: (b.parent.name if b.parent else None) for b in self.bones}
        self.rest = {b.name: b.matrix_local.copy() for b in self.bones}
        self.rest_inv = {n: m.inverted() for n, m in self.rest.items()}
        scene = bpy.context.scene
        action = self.arm.animation_data.action if self.arm.animation_data else None
        start = int(action.frame_range[0]) if action else scene.frame_start
        scene.frame_set(start)
        bpy.context.view_layer.update()
        self.idle_basis = {}
        self.idle = {}
        for pb in self.arm.pose.bones:
            q = pb.matrix_basis.to_quaternion()
            self.idle_basis[pb.name] = q.to_matrix().to_4x4()
            self.idle[pb.name] = pb.matrix.copy()
        # The idle's own pelvis carries its translation; everything else is the rest offsets.
        self.idle_pelvis = self.idle["pelvis"].translation.copy()
        if self.arm.animation_data:
            self.arm.animation_data.action = None
        for pb in self.arm.pose.bones:
            pb.rotation_mode = "QUATERNION"
            pb.matrix_basis = Matrix.Identity(4)
        self.spine = SPINE_WEIGHTS[kind]
        self.neck = NECK_WEIGHTS[kind]
        self.chest_bone = self.spine[-1][0]
        self.children = {}
        for n, p in self.parent.items():
            self.children.setdefault(p, []).append(n)

        def seg(a, b):
            return (self.rest[b].translation - self.rest[a].translation).length

        self.upper_arm = seg("upperarm_l", "lowerarm_l")
        self.fore_arm = seg("lowerarm_l", "hand_l")
        self.thigh = seg("thigh_l", "calf_l")
        self.shin = seg("calf_l", "foot_l")
        self.idle_pos = {n: m.translation.copy() for n, m in self.idle.items()}

    # ---- measures --------------------------------------------------------------------------------

    def idle_foot(self, side):
        return self.idle_pos["foot_" + side].copy()

    def idle_hand(self, side):
        return self.idle_pos["hand_" + side].copy()

    def ankle_height(self):
        return min(self.idle_pos["foot_l"].z, self.idle_pos["foot_r"].z)

    def ball_offset(self, side):
        """The ball's place from the ankle in the idle foot's frame (armature axes)."""
        return self.idle_pos["ball_" + side] - self.idle_pos["foot_" + side]

    # ---- the solver --------------------------------------------------------------------------------

    def solve(self, p):
        """Bone matrices (armature space) for the pose p (a dict of evaluated track values)."""
        out = {}
        p = dict(p)
        # A foot can be placed by its ball (a kneeling toe, a heel coming up): pivot_<side> blends the ankle target
        # (0) with the one that puts the ball at ball_<side> (1).
        for side in ("l", "r"):
            if "ball_" + side in p:
                from_ball = self.foot_from_ball(side, p["ball_" + side], p.get("foot_" + side + "_rot", (0, 0, 0)))
                w = p.get("pivot_" + side, 1.0)
                ankle = Vector(p["foot_" + side]) if "foot_" + side in p else from_ball
                p["foot_" + side] = tuple(ankle.lerp(from_ball, w))
        pelvis_rot = euler(*p.get("pelvis_rot", (0, 0, 0)))
        chest = p.get("chest", (0, 0, 0))
        chest_total = pelvis_rot @ euler(*chest)
        neck = p.get("neck", (0, 0, 0))
        spine_w = dict(self.spine)
        neck_w = dict(self.neck)
        # Running fraction of the chest bend reached at each spine bone.
        spine_acc = {}
        acc = 0.0
        for name, w in self.spine:
            acc += w
            spine_acc[name] = acc
        ik = {}

        def delta_at(name):
            if name == "pelvis":
                return pelvis_rot
            if name in spine_w:
                prev = spine_acc[name] - spine_w[name]
                d = pelvis_rot @ _scaled(chest, spine_acc[name]) @ _scaled(chest, prev).inverted() @ pelvis_rot.inverted()
                return d
            if name in neck_w:
                frac = sum(w for n, w in self.neck if n == name)
                return chest_total @ _scaled(neck, frac) @ chest_total.inverted()
            for side in ("l", "r"):
                if name == "clavicle_" + side and ("clav_" + side) in p:
                    return chest_total @ euler(*p["clav_" + side]) @ chest_total.inverted()
            return None

        for name in self.order:
            parent = self.parent[name]
            if name in ik:
                out[name] = ik[name]
                continue
            if parent is None:
                base = self.rest[name] @ self.idle_basis[name]
                if name == "pelvis":
                    pos = Vector(p["pelvis_pos"]) if "pelvis_pos" in p else self.idle_pelvis.copy()
                    rot = pelvis_rot @ _rot3(self.idle["pelvis"])
                    out[name] = _with(rot, pos)
                    continue
                out[name] = base
                continue
            local = self.rest_inv[parent] @ self.rest[name] @ self._basis(name, p)
            m = out[parent] @ local
            d = delta_at(name)
            if d is not None:
                m = _with(d @ _rot3(m), m.translation)
            out[name] = m
            # Limbs: when the upper bone is reached, the whole chain is solved and stored.
            for side in ("l", "r"):
                if name == "upperarm_" + side and ("hand_" + side) in p:
                    self._limb(out, ik, p, side, "upperarm", "lowerarm", "hand", chest_total, arm=True)
                    out[name] = ik.pop(name)
                if name == "thigh_" + side and ("foot_" + side) in p:
                    self._limb(out, ik, p, side, "thigh", "calf", "foot", pelvis_rot, arm=False)
                    out[name] = ik.pop(name)
            if name in ("ball_l", "ball_r"):
                side = name[-1]
                bend = p.get("toe_" + side, 0.0)
                if bend:
                    foot = out["foot_" + side]
                    axis = _rot3(foot) @ (_rot3(self.idle["foot_" + side]).inverted() @ LEFT)
                    r = Matrix.Rotation(math.radians(bend), 3, axis)
                    out[name] = _with(r @ _rot3(out[name]), out[name].translation)
        return out

    def _basis(self, name, p):
        b = self.idle_basis[name]
        # Finger curl: the idle's finger turn scaled (1 is the idle, 2 a fist), or opened toward the rest pose.
        for side in ("l", "r"):
            key = "fingers_" + side
            if key in p and name.endswith("_" + side) and any(name.startswith(f) for f in
                                                              ("index", "middle", "ring", "pinky", "thumb")):
                f = p[key]
                q = b.to_quaternion()
                if f <= 1.0:
                    q = Quaternion().slerp(q, max(f, 0.0))
                else:
                    angle = q.angle * f
                    q = Quaternion(q.axis, angle)
                return q.to_matrix().to_4x4()
        return b

    def _limb(self, out, ik, p, side, upper, lower, end, frame, arm):
        u, lo, e = upper + "_" + side, lower + "_" + side, end + "_" + side
        mu = out[u]
        start = mu.translation.copy()
        a = (self.rest[lo].translation - self.rest[u].translation).length
        b = (self.rest[e].translation - self.rest[lo].translation).length
        target = Vector(p[e])
        # FK chain as it stands (inherited), to measure the bone directions and the bend plane.
        m_lo = mu @ (self.rest_inv[u] @ self.rest[lo] @ self._basis(lo, p))
        m_e = m_lo @ (self.rest_inv[lo] @ self.rest[e] @ self._basis(e, p))
        v_u = (m_lo.translation - start).normalized()
        v_l = (m_e.translation - m_lo.translation).normalized()
        default_pole = (frame @ (FWD if not arm else Vector((0.0, 0.3, -1.0)))).normalized()
        if not arm:
            # Knees forward and a touch out.
            out_dir = LEFT if side == "l" else -LEFT
            default_pole = (frame @ (FWD + 0.15 * out_dir)).normalized()
        else:
            out_dir = LEFT if side == "l" else -LEFT
            default_pole = (frame @ (Vector((0.0, 0.35, -1.0)) + 0.6 * out_dir)).normalized()
        pole = Vector(p["pole_" + e]).normalized() if ("pole_" + e) in p else default_pole
        to = target - start
        dist = max(min(to.length, a + b - 0.3), abs(a - b) + 0.5)
        dirn = to.normalized()
        tgt = start + dirn * dist
        cos_a = (a * a + dist * dist - b * b) / (2 * a * dist)
        cos_a = min(max(cos_a, -1.0), 1.0)
        along = a * cos_a
        h = a * math.sqrt(max(0.0, 1 - cos_a * cos_a))
        perp = pole - dirn * pole.dot(dirn)
        if perp.length < 1e-4:
            perp = default_pole - dirn * default_pole.dot(dirn)
        perp.normalize()
        joint = start + dirn * along + perp * h
        u_t = (joint - start).normalized()
        l_t = (tgt - joint).normalized()
        n_cur = v_u.cross(v_l)
        if n_cur.length < 0.15:
            fk_pole = (frame @ FWD) if not arm else default_pole
            n_cur = v_u.cross(-fk_pole)
        n_cur.normalize()
        n_t = u_t.cross(l_t).normalized()
        r1 = _min_rot(v_u, u_t)
        n1 = (r1 @ n_cur).normalized()
        # Twist about the upper bone so the bend plane turns onto the target's.
        n1p = (n1 - u_t * n1.dot(u_t)).normalized()
        ang = math.atan2(u_t.dot(n1p.cross(n_t)), n1p.dot(n_t))
        r_upper = Matrix.Rotation(ang, 3, u_t) @ r1
        m_u = _with(r_upper @ _rot3(mu), start)
        m_lo = m_u @ (self.rest_inv[u] @ self.rest[lo] @ self._basis(lo, p))
        cur_l = (m_lo @ (self.rest_inv[lo] @ self.rest[e] @ self._basis(e, p))).translation - m_lo.translation
        m_lo = _with(_min_rot(cur_l, l_t) @ _rot3(m_lo), m_lo.translation)
        end_pos = m_lo @ (self.rest_inv[lo] @ self.rest[e]).translation
        # The hand or foot: its keyed turn on the idle's (in the chest's frame for a hand, the world for a foot),
        # or following the forearm or shin ("follow").
        rot_key = e + "_rot"
        space = p.get(e + "_space", "chest" if arm else "world")
        if space == "follow":
            m_e = m_lo @ (self.rest_inv[lo] @ self.rest[e] @ self._basis(e, p))
            rot = euler(*p.get(rot_key, (0, 0, 0)))
            m_e = _with(_rot3(m_e) @ rot, end_pos)
        else:
            base = _rot3(self.idle[e])
            spin = frame if (space == "chest") else Matrix.Identity(3)
            m_e = _with(spin @ euler(*p.get(rot_key, (0, 0, 0))) @ base, end_pos)
        ik[u] = m_u
        ik[lo] = m_lo
        ik[e] = m_e

    def foot_from_ball(self, side, ball, rot_euler):
        """The ankle target that puts the ball at ball with the foot turned by rot_euler (world, on the idle's)."""
        r = euler(*rot_euler)
        return Vector(ball) - r @ self.ball_offset(side)

    def ball_of(self, side, ankle, rot_euler):
        return Vector(ankle) + euler(*rot_euler) @ self.ball_offset(side)


# --------------------------------------------------------------------------------------------------
# clips
# --------------------------------------------------------------------------------------------------


class Clip:
    """name, seconds, tracks {param: Track}, fade_in/fade_out (s: cross-fade with the idle at either end),
    planted [(side, t0, t1, 'flat' | 'ball')], optional start_offset (cm, the whole pose moved at the start)."""

    def __init__(self, name, seconds, tracks, fade_in=0.0, fade_out=0.0, planted=(), notes="", strip_times=None,
                 floor_ok=(), torso_clear=True, extra=None):
        self.name = name
        self.seconds = seconds
        self.tracks = tracks
        self.fade_in = fade_in
        self.fade_out = fade_out
        self.planted = list(planted)
        self.notes = notes
        self.strip_times = strip_times
        self.floor_ok = floor_ok
        self.torso_clear = torso_clear
        self.extra = extra or {}

    def frames(self):
        return int(round(self.seconds * FPS)) + 1

    def params(self, t):
        return dict((k, tr(t) if callable(tr) else tr) for k, tr in self.tracks.items())


def _blend(ma, mb, w):
    qa = ma.to_quaternion()
    qb = mb.to_quaternion()
    if qa.dot(qb) < 0:
        qb.negate()
    q = qa.slerp(qb, w)
    pos = ma.translation.lerp(mb.translation, w)
    return _with(q.to_matrix(), pos)


def solve_clip(rig, clip):
    """[frame -> {bone: matrix}] with the idle cross-fades at either end."""
    frames = []
    n = clip.frames()
    for f in range(n):
        t = f / FPS
        pose = rig.solve(clip.params(t))
        w = 0.0
        if clip.fade_in > 0 and t < clip.fade_in:
            w = 1.0 - smoothstep(t / clip.fade_in)
        if clip.fade_out > 0 and t > clip.seconds - clip.fade_out:
            w = max(w, smoothstep((t - (clip.seconds - clip.fade_out)) / clip.fade_out))
        if w > 0:
            pose = dict((name, _blend(m, rig.idle[name], w)) for name, m in pose.items())
        frames.append(pose)
    return frames


def write_action(rig, clip, frames):
    arm = rig.arm
    if arm.animation_data is None:
        arm.animation_data_create()
    action = bpy.data.actions.new(clip.name)
    arm.animation_data.action = action
    last = {}
    for f, pose in enumerate(frames):
        for name in rig.order:
            pb = arm.pose.bones[name]
            parent = rig.parent[name]
            if parent is None:
                basis = rig.rest_inv[name] @ pose[name]
            else:
                basis = (pose[parent] @ rig.rest_inv[parent] @ rig.rest[name]).inverted() @ pose[name]
            loc, q, _scale = basis.decompose()
            if name in last and last[name].dot(q) < 0:
                q.negate()
            last[name] = q
            pb.location = loc
            pb.rotation_quaternion = q
            pb.scale = (1.0, 1.0, 1.0)
            pb.keyframe_insert("location", frame=f)
            pb.keyframe_insert("rotation_quaternion", frame=f)
            pb.keyframe_insert("scale", frame=f)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 0
    scene.frame_end = len(frames) - 1
    return action


# --------------------------------------------------------------------------------------------------
# checks
# --------------------------------------------------------------------------------------------------


TORSO_RADIUS = 13.0      # cm: a wrist closer than this to the spine's line is inside the chest or belly
PLANT_TOLERANCE = 0.6    # cm: a planted ankle or ball may wander this much
POP_DEGREES = 24.0       # per frame at 30 fps (720 deg/s): faster than any limb should turn
FLOOR = -1.5             # cm: nothing below the floor (bones are joints, so the flesh is a little lower still)


def _seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.dot(ab), 1e-6)))
    return (a + ab * t - p).length


def check_clip(rig, clip, frames):
    """Problems as text lines (empty: the clip passes), and the measures for the log."""
    problems = []
    notes = []
    # Feet planted: the ankle (flat) or the ball (ball) stays where it was put down.
    for side, t0, t1, mode in clip.planted:
        f0, f1 = int(round(t0 * FPS)), int(round(t1 * FPS))
        bone = "foot_" + side if mode == "flat" else "ball_" + side
        ref = frames[f0][bone].translation
        span = range(f0, min(f1, len(frames) - 1) + 1)
        drift = [((frames[f][bone].translation - ref).length, f) for f in span]
        if mode == "flat":
            ball = frames[f0]["ball_" + side].translation
            drift += [((frames[f]["ball_" + side].translation - ball).length, f) for f in span]
        worst, at = max(drift)
        notes.append("{0} {1} planted {2:.2f}-{3:.2f} s: drift {4:.2f} cm (worst at frame {5})".format(
            side, mode, t0, t1, worst, at))
        if worst > PLANT_TOLERANCE:
            problems.append("{0} foot slides {1:.1f} cm while planted ({2:.2f}-{3:.2f} s)".format(side, worst, t0, t1))
    # Popping: no bone turns or the pelvis moves faster than a body can.
    watch = [n for n in rig.order if n.split("_")[0] in ("pelvis", "spine", "neck", "head", "clavicle", "upperarm",
                                                          "lowerarm", "hand", "thigh", "calf", "foot", "ball")
             and "twist" not in n]
    worst_turn = (0.0, "", 0)
    worst_move = 0.0
    for f in range(1, len(frames)):
        for n in watch:
            qa = frames[f - 1][n].to_quaternion()
            qb = frames[f][n].to_quaternion()
            ang = math.degrees(qa.rotation_difference(qb).angle)
            ang = min(ang, 360 - ang)
            if ang > worst_turn[0]:
                worst_turn = (ang, n, f)
        worst_move = max(worst_move, (frames[f]["pelvis"].translation - frames[f - 1]["pelvis"].translation).length)
    notes.append("fastest turn {0:.1f} deg/frame ({1}, frame {2}); pelvis {3:.1f} cm/frame".format(
        worst_turn[0], worst_turn[1], worst_turn[2], worst_move))
    if worst_turn[0] > POP_DEGREES:
        problems.append("{0} pops {1:.0f} deg in one frame (frame {2})".format(worst_turn[1], worst_turn[0], worst_turn[2]))
    if worst_move > 12.0:
        problems.append("the pelvis jumps {0:.0f} cm in one frame".format(worst_move))
    # Hands clear of the torso.
    if clip.torso_clear:
        closest = (1e9, "", 0)
        for f, pose in enumerate(frames):
            a = pose["pelvis"].translation
            b = pose[rig.chest_bone].translation
            for side in ("l", "r"):
                d = _seg_dist(pose["hand_" + side].translation, a, b)
                if d < closest[0]:
                    closest = (d, side, f)
        notes.append("hands at least {0:.1f} cm off the spine ({1}, frame {2})".format(*closest))
        if closest[0] < TORSO_RADIUS:
            problems.append("hand_{0} {1:.0f} cm off the spine at frame {2}: inside the body".format(
                closest[1], closest[0], closest[2]))
    # Floor.
    low = (1e9, "", 0)
    for f, pose in enumerate(frames):
        for n in watch:
            z = pose[n].translation.z
            if z < low[0]:
                low = (z, n, f)
    notes.append("lowest joint {0:.1f} cm ({1}, frame {2})".format(*low))
    if low[0] < FLOOR:
        problems.append("{0} {1:.1f} cm under the floor at frame {2}".format(low[1], -low[0], low[2]))
    return problems, notes


# --------------------------------------------------------------------------------------------------
# the stand-in body (rigid capsules per bone), for the renders and as the FBX's skinned mesh
# --------------------------------------------------------------------------------------------------


_RADII = {"pelvis": 13, "spine": 12, "neck": 5, "head": 10, "clavicle": 4, "upperarm": 5, "lowerarm": 4, "hand": 3.5,
          "thigh": 7.5, "calf": 5.5, "foot": 4, "ball": 3.5}


def _bone_end(rig, name):
    kids = [k for k in rig.children.get(name, []) if "twist" not in k and not k.startswith("ik_")
            and not k.startswith(("index", "middle", "ring", "pinky", "thumb", "weapon", "clavicle", "thigh"))]
    head = rig.rest[name].translation
    if name == "head":
        return head + Vector((0.0, -4.0, 20.0))
    if name.startswith("hand"):
        mid = [k for k in rig.children.get(name, []) if k.startswith("middle_01") or k.startswith("middle_metacarpal")]
        if mid:
            return head + (rig.rest[mid[0]].translation - head) * 1.6
    if name.startswith("ball"):
        return head + FWD * 8.0
    if name.startswith("pelvis"):
        return head + UP * 10.0
    if kids:
        return rig.rest[kids[0]].translation
    return head + (rig.bones[name].tail_local - rig.bones[name].head_local)


def build_body(rig, name="Body"):
    """A mesh of tapered cylinders, one per main bone, each weighted wholly to its bone: the render's body and the
    skinned mesh the FBX carries (the import defines the source skeleton from it)."""
    import bmesh
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    bm = bmesh.new()
    groups = []
    seg = 10
    for bone in rig.order:
        fam = bone.split("_")[0]
        if fam not in _RADII or "twist" in bone or bone.startswith(("ik_", "props", "prop_", "poi", "attach")):
            continue
        a = rig.rest[bone].translation
        b = _bone_end(rig, bone)
        axis = (b - a)
        if axis.length < 1.0:
            continue
        r = _RADII[fam]
        z = axis.normalized()
        x = z.orthogonal().normalized()
        y = z.cross(x)
        rings = []
        for end, scale in ((a, 1.0), (b, 0.8)):
            ring = []
            for i in range(seg):
                ang = 2 * math.pi * i / seg
                ring.append(bm.verts.new(end + (x * math.cos(ang) + y * math.sin(ang)) * r * scale))
            rings.append(ring)
        for i in range(seg):
            j = (i + 1) % seg
            bm.faces.new((rings[0][i], rings[0][j], rings[1][j], rings[1][i]))
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[1])
        groups.append((bone, [v for ring in rings for v in ring]))
    bm.verts.index_update()
    index_groups = [(bone, [v.index for v in verts]) for bone, verts in groups]
    bm.to_mesh(mesh)
    bm.free()
    # The mesh's vertices are in the armature's space; the object sits where the armature does.
    obj.matrix_world = rig.arm.matrix_world.copy()
    for bone, idx in index_groups:
        vg = obj.vertex_groups.new(name=bone)
        vg.add(idx, 1.0, "REPLACE")
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig.arm
    return obj


# --------------------------------------------------------------------------------------------------
# renders
# --------------------------------------------------------------------------------------------------


def render_strip(rig, clip, frames, out_png, times=None, width=300, height=420, span=(-150.0, 150.0)):
    """Side-on frames at fixed times, stitched left to right into one PNG (orthographic, from his left)."""
    import numpy as np
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "SINGLE"
    scene.display.shading.single_color = (0.75, 0.62, 0.5)
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    world = bpy.data.worlds.new("W") if not scene.world else scene.world
    scene.world = world
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = "ORTHO"
    mw = rig.arm.matrix_world
    s = mw.to_scale()[0]
    cam_data.ortho_scale = (span[1] - span[0]) * s * (max(width, height) / width)
    cam = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam)
    centre_y = (span[0] + span[1]) * 0.5
    cam.location = mw @ Vector((400.0, centre_y, 95.0))
    cam.rotation_euler = (math.radians(90), 0.0, math.radians(90))
    scene.camera = cam
    # A floor line.
    bpy.ops.mesh.primitive_plane_add(size=1.0)
    floor = bpy.context.active_object
    floor.scale = (0.5, 4.0, 1.0)
    floor.location = mw @ Vector((0.0, centre_y, -0.5))
    times = times or clip.strip_times or [clip.seconds * i / 5.0 for i in range(6)]
    tiles = []
    tmp = os.path.join(os.path.dirname(out_png), "_frame.png")
    for t in times:
        f = min(int(round(t * FPS)), len(frames) - 1)
        scene.frame_set(f)
        scene.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp, check_existing=False)
        px = np.array(img.pixels[:], dtype=np.float32).reshape(img.size[1], img.size[0], 4)
        # A time mark along the top: one bar per tenth of a second.
        bars = min(int(round(t * 10)), 30)
        for k in range(bars):
            px[height - 12:height - 4, 6 + k * 8:11 + k * 8, :3] = (0.9, 0.2, 0.2)
        tiles.append(px)
        bpy.data.images.remove(img)
    strip = np.concatenate(tiles, axis=1)
    out = bpy.data.images.new("strip", width=strip.shape[1], height=strip.shape[0])
    out.pixels = strip.ravel()
    out.filepath_raw = out_png
    out.file_format = "PNG"
    out.save()
    bpy.data.images.remove(out)
    if os.path.isfile(tmp):
        os.remove(tmp)
    for o in (cam, floor):
        bpy.data.objects.remove(o, do_unlink=True)


# --------------------------------------------------------------------------------------------------
# export
# --------------------------------------------------------------------------------------------------


def export_fbx(rig, body, path):
    """The armature (its object is the root bone) and the stand-in body, the action baked at 30 fps."""
    bpy.ops.object.select_all(action="DESELECT")
    rig.arm.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = rig.arm
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
        bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", armature_nodetype="NULL",
        primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False, mesh_smooth_type="FACE")
