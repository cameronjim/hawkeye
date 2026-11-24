"""Retarget pose for arms, hands and fingers of a Quaternius body against a UE mannequin driver.

Why this exists. The engine's auto-align (CHAIN_TO_CHAIN) turns each bone by the shortest rotation that points it
along the source's chain. It never matches roll, skips the last bone of every chain (the hand, every third finger
joint) and single-bone chains (the clavicle). The Quaternius body is built in a T-pose with its palms down and straight
fingers; the mannequin's reference is an A-pose with relaxed fingers. So the body's wrists and fingers kept their
T-pose roll and the hands read as claws.

What it does. For each bone of the arm (upper arm, forearm, hand) and every finger joint it builds a frame from the
joint positions (the direction along the bone, and the palm normal for roll, which both skeletons give by the same
formula) on both skeletons, and turns the target bone by the rotation that carries its frame onto the source's. Then it
converts those global turns to the local rotation offsets a retarget pose stores (global = parent global * local ref *
offset, the engine's own convention). Plain math, no engine: ``test_handpose.py`` checks it. The engine glue
(``apply_hand_pose``) imports unreal lazily.
"""

import math

SIDES = ("l", "r")
FINGERS = ("thumb", "index", "middle", "ring", "pinky")
X_AXIS = (1.0, 0.0, 0.0)

# ----------------------------------------------------------------------------------------------------
# vectors and quaternions (x, y, z, w), Hamilton convention, like the engine's FQuat
# ----------------------------------------------------------------------------------------------------


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vscale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def unit(a):
    n = length(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-9 else (0.0, 0.0, 0.0)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qinv(q):
    n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]
    return (-q[0] / n, -q[1] / n, -q[2] / n, q[3] / n)


def qnorm(q):
    n = math.sqrt(sum(v * v for v in q))
    return tuple(v / n for v in q)


def qrot(q, v):
    u = (q[0], q[1], q[2])
    t = vscale(cross(u, v), 2.0)
    return vadd(vadd(v, vscale(t, q[3])), cross(u, t))


def q_axis_angle(axis, degrees):
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    a = unit(axis)
    return (a[0] * s, a[1] * s, a[2] * s, math.cos(half))


def q_from_columns(cx, cy, cz):
    """The rotation whose matrix has these orthonormal, right-handed columns."""
    m00, m10, m20 = cx
    m01, m11, m21 = cy
    m02, m12, m22 = cz
    tr = m00 + m11 + m22
    if tr > 0.0:
        s = math.sqrt(tr + 1.0) * 2.0
        q = ((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s)
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2.0
        q = (0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s)
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2.0
        q = ((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s)
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2.0
        q = ((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s)
    return qnorm(q)


def angle_between(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, dot(unit(a), unit(b))))))


# ----------------------------------------------------------------------------------------------------
# skeleton data: pos / rot (world, reference pose) / local_rot, all by bone name
# ----------------------------------------------------------------------------------------------------


class Skel(object):
    def __init__(self, pos, rot, local_rot=None):
        self.pos = pos
        self.rot = rot
        self.local_rot = local_rot or {}


def arm_bones(side):
    return ["upperarm_" + side, "lowerarm_" + side, "hand_" + side]


def finger_bones(side):
    return ["{0}_{1:02d}_{2}".format(f, j, side) for f in FINGERS for j in (1, 2, 3)]


def target_parent(bone):
    """Parent of an arm or finger bone on the Quaternius body (UE mannequin names)."""
    for side in SIDES:
        if bone.endswith("_" + side):
            stem = bone[:-2]
            if stem == "clavicle":
                return "spine_03"
            if stem == "upperarm":
                return "clavicle_" + side
            if stem == "lowerarm":
                return "upperarm_" + side
            if stem == "hand":
                return "lowerarm_" + side
            finger, joint = stem.rsplit("_", 1)
            if finger in FINGERS:
                return "hand_" + side if joint == "01" else "{0}_{1:02d}_{2}".format(finger, int(joint) - 1, side)
    return {"spine_03": "spine_02", "spine_02": "spine_01", "spine_01": "pelvis", "pelvis": "root"}.get(bone)


