"""Unit tests for headshape.py and headtex.py (the sculpted head and its bakes). Needs numpy, not Blender:

    python Tools/Blender/test_headshape.py

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

    import headshape as hs
    import headtex as ht
    import looks
except ImportError:   # pragma: no cover
    np = None

SIZE = 96   # texture size in the tests (the build uses 1024)


def directed_edge_problems(faces):
    """Directed edges that are not used exactly once with their reverse present (a watertight, consistently wound mesh
    has none)."""
    counts = Counter()
    for f in faces:
        for i in range(len(f)):
            counts[(f[i], f[(i + 1) % len(f)])] += 1
    return [e for e, n in counts.items() if n != 1 or (e[1], e[0]) not in counts]


def outward_fraction(verts, faces, centre):
    good = 0
    for f in faces:
        p = verts[list(f)]
        n = np.cross(p[1] - p[0], p[2] - p[1])
        good += float(np.dot(n, p.mean(axis=0) - centre) > 0)
    return good / len(faces)


@unittest.skipIf(np is None, "numpy is not installed")
class GridTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.grids = {look_id: hs.HeadGrid(look.face) for look_id, look in looks.LOOKS.items()}

    def test_shapes_and_finite(self):
        for look_id, g in self.grids.items():
            self.assertEqual(g.P.shape, (hs.row_count(), hs.SEGMENTS, 3), look_id)
            self.assertTrue(np.isfinite(g.vertices()).all(), look_id)
            self.assertTrue(np.isfinite(g.N).all(), look_id)

    def test_watertight_and_outward(self):
        for look_id, g in self.grids.items():
            faces = g.quads()
            self.assertEqual(directed_edge_problems(faces), [], look_id)
            self.assertGreater(outward_fraction(g.vertices(), faces, np.array([0.0, 0.0, 1.62])), 0.97, look_id)

    def test_proportions_of_an_adult_head(self):
        for look_id, g in self.grids.items():
            v = g.vertices()
            self.assertAlmostEqual(v[:, 2].max(), hs.CROWN_Z, places=3)
            height = hs.CROWN_Z - hs.CHIN_Z
            width = v[:, 0].max() - v[:, 0].min()
            # Width 14 to 17 cm over the skull and cheeks; length (nose to back) 18 to 22 cm; 22.5 cm tall.
            self.assertTrue(0.14 < width < 0.17, "{0} width {1}".format(look_id, width))
            top = g.P[g.row_of_t(0.55).astype(int):]
            depth = top[..., 1].max() - top[..., 1].min()
            self.assertTrue(0.17 < depth < 0.23, "{0} depth {1}".format(look_id, depth))
            self.assertAlmostEqual(height, 0.225, places=3)
            # The nose is the most forward thing on the face.
            front = g.P[hs.NECK_ROWS:, :, 1].min()
            nose_z = hs.Z(0.335) + 0.0125
            row = int(round(float(g.row_of_t((nose_z - hs.CHIN_Z) / hs.HEAD_H))))
            self.assertLess(g.P[row - 2:row + 3, :, 1].min(), front + 0.012, look_id)

    def test_left_and_right_mirror(self):
        for look_id, g in self.grids.items():
            S = hs.SEGMENTS
            for k in (5, 20, 47):
                np.testing.assert_allclose(g.P[:, k, 0], -g.P[:, (S - k) % S, 0], atol=1e-6, err_msg=look_id)
                np.testing.assert_allclose(g.P[:, k, 1], g.P[:, (S - k) % S, 1], atol=1e-6, err_msg=look_id)

    def test_neck_is_a_tube_under_the_chin(self):
        for look_id, g in self.grids.items():
            neck = g.P[:hs.NECK_ROWS]
            self.assertLess(neck[..., 2].max(), hs.CHIN_Z + 1e-9, look_id)
            self.assertGreaterEqual(neck[..., 2].min(), hs.NECK_BASE_Z - 1e-9, look_id)
            half_width = np.abs(neck[0, :, 0]).max()
            half_depth = (neck[0, :, 1].max() - neck[0, :, 1].min()) / 2
            self.assertAlmostEqual(half_width, 0.80 * looks.LOOKS[look_id].face.neck_radius, places=3)
            self.assertAlmostEqual(half_depth, looks.LOOKS[look_id].face.neck_radius, places=3)

    def test_faces_are_not_the_same_head(self):
        ids = list(self.grids)
        for i, a in enumerate(ids):
            for b in ids[i + 1:]:
                rms = float(np.sqrt(((self.grids[a].P - self.grids[b].P) ** 2).sum(axis=2).mean()))
                self.assertGreater(rms, 0.0012, "{0} and {1}: {2:.4f} m".format(a, b, rms))

    def test_features_sit_where_the_landmarks_say(self):
        g = self.grids["thug"]
        lm = hs.Landmarks(g.face)
        # The brow ridge is in front of the eye socket's floor and the forehead plane below it.
        brow_y = g.surface_at(np.array([0.03]), np.array([lm.brow]))[0][0, 1]
        eye_y = g.surface_at(np.array([lm.eye_x]), np.array([lm.eye + 0.0]))[0][0, 1]
        self.assertLess(brow_y, eye_y - 0.003)
        # The nose tip is forward of the cheek and the lips forward of the groove between them.
        tip_y = g.surface_at(np.array([0.0]), np.array([lm.tip]))[0][0, 1]
        cheek_y = g.surface_at(np.array([0.045]), np.array([lm.sub]))[0][0, 1]
        self.assertLess(tip_y, cheek_y - 0.012)
        lip_y = g.surface_at(np.array([0.0]), np.array([lm.ll]))[0][0, 1]
        groove_y = g.surface_at(np.array([0.0]), np.array([lm.mouth]))[0][0, 1]
        self.assertLess(lip_y, groove_y)

    def test_eyes_are_symmetric_and_inside_the_head(self):
        for look_id, g in self.grids.items():
            (c1, r1), (c2, r2) = g.eyes()
            self.assertAlmostEqual(c1[0], -c2[0], places=9)
            self.assertAlmostEqual(c1[1], c2[1], places=9)
            self.assertAlmostEqual(r1, r2)
            self.assertTrue(0.010 < r1 < 0.014, look_id)
            self.assertTrue(0.058 < 2 * abs(c1[0]) < 0.072, look_id)       # between 5.8 and 7.2 cm apart
            # The ball's front is behind the brow ridge and in front of the cheek's plane.
            self.assertLess(c1[1] - r1, g.P[:, :, 1].mean())
            self.assertGreater(c1[1] - r1, g.P[:, :, 1].min())

    def test_row_spacing_is_finer_over_the_face(self):
        dz = np.diff(hs.row_heights())
        self.assertTrue((dz > 0).all())
        face = dz[hs.NECK_ROWS:hs.NECK_ROWS + hs.FACE_ROWS - 1]
        self.assertLess(face.max(), 0.0030)


@unittest.skipIf(np is None, "numpy is not installed")
class UvTests(unittest.TestCase):
    def test_uvs_cover_the_unit_square_and_match_corners(self):
        g = hs.HeadGrid(looks.LOOKS["thug"].face)
        faces = g.quads()
        uv = g.loop_uvs(faces)
        self.assertEqual(uv.shape, (sum(len(f) for f in faces), 2))
        self.assertGreaterEqual(uv.min(), 0.0)
        self.assertLessEqual(uv.max(), 1.0)
        # No face spans more than a few columns of u (the seam face takes u = 1, not 0).
        k = 0
        for f in faces:
            u = uv[k:k + len(f), 0]
            if len(f) == 4:     # quads (the fans to the crown and the neck's centre span half the way by design)
                self.assertLess(u.max() - u.min(), 0.1)
            k += len(f)

    def test_angles_are_dense_in_front(self):
        th = hs.angles()
        self.assertTrue((np.diff(th) > 0).all())
        gap = np.diff(np.concatenate([th, [th[0] + 2 * math.pi]]))
        front = gap[np.argmin(np.abs(th))]
        self.assertLess(front, 0.6 * gap.max())
        self.assertAlmostEqual(float(np.sum(gap)), 2 * math.pi, places=9)


@unittest.skipIf(np is None, "numpy is not installed")
class ShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.look = looks.LOOKS["thug"]
        cls.g = hs.HeadGrid(cls.look.face)

    def test_hairline_runs_from_the_forehead_to_the_nape(self):
        for look_id, look in looks.LOOKS.items():
            t = hs.hairline_t(look.face, self.g.theta)
            self.assertTrue(((t > 0.05) & (t < 0.9)).all(), look_id)
            front = t[np.argmin(np.abs(self.g.theta))]
            back = t[0]
            self.assertGreater(front, back + 0.3, look_id)     # the nape is far below the forehead
            self.assertAlmostEqual(front, look.face.hairline, places=2)

    def test_a_fade_pulls_the_sides_up(self):
        base = looks.with_face(self.look, hair_fade=0.0).face
        faded = looks.with_face(self.look, hair_fade=1.0).face
        theta = np.array([math.radians(120.0)])
        self.assertGreater(hs.hairline_t(faded, theta)[0], hs.hairline_t(base, theta)[0] + 0.1)

    def test_cap_shell_sits_on_the_skin_and_closes_at_the_crown(self):
        edge = hs.hairline_t(self.look.face, self.g.theta)
        P, N, apex, s = hs.cap_grid(self.g, edge, 0.01, rows=12, feather=0.2)
        self.assertEqual(P.shape, (12, hs.SEGMENTS, 3))
        self.assertAlmostEqual(float(s[0]), 0.0)
        self.assertAlmostEqual(float(s[-1]), 1.0)
        # Thickness feathers to nothing at the edge and reaches the full 1 cm above it.
        d0 = np.linalg.norm(P[0] - self.g.P[self.g.row_of_t(edge).round().astype(int), np.arange(hs.SEGMENTS)], axis=1)
        self.assertLess(float(d0.max()), 0.004)
        self.assertGreater(float(apex[2]), hs.CROWN_Z)
        faces = hs.cap_faces(12, hs.SEGMENTS)
        verts = np.concatenate([P.reshape(-1, 3), apex[None, :]])
        self.assertEqual(len(directed_edge_problems(faces)), hs.SEGMENTS)   # open at the edge only
        # Wound the way the skin's own normals point (the skull is not convex about any one centre).
        agree = 0
        for f in faces:
            p = verts[list(f)]
            n = np.cross(p[1] - p[0], p[2] - p[1])
            ref = N.reshape(-1, 3)[[i for i in f if i < 12 * hs.SEGMENTS]].mean(axis=0)
            agree += float(np.dot(n, ref) > 0)
        self.assertEqual(agree, len(faces))

    def test_headwear_edges_are_where_they_should_be(self):
        theta = np.array([0.0, math.radians(90.0), math.pi])
        beanie = hs.beanie_edge_t(theta)
        cap = hs.cap_edge_t(theta)
        self.assertTrue((beanie[1] < beanie[0]).all() and beanie[1] < cap[1])    # a beanie covers the ears, a cap not
        self.assertTrue(((cap > 0.45) & (cap < 0.75)).all())

    def test_brim_leaves_the_front_of_the_crown(self):
        edge = hs.cap_edge_t(self.g.theta)
        P, N, _apex, _s = hs.cap_grid(self.g, edge, 0.011, rows=8, feather=0.03)
        bp, bn = hs.brim_arrays(self.g, P, N, self.g.theta)
        self.assertEqual(bp.shape[2], 3)
        self.assertLess(bp[..., 1].min(), P[0, :, 1].min() - 0.05)          # it reaches forward
        self.assertTrue((bn[..., 2] == 1.0).all())

    def test_ears_stand_off_the_head_on_both_sides(self):
        for side in (1, -1):
            verts, faces, anchor = hs.ear_arrays(self.look.face, self.g, side)
            self.assertEqual(directed_edge_problems(faces), [])
            self.assertGreater(float(np.abs(verts[:, 0]).max()), 0.075)
            self.assertTrue((np.sign(verts[:, 0]) == side).all())
            self.assertTrue(np.isfinite(verts).all())
            self.assertGreater(float(anchor[2]), hs.CHIN_Z)

    def test_brow_cards_are_above_the_eyes_and_on_the_skin(self):
        for side in (1, -1):
            P, N, u, v = hs.brow_arrays(self.look.face, self.g, side)
            lm = hs.Landmarks(self.look.face)
            self.assertGreater(float(P[..., 2].mean()), lm.eye + 0.008)
            self.assertTrue(np.isfinite(P).all())
            self.assertGreaterEqual(float(u.min()), 0.0)
            self.assertLessEqual(float(v.max()), 1.0)

    def test_eyeball_has_a_cornea_dome_and_wound_outward(self):
        verts, faces, uv, iris = hs.eye_arrays((0.0, -0.06, 1.7), 0.012)
        self.assertEqual(directed_edge_problems(faces), [])
        self.assertGreater(outward_fraction(verts, faces, np.array([0.0, -0.06, 1.7])), 0.99)
        self.assertGreater(int(iris.sum()), 20)
        self.assertLess(int(iris.sum()), len(faces) // 2)
        # The pole is proud of a plain sphere.
        self.assertLess(verts[0, 1], -0.06 - 0.012 - 0.0005)
        self.assertEqual(uv.shape, (len(verts), 2))


@unittest.skipIf(np is None, "numpy is not installed")
class TextureTests(unittest.TestCase):
    def test_skin_bake(self):
        for look_id in ("thug", "thug_d", "heavy"):
            look = looks.LOOKS[look_id]
            g = hs.HeadGrid(look.face)
            albedo, normal, zones = ht.skin_maps(look.face, g, size=SIZE)
            self.assertEqual(albedo.shape, (SIZE, SIZE, 3))
            self.assertEqual(normal.shape, (SIZE, SIZE, 3))
            self.assertTrue(np.isfinite(albedo).all() and np.isfinite(normal).all())
            self.assertGreaterEqual(albedo.min(), 0.0)
            self.assertLessEqual(albedo.max(), 1.0)
            self.assertGreater(normal[..., 2].min(), 0.5)           # tangent-space normals point mostly out of the surface
            # The face's own skin tone (sRGB) is what the middle of the cheek is, within the flush and the noise.
            cheek = albedo[int(SIZE * 0.62), SIZE // 2 + 4]
            tone = np.array(looks.SKIN_TONES[look.face.skin])
            self.assertLess(float(np.abs(cheek - tone).max()), 0.20, look_id)

    def test_beard_only_where_the_style_puts_it(self):
        g = hs.HeadGrid(looks.LOOKS["thug"].face)
        none = looks.with_face(looks.LOOKS["thug"], facial_hair="none", stubble=0.0).face
        _a, _n, with_beard = ht.skin_maps(looks.LOOKS["thug"].face, g, size=SIZE)
        _a, _n, without = ht.skin_maps(none, hs.HeadGrid(none), size=SIZE)
        self.assertGreater(float(with_beard["beard"].max()), 0.5)
        self.assertEqual(float(without["beard"].max()), 0.0)
        # Lower face and neck, not the forehead: the top third of the texture has none.
        self.assertEqual(float(with_beard["beard"][int(SIZE * 0.8):].max()), 0.0)

    def test_eye_texture_has_a_pupil_an_iris_and_a_white(self):
        img = ht.eye_image(looks.LOOKS["thug_b"].face, size=128)
        centre = img[64, 64]
        self.assertLess(float(centre.max()), 0.05)                       # pupil
        iris = img[64, 64 + int(128 * 0.18)]
        sclera = img[64, 64 + int(128 * 0.42 * 2 / 2) + 6]
        self.assertGreater(float(sclera.mean()), 0.3)
        self.assertGreater(float(sclera.mean()), float(iris.mean()))

    def test_brow_and_hair_textures(self):
        face = looks.LOOKS["thug_c"].face
        brow = ht.brow_image(face, width=128, height=32)
        self.assertEqual(brow.shape, (32, 128, 4))
        self.assertTrue(0.25 < float(brow[..., 3].mean()) < 0.95)       # strokes, not a plain card or an empty one
        self.assertTrue(set(np.unique(brow[..., 3]).tolist()) <= {0.0, 1.0})   # clipped alpha
        rgb, normal = ht.hair_maps(face, size=64)
        self.assertEqual(rgb.shape, (64, 64, 4))
        self.assertTrue(set(np.unique(rgb[..., 3]).tolist()) <= {0.0, 1.0})
        self.assertLess(float(rgb[:8, :, 3].mean()), float(rgb[-8:, :, 3].mean()))   # dissolves toward the hairline
        self.assertEqual(normal.shape, (64, 64, 3))
        self.assertLessEqual(float(rgb.max()), 1.0)

    def test_deterministic(self):
        face = looks.LOOKS["thug_b"].face
        a = ht.brow_image(face, 64, 16)
        b = ht.brow_image(face, 64, 16)
        np.testing.assert_array_equal(a, b)
        g = hs.HeadGrid(face)
        np.testing.assert_array_equal(ht.skin_maps(face, g, size=48)[0], ht.skin_maps(face, g, size=48)[0])


if __name__ == "__main__":
    unittest.main()
