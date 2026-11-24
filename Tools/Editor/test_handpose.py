"""Unit tests for _handpose.py. Plain unittest, no editor:

    Tools/test-handpose.ps1
    (or) python Tools/Editor/test_handpose.py

A synthetic left and right arm with fingers is built twice by forward kinematics: a "target" in a T-pose whose bone
axes are turned by an arbitrary constant (so its local axes disagree with the source's, as the Quaternius body's do
against the mannequin's) and a "source" in an A-pose with a twisted wrist and curled fingers. The solver's offsets
must put the target's bones along the source's, with the same palm roll.
"""

import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _handpose as hp  # noqa: E402

IDENT = (0.0, 0.0, 0.0, 1.0)


def build(arm_dir, twist, curl, axis_fix, side, hand_roll=0.0):
    """FK of a body fragment. Bone local axes are turned by axis_fix (a constant per skeleton) so global rotations differ
    between skeletons even in the same pose. Returns (Skel, parents, local translations)."""
    mirror = 1.0 if side == "l" else -1.0
    names = ["root", "pelvis", "spine_01", "spine_02", "spine_03", "clavicle_" + side, "upperarm_" + side,
             "lowerarm_" + side, "hand_" + side]
    parent = {"pelvis": "root", "spine_01": "pelvis", "spine_02": "spine_01", "spine_03": "spine_02",
              "clavicle_" + side: "spine_03", "upperarm_" + side: "clavicle_" + side,
              "lowerarm_" + side: "upperarm_" + side, "hand_" + side: "lowerarm_" + side}
    # offsets of each bone from its parent along the *pose's* own arm axis (in the bone's rest frame, along +X)
    length = {"upperarm_" + side: 28.0, "lowerarm_" + side: 26.0, "hand_" + side: 9.0}
    # finger placement on the hand: along the hand (x), across it (y) and the thumb on the palm side (z -1 = palm)
    fingers = {"thumb": (3.0, -3.0, -2.0, 3.5), "index": (9.0, -3.0, 0.0, 3.2), "middle": (9.2, -0.4, 0.0, 3.6),
               "ring": (8.6, 1.8, 0.0, 3.2), "pinky": (7.6, 3.6, 0.0, 2.6)}
    for f in hp.FINGERS:
        for j in (1, 2, 3):
            n = "{0}_{1:02d}_{2}".format(f, j, side)
            parent[n] = "hand_" + side if j == 1 else "{0}_{1:02d}_{2}".format(f, j - 1, side)
            names.append(n)
    # world rotation of the arm in this pose: arm_dir with the palm turned by twist about it
    d = hp.unit(arm_dir)
    up0 = (0.0, 0.0, 1.0) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = hp.unit(hp.vsub(up0, hp.vscale(d, hp.dot(up0, d))))
    base = hp.q_from_columns(d, hp.cross(u, d), u)
    base = hp.qmul(hp.q_axis_angle(d, twist), base)
    pos, rot, local = {"root": (0.0, 0.0, 0.0)}, {"root": IDENT}, {"root": IDENT}
    pos["pelvis"] = (0.0, 0.0, 95.0)
    rot["pelvis"] = IDENT
    local["pelvis"] = IDENT
    for b, off in (("spine_01", 10.0), ("spine_02", 10.0), ("spine_03", 12.0)):
        pos[b] = hp.vadd(pos[parent[b]], (0.0, 0.0, off))
        rot[b] = IDENT
        local[b] = IDENT
    pos["clavicle_" + side] = hp.vadd(pos["spine_03"], (0.0, 0.0, 14.0))
    rot["clavicle_" + side] = IDENT
    local["clavicle_" + side] = IDENT
    pos["upperarm_" + side] = hp.vadd(pos["clavicle_" + side], (mirror * 17.0, 0.0, 0.0))
    # arm bones: global rotation base (bone frame x along the arm), then the constant axis fix on top
    chain = ["upperarm_" + side, "lowerarm_" + side, "hand_" + side]
    for i, b in enumerate(chain):
        rot[b] = hp.qmul(base, axis_fix)
        local[b] = hp.qmul(hp.qinv(rot[parent[b]]), rot[b])
        if i:
            pos[b] = hp.vadd(pos[parent[b]], hp.vscale(d, length[parent[b]]))
    # the hand's own frame: palm normal along base z (so a palm-down T-pose has z = -1: the thumb sits on +palm)
    hx, hy, hz = hp.qrot(base, (1, 0, 0)), hp.qrot(base, (0, 1, 0)), hp.qrot(base, (0, 0, 1))
    for f, (along, across, palm, ln) in fingers.items():
        hand_pos = pos["hand_" + side]
        prev_dir, fdir, bend_axis = hx, hx, hy
        for j in (1, 2, 3):
            n = "{0}_{1:02d}_{2}".format(f, j, side)
            if j == 1:
                at = hp.vadd(hand_pos, hp.vadd(hp.vscale(hx, along * (1.0 if f != "thumb" else 0.4)),
                                               hp.vadd(hp.vscale(hy, across * -mirror), hp.vscale(hz, palm))))
                fdir = hx if f != "thumb" else hp.unit(hp.vadd(hx, hp.vscale(hz, -0.8)))
                bend_axis = hy
            else:
                fdir = hp.qrot(hp.q_axis_angle(bend_axis, -curl), fdir)
                at = hp.vadd(pos[parent[n]], hp.vscale(prev_dir, ln))
            pos[n] = at
            prev_dir = fdir
            q = hp.q_from_columns(fdir, hp.cross(hz, fdir), hz)
            rot[n] = hp.qmul(q, axis_fix)
            local[n] = hp.qmul(hp.qinv(rot[parent[n]]), rot[n])
    return hp.Skel(pos, rot, local), parent


