"""Unit tests for the city generator's pure modules (_geomutil, _district, _ledges, _reach) on small
hand-built districts. Plain unittest, no editor and no ``unreal``:

    python Tools/Editor/test_city_geometry.py
"""

import math
import os
import random
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _district as dist  # noqa: E402
import _geomutil as gu  # noqa: E402
import _ledges as ledges  # noqa: E402
import _reach as reach  # noqa: E402


def box(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


class FakeDistrict(object):
    """District's shape with the outlines already in centimetres: ring_cm is the identity."""

    def __init__(self, buildings, roads=(), parks=(), ground=(-20000.0, -20000.0, 20000.0, 20000.0)):
        self.buildings = [{"id": osm, "outer": ring, "height_m": h, "tags": {}} for osm, ring, h in buildings]
        self.roads = list(roads)
        self.parks = list(parks)
        self.meta = {"ground_corners_latlon": box(*ground)}

    def cm(self, p):
        return (float(p[0]), float(p[1]))

    def ring_cm(self, ring):
        return [self.cm(p) for p in ring]


# The copy create_crimes had until _geomutil, kept here as the reference the shared helper must match.
def crimes_segment_distance(pt, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((pt[0] - a[0]) * dx + (pt[1] - a[1]) * dy) / L2))
    return math.hypot(a[0] + t * dx - pt[0], a[1] + t * dy - pt[1])


def random_points(seed, n):
    rnd = random.Random(seed)
    out = []
    for _ in range(n):
        kind = rnd.random()
        if kind < 0.1:
            p = (rnd.randint(-5, 5), rnd.randint(-5, 5))                  # small integers, many degenerate
        elif kind < 0.2:
            p = (rnd.uniform(-1e-6, 1e-6), rnd.uniform(-1e-6, 1e-6))      # tiny
        else:
            p = (rnd.uniform(-5e4, 5e4), rnd.uniform(-5e4, 5e4))
        out.append(p)
    return out


class SegmentDistanceTest(unittest.TestCase):
    def test_projection_clamped_to_the_ends(self):
        self.assertEqual(gu._segment_distance((5.0, 3.0), (0.0, 0.0), (10.0, 0.0)), 3.0)
        self.assertEqual(gu._segment_distance((-3.0, 4.0), (0.0, 0.0), (10.0, 0.0)), 5.0)
        self.assertEqual(gu._segment_distance((13.0, 4.0), (0.0, 0.0), (10.0, 0.0)), 5.0)

    def test_degenerate_segment_is_a_point(self):
        self.assertEqual(gu._segment_distance((3.0, 4.0), (0.0, 0.0), (0.0, 0.0)), 5.0)

    def test_takes_3d_ends(self):
        self.assertEqual(gu._segment_distance((5.0, 3.0), (0.0, 0.0, 99.0), (10.0, 0.0, -7.0)), 3.0)

    def test_merged_helpers_are_bit_identical(self):
        """_near_segment (was closest_point_on_polyline on one segment) and create_crimes' copy give the
        same float as _segment_distance for every input, degenerate and integer ones included."""
        self.assertIs(gu._near_segment, gu._segment_distance)
        pts = random_points(7, 3000)
        for i in range(0, len(pts) - 2, 3):
            p, a, b = pts[i], pts[i + 1], pts[i + 2]
            for aa, bb in ((a, b), (a, a)):
                want = gu._segment_distance(p, aa, bb)
                self.assertEqual(gu.closest_point_on_polyline(p, [aa, bb])[0], want)
                self.assertEqual(crimes_segment_distance(p, aa, bb), want)


