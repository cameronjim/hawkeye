"""The boss arena's plan (_arena.plan_arena, create_boss.py) over the committed East Village data: the roof it picks
suits a fight and a way up, and the layout keeps the fighters, the Santa's lane and the props apart
(claude-docs/gameplay-semantics.md, "Bosses").

Plain CPython, no editor (test_city_golden's stub stands in for ``unreal``). About two minutes, most of it the
planning world the chase and the challenges share.

    python -B -m unittest Tools/Editor/test_arena.py
"""

import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import test_city_golden as golden  # noqa: E402


class ArenaPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = golden.import_generator()
        cls.district = cls.gen.District()
        import _arena
        import create_challenges as cc
        cls.arena = _arena
        cls.cc = cc
        cls.plan = _arena.plan_arena(cls.district)
        cls.world = cc.planning_world(cls.district)

    def test_a_roof_is_found(self):
        self.assertIsNotNone(self.plan, self.arena.describe(None))

    def test_the_roof_suits_a_fight(self):
        plan = self.plan
        low, high = self.arena.ROOF_HEIGHT
        self.assertTrue(low <= plan["height_m"] <= high, plan["height_m"])
        self.assertGreaterEqual(plan["height_m"], self.gen.PARAPET_MIN_HEIGHT_M, "a roof that tall has parapets")
        self.assertGreaterEqual(plan["area_m2"], self.arena.MIN_AREA)
        self.assertGreaterEqual(plan["radius"], self.arena.MIN_RADIUS)
        top = self.world.by_id[plan["roof"]]
        self.assertAlmostEqual(plan["roof_z"], self.world.roof_z(top))

    def test_the_roof_is_nobody_elses(self):
        taken = self.arena._taken(self.district, self.world, self.cc)
        self.assertNotIn(self.plan["roof"], taken)

    def test_the_fight_circle_is_clear(self):
        plan = self.plan
        top = self.world.by_id[plan["roof"]]
        centre = plan["centre"][:2]
        self.assertGreaterEqual(self.gen._edge_clearance(centre, top[1]), plan["radius"] - 1.0)
        for x, y, z0, z1, radius, kind in self.world.props_near(centre[0], centre[1], plan["radius"] + 500.0):
            if z0 - 50.0 <= plan["roof_z"] <= z1 + 50.0:
                self.assertGreaterEqual(math.hypot(x - centre[0], y - centre[1]) - radius, plan["radius"], kind)

    def test_the_street_stand_grapples_onto_the_roof(self):
        import _chase
        plan = self.plan
        stand = plan["player_start"]
        self.assertTrue(self.cc.on_sidewalk(self.world, stand[:2]))
        anchor = next(a for a in self.world.anchors if a[6] == plan["roof"] and abs(a[0] - plan["anchor"][0]) < 1.0
                      and abs(a[1] - plan["anchor"][1]) < 1.0)
        self.assertTrue(_chase.grapple_ok(self.cc, self.world, stand, None, anchor))
        self.assertLess(abs(anchor[5] - plan["roof_z"]), self.arena.LANDING_ROOF_TOL, "the landing is on the roof")

    def test_the_fighters_stand_in_the_circle(self):
        plan = self.plan
        centre = plan["centre"]
        for at in [plan["boss"]] + plan["crew"]:
            self.assertLessEqual(math.hypot(at[0] - centre[0], at[1] - centre[1]), plan["radius"])
        self.assertEqual(len(plan["crew"]), 4)

    def test_the_lane_crosses_the_middle_and_the_props_keep_off_it(self):
        plan = self.plan
        a, b = plan["lane"]
        centre = plan["centre"]
        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        self.assertLess(math.hypot(mid[0] - centre[0], mid[1] - centre[1]), 1.0)
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        self.assertGreater(length, 900.0)
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        for kind, at in plan["props"]:
            across = abs((at[0] - centre[0]) * -uy + (at[1] - centre[1]) * ux)
            self.assertGreater(across, self.arena.SANTA_HALF_WIDTH, kind)
            for person in [plan["boss"]] + plan["crew"]:
                self.assertGreater(math.hypot(at[0] - person[0], at[1] - person[1]), self.arena.PROP_PERSON_CLEAR, kind)

    def test_it_is_dressed(self):
        kinds = [kind for kind, _at in self.plan["props"]]
        self.assertIn("speaker", kinds)
        self.assertGreaterEqual(kinds.count("pole"), 2)
        self.assertGreaterEqual(len(self.plan["bulbs"]), 3)


if __name__ == "__main__":
    unittest.main()
