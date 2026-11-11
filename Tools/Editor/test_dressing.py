"""December dressing (_dressing.py, docs/plans/december-dressing.md): the constraint predicates on hand-made cases,
the whole planner over a small made-up street, and the hero block over the committed East Village data with its
golden hash.

The first two classes are plain geometry and run in well under a second. HeroBlockTest reads the district through
test_city_golden's ``unreal`` stub and the chase, toy drive and arena planners (about two and a half minutes).

    python -B -m unittest Tools/Editor/test_dressing.py

The golden hash is sha256 of plan_json (sorted keys, floats rounded to 0.01 cm), first 16 hex digits, recorded with
CPython 3.12 on Windows. A change means the hero block's dressing changes; if intended, say so in the commit and
record the new hash.
"""

import hashlib
import io
import json
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import _dressing as dr  # noqa: E402
import _dressing_kit as kit  # noqa: E402

GOLDEN = {
    "hero_block": "blk_R3363343",
    # 2026-11-10: windows on M_Facade's grid, strings' bulbs in their envelope, the safehouse and interior doors kept clear
    # 2026-11-11: the anchors moved with the reach model's audit parity; strings and lights keep off the new anchor lines
    # (10 to 8 string lights, 369 to 367 window lights, 418 to 420 parapet snow). Was e973fcdb265016cc.
    "hero_plan": "986763cdbd5bc4d6",
}


# ----------------------------------------------------------------------------------------------------------------------
# a made-up street: Test Street along x (centre line y = 0, 12 m wide), six buildings either side, 4 m pavements
# ----------------------------------------------------------------------------------------------------------------------

FACADE_Y = 1000.0       # the north row's street wall; the south row's at -FACADE_Y
DEPTH = 1500.0
WIDTHS = (-3000.0, -2000.0, -1000.0, 0.0, 1000.0, 2000.0, 3000.0)


def _rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]   # ccw


def _building(osm, ring, height, number="", street="Test Street", kind="apartments"):
    return {"id": osm, "ring": ring, "height": height, "top": height + 90.0, "parapet": True, "street": street,
            "number": number, "kind": kind}


def _ledges(b):
    """One slab per edge as _ledges makes them: 2 cm proud of the wall to 30 cm inside, the top 4 m."""
    out = []
    ring = b["ring"]
    for i in range(len(ring)):
        a, c = ring[i], ring[(i + 1) % len(ring)]
        u = dr._unit(a, c)
        inward = (-u[1], u[0])
        pts = [(a[0] - inward[0] * 2.0, a[1] - inward[1] * 2.0), (c[0] - inward[0] * 2.0, c[1] - inward[1] * 2.0),
               (c[0] + inward[0] * 30.0, c[1] + inward[1] * 30.0), (a[0] + inward[0] * 30.0, a[1] + inward[1] * 30.0)]
        out.append({"id": "City_Ledge_{0}_{1}".format(b["id"], i), "osm": b["id"], "ring": pts,
                    "z0": b["top"] - 400.0, "z1": b["top"]})
    return out