class PolylineAndRingTest(unittest.TestCase):
    SQUARE = box(0.0, 0.0, 100.0, 100.0)

    def test_closest_point_on_polyline(self):
        d, q = gu.closest_point_on_polyline((50.0, 10.0), [(0.0, 0.0), (100.0, 0.0), (100.0, 100.0)])
        self.assertEqual((d, q), (10.0, (50.0, 0.0)))
        d, q = gu.closest_point_on_polyline((90.0, 50.0), [(0.0, 0.0), (100.0, 0.0), (100.0, 100.0)])
        self.assertEqual((d, q), (10.0, (100.0, 50.0)))

    def test_ring_distances_differ_only_inside(self):
        inside, outside = (50.0, 20.0), (50.0, -30.0)
        self.assertEqual(gu.ring_distance(inside, self.SQUARE), 0.0)
        self.assertEqual(gu._edge_distance(inside, self.SQUARE), 20.0)
        self.assertEqual(gu._edge_clearance(inside, self.SQUARE), 20.0)
        self.assertEqual(gu.ring_distance(outside, self.SQUARE), 30.0)
        self.assertEqual(gu._edge_distance(outside, self.SQUARE), 30.0)
        self.assertEqual(gu._edge_clearance(outside, self.SQUARE), -30.0)

    def test_the_closing_edge_counts(self):
        self.assertEqual(gu._edge_distance((5.0, 50.0), self.SQUARE), 5.0)

    def test_point_quad_distance_is_ring_distance(self):
        self.assertIs(gu._point_quad_distance, gu.ring_distance)

    def test_segments_cross(self):
        self.assertTrue(gu._segments_cross((0, 0), (10, 10), (0, 10), (10, 0)))
        self.assertFalse(gu._segments_cross((0, 0), (10, 0), (0, 5), (10, 5)))
        self.assertFalse(gu._segments_cross((0, 0), (4, 4), (0, 10), (10, 0)))

    def test_segment_params(self):
        ts = sorted(gu._segment_params((-50.0, 50.0), (150.0, 50.0), self.SQUARE))
        self.assertEqual(ts, [0.25, 0.75])
        ts = sorted(gu._segment_params((50.0, 50.0), (150.0, 50.0), self.SQUARE))
        self.assertEqual(ts, [0.0, 0.5])
        self.assertEqual(gu._segment_params((-50.0, -50.0), (-10.0, 150.0), self.SQUARE), [])

    def test_oriented_rect_of_a_rotated_rectangle(self):
        a = math.radians(30.0)
        ux, uy = math.cos(a), math.sin(a)
        ring = [(1000.0 + ux * s - uy * t, 500.0 + uy * s + ux * t) for s, t in ((-200, -50), (200, -50), (200, 50), (-200, 50))]
        cx, cy, hl, hw, yaw = gu.oriented_rect(ring)
        self.assertAlmostEqual(cx, 1000.0, places=6)
        self.assertAlmostEqual(cy, 500.0, places=6)
        self.assertAlmostEqual(hl, 200.0, places=6)
        self.assertAlmostEqual(hw, 50.0, places=6)
        self.assertAlmostEqual(yaw, 30.0, places=6)
        self.assertIsNone(gu.oriented_rect([(0.0, 0.0), (0.5, 0.0), (0.0, 0.5)]))

    def test_stable_hash(self):
        self.assertEqual(gu.stable_hash("w123", "facade"), 0.2414918679278344)
        self.assertEqual(gu.stable_hash(1, 2.5, "x"), 0.5444397493265569)
        self.assertEqual(gu.stable_hash("a", "b"), gu.stable_hash("a|b"))
        self.assertTrue(0.0 <= gu.stable_hash("anything") < 1.0)


class DistrictTest(unittest.TestCase):
    def test_facade_style_bands(self):
        def style(h, kind=None, osm="w1"):
            return dist.facade_style({"id": osm, "height_m": h, "tags": {"building": kind} if kind else {}})
        self.assertEqual(style(4.0), "Painted")
        self.assertEqual(style(20.0, "garage"), "Painted")
        self.assertEqual(style(31.0), "Stone")
        self.assertEqual(style(20.0, "church"), "Stone")
        self.assertEqual(style(8.0), "Brownstone")
        self.assertIn(style(20.0), [s for s, _share in dist.TENEMENT_STYLES])

    def test_road_paths_drop_one_point_pieces(self):
        d = FakeDistrict([], roads=[{"id": "r1", "width_m": 10.0, "pieces": [[(0, 0), (100, 0)], [(5, 5)]]},
                                    {"id": "r2", "width_m": 10.0, "pieces": [[(9, 9)]]}])
        paths = dist.road_paths(d)
        self.assertEqual([rec["id"] for rec, _p in paths], ["r1"])
        self.assertEqual(paths[0][1], [[(0.0, 0.0), (100.0, 0.0)]])

    def test_data_paths_point_at_the_committed_osm_data(self):
        for path in dist.data_paths():
            self.assertTrue(os.path.exists(path), path)