def palm_frame(skel, side):
    """(h, n, p): along the hand, the palm normal (same formula on every skeleton), and the normal pointing into the
    palm (the side the thumb's base lies on)."""
    pos = skel.pos
    hand, mid = pos["hand_" + side], pos["middle_01_" + side]
    h = unit(vsub(mid, hand))
    s = vsub(pos["index_01_" + side], pos["pinky_01_" + side])
    s = unit(vsub(s, vscale(h, dot(s, h))))
    n = unit(cross(h, s))
    knuckles = vscale(vadd(pos["index_01_" + side], pos["pinky_01_" + side]), 0.5)
    p = n if dot(n, vsub(pos["thumb_01_" + side], knuckles)) > 0.0 else vscale(n, -1.0)
    return h, n, p, s


def tip_axis_sign(skel, side):
    """+1 or -1 when a finger bone's local X axis runs along the bone (the UE skeletons' convention), else None."""
    cos = []
    for f in FINGERS:
        for j in (1, 2):
            b, child = "{0}_{1:02d}_{2}".format(f, j, side), "{0}_{1:02d}_{2}".format(f, j + 1, side)
            if b in skel.rot and child in skel.pos:
                cos.append(dot(qrot(skel.rot[b], X_AXIS), unit(vsub(skel.pos[child], skel.pos[b]))))
    if not cos:
        return None
    mean = sum(cos) / len(cos)
    return (1.0 if mean > 0 else -1.0) if abs(mean) > 0.9 and min(abs(x) for x in cos) > 0.8 else None


def directions(skel, side):
    """Bone name -> unit direction along it, for the arm and the fingers. A finger's last bone points at the tip bone
    when there is one, else carries on the bend of the joint before it."""
    pos = skel.pos
    d = {}
    d["upperarm_" + side] = unit(vsub(pos["lowerarm_" + side], pos["upperarm_" + side]))
    d["lowerarm_" + side] = unit(vsub(pos["hand_" + side], pos["lowerarm_" + side]))
    d["hand_" + side] = unit(vsub(pos["middle_01_" + side], pos["hand_" + side]))
    for f in FINGERS:
        for j in (1, 2, 3):
            b = "{0}_{1:02d}_{2}".format(f, j, side)
            child = "{0}_{1:02d}_{2}".format(f, j + 1, side)
            leaf = "{0}_04_leaf_{1}".format(f, side)
            if j < 3:
                d[b] = unit(vsub(pos[child], pos[b]))
            elif leaf in pos:
                d[b] = unit(vsub(pos[leaf], pos[b]))
            else:
                # No tip bone: the last joint carries on the bend of the one before (the last bone's own axes are
                # not reliable on every skeleton: UEFN's index_03 points back along the finger)
                first = d["{0}_01_{1}".format(f, side)]
                second = d["{0}_02_{1}".format(f, side)]
                axis = cross(first, second)
                bend = angle_between(first, second) * 0.7
                d[b] = qrot(q_axis_angle(axis, bend), second) if length(axis) > 1e-6 and bend > 0.0 else second
    return d


# A relaxed hand: per finger the bend at each of its three joints in degrees (the thumb too, a little). The
# UE4 mannequin's own rest hand holds its index and thumb hooked ("OK sign"); the bodies it drives look better with
# the fingers curling together, a little more each toward the little finger.
RELAXED_HAND = {"index": (28.0, 42.0, 26.0), "middle": (32.0, 44.0, 28.0), "ring": (36.0, 44.0, 28.0),
                "pinky": (40.0, 42.0, 26.0), "thumb": (10.0, 14.0, 10.0)}


def relaxed_directions(src, side, table):
    """Bone -> direction for the four fingers' bones with the bend the table gives (cumulative per joint, toward the palm),
    keeping each finger's own spread across the palm from the source."""
    _h, _n, p, _s = palm_frame(src, side)
    sd = directions(src, side)
    out = {}
    for finger, angles in table.items():
        total = 0.0
        for joint in (1, 2, 3):
            bone = "{0}_{1:02d}_{2}".format(finger, joint, side)
            total += angles[joint - 1]
            flat = vsub(sd[bone], vscale(p, dot(sd[bone], p)))
            flat = unit(flat) if length(flat) > 1e-6 else unit(sd["hand_" + side])
            a = math.radians(total)
            out[bone] = unit(vadd(vscale(flat, math.cos(a)), vscale(p, math.sin(a))))
    return out


