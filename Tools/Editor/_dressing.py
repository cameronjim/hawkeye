"""December dressing for one hero block: the plan half (docs/plans/december-dressing.md).

``plan_block_dressing(district, block_id)`` lays out, from the same OpenStreetMap records generate_city.py builds the
district from, the winter set dressing of one city block as plain records: string lights spanning the street between
facing facades, wreaths over the doors, lit windows and window candles, roof snow caps and parapet snow, snow banks at
the kerbs, a Christmas tree stand on one corner, steam vents, snow lumps on the parked cars and awnings and shop-sign
blanks over the shops. Greybox primitives only (box, cylinder, sphere, spline). The engine half spawns one actor per
record and owns nothing else: see the record schema in the plan doc and ``write_plan``.

Two layers, so the rules test without the district:

* ``gather_scene(district)`` reads the district once into plain data (buildings, roads, POIs, fire escapes, ledges,
  anchors and the lines a player on the pavement grapples them along, lamps and street clutter, and the keep-outs:
  chapter 1's chase waypoints, chapter 2's toy drive and the boss arena). It is the only part that calls the other
  planners and the only slow part (about two minutes, the chase's planning world); cached on the district.
* everything else is pure over that scene: ``partition_blocks``, ``hero_block_id``, ``plan_scene_dressing`` and the
  constraint predicates (``blocks_anchor_line``, ``in_keepout``, ``overlaps_structure``, ``clears_street``,
  ``within_density_caps``) each test takes directly.

Deterministic: candidates are generated in a stable order (sorted OSM ids, edge indices, distances along an edge) and
every random-looking choice is ``stable_hash`` of the record's stable id, so the same scene gives byte-identical JSON.

No ``unreal``.
"""

import io
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402
from _geomutil import _segment_params, closest_point_on_polyline, ring_distance, stable_hash  # noqa: E402

DRESSING_VERSION = 1

# --- dimensions shared with the generator (copied, not imported, so the pure layer has no generator import) ---------
ROAD_TOP = 2.0                  # _district.ROAD_TOP
SIDEWALK_TOP = 15.0             # _district.SIDEWALK_TOP
PARAPET_HEIGHT = 90.0           # _district.PARAPET_HEIGHT
PARAPET_THICK = 30.0            # _district.PARAPET_THICK
PARAPET_MIN_HEIGHT_M = 6.0      # _district.PARAPET_MIN_HEIGHT_M
FLOOR = 330.0                   # _fire_escapes.FIRE_ESCAPE_FLOOR: one storey

# --- blocks --------------------------------------------------------------------------------------------------------
BLOCK_LINK = 2500.0             # cm; footprints this close with no street centre line between them share a block
FRONTAGE_MIN_EDGE = 300.0       # cm; shorter walls are not frontage
FRONTAGE_ROAD_REACH = 1200.0    # cm past a road's kerb a wall still fronts it (_reach.FACADE_ANCHOR_ROAD_REACH)
FRONTAGE_FACING = 0.5           # the wall's outward normal within 60 degrees of the road (_reach.FIRE_ESCAPE_FACING)
FRONTAGE_COVER_PROBE = 150.0    # cm out from a wall a neighbour standing there covers it

# --- the constraints -----------------------------------------------------------------------------------------------
LINE_MARGIN = 20.0              # cm every dressing volume is grown by before an anchor line is tested against it
SIGHT_TOLERANCE = 60.0          # cm short of the marker a sight line may be blocked (_reach.REACH_SIGHT_TOLERANCE)
WAYPOINT_KEEPOUT = 150.0        # cm round a chase waypoint, from 30 cm under it to 250 cm over it
SITE_MARGIN = 150.0             # cm round the toy drive's props and people
COURIER_KEEPOUT = 700.0         # cm round the courier scene's body and cruiser
ARENA_STAND_KEEPOUT = 300.0     # cm round the boss arena's street stand
DOOR_KEEPOUT_ALONG = 200.0      # cm either side of a safehouse or interior door along its wall
DOOR_KEEPOUT_OUT = 250.0        # cm out from the wall
DOOR_KEEPOUT_HIGH = 600.0       # cm up: the door, its sign and the storefront band
STRUCTURE_TOL = 0.5             # cm two volumes may share a face without overlapping
STRING_LIGHT_CLEARANCE = 550.0  # cm from the road or pavement to the lowest point of a string of lights
PROP_CLEAR = 30.0               # cm between a colliding dressing volume and a lamp or street prop

# Per 100 m of the block's frontage, at most this many records of a kind (the density caps).
DENSITY_CAPS = {
    "string_light": 10,
    "wreath": 14,
    "window_light": 160,
    "window_candle": 30,
    "roof_snow": 40,
    "parapet_snow": 120,
    "snow_bank": 30,
    "tree_stand": 8,
    "tree_for_sale": 20,
    "steam_vent": 3,
    "car_snow": 12,
    "awning": 14,
    "shop_sign": 14,
}

_SCENE = {}


# ======================================================================================================================
# geometry
# ======================================================================================================================


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _unit(a, b):
    d = _dist2(a, b)
    return ((b[0] - a[0]) / d, (b[1] - a[1]) / d) if d > 1e-9 else (1.0, 0.0)


def _yaw(u):
    return math.degrees(math.atan2(u[1], u[0]))


def _r(v, nd=2):
    """Round for the plan: cm to 0.01 so the JSON is short and the same on every machine."""
    out = round(float(v), nd)
    return 0.0 if out == 0.0 else out


