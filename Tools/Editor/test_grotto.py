"""Chapter 3's grotto (_grotto.plan_grotto, create_grotto.py) over the committed East Village data and
Tools/Interiors/Grotto.json: the street door stands on a pavement nobody else uses with its roof point inside the same
building, and the inside's dressing, triggers, keypad, alarm box and Santa's room sit where the layout's rooms, doors and
elves leave room for them (claude-docs/gameplay-semantics.md, "Chapter sites", "Chapter interiors").

Plain CPython, no editor (test_city_golden's stub stands in for ``unreal``). The street half takes about two minutes,
most of it the planning world the chase, the challenges, the arena and the toy drive share; the inside half is instant.

    python -B -m unittest Tools/Editor/test_grotto.py
"""

import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import _grotto  # noqa: E402
import _interior as it  # noqa: E402


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _segment_distance(p, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0.0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / length2))
    return math.hypot(p[0] - (ax + dx * t), p[1] - (ay + dy * t))


# Footprint half sizes of the inside's blocking props (cm), from create_grotto's meshes.
FOOTPRINT = {"throne": 70.0, "post": 15.0, "toys": 40.0, "sleigh": 125.0}
ELF_CLEAR = 60.0          # cm between an elf's beat and a blocking prop's footprint


class GrottoInsideTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layout = it.parse(_grotto.LAYOUT)
        cls.inside = _grotto.plan_inside()

    def room_of(self, x, y):
        for room in self.layout.rooms:
            if room.contains(x, y):
                return room
        return None

    def test_every_prop_stands_in_a_room(self):
        for kind, at in self.inside["props"]:
            self.assertIsNotNone(self.room_of(at[0], at[1]), "{0} at {1}".format(kind, at))

    def test_blocking_props_keep_off_the_elves_beats(self):
        for enemy in self.layout.enemies():
            (x, y, _z), _yaw = self.layout.enemy_start(enemy)
            route = [(x, y)] + [(p[0], p[1]) for p in self.layout.enemy_route(enemy)]
            legs = list(zip(route, route[1:])) or [(route[0], route[0])]
            for kind, at in self.inside["props"]:
                if kind not in FOOTPRINT:
                    continue
                for a, b in legs:
                    self.assertGreater(_segment_distance(at[:2], a, b), FOOTPRINT[kind] + ELF_CLEAR,
                                       "{0} at {1} is on {2}'s beat".format(kind, at[:2], enemy["id"]))

    def test_the_keypad_and_the_volume_are_at_the_workshop_door(self):
        door = next((bd, at) for d, bd, at, _w in self.layout.door_spots() if d["id"] == "workshop")
        bd, at = door
        door_xy = bd.point(at)
        keypad = self.inside["keypad"]
        self.assertLess(_dist2(keypad, door_xy), 200.0, "the keypad is beside the door")
        self.assertLess(abs(keypad[1] - door_xy[1]), 20.0, "...on its wall")
        volume, extent = self.inside["enter_volume"], self.inside["enter_extent"]
        self.assertLessEqual(abs(door_xy[0] - volume[0]), extent[0])
        self.assertLessEqual(door_xy[1] - (volume[1] + extent[1]), 20.0, "the volume reaches the door")
        self.assertEqual(self.room_of(volume[0], volume[1]).id, "corridor")
        opens = next(d for d, _bd, _at, _w in self.layout.door_spots() if d["id"] == "workshop")
        self.assertEqual(opens.get("opens_on"), "open_vault")
        self.assertTrue(opens.get("locked"))

    def test_the_alarm_box_is_on_the_grotto_wall_and_the_guard_can_hear_it(self):
        sx, sy, sz, syaw = self.inside["switch"]
        grotto = self.layout.by_id["grotto"]
        self.assertLess(abs(sx - grotto.x0), 20.0, "on the grotto's west wall")
        self.assertTrue(grotto.y0 < sy < grotto.y1)
        self.assertEqual(syaw, 0.0, "facing into the room")
        guard = next(e for e in self.layout.enemies() if e["id"] == "guard")
        (gx, gy, _gz), _yaw = self.layout.enemy_start(guard)
        sx += 80.0   # the noise is heard from the floor in front of the box (AArrowSwitch::HandleArrowHit)
        archway = next(bd.point(at) for d, bd, at, _w in self.layout.door_spots() if d["id"] == "grotto_corridor")
        walk = _dist2((gx, gy), archway) + _dist2(archway, (sx, sy))
        self.assertLess(walk, 1200.0 * _grotto.SWITCH_NOISE - 200.0, "the corridor's guard hears it round the archway")
        # The two in the stockroom do not: they would walk into the hall while she stalks the guard.
        stock_arch = next(bd.point(at) for d, bd, at, _w in self.layout.door_spots() if d["id"] == "grotto_stock")
        for elf in ("talk_a", "talk_b"):
            enemy = next(e for e in self.layout.enemies() if e["id"] == elf)
            (ex, ey, _ez), _yaw = self.layout.enemy_start(enemy)
            far = _dist2((ex, ey), stock_arch) + _dist2(stock_arch, (sx, sy))
            self.assertGreater(far, 1200.0 * _grotto.SWITCH_NOISE + 200.0, elf + " hears the alarm box")

    def test_the_triggers_are_where_their_events_happen(self):
        rooms = {event: self.room_of(at[0], at[1]).id for event, at, _extent, _unseen in self.inside["triggers"]}
        self.assertEqual(rooms["trigger:first_elf"], "grotto")
        self.assertEqual(rooms["trigger:overheard"], "stockroom")
        self.assertEqual(rooms["trigger:boomerang"], "stockroom")
        unseen = {event: flag for event, _at, _extent, flag in self.inside["triggers"]}
        self.assertTrue(unseen["trigger:overheard"], "the talk stops once she is seen")

    def test_santas_room_is_the_workshop(self):
        arena = self.inside["arena"]
        workshop = self.layout.by_id["workshop"]
        xs = [p[0] for p in arena["outline"]]
        ys = [p[1] for p in arena["outline"]]
        self.assertEqual((min(xs), min(ys), max(xs), max(ys)), (workshop.x0, workshop.y0, workshop.x1, workshop.y1))
        self.assertTrue(workshop.contains(arena["boss"][0], arena["boss"][1], margin=60.0))
        sleighs = [at for kind, at in self.inside["props"] if kind == "sleigh"]
        self.assertEqual(len(sleighs), 4)
        for at in sleighs:
            self.assertGreater(_dist2(at, arena["centre"]), arena["radius"], "the fight's middle is clear of the sleighs")

    def test_nothing_in_the_layout_blocks_the_workshop_doorway(self):
        door = next(bd.point(at) for d, bd, at, _w in self.layout.door_spots() if d["id"] == "workshop")
        for prop in self.layout.props:
            room = self.layout.by_id[prop["room"]]
            x, y = room.point(*prop["at"])
            self.assertGreater(_dist2((x, y), door), 250.0, "{0} in {1} stands in the workshop doorway".format(prop["tag"], room.id))

    def test_the_roof_stairs_leave_from_the_workshop(self):
        exits = {item["id"]: item for item, _bd, _at, _w in self.layout.exit_spots()}
        self.assertEqual(exits["roof"]["room"], "workshop")
        self.assertEqual(exits["roof"]["return"], _grotto.ROOF_POINT)
        self.assertEqual(self.layout.doorstep, _grotto.DOORSTEP)

    def test_the_elves_are_elves(self):
        for enemy in self.layout.enemies():
            self.assertEqual(enemy["blueprint"], "/Game/Blueprints/AI/BP_Thug_Elf", enemy["id"])
            self.assertEqual(enemy["alert_group"], "grotto")


class GrottoStreetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import test_city_golden as golden
        cls.gen = golden.import_generator()
        cls.district = cls.gen.District()
        import create_challenges as cc
        cls.cc = cc
        cls.plan = _grotto.plan_grotto(cls.district)
        cls.world = cc.planning_world(cls.district)

    def test_a_building_is_found(self):
        self.assertIsNotNone(self.plan, _grotto.describe(None))

    def test_the_door_is_on_a_facade_with_open_pavement_in_front(self):
        plan = self.plan
        out = (math.cos(math.radians(plan["yaw"])), math.sin(math.radians(plan["yaw"])))
        front = (plan["door"][0] + out[0] * _grotto.FRONT_OUT, plan["door"][1] + out[1] * _grotto.FRONT_OUT)
        behind = (plan["door"][0] - out[0] * 60.0, plan["door"][1] - out[1] * 60.0)
        self.assertIsNone(self.world.building_at(front))
        self.assertTrue(self.cc.on_sidewalk(self.world, front))
        wall = self.world.building_at(behind)
        self.assertIsNotNone(wall, "a wall behind the door")
        self.assertEqual(wall[0]["id"], plan["osm"], "...the grotto's own")

    def test_nobody_elses_spot_is_near(self):
        blockers = _grotto._blockers(self.district, self.world, self.gen, self.cc)
        out = (math.cos(math.radians(self.plan["yaw"])), math.sin(math.radians(self.plan["yaw"])))
        front = (self.plan["door"][0] + out[0] * _grotto.FRONT_OUT, self.plan["door"][1] + out[1] * _grotto.FRONT_OUT)
        for x, y, clear, what in blockers:
            self.assertGreaterEqual(_dist2(front, (x, y)), clear, what)

    def test_the_roof_point_is_on_the_same_roof_well_inside(self):
        import _geo as geo
        rec = next(r for r in self.district.buildings if r["id"] == self.plan["osm"])
        ring = geo.clean_ring(self.district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        self.assertTrue(geo.point_in_polygon(self.plan["roof"][:2], ring))
        self.assertGreaterEqual(self.plan["roof_edge"], _grotto.ROOF_EDGE)
        self.assertAlmostEqual(self.plan["roof"][2], rec["height_m"] * 100.0 + _grotto.ROOF_ABOVE, places=3)
        self.assertNotIn(self.plan["osm"], _grotto._taken_roofs(self.district, self.world, self.cc, self.gen))
        # The leave_roof volume is a few steps off the point she comes out at, still well inside the roof, and does not
        # hold the point itself (she walks into it).
        walk = self.plan["roof_volume"]
        self.assertTrue(geo.point_in_polygon(walk[:2], ring))
        self.assertGreaterEqual(self.gen._edge_distance(walk[:2], ring), _grotto.ROOF_WALK_EDGE)
        extent = self.plan["roof_extent"]
        self.assertTrue(abs(walk[0] - self.plan["roof"][0]) > extent[0] or abs(walk[1] - self.plan["roof"][1]) > extent[1])

    def test_the_chapter_opens_with_a_run(self):
        self.assertTrue(_grotto.START_RANGE[0] <= self.plan["start_distance"] <= _grotto.START_RANGE[1])

    def test_the_ornament_is_by_kates_door(self):
        spots = self.gen.safehouse_spots(self.district)
        door = spots[0][1]
        d = _dist2(self.plan["ornament"], (door["x"], door["y"]))
        self.assertTrue(250.0 < d < 450.0, "beside her door, clear of the door's own zone ({0:.0f} cm)".format(d))


if __name__ == "__main__":
    unittest.main()