class LedgeTest(unittest.TestCase):
    def test_one_ledge_per_long_enough_edge(self):
        # A 10 x 10 m 12 m tenement (parapet) and a shed with one 80 cm edge.
        d = FakeDistrict([("a", box(0, 0, 1000, 1000), 12.0), ("b", [(2000, 0), (2080, 0), (2080, 500), (2000, 500)], 3.0)])
        spots = ledges.ledge_spots(d)
        self.assertEqual(len([s for s in spots if s[1] == "a"]), 4)
        self.assertEqual(len([s for s in spots if s[1] == "b"]), 2)   # the two 80 cm ends are under LEDGE_MIN_EDGE
        top = 1200.0 + dist.PARAPET_HEIGHT
        for label, osm, origin, _yaw, scale, a, b, _hash in spots:
            self.assertTrue(label.startswith(ledges.LEDGE_PREFIX + osm + "_"))
            if osm == "a":
                self.assertEqual((a[2], b[2]), (top, top))
                self.assertAlmostEqual(origin[2], top - ledges.LEDGE_SLAB_HEIGHT)
                self.assertAlmostEqual(scale[0], 10.0)
                # Ledge_1 runs LEDGE_OUTSET outside the facade.
                self.assertAlmostEqual(gu._edge_distance((a[0], a[1]), box(0, 0, 1000, 1000)), ledges.LEDGE_OUTSET)


class ReachTest(unittest.TestCase):
    """Two 10 m roofs 15 m apart across a gap; Kate on the west roof, the anchor on the east roof's facade
    facing her, as anchor_spots places one (FACADE_ANCHOR_OUT off the wall, top flush with the parapet)."""

    HEIGHT_M = 10.0
    WEST = box(0, 0, 1000, 1000)

    def district(self, *more):
        return FakeDistrict([("west", self.WEST, self.HEIGHT_M), ("east", box(2500, 0, 3500, 1000), self.HEIGHT_M)] + list(more))

    def facade_anchor(self, wall_x, osm, height_m=None):
        roof = (height_m or self.HEIGHT_M) * 100.0
        top = roof + dist.PARAPET_HEIGHT
        z = top - reach.FACADE_ANCHOR_DOWN
        # yaw 0: +X into the building, the landing FACADE_ANCHOR_LANDING in on the roof.
        return (wall_x - reach.FACADE_ANCHOR_OUT, 500.0, z, 0.0, reach.FACADE_ANCHOR_LANDING, z - roof, osm)

    FEET = (800.0, 500.0, 1000.0)   # on the west roof, 2 m in from its east parapet

    def test_anchor_in_range_cone_and_sight_is_reachable(self):
        r = reach.GrappleReach(self.district())
        spot = self.facade_anchor(2500.0, "east")
        self.assertIsNotNone(r.building_at(reach._anchor_landing(spot)[:2]))
        self.assertTrue(r.seen((self.FEET[0], self.FEET[1], self.FEET[2] + reach.REACH_HALF_HEIGHT), spot))
        self.assertTrue(r.zip_clear(self.FEET, spot))
        self.assertTrue(r.targetable(self.FEET, spot))

    def test_anchor_behind_a_taller_building_is_not(self):
        r = reach.GrappleReach(self.district(("tower", box(1400, 200, 2100, 800), 20.0)))
        spot = self.facade_anchor(2500.0, "east")
        self.assertFalse(r.seen((self.FEET[0], self.FEET[1], self.FEET[2] + reach.REACH_HALF_HEIGHT), spot))
        self.assertFalse(r.targetable(self.FEET, spot))

    def test_a_lower_building_between_does_not_block(self):
        r = reach.GrappleReach(self.district(("shed", box(1400, 200, 2100, 800), 3.0)))
        self.assertTrue(r.targetable(self.FEET, self.facade_anchor(2500.0, "east")))

    def test_anchor_beyond_range_is_not(self):
        d = FakeDistrict([("west", self.WEST, self.HEIGHT_M), ("far", box(3500, 0, 4500, 1000), self.HEIGHT_M)])
        r = reach.GrappleReach(d)
        spot = self.facade_anchor(3500.0, "far")
        flat = math.hypot(spot[0] - self.FEET[0], spot[1] - self.FEET[1])
        self.assertGreater(flat, reach.REACH_RANGE - reach.REACH_RANGE_SLACK)
        self.assertTrue(r.seen((self.FEET[0], self.FEET[1], self.FEET[2] + reach.REACH_HALF_HEIGHT), spot))
        self.assertFalse(r.targetable(self.FEET, spot))

    def test_anchor_inside_min_range_is_not(self):
        r = reach.GrappleReach(self.district())
        feet = (2300.0, 500.0, 1000.0)   # hovering over the gap 2 m from the wall: closer than MinRange
        self.assertFalse(r.targetable(feet, self.facade_anchor(2500.0, "east")))

    def test_the_two_roofs_face_each_other(self):
        r = reach.GrappleReach(self.district())
        west, east = r.index["west"], r.index["east"]
        self.assertEqual([(j, gap) for j, gap, _p, _q in r.across(west)], [(east, 1500.0)])
        self.assertTrue(r.pair_in_range(west, east))

    def test_a_tower_between_hides_the_neighbour(self):
        r = reach.GrappleReach(self.district(("tower", box(1400, -500, 2100, 1500), 20.0)))
        west, east = r.index["west"], r.index["east"]
        self.assertNotIn(east, [j for j, _gap, _p, _q in r.across(west)])

    def test_anchor_spots_gives_each_roof_one_the_other_reaches(self):
        d = self.district()
        spots = reach.anchor_spots(d)
        r = reach.grapple_reach(d)
        self.assertIs(r, reach.grapple_reach(d))
        for target, feet_osm in (("east", "west"), ("west", "east")):
            feet = r.roof_spots[feet_osm]
            self.assertTrue(reach._roof_reached(r, feet, spots, target), target)