def box_ring(cx, cy, half_x, half_y, yaw):
    """The four (x, y) corners of a box turned yaw degrees, local +X first."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return [(cx + c * a - s * b, cy + s * a + c * b) for a, b in ((-half_x, -half_y), (half_x, -half_y),
                                                                     (half_x, half_y), (-half_x, half_y))]


def circle_ring(cx, cy, radius, sides=8):
    """A polygon round a circle (its edges outside it)."""
    r = radius / math.cos(math.pi / sides)
    return [(cx + r * math.cos(2.0 * math.pi * k / sides), cy + r * math.sin(2.0 * math.pi * k / sides))
            for k in range(sides)]


def _grow_ring(ring, cm):
    """A convex ring pushed out by cm along each vertex's direction from the centroid (enough for the small
    convex volumes dressing is made of)."""
    cx = sum(p[0] for p in ring) / len(ring)
    cy = sum(p[1] for p in ring) / len(ring)
    out = []
    for x, y in ring:
        d = math.hypot(x - cx, y - cy)
        f = (d + cm * 1.4143) / d if d > 1e-9 else 1.0
        out.append((cx + (x - cx) * f, cy + (y - cy) * f))
    return out


def _segments_intersect(a, b, p, q):
    def orient(o, s, t):
        return (s[0] - o[0]) * (t[1] - o[1]) - (s[1] - o[1]) * (t[0] - o[0])
    d1, d2 = orient(p, q, a), orient(p, q, b)
    d3, d4 = orient(a, b, p), orient(a, b, q)
    return ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0))


def rings_overlap(a, b):
    """Two simple polygons share area: an edge of one crosses an edge of the other, or one holds the other."""
    ba, bb = geo.bounds(a), geo.bounds(b)
    if ba[0] > bb[2] or bb[0] > ba[2] or ba[1] > bb[3] or bb[1] > ba[3]:
        return False
    na, nb = len(a), len(b)
    for i in range(na):
        p, q = a[i], a[(i + 1) % na]
        for j in range(nb):
            if _segments_intersect(p, q, b[j], b[(j + 1) % nb]):
                return True
    cxa = (sum(p[0] for p in a) / na, sum(p[1] for p in a) / na)
    cxb = (sum(p[0] for p in b) / nb, sum(p[1] for p in b) / nb)
    return (geo.point_in_polygon(a[0], b) or geo.point_in_polygon(b[0], a)
            or geo.point_in_polygon(cxa, b) or geo.point_in_polygon(cxb, a))


def prisms_overlap(p, q, tol=STRUCTURE_TOL):
    """Two prisms (ring, z0, z1) share volume deeper than tol in height (touching faces do not count)."""
    if min(p[2], q[2]) - max(p[1], q[1]) <= tol:
        return False
    return rings_overlap(p[0], q[0])


def segment_hits_prism(a3, b3, prism):
    """(t 0..1) where the segment a3 -> b3 first enters the prism (ring, z0, z1), or None."""
    ring, zlo, zhi = prism
    bx = geo.bounds(ring)
    if bx[0] > max(a3[0], b3[0]) or bx[2] < min(a3[0], b3[0]) or bx[1] > max(a3[1], b3[1]) or bx[3] < min(a3[1], b3[1]):
        return None
    if max(a3[2], b3[2]) < zlo or min(a3[2], b3[2]) > zhi:
        return None
    flat = math.hypot(b3[0] - a3[0], b3[1] - a3[1])
    if flat < 1e-6:
        if geo.point_in_polygon((a3[0], a3[1]), ring):
            lo = (zlo - a3[2]) / (b3[2] - a3[2]) if abs(b3[2] - a3[2]) > 1e-9 else 0.0
            hi = (zhi - a3[2]) / (b3[2] - a3[2]) if abs(b3[2] - a3[2]) > 1e-9 else 1.0
            t0 = max(0.0, min(lo, hi))
            return t0 if t0 <= min(1.0, max(lo, hi)) else None
        return None
    ts = sorted(set(_segment_params((a3[0], a3[1]), (b3[0], b3[1]), ring)))
    for t0, t1 in zip(ts, ts[1:]):
        mid = (a3[0] + (b3[0] - a3[0]) * (t0 + t1) * 0.5, a3[1] + (b3[1] - a3[1]) * (t0 + t1) * 0.5)
        if not geo.point_in_polygon(mid, ring):
            continue
        z0 = a3[2] + (b3[2] - a3[2]) * t0
        z1 = a3[2] + (b3[2] - a3[2]) * t1
        if max(z0, z1) < zlo or min(z0, z1) > zhi:
            continue
        if zlo <= z0 <= zhi:
            return t0
        edge = zhi if z0 > zhi else zlo
        return t0 + (t1 - t0) * (z0 - edge) / (z0 - z1)
    return None


def catenary_points(a3, b3, sag, count=9):
    """count points along a hanging wire from a3 to b3 that dips sag cm under the straight chord at its middle
    (a true catenary: y = c cosh(x / c), c found by bisection; the chord's slope is added linearly, which for the
    small slopes of street wires is the usual engineering approximation)."""
    span = _dist2(a3, b3)
    pts = []
    if span < 1.0 or sag <= 0.0:
        return [tuple(a3[k] + (b3[k] - a3[k]) * i / (count - 1.0) for k in range(3)) for i in range(count)]
    half = span * 0.5
    lo, hi = 1e-3, 1e9
    for _ in range(200):
        c = math.sqrt(lo * hi)
        if c * (math.cosh(half / c) - 1.0) > sag:
            lo = c
        else:
            hi = c
    c = math.sqrt(lo * hi)
    for i in range(count):
        t = i / (count - 1.0)
        x = -half + span * t
        dip = sag - c * (math.cosh(x / c) - 1.0)
        pts.append((a3[0] + (b3[0] - a3[0]) * t, a3[1] + (b3[1] - a3[1]) * t, a3[2] + (b3[2] - a3[2]) * t - dip))
    return pts


# ======================================================================================================================
# the scene: everything the plan reads, as plain data
# ======================================================================================================================


def _building(rec, ring):
    height = rec["height_m"] * 100.0
    parapet = rec["height_m"] >= PARAPET_MIN_HEIGHT_M
    return {"id": rec["id"], "ring": [(float(x), float(y)) for x, y in ring], "height": height,
            "top": height + (PARAPET_HEIGHT if parapet else 0.0), "parapet": parapet,
            "street": (rec.get("tags") or {}).get("addr:street", ""),
            "number": (rec.get("tags") or {}).get("addr:housenumber", ""),
            "kind": (rec.get("tags") or {}).get("building", "")}


def gather_scene(district):
    """The district as plain data for the planner (see the module docstring). Cached on the district."""
    cached = getattr(district, "_dressing_scene", None)
    if cached is not None:
        return cached
    import generate_city as gen  # noqa: PLC0415
    import _arena  # noqa: PLC0415
    import _chase  # noqa: PLC0415
    import _site  # noqa: PLC0415

    buildings = []
    for rec in sorted(district.buildings, key=lambda r: r["id"]):
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) >= 3:
            buildings.append(_building(rec, ring))
    roads = []
    for rec, paths in sorted(gen.road_paths(district), key=lambda rp: rp[0]["id"]):
        for path in paths:
            for a, b in zip(path, path[1:]):
                if _dist2(a, b) >= 1.0:
                    roads.append({"road": rec["id"], "name": rec.get("name") or rec["id"], "a": tuple(a), "b": tuple(b),
                                  "half": rec["width_m"] * 50.0})
    pois = []
    for p in sorted(getattr(district, "pois", None) or _district_pois(district), key=lambda p: p["id"]):
        x, y = district.cm((p["lat"], p["lon"]))
        pois.append({"id": p["id"], "at": (x, y), "amenity": p.get("amenity"), "shop": p.get("shop"),
                     "name": p.get("name")})

    reach = gen.grapple_reach(district)
    escapes = [{"osm": district_osm, "ring": [tuple(p) for p in ring], "z0": z0, "z1": z1}
               for district_osm, (ring, _b, z0, z1) in zip(_escape_ids(district, gen), reach.boxes)]
    ledges = []
    for label, osm, origin, yaw, scale, _a, _b, _h in gen.ledge_spots(district):
        length, depth, height = scale[0] * 100.0, scale[1] * 100.0, scale[2] * 100.0
        ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        vx, vy = -uy, ux
        o = origin
        ring = [(o[0], o[1]), (o[0] + ux * length, o[1] + uy * length),
                (o[0] + ux * length + vx * depth, o[1] + uy * length + vy * depth), (o[0] + vx * depth, o[1] + vy * depth)]
        ledges.append({"id": label, "osm": osm, "ring": ring, "z0": o[2], "z1": o[2] + height})
    anchors = []
    for k, spot in enumerate(gen.anchor_spots(district)):
        x, y, z, yaw, forward, drop, osm = spot
        anchors.append({"id": "A{0}".format(k), "osm": osm, "spot": tuple(spot), "marker": (x, y, z + 20.0),
                        "landing": (x + math.cos(math.radians(yaw)) * forward,
                                    y + math.sin(math.radians(yaw)) * forward, z - drop)})
    lamps = [(x, y, yaw) for x, y, yaw, _s in gen.lamp_spots(district)]
    props = []
    plan = gen.clutter_plan(district)
    for kind in sorted(plan):
        radius = gen.CLUTTER_KINDS[kind][4]
        height = gen.CLUTTER_HEIGHTS.get(kind, 120.0 if kind.startswith("ParkedCar") else 100.0)
        if kind == "Scaffold":
            radius = 160.0
        for x, y, z, yaw, scale in plan[kind]:
            prop = {"kind": kind, "at": (x, y, z), "yaw": yaw, "z1": z + height * scale, "radius": radius * scale}
            if kind in gen.CAR_KINDS:
                # The car's own box (the line model's 245 cm circle would keep snow off the whole kerb).
                prop["size"] = (gen.CAR_SIZE[0] * scale, gen.CAR_SIZE[1] * scale)
                prop["z1"] = z + gen.CAR_SIZE[2] * scale
            props.append(prop)
    for x, y, yaw in lamps:
        hx, hy = x + math.cos(math.radians(yaw)) * gen.LAMP_ARM, y + math.sin(math.radians(yaw)) * gen.LAMP_ARM
        props.append({"kind": "LampPole", "at": (x, y, 0.0), "yaw": yaw, "z1": gen.LAMP_POLE_HEIGHT + 30.0, "radius": 20.0})
        props.append({"kind": "LampHead", "at": (hx, hy, gen.LAMP_POLE_HEIGHT - 40.0), "yaw": yaw,
                      "z1": gen.LAMP_POLE_HEIGHT + 30.0, "radius": 40.0})

    keepouts = []
    chase = _chase.plan_chase(district)
    chase_start = None
    if chase is not None:
        for k, w in enumerate(chase["waypoints"]):
            x, y, z = w["at"]
            keepouts.append({"what": "chase waypoint {0}".format(k), "ring": circle_ring(x, y, WAYPOINT_KEEPOUT),
                             "z0": z - 30.0, "z1": z + 250.0})
        chase_start = tuple(chase["waypoints"][0]["at"])
    site = _site.plan_site(district)
    site_info = None
    if site is not None:
        keepouts.extend(_site_keepouts(site))
        behind = (site["spot"][0] - math.cos(math.radians(site["facing"])) * 150.0,
                  site["spot"][1] - math.sin(math.radians(site["facing"])) * 150.0)
        site_info = {"spot": tuple(site["spot"]), "behind": behind, "avenue": site["avenue"]}
    arena = _arena.plan_arena(district)
    if arena is not None:
        keepouts.append({"what": "boss arena roof", "ring": [tuple(p) for p in arena["outline"]],
                         "z0": arena["roof_z"] - 100.0, "z1": arena["roof_z"] + 1500.0})
        sx, sy, sz = arena["player_start"]
        keepouts.append({"what": "boss arena stand", "ring": circle_ring(sx, sy, ARENA_STAND_KEEPOUT),
                         "z0": sz - 50.0, "z1": sz + 400.0})

    # The doors the game puts in walls: the safehouses and the interior entrance, their signs and the pavement in front.
    doors = [(label, spot) for label, spot in gen.safehouse_spots(district)]
    interior = gen.interior_spot(district)
    if interior is not None:
        doors.append(("interior entrance", interior))
    for label, spot in doors:
        keepouts.append(door_keepout(label, spot["x"], spot["y"], spot["yaw"]))

    scene = {
        "buildings": buildings, "roads": roads, "pois": pois, "escapes": escapes, "ledges": ledges,
        "anchors": anchors, "props": props, "keepouts": keepouts, "chase_start": chase_start, "site": site_info,
        "anchor_lines": anchor_lines(reach, anchors),
    }
    district._dressing_scene = scene
    return scene


def door_keepout(what, x, y, yaw):
    """A door's keep-out: DOOR_KEEPOUT_ALONG either side of it along the wall, DOOR_KEEPOUT_OUT out from the wall,
    from under the pavement to over its sign. ``yaw`` faces out of the wall."""
    ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    half_out = DOOR_KEEPOUT_OUT * 0.5
    return {"what": "door " + what, "ring": box_ring(x + ux * half_out, y + uy * half_out, half_out, DOOR_KEEPOUT_ALONG, yaw),
            "z0": -30.0, "z1": DOOR_KEEPOUT_HIGH}


def anchor_lines(reach, anchors):
    """[line dict] for every (pavement spot, anchor) pair _reach's GrappleReach counts as targetable: the camera's
    ``sight`` line (lens to marker) and the ``zip`` line (launch point to landing, the capsule's feet) as
    GrappleReach.seen and zip_clear test them, with the zip's supporting buildings."""
    import _reach  # noqa: PLC0415
    cells = {}
    for feet in reach.street_spots:
        cells.setdefault((int(math.floor(feet[0] / _reach.REACH_RANGE)), int(math.floor(feet[1] / _reach.REACH_RANGE))),
                         []).append(feet)
    out = []
    drop = _reach.REACH_CAPSULE_HALF + _reach.REACH_FEET_CLEAR
    for anchor in anchors:
        spot = anchor["spot"]
        cx, cy = int(math.floor(spot[0] / _reach.REACH_RANGE)), int(math.floor(spot[1] / _reach.REACH_RANGE))
        near = sorted(f for dx in (-1, 0, 1) for dy in (-1, 0, 1) for f in cells.get((cx + dx, cy + dy), ()))
        landing = _reach._anchor_landing(spot)
        supports = set()
        target = reach.building_at((landing[0], landing[1]))
        if target is not None:
            supports.add(reach.tops[target][0]["id"])
        r = _reach.REACH_SUPPORT_RADIUS
        for i in reach.near(spot[0] - r, spot[1] - r, spot[0] + r, spot[1] + r):
            if ring_distance((spot[0], spot[1]), reach.tops[i][1]) < r:
                supports.add(reach.tops[i][0]["id"])
        supports = sorted(supports)
        for feet in near:
            if not reach.targetable(feet, spot):
                continue
            centre = (feet[0], feet[1], feet[2] + _reach.REACH_HALF_HEIGHT)
            marker = _reach._anchor_marker(spot)
            lens, _fwd = reach.lens(centre, marker)
            tag = "{0}@{1:.0f},{2:.0f}".format(anchor["id"], feet[0], feet[1])
            out.append({"id": tag + "/sight", "anchor": anchor["id"], "osm": anchor["osm"], "kind": "sight",
                        "a": tuple(lens), "b": tuple(marker), "supports": supports})
            a = (feet[0], feet[1], feet[2] + _reach.REACH_HALF_HEIGHT + _reach.REACH_ZIP_LAUNCH - drop)
            b = (landing[0], landing[1], landing[2] + _reach.REACH_HALF_HEIGHT - drop)
            out.append({"id": tag + "/zip", "anchor": anchor["id"], "osm": anchor["osm"], "kind": "zip",
                        "a": a, "b": b, "supports": supports})
    return out


def _district_pois(district):
    import _district  # noqa: PLC0415
    _b, streets = _district.data_paths()
    return _district.load_json(streets).get("pois", [])


def _escape_ids(district, gen):
    """The OSM id of each GrappleReach box, in the same order GrappleReach built them (sorted ids)."""
    ids = sorted({osm for osm, _f, _at, _yaw, _d, _s in gen.fire_escape_spots(district)})
    return ids


def _site_keepouts(site):
    """The toy drive's footprint: its arrival box, the hull round every prop and person in it, and the courier's."""
    out = []
    ax, ay, az = site["arrive"]
    ex, ey, ez = site["arrive_extent"]
    out.append({"what": "toy drive arrival", "ring": [(ax - ex, ay - ey), (ax + ex, ay - ey), (ax + ex, ay + ey),
                                                       (ax - ex, ay + ey)],
                "z0": -100.0, "z1": az + ez + 600.0})
    pts = [p[1][:2] for p in site["props"]] + [c[1][:2] for c in site["crew"]] + [s[:2] for s in site["sacks"]]
    pts += [b[:2] for b in site["bulbs"]] + [site["spot"][:2]]
    hull = _hull(pts)
    if len(hull) >= 3:
        out.append({"what": "toy drive", "ring": _grow_ring(hull, SITE_MARGIN), "z0": -100.0, "z1": 1200.0})
    for what, at in (("courier", site["courier"]), ("cruiser", site["cruiser"])):
        out.append({"what": what, "ring": circle_ring(at[0], at[1], COURIER_KEEPOUT), "z0": -100.0, "z1": 1200.0})
    return out


def _hull(pts):
    pts = sorted(set((round(p[0], 3), round(p[1], 3)) for p in pts))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


# ======================================================================================================================
# blocks
# ======================================================================================================================


def _ring_gap(a, b):
    """(gap cm, point on a, point on b) between two rings; 0 when they overlap."""
    if geo.point_in_polygon(a[0], b) or geo.point_in_polygon(b[0], a):
        return 0.0, a[0], a[0]
    best = None
    la, lb = list(a) + [a[0]], list(b) + [b[0]]
    for p in a:
        d, q = closest_point_on_polyline(p, lb)
        if best is None or d < best[0]:
            best = (d, tuple(p), q)
    for p in b:
        d, q = closest_point_on_polyline(p, la)
        if best is None or d < best[0]:
            best = (d, q, tuple(p))
    return best


def partition_blocks(buildings, roads):
    """{block id: [building ids, sorted]}: footprints within BLOCK_LINK of each other whose closest points (and
    centroids) have no street centre line between them share a block, transitively. A block's id is ``blk_`` and its
    smallest OSM id, so it survives a re-download that keeps the buildings."""
    ids = [b["id"] for b in buildings]
    parent = {i: i for i in ids}

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    boxes = [geo.bounds(b["ring"]) for b in buildings]
    cents = [geo.centroid(b["ring"]) for b in buildings]
    cell = BLOCK_LINK
    grid = {}
    for k, bx in enumerate(boxes):
        for gx in range(int(math.floor(bx[0] / cell)), int(math.floor(bx[2] / cell)) + 1):
            for gy in range(int(math.floor(bx[1] / cell)), int(math.floor(bx[3] / cell)) + 1):
                grid.setdefault((gx, gy), []).append(k)
    segs = [(r["a"], r["b"]) for r in roads]

    def crosses_road(p, q):
        return any(_segments_intersect(p, q, a, b) for a, b in segs)

    for k, bx in enumerate(boxes):
        near = set()
        for gx in range(int(math.floor((bx[0] - cell) / cell)), int(math.floor((bx[2] + cell) / cell)) + 1):
            for gy in range(int(math.floor((bx[1] - cell) / cell)), int(math.floor((bx[3] + cell) / cell)) + 1):
                near.update(grid.get((gx, gy), ()))
        for j in sorted(near):
            if j <= k:
                continue
            ob = boxes[j]
            if ob[0] > bx[2] + cell or bx[0] > ob[2] + cell or ob[1] > bx[3] + cell or bx[1] > ob[3] + cell:
                continue
            if find(ids[k]) == find(ids[j]):
                continue
            gap, p, q = _ring_gap(buildings[k]["ring"], buildings[j]["ring"])
            if gap > BLOCK_LINK:
                continue
            if gap > 1.0 and crosses_road(p, q):
                continue
            if crosses_road(cents[k], cents[j]):
                continue
            ri, rj = find(ids[k]), find(ids[j])
            parent[max(ri, rj)] = min(ri, rj)
    groups = {}
    for i in ids:
        groups.setdefault(find(i), []).append(i)
    return {"blk_" + min(members): sorted(members) for members in groups.values()}


def block_of(blocks, osm):
    for block_id in sorted(blocks):
        if osm in blocks[block_id]:
            return block_id
    return None


def _building_at(buildings, pt):
    for b in buildings:
        bx = geo.bounds(b["ring"])
        if bx[0] <= pt[0] <= bx[2] and bx[1] <= pt[1] <= bx[3] and geo.point_in_polygon(pt, b["ring"]):
            return b
    return None


def hero_block_id(scene, blocks=None):
    """The block December dressing is planned for first: the toy drive's block, else the chase start's.

    The toy drive (chapter 2, "Reindeer Games", on Avenue B) is the one street-level set piece in Act 1 and the one
    the season is about: Kate walks up to a Christmas toy drive and the fight happens on its pavement, so the
    street-level kit this plan is made of (lights over the street, wreaths, shop fronts, snow banks, a tree stand)
    is seen at the distance it was made for, and the block's facades frame the chapter's first shot. Chapter 1's
    chase starts on a roof at night and runs away from its block within seconds; its roofs get snow either way.
    The block is the one holding the building the drive's table has its back to (``site["behind"]``, 1.5 m behind
    the table's spot); with no toy drive, the block holding the building under the chase's first waypoint.
    """
    blocks = blocks if blocks is not None else partition_blocks(scene["buildings"], scene["roads"])
    for key in ("site", "chase"):
        if key == "site" and scene.get("site"):
            b = _building_at(scene["buildings"], scene["site"]["behind"])
        elif key == "chase" and scene.get("chase_start"):
            b = _building_at(scene["buildings"], scene["chase_start"][:2])
        else:
            b = None
        if b is not None:
            return block_of(blocks, b["id"])
    return None


# ======================================================================================================================
# frontages: the block's street walls
# ======================================================================================================================


def block_frontages(scene, block_ids):
    """[frontage dict] in a stable order (OSM id, edge index): every wall of a block building FRONTAGE_MIN_EDGE or
    longer whose outward normal faces a road within FRONTAGE_ROAD_REACH of its kerb and that no building stands in
    front of. ``kerb`` is the distance out from the wall to the kerb line (negative where the tagged width runs the
    carriageway under the facades, as the avenues' 25 m default does)."""
    ids = set(block_ids)
    out = []
    for b in scene["buildings"]:
        if b["id"] not in ids:
            continue
        ring = b["ring"]
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        n = len(ring)
        for i in range(n):
            a, c = ring[i], ring[(i + 1) % n]
            length = _dist2(a, c)
            if length < FRONTAGE_MIN_EDGE:
                continue
            u = ((c[0] - a[0]) / length, (c[1] - a[1]) / length)
            nrm = (u[1] * inward, -u[0] * inward)
            covered = 0
            for t in (0.25, 0.5, 0.75):
                q = (a[0] + u[0] * length * t + nrm[0] * FRONTAGE_COVER_PROBE,
                     a[1] + u[1] * length * t + nrm[1] * FRONTAGE_COVER_PROBE)
                if _building_at(scene["buildings"], q) is not None:
                    covered += 1
            if covered >= 2:
                continue
            mid = ((a[0] + c[0]) * 0.5, (a[1] + c[1]) * 0.5)
            best = None
            for k, road in enumerate(scene["roads"]):
                d, q = closest_point_on_polyline(mid, [road["a"], road["b"]])
                if d < 1.0 or d > road["half"] + FRONTAGE_ROAD_REACH:
                    continue
                if ((q[0] - mid[0]) * nrm[0] + (q[1] - mid[1]) * nrm[1]) / d < FRONTAGE_FACING:
                    continue
                key = (d, k)
                if best is None or key < best[0]:
                    best = (key, road, q)
            if best is None:
                continue
            _key, road, q = best
            out_dist = (q[0] - mid[0]) * nrm[0] + (q[1] - mid[1]) * nrm[1]
            out.append({"osm": b["id"], "edge": i, "a": a, "b": c, "length": length, "u": u, "n": nrm,
                        "road": road["name"], "half": road["half"], "centre": out_dist, "kerb": out_dist - road["half"],
                        "height": b["height"], "top": b["top"]})
    return out


def frontage_point(f, along, out=0.0):
    return (f["a"][0] + f["u"][0] * along + f["n"][0] * out, f["a"][1] + f["u"][1] * along + f["n"][1] * out)


def _across(scene, block_ids, p, n, reach):
    """(distance, building) of the first building outside the block straight out from p along n, or None."""
    q = (p[0] + n[0] * reach, p[1] + n[1] * reach)
    best = None
    ids = set(block_ids)
    for b in scene["buildings"]:
        if b["id"] in ids:
            continue
        bx = geo.bounds(b["ring"])
        if bx[0] > max(p[0], q[0]) or bx[2] < min(p[0], q[0]) or bx[1] > max(p[1], q[1]) or bx[3] < min(p[1], q[1]):
            continue
        for t in _segment_params(p, q, b["ring"]):
            if t * reach > 100.0 and (best is None or t < best[0]):
                best = (t, b)
    if best is None:
        return None
    return best[0] * reach, best[1]


# ======================================================================================================================
# records
# ======================================================================================================================

MESHES = {"box": "/Engine/BasicShapes/Cube", "cylinder": "/Engine/BasicShapes/Cylinder",
          "sphere": "/Engine/BasicShapes/Sphere", "spline": None}

# kind: (shape, collision, coating, material). A coating lies on its host building's surface (roof or parapet top)
# and counts as part of it for the zip test, as _reach's zip_clear counts the anchor's own roof.
KINDS = {
    "string_light": ("spline", False, False, "bulb_string"),
    "wreath": ("cylinder", False, False, "evergreen"),
    "window_light": ("box", False, False, "warm_window"),
    "window_candle": ("cylinder", False, False, "candle"),
    "roof_snow": ("spline", False, True, "snow"),
    "parapet_snow": ("box", False, True, "snow"),
    "snow_bank": ("sphere", True, False, "snow"),
    "tree_stand": ("box", True, False, "lumber"),
    "tree_for_sale": ("sphere", True, False, "evergreen"),
    "steam_vent": ("cylinder", True, False, "steam_stack"),
    "car_snow": ("sphere", False, False, "snow"),
    "awning": ("box", False, False, "awning"),
    "shop_sign": ("box", False, False, "sign_blank"),
}
KIND_ORDER = tuple(KINDS)


def make_record(kind, rid, position, size=None, yaw=0.0, pitch=0.0, host=None, params=None):
    """One placement. ``size`` is the primitive's extent in cm (local X, Y, Z); ``scale`` is that over the engine's
    100 cm basic shapes, so the engine half spawns ``mesh`` at ``position`` (the shape's centre) with
    Rotator(pitch, yaw, roll) and ``scale``. Splines carry their points in ``params`` and scale 1."""
    shape, collision, coating, material = KINDS[kind]
    size = size or (100.0, 100.0, 100.0)
    return {
        "id": rid,
        "kind": kind,
        "shape": shape,
        "mesh": MESHES[shape],
        "position": [_r(position[0]), _r(position[1]), _r(position[2])],
        "yaw": _r(yaw, 3),
        "pitch": _r(pitch, 3),
        "roll": 0.0,
        "scale": [1.0, 1.0, 1.0] if shape == "spline" else [_r(s / 100.0, 4) for s in size],
        "collision": collision,
        "coating": coating,
        "material": material,
        "host": host,
        "params": params or {},
    }


def _axes(yaw, pitch, roll=0.0):
    """Unreal's FRotationMatrix axes (X, Y, Z) for Rotator(pitch, yaw, roll)."""
    sp, cp = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    sy, cy = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sr, cr = math.sin(math.radians(roll)), math.cos(math.radians(roll))
    x = (cp * cy, cp * sy, sp)
    y = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    z = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    return x, y, z


def string_envelope(params):
    """cm round a string's points its wire and the bulbs hanging under it reach: the wire's radius plus the bulbs'
    drop and size (a little more than they reach under it, the same all round)."""
    return params.get("radius", 2.0) + params.get("bulb_drop", 0.0) + params.get("bulb_size", 0.0)


def record_volumes(rec, grow=0.0):
    """The record's space as prisms (ring, z0, z1): its rotated box's footprint hull and height range for box,
    cylinder and sphere (conservative for the round ones), a box round each segment of an open spline, and the
    outline with its thickness for a closed one. ``grow`` pushes every face out by that many cm."""
    if rec["shape"] == "spline":
        p = rec["params"]
        pts = p["points"]
        if p.get("closed"):
            ring = [(q[0], q[1]) for q in pts]
            z = rec["position"][2]
            return [(_grow_ring(ring, grow) if grow else ring, z - grow, z + p["thickness"] + grow)]
        r = string_envelope(p) + grow
        out = []
        for a, b in zip(pts, pts[1:]):
            half = _dist2(a, b) * 0.5
            mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
            ring = box_ring(mid[0], mid[1], half + r, r, _yaw(_unit(a, b)))
            out.append((ring, min(a[2], b[2]) - r, max(a[2], b[2]) + r))
        return out
    sx, sy, sz = (s * 50.0 + grow for s in rec["scale"])
    ax, ay, az = _axes(rec["yaw"], rec["pitch"], rec["roll"])
    cx, cy, cz = rec["position"]
    corners = []
    for i in (-1.0, 1.0):
        for j in (-1.0, 1.0):
            for k in (-1.0, 1.0):
                corners.append((cx + ax[0] * sx * i + ay[0] * sy * j + az[0] * sz * k,
                                cy + ax[1] * sx * i + ay[1] * sy * j + az[1] * sz * k,
                                cz + ax[2] * sx * i + ay[2] * sy * j + az[2] * sz * k))
    ring = _hull([(c[0], c[1]) for c in corners])
    return [(ring, min(c[2] for c in corners), max(c[2] for c in corners))]


# ======================================================================================================================
# the constraints (each a pure predicate the tests call directly)
# ======================================================================================================================

GROUND_DEPTH = 100.0            # cm under the ground a building's prism starts


def blocks_anchor_line(rec, line):
    """True when the record stands in a grapple line a player on the pavement has to an anchor (_reach's model:
    ``sight`` from the hip camera's lens to the marker, blocked short of SIGHT_TOLERANCE; ``zip`` from the launch
    point to the landing, where coatings on the anchor's supporting roofs count as that roof). Every volume is
    grown by LINE_MARGIN first."""
    if line["kind"] == "zip" and rec["coating"] and rec["host"] in line["supports"]:
        return False
    a, b = line["a"], line["b"]
    length = math.sqrt(sum((b[k] - a[k]) ** 2 for k in range(3)))
    for prism in record_volumes(rec, LINE_MARGIN):
        t = segment_hits_prism(a, b, prism)
        if t is None:
            continue
        if line["kind"] == "sight" and (1.0 - t) * length <= SIGHT_TOLERANCE:
            continue
        return True
    return False


def in_keepout(rec, keepouts):
    """The keep-out (chase waypoint, toy drive, boss arena) the record reaches into, or None."""
    vols = record_volumes(rec)
    for k in keepouts:
        prism = (k["ring"], k["z0"], k["z1"])
        if any(prisms_overlap(v, prism) for v in vols):
            return k["what"]
    return None


def overlaps_structure(rec, scene):
    """What fixed structure the record overlaps: a fire escape's box (_reach's: landings, rails and ladders), a
    traversable ledge slab, or a building other than its host; None when clear."""
    vols = record_volumes(rec)
    for e in scene["escapes"]:
        prism = (e["ring"], e["z0"], e["z1"])
        if any(prisms_overlap(v, prism) for v in vols):
            return "fire escape " + e["osm"]
    for led in scene["ledges"]:
        prism = (led["ring"], led["z0"], led["z1"])
        if any(prisms_overlap(v, prism) for v in vols):
            return "ledge " + led["id"]
    for b in scene["buildings"]:
        if b["id"] == rec["host"]:
            continue
        prism = (b["ring"], -GROUND_DEPTH, b["top"])
        if any(prisms_overlap(v, prism) for v in vols):
            return "building " + b["id"]
    return None


def prop_ring(p):
    """A street prop's footprint: its box for a parked car (``size``, turned by its yaw), else its circle."""
    x, y = p["at"][0], p["at"][1]
    if p.get("size"):
        return box_ring(x, y, p["size"][0] * 0.5, p["size"][1] * 0.5, p["yaw"])
    return circle_ring(x, y, p["radius"])


def prop_id(p):
    return "{0}@{1:.0f},{2:.0f}".format(p["kind"], p["at"][0], p["at"][1])


def hits_prop(rec, props):
    """The lamp or street prop (cylinders from the clutter plan) the record comes within PROP_CLEAR of, or None.
    ``params.on_prop`` names the one prop it is meant to sit on (a car's snow); coatings lie under the roof props."""
    if rec["coating"]:
        return None
    own = rec["params"].get("on_prop")
    vols = record_volumes(rec, PROP_CLEAR)
    for p in props:
        if own is not None and own == prop_id(p):
            continue
        prism = (prop_ring(p), p["at"][2], p["z1"])
        if any(prisms_overlap(v, prism) for v in vols):
            return prop_id(p)
    return None


def clears_street(rec):
    """A string of lights hangs STRING_LIGHT_CLEARANCE or more over the pavement (the higher of the road and the
    pavement) at every point; every other kind passes."""
    if rec["kind"] != "string_light":
        return True
    lowest = min(p[2] for p in rec["params"]["points"]) - string_envelope(rec["params"])
    return lowest - SIDEWALK_TOP >= STRING_LIGHT_CLEARANCE


def density_limits(frontage_m):
    return {kind: int(math.floor(cap * frontage_m / 100.0)) for kind, cap in DENSITY_CAPS.items()}


def within_density_caps(records, frontage_m):
    limits = density_limits(frontage_m)
    counts = {}
    for rec in records:
        counts[rec["kind"]] = counts.get(rec["kind"], 0) + 1
    return all(counts[k] <= limits.get(k, 0) for k in counts)


def apply_density_caps(records, frontage_m):
    """The records with each kind thinned to its cap: the ones whose ids hash lowest stay (stable, and spread along
    the block rather than the first ones on it)."""
    limits = density_limits(frontage_m)
    by_kind = {}
    for rec in records:
        by_kind.setdefault(rec["kind"], []).append(rec)
    keep = set()
    for kind, recs in by_kind.items():
        ranked = sorted(recs, key=lambda r: (stable_hash(r["id"], "cap"), r["id"]))
        keep.update(r["id"] for r in ranked[:limits.get(kind, 0)])
    return [r for r in records if r["id"] in keep]


def violations(rec, scene, lines=None):
    """Every constraint the record breaks, as text (empty when it passes all of them)."""
    out = []
    what = in_keepout(rec, scene["keepouts"])
    if what:
        out.append("in " + what)
    what = overlaps_structure(rec, scene)
    if what:
        out.append("overlaps " + what)
    what = hits_prop(rec, scene["props"])
    if what:
        out.append("hits " + what)
    if not clears_street(rec):
        out.append("hangs too low")
    bx = geo.bounds([p for ring, _z0, _z1 in record_volumes(rec, LINE_MARGIN) for p in ring])
    for line in (lines if lines is not None else scene.get("anchor_lines") or ()):
        a, b = line["a"], line["b"]
        if max(a[0], b[0]) < bx[0] or min(a[0], b[0]) > bx[2] or max(a[1], b[1]) < bx[1] or min(a[1], b[1]) > bx[3]:
            continue
        if blocks_anchor_line(rec, line):
            out.append("blocks {0} line to {1}".format(line["kind"], line["anchor"]))
            break
    return out


# ======================================================================================================================
# the plan
# ======================================================================================================================

# windows: on the window grid M_Facade paints (_materials.BAY_PITCH, FLOOR_PITCH, WINDOW_W/H/SILL,
# UPPER_FLOORS_FROM, CORNICE_FROM_TOP), so a lit card covers a painted window exactly. The material's horizontal
# coordinate is world y * nx - x * ny on a wall whose outward normal is (nx, ny), windows centred on half bays;
# storeys are world z in FLOOR_PITCH steps.
WINDOW_BAY = 160.0              # cm between window centres along a facade (_materials.BAY_PITCH)
WINDOW_END = 90.0               # cm from either end of a wall to the first window's middle
WINDOW_SIZE = (2.0, 110.0, 150.0)   # out, along, up (_materials.WINDOW_W, WINDOW_H)
WINDOW_SILL = 95.0              # cm over the storey line (_materials.WINDOW_SILL)
WINDOW_UPPER_FROM = 420.0       # world z the painted upper floors start at (_materials.UPPER_FLOORS_FROM)
WINDOW_TOP_CLEAR = 185.0        # cm under the top of the wall the painted windows stop (CORNICE_FROM_TOP[1] + 20)
WINDOW_OUT = 1.5                # cm from the wall to the card's middle
LIT_SHARE = 0.35                # of windows lit
CANDLE_SHARE = 0.3              # of lit windows with a candle
CANDLE_SIZE = (5.0, 5.0, 24.0)
CANDLE_OUT = 5.0
# doors
WREATH_SIZE = (55.0, 55.0, 10.0)
WREATH_Z = 180.0                # cm over the ground to the wreath's middle
WREATH_OUT = 6.0
DOOR_END_CLEAR = 120.0          # cm from either end of the wall
# shops
SHOP_AMENITIES = ("restaurant", "bar", "fast_food", "cafe", "pub", "ice_cream", "theatre", "veterinary")
SHOP_REACH = 900.0              # cm from a POI to the wall it is put on
AWNING_WIDTH = 360.0
AWNING_DEPTH = 110.0
AWNING_THICK = 6.0
AWNING_PITCH = -25.0            # degrees: the front edge down
AWNING_BACK_Z = 300.0           # cm over the ground where the awning meets the wall
SIGN_SIZE = (8.0, 300.0, 60.0)
SIGN_Z = 345.0
SIGN_OUT = 5.0
SHOP_SHIFTS = (0.0, -150.0, 150.0, -300.0, 300.0)
AWNING_COLOURS = ("purple", "cream", "grey", "black")
# roofs
ROOF_SNOW_THICK = 8.0
ROOF_SNOW_INSET = 10.0          # cm inside the parapet's inner face (or the roof edge with no parapet)
PARAPET_SNOW_PIECE = 300.0      # cm, the longest piece of parapet snow
PARAPET_SNOW_MIN_EDGE = 100.0
PARAPET_SNOW_THICK = 6.0
PARAPET_SNOW_ANCHOR_CLEAR = 80.0  # cm from an anchor's marker: the fitting stays bare
# string lights
STRING_SPACING = 1000.0         # cm between spans along a street face
STRING_END_CLEAR = 400.0        # cm from either end of the face
STRING_HEIGHTS = (720.0, 780.0, 840.0, 660.0)   # attach heights over the ground tried, in order
STRING_REACH = 5000.0           # cm, the longest span
STRING_SAG = 0.025              # of the span
STRING_MIN_SAG = 30.0
STRING_OFF_WALL = 20.0          # cm out from each wall the string is hooked: clear of the wall with its bulbs
                                # (string_envelope), so the far building does not count as an overlap
STRING_RADIUS = 2.0
STRING_BULB_DROP = 6.0          # cm a bulb's middle hangs under the wire
STRING_BULB_SIZE = 9.0          # cm across a bulb (bigger than a real C9, so a string reads across the street)
STRING_POINTS = 9
STRING_TOP_CLEAR = 100.0        # cm the attach stays under either building's roof
# snow banks
BANK_STEP = 500.0
BANK_CORNER = 700.0             # cm from a street face's end (the crossing) to the first bank
BANK_MIN_KERB = 250.0           # cm of pavement a bank needs
BANK_SIZE = (220.0, 90.0, 120.0)  # length (plus up to BANK_EXTRA), width, the ellipsoid's full height (half shows)
BANK_EXTRA = 160.0
BANK_GAP_SHARE = 0.3            # of stations left bare
BANK_DOOR_CLEAR = 150.0
BANK_KERB_IN = 35.0             # cm from the kerb to the bank's street side
# tree stand
TREE_CORNER = 500.0             # cm from the face's end to the stand
TREE_MIN_KERB = 260.0
TREE_COUNT = 6
TREE_STEP = 100.0
TREE_SIZE = (70.0, 70.0, 190.0)
TREE_RAIL_IN = 40.0             # cm in from the kerb the rail stands
TREE_RAIL_HEIGHT = 220.0
TREE_MIN_TREES = 4
# steam vents
STEAM_COUNT = 3
STEAM_STEP = 1500.0
STEAM_LANE = 0.25               # of the half width from the centre line, toward the block
STEAM_SIZE = (45.0, 45.0, 200.0)
# cars
CAR_SNOW_SIZE = (260.0, 150.0, 40.0)   # on the cabin (generate_city's car: 250 cm long, set back 20 cm, 140 cm up)
CAR_CABIN_BACK = 20.0
CAR_SNOW_SHARE = 0.5
CAR_SNOW_MAX = 8


def inset_ring(ring, d):
    """The ring moved d cm inward (mitred corners), or None when that does not leave a clean polygon d clear of
    the outline (a notch narrower than 2 d, a spike)."""
    pts = list(geo.oriented(ring, ccw=True))
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        u1, u2 = _unit(p0, p1), _unit(p1, p2)
        n1, n2 = (-u1[1], u1[0]), (-u2[1], u2[0])   # inward (left) normals of a ccw ring
        a1 = (p0[0] + n1[0] * d, p0[1] + n1[1] * d)
        a2 = (p1[0] + n2[0] * d, p1[1] + n2[1] * d)
        hit = geo.intersect_lines(a1, u1, a2, u2)
        if hit is None:
            out.append((p1[0] + n1[0] * d, p1[1] + n1[1] * d))
            continue
        q = (hit[0], hit[1])
        if _dist2(q, p1) > d * 4.0:
            return None
        out.append(q)
    if geo.signed_area(out) <= 0.0:
        return None
    m = len(out)
    for i in range(m):
        for j in range(i + 2, m):
            if i == 0 and j == m - 1:
                continue
            if _segments_intersect(out[i], out[(i + 1) % m], out[j], out[(j + 1) % m]):
                return None
    loop = list(pts) + [pts[0]]
    for q in out:
        if not geo.point_in_polygon(q, pts) or closest_point_on_polyline(q, loop)[0] < d - 0.5:
            return None
    return out


def street_faces(frontages):
    """[face dict]: the block's frontages grouped by road and side (normals within about 25 degrees), each with a
    unit ``u`` along it, ``s`` (cm along u) for both ends of every frontage, and the face's extent s0..s1."""
    faces = []
    for f in frontages:
        for face in faces:
            if face["road"] == f["road"] and f["n"][0] * face["n"][0] + f["n"][1] * face["n"][1] > 0.9:
                face["frontages"].append(f)
                break
        else:
            faces.append({"road": f["road"], "n": f["n"], "frontages": [f]})
    for face in faces:
        nx = sum(f["n"][0] * f["length"] for f in face["frontages"])
        ny = sum(f["n"][1] * f["length"] for f in face["frontages"])
        L = math.hypot(nx, ny)
        face["n"] = (nx / L, ny / L)
        face["u"] = (-face["n"][1], face["n"][0])
        u = face["u"]
        spans = []
        for f in face["frontages"]:
            sa = f["a"][0] * u[0] + f["a"][1] * u[1]
            sb = f["b"][0] * u[0] + f["b"][1] * u[1]
            spans.append((min(sa, sb), max(sa, sb), f))
        spans.sort(key=lambda s: (s[0], s[1], s[2]["osm"], s[2]["edge"]))
        face["spans"] = spans
        face["s0"] = spans[0][0]
        face["s1"] = max(s[1] for s in spans)
        face["id"] = "{0}:{1:.0f}".format(face["road"], (_yaw(face["n"]) + 360.0) % 360.0)
    faces.sort(key=lambda fc: fc["id"])
    return faces


def face_at(face, s):
    """(frontage, cm along it) of the face's frontage covering s, or None (a gap between buildings)."""
    u = face["u"]
    for lo, hi, f in face["spans"]:
        if lo <= s <= hi:
            sa = f["a"][0] * u[0] + f["a"][1] * u[1]
            along = (s - sa) if f["u"][0] * u[0] + f["u"][1] * u[1] > 0 else (sa - s)
            return f, max(0.0, min(f["length"], along))
    return None


def _ground(f):
    return SIDEWALK_TOP if f["kerb"] > 0.0 else ROAD_TOP


class _Ctx(object):
    """What the passes share: the scene, the block, its frontages and faces, the anchor lines near it in a grid,
    and the count of candidates each constraint turned away."""

    CELL = 1000.0

    def __init__(self, scene, block_id, ids):
        self.scene = scene
        self.block_id = block_id
        self.ids = sorted(ids)
        self.buildings = [b for b in scene["buildings"] if b["id"] in set(ids)]
        self.frontages = block_frontages(scene, ids)
        self.faces = street_faces(self.frontages) if self.frontages else []
        self.rejected = {}
        self.doors = []          # (face id, s) of every door, for the snow banks
        pts = [p for b in self.buildings for p in b["ring"]]
        bx = geo.bounds(pts)
        m = STRING_REACH + 1000.0
        self.box = (bx[0] - m, bx[1] - m, bx[2] + m, bx[3] + m)
        self.grid = {}
        self.lines = []
        for line in scene.get("anchor_lines") or ():
            a, b = line["a"], line["b"]
            if max(a[0], b[0]) < self.box[0] or min(a[0], b[0]) > self.box[2] or \
                    max(a[1], b[1]) < self.box[1] or min(a[1], b[1]) > self.box[3]:
                continue
            k = len(self.lines)
            self.lines.append(line)
            for gx, gy in self._cells(min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1])):
                self.grid.setdefault((gx, gy), []).append(k)
        self.keepouts = [k for k in scene["keepouts"] if self._near(k["ring"])]
        self.escapes = [e for e in scene["escapes"] if self._near(e["ring"])]
        self.ledges = [led for led in scene["ledges"] if self._near(led["ring"])]
        self.props = [p for p in scene["props"] if self.box[0] <= p["at"][0] <= self.box[2]
                      and self.box[1] <= p["at"][1] <= self.box[3]]
        self.near_buildings = [b for b in scene["buildings"] if self._near(b["ring"])]
        self.local = {"escapes": self.escapes, "ledges": self.ledges, "buildings": self.near_buildings,
                      "keepouts": self.keepouts, "props": self.props}

    def _cells(self, x0, y0, x1, y1):
        c = self.CELL
        return [(gx, gy) for gx in range(int(math.floor(x0 / c)), int(math.floor(x1 / c)) + 1)
                for gy in range(int(math.floor(y0 / c)), int(math.floor(y1 / c)) + 1)]

    def _near(self, ring):
        b = geo.bounds(ring)
        return not (b[2] < self.box[0] or b[0] > self.box[2] or b[3] < self.box[1] or b[1] > self.box[3])

    def lines_near(self, rec):
        pts = [p for ring, _z0, _z1 in record_volumes(rec, LINE_MARGIN) for p in ring]
        b = geo.bounds(pts)
        ks = set()
        for cell in self._cells(*b):
            ks.update(self.grid.get(cell, ()))
        return [self.lines[k] for k in sorted(ks)]

    def check(self, rec):
        """True when the record passes every constraint; else counts the first it fails."""
        why = violations(rec, self.local, self.lines_near(rec))
        if why:
            reason = why[0].split(" ")[0] + " " + why[0].split(" ")[1]
            bucket = self.rejected.setdefault(rec["kind"], {})
            bucket[reason] = bucket.get(reason, 0) + 1
            return False
        return True

    def rid(self, kind, *parts):
        return "/".join([self.block_id, kind] + [str(p) for p in parts])

    def note(self, kind, reason):
        bucket = self.rejected.setdefault(kind, {})
        bucket[reason] = bucket.get(reason, 0) + 1


