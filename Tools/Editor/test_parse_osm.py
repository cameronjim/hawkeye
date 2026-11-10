"""Unit tests for parse_osm.py. Plain unittest, no editor and no ``unreal`` module:

    python -B -m unittest discover -s Tools/Editor -p "test_parse_osm.py"
"""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402
import parse_osm as po  # noqa: E402

A, B, C, D = (0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0)


def pts(ring):
    return [{"lat": la, "lon": lo} for la, lo in ring]


def way(wid, ring, **tags):
    return {"type": "way", "id": wid, "tags": tags, "geometry": pts(ring)}


def square(x, y, size=1.0):
    return [(x, y), (x, y + size), (x + size, y + size), (x + size, y), (x, y)]


def member(ring, role):
    return {"type": "way", "role": role, "geometry": pts(ring)}


def canon(ring):
    """A ring as a frozenset of its edges, so start point and direction do not matter."""
    n = len(ring)
    return frozenset(frozenset((ring[i], ring[(i + 1) % n])) for i in range(n))


class StitchRingsTest(unittest.TestCase):
    def test_closed_way_drops_repeated_last_point(self):
        self.assertEqual(po.stitch_rings([[A, B, C, D, A]]), [[A, B, C, D]])

    def test_two_open_ways_join_end_to_end(self):
        rings = po.stitch_rings([[A, B, C], [C, D, A]])
        self.assertEqual(len(rings), 1)
        self.assertEqual(canon(rings[0]), canon([A, B, C, D]))

    def test_reversed_segment_is_flipped_to_fit(self):
        rings = po.stitch_rings([[A, B, C], [A, D, C]])
        self.assertEqual(len(rings), 1)
        self.assertEqual(canon(rings[0]), canon([A, B, C, D]))

    def test_segment_joining_at_the_start_of_the_ring(self):
        rings = po.stitch_rings([[B, C], [A, B], [C, D, A]])
        self.assertEqual(len(rings), 1)
        self.assertEqual(canon(rings[0]), canon([A, B, C, D]))

    def test_three_ways_in_any_order(self):
        ways = [[A, B], [B, C], [C, D, A]]
        for order in ([0, 1, 2], [2, 0, 1], [1, 2, 0], [2, 1, 0]):
            rings = po.stitch_rings([ways[i] for i in order])
            self.assertEqual(len(rings), 1)
            self.assertEqual(canon(rings[0]), canon([A, B, C, D]))

    def test_unclosable_fragment_is_dropped(self):
        self.assertEqual(po.stitch_rings([[A, B, C]]), [])

    def test_fragment_dropped_but_other_ring_kept(self):
        rings = po.stitch_rings([[A, B, C], square(5, 5)])
        self.assertEqual(len(rings), 1)
        self.assertIn((5, 5), rings[0])

    def test_closed_ring_too_small_is_dropped(self):
        self.assertEqual(po.stitch_rings([[A, B, A]]), [])

    def test_single_point_ways_are_ignored(self):
        self.assertEqual(po.stitch_rings([[A], []]), [])

    def test_two_separate_rings(self):
        self.assertEqual(len(po.stitch_rings([square(0, 0), square(5, 5)])), 2)

    def test_does_not_mutate_input(self):
        ways = [[A, B, C], [C, D, A]]
        po.stitch_rings(ways)
        self.assertEqual(ways, [[A, B, C], [C, D, A]])


class DistrictRectTest(unittest.TestCase):
    CFG = {"west": "West St", "east": "East St", "south": "South St", "north": "North St"}
    LAT0, LON0 = 40.0, -73.0

    def grid(self):
        # Axis-aligned in lat/lon: the avenues are constant lon, the streets constant lat.
        return [
            way(1, [(40.000, -73.001), (40.002, -73.001)], name="West St", highway="residential"),
            way(2, [(40.000, -72.999), (40.002, -72.999)], name="East St", highway="residential"),
            way(3, [(39.999, -73.002), (39.999, -72.998)], name="South St", highway="residential"),
            way(4, [(40.001, -73.002), (40.001, -72.998)], name="North St", highway="residential"),
        ]

    def test_corners_are_street_intersections(self):
        corners = po.district_rect(self.grid(), self.CFG, self.LAT0, self.LON0)
        self.assertEqual(sorted(corners), ["north_east", "north_west", "south_east", "south_west"])
        expect = {"south_west": (39.999, -73.001), "south_east": (39.999, -72.999),
                  "north_west": (40.001, -73.001), "north_east": (40.001, -72.999)}
        for key, (la, lo) in expect.items():
            x, y = geo.project(la, lo, self.LAT0, self.LON0)
            self.assertAlmostEqual(corners[key][0], x, places=3)
            self.assertAlmostEqual(corners[key][1], y, places=3)

    def test_missing_street_exits(self):
        elements = [e for e in self.grid() if e["tags"]["name"] != "North St"]
        with self.assertRaises(SystemExit):
            po.district_rect(elements, self.CFG, self.LAT0, self.LON0)

    def test_non_road_ways_with_the_name_are_ignored(self):
        elements = self.grid()
        for e in elements:
            if e["tags"]["name"] == "North St":
                e["tags"]["highway"] = "footway"
        with self.assertRaises(SystemExit):
            po.district_rect(elements, self.CFG, self.LAT0, self.LON0)