class ZipLikeTheGameTest(unittest.TestCase):
    """The zip as UGrappleComponent::IsZipClear sweeps it (the 2026-11-11 audit parity): the whole capsule, the start's
    supports only to the release point, the anchor's within 150 cm in 3D, each fire-escape landing on its own."""

    def test_her_own_shop_counts_again_past_the_release_point(self):
        # She stands 60 cm off a 3.3 m shop; the anchor is on the 12 m building behind it. The line climbs through the
        # shop's roof well past 284 cm from her: the game blocks it. The old feet-line model let her own building be
        # crossed for the first 164 cm of the line and never looked again.
        shop = ("shop", box(0, 0, 1000, 500), 3.3)
        tall = ("tall", box(0, 500, 1000, 1500), 12.0)
        top = 1200.0 + dist.PARAPET_HEIGHT
        z = top - reach.FACADE_ANCHOR_DOWN
        spot = (500.0, 500.0 - reach.FACADE_ANCHOR_OUT, z, 90.0, reach.FACADE_ANCHOR_LANDING, z - 1200.0, "tall")
        feet = (500.0, -60.0, dist.SIDEWALK_TOP)
        self.assertFalse(reach.GrappleReach(FakeDistrict([shop, tall])).zip_clear(feet, spot))
        self.assertTrue(reach.GrappleReach(FakeDistrict([tall])).zip_clear(feet, spot), "with no shop in front it is clear")

    def test_the_capsule_not_a_line(self):
        # A 30 cm post 30 cm beside the line of a roof-to-roof zip: a line misses it, the capsule (34 cm) does not.
        west, east = ("west", box(0, 0, 1000, 1000), 10.0), ("east", box(2500, 0, 3500, 1000), 10.0)
        z = 1000.0 + dist.PARAPET_HEIGHT - reach.FACADE_ANCHOR_DOWN
        spot = (2500.0 - reach.FACADE_ANCHOR_OUT, 500.0, z, 0.0, reach.FACADE_ANCHOR_LANDING, z - 1000.0, "east")
        feet = (800.0, 500.0, 1000.0)
        self.assertTrue(reach.GrappleReach(FakeDistrict([west, east])).zip_clear(feet, spot))
        post = ("post", box(1700, 530, 1730, 560), 14.0)
        self.assertFalse(reach.GrappleReach(FakeDistrict([west, east, post])).zip_clear(feet, spot))

    def test_the_anchors_supports_are_within_150_cm_in_3d(self):
        # A corner anchor on a 15 m roof beside a 10 m neighbour: 2D the neighbour is 15 cm off, but its parapet top is
        # 4 m below the anchor, so the game does not ignore it (the model used to). One the same height is ignored.
        r = reach.GrappleReach(FakeDistrict([("tall", box(0, 0, 600, 2000), 15.0), ("low", box(600, 0, 1500, 2000), 10.0),
                                             ("even", box(-900, 0, 0, 2000), 15.0)]))
        origin = (585.0, 15.0, 1500.0 + dist.PARAPET_HEIGHT)
        near = {r.tops[k][0]["id"] for kind, k in r._supports(origin, reach.REACH_SUPPORT_RADIUS) if kind == "b"}
        self.assertEqual(near, {"tall"})
        origin = (15.0, 15.0, 1500.0 + dist.PARAPET_HEIGHT)
        near = {r.tops[k][0]["id"] for kind, k in r._supports(origin, reach.REACH_SUPPORT_RADIUS) if kind == "b"}
        self.assertEqual(near, {"tall", "even"})

    def test_each_landing_is_its_own_support(self):
        # Standing under a fire escape the first landing (330 cm) is within 250 cm of her and ignored to the release
        # point; the one above it (660 cm) is not, as the game spawns a landing per floor.
        d = FakeDistrict([("tenement", box(0, 0, 1000, 2000), 15.0)])
        escapes = [("tenement", f, (500.0, 0.0, f * 330.0), 180.0, 330.0 if f > 1 else 0.0, 1.0) for f in (1, 2, 3)]
        r = reach.GrappleReach(d, escapes=escapes)
        centre = (500.0, -150.0, dist.SIDEWALK_TOP + reach.REACH_HALF_HEIGHT)
        landings = sorted(r.landings[k][2] for kind, k in r._supports(centre, reach.REACH_START_IGNORE) if kind == "l")
        self.assertEqual(landings, [330.0 - 8.0])