def street_scene(anchor_x=-600.0, keepouts=()):
    buildings = []
    for k, (x0, x1) in enumerate(zip(WIDTHS, WIDTHS[1:])):
        h = 1500.0 + 100.0 * k
        buildings.append(_building("N{0}".format(k), _rect(x0, FACADE_Y, x1, FACADE_Y + DEPTH), h, number=str(100 + k)))
        buildings.append(_building("S{0}".format(k), _rect(x0, -FACADE_Y - DEPTH, x1, -FACADE_Y), h + 50.0,
                                   number=str(200 + k)))
    buildings.sort(key=lambda b: b["id"])
    roads = [{"road": "W1", "name": "Test Street", "a": (-6000.0, 0.0), "b": (6000.0, 0.0), "half": 600.0}]
    escapes = [{"osm": "N1", "ring": _rect(-1640.0, FACADE_Y - 115.0, -1360.0, FACADE_Y), "z0": 302.0, "z1": 1500.0}]
    ledges = [led for b in buildings for led in _ledges(b)]
    n2 = [b for b in buildings if b["id"] == "N2"][0]
    marker = (anchor_x, FACADE_Y - 20.0, n2["top"] - 20.0)
    landing = (anchor_x, FACADE_Y + 120.0, n2["height"])
    anchors = [{"id": "A0", "osm": "N2", "spot": (anchor_x, FACADE_Y - 20.0, n2["top"] - 40.0, 90.0, 140.0, 0.0, "N2"),
                "marker": marker, "landing": landing}]
    lines = [
        {"id": "A0@street/sight", "anchor": "A0", "osm": "N2", "kind": "sight", "a": (anchor_x, -100.0, 300.0),
         "b": marker, "supports": ["N2"]},
        {"id": "A0@street/zip", "anchor": "A0", "osm": "N2", "kind": "zip", "a": (anchor_x, 300.0, 127.0),
         "b": (landing[0], landing[1], landing[2] - 8.0), "supports": ["N2"]},
    ]
    props = [
        {"kind": "ParkedCar_Grey", "at": (1500.0, 490.0, 0.0), "yaw": 0.0, "z1": 140.0, "radius": 245.0,
         "size": (450.0, 180.0)},
        {"kind": "ParkedCar_Navy", "at": (2200.0, 490.0, 0.0), "yaw": 0.0, "z1": 140.0, "radius": 245.0,
         "size": (450.0, 180.0)},
        {"kind": "ParkedCar_Black", "at": (-2300.0, 490.0, 0.0), "yaw": 180.0, "z1": 140.0, "radius": 245.0,
         "size": (450.0, 180.0)},
        {"kind": "Hydrant", "at": (500.0, 650.0, 15.0), "yaw": 0.0, "z1": 115.0, "radius": 25.0},
        {"kind": "LampPole", "at": (2900.0, 660.0, 0.0), "yaw": -90.0, "z1": 730.0, "radius": 20.0},
        {"kind": "LampHead", "at": (2900.0, 570.0, 660.0), "yaw": -90.0, "z1": 730.0, "radius": 40.0},
    ]
    pois = [{"id": "N900", "at": (1500.0, 1100.0), "amenity": "restaurant", "shop": None, "name": "Test Diner"},
            {"id": "N901", "at": (1450.0, 1200.0), "amenity": "bench", "shop": None, "name": None}]
    return {"buildings": buildings, "roads": roads, "pois": pois, "escapes": escapes, "ledges": ledges,
            "anchors": anchors, "anchor_lines": lines, "props": props, "keepouts": list(keepouts),
            "chase_start": None, "site": {"spot": (500.0, 700.0, 15.0), "behind": (500.0, 1150.0), "avenue": "Test Street"}}


def _box(kind, at, size=None, yaw=0.0, pitch=0.0, host=None, params=None):
    return dr.make_record(kind, "t/" + kind, at, size, yaw=yaw, pitch=pitch, host=host, params=params)


# ----------------------------------------------------------------------------------------------------------------------


class GeometryTest(unittest.TestCase):
    def test_catenary_hangs_from_both_ends_and_dips_by_the_sag(self):
        pts = dr.catenary_points((0.0, 0.0, 700.0), (2000.0, 0.0, 700.0), 60.0, 9)
        self.assertEqual(len(pts), 9)
        self.assertAlmostEqual(pts[0][2], 700.0, places=6)
        self.assertAlmostEqual(pts[-1][2], 700.0, places=6)
        self.assertAlmostEqual(pts[4][2], 640.0, places=3)
        self.assertTrue(all(pts[k][2] >= pts[k + 1][2] for k in range(4)), "falls to the middle")

    def test_segment_and_prism(self):
        prism = (_rect(0.0, 0.0, 100.0, 100.0), 0.0, 100.0)
        self.assertAlmostEqual(dr.segment_hits_prism((-100.0, 50.0, 50.0), (200.0, 50.0, 50.0), prism), 1.0 / 3.0)
        self.assertIsNone(dr.segment_hits_prism((-100.0, 50.0, 150.0), (200.0, 50.0, 150.0), prism))
        self.assertIsNone(dr.segment_hits_prism((-100.0, 150.0, 50.0), (200.0, 150.0, 50.0), prism))
        self.assertIsNotNone(dr.segment_hits_prism((50.0, 50.0, -10.0), (50.0, 50.0, 300.0), prism), "vertical")

    def test_touching_prisms_do_not_overlap(self):
        a = (_rect(0.0, 0.0, 100.0, 100.0), 0.0, 100.0)
        self.assertFalse(dr.prisms_overlap(a, (_rect(0.0, 0.0, 100.0, 100.0), 100.0, 106.0)))
        self.assertFalse(dr.prisms_overlap(a, (_rect(100.0, 0.0, 200.0, 100.0), 0.0, 100.0)))
        self.assertTrue(dr.prisms_overlap(a, (_rect(99.0, 0.0, 200.0, 100.0), 0.0, 100.0)))
        self.assertTrue(dr.prisms_overlap(a, (_rect(20.0, 20.0, 30.0, 30.0), 50.0, 60.0)), "held inside")

    def test_inset_ring(self):
        ring = dr.inset_ring(_rect(0.0, 0.0, 1000.0, 600.0), 40.0)
        self.assertEqual(sorted(ring), sorted(_rect(40.0, 40.0, 960.0, 560.0)))
        self.assertIsNone(dr.inset_ring(_rect(0.0, 0.0, 60.0, 600.0), 40.0), "too narrow")

    def test_a_wreath_turned_to_the_street_is_thin_out_of_the_wall(self):
        rec = _box("wreath", (0.0, 0.0, 180.0), (55.0, 55.0, 10.0), yaw=90.0, pitch=-90.0)
        ring, z0, z1 = dr.record_volumes(rec)[0]
        xs, ys = [p[0] for p in ring], [p[1] for p in ring]
        self.assertAlmostEqual(max(xs) - min(xs), 55.0, places=3)
        self.assertAlmostEqual(max(ys) - min(ys), 10.0, places=3)
        self.assertAlmostEqual(z1 - z0, 55.0, places=3)


