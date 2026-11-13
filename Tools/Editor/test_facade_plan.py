"""Tests for _facade_plan (how M_Facade cuts a building's walls into facades and shop bays) and the sign tables.
Plain CPython, no editor.

    python Tools/Editor/test_facade_plan.py
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import _facade_plan as fp  # noqa: E402


class FacadePlanTest(unittest.TestCase):
    def test_hash_is_lowbias32(self):
        # The shader's FLook.IH must give the same: pinned values of Wellons' lowbias32.
        self.assertEqual(fp.ih(0), 0)
        self.assertEqual(fp.ih(1), 1753845952)
        self.assertEqual(fp.ih(12345), 2435775735)
        values = [fp.hf(fp.key(s, f, g, b, t)) for s in range(0, 9000, 997) for f in range(4) for g in range(3)
                  for b in range(3) for t in range(3)]
        self.assertTrue(all(0.0 <= v < 1.0 for v in values))
        self.assertAlmostEqual(sum(values) / len(values), 0.5, delta=0.05)

    def test_frame_spans_a_rectangle(self):
        ring = [(1000.0, 2000.0), (4000.0, 2000.0), (4000.0, 2800.0), (1000.0, 2800.0)]
        fr = fp.frame(ring, "W1")
        ax, ay, a0, a1, b0, b1, seed = fr
        self.assertAlmostEqual(abs(ax), 1.0, places=5)
        self.assertAlmostEqual(a1 - a0, 3000.0, places=1)
        self.assertAlmostEqual(b1 - b0, 800.0, places=1)
        self.assertTrue(0 <= seed < fp.SEED_MOD)
        # the south wall (outward normal -y): tangent (-ny, nx) = (1, 0)
        span = fp.face_span(fr, 1.0, 0.0)
        self.assertEqual(span, (1000.0, 4000.0, 0))
        self.assertEqual(fp.face_span(fr, -1.0, 0.0), (-4000.0, -1000.0, 1))
        self.assertIsNone(fp.face_span(fr, 0.7071, 0.7071))

    def test_long_fronts_are_cut_on_the_window_grid(self):
        for length in (900.0, 1500.0, 2500.0, 3100.0, 6000.0, 9000.0):
            for seed in (7, 1234, 40000):
                segs = fp.segments(-1234.5, -1234.5 + length, seed, 2)
                self.assertAlmostEqual(segs[0][0], -1234.5)
                self.assertAlmostEqual(segs[-1][1], -1234.5 + length, places=3)
                for (s0, s1, k), nxt in zip(segs, segs[1:] + [None]):
                    self.assertGreaterEqual(s1 - s0, min(fp.SEG_MIN, length) - 1e-6)
                    self.assertLessEqual(s1 - s0, fp.SEG_MAX * 1.6)
                    if nxt is not None:
                        self.assertEqual(s1, nxt[0])
                        self.assertAlmostEqual(s1 / fp.BAY_GRID, round(s1 / fp.BAY_GRID))
                self.assertEqual([k for _a, _b, k in segs], list(range(len(segs))))

    def test_each_sign_stays_in_its_bay(self):
        for width in (640.0, 900.0, 1300.0, 1700.0):
            pitch, bays, entrance = fp.shop_bays(100.0, 100.0 + width, 99, 0, 1)
            self.assertGreater(pitch, 150.0)
            lo, hi = 100.0 + fp.END_PIER, 100.0 + width - fp.END_PIER
            for uc, i in bays:
                # M_Facade's panel: half the pitch less 14 cm either side of the bay's middle
                self.assertGreaterEqual(uc - (0.5 * pitch - 14.0), lo)
                self.assertLessEqual(uc + (0.5 * pitch - 14.0), hi + 1e-6)
            centres = [uc for uc, _i in bays]
            self.assertTrue(all(b - a >= pitch - 1e-6 for a, b in zip(centres, centres[1:])))
            self.assertEqual(entrance, len(bays) // 2 if len(bays) >= 3 else -1)

    def test_sign_names_are_in_the_atlas(self):
        import _look
        import _sign_font as sf
        for name, kind in _look.SHOPS:
            self.assertTrue(all(ch in sf.GLYPHS for ch in name), name)
            self.assertTrue(0 <= kind <= 7)
        self.assertEqual(len(sf.ADVANCE), 4)
        self.assertTrue(all(len(a) == len(sf.GLYPHS) for a in sf.ADVANCE))
        self.assertIn("ADV[", _look.FACADE_HLSL)


if __name__ == "__main__":
    unittest.main()