def _facade_box(ctx, kind, f, along, out, z, size, rid, params=None):
    p = frontage_point(f, along, out)
    return make_record(kind, rid, (p[0], p[1], z), size, yaw=_yaw(f["n"]), host=f["osm"], params=params)


def window_alongs(f):
    """cm along the frontage of every painted window's middle (M_Facade's grid), WINDOW_END clear of either end."""
    nx, ny = f["n"]
    u0 = f["a"][1] * nx - f["a"][0] * ny
    rate = f["u"][1] * nx - f["u"][0] * ny          # +1 or -1: how the material's coordinate runs along the wall
    lo, hi = sorted((u0, u0 + rate * f["length"]))
    out = []
    j = int(math.floor((lo - WINDOW_BAY * 0.5) / WINDOW_BAY))
    while j * WINDOW_BAY + WINDOW_BAY * 0.5 <= hi:
        along = (j * WINDOW_BAY + WINDOW_BAY * 0.5 - u0) * rate
        if WINDOW_END <= along <= f["length"] - WINDOW_END:
            out.append(along)
        j += 1
    return sorted(out)


def _plan_windows(ctx):
    out = []
    for f in ctx.frontages:
        alongs = window_alongs(f)
        floor = 1
        while floor * FLOOR + WINDOW_SILL + WINDOW_SIZE[2] <= f["top"] - WINDOW_TOP_CLEAR:
            if floor * FLOOR + WINDOW_SILL < WINDOW_UPPER_FROM:
                floor += 1
                continue
            for k, along in enumerate(alongs):
                rid = ctx.rid("window_light", f["osm"], "e{0}".format(f["edge"]), "f{0}".format(floor), "w{0}".format(k))
                if stable_hash(rid, "lit") >= LIT_SHARE:
                    continue
                sill = floor * FLOOR + WINDOW_SILL
                rec = _facade_box(ctx, "window_light", f, along, WINDOW_OUT, sill + WINDOW_SIZE[2] * 0.5, WINDOW_SIZE, rid,
                                  {"floor": floor, "warmth": _r(0.6 + 0.4 * stable_hash(rid, "warm"), 3)})
                if not ctx.check(rec):
                    continue
                out.append(rec)
                if stable_hash(rid, "candle") < CANDLE_SHARE:
                    cid = rid.replace("/window_light/", "/window_candle/")
                    candle = _facade_box(ctx, "window_candle", f, along, CANDLE_OUT, sill + CANDLE_SIZE[2] * 0.5,
                                         CANDLE_SIZE, cid, {"window": rid, "flame": True})
                    if ctx.check(candle):
                        out.append(candle)
            floor += 1
    return out