class ConstraintTest(unittest.TestCase):
    def setUp(self):
        self.scene = street_scene()

    def test_blocks_anchor_line(self):
        sight, zip_line = self.scene["anchor_lines"]
        on_line = _box("snow_bank", (-600.0, 300.0, 15.0), (200.0, 90.0, 300.0))
        self.assertTrue(dr.blocks_anchor_line(on_line, zip_line))
        off_line = _box("snow_bank", (-200.0, 300.0, 15.0), (200.0, 90.0, 120.0))
        self.assertFalse(dr.blocks_anchor_line(off_line, zip_line))
        self.assertFalse(dr.blocks_anchor_line(off_line, sight))

    def test_a_line_is_not_blocked_within_the_sight_tolerance_of_its_marker(self):
        sight = self.scene["anchor_lines"][0]
        m = sight["b"]
        near = _box("parapet_snow", (m[0], m[1] + 30.0, m[2] - 10.0), (20.0, 20.0, 4.0), host="N3")
        self.assertFalse(dr.blocks_anchor_line(near, sight))

    def test_a_coating_on_the_anchors_own_roof_is_that_roof_for_the_zip(self):
        zip_line = self.scene["anchor_lines"][1]
        end = zip_line["b"]
        cap = dr.make_record("roof_snow", "t/cap", (end[0], end[1] + 300.0, end[2] + 8.0), host="N2",
                             params={"closed": True, "thickness": 8.0,
                                     "points": [[x, y, end[2] + 8.0] for x, y in _rect(-900.0, 1040.0, -300.0, 2400.0)]})
        self.assertFalse(dr.blocks_anchor_line(cap, zip_line))
        cap["host"] = "N9"
        self.assertTrue(dr.blocks_anchor_line(cap, zip_line), "another building's coating still blocks")

    def test_in_keepout(self):
        keep = [{"what": "chase waypoint 3", "ring": dr.circle_ring(0.0, 700.0, 150.0), "z0": -15.0, "z1": 265.0}]
        self.assertEqual(dr.in_keepout(_box("snow_bank", (100.0, 700.0, 15.0), (200.0, 90.0, 120.0)), keep),
                         "chase waypoint 3")
        self.assertIsNone(dr.in_keepout(_box("snow_bank", (600.0, 700.0, 15.0), (200.0, 90.0, 120.0)), keep))
        high = _box("string_light", (0.0, 700.0, 0.0), host="N3",
                    params={"points": [[0.0, 995.0, 720.0], [0.0, -995.0, 720.0]], "radius": 2.0})
        self.assertIsNone(dr.in_keepout(high, keep), "over the waypoint's keep-out")

    def test_a_door_keeps_its_wall_and_pavement_clear(self):
        keep = [dr.door_keepout("safehouse", 0.0, FACADE_Y, -90.0)]   # in the north row's wall, facing the street
        card = _box("window_light", (100.0, FACADE_Y - 1.5, 500.0), (2.0, 110.0, 150.0), yaw=-90.0, host="N3")
        self.assertEqual(dr.in_keepout(card, keep), "door safehouse", "over the door, under its sign line")
        high = _box("window_light", (100.0, FACADE_Y - 1.5, 830.0), (2.0, 110.0, 150.0), yaw=-90.0, host="N3")
        self.assertIsNone(dr.in_keepout(high, keep))
        aside = _box("window_light", (400.0, FACADE_Y - 1.5, 500.0), (2.0, 110.0, 150.0), yaw=-90.0, host="N3")
        self.assertIsNone(dr.in_keepout(aside, keep))

    def test_overlaps_structure(self):
        sc = self.scene
        under_escape = _box("awning", (-1500.0, 950.0, 330.0), (110.0, 300.0, 6.0), yaw=-90.0, host="N1")
        self.assertEqual(dr.overlaps_structure(under_escape, sc), "fire escape N1")
        in_ledge = _box("window_light", (-500.0, FACADE_Y - 1.5, 1500.0), (2.0, 100.0, 150.0), yaw=-90.0, host="N2")
        self.assertTrue(dr.overlaps_structure(in_ledge, sc).startswith("ledge "))
        on_ledge_top = _box("parapet_snow", (-500.0, FACADE_Y + 15.0, 1790.0 + 3.0), (300.0, 34.0, 6.0), host="N2")
        self.assertIsNone(dr.overlaps_structure(on_ledge_top, sc), "touching the slab's top is not overlapping it")
        in_other = _box("wreath", (-500.0, FACADE_Y + 50.0, 180.0), (55.0, 55.0, 10.0), host="N3")
        self.assertEqual(dr.overlaps_structure(in_other, sc), "building N2")
        in_own = _box("wreath", (-500.0, FACADE_Y + 50.0, 180.0), (55.0, 55.0, 10.0), host="N2")
        self.assertIsNone(dr.overlaps_structure(in_own, sc))

    def test_hits_prop(self):
        props = self.scene["props"]
        beside_car = _box("snow_bank", (1500.0, 670.0, 15.0), (220.0, 90.0, 120.0))   # 45 cm from the car's side
        self.assertIsNone(dr.hits_prop(beside_car, props), "the car's box, not its 245 cm circle")
        on_car = _box("car_snow", (1480.0, 490.0, 135.0), (260.0, 150.0, 40.0))
        self.assertTrue(dr.hits_prop(on_car, props).startswith("ParkedCar_Grey"))
        on_car["params"]["on_prop"] = dr.prop_id(props[0])
        self.assertIsNone(dr.hits_prop(on_car, props))
        by_lamp = _box("snow_bank", (2900.0, 700.0, 15.0), (220.0, 90.0, 120.0))
        self.assertEqual(dr.hits_prop(by_lamp, props), "LampPole@2900,660")

    def test_clears_street(self):
        def light(z, sag):
            pts = dr.catenary_points((0.0, 995.0, z), (0.0, -995.0, z), sag, 9)
            return _box("string_light", (0.0, 0.0, z), host="N3",
                        params={"points": [list(p) for p in pts], "radius": 2.0})
        self.assertTrue(dr.clears_street(light(720.0, 50.0)))
        self.assertFalse(dr.clears_street(light(600.0, 50.0)))
        self.assertTrue(dr.clears_street(_box("snow_bank", (0.0, 0.0, 15.0), (200.0, 90.0, 120.0))))
        # The bulbs hanging under the wire count: a wire just clear on its own is too low with them.
        bare = light(620.0, 50.0)
        self.assertTrue(dr.clears_street(bare))
        bare["params"].update(bulb_drop=dr.STRING_BULB_DROP, bulb_size=dr.STRING_BULB_SIZE)
        self.assertFalse(dr.clears_street(bare))

    def test_density_caps(self):
        recs = [dr.make_record("steam_vent", "t/steam/{0}".format(k), (k * 100.0, 0.0, 100.0), (45.0, 45.0, 200.0))
                for k in range(10)]
        self.assertFalse(dr.within_density_caps(recs, 100.0))
        kept = dr.apply_density_caps(recs, 100.0)
        self.assertEqual(len(kept), dr.DENSITY_CAPS["steam_vent"])
        self.assertTrue(dr.within_density_caps(kept, 100.0))
        self.assertEqual(sorted(r["id"] for r in kept), sorted(r["id"] for r in dr.apply_density_caps(recs[::-1], 100.0)),
                         "the same records stay whatever order they come in")