def frame(direction, up, fallback):
    u = vsub(up, vscale(direction, dot(up, direction)))
    if length(u) < 0.25:
        u = vsub(fallback, vscale(direction, dot(fallback, direction)))
    u = unit(u)
    return q_from_columns(direction, cross(u, direction), u)


def desired_rotations(src, tgt, side, relaxed=None):
    """Bone -> the global rotation the target bone should have in its retarget pose to match the source's frames
    (``relaxed``: a table like RELAXED_HAND, the fingers' bend taken from it rather than from the source)."""
    sh, sn, _sp, ss = palm_frame(src, side)
    th, tn, _tp, ts = palm_frame(tgt, side)
    sd, td = directions(src, side), directions(tgt, side)
    if relaxed:
        sd.update(relaxed_directions(src, side, relaxed))
    out = {}
    for bone in arm_bones(side) + finger_bones(side):
        s_up, t_up, s_fb, t_fb = sn, tn, ss, ts
        if abs(dot(sn, sd[bone])) > 0.97:
            # the bone points along the palm normal on the source: roll from the knuckle line on both
            s_up, t_up = ss, ts
        fs = frame(sd[bone], s_up, s_fb)
        ft = frame(td[bone], t_up, t_fb)
        out[bone] = qnorm(qmul(qmul(fs, qinv(ft)), tgt.rot[bone]))
    return out


def curl_extra(src, side, per_joint):
    """Extra bend per joint (degrees) so a finger that the source holds straighter than ``per_joint`` * 3 is brought to
    it: {bone: degrees}. Fingers only; the thumb takes a third."""
    sd = directions(src, side)
    out = {}
    for f in FINGERS:
        names = ["{0}_{1:02d}_{2}".format(f, j, side) for j in (1, 2, 3)]
        total = angle_between(sd[names[0]], sd[names[1]]) + angle_between(sd[names[1]], sd[names[2]])
        want = per_joint * (0.33 if f == "thumb" else 1.0) * 2.0
        extra = max(0.0, want - total)
        for name, share in zip(names, (0.0, 0.5, 0.5)):
            out[name] = extra * share
    return out


def apply_curl(desired, tgt, side, extra):
    """Bend each finger's joints by ``extra`` degrees toward the palm, on top of the desired global rotations
    (child rotations follow their parent's change)."""
    _h, _n, p, _s = palm_frame(tgt, side)
    td = directions(tgt, side)
    out = dict(desired)
    for f in FINGERS:
        carried = (0.0, 0.0, 0.0, 1.0)
        for j in (1, 2, 3):
            b = "{0}_{1:02d}_{2}".format(f, j, side)
            deg = extra.get(b, 0.0)
            carried_dir = qrot(carried, td[b])
            # the desired rotation already turned the bone; its direction is the matched one, so bend about
            # the axis across the finger, in the palm plane
            axis = cross(carried_dir, p)
            if deg and length(axis) > 1e-6:
                bend = q_axis_angle(axis, deg)
                out[b] = qnorm(qmul(bend, out[b]))
                carried = qmul(bend, carried)
    return out


def adduct(want, tgt, side, degrees):
    """The arm chain (upper arm, forearm, hand, fingers) swung ``degrees`` toward the body's midline about the shoulder, in
    place: a body with much wider shoulders than hips (the Quaternius superhero) hangs the mannequin's arms outside its
    thighs. Only the global rotations in ``want`` change; the bones' places follow from them."""
    if not degrees:
        return want
    other = "r" if side == "l" else "l"
    lateral = vsub(tgt.pos["upperarm_" + side], tgt.pos["upperarm_" + other])
    lateral = unit((lateral[0], lateral[1], 0.0))
    inward = vscale(lateral, -1.0)
    ua = "upperarm_" + side
    turn = qmul(want[ua], qinv(tgt.rot[ua]))
    arm = qrot(turn, directions(tgt, side)[ua])
    axis = cross(arm, inward)
    if length(axis) < 1e-6:
        return want
    swing = q_axis_angle(axis, degrees)
    out = dict(want)
    for bone in arm_bones(side) + finger_bones(side):
        out[bone] = qnorm(qmul(swing, want[bone]))
    return out