def fk_after(tgt, parent, offsets):
    """Global rotations and positions of the fragment with the retarget pose's offsets applied; positions of bones
    keep their parent-relative offset in the parent's frame."""
    order = [b for b in tgt.rot]
    g, p = {}, {}
    for b in order:
        if b == "root":
            g[b], p[b] = tgt.rot[b], tgt.pos[b]
            continue
        par = parent[b]
        off = offsets.get(b, IDENT)
        g[b] = hp.qmul(hp.qmul(g[par], tgt.local_rot[b]), off)
        rel = hp.qrot(hp.qinv(tgt.rot[par]), hp.vsub(tgt.pos[b], tgt.pos[par]))
        p[b] = hp.vadd(p[par], hp.qrot(g[par], rel))
    return g, p


class HandPoseTest(unittest.TestCase):
    def setUp(self):
        self.fix_target = hp.q_axis_angle((0.3, 0.8, -0.5), 77.0)
        self.side = "l"

    def pair(self, side):
        # source: A-pose arm down 40 degrees, wrist twisted 25 degrees, fingers curled 22 degrees per joint
        mirror = 1.0 if side == "l" else -1.0
        src, _ = build((mirror * math.cos(math.radians(40)), 0.0, -math.sin(math.radians(40))), 25.0, 22.0, IDENT, side)
        tgt, parent = build((mirror, 0.0, 0.0), 0.0, 0.0, self.fix_target, side)
        return src, tgt, parent

    def test_directions_match_after_solve(self):
        for side in hp.SIDES:
            src, tgt, parent = self.pair(side)
            offsets, report = hp.solve(src, tgt, sides=(side,))
            worst = max(report[side].values())
            self.assertLess(worst, 0.5, "{0}: direction error {1}".format(side, report[side]))

    def test_applied_offsets_move_the_fk_pose_onto_the_source(self):
        for side in hp.SIDES:
            src, tgt, parent = self.pair(side)
            offsets, _ = hp.solve(src, tgt, sides=(side,))
            g, p = fk_after(tgt, parent, offsets)
            sd = hp.directions(src, side)
            for bone in hp.arm_bones(side) + hp.finger_bones(side):
                kid = [k for k, v in parent.items() if v == bone]
                if not kid or bone.startswith("hand"):
                    continue
                d = hp.unit(hp.vsub(p[kid[0]], p[bone]))
                self.assertLess(hp.angle_between(d, sd[bone]), 1.0, "{0} {1}".format(side, bone))

    def test_palm_roll_matches(self):
        for side in hp.SIDES:
            src, tgt, parent = self.pair(side)
            offsets, _ = hp.solve(src, tgt, sides=(side,))
            g, _p = fk_after(tgt, parent, offsets)
            # the hand bone's rotation relative to the target's own reference must carry the target's palm normal onto the source's
            _h, n_src, _p1, _s = hp.palm_frame(src, side)
            _h, n_tgt, _p2, _s = hp.palm_frame(tgt, side)
            turn = hp.qmul(g["hand_" + side], hp.qinv(tgt.rot["hand_" + side]))
            self.assertLess(hp.angle_between(hp.qrot(turn, n_tgt), n_src), 1.0, side)

    def test_palm_points_into_the_palm(self):
        src, tgt, _ = self.pair("l")
        _h, _n, p, _s = hp.palm_frame(tgt, "l")
        self.assertLess(p[2], -0.5)

    def test_curl_brings_straight_source_fingers_up_to_the_floor(self):
        for side in hp.SIDES:
            mirror = 1.0 if side == "l" else -1.0
            src, _ = build((mirror, 0.0, -0.3), 0.0, 0.0, IDENT, side)
            tgt, parent = build((mirror, 0.0, 0.0), 0.0, 0.0, self.fix_target, side)
            offsets, _ = hp.solve(src, tgt, curl_degrees=18.0, sides=(side,))
            g, p = fk_after(tgt, parent, offsets)
            a = hp.unit(hp.vsub(p["middle_02_" + side], p["middle_01_" + side]))
            b = hp.unit(hp.vsub(p["middle_03_" + side], p["middle_02_" + side]))
            self.assertGreater(hp.angle_between(a, b), 10.0)

    def test_relaxed_table_sets_the_fingers_bend(self):
        for side in hp.SIDES:
            src, tgt, parent = self.pair(side)
            offsets, _ = hp.solve(src, tgt, sides=(side,), relaxed=hp.RELAXED_HAND)
            g, p = fk_after(tgt, parent, offsets)
            for finger, angles in hp.RELAXED_HAND.items():
                a = hp.unit(hp.vsub(p["{0}_02_{1}".format(finger, side)], p["{0}_01_{1}".format(finger, side)]))
                b = hp.unit(hp.vsub(p["{0}_03_{1}".format(finger, side)], p["{0}_02_{1}".format(finger, side)]))
                self.assertAlmostEqual(hp.angle_between(a, b), angles[1], delta=1.5, msg="{0} {1}".format(side, finger))
            # the thumb follows the table too
            _o, report = hp.solve(src, tgt, sides=(side,), relaxed=hp.RELAXED_HAND)
            self.assertLess(report[side]["thumb_02_" + side], 0.5)

    def test_adduct_brings_the_hand_toward_the_midline(self):
        side_pairs = {}
        for side in hp.SIDES:
            src, tgt, parent = self.pair(side)
            side_pairs[side] = (src, tgt, parent)
        # both arms in one skeleton: the other shoulder is what says which way is inward
        src_l, tgt_l, parent_l = side_pairs["l"]
        _src_r, tgt_r, _parent_r = side_pairs["r"]
        tgt_l.pos["upperarm_r"] = tgt_r.pos["upperarm_r"]
        want = hp.desired_rotations(src_l, tgt_l, "l")
        swung = hp.adduct(want, tgt_l, "l", 14.0)
        for bone in ("lowerarm_l", "hand_l"):
            before = hp.qrot(hp.qmul(want[bone], hp.qinv(tgt_l.rot[bone])), hp.directions(tgt_l, "l")[bone])
            after = hp.qrot(hp.qmul(swung[bone], hp.qinv(tgt_l.rot[bone])), hp.directions(tgt_l, "l")[bone])
            self.assertAlmostEqual(hp.angle_between(before, after), 14.0, delta=0.5, msg=bone)
        # toward the midline: the left arm is on +x here, so inward is -x
        before = hp.qrot(hp.qmul(want["upperarm_l"], hp.qinv(tgt_l.rot["upperarm_l"])), hp.directions(tgt_l, "l")["upperarm_l"])
        after = hp.qrot(hp.qmul(swung["upperarm_l"], hp.qinv(tgt_l.rot["upperarm_l"])), hp.directions(tgt_l, "l")["upperarm_l"])
        self.assertLess(after[0], before[0])
        self.assertEqual(hp.adduct(want, tgt_l, "l", 0.0), want)

    def test_local_offsets_round_trip(self):
        src, tgt, parent = self.pair("l")
        want = hp.desired_rotations(src, tgt, "l")
        offsets = hp.local_offsets(tgt, want, {})
        g, _p = fk_after(tgt, parent, offsets)
        for bone, q in want.items():
            self.assertLess(angle_q(g[bone], q), 0.1, bone)


def angle_q(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2.0 * math.acos(min(1.0, d)))


if __name__ == "__main__":
    unittest.main()