class BuildingRecordsTest(unittest.TestCase):
    def test_way_record_shape(self):
        recs = po.building_records([way(7, square(0, 0), building="yes", height="12")])
        self.assertEqual(len(recs), 1)
        rid, kind, osm_id, outers, holes, tags = recs[0]
        self.assertEqual((rid, kind, osm_id), ("W7", "way", 7))
        self.assertEqual(outers, [[(0, 0), (0, 1), (1, 1), (1, 0)]])
        self.assertEqual(holes, [])
        self.assertEqual(tags["height"], "12")

    def test_non_buildings_and_open_ways_are_skipped(self):
        elements = [way(1, square(0, 0), highway="residential"),
                    way(2, [A, B, C], building="yes"),
                    way(3, [A, B, A], building="yes")]
        self.assertEqual(po.building_records(elements), [])

    def test_height_from_height_tag(self):
        tags = po.building_records([way(1, square(0, 0), building="yes", height="21.5 m")])[0][5]
        self.assertEqual(geo.building_height_m(tags), (21.5, "height"))

    def test_height_from_levels(self):
        tags = po.building_records([way(1, square(0, 0), building="yes", **{"building:levels": "5"})])[0][5]
        h, source = geo.building_height_m(tags)
        self.assertEqual(source, "levels")
        self.assertAlmostEqual(h, 5 * 3.2 + 1.5)

    def test_height_tag_wins_over_levels(self):
        tags = po.building_records([way(1, square(0, 0), building="yes", height="30",
                                        **{"building:levels": "5"})])[0][5]
        self.assertEqual(geo.building_height_m(tags), (30.0, "height"))

    def test_height_default(self):
        tags = po.building_records([way(1, square(0, 0), building="yes")])[0][5]
        self.assertEqual(geo.building_height_m(tags), (15.0, "default"))

    def test_multipolygon_outer_and_inner(self):
        rel = {"type": "relation", "id": 9, "tags": {"building": "yes"}, "members": [
            member(square(0, 0, 10), "outer"), member(square(4, 4, 2), "inner")]}
        recs = po.building_records([rel])
        self.assertEqual(len(recs), 1)
        rid, kind, osm_id, outers, holes, _tags = recs[0]
        self.assertEqual((rid, kind, osm_id), ("R9", "relation", 9))
        self.assertEqual(len(outers), 1)
        self.assertEqual(len(holes), 1)
        self.assertIn((0, 0), outers[0])
        self.assertIn((4, 4), holes[0])

    def test_multipolygon_outer_stitched_from_two_ways(self):
        rel = {"type": "relation", "id": 9, "tags": {"building": "yes"}, "members": [
            member([A, B, C], "outer"), member([C, D, A], "outer")]}
        outers = po.building_records([rel])[0][3]
        self.assertEqual(len(outers), 1)
        self.assertEqual(canon(outers[0]), canon([A, B, C, D]))

    def test_multipolygon_with_two_outers(self):
        rel = {"type": "relation", "id": 9, "tags": {"building": "yes"}, "members": [
            member(square(0, 0), "outer"), member(square(5, 5), "outer")]}
        self.assertEqual(len(po.building_records([rel])[0][3]), 2)

    def test_multipolygon_without_closable_outer_is_skipped(self):
        rel = {"type": "relation", "id": 9, "tags": {"building": "yes"},
               "members": [member([A, B, C], "outer")]}
        self.assertEqual(po.building_records([rel]), [])

    def test_output_follows_input_order(self):
        # building_records itself does not sort; main() sorts by id.
        elements = [way(5, square(0, 0), building="yes"), way(2, square(3, 3), building="yes")]
        self.assertEqual([r[0] for r in po.building_records(elements)], ["W5", "W2"])


