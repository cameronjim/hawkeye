"""Grapple anchors and the reach model they are placed by: the parapet (corner) anchors, the facade
anchors, GrappleReach (the grapple's range, cone, line of sight and clear zip as plain geometry),
grapple_reach and anchor_spots. No ``unreal``; generate_city re-exports every name.

anchor_reach_report stays in generate_city: it reads the chapter's objective roofs, which hang off
player_start_transform and its unreal.Vector.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402
from _district import (  # noqa: E402
    LAMP_ARM, LAMP_POLE_HEIGHT, PARAPET_HEIGHT, PARAPET_MIN_HEIGHT_M, PARAPET_THICK, SIDEWALK_TOP,
    SIDEWALK_WIDTH, ground_ring_cm, lamp_spots, map_street_lines, road_paths)
from _fire_escapes import (  # noqa: E402
    FIRE_ESCAPE_FACING, FIRE_ESCAPE_GAP, FIRE_ESCAPE_RAIL, FIRE_ESCAPE_SLAB, _landing_corners, fire_escape_spots)
from _geomutil import _edge_distance, _segment_params, closest_point_on_polyline, ring_distance  # noqa: E402


ANCHOR_MIN_HEIGHT_M = 8.0         # only buildings taller than this get anchors
ANCHOR_MIN_GAP = 400.0            # cm; an anchor this close to one already placed is skipped
ANCHOR_LONG_EDGE = 2500.0         # cm; edges longer than this also get one at the middle
ANCHOR_CORNER_TURN = 30.0         # degrees the outline has to turn for a vertex to be a corner
ANCHOR_MIN_INTERIOR = 45.0        # degrees; sharper corners put the landing too far in
ANCHOR_LANDING_INBOARD = 60.0     # cm from the anchor to the landing point, square to each edge
ANCHOR_LANDING_CLEARANCE = 70.0   # cm from any roof edge: the parapet (30) plus Kate's capsule (34)
ANCHOR_BURIED_DISTANCE = 40.0     # cm; an anchor this close to a taller neighbour is in its wall


def _corner_anchor_spots(district):
    """The parapet anchors, before the facade anchors anchor_spots adds: [(x, y, z, yaw, landing_forward,
    landing_drop, osm id)] in a stable order.

    One anchor per roof corner and one mid-edge on edges over ANCHOR_LONG_EDGE, for every
    building over ANCHOR_MIN_HEIGHT_M. Each sits centred on the parapet (half its thickness in
    from the edge) on the parapet top; +X points inboard along the corner bisector or the edge
    normal. The landing point is ANCHOR_LANDING_INBOARD further in, square to the edges (so on
    a corner it is 60 / sin(half the corner angle) along the bisector), down on the roof.
    Anchors closer than ANCHOR_MIN_GAP to an earlier one, buried in a taller neighbour's wall,
    or whose landing point is not clear of the roof edges, are skipped.
    """
    buildings = []
    for rec in sorted(district.buildings, key=lambda r: r["id"]):
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) >= 3:
            buildings.append((rec, ring, geo.bounds(ring)))

    inset = PARAPET_THICK * 0.5
    placed = []
    grid = {}

    def near_placed(x, y):
        gx, gy = int(math.floor(x / ANCHOR_MIN_GAP)), int(math.floor(y / ANCHOR_MIN_GAP))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for px, py in grid.get((gx + dx, gy + dy), ()):
                    if math.hypot(px - x, py - y) < ANCHOR_MIN_GAP:
                        return True
        return False

    def buried(x, y, roof_top, own_id):
        for rec, ring, (x0, y0, x1, y1) in buildings:
            if rec["id"] == own_id or rec["height_m"] * 100.0 <= roof_top:
                continue
            pad = ANCHOR_BURIED_DISTANCE
            if x0 - pad <= x <= x1 + pad and y0 - pad <= y <= y1 + pad and ring_distance((x, y), ring) < pad:
                return True
        return False

    for rec, ring, _box in buildings:
        if rec["height_m"] <= ANCHOR_MIN_HEIGHT_M:
            continue
        height_cm = rec["height_m"] * 100.0
        parapet = rec["height_m"] >= PARAPET_MIN_HEIGHT_M
        drop = PARAPET_HEIGHT if parapet else 0.0
        top = height_cm + drop
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        n = len(ring)
        candidates = []   # (anchor x, y, unit inboard x, y, distance anchor->landing)
        for i in range(n):
            px, py = ring[i - 1]
            cx, cy = ring[i]
            nx_, ny_ = ring[(i + 1) % n]
            ax, ay = cx - px, cy - py
            bx, by = nx_ - cx, ny_ - cy
            la, lb = math.hypot(ax, ay), math.hypot(bx, by)
            if la < 1.0 or lb < 1.0:
                continue
            ax, ay, bx, by = ax / la, ay / la, bx / lb, by / lb
            turn = math.degrees(math.atan2(ax * by - ay * bx, ax * bx + ay * by)) * inward
            interior = 180.0 - turn
            if turn >= ANCHOR_CORNER_TURN and interior >= ANCHOR_MIN_INTERIOR:
                # Inward normals of both edges; their sum is the bisector.
                ux, uy = -ay * inward - by * inward, ax * inward + bx * inward
                ul = math.hypot(ux, uy)
                s = math.sin(math.radians(interior) * 0.5)
                if ul > 1e-6 and s > 1e-3:
                    ux, uy = ux / ul, uy / ul
                    candidates.append((cx + ux * inset / s, cy + uy * inset / s, ux, uy,
                                       ANCHOR_LANDING_INBOARD / s))
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            length = math.hypot(bx - ax, by - ay)
            if length <= ANCHOR_LONG_EDGE:
                continue
            ux, uy = -(by - ay) / length * inward, (bx - ax) / length * inward
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            candidates.append((mx + ux * inset, my + uy * inset, ux, uy, ANCHOR_LANDING_INBOARD))

        for x, y, ux, uy, forward in candidates:
            lx, ly = x + ux * forward, y + uy * forward
            if not geo.point_in_polygon((lx, ly), ring) or _edge_distance((lx, ly), ring) < ANCHOR_LANDING_CLEARANCE:
                continue
            if near_placed(x, y) or buried(x, y, top, rec["id"]):
                continue
            placed.append((x, y, top, math.degrees(math.atan2(uy, ux)), forward, drop, rec["id"]))
            key = (int(math.floor(x / ANCHOR_MIN_GAP)), int(math.floor(y / ANCHOR_MIN_GAP)))
            grid.setdefault(key, []).append((x, y))
    return placed


# --- facade anchors and the grapple reach model -------------------------------------------------
#
# The parapet anchors above sit 15 cm in from the roof edge on the parapet top. From the sidewalk under a
# tall tenement the lens looks up past the parapet's outer face and never sees them (the audit,
# 2026-09-29: 280 of 507 anchored roofs had no anchor reachable from the street, the find_arrow roof one
# anchor at a back corner). A facade anchor is the same fitting bolted to the outer face of the parapet,
# its top flush with the parapet's: seen from the pavement below and from the roofs across the street.
# Every anchored roof gets one on the parapet facing its widest street, and one facing every neighbour
# (a roof 1 to 25 m away across a street or a gap) that cannot reach any of its anchors yet. The reach
# model below is the grapple's rules in plain geometry (buildings as prisms to their parapet tops, lamp
# poles and heads as columns; the hip camera's sums without the probe); Hawkeye.Grapple.Audit checks the
# same guarantees with the game's own traces.

FACADE_ANCHOR_OUT = 20.0          # cm from the facade to the fitting's centre: its inner face on the wall
FACADE_ANCHOR_DOWN = 40.0         # cm from the parapet top to the fitting's base: its top flush with the parapet
FACADE_ANCHOR_LANDING = FACADE_ANCHOR_OUT + PARAPET_THICK + ANCHOR_LANDING_CLEARANCE   # anchor to landing, cm
FACADE_ANCHOR_MIN_EDGE = 300.0    # cm; shorter edges get none
FACADE_ANCHOR_END_CLEAR = 100.0   # cm from either end of the edge
FACADE_ANCHOR_MIN_GAP = 100.0     # cm from any other anchor (a corner anchor sits on the parapet top, not the face)
FACADE_ANCHOR_ESCAPE_CLEAR = 160.0  # cm along the facade from the middle of a fire escape on it (half its 240 cm slab, the fitting, 20 cm spare)
FACADE_ANCHOR_ROAD_REACH = 1200.0   # cm past a road's kerb an edge still faces it
FACADE_ANCHOR_SPOTS = (0.5, 0.35, 0.65, 0.25, 0.75, 0.18, 0.82)   # where along an edge, in the order tried

REACH_RANGE = 2500.0              # UGrappleComponent::Range, cm
REACH_MIN_RANGE = 300.0           # UGrappleComponent::MinRange
REACH_RANGE_SLACK = 100.0         # cm inside both a counted pair stays
REACH_CONE = 30.0                 # UGrappleComponent::ConeDegrees
REACH_CONE_SLACK = 3.0
REACH_SIGHT_TOLERANCE = 60.0      # UGrappleComponent::SightTolerance
REACH_HALF_HEIGHT = 90.0          # Kate's capsule half height plus the 2 cm a spot stands her above the floor
REACH_CAPSULE_HALF = 88.0
REACH_ZIP_LAUNCH = 120.0          # ZipLaunchHeight
REACH_START_RELEASE = 284.0       # ZipStartIgnoreRadius plus the capsule radius
REACH_START_IGNORE = 250.0        # ZipStartIgnoreRadius: the start's supports are what lies this near her capsule centre
REACH_SUPPORT_RADIUS = 150.0      # SupportRadius: the anchor's supports are what lies this near its origin
REACH_FEET_CLEAR = 10.0           # cm the capsule's bottom has to clear a roof or parapet by (the dressing's checks)
REACH_CAPSULE_RADIUS = 34.0       # Kate's capsule radius: the zip sweeps the whole capsule, not a line
REACH_ZIP_MARGIN = 5.0            # cm the model's capsule is grown by, so a pair it passes is not a graze in the game
REACH_PITCH = (-70.0, 75.0)       # CameraPitchMin, CameraPitchMax
REACH_CAMERA = {"arm": (350.0, 220.0), "socket_y": 70.0, "socket_z": (60.0, 110.0), "lift": (0.0, 150.0),
                "pitch": (20.0, 60.0), "probe": 12.0}   # HipCamera and the LookUp* values of AHawkeyeCharacter
REACH_STREET_ROBUST = 3           # sidewalk spots a street anchor is placed to be seen from, where it can be
REACH_STREET_STEP = 250.0         # cm between sidewalk spots: a player stands anywhere on the sidewalk (the audit's
                                  # report uses 10 m, then 2 m for a roof none of those reach)
REACH_AUDIT_STREET_STEPS = (1000.0, 250.0)   # FGrappleAuditSettings StreetStep, FineStreetStep: the audit's two passes
REACH_SIDEWALK_MIDDLE = SIDEWALK_WIDTH * 0.5   # cm out from the kerb
REACH_SIDEWALK_BACKOFF = (0.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0)   # cm towards the centre line
REACH_STAND_CLEAR = 60.0          # cm from any wall a street spot stands (her capsule and a little)
REACH_ROOF_SPOT_IN = 100.0        # cm in from a parapet a roof spot stands
REACH_ROOF_SPOT_EDGE = 300.0      # cm; the middle of every parapet this long is a roof spot (and the roof's centre)
REACH_NEIGHBOUR_GAP = 2500.0      # cm; roofs this close across a street or gap are neighbours
REACH_TOUCHING = 100.0            # cm; closer, the roofs share a wall
REACH_WALKABLE_RISE = 250.0       # cm; a touching roof within this is walked or mantled onto
REACH_ESCAPE_MARGIN = 20.0        # cm a fire escape's box is grown by all round
REACH_CELL = 2500.0


def _anchor_marker(spot):
    x, y, z = spot[0], spot[1], spot[2]
    return (x, y, z + 20.0)


def _anchor_landing(spot):
    x, y, z, yaw, forward, drop, _osm = spot
    return (x + math.cos(math.radians(yaw)) * forward, y + math.sin(math.radians(yaw)) * forward, z - drop)


class GrappleReach(object):
    """The grapple's rules (range, cone, the camera's line of sight, the clear zip) as plain geometry over
    the building records, for placing anchors and for verify_city's guarantees."""

    def __init__(self, district, escapes=None):
        """escapes: the fire escapes to model (fire_escape_spots' rows); the district's own when None."""
        self.district = district
        if escapes is None:
            escapes = fire_escape_spots(district)
        self.tops = []   # (rec, ring, bounds, top z, roof z)
        for rec in sorted(district.buildings, key=lambda r: r["id"]):
            ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
            if len(ring) < 3:
                continue
            roof = rec["height_m"] * 100.0
            top = roof + (PARAPET_HEIGHT if rec["height_m"] >= PARAPET_MIN_HEIGHT_M else 0.0)
            self.tops.append((rec, ring, geo.bounds(ring), top, roof))
        self.index = {t[0]["id"]: i for i, t in enumerate(self.tops)}
        self.grid = {}
        for i, t in enumerate(self.tops):
            b = t[2]
            for gx in range(int(math.floor(b[0] / REACH_CELL)), int(math.floor(b[2] / REACH_CELL)) + 1):
                for gy in range(int(math.floor(b[1] / REACH_CELL)), int(math.floor(b[3] / REACH_CELL)) + 1):
                    self.grid.setdefault((gx, gy), []).append(i)
        self.columns = []   # (x, y, z0, z1, radius): lamp poles and heads
        for x, y, yaw, _s in lamp_spots(district):
            hx, hy = x + math.cos(math.radians(yaw)) * LAMP_ARM, y + math.sin(math.radians(yaw)) * LAMP_ARM
            self.columns.append((x, y, 0.0, LAMP_POLE_HEIGHT + 30.0, 20.0))
            self.columns.append((hx, hy, LAMP_POLE_HEIGHT - 40.0, LAMP_POLE_HEIGHT + 30.0, 40.0))
        # Every fire escape, landings, rails and ladders, as one box off the facade from under the lowest
        # slab to over the top landing's rail, REACH_ESCAPE_MARGIN bigger all round.
        self.boxes = []   # (ring, bounds, z0, z1)
        stacks = {}
        for osm, _floor, (x, y, z), yaw, _drop, _side in escapes:
            stacks.setdefault(osm, []).append((x, y, z, yaw))
        for osm in sorted(stacks):
            x, y, _z, yaw = stacks[osm][0]
            z0 = min(l[2] for l in stacks[osm]) - FIRE_ESCAPE_SLAB[2] - REACH_ESCAPE_MARGIN
            z1 = max(l[2] for l in stacks[osm]) + FIRE_ESCAPE_RAIL + REACH_ESCAPE_MARGIN
            ax, ay = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            ox, oy = -ay, ax
            half = FIRE_ESCAPE_SLAB[0] * 0.5 + REACH_ESCAPE_MARGIN
            out = FIRE_ESCAPE_GAP + FIRE_ESCAPE_SLAB[1] + REACH_ESCAPE_MARGIN
            ring = [(x + ax * s_ + ox * o, y + ay * s_ + oy * o) for s_, o in ((-half, 0.0), (half, 0.0), (half, out), (-half, out))]
            self.boxes.append((ring, geo.bounds(ring), z0, z1))
        self.box_grid = {}
        for k, (_ring, b, _z0, _z1) in enumerate(self.boxes):
            for gx in range(int(math.floor(b[0] / REACH_CELL)), int(math.floor(b[2] / REACH_CELL)) + 1):
                for gy in range(int(math.floor(b[1] / REACH_CELL)), int(math.floor(b[3] / REACH_CELL)) + 1):
                    self.box_grid.setdefault((gx, gy), []).append(k)
        # The zip collides with each landing on its own (AFireEscapeLanding: the slab and its rails, one actor a
        # floor), so a landing near where she stands is one of the start's supports and the ones above it are not.
        self.landings = []   # (ring, bounds, z0, z1)
        for _osm, _floor, (x, y, z), yaw, _drop, _side in escapes:
            ring = _landing_corners(x, y, yaw)
            self.landings.append((ring, geo.bounds(ring), z - FIRE_ESCAPE_SLAB[2], z + FIRE_ESCAPE_RAIL))
        self.landing_grid = {}
        for k, (_ring, b, _z0, _z1) in enumerate(self.landings):
            for gx in range(int(math.floor(b[0] / REACH_CELL)), int(math.floor(b[2] / REACH_CELL)) + 1):
                for gy in range(int(math.floor(b[1] / REACH_CELL)), int(math.floor(b[3] / REACH_CELL)) + 1):
                    self.landing_grid.setdefault((gx, gy), []).append(k)
        self.column_grid = {}
        for k, (x, y, _z0, _z1, radius) in enumerate(self.columns):
            for gx in range(int(math.floor((x - radius) / REACH_CELL)), int(math.floor((x + radius) / REACH_CELL)) + 1):
                for gy in range(int(math.floor((y - radius) / REACH_CELL)), int(math.floor((y + radius) / REACH_CELL)) + 1):
                    self.column_grid.setdefault((gx, gy), []).append(k)
        self.roads = []   # (a, b, half width cm)
        for rec, paths in road_paths(district):
            for path in paths:
                for a, b in zip(path, path[1:]):
                    self.roads.append((a, b, rec["width_m"] * 50.0))
        ground = geo.bounds(ground_ring_cm(district))
        self.street_spots = self._street_spots(district, ground)
        self.roof_spots = {t[0]["id"]: self._roof_spots(t) for t in self.tops}

    # --- lookups -------------------------------------------------------------------------------

    def near(self, x0, y0, x1, y1):
        """Indices of the buildings whose grid cells the box touches."""
        out = set()
        for gx in range(int(math.floor(min(x0, x1) / REACH_CELL)), int(math.floor(max(x0, x1) / REACH_CELL)) + 1):
            for gy in range(int(math.floor(min(y0, y1) / REACH_CELL)), int(math.floor(max(y0, y1) / REACH_CELL)) + 1):
                out.update(self.grid.get((gx, gy), ()))
        return sorted(out)

    def building_at(self, pt):
        pt = (pt[0], pt[1])
        for i in self.near(pt[0], pt[1], pt[0], pt[1]):
            b = self.tops[i][2]
            if b[0] <= pt[0] <= b[2] and b[1] <= pt[1] <= b[3] and geo.point_in_polygon(pt, self.tops[i][1]):
                return i
        return None

    def solid_at(self, p3):
        i = self.building_at(p3)
        return i is not None and p3[2] < self.tops[i][3]

    def _street_spots(self, district, ground):
        """The traversal audit's own sidewalk spots (FGrappleReachQuery::GatherSidewalkSpots), both passes: every
        REACH_AUDIT_STREET_STEPS along the street lines the world map stores (map_street_lines), each side, at
        the sidewalk's middle or, where that is in a building, up to 8 m nearer the centre line. Until
        2026-11-11 the model sampled the raw OSM lines and kept 60 cm off every wall, so its spots stood up to
        a metre from the audit's: a roof the model reached from one spot could be reached from none of the
        audit's (5 of the 14 street-facing roofs the audit failed)."""
        spots = []
        seen = set()
        for step in REACH_AUDIT_STREET_STEPS:
            taken = set()
            for _rec, width, line in map_street_lines(district):
                out = width * 50.0 + REACH_SIDEWALK_MIDDLE
                carry = 0.0
                for a, b in zip(line, line[1:]):
                    length = math.hypot(b[0] - a[0], b[1] - a[1])
                    if length < 1.0:
                        continue
                    dx, dy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
                    t = carry
                    while t <= length:
                        for side in (-1.0, 1.0):
                            # The sidewalk's middle, or nearer the centre line where the tagged width runs the
                            # carriageway into the buildings (the avenues' 25 m default): the foot of the wall.
                            for back in REACH_SIDEWALK_BACKOFF:
                                o = out - back
                                x, y = a[0] + dx * t - dy * o * side, a[1] + dy * t + dx * o * side
                                if self.building_at((x, y)) is not None:
                                    continue
                                key = (int(math.floor(x / (step * 0.5))), int(math.floor(y / (step * 0.5))))
                                if key not in taken and ground[0] < x < ground[2] and ground[1] < y < ground[3]:
                                    taken.add(key)
                                    where = (round(x, 1), round(y, 1))
                                    if where not in seen:
                                        seen.add(where)
                                        spots.append((x, y, SIDEWALK_TOP))
                                break
                        t += step
                    carry = t - length
        return spots

    def _clear_of_buildings(self, pt, clearance):
        for i in self.near(pt[0] - clearance, pt[1] - clearance, pt[0] + clearance, pt[1] + clearance):
            if ring_distance(pt, self.tops[i][1]) < clearance:
                return False
        return True

    def _roof_spots(self, top):
        rec, ring, _b, _top, roof = top
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        pts = [geo.centroid(ring)]
        n = len(ring)
        for i in range(n):
            (ax, ay), (bx, by) = ring[i], ring[(i + 1) % n]
            length = math.hypot(bx - ax, by - ay)
            if length < REACH_ROOF_SPOT_EDGE:
                continue
            ux, uy = -(by - ay) / length * inward, (bx - ax) / length * inward
            pts.append(((ax + bx) * 0.5 + ux * REACH_ROOF_SPOT_IN, (ay + by) * 0.5 + uy * REACH_ROOF_SPOT_IN))
        return [(x, y, roof) for x, y in pts if geo.point_in_polygon((x, y), ring)]

    # --- the rules -----------------------------------------------------------------------------

    def first_block(self, a3, b3, ignore=(), columns=True):
        """(t 0..1, building index or -1) where the line a3 -> b3 first goes into a building (under its
        parapet top) or a lamp, skipping the building indices in ignore; None when it is clear."""
        best = None
        for i in self.near(a3[0], a3[1], b3[0], b3[1]):
            if i in ignore:
                continue
            _rec, ring, box, top, _roof = self.tops[i]
            if box[0] > max(a3[0], b3[0]) or box[2] < min(a3[0], b3[0]) or \
                    box[1] > max(a3[1], b3[1]) or box[3] < min(a3[1], b3[1]):
                continue
            ts = sorted(_segment_params((a3[0], a3[1]), (b3[0], b3[1]), ring))
            # Consecutive crossings bound the stretches inside the footprint.
            for t0, t1 in zip(ts, ts[1:]):
                mid = ((a3[0] + (b3[0] - a3[0]) * (t0 + t1) * 0.5), (a3[1] + (b3[1] - a3[1]) * (t0 + t1) * 0.5))
                if not geo.point_in_polygon(mid, ring):
                    continue
                z0 = a3[2] + (b3[2] - a3[2]) * t0
                z1 = a3[2] + (b3[2] - a3[2]) * t1
                if z0 < top:
                    tb = t0
                elif z1 < top:
                    tb = t0 + (t1 - t0) * (z0 - top) / (z0 - z1)
                else:
                    continue
                if best is None or tb < best[0]:
                    best = (tb, i)
                break
        if columns:
            near_boxes = set()
            for gx in range(int(math.floor(min(a3[0], b3[0]) / REACH_CELL)), int(math.floor(max(a3[0], b3[0]) / REACH_CELL)) + 1):
                for gy in range(int(math.floor(min(a3[1], b3[1]) / REACH_CELL)), int(math.floor(max(a3[1], b3[1]) / REACH_CELL)) + 1):
                    near_boxes.update(self.box_grid.get((gx, gy), ()))
            for k in sorted(near_boxes):
                ring, box, zlo, zhi = self.boxes[k]
                if box[0] > max(a3[0], b3[0]) or box[2] < min(a3[0], b3[0]) or \
                        box[1] > max(a3[1], b3[1]) or box[3] < min(a3[1], b3[1]):
                    continue
                ts = sorted(_segment_params((a3[0], a3[1]), (b3[0], b3[1]), ring))
                for t0, t1 in zip(ts, ts[1:]):
                    mid = ((a3[0] + (b3[0] - a3[0]) * (t0 + t1) * 0.5), (a3[1] + (b3[1] - a3[1]) * (t0 + t1) * 0.5))
                    if not geo.point_in_polygon(mid, ring):
                        continue
                    z0 = a3[2] + (b3[2] - a3[2]) * t0
                    z1 = a3[2] + (b3[2] - a3[2]) * t1
                    if max(z0, z1) < zlo or min(z0, z1) > zhi:
                        continue
                    # The first point of the stretch inside the box's height.
                    if zlo <= z0 <= zhi:
                        tb = t0
                    else:
                        edge = zhi if z0 > zhi else zlo
                        tb = t0 + (t1 - t0) * (z0 - edge) / (z0 - z1)
                    if best is None or tb < best[0]:
                        best = (tb, -1)
                    break
            span = math.hypot(b3[0] - a3[0], b3[1] - a3[1])
            near = set()
            for gx in range(int(math.floor(min(a3[0], b3[0]) / REACH_CELL)), int(math.floor(max(a3[0], b3[0]) / REACH_CELL)) + 1):
                for gy in range(int(math.floor(min(a3[1], b3[1]) / REACH_CELL)), int(math.floor(max(a3[1], b3[1]) / REACH_CELL)) + 1):
                    near.update(self.column_grid.get((gx, gy), ()))
            for k in sorted(near):
                x, y, z0, z1, radius = self.columns[k]
                if max(a3[0], b3[0]) < x - radius or min(a3[0], b3[0]) > x + radius or \
                        max(a3[1], b3[1]) < y - radius or min(a3[1], b3[1]) > y + radius:
                    continue
                d, q = closest_point_on_polyline((x, y), [(a3[0], a3[1]), (b3[0], b3[1])])
                if d > radius:
                    continue
                t = math.hypot(q[0] - a3[0], q[1] - a3[1]) / span if span > 1.0 else 0.0
                h = a3[2] + (b3[2] - a3[2]) * t
                if z0 < h < z1 and (best is None or t < best[0]):
                    best = (t, -1)
        return best

    def lens(self, centre, marker):
        """(lens, forward) of the hip camera turned to put marker in the middle of the screen, the arm pulled
        in where it would go into a building or under the pavement."""
        cam = REACH_CAMERA
        lens = centre
        yaw = math.atan2(marker[1] - centre[1], marker[0] - centre[0])
        pitch = math.atan2(marker[2] - centre[2], math.hypot(marker[0] - centre[0], marker[1] - centre[1]))
        fwd = (1.0, 0.0, 0.0)
        for _ in range(3):
            p = max(math.radians(REACH_PITCH[0]), min(math.radians(REACH_PITCH[1]), pitch))
            a = max(0.0, min(1.0, (math.degrees(p) - cam["pitch"][0]) / (cam["pitch"][1] - cam["pitch"][0])))
            arm = cam["arm"][0] + (cam["arm"][1] - cam["arm"][0]) * a
            sz = cam["socket_z"][0] + (cam["socket_z"][1] - cam["socket_z"][0]) * a
            lift = cam["lift"][0] + (cam["lift"][1] - cam["lift"][0]) * a
            cp, sp, cy, sy = math.cos(p), math.sin(p), math.cos(yaw), math.sin(yaw)
            fwd = (cp * cy, cp * sy, sp)
            up = (-sp * cy, -sp * sy, cp)
            origin = (centre[0], centre[1], centre[2] + lift)
            want = (origin[0] - fwd[0] * arm - sy * cam["socket_y"] + up[0] * sz,
                    origin[1] - fwd[1] * arm + cy * cam["socket_y"] + up[1] * sz,
                    origin[2] - fwd[2] * arm + up[2] * sz)
            # The probe: back towards the pivot until the lens is out of the walls and above the pavement.
            lo, hi = 0.0, 1.0
            if self._lens_blocked(want):
                for _step in range(8):
                    mid = (lo + hi) * 0.5
                    q = tuple(origin[k] + (want[k] - origin[k]) * mid for k in range(3))
                    if self._lens_blocked(q):
                        hi = mid
                    else:
                        lo = mid
                want = tuple(origin[k] + (want[k] - origin[k]) * lo for k in range(3))
            lens = want
            yaw = math.atan2(marker[1] - lens[1], marker[0] - lens[0])
            pitch = math.atan2(marker[2] - lens[2], math.hypot(marker[0] - lens[0], marker[1] - lens[1]))
        p = max(math.radians(REACH_PITCH[0]), min(math.radians(REACH_PITCH[1]), pitch))
        fwd = (math.cos(p) * math.cos(yaw), math.cos(p) * math.sin(yaw), math.sin(p))
        return lens, fwd

    def _lens_blocked(self, q):
        probe = REACH_CAMERA["probe"]
        if q[2] < SIDEWALK_TOP + probe:
            i = self.building_at(q)
            if i is None:
                return True
        return self.solid_at((q[0], q[1], q[2] - probe))

    def seen(self, centre, spot, own=None):
        """The camera turned to the anchor has its marker in the cone and in view (SightTolerance)."""
        marker = _anchor_marker(spot)
        lens, fwd = self.lens(centre, marker)
        d = (marker[0] - lens[0], marker[1] - lens[1], marker[2] - lens[2])
        dist = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
        if dist < 1.0:
            return False
        cosine = (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / dist
        if math.degrees(math.acos(max(-1.0, min(1.0, cosine)))) > REACH_CONE - REACH_CONE_SLACK:
            return False
        hit = self.first_block(lens, marker)
        return hit is None or (1.0 - hit[0]) * dist <= REACH_SIGHT_TOLERANCE

    # --- the zip: Kate's capsule swept along the line, as UGrappleComponent::IsZipClear does -------------
    #
    # Until 2026-11-11 the model swept a line at the capsule's feet and let her own building be crossed for
    # the first 164 cm of it. The game sweeps the whole capsule (34 cm round, 88 half height) and ignores
    # only what lies within 250 cm of where she stood (to the release point, 284 cm out) and within 150 cm of
    # the anchor in 3D. The line was kinder on steep zips over a parapet's edge, on a low neighbour beside a
    # corner anchor (2D 150 cm, but metres below it), and on a line that stays inside her own building past
    # the release point: 1018 of 6859 sidewalk pairs the line passed were blocked in the game (the traversal
    # audit's pair probe), and 5 street-facing roofs had no anchor the street could zip to.

    def _obstacles_near(self, x0, y0, x1, y1):
        """[(kind, index)] of the buildings ("b"), fire-escape landings ("l") and lamps ("c") whose cells the box
        touches."""
        out = [("b", i) for i in self.near(x0, y0, x1, y1)]
        landings, columns = set(), set()
        for gx in range(int(math.floor(min(x0, x1) / REACH_CELL)), int(math.floor(max(x0, x1) / REACH_CELL)) + 1):
            for gy in range(int(math.floor(min(y0, y1) / REACH_CELL)), int(math.floor(max(y0, y1) / REACH_CELL)) + 1):
                landings.update(self.landing_grid.get((gx, gy), ()))
                columns.update(self.column_grid.get((gx, gy), ()))
        return out + [("l", k) for k in sorted(landings)] + [("c", k) for k in sorted(columns)]

    def _obstacle_gap(self, obstacle, p, core):
        """Distance from the vertical segment p.z - core .. p.z + core at p's XY to the obstacle (0 inside it):
        with core 0, from the point; with the capsule's core, the capsule touches it under its radius."""
        kind, k = obstacle
        lo, hi = p[2] - core, p[2] + core
        if kind == "c":
            x, y, z0, z1, radius = self.columns[k]
            dh = max(0.0, math.hypot(p[0] - x, p[1] - y) - radius)
            dv = max(0.0, lo - z1, z0 - hi)
            return math.hypot(dh, dv)
        if kind == "b":
            _rec, ring, box, top, _roof = self.tops[k]
            z0, z1 = -1.0e9, top
        else:
            ring, box, z0, z1 = self.landings[k]
        dv = max(0.0, lo - z1, z0 - hi)
        # The box first: a cheap lower bound that settles most obstacles far from the line.
        bx = max(box[0] - p[0], 0.0, p[0] - box[2])
        by = max(box[1] - p[1], 0.0, p[1] - box[3])
        if bx > 0.0 or by > 0.0:
            far = math.hypot(math.hypot(bx, by), dv)
            if far > 150.0:
                return far
        dh = 0.0 if geo.point_in_polygon((p[0], p[1]), ring) else ring_distance((p[0], p[1]), ring)
        return math.hypot(dh, dv)

    def _supports(self, p, radius):
        """The obstacles within radius of the point p in 3D (the game's sphere overlap)."""
        return {o for o in self._obstacles_near(p[0] - radius, p[1] - radius, p[0] + radius, p[1] + radius)
                if self._obstacle_gap(o, p, 0.0) < radius}

    def _sweep_clear(self, a, b, obstacles):
        """The capsule (REACH_CAPSULE_RADIUS plus REACH_ZIP_MARGIN, standing upright) from a to b touches none of
        the obstacles. Sphere-traced: each step moves by the clearance just measured, which no obstacle can close."""
        length = math.sqrt(sum((b[k] - a[k]) ** 2 for k in range(3)))
        radius = REACH_CAPSULE_RADIUS + REACH_ZIP_MARGIN
        core = REACH_CAPSULE_HALF - REACH_CAPSULE_RADIUS
        if not obstacles:
            return True
        t = 0.0
        while True:
            f = t / length if length > 0.1 else 1.0
            p = (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f)
            gap = min(self._obstacle_gap(o, p, core) for o in obstacles) - radius
            if gap < 0.0:
                return False
            if t >= length:
                return True
            t = min(length, t + max(gap, 2.0))

    def zip_clear(self, feet, spot):
        """IsZipClear from a standing spot: the capsule from the launch point (REACH_ZIP_LAUNCH over her centre) to
        her centre over the landing touches no building (a prism to its parapet top), fire escape or lamp except
        the anchor's supports (the building under the landing and anything within REACH_SUPPORT_RADIUS of the
        anchor's origin) and, until the release point (REACH_START_RELEASE from her centre along the line), the
        start's (anything within REACH_START_IGNORE of her centre)."""
        centre = (feet[0], feet[1], feet[2] + REACH_HALF_HEIGHT)
        launch = (centre[0], centre[1], centre[2] + REACH_ZIP_LAUNCH)
        landing = _anchor_landing(spot)
        end = (landing[0], landing[1], landing[2] + REACH_HALF_HEIGHT)
        target = self.building_at((landing[0], landing[1]))
        supports = self._supports((spot[0], spot[1], spot[2]), REACH_SUPPORT_RADIUS)
        if target is not None:
            supports.add(("b", target))
        start = self._supports(centre, REACH_START_IGNORE)
        # The release point: the far root of |launch + dir s - centre| = REACH_START_RELEASE (ComputeStartReleasePoint).
        line = [end[k] - launch[k] for k in range(3)]
        length = math.sqrt(sum(v * v for v in line))
        if length <= 1e-3:
            return True
        unit = [v / length for v in line]
        offset = [launch[k] - centre[k] for k in range(3)]
        bb = sum(offset[k] * unit[k] for k in range(3))
        cc = sum(v * v for v in offset) - REACH_START_RELEASE * REACH_START_RELEASE
        disc = bb * bb - cc
        s = -bb + math.sqrt(disc) if disc >= 0.0 else 0.0
        s = max(0.0, min(length, s))
        release = tuple(launch[k] + unit[k] * s for k in range(3))
        grow = REACH_CAPSULE_RADIUS + REACH_ZIP_MARGIN
        near = self._obstacles_near(min(launch[0], end[0]) - grow, min(launch[1], end[1]) - grow,
                                    max(launch[0], end[0]) + grow, max(launch[1], end[1]) + grow)
        if not self._sweep_clear(launch, release, [o for o in near if o not in supports and o not in start]):
            return False
        return self._sweep_clear(release, end, [o for o in near if o not in supports])

    def targetable(self, feet, spot):
        centre = (feet[0], feet[1], feet[2] + REACH_HALF_HEIGHT)
        marker = _anchor_marker(spot)
        dist = math.sqrt(sum((marker[k] - centre[k]) ** 2 for k in range(3)))
        if dist > REACH_RANGE - REACH_RANGE_SLACK or dist < REACH_MIN_RANGE + REACH_RANGE_SLACK:
            return False
        return self.seen(centre, spot) and self.zip_clear(feet, spot)

    # --- the guarantees ------------------------------------------------------------------------

    def neighbours(self):
        """{building index: sorted [(index, gap cm)]} of the roofs within REACH_NEIGHBOUR_GAP."""
        out = {i: [] for i in range(len(self.tops))}
        for i, (_r, ring, box, _t, _z) in enumerate(self.tops):
            for j in self.near(box[0] - REACH_NEIGHBOUR_GAP, box[1] - REACH_NEIGHBOUR_GAP,
                               box[2] + REACH_NEIGHBOUR_GAP, box[3] + REACH_NEIGHBOUR_GAP):
                if j <= i:
                    continue
                other = self.tops[j][1]
                gap = min(min(closest_point_on_polyline(p, list(other) + [other[0]])[0] for p in ring),
                          min(closest_point_on_polyline(p, list(ring) + [ring[0]])[0] for p in other))
                if geo.point_in_polygon(ring[0], other) or geo.point_in_polygon(other[0], ring):
                    gap = 0.0
                if gap <= REACH_NEIGHBOUR_GAP:
                    out[i].append((j, gap))
                    out[j].append((i, gap))
        for i in out:
            out[i].sort()
        return out


    def pair_in_range(self, i, j):
        """Some roof spot on i stands within the grapple's range (less the slack) of the top of j's walls."""
        ring = self.tops[j][1]
        z = self.tops[j][3] - 20.0
        for feet in self.roof_spots[self.tops[i][0]["id"]]:
            flat = closest_point_on_polyline((feet[0], feet[1]), list(ring) + [ring[0]])[0]
            if math.hypot(flat, z - feet[2] - REACH_HALF_HEIGHT) <= REACH_RANGE - REACH_RANGE_SLACK:
                return True
        return False

    def across(self, i):
        """[(j, gap, point on j, point on i)] of the roofs facing roof i across a street or gap: footprints
        REACH_TOUCHING to REACH_NEIGHBOUR_GAP apart whose closest points see each other over no third
        building standing as high as the lower of the two (cached)."""
        cache = self.__dict__.setdefault("_across", {})
        if i in cache:
            return cache[i]
        if "_neighbours" not in self.__dict__:
            self._neighbours = self.neighbours()
        ring = self.tops[i][1]
        out = []
        for j, gap in self._neighbours[i]:
            if gap <= REACH_TOUCHING:
                continue
            other = self.tops[j][1]
            best = None
            for pt in other:
                d, c_pt = closest_point_on_polyline(pt, list(ring) + [ring[0]])
                if best is None or d < best[0]:
                    best = (d, pt, c_pt)
            for pt in ring:
                d, c_pt = closest_point_on_polyline(pt, list(other) + [other[0]])
                if best is None or d < best[0]:
                    best = (d, c_pt, pt)
            _d, p, q = best
            low = min(self.tops[i][3], self.tops[j][3])
            blocked = False
            for k in self.near(p[0], p[1], q[0], q[1]):
                if k in (i, j) or self.tops[k][3] < low - 100.0:
                    continue
                ts = _segment_params(p, q, self.tops[k][1])
                if any(0.01 < t < 0.99 for t in ts):
                    blocked = True
                    break
            if not blocked:
                out.append((j, gap, p, q))
        cache[i] = out
        return out


def _outward_edges(ring):
    """[(index, a, b, length, (unit along), (outward normal))] for every edge."""
    inward = 1.0 if geo.is_ccw(ring) else -1.0
    n = len(ring)
    out = []
    for i in range(n):
        (ax, ay), (bx, by) = ring[i], ring[(i + 1) % n]
        length = math.hypot(bx - ax, by - ay)
        if length < 1.0:
            continue
        ux, uy = (bx - ax) / length, (by - ay) / length
        out.append((i, (ax, ay), (bx, by), length, (ux, uy), (uy * inward, -ux * inward)))
    return out


def _along_options(length, preferred=None):
    """Where along an edge of this length (cm from its start) a facade anchor is tried, in order: the
    preferred spot, the middle, then either side of a fire escape in the middle, then out towards the ends."""
    raw = ([preferred] if preferred is not None else []) + [length * 0.5]
    raw += [length * 0.5 - FACADE_ANCHOR_ESCAPE_CLEAR - 5.0, length * 0.5 + FACADE_ANCHOR_ESCAPE_CLEAR + 5.0]
    raw += [length * t for t in FACADE_ANCHOR_SPOTS[1:]]
    raw += [FACADE_ANCHOR_END_CLEAR, length - FACADE_ANCHOR_END_CLEAR]
    out = []
    for a in raw:
        a = round(max(FACADE_ANCHOR_END_CLEAR, min(length - FACADE_ANCHOR_END_CLEAR, a)), 1)
        if a not in out and FACADE_ANCHOR_END_CLEAR <= a <= length - FACADE_ANCHOR_END_CLEAR:
            out.append(a)
    return out


def _facade_anchor(reach, index, edge, along, placed, escapes, allow_escape=False):
    """The facade anchor along cm along edge of building index, or None where it would be buried in a
    neighbour, too near an end or another anchor, land off the roof, or (unless allow_escape) stand over
    the building's fire escape, where only a view from across the street clears the landings."""
    rec, ring, _box, top, roof = reach.tops[index]
    _i, (ax, ay), (bx, by), length, (ux, uy), (ox, oy) = edge
    if along < FACADE_ANCHOR_END_CLEAR or length - along < FACADE_ANCHOR_END_CLEAR:
        return None
    px, py = ax + ux * along, ay + uy * along
    over_escape = any(math.hypot(ex - px, ey - py) < FACADE_ANCHOR_ESCAPE_CLEAR for ex, ey in escapes.get(rec["id"], ()))
    if over_escape and not allow_escape:
        return None
    z = top - FACADE_ANCHOR_DOWN
    x, y = px + ox * FACADE_ANCHOR_OUT, py + oy * FACADE_ANCHOR_OUT
    # Open air in front of it up to the parapet top: no neighbour standing that high against the wall.
    for s in (-40.0, 0.0, 40.0):
        for out in (30.0, 80.0):
            q = (px + ox * out + ux * s, py + oy * out + uy * s)
            j = reach.building_at(q)
            if j is not None and reach.tops[j][3] > z - 20.0:
                return None
    lx, ly = x - ox * FACADE_ANCHOR_LANDING, y - oy * FACADE_ANCHOR_LANDING
    if not geo.point_in_polygon((lx, ly), ring) or _edge_distance((lx, ly), ring) < ANCHOR_LANDING_CLEARANCE:
        return None
    if any(math.hypot(qx - x, qy - y) < FACADE_ANCHOR_MIN_GAP for qx, qy, _qz, _a, _b, _c, _d in placed):
        return None
    yaw = math.degrees(math.atan2(-oy, -ox))
    return (x, y, z, yaw, FACADE_ANCHOR_LANDING, z - roof, rec["id"])


def _street_edges(reach, index):
    """The building's open walls facing a road (no building standing as high in front of them), widest road
    first, then nearest, then longest."""
    _rec, ring, _box, top, _roof = reach.tops[index]
    out = []
    for edge in _outward_edges(ring):
        _i, a, b, length, (ux, uy), (ox, oy) = edge
        if length < FACADE_ANCHOR_MIN_EDGE:
            continue
        mx, my = (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5
        covered = 0
        for t in (0.25, 0.5, 0.75):
            j = reach.building_at((a[0] + ux * length * t + ox * 150.0, a[1] + uy * length * t + oy * 150.0))
            covered += 1 if j is not None and reach.tops[j][3] >= top - 100.0 else 0
        if covered >= 2:
            continue
        best = None
        for p, q, half in reach.roads:
            d, (cx, cy) = closest_point_on_polyline((mx, my), [p, q])
            if d < 1.0 or d > half + FACADE_ANCHOR_ROAD_REACH:
                continue
            if ((cx - mx) * ox + (cy - my) * oy) / d < FIRE_ESCAPE_FACING:
                continue
            key = (-half, d)
            if best is None or key < best:
                best = key
        if best is not None:
            out.append((best[0], best[1], -length, edge[0], edge))
    out.sort()
    return [entry[4] for entry in out]


def _street_anchor_search(reach, index, placed, escapes, street_near, street, edges):
    """(chosen, fallback, spots) for building index's street facade anchor: over edges in order, the first
    position seen from REACH_STREET_ROBUST sidewalk spots wins, else the one reached from the most (spots, up to
    REACH_STREET_ROBUST); fallback is the first that fits on a street edge clear of the fire escape."""
    fallback = None
    chosen = None
    best_seen = 0
    for edge in edges:
        # Straight above a sidewalk spot in front of the edge first (the lens then looks up the wall at
        # it), the nearest spot first; then the usual places along the edge.
        _k, a, b, length, (ux, uy), (ox, oy) = edge
        feet_along = []
        for feet in street_near((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5):
            out = (feet[0] - a[0]) * ox + (feet[1] - a[1]) * oy
            along = (feet[0] - a[0]) * ux + (feet[1] - a[1]) * uy
            if 0.0 < out < REACH_RANGE * 0.5 and 0.0 <= along <= length:
                feet_along.append((out, along))
        feet_along.sort()
        options = []
        for _out, along in feet_along:
            for extra in _along_options(length, along)[:1]:
                if extra not in options:
                    options.append(extra)
        options += [x for x in _along_options(length) if x not in options]
        # Clear of the fire escape first; over it only where a spot across the street sees past it. The
        # first seen from REACH_STREET_ROBUST sidewalk spots wins; else the one seen from the most.
        for allow_escape in (False, True):
            for along in options:
                spot = _facade_anchor(reach, index, edge, along, placed, escapes, allow_escape)
                if spot is None:
                    continue
                if edge in street and not allow_escape:
                    fallback = fallback or spot
                seen = 0
                for feet in street_near(spot[0], spot[1]):
                    if reach.targetable(feet, spot):
                        seen += 1
                        if seen >= REACH_STREET_ROBUST:
                            break
                if seen > best_seen:
                    chosen, best_seen = spot, seen
                if best_seen >= REACH_STREET_ROBUST:
                    break
            if best_seen >= REACH_STREET_ROBUST:
                break
        if best_seen >= REACH_STREET_ROBUST:
            break
    return chosen, fallback, best_seen


def _street_cells(reach):
    """street_near(x, y): the sidewalk spots in the REACH_CELL cells round a point."""
    cells = {}
    for feet in reach.street_spots:
        cells.setdefault((int(math.floor(feet[0] / REACH_CELL)), int(math.floor(feet[1] / REACH_CELL))), []).append(feet)

    def street_near(x, y):
        cx, cy = int(math.floor(x / REACH_CELL)), int(math.floor(y / REACH_CELL))
        return [f for dx in (-1, 0, 1) for dy in (-1, 0, 1) for f in cells.get((cx + dx, cy + dy), ())]
    return street_near


def _first_floor_escapes(escapes):
    out = {}
    for osm, floor, (x, y, _z), _yaw, _drop, _side in escapes:
        if floor == 1:
            out.setdefault(osm, []).append((x, y))
    return out


def escapes_blocking_street(district, plan):
    """The osm ids whose fire escape (in plan, fire_escape_spots' rows before this rule) leaves the street no way
    up: the building has an open wall on a street, its escape stands on one, no facade anchor anywhere on it is
    reached from any sidewalk spot with the escape there, and one is with it gone.

    A tenement's street wall can be 4 to 7.5 m: its escape (240 cm wide, out 95 cm, landings every 330 cm up)
    fills the middle, the sidewalk under it sees the parapet only past the landings, and every other sidewalk spot
    in range stands in front of a neighbour (whose wall or escape is in the way). Where the far sidewalk is out of
    range too (an avenue, or a roof over 16 m) nothing on the pavement reaches the roof. Such a building gets no
    fire escape: its roof is grappled from the pavement instead. Only where no spot reaches at all: a roof some
    spot reaches keeps its escape, even when fewer than REACH_STREET_ROBUST do."""
    if not plan:
        return set()
    reach = GrappleReach(district, escapes=plan)
    placed = list(_corner_anchor_spots(district))
    street_near = _street_cells(reach)
    escapes = _first_floor_escapes(plan)

    def best(model, i, esc):
        street = _street_edges(model, i)
        others = sorted((e for e in _outward_edges(model.tops[i][1]) if e[3] >= FACADE_ANCHOR_MIN_EDGE
                         and all(e[0] != f[0] for f in street)), key=lambda e: (-e[3], e[0]))
        return _street_anchor_search(model, i, placed, esc, street_near, street, street + others)[2]

    candidates = []
    for i, t in enumerate(reach.tops):
        osm = t[0]["id"]
        if t[0]["height_m"] <= ANCHOR_MIN_HEIGHT_M or osm not in escapes:
            continue
        street = _street_edges(reach, i)
        if not any(closest_point_on_polyline(xy, [e[1], e[2]])[0] < 50.0 for e in street for xy in escapes[osm]):
            continue
        if not _street_anchor_search(reach, i, placed, escapes, street_near, street, street)[2] and not best(reach, i, escapes):
            candidates.append(osm)
    if not candidates:
        return set()
    without = [s for s in plan if s[0] not in candidates]
    reach_wo = GrappleReach(district, escapes=without)
    escapes_wo = _first_floor_escapes(without)
    return {osm for osm in candidates if best(reach_wo, reach_wo.index[osm], escapes_wo) > 0}


def _roof_reached(reach, feet_list, spots, target_osm):
    for feet in feet_list:
        for spot in spots:
            if spot[6] == target_osm and reach.targetable(feet, spot):
                return True
    return False


def grapple_reach(district):
    """The GrappleReach for this district, built once."""
    cached = getattr(district, "_grapple_reach", None)
    if cached is None:
        cached = GrappleReach(district)
        district._grapple_reach = cached
    return cached


def anchor_spots(district):
    """[(x, y, z, yaw, landing_forward, landing_drop, osm id)] in a stable order: the parapet anchors
    (_corner_anchor_spots), then the facade anchors.

    Facade anchors: on the outer face of the parapet, FACADE_ANCHOR_OUT out from the wall, the fitting's
    top flush with the parapet top, +X inboard to a landing point FACADE_ANCHOR_LANDING in (clear of the
    parapet). Every building over ANCHOR_MIN_HEIGHT_M gets one on the edge facing its widest street, at the
    first of FACADE_ANCHOR_SPOTS along it that a sidewalk spot can reach (the first that fits when none
    can), clear of its fire escape. Then, for every neighbour within REACH_NEIGHBOUR_GAP (not sharing a
    wall) whose roof spots reach none of its anchors, one on its edge facing that neighbour, where one of
    those spots reaches it. Cached on the district: the reach model takes a few seconds.
    """
    cached = getattr(district, "_anchor_spots", None)
    if cached is not None:
        return list(cached)
    placed = list(_corner_anchor_spots(district))
    reach = grapple_reach(district)
    escapes = {}
    for osm, floor, (x, y, _z), _yaw, _drop, _side in fire_escape_spots(district):
        if floor == 1:
            escapes.setdefault(osm, []).append((x, y))
    street_cells = {}
    for feet in reach.street_spots:
        street_cells.setdefault((int(math.floor(feet[0] / REACH_CELL)), int(math.floor(feet[1] / REACH_CELL))), []).append(feet)

    def street_near(x, y):
        cx, cy = int(math.floor(x / REACH_CELL)), int(math.floor(y / REACH_CELL))
        return [f for dx in (-1, 0, 1) for dy in (-1, 0, 1) for f in street_cells.get((cx + dx, cy + dy), ())]

    anchored = [i for i, t in enumerate(reach.tops) if t[0]["height_m"] > ANCHOR_MIN_HEIGHT_M]
    for i in anchored:
        # The widest street's edge first; a narrower street's only when no spot on it can be reached, and
        # then any open wall a sidewalk round the corner can see.
        street = _street_edges(reach, i)
        others = sorted((e for e in _outward_edges(reach.tops[i][1]) if e[3] >= FACADE_ANCHOR_MIN_EDGE
                         and all(e[0] != f[0] for f in street)), key=lambda e: (-e[3], e[0]))
        chosen, fallback, _best = _street_anchor_search(reach, i, placed, escapes, street_near, street, street + others)
        if chosen or fallback:
            placed.append(chosen or fallback)

    by_osm = {}
    for spot in placed:
        by_osm.setdefault(spot[6], []).append(spot)
    for i in anchored:
        osm = reach.tops[i][0]["id"]
        for j, _gap, _p, q in reach.across(i):
            feet_list = reach.roof_spots[reach.tops[j][0]["id"]]
            if not feet_list or _roof_reached(reach, feet_list, by_osm.get(osm, []), osm):
                continue
            # This roof's edges that look across at the neighbour, the one nearest it first, each tried
            # at the point nearest the neighbour's roof spots and then along it.
            options = []
            for edge in _outward_edges(reach.tops[i][1]):
                _k, a, b, length, (ux, uy), (ox, oy) = edge
                if length < FACADE_ANCHOR_MIN_EDGE:
                    continue
                d, (qx, qy) = closest_point_on_polyline(q, [a, b])
                facing = [f for f in feet_list if (f[0] - qx) * ox + (f[1] - qy) * oy > 0.0]
                if not facing or d > 300.0:
                    continue
                near_feet = min(facing, key=lambda f: math.hypot(f[0] - qx, f[1] - qy))
                pref = (near_feet[0] - a[0]) * ux + (near_feet[1] - a[1]) * uy
                options.append((d, edge[0], edge, pref))
            options.sort(key=lambda o: (o[0], o[1]))
            done = False
            for _d, _k, edge, pref in options:
                for along in _along_options(edge[3], pref):
                    spot = _facade_anchor(reach, i, edge, along, placed, escapes, allow_escape=True)
                    if spot is not None and any(reach.targetable(feet, spot) for feet in feet_list):
                        placed.append(spot)
                        by_osm.setdefault(osm, []).append(spot)
                        done = True
                        break
                if done:
                    break
    district._anchor_spots = list(placed)
    return list(placed)
