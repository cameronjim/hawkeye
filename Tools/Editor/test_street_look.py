"""Tests for _street_look (the hero block's relief and street dressing). Plain CPython over the committed data.

    python Tools/Editor/test_street_look.py
"""

import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_city_golden import import_generator  # noqa: E402


class StreetLookTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = import_generator()
        import _geo as geo
        import _street_look as sl
        cls.sl = sl
        cls.district = cls.gen.District()
        loc, _rot = cls.gen.player_start_transform(cls.district)
        cls.start = (loc.x, loc.y)
        cls.records = []
        for rec in cls.district.buildings:
            ring = geo.clean_ring(cls.district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
            if len(ring) >= 3:
                cls.records.append((rec, ring, cls.gen.facade_style(rec)))

    def test_hash_matches_the_shader_range(self):
        values = [self.sl.h21(x, y) for x in range(-20, 20) for y in range(-5, 5)]
        self.assertTrue(all(0.0 <= v < 1.0 for v in values))
        self.assertGreater(len(set(round(v, 4) for v in values)), 300)

    def test_relief_is_deterministic_and_near_the_start(self):
        a = self.sl.relief(self.district, self.start, self.records)
        b = self.sl.relief(self.district, self.start, self.records)
        self.assertEqual(a, b)
        self.assertIn("TrimCream", a)
        self.assertIn("StoopStone", a)
        reach = self.sl.HERO_RADIUS + 10000.0
        for boxes in a.values():
            for box in boxes:
                self.assertLess(math.hypot(box[0] - self.start[0], box[1] - self.start[1]), reach)

    def test_trees_keep_clear(self):
        avoid = [self.start]
        trees = self.sl.tree_spots(self.district, self.start, avoid)
        self.assertGreater(len(trees), 10)
        for x, y, _yaw, scale in trees:
            self.assertGreaterEqual(math.hypot(x - self.start[0], y - self.start[1]), self.sl.TREE_CLEAR)
            self.assertTrue(0.8 <= scale <= 1.2)

    def test_crosswalks_exist(self):
        self.assertGreater(len(self.sl.crosswalks(self.district, self.start).get(self.sl.PAINT, [])), 10)


if __name__ == "__main__":
    unittest.main()