class StreetPlanTest(unittest.TestCase):
    """The whole planner over street_scene: the north row is the hero block (the made-up toy drive is in front of
    N3)."""

    @classmethod
    def setUpClass(cls):
        cls.scene = street_scene()
        cls.blocks = dr.partition_blocks(cls.scene["buildings"], cls.scene["roads"])
        cls.block = dr.hero_block_id(cls.scene, cls.blocks)
        cls.plan = dr.plan_scene_dressing(cls.scene, cls.block)
        cls.records = cls.plan["records"]

    def kinds(self):
        return {r["kind"] for r in self.records}

    def test_the_street_splits_the_blocks(self):
        self.assertEqual(sorted(self.blocks), ["blk_N0", "blk_S0"])
        self.assertEqual(self.blocks["blk_N0"], ["N0", "N1", "N2", "N3", "N4", "N5"])
        self.assertEqual(self.block, "blk_N0")

    def test_the_hero_falls_back_to_the_chase_start(self):
        scene = dict(self.scene, site=None, chase_start=(-500.0, -1500.0, 1600.0))
        self.assertEqual(dr.hero_block_id(scene, self.blocks), "blk_S0")
        self.assertIsNone(dr.hero_block_id(dict(scene, chase_start=None), self.blocks))

    def test_every_kind_the_street_has_room_for_is_planned(self):
        for kind in ("string_light", "wreath", "window_light", "window_candle", "roof_snow", "parapet_snow",
                     "snow_bank", "tree_stand", "tree_for_sale", "steam_vent", "car_snow", "awning", "shop_sign"):
            self.assertIn(kind, self.kinds(), kind)

    def test_every_record_passes_every_constraint(self):
        for rec in self.records:
            self.assertEqual(dr.violations(rec, self.scene), [], rec["id"])
        self.assertTrue(dr.within_density_caps(self.records, self.plan["frontage_m"]))

    def test_no_string_of_lights_crosses_the_anchor_line(self):
        lights = [r for r in self.records if r["kind"] == "string_light"]
        self.assertTrue(lights)
        for rec in lights:
            for line in self.scene["anchor_lines"]:
                self.assertFalse(dr.blocks_anchor_line(rec, line), rec["id"])
            self.assertGreater(abs(rec["position"][0] - (-600.0)), 30.0, "the span in the anchor's lines is gone")
        moved = dr.plan_scene_dressing(street_scene(anchor_x=-4000.0), self.block)
        self.assertGreater(sum(1 for r in moved["records"] if r["kind"] == "string_light"), len(lights),
                           "with the anchor out of the way that span is planned")

    def test_a_keepout_clears_what_was_there(self):
        wreath = [r for r in self.records if r["id"] == "blk_N0/wreath/N1"][0]
        x, y, _z = wreath["position"]
        keep = {"what": "chase waypoint 0", "ring": dr.circle_ring(x, y - 100.0, 200.0), "z0": 0.0, "z1": 300.0}
        plan = dr.plan_scene_dressing(street_scene(keepouts=[keep]), self.block)
        for rec in plan["records"]:
            self.assertIsNone(dr.in_keepout(rec, [keep]), rec["id"])
        moved = [r for r in plan["records"] if r["id"] == "blk_N0/wreath/N1"]
        self.assertTrue(not moved or moved[0]["position"] != wreath["position"])

    def test_string_lights_span_to_the_facing_facade(self):
        for rec in (r for r in self.records if r["kind"] == "string_light"):
            pts = rec["params"]["points"]
            self.assertAlmostEqual(pts[0][1], FACADE_Y - dr.STRING_OFF_WALL, places=1)
            self.assertAlmostEqual(pts[-1][1], -FACADE_Y + dr.STRING_OFF_WALL, places=1)
            self.assertTrue(rec["params"]["far_host"].startswith("S"))
            self.assertTrue(dr.clears_street(rec))

    def test_the_shop_gets_an_awning_and_a_blank_sign(self):
        awnings = [r for r in self.records if r["kind"] == "awning"]
        signs = [r for r in self.records if r["kind"] == "shop_sign"]
        self.assertEqual([r["params"]["name"] for r in awnings], ["Test Diner"])
        self.assertEqual([r["params"]["text"] for r in signs], [""])
        self.assertEqual(awnings[0]["host"], "N4")

    def test_ids_are_unique_and_stable(self):
        ids = [r["id"] for r in self.records]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(i.startswith("blk_N0/") for i in ids))
        self.assertEqual(len({dr.actor_label(r) for r in self.records}), len(ids))

    def test_same_scene_same_bytes(self):
        again = dr.plan_scene_dressing(street_scene(), self.block)
        self.assertEqual(dr.plan_json(again), dr.plan_json(self.plan))

    def test_records_spawn_as_written(self):
        for rec in self.records:
            self.assertEqual(rec["mesh"], dr.MESHES[rec["shape"]])
            self.assertEqual(len(rec["position"]), 3)
            if rec["shape"] == "spline":
                self.assertGreaterEqual(len(rec["params"]["points"]), 2)
            else:
                self.assertTrue(all(s > 0.0 for s in rec["scale"]))

    def test_windows_sit_on_the_facades_painted_grid(self):
        windows = [r for r in self.records if r["kind"] == "window_light"]
        self.assertTrue(windows)
        for rec in windows:
            nx, ny = math.cos(math.radians(rec["yaw"])), math.sin(math.radians(rec["yaw"]))
            x, y, z = rec["position"]
            # Back on the wall, M_Facade's horizontal coordinate is a half bay; the card spans its sill to its head.
            u = (y - ny * dr.WINDOW_OUT) * nx - (x - nx * dr.WINDOW_OUT) * ny
            off = (u - dr.WINDOW_BAY * 0.5) % dr.WINDOW_BAY
            self.assertLess(min(off, dr.WINDOW_BAY - off), 0.5, rec["id"])
            sill = z - dr.WINDOW_SIZE[2] * 0.5
            self.assertAlmostEqual((sill - dr.WINDOW_SILL) % dr.FLOOR, 0.0, places=1, msg=rec["id"])
            self.assertGreaterEqual(sill, dr.WINDOW_UPPER_FROM)

    def doc(self):
        return json.loads(dr.plan_json(self.plan))

    def test_the_kit_draws_every_record(self):
        groups = kit.instance_groups(self.doc())
        self.assertTrue(set(groups) <= set(kit.GROUPS))
        counts = self.plan["counts"]
        strings = [r for r in self.records if r["kind"] == "string_light"]
        self.assertEqual(len(groups["Dress_Wire"]), sum(len(r["params"]["points"]) - 1 for r in strings))
        bulbs = sum(len(v) for k, v in groups.items() if k.startswith("Dress_Bulb_"))
        self.assertGreater(bulbs, 20 * len(strings))
        self.assertEqual(len(groups["Dress_Wreath"]), counts["wreath"])
        self.assertEqual(len(groups["Dress_WreathBow"]), counts["wreath"])
        self.assertEqual(sum(len(v) for k, v in groups.items() if k.startswith("Dress_Window_")), counts["window_light"])
        self.assertEqual(len(groups["Dress_Candle"]), counts["window_candle"])
        self.assertEqual(len(groups["Dress_CandleFlame"]), counts["window_candle"])
        self.assertEqual(len(groups["Dress_RoofSnow"]), 1, "one mesh for every roof")
        self.assertEqual(len(kit.roof_snow_outlines(self.doc())), counts["roof_snow"])
        self.assertEqual(len(groups.get("Dress_SnowBank", [])) + len(groups.get("Dress_SnowBankDirty", [])),
                         counts["snow_bank"])
        self.assertEqual(len(groups["Dress_SteamStripe"]), 2 * counts["steam_vent"])
        for instances in groups.values():
            for inst in instances:
                self.assertEqual(len(inst), 9)
                self.assertTrue(all(s > 0.0 for s in inst[6:]))
        self.assertEqual(kit.group_counts(self.doc()), {k: len(v) for k, v in groups.items()})

    def test_bulbs_hang_inside_their_strings_envelope(self):
        for rec in (r for r in self.records if r["kind"] == "string_light"):
            one = kit.instance_groups({"block": "t", "records": [rec]})
            env = dr.string_envelope(rec["params"])
            pts = rec["params"]["points"]
            lowest = min(p[2] for p in pts)
            for key, instances in one.items():
                if not key.startswith("Dress_Bulb_"):
                    continue
                for inst in instances:
                    self.assertGreaterEqual(inst[2] - inst[8] * 50.0, lowest - env - 0.01, rec["id"])
                    near = min(math.hypot(inst[0] - p[0], inst[1] - p[1]) for p in pts)
                    self.assertLess(near, dr._dist2(pts[0], pts[1]), rec["id"])

    def test_a_handful_of_real_lights_and_the_shot_cameras(self):
        lights = kit.light_spots(self.doc())
        self.assertTrue(1 <= len(lights) <= 4)
        for _name, (_x, _y, z), _rgb, lumens, radius in lights:
            self.assertGreater(z, 100.0)
            self.assertLessEqual(lumens, 500.0)
            self.assertLessEqual(radius, 1000.0)
        names = [s[0] for s in kit.shot_cameras(self.doc())]
        self.assertEqual(len(names), len(set(names)))
        for name in ("street_lights", "street_back", "facade_wreaths", "tree_stand", "overview"):
            self.assertIn(name, names)