class StreetSpotsLikeTheAuditTest(unittest.TestCase):
    """The model's sidewalk spots are the audit's (FGrappleReachQuery::GatherSidewalkSpots, Hawkeye.Grapple.Reach.SidewalkSpots):
    both passes, every 10 m and every 2.5 m along the map's street lines, at the sidewalk's middle or backed off."""

    ROAD = {"id": "r", "width_m": 12.0, "pieces": [[(0, 0), (3000, 0)]]}

    def test_both_passes_both_sides(self):
        r = reach.GrappleReach(FakeDistrict([], roads=[self.ROAD]))
        xs = sorted({round(f[0]) for f in r.street_spots})
        self.assertEqual(xs, list(range(0, 3001, 250)))
        self.assertEqual(len(r.street_spots), 2 * 13)   # the 10 m spots are among the 2.5 m ones
        self.assertEqual({round(abs(f[1])) for f in r.street_spots}, {800})

    def test_a_building_over_the_middle_backs_the_spot_off(self):
        # As the audit's trace: a spot inside a building backs off 100 cm at a time; no 60 cm keep-off from walls.
        r = reach.GrappleReach(FakeDistrict([("b", box(-500, 650, 3500, 2000), 3.0)], roads=[self.ROAD]))
        self.assertEqual({round(f[1]) for f in r.street_spots}, {-800, 600})


class EscapeLeavesTheStreetAWayTest(unittest.TestCase):
    """_reach.escapes_blocking_street: a fire escape that leaves the street no anchor to reach goes."""

    def district(self, front, width):
        kerb = width * 50.0 + dist.SIDEWALK_WIDTH
        return FakeDistrict([("left", box(0, kerb, 900, kerb + 2000), 10.1),
                             ("narrow", box(900, kerb, 900 + front, kerb + 2000), 15.0),
                             ("right", box(900 + front, kerb, 1800 + front, kerb + 2000), 11.9)],
                            roads=[{"id": "ave", "width_m": width, "pieces": [[(-3000, 0), (6000, 0)]]}])

    def test_a_4_m_wall_on_an_avenue_loses_its_escape(self):
        # Lower neighbours, the far sidewalk out of range, the escape across the whole 4.2 m wall: nothing reaches
        # the roof from the pavement with it, so it goes, and then the street reaches it.
        from _fire_escapes import fire_escape_plan, fire_escape_spots
        d = self.district(420, 25.0)
        self.assertIn("narrow", {s[0] for s in fire_escape_plan(d)})
        self.assertNotIn("narrow", {s[0] for s in fire_escape_spots(d)})
        self.assertEqual(d._fire_escapes_dropped, ["narrow"])
        r = reach.grapple_reach(d)
        spots = [s for s in reach.anchor_spots(d) if s[6] == "narrow"]
        self.assertTrue(any(r.targetable(f, s) for s in spots for f in r.street_spots))

    def test_a_wider_wall_on_a_street_keeps_it(self):
        from _fire_escapes import fire_escape_spots
        d = self.district(751, 12.0)
        self.assertIn("narrow", {s[0] for s in fire_escape_spots(d)})
        self.assertEqual(d._fire_escapes_dropped, [])


if __name__ == "__main__":
    unittest.main()
