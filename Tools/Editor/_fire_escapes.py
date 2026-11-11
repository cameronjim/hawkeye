"""Where the fire escapes go: fire_escape_spots and the landing geometry it tests with. No
``unreal``; generate_city re-exports every name and builds the props from these spots.

FIRE_ESCAPE_MIN_HEIGHT_M and FIRE_ESCAPE_MAX_HEIGHT_M live in _district (facade_style uses them too).
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402
from _district import (  # noqa: E402
    FIRE_ESCAPE_MAX_HEIGHT_M, FIRE_ESCAPE_MIN_HEIGHT_M, LAMP_ARM, LAMP_POLE_DIAMETER, LAMP_POLE_HEIGHT,
    PARAPET_HEIGHT, lamp_spots, road_paths)
from _geomutil import _point_quad_distance, closest_point_on_polyline  # noqa: E402


FIRE_ESCAPE_PREFIX = "City_FireEscape_"
FIRE_ESCAPE_FLOOR = 330.0          # cm between landings; the lowest is at the second floor
FIRE_ESCAPE_ROOF_CLEARANCE = 180.0  # cm; the top landing's slab is at least this far under the roof
FIRE_ESCAPE_SLAB = (240.0, 90.0, 8.0)
FIRE_ESCAPE_GAP = 5.0              # cm between the facade and the slab
FIRE_ESCAPE_RAIL = 90.0            # rail height above the slab
FIRE_ESCAPE_RAIL_THICK = 6.0       # the rails' collision; the visible bars are thinner
FIRE_ESCAPE_MIN_EDGE = 300.0       # cm; the landing plus 30 cm each side
FIRE_ESCAPE_STREET_SLACK = 200.0   # cm; edges this much further from a road than the nearest still face it
FIRE_ESCAPE_FACING = 0.5           # the edge's outward normal within 60 degrees of the road
FIRE_ESCAPE_LAMP_CLEARANCE = 30.0  # cm between a landing and a lamp pole (or head)
FIRE_ESCAPE_GAP_TO_OTHER = 10.0    # cm between two buildings' landings


def _landing_corners(x, y, yaw, margin=0.0):
    """The landing's footprint as four (x, y) corners: along +/- half the slab, out from the gap."""
    ox, oy = -math.sin(math.radians(yaw)), math.cos(math.radians(yaw))   # local +Y, out of the facade
    ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))    # local +X, along it
    half = FIRE_ESCAPE_SLAB[0] * 0.5 + margin
    near, far = FIRE_ESCAPE_GAP - margin, FIRE_ESCAPE_GAP + FIRE_ESCAPE_SLAB[1] + margin
    return [(x + ux * a + ox * b, y + uy * a + oy * b) for a, b in ((-half, near), (half, near), (half, far), (-half, far))]


def _rects_overlap(a, b):
    """Separating-axis test for two convex quads given as corner lists."""
    for quad in (a, b):
        for i in range(4):
            (x0, y0), (x1, y1) = quad[i], quad[(i + 1) % 4]
            nx, ny = y1 - y0, x0 - x1
            pa = [nx * px + ny * py for px, py in a]
            pb = [nx * px + ny * py for px, py in b]
            if max(pa) < min(pb) or max(pb) < min(pa):
                return False
    return True


def fire_escape_spots(district):
    """[(osm id, floor, (x, y, z), yaw, ladder drop, ladder side)] in a stable order: fire_escape_plan less the
    buildings whose escape would leave the street no grapple onto their roof (_reach.escapes_blocking_street).
    Cached on the district: the second rule runs the reach model."""
    cached = getattr(district, "_fire_escape_spots", None)
    if cached is not None:
        return list(cached)
    plan = fire_escape_plan(district)
    import _reach   # here, not at the top: _reach builds on this module
    dropped = _reach.escapes_blocking_street(district, plan)
    spots = [s for s in plan if s[0] not in dropped]
    district._fire_escape_spots = list(spots)
    district._fire_escapes_dropped = sorted(dropped)
    return spots


