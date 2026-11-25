"""Unit tests for headcage.py (the enemies' hand-authored head cage). Needs numpy, not Blender:

    python Tools/Blender/test_headcage.py

Skipped, with a note, where numpy is not installed (the engine's bundled Python has none).
"""

import math
import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import numpy as np

    import headcage as hc
    import looks
except ImportError:   # pragma: no cover
    np = None


def edge_counts(faces):
    counts = Counter()
    for f in faces:
        for i in range(len(f)):
            counts[(f[i], f[(i + 1) % len(f)])] += 1
    return counts


@unittest.skipIf(np is None, "numpy is not installed")
class CageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cages = {look_id: hc.build_cage(look.face) for look_id, look in looks.LOOKS.items()}

    def test_size_of_a_hand_authored_cage(self):
        for look_id, d in self.cages.items():
            self.assertTrue(350 <= len(d.verts) <= 500, "{0}: {1} vertices".format(look_id, len(d.verts)))
            quads = sum(1 for f in d.faces if len(f) == 4)
            self.assertGreater(quads / len(d.faces), 0.8, look_id)       # quad-dominant
            self.assertTrue(np.isfinite(d.verts).all(), look_id)

    def test_closed(self):
        for look_id, d in self.cages.items():
            counts = edge_counts(d.faces)
            self.assertTrue(all(n == 1 for n in counts.values()), "{0}: an edge used twice the same way".format(look_id))
            self.assertEqual([e for e in counts if (e[1], e[0]) not in counts], [], look_id)   # watertight (the neck is capped)

    def test_faces_look_outward(self):
        for look_id, d in self.cages.items():
            good = 0
            for f in d.faces:
                p = d.verts[list(f)]
                n = np.cross(p[1] - p[0], p[2] - p[1])
                good += float(np.dot(n, p.mean(axis=0) - np.array([0.0, 0.0, hc.CHIN_Z + 0.09])) > 0)
            self.assertGreater(good / len(d.faces), 0.85, look_id)

    def test_mirrored_on_x(self):
        for look_id, d in self.cages.items():
            mirrored = d.verts * np.array([-1.0, 1.0, 1.0])
            key = lambda a: sorted(map(tuple, np.round(a, 6)))    # noqa: E731
            self.assertEqual(key(d.verts), key(mirrored), look_id)

    def test_proportions_of_an_adult_head(self):
        for look_id, d in self.cages.items():
            v = d.verts
            head = v[v[:, 2] > hc.CHIN_Z - 0.005]
            self.assertAlmostEqual(v[:, 2].max() - hc.CHIN_Z, 0.225, places=3)
            width = head[:, 0].max() - head[:, 0].min()
            depth = head[:, 1].max() - head[:, 1].min()
            self.assertTrue(0.14 < width < 0.175, "{0} width {1}".format(look_id, width))
            self.assertTrue(0.17 < depth < 0.24, "{0} depth {1}".format(look_id, depth))
            # The nose is the most forward thing on the face.
            nose = d.names.index(("L", hc.R["tip"], 0, 1))
            self.assertEqual(int(np.argmin(v[:, 1])), nose, look_id)

    def test_landmarks_by_the_rules_of_the_face(self):
        d = self.cages["thug"]
        v, idx = d.verts, {k: i for i, k in enumerate(d.names)}
        z = lambda key: (v[idx[key]][2] - hc.CHIN_Z) * 100.0           # noqa: E731
        eye = 0.5 * (z(("A", 2, 1)) + z(("A", 6, 1)))
        self.assertAlmostEqual(eye, 0.5 * hc.HEAD_H_CM, delta=0.3)                    # eyes at half the head's height
        sub, brow, chin = z(("L", hc.R["sub"], 0, 1)), z(("L", hc.R["e0"], 0, 1)), 0.0
        self.assertAlmostEqual(sub, chin + (brow - chin) / 2.0, delta=0.7)            # nose base about halfway chin to brow
        mouth = 0.5 * (z(("L0", "t0", 1)) + z(("L0", "b0", 1)))
        self.assertAlmostEqual(mouth, sub - sub / 3.0, delta=0.35)                    # mouth a third from nose base to chin
        self.assertAlmostEqual(abs(v[idx[("A", 0, 1)]][0] - v[idx[("A", 0, -1)]][0]) * 100.0, 2.0 * 1.65, delta=0.4)

    def test_neck_is_narrower_than_the_jaw(self):
        for look_id, d in self.cages.items():
            v = d.verts
            neck = v[(v[:, 2] < hc.CHIN_Z - 0.04)]
            jaw = v[(v[:, 2] > hc.CHIN_Z + 0.02) & (v[:, 2] < hc.CHIN_Z + 0.055)]
            self.assertLess(np.abs(neck[:, 0]).max(), np.abs(jaw[:, 0]).max() - 0.004, look_id)

    def test_uvs(self):
        for look_id, d in self.cages.items():
            self.assertEqual(len(d.uvs), sum(len(f) for f in d.faces), look_id)
            self.assertTrue(((d.uvs >= -1e-6) & (d.uvs <= 1.0 + 1e-6)).all(), look_id)
            self.assertLessEqual(d.uvs[:, 1].max(), hc.V_HEAD + 1e-6, look_id)

    def test_every_row_of_looks_makes_a_different_man(self):
        ids = ["thug", "thug_b", "thug_c", "thug_d"]
        for i, a in enumerate(ids):
            for b in ids[i + 1:]:
                rms = float(np.sqrt(np.mean((self.cages[a].verts - self.cages[b].verts) ** 2)))
                self.assertGreater(rms, 0.0012, "{0} and {1} are alike (rms {2:.4f} m)".format(a, b, rms))

    def test_a_parameter_moves_its_landmark(self):
        base = looks.LOOKS["thug"].face
        n0 = hc.build_cage(base)
        i = n0.names.index(("L", hc.R["tip"], 0, 1))
        longer = hc.build_cage(looks.with_face(looks.LOOKS["thug"], nose_proj=1.2).face)
        self.assertLess(longer.verts[i][1], n0.verts[i][1] - 0.004)                    # the tip stands out further
        j = n0.names.index(("L", hc.R["m1"], 2, 1))
        wide = hc.build_cage(looks.with_face(looks.LOOKS["thug"], mouth_width=1.12).face)
        narrow = hc.build_cage(looks.with_face(looks.LOOKS["thug"], mouth_width=0.9).face)
        self.assertGreater(wide.verts[j][0], narrow.verts[j][0] + 0.0012)              # the corner moves out
        k = n0.names.index(("L", hc.R["chin"], 0, 1))
        chin = hc.build_cage(looks.with_face(looks.LOOKS["thug"], chin=1.3).face)
        self.assertLess(chin.verts[k][1], n0.verts[k][1] - 0.001)

    def test_ear(self):
        face = looks.LOOKS["thug"].face
        anchor = (0.072, 0.008, hc.CHIN_Z + 0.094)
        for side in (1, -1):
            verts, faces, uv = hc.ear_cage(face, (anchor[0] * side, anchor[1], anchor[2]), side, (0.8, 0.6), 0.015)
            self.assertTrue(np.isfinite(verts).all())
            self.assertEqual(len(uv), sum(len(f) for f in faces))
            self.assertTrue(all(0 <= i < len(verts) for f in faces for i in f))
            undirected = Counter()
            for f in faces:
                for k in range(len(f)):
                    undirected[tuple(sorted((f[k], f[(k + 1) % len(f)])))] += 1
            self.assertTrue(all(n == 2 for n in undirected.values()))                 # closed (the build fixes its winding)
            length = (verts[:, 2].max() - verts[:, 2].min()) * 100.0
            self.assertTrue(5.0 < length < 8.0, length)
            # it stands off the head's side, away from the front
            self.assertGreater(abs(verts[:, 0]).max(), 0.072)
            self.assertGreater(verts[:, 1].max(), anchor[1] + 0.02)


if __name__ == "__main__":
    unittest.main()