def _main_frontage(ctx, b):
    """The wall a building's door is on: the one fronting its addr:street, else its longest frontage."""
    own = [f for f in ctx.frontages if f["osm"] == b["id"]]
    if not own:
        return None
    street = _norm_street(b["street"])
    named = [f for f in own if street and _norm_street(f["road"]) == street]
    pool = named or own
    return sorted(pool, key=lambda f: (-f["length"], f["edge"]))[0]


def _norm_street(name):
    return " ".join((name or "").lower().replace(".", "").split())


def _face_s(ctx, f, along):
    p = frontage_point(f, along)
    for face in ctx.faces:
        if f in face["frontages"]:
            return face["id"], p[0] * face["u"][0] + p[1] * face["u"][1]
    return None, None


def _plan_wreaths(ctx):
    out = []
    for b in ctx.buildings:
        if not b["number"]:
            continue
        f = _main_frontage(ctx, b)
        if f is None or f["length"] < 2.0 * DOOR_END_CLEAR:
            ctx.note("wreath", "no frontage")
            continue
        rid = ctx.rid("wreath", b["id"])
        mid = f["length"] * (0.5 + (stable_hash(rid, "door") - 0.5) * 0.4)
        for shift in (0.0, -100.0, 100.0, -200.0, 200.0):
            along = max(DOOR_END_CLEAR, min(f["length"] - DOOR_END_CLEAR, mid + shift))
            p = frontage_point(f, along, WREATH_OUT)
            rec = make_record("wreath", rid, (p[0], p[1], _ground(f) + WREATH_Z), WREATH_SIZE, yaw=_yaw(f["n"]),
                              pitch=-90.0, host=b["id"], params={"door": b["number"], "street": f["road"], "bow": True})
            if ctx.check(rec):
                out.append(rec)
                ctx.doors.append(_face_s(ctx, f, along))
                break
    return out