class RoadWidthTest(unittest.TestCase):
    def test_street_default(self):
        self.assertEqual(po.road_width({"highway": "residential"}), (12.0, "street default"))

    def test_avenue_by_class(self):
        for hw in ("primary", "secondary", "tertiary"):
            self.assertEqual(po.road_width({"highway": hw}), (25.0, "avenue default"))

    def test_avenue_by_name(self):
        tags = {"highway": "residential", "name": "Avenue C"}
        self.assertEqual(po.road_width(tags), (25.0, "avenue default"))

    def test_explicit_width_wins(self):
        self.assertEqual(po.road_width({"highway": "primary", "width": "18 m"}), (18.0, "width"))

    def test_width_in_feet(self):
        w, source = po.road_width({"highway": "residential", "width": "40 ft"})
        self.assertEqual(source, "width")
        self.assertAlmostEqual(w, 12.192)

    def test_out_of_range_width_is_ignored(self):
        self.assertEqual(po.road_width({"highway": "residential", "width": "2"})[1], "street default")
        self.assertEqual(po.road_width({"highway": "residential", "width": "100"})[1], "street default")

    def test_unparseable_width_is_ignored(self):
        self.assertEqual(po.road_width({"highway": "residential", "width": "wide"})[1], "street default")

    def test_no_tags(self):
        self.assertEqual(po.road_width({}), (12.0, "street default"))


class ParkTest(unittest.TestCase):
    def test_single_way_keeps_plain_id(self):
        out = po.park_rings([way(10, square(0, 0), leisure="park", name="P")])
        self.assertEqual([(i, n) for i, n, _r in out], [("W10", "P")])

    def test_single_ring_relation_keeps_plain_id(self):
        rel = {"type": "relation", "id": 4, "tags": {"leisure": "park"},
               "members": [member(square(0, 0), "outer")]}
        self.assertEqual([i for i, _n, _r in po.park_rings([rel])], ["R4"])

    def test_multi_ring_relation_gets_unique_stable_ids(self):
        rel = {"type": "relation", "id": 4, "tags": {"leisure": "park", "name": "Two"}, "members": [
            member(square(0, 0), "outer"), member(square(5, 5), "outer"),
            member(square(2, 2, 0.5), "inner")]}
        out = po.park_rings([rel])
        self.assertEqual([i for i, _n, _r in out], ["R4_1", "R4_2"])
        self.assertEqual({n for _i, n, _r in out}, {"Two"})
        self.assertEqual(out, po.park_rings([rel]))

    def test_non_parks_are_ignored(self):
        self.assertEqual(po.park_rings([way(1, square(0, 0), leisure="pitch")]), [])

    def test_sort_by_id_then_geometry(self):
        a = {"id": "W9", "name": None, "outer": [[1, 1]]}
        b = {"id": "W10", "name": None, "outer": [[2, 2]]}
        c1 = {"id": "W9", "name": None, "outer": [[0, 5]]}
        self.assertEqual(po.sort_parks([a, b, c1]), [b, c1, a])
        self.assertEqual(po.sort_parks([c1, a, b]), po.sort_parks([b, a, c1]))


class MainTest(unittest.TestCase):
    """End to end on a tiny hand-built download: output is sorted and repeatable."""

    CFG = {"title": "t", "west": "West St", "east": "East St", "south": "South St",
           "north": "North St", "margin_m": 10.0}

    def elements(self):
        streets = DistrictRectTest().grid()
        # Rings inside the district (lat 39.999..40.001, lon -73.001..-72.999).
        b1 = way(20, square(40.0002, -73.0002, 0.0001), building="yes")
        b2 = way(5, square(40.0004, -73.0002, 0.0001), building="yes")
        b3 = way(11, square(40.0006, -73.0002, 0.0001), building="yes")
        park = {"type": "relation", "id": 3, "tags": {"leisure": "park", "name": "Multi"}, "members": [
            member(square(40.0006, -73.0006, 0.0001), "outer"),
            member(square(40.0002, -73.0006, 0.0001), "outer")]}
        return streets + [b1, b2, b3, park]

    def run_main(self, elements):
        po.DISTRICTS["t_tiny"] = self.CFG
        try:
            with tempfile.TemporaryDirectory() as tmp:
                raw = os.path.join(tmp, "t_tiny.json")
                with open(raw, "w") as f:
                    json.dump({"elements": elements}, f)
                with contextlib.redirect_stdout(io.StringIO()):
                    po.main(raw)
                with open(os.path.join(tmp, "t_tiny.buildings.json")) as f:
                    buildings = json.load(f)
                with open(os.path.join(tmp, "t_tiny.streets.json")) as f:
                    streets = json.load(f)
        finally:
            del po.DISTRICTS["t_tiny"]
        return buildings, streets

    def test_sorted_and_deterministic(self):
        els = self.elements()
        b1, s1 = self.run_main(els)
        b2, s2 = self.run_main(list(reversed(els)))
        self.assertEqual([b["id"] for b in b1["buildings"]], ["W11", "W20", "W5"])
        self.assertEqual([r["id"] for r in s1["roads"]], ["W1", "W2", "W3", "W4"])
        self.assertEqual([p["id"] for p in s1["parks"]], ["R3_1", "R3_2"])
        self.assertEqual(b1["buildings"], b2["buildings"])
        self.assertEqual(s1["roads"], s2["roads"])
        self.assertEqual(s1["parks"], s2["parks"])


if __name__ == "__main__":
    unittest.main()