def fire_escape_plan(district):
    """[(osm id, floor, (x, y, z), yaw, ladder drop, ladder side)] in a stable order, before the street rule.

    One escape per building FIRE_ESCAPE_MIN_HEIGHT_M to FIRE_ESCAPE_MAX_HEIGHT_M tall, on the
    longest exterior edge whose outward normal faces a road and whose middle is within
    FIRE_ESCAPE_STREET_SLACK of the nearest such edge's road distance. (x, y) is the edge's middle
    on the facade line, z the slab top, yaw turns local +X along the facade and +Y out of it.
    Landings at FIRE_ESCAPE_FLOOR, 2 x, ... up to FIRE_ESCAPE_ROOF_CLEARANCE under the roof. A
    building whose landings would stand in another building (or its own), within
    FIRE_ESCAPE_LAMP_CLEARANCE of a lamp, or on an earlier building's escape, gets none.
    """
    roads = sorted(road_paths(district), key=lambda rp: rp[0]["id"])
    segments = [(p, q) for _rec, paths in roads for path in paths for p, q in zip(path, path[1:])]
    buildings = []
    for rec in sorted(district.buildings, key=lambda r: r["id"]):
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) >= 3:
            buildings.append((rec, ring, geo.bounds(ring)))
    lamps = [(x, y, yaw) for x, y, yaw, _shadows in lamp_spots(district)]

    def inside_other(pt, own_id, below_z=None):
        for rec, ring, (x0, y0, x1, y1) in buildings:
            if rec["id"] == own_id:
                continue
            if below_z is not None and rec["height_m"] * 100.0 + PARAPET_HEIGHT < below_z:
                continue
            if x0 <= pt[0] <= x1 and y0 <= pt[1] <= y1 and geo.point_in_polygon(pt, ring):
                return True
        return False

    def nearest_road(pt):
        best = None
        for p, q in segments:
            d, c_pt = closest_point_on_polyline(pt, [p, q])
            if best is None or d < best[0]:
                best = (d, c_pt)
        return best

    placed = []   # (quads by z) of accepted landings
    spots = []
    for rec, ring, _box in buildings:
        height_m = rec["height_m"]
        if height_m < FIRE_ESCAPE_MIN_HEIGHT_M or height_m > FIRE_ESCAPE_MAX_HEIGHT_M:
            continue
        height_cm = height_m * 100.0
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        n = len(ring)
        candidates = []   # (road distance, -length, index, mid, outward, along)
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            length = math.hypot(bx - ax, by - ay)
            if length < FIRE_ESCAPE_MIN_EDGE or not segments:
                continue
            ox, oy = (by - ay) / length * inward, -(bx - ax) / length * inward   # outward normal
            ux, uy = (bx - ax) / length, (by - ay) / length
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            dist, (qx, qy) = nearest_road((mx, my))
            if dist < 1.0 or ((qx - mx) * ox + (qy - my) * oy) / dist < FIRE_ESCAPE_FACING:
                continue
            outside = [(mx + ox * 150.0 + ux * t, my + oy * 150.0 + uy * t) for t in (-100.0, 0.0, 100.0)]
            if any(inside_other(pt, rec["id"]) or geo.point_in_polygon(pt, ring) for pt in outside):
                continue
            candidates.append((dist, -length, i, (mx, my), (ox, oy)))
        if not candidates:
            continue
        nearest = min(cand[0] for cand in candidates)
        facing = [cand for cand in candidates if cand[0] <= nearest + FIRE_ESCAPE_STREET_SLACK]
        facing.sort(key=lambda cand: (cand[1], cand[2]))
        _dist, _neg, _index, (mx, my), (ox, oy) = facing[0]
        yaw = math.degrees(math.atan2(-ox, oy))

        floors = []
        floor = 1
        while floor * FIRE_ESCAPE_FLOOR <= height_cm - FIRE_ESCAPE_ROOF_CLEARANCE:
            floors.append(floor)
            floor += 1
        if not floors:
            continue
        quad = _landing_corners(mx, my, yaw)
        probe = quad + [((quad[0][0] + quad[2][0]) * 0.5, (quad[0][1] + quad[2][1]) * 0.5)]
        blocked = any(geo.point_in_polygon(pt, ring) for pt in probe)
        for f in floors:
            if blocked:
                break
            z = f * FIRE_ESCAPE_FLOOR
            if any(inside_other(pt, rec["id"], z - FIRE_ESCAPE_SLAB[2]) for pt in probe):
                blocked = True
            for lx, ly, lyaw in lamps:
                if z - FIRE_ESCAPE_SLAB[2] <= LAMP_POLE_HEIGHT and _point_quad_distance((lx, ly), quad) < FIRE_ESCAPE_LAMP_CLEARANCE + LAMP_POLE_DIAMETER * 0.5:
                    blocked = True
                hx = lx + math.cos(math.radians(lyaw)) * LAMP_ARM
                hy = ly + math.sin(math.radians(lyaw)) * LAMP_ARM
                if abs(z - LAMP_POLE_HEIGHT) < FIRE_ESCAPE_RAIL + 50.0 and _point_quad_distance((hx, hy), quad) < FIRE_ESCAPE_LAMP_CLEARANCE + 20.0:
                    blocked = True
        padded = _landing_corners(mx, my, yaw, FIRE_ESCAPE_GAP_TO_OTHER * 0.5)
        if not blocked and any(_rects_overlap(padded, other) for other in placed):
            blocked = True
        if blocked:
            continue
        placed.append(padded)
        for f in floors:
            drop = FIRE_ESCAPE_FLOOR if f > 1 else 0.0
            side = 1.0 if f % 2 == 0 else -1.0
            spots.append((rec["id"], f, (mx, my, f * FIRE_ESCAPE_FLOOR), yaw, drop, side))
    return spots