def _plan_shops(ctx):
    """Awnings and sign blanks over the shops: the block's commercial POIs on the wall nearest each, then the
    buildings tagged retail with no POI on their main wall."""
    shops = []
    for poi in ctx.scene["pois"]:
        if not (poi["shop"] or poi["amenity"] in SHOP_AMENITIES):
            continue
        best = None
        for f in ctx.frontages:
            d, q = closest_point_on_polyline(poi["at"], [f["a"], f["b"]])
            if d <= SHOP_REACH and (best is None or (d, f["osm"], f["edge"]) < best[0]):
                best = ((d, f["osm"], f["edge"]), f, q)
        if best is None:
            continue
        _key, f, q = best
        along = (q[0] - f["a"][0]) * f["u"][0] + (q[1] - f["a"][1]) * f["u"][1]
        shops.append((poi["id"], f, along, poi["name"] or "", poi["shop"] or poi["amenity"]))
    with_poi = {f["osm"] for _i, f, _a, _n, _k in shops}
    for b in ctx.buildings:
        if b["kind"] == "retail" and b["id"] not in with_poi:
            f = _main_frontage(ctx, b)
            if f is not None:
                shops.append((b["id"], f, f["length"] * 0.5, "", "retail"))
    out = []
    taken = []   # (osm, edge, along lo, along hi)
    for key, f, along0, name, what in shops:
        width = min(AWNING_WIDTH, f["length"] - 60.0)
        if width < 150.0:
            ctx.note("awning", "wall too short")
            continue
        aid, sid = ctx.rid("awning", key), ctx.rid("shop_sign", key)
        placed = False
        for shift in SHOP_SHIFTS:
            along = max(width * 0.5 + 30.0, min(f["length"] - width * 0.5 - 30.0, along0 + shift))
            if any(o == f["osm"] and e == f["edge"] and lo < along + width * 0.5 and along - width * 0.5 < hi
                   for o, e, lo, hi in taken):
                continue
            ground = _ground(f)
            run = AWNING_DEPTH * 0.5 * math.cos(math.radians(AWNING_PITCH))
            drop = AWNING_DEPTH * 0.5 * math.sin(math.radians(-AWNING_PITCH))
            p = frontage_point(f, along, run + 1.0)
            colour = AWNING_COLOURS[int(stable_hash(aid, "colour") * len(AWNING_COLOURS))]
            awning = make_record("awning", aid, (p[0], p[1], ground + AWNING_BACK_Z - drop),
                                 (AWNING_DEPTH, width, AWNING_THICK), yaw=_yaw(f["n"]), pitch=AWNING_PITCH, host=f["osm"],
                                 params={"colour": colour, "shop": what, "name": name})
            sign = _facade_box(ctx, "shop_sign", f, along, SIGN_OUT, ground + SIGN_Z,
                               (SIGN_SIZE[0], min(SIGN_SIZE[1], width - 40.0), SIGN_SIZE[2]), sid,
                               {"text": "", "shop": what, "name": name})
            if ctx.check(awning) and ctx.check(sign):
                out.extend([awning, sign])
                taken.append((f["osm"], f["edge"], along - width * 0.5, along + width * 0.5))
                placed = True
                break
        if not placed:
            ctx.note("awning", "no clear spot")
    return out