def local_offsets(tgt, desired, base_offsets):
    """The local rotation offsets (bone -> quat) that give each bone in ``desired`` its global rotation, walking
    from the root down. ``base_offsets`` are the retarget pose's offsets for the bones this does not set (the
    engine's alignment of the spine). Offsets for bones it does set are returned; others are not."""
    cache = {}

    def global_rot(bone):
        if bone in cache:
            return cache[bone]
        if bone in desired:
            g = desired[bone]
        else:
            parent = target_parent(bone)
            if parent is None:
                g = tgt.rot[bone]
            else:
                g = qmul(qmul(global_rot(parent), tgt.local_rot[bone]), base_offsets.get(bone, (0.0, 0.0, 0.0, 1.0)))
        cache[bone] = g
        return g

    out = {}
    for bone in desired:
        parent_global = global_rot(target_parent(bone))
        out[bone] = qnorm(qmul(qinv(qmul(parent_global, tgt.local_rot[bone])), desired[bone]))
    return out


def solve(src, tgt, base_offsets=None, curl_degrees=0.0, sides=SIDES, relaxed=None, adduct_degrees=0.0):
    """Offsets for both arms and all fingers, plus a report: {side: {bone: angle between the target's and the source's
    bone direction after the offsets, in degrees}}."""
    base_offsets = base_offsets or {}
    offsets = {}
    report = {}
    for side in sides:
        want = adduct(desired_rotations(src, tgt, side, relaxed), tgt, side, adduct_degrees)
        if curl_degrees > 0.0:
            want = apply_curl(want, tgt, side, curl_extra(src, side, curl_degrees))
        offsets.update(local_offsets(tgt, want, base_offsets))
        sd, td = directions(src, side), directions(tgt, side)
        if relaxed:
            sd.update(relaxed_directions(src, side, relaxed))
        report[side] = {}
        for bone in want:
            # the target's direction after: its own direction carried by the global turn
            turn = qmul(want[bone], qinv(tgt.rot[bone]))
            report[side][bone] = angle_between(qrot(turn, td[bone]), sd[bone]) if not adduct_degrees else 0.0
    return offsets, report


# ----------------------------------------------------------------------------------------------------
# engine glue
# ----------------------------------------------------------------------------------------------------


def read_skeleton(mesh):
    """A Skel from a skeletal mesh's skeleton reference pose (world and local rotations, world positions)."""
    import unreal  # noqa: PLC0415

    skeleton = mesh.get_editor_property("skeleton")
    pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    names = [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(pose)]
    pos, rot, local = {}, {}, {}
    for name in names:
        w = unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)
        lo = unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.LOCAL)
        pos[name] = (w.translation.x, w.translation.y, w.translation.z)
        rot[name] = (w.rotation.x, w.rotation.y, w.rotation.z, w.rotation.w)
        local[name] = (lo.rotation.x, lo.rotation.y, lo.rotation.z, lo.rotation.w)
    return Skel(pos, rot, local)


def apply_hand_pose(ctl, source_mesh, target_mesh, curl_degrees=0.0, relaxed=None, adduct_degrees=0.0):
    """Write the arms' and fingers' offsets into the target's current retarget pose. Returns a one-line note."""
    import unreal  # noqa: PLC0415

    side = unreal.RetargetSourceOrTarget.TARGET
    src, tgt = read_skeleton(source_mesh), read_skeleton(target_mesh)
    for s in SIDES:
        for b in ("hand_", "middle_01_", "index_01_", "pinky_01_", "thumb_01_"):
            if b + s not in src.pos or b + s not in tgt.pos:
                return "FAILED: no {0}{1} on the {2}".format(b, s, "source" if b + s not in src.pos else "target")
    base = {}
    for bone in ("pelvis", "spine_01", "spine_02", "spine_03", "clavicle_l", "clavicle_r"):
        q = ctl.get_rotation_offset_for_retarget_pose_bone(bone, side)
        base[bone] = (q.x, q.y, q.z, q.w)
    offsets, report = solve(src, tgt, base, curl_degrees, relaxed=relaxed, adduct_degrees=adduct_degrees)
    for bone, q in offsets.items():
        ctl.set_rotation_offset_for_retarget_pose_bone(bone, unreal.Quat(q[0], q[1], q[2], q[3]), side)
    worst = max(max(v.values()) for v in report.values())
    hand_up = [palm_frame(tgt, s)[2] for s in SIDES]
    return "hands: {0} bones offset, worst direction miss {1:.1f} deg, target palms face (z) {2:.2f}, {3:.2f}".format(
        len(offsets), worst, hand_up[0][2], hand_up[1][2])
