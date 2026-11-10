"""Chapter 2's toy drive (_site.plan_site, create_site.py) over the committed East Village data: it stands on Avenue B
at street level with its back to a facade, nobody else's spot is near it, and its crew, sacks and courier are laid out
on open street (claude-docs/gameplay-semantics.md, "Chapter sites").

Plain CPython, no editor (test_city_golden's stub stands in for ``unreal``). About two minutes, most of it the
planning world the chase, the challenges and the arena share.

    python -B -m unittest Tools/Editor/test_site.py
"""

import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import test_city_golden as golden  # noqa: E402


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


class SitePlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = golden.import_generator()
        cls.district = cls.gen.District()
        import _site
        import create_challenges as cc
        cls.site = _site
        cls.cc = cc
        cls.plan = _site.plan_site(cls.district)
        cls.world = cc.planning_world(cls.district)

    def test_a_spot_is_found(self):
        self.assertIsNotNone(self.plan, self.site.describe(None))

    def test_it_is_on_avenue_b_at_street_level(self):
        plan = self.plan
        spot = plan["spot"][:2]
        self.assertEqual(plan["avenue"], "Avenue B")
        self.assertAlmostEqual(plan["spot"][2], self.gen.SIDEWALK_TOP)
        nearest = min((self.cc._segment_point_distance(spot, a, b)[0], name) for a, b, _half, name in self.world.roads)
        self.assertEqual(nearest[1], "Avenue B", "the nearest street is the avenue")
        self.assertTrue(self.cc.on_sidewalk(self.world, spot))
        self.assertIsNone(self.world.building_at(spot))

    def test_nobody_elses_spot_is_near(self):
        blockers = self.site._blockers(self.district, self.world, self.gen, self.cc)
        for x, y, clear, what in blockers:
            self.assertGreaterEqual(_dist2(self.plan["spot"], (x, y)), clear, what)

    def test_the_chapter_opens_with_a_run(self):
        self.assertGreaterEqual(self.plan["start_distance"], self.site.DRIVE_START_MIN)
        self.assertLessEqual(self.plan["start_distance"], self.site.DRIVE_START_MAX)

    def test_crew_sacks_and_courier_stand_on_open_street(self):
        plan = self.plan
        self.assertEqual(len(plan["crew"]), 5)
        self.assertEqual([w for w, _at, _yaw in plan["crew"]].count("Pistol"), 1)
        self.assertEqual(len(plan["sacks"]), 3)
        for _w, at, _yaw in plan["crew"]:
            self.assertIsNone(self.world.building_at(at[:2]))
            self.assertTrue(self.cc.street_clear(self.world, at[:2], self.site.CREW_CLEAR))
        for at in plan["sacks"]:
            self.assertIsNone(self.world.building_at(at[:2]))
        courier = plan["courier"][:2]
        d = _dist2(courier, plan["spot"])
        self.assertTrue(self.site.COURIER_RANGE[0] <= d <= self.site.COURIER_RANGE[1], d)
        self.assertTrue(self.cc.on_sidewalk(self.world, courier))
        self.assertEqual(len(plan["police"]), 2)

    def test_the_arrival_volume_holds_the_drive(self):
        plan = self.plan
        ax, ay, _az = plan["arrive"]
        ex, ey, _ez = plan["arrive_extent"]
        # An axis-aligned box: the table is inside it, so she cannot reach the drive without arriving.
        self.assertLessEqual(abs(plan["table"][0] - ax), ex)
        self.assertLessEqual(abs(plan["table"][1] - ay), ey)

    def test_it_is_dressed(self):
        kinds = [kind for kind, _at in self.plan["props"]]
        self.assertEqual(kinds.count("table"), 2)
        self.assertIn("banner", kinds)
        self.assertIn("volunteer", kinds)
        self.assertGreaterEqual(kinds.count("box"), 3)
        self.assertGreaterEqual(kinds.count("pole"), 2)
        self.assertGreaterEqual(len(self.plan["bulbs"]), 6)


if __name__ == "__main__":
    unittest.main()