def _plan_roof_snow(ctx):
    out = []
    for b in ctx.buildings:
        d = (PARAPET_THICK if b["parapet"] else 0.0) + ROOF_SNOW_INSET
        ring = inset_ring(b["ring"], d)
        if ring is None:
            ctx.note("roof_snow", "no clean inset")
            continue
        c = geo.centroid(ring)
        rid = ctx.rid("roof_snow", b["id"])
        rec = make_record("roof_snow", rid, (c[0], c[1], b["height"]), yaw=0.0, host=b["id"],
                          params={"closed": True, "thickness": ROOF_SNOW_THICK,
                                  "points": [[_r(x), _r(y), _r(b["height"])] for x, y in ring]})
        if ctx.check(rec):
            out.append(rec)
    return out


def _plan_parapet_snow(ctx):
    out = []
    for b in ctx.buildings:
        if not b["parapet"]:
            continue
        ring = list(geo.oriented(b["ring"], ccw=True))
        markers = [a["marker"] for a in ctx.scene["anchors"] if a["osm"] == b["id"]]
        n = len(ring)
        for i in range(n):
            a, c = ring[i], ring[(i + 1) % n]
            length = _dist2(a, c)
            if length < PARAPET_SNOW_MIN_EDGE:
                continue
            u = _unit(a, c)
            inward = (-u[1], u[0])
            pieces = int(math.ceil(length / PARAPET_SNOW_PIECE))
            piece = length / pieces
            for k in range(pieces):
                lo, hi = piece * k, piece * (k + 1)
                pa = (a[0] + u[0] * lo, a[1] + u[1] * lo)
                pb = (a[0] + u[0] * hi, a[1] + u[1] * hi)
                rid = ctx.rid("parapet_snow", b["id"], "e{0}".format(i), "p{0}".format(k))
                if any(closest_point_on_polyline(m, [pa, pb])[0] < PARAPET_SNOW_ANCHOR_CLEAR for m in markers):
                    ctx.note("parapet_snow", "anchor fitting")
                    continue
                mid = ((pa[0] + pb[0]) * 0.5 + inward[0] * PARAPET_THICK * 0.5,
                       (pa[1] + pb[1]) * 0.5 + inward[1] * PARAPET_THICK * 0.5)
                rec = make_record("parapet_snow", rid, (mid[0], mid[1], b["top"] + PARAPET_SNOW_THICK * 0.5),
                                  (piece - 2.0, PARAPET_THICK + 4.0, PARAPET_SNOW_THICK), yaw=_yaw(u), host=b["id"])
                if ctx.check(rec):
                    out.append(rec)
    return out