class HeroBlockTest(unittest.TestCase):
    """The hero block over the committed East Village data."""

    @classmethod
    def setUpClass(cls):
        import test_city_golden as golden
        cls.gen = golden.import_generator()
        cls.district = cls.gen.District()
        cls.scene = dr.gather_scene(cls.district)
        cls.block = dr.hero_block_id(cls.scene)
        cls.plan = dr.plan_scene_dressing(cls.scene, cls.block)

    def test_the_hero_block_holds_the_toy_drive(self):
        self.assertEqual(self.block, GOLDEN["hero_block"])
        behind = dr._building_at(self.scene["buildings"], self.scene["site"]["behind"])
        self.assertIn(behind["id"], self.plan["buildings"])

    def test_golden(self):
        digest = hashlib.sha256(dr.plan_json(self.plan).encode("utf-8")).hexdigest()[:16]
        self.assertEqual(digest, GOLDEN["hero_plan"], "the hero block's dressing changed")

    def test_plan_block_dressing_is_the_plans_records(self):
        self.assertEqual(dr.plan_block_dressing(self.district, self.block), self.plan["records"])

    def test_every_record_passes_every_constraint(self):
        for rec in self.plan["records"]:
            self.assertEqual(dr.violations(rec, self.scene), [], rec["id"])
        self.assertTrue(dr.within_density_caps(self.plan["records"], self.plan["frontage_m"]))

    def test_it_is_dressed(self):
        counts = self.plan["counts"]
        for kind in dr.KIND_ORDER:
            self.assertGreaterEqual(counts.get(kind, 0), 1, kind)
        self.assertGreaterEqual(counts["string_light"], 6)
        self.assertEqual(counts["steam_vent"], dr.STEAM_COUNT)

    def test_nothing_sits_on_the_chase_or_in_the_toy_drive(self):
        keep = [k for k in self.scene["keepouts"]]
        self.assertTrue(any(k["what"] == "toy drive" for k in keep))
        self.assertTrue(any(k["what"].startswith("door City_Safehouse_2") for k in keep),
                        "the second safehouse's door is on the hero block")
        for rec in self.plan["records"]:
            self.assertIsNone(dr.in_keepout(rec, keep), rec["id"])

    def test_ids_unique(self):
        ids = [r["id"] for r in self.plan["records"]]
        self.assertEqual(len(ids), len(set(ids)))

    def test_the_committed_plan_is_the_planners(self):
        path = kit.plan_path(self.block)
        self.assertTrue(os.path.exists(path), "run python -B Tools/Editor/write_dressing_plan.py")
        with io.open(path, "r", encoding="utf-8", newline="") as f:
            committed = f.read()
        # The line end may be CRLF in a Windows checkout (core.autocrlf); the record bytes may not differ.
        self.assertEqual(committed.rstrip("\r\n"), dr.plan_json(self.plan).rstrip("\n"),
                         "Tools/Data/dressing is stale; run python -B Tools/Editor/write_dressing_plan.py")

    def test_the_kit_draws_the_block_in_a_few_dozen_instanced_meshes(self):
        doc = json.loads(dr.plan_json(self.plan))
        counts = kit.group_counts(doc)
        self.assertLessEqual(len(counts), 30, "one instanced mesh per group")
        self.assertGreater(sum(counts.values()), len(doc["records"]))
        self.assertLessEqual(len(kit.light_spots(doc)), 4)


if __name__ == "__main__":
    unittest.main()