def _string_light(ctx, f, along, ground, z, far, far_b, rid):
    p = frontage_point(f, along, STRING_OFF_WALL)
    q = frontage_point(f, along, far - STRING_OFF_WALL)
    a3, b3 = (p[0], p[1], ground + z), (q[0], q[1], ground + z)
    span = _dist2(p, q)
    sag = max(STRING_MIN_SAG, span * STRING_SAG)
    pts = catenary_points(a3, b3, sag, STRING_POINTS)
    mid = ((a3[0] + b3[0]) * 0.5, (a3[1] + b3[1]) * 0.5, a3[2])
    return make_record("string_light", rid, mid, yaw=_yaw(f["n"]), host=f["osm"],
                       params={"points": [[_r(x), _r(y), _r(zz)] for x, y, zz in pts], "radius": STRING_RADIUS,
                               "sag": _r(sag), "span": _r(span), "far_host": far_b["id"], "bulb_spacing": 30.0,
                               "bulb_drop": STRING_BULB_DROP, "bulb_size": STRING_BULB_SIZE, "palette": "warm_multi"})


def _plan_string_lights(ctx):
    out = []
    for face in ctx.faces:
        s = face["s0"] + STRING_END_CLEAR
        k = 0
        while s <= face["s1"] - STRING_END_CLEAR:
            hit = face_at(face, s)
            rid = ctx.rid("string_light", face["id"], "s{0}".format(k))
            s += STRING_SPACING
            k += 1
            if hit is None:
                ctx.note("string_light", "gap in face")
                continue
            f, along = hit
            across = _across(ctx.scene, ctx.ids, frontage_point(f, along), f["n"], STRING_REACH)
            if across is None:
                ctx.note("string_light", "nothing across")
                continue
            far, far_b = across
            ground = ROAD_TOP
            for z in STRING_HEIGHTS:
                if z + ground > min(f["height"], far_b["height"]) - STRING_TOP_CLEAR:
                    continue
                rec = _string_light(ctx, f, along, ground, z, far, far_b, rid)
                if ctx.check(rec):
                    out.append(rec)
                    break
    return out


def _plan_snow_banks(ctx):
    out = []
    for face in ctx.faces:
        doors = [s for fid, s in ctx.doors if fid == face["id"]]
        s = face["s0"] + BANK_CORNER
        k = 0
        while s <= face["s1"] - BANK_CORNER:
            here = s
            s += BANK_STEP
            rid = ctx.rid("snow_bank", face["id"], "s{0}".format(k))
            k += 1
            hit = face_at(face, here)
            if hit is None:
                continue
            f, along = hit
            if f["kerb"] < BANK_MIN_KERB:
                ctx.note("snow_bank", "no kerb")
                continue
            if stable_hash(rid, "gap") < BANK_GAP_SHARE:
                continue
            if any(abs(here - d) < BANK_DOOR_CLEAR for d in doors):
                ctx.note("snow_bank", "door")
                continue
            length = BANK_SIZE[0] + BANK_EXTRA * stable_hash(rid, "length")
            p = frontage_point(f, along, f["kerb"] - BANK_SIZE[1] * 0.5 - BANK_KERB_IN)
            yaw = _yaw(f["u"]) + (stable_hash(rid, "yaw") - 0.5) * 8.0
            rec = make_record("snow_bank", rid, (p[0], p[1], SIDEWALK_TOP), (length, BANK_SIZE[1], BANK_SIZE[2]),
                              yaw=yaw, host=f["osm"], params={"dirty": _r(stable_hash(rid, "dirt"), 3)})
            if ctx.check(rec):
                out.append(rec)
    return out


def _kerb_point(face, s, back):
    """(x, y) and the frontage at s along the face, back cm in from its kerb; None over a gap or a narrow pavement."""
    hit = face_at(face, s)
    if hit is None or hit[0]["kerb"] < TREE_MIN_KERB:
        return None
    f, along = hit
    return frontage_point(f, along, f["kerb"] - back), f


def _plan_tree_stand(ctx):
    """One NYC sidewalk tree stand near a corner: a rail on two posts by the kerb with trees leaning on it, on the
    widest pavement of the block's street faces, TREE_CORNER back from the crossing. The first candidate whose
    frame passes every constraint and keeps TREE_MIN_TREES trees is used; the rest are not."""
    length = TREE_COUNT * TREE_STEP
    candidates = []
    for face in ctx.faces:
        for end, sign in (("s0", 1.0), ("s1", -1.0)):
            s = face[end] + sign * TREE_CORNER
            hit = face_at(face, s + sign * length * 0.5)
            if hit is None or hit[0]["kerb"] < TREE_MIN_KERB:
                continue
            candidates.append((-round(hit[0]["kerb"]), face["id"], end, face, s, sign))
    candidates.sort(key=lambda c: c[:3])
    for _k, fid, end, face, s, sign in candidates:
        base = ctx.rid("tree_stand", fid, end)
        posts = [_kerb_point(face, s, TREE_RAIL_IN), _kerb_point(face, s + sign * length, TREE_RAIL_IN)]
        if None in posts:
            ctx.note("tree_stand", "narrow pavement")
            continue
        (p0, f0), (p1, _f1) = posts
        along_yaw = _yaw(_unit(p0, p1))
        group = []
        for k, (p, f) in enumerate(posts):
            group.append(make_record("tree_stand", base + "/post{0}".format(k),
                                     (p[0], p[1], SIDEWALK_TOP + TREE_RAIL_HEIGHT * 0.5), (10.0, 10.0, TREE_RAIL_HEIGHT),
                                     yaw=along_yaw, host=f["osm"]))
        mid = ((p0[0] + p1[0]) * 0.5, (p0[1] + p1[1]) * 0.5)
        group.append(make_record("tree_stand", base + "/rail", (mid[0], mid[1], SIDEWALK_TOP + TREE_RAIL_HEIGHT - 5.0),
                                 (_dist2(p0, p1), 10.0, 10.0), yaw=along_yaw, host=f0["osm"], params={"lights": True}))
        if not all(ctx.check(r) for r in group):
            continue
        trees = []
        for k in range(TREE_COUNT):
            tid = base + "/tree{0}".format(k)
            at = _kerb_point(face, s + sign * (k + 0.5) * TREE_STEP, TREE_RAIL_IN + TREE_SIZE[1] * 0.5 + 8.0)
            if at is None:
                continue
            p, f = at
            tree = make_record("tree_for_sale", tid, (p[0], p[1], SIDEWALK_TOP + TREE_SIZE[2] * 0.5), TREE_SIZE,
                               yaw=_yaw(f["n"]), pitch=6.0 * (stable_hash(tid, "lean") - 0.5), host=f["osm"],
                               params={"netted": stable_hash(tid, "net") < 0.3})
            if ctx.check(tree):
                trees.append(tree)
        if len(trees) >= TREE_MIN_TREES:
            return group + trees
        ctx.note("tree_stand", "too few trees")
    return []


def _plan_steam(ctx):
    candidates = []
    for face in ctx.faces:
        s = face["s0"] + STEAM_STEP * 0.5
        k = 0
        while s <= face["s1"]:
            hit = face_at(face, s)
            rid = ctx.rid("steam_vent", face["id"], "s{0}".format(k))
            s += STEAM_STEP
            k += 1
            if hit is None:
                continue
            f, along = hit
            out = f["centre"] - f["half"] * STEAM_LANE
            p = frontage_point(f, along, out)
            candidates.append((stable_hash(rid, "pick"), rid, p, f))
    candidates.sort(key=lambda c: (c[0], c[1]))
    out = []
    for _h, rid, p, f in candidates:
        if len(out) >= STEAM_COUNT:
            break
        rec = make_record("steam_vent", rid, (p[0], p[1], ROAD_TOP + STEAM_SIZE[2] * 0.5), STEAM_SIZE, host=None,
                          params={"steam": True, "stripes": "orange_white", "road": f["road"]})
        if ctx.check(rec):
            out.append(rec)
    return out


def _plan_car_snow(ctx):
    out = []
    cars = []
    for p in ctx.props:
        if not p["kind"].startswith("ParkedCar"):
            continue
        for face in ctx.faces:
            s = p["at"][0] * face["u"][0] + p["at"][1] * face["u"][1]
            hit = face_at(face, s)
            if hit is None:
                continue
            f, along = hit
            base = frontage_point(f, along)
            off = (p["at"][0] - base[0]) * f["n"][0] + (p["at"][1] - base[1]) * f["n"][1]
            if f["kerb"] <= off <= f["kerb"] + 350.0:
                cars.append(p)
                break
    for p in sorted(cars, key=lambda c: prop_id(c)):
        rid = ctx.rid("car_snow", prop_id(p))
        if stable_hash(rid, "pick") >= CAR_SNOW_SHARE:
            continue
        scale = p["radius"] / 245.0
        top = p["z1"]
        cx = p["at"][0] - math.cos(math.radians(p["yaw"])) * CAR_CABIN_BACK * scale
        cy = p["at"][1] - math.sin(math.radians(p["yaw"])) * CAR_CABIN_BACK * scale
        rec = make_record("car_snow", rid, (cx, cy, top - 5.0),
                          (CAR_SNOW_SIZE[0] * scale, CAR_SNOW_SIZE[1] * scale, CAR_SNOW_SIZE[2]), yaw=p["yaw"],
                          params={"on_prop": prop_id(p)})
        if ctx.check(rec):
            out.append(rec)
        if len(out) >= CAR_SNOW_MAX:
            break
    return out


PASSES = (_plan_wreaths, _plan_shops, _plan_windows, _plan_roof_snow, _plan_parapet_snow, _plan_string_lights,
          _plan_snow_banks, _plan_tree_stand, _plan_steam, _plan_car_snow)


def plan_scene_dressing(scene, block_id):
    """{"block", "buildings", "frontage_m", "records", "counts", "rejected"} for one block of the scene. The records
    pass every constraint and the density caps, sorted by kind (KIND_ORDER) and id."""
    blocks = partition_blocks(scene["buildings"], scene["roads"])
    if block_id not in blocks:
        raise KeyError("no block " + str(block_id))
    ctx = _Ctx(scene, block_id, blocks[block_id])
    records = []
    for step in PASSES:
        records.extend(step(ctx))
    frontage_m = sum(f["length"] for f in ctx.frontages) / 100.0
    capped = apply_density_caps(records, frontage_m)
    kept = {r["id"] for r in capped}
    for rec in records:
        if rec["id"] not in kept:
            ctx.note(rec["kind"], "density cap")
    order = {k: i for i, k in enumerate(KIND_ORDER)}
    capped.sort(key=lambda r: (order[r["kind"]], r["id"]))
    counts = {}
    for rec in capped:
        counts[rec["kind"]] = counts.get(rec["kind"], 0) + 1
    return {"block": block_id, "buildings": ctx.ids, "frontage_m": _r(frontage_m), "records": capped,
            "counts": counts, "rejected": ctx.rejected, "faces": [f["id"] for f in ctx.faces]}


def plan_block_dressing(district, block_id):
    """[record] for one block of the district (``hero_block_id(gather_scene(district))`` for the hero block)."""
    return plan_scene_dressing(gather_scene(district), block_id)["records"]


def plan_json(plan):
    """The plan as JSON text: sorted keys, floats already rounded to 0.01 cm, so the same scene gives the same bytes."""
    doc = {"version": DRESSING_VERSION, "block": plan["block"], "buildings": plan["buildings"],
           "frontage_m": plan["frontage_m"], "counts": plan["counts"], "records": plan["records"]}
    return json.dumps(doc, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n"


def write_plan(plan, path):
    """Write plan_json(plan) to path (LF line ends, UTF-8)."""
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(plan_json(plan))
    return path


def actor_label(rec):
    """A short, stable actor label for the engine half: City_Dress_<kind>_<8 hex of the id's crc32>."""
    import zlib  # noqa: PLC0415
    return "City_Dress_{0}_{1:08x}".format(rec["kind"], zlib.crc32(rec["id"].encode("utf-8")) & 0xffffffff)


def describe(plan):
    parts = ", ".join("{0} {1}".format(k, plan["counts"][k]) for k in KIND_ORDER if k in plan["counts"])
    return "{0}: {1} buildings, {2:.0f} m of frontage, {3} records ({4})".format(
        plan["block"], len(plan["buildings"]), plan["frontage_m"], len(plan["records"]), parts)
