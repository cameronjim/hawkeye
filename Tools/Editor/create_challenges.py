"""Create the district's side challenges from its geometry (claude-docs/gameplay-semantics.md,
"Side challenges").

    /Game/Challenges/DA_Challenge_archery_<n>     UChallengeDefinition, three archery ranges
    /Game/Challenges/DA_Challenge_traversal_<n>   UChallengeDefinition, three traversal routes

Everything is planned from the same OpenStreetMap records generate_city.py builds the district from
(footprints, heights, parapets, grapple anchors, fire escapes, clutter), so the challenges move with
the city and nothing is placed by hand:

* archery, from three rooftops within 250 m of the PlayerStart: a shooting spot on a roof 10 to 25 m
  tall with twelve targets 10 to 40 m from it on other roofs and on fire-escape landings, every line
  from the bow to a target's centre and to its bottom edge clear of every building, parapet, rooftop
  prop, lamp and fire escape. Up to four (at least three) slide 6 m along the roof edge they stand
  behind; up to four (at least three) stand on landings. 60 s; medals at 36 / 60 / 84 points.
* traversal, from three street corners anywhere in the district (routes with a mantle are rare): eight
  checkpoint rings, 200 cm across: along the sidewalk, a grapple to a roof, across it, a grapple to
  another roof, across it, a grapple to a third, to the spot behind the top landing of a fire escape,
  and down the escape to the street; one of the roof legs is a mantle onto a neighbour a step (30 to
  110 cm) up. Every grapple is within 2200 cm of where she stands, no steeper than 55 degrees, in view
  of her camera, and clear by the grapple's own rule (the capsule's feet from 120 cm up, her own roof
  ignored for the first 284 cm, the anchor's building all the way). 150 s; medals under 120 / 90 / 60 s.

Challenge names are bracketed placeholders ("[Archery challenge 1]") until the side content is
written. generate_city.py places a City_Challenge_<id> pedestal (AChallengeStart) at each
definition's StartLocation; verify_city.py counts them and checks every checkpoint is reachable.

Idempotent: an asset is rewritten only when its planned values differ from what it holds.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\create_challenges.py" -unattended -nullrhi -nosplash -nop4 -stdout
"""

import math
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _geo as geo  # noqa: E402
import generate_city as gen  # noqa: E402

CHALLENGE_PATH = "/Game/Challenges"
ASSET_PREFIX = "DA_Challenge_"

SEARCH_RADIUS = 25000.0         # cm from the PlayerStart an archery range may start: the block and its streets
TRAVERSAL_SEARCH_RADIUS = 45000.0  # cm; routes with a mantle are rarer, so the whole district is searched
AREA_RADIUS = 10000.0           # cm; leaving this far from the start fails a run
CALM_RADIUS = 6000.0            # cm; thugs this close to the start go calm for the run

# --- archery ---------------------------------------------------------------------------------------
ARCHERY_COUNT = 3
ARCHERY_TARGETS = 12
ARCHERY_MOVING = 4
ARCHERY_ON_ESCAPES = 4
ARCHERY_MIN_KIND = 3            # at least this many movers and landing targets, or the spot is no good
ARCHERY_TIME = 60.0
ARCHERY_MEDALS = (36.0, 60.0, 84.0)     # bronze, silver, gold: points
ARCHERY_ROOF_HEIGHT = (10.0, 25.0)      # m, the shooting roof
TARGET_MIN_DISTANCE = 1000.0
TARGET_MAX_DISTANCE = 4000.0
TARGET_FACE = 150.0             # cm from the foot of the post to the face's centre
TARGET_RADIUS = 30.0            # the 60 cm face
TARGET_EDGE_INSET = 130.0       # cm in from a roof edge a target's post stands
TARGET_EDGE_CLEAR = 110.0       # cm from any roof edge, wherever it slides
TARGET_SPACING = 400.0          # cm between two targets
TARGET_MOVER_SPACING = 800.0    # cm between a mover's centre and any other target
TRACK_LENGTH = 600.0
TRACK_SPEED = 150.0
TRACK_SAMPLES = 5
TRACK_MAX_ALONG_FIRE = 0.7      # cos of the narrowest angle between a mover's track and the line of fire
TARGET_PITCH_LIMIT = 35.0       # degrees the face may tilt toward the shooter
LANDING_OUT = 50.0              # cm from the facade to a landing's middle (5 cm gap, 90 cm slab)
LANDING_TARGET_SLOPE = 0.6      # a shooter below a landing: the line to its face clears the rail
LANDING_FACING = 0.2            # the shooter at least this far round in front of the facade (cos)
BOW_HEIGHT = 150.0              # cm above her feet the arrow leaves from
LINE_MARGIN = 20.0              # cm a line clears a roof, parapet or prop by
STAND_GRID = 350.0              # cm between shooting spots tried on a roof
STAND_EDGE_CLEAR = 120.0        # cm from a roof edge to the shooting spot and the pedestal
PEDESTAL_BACK = 150.0           # cm from the pedestal back to where she stands (AChallengeStart::GetStandLocation)
PROP_CLEAR = 90.0               # cm from a rooftop prop to a pedestal, a spot or a target
ARCHERY_START_APART = 3000.0    # cm between two archery pedestals
ARCHERY_MIN_LANDINGS_FACING = 8 # landings facing a roof's middle before its spots are tried

# --- traversal -------------------------------------------------------------------------------------
TRAVERSAL_COUNT = 3
TRAVERSAL_TIME = 150.0
TRAVERSAL_MEDALS = (120.0, 90.0, 60.0)  # bronze, silver, gold: seconds to beat
CHECKPOINT_UP = 110.0           # cm from the floor to a ring's centre (her capsule centre is about 90)
CHECKPOINT_RADIUS = 150.0
GRAPPLE_RANGE = 2500.0          # cm, UGrappleComponent::Range
GRAPPLE_MIN = 700.0             # cm; shorter zips are not worth a checkpoint
GRAPPLE_SLACK = 300.0           # cm under the range a planned zip stays: she fires from anywhere in the ring
ZIP_LAUNCH = 120.0              # cm the 0.15 s hop lifts her feet before the straight line (ZipLaunchHeight)
ZIP_CAPSULE_RADIUS = 34.0       # Kate's capsule
ZIP_START_RELEASE = 284.0       # cm from where she stood her own roof counts again (ZipStartIgnoreRadius + radius)
ZIP_FEET_CLEAR = 10.0           # cm her feet clear a parapet by on a planned zip
CAMERA_BACK = 330.0             # cm behind her the lens is when she looks at an anchor (the 350 cm arm, pulled in a little)
CAMERA_UP = 160.0               # cm above her feet
CAMERA_SIGHT_TOLERANCE = 60.0   # cm short of the marker a hit still counts as seeing it
GRAPPLE_MAX_PITCH = 55.0        # degrees up to an anchor, at most: steeper and the camera cannot hold it in view
LANDING_RADIUS = 130.0          # cm; a fire-escape landing (240 x 90 cm) as a column for the line checks
RUN_MIN = 600.0
RUN_MAX = 2000.0
STREET_RUN = (900.0, 1800.0)    # cm along the sidewalk to the first ring
MANTLE_STEP = (30.0, 110.0)     # cm a neighbour's roof may stand above hers: parapet plus step within 200
ROOF_PATH_CLEAR = 60.0          # cm from a roof edge a run keeps (except where it crosses to a neighbour)
PATH_PROP_CLEAR = 80.0          # cm from any prop to a run
DESCENT_BACK = 110.0            # cm back from the facade over the top landing (the lap's descent stand)
DESCENT_OUT = 140.0             # cm out from the facade the street ring stands
CORNER_OUT = 250.0              # cm out from a building's corner along its bisector for a street pedestal
CORNER_TURN = 60.0              # degrees the outline turns at a block corner
CORNER_EDGE = 100.0             # cm; both edges at the corner at least this long
STREET_HUG = 450.0              # cm from a building's outline a street spot stays within
STREET_INTO_ROAD = 600.0        # cm into a carriageway (past its kerb) a street spot may stand
CORNER_ROADS = 3000.0           # cm from the corner to two different roads' centre lines, at most (an avenue is 12.5 m to its kerb)
TRAVERSAL_START_APART = 5000.0  # cm between two traversal pedestals
ARCHERY_TRAVERSAL_APART = 3000.0  # cm between a traversal pedestal and an archery one
ROUTE_REACH = 8000.0            # cm from the pedestal every ring stays within (the area is 100 m)
ROUTE_BRANCH = 8                # anchors tried from each spot while searching for a route
ROUTE_OPTIONS = 20              # spots with a grapple tried per roof leg

GRID_CELL = 2000.0              # cm; the buildings are bucketed this coarse for point lookups


# --------------------------------------------------------------------------------------------------
# the district, once
# --------------------------------------------------------------------------------------------------


class World(object):
    """Everything the planners read, computed once from the records."""

    def __init__(self, district):
        self.district = district
        start, _rot = gen.player_start_transform(district)
        self.start = (start.x, start.y)
        self.tops = gen._building_tops(district)            # (rec, ring, bounds, top z)
        self.by_id = {t[0]["id"]: t for t in self.tops}
        self._index()
        taken = {rec["id"] for rec in gen.objective_roofs(district).values()}
        taken |= {p[5] for p in gen.archer_placements(district)}
        safehouse = gen.safehouse_spot(district)
        if safehouse is not None:
            taken.add(safehouse["rec"]["id"])
        _thugs, _points, roof_rec = gen.thug_placements(district)
        if roof_rec is not None:
            taken.add(roof_rec["id"])
        self.taken = taken
        self.anchors = []   # (marker x, y, z, landing x, y, z, osm)
        for x, y, z, yaw, forward, drop, osm in gen.anchor_spots(district):
            lx, ly = x + math.cos(math.radians(yaw)) * forward, y + math.sin(math.radians(yaw)) * forward
            self.anchors.append((x, y, z + 20.0, lx, ly, z - drop, osm))
        self.escapes = {}   # osm -> [(floor, (x, y, z), yaw, out unit)]
        for osm, floor, (x, y, z), yaw, _drop, _side in gen.fire_escape_spots(district):
            out = (-math.sin(math.radians(yaw)), math.cos(math.radians(yaw)))
            self.escapes.setdefault(osm, []).append((floor, (x, y, z), yaw, out))
        for landings in self.escapes.values():
            landings.sort()
        self.props = []     # (x, y, base z, top z, radius, kind)
        plan = gen.clutter_plan(district)
        for kind in sorted(plan):
            radius = gen.CLUTTER_KINDS[kind][4]
            height = gen.CLUTTER_HEIGHTS.get(kind, 120.0 if kind.startswith("ParkedCar") else 100.0)
            if kind == "Scaffold":
                radius = 160.0
            for x, y, z, _yaw, scale in plan[kind]:
                self.props.append((x, y, z, z + height * scale, radius * scale, kind))
        # Every fire escape, its landings and the ladders between them, as one column the zips and the
        # camera must miss: from under the lowest slab to the top landing's rail.
        self.landings = []
        for landings in self.escapes.values():
            _floor, (x, y, _z), _yaw, out = landings[0]
            cx, cy = x + out[0] * LANDING_OUT, y + out[1] * LANDING_OUT
            low = min(l[1][2] for l in landings) - gen.FIRE_ESCAPE_SLAB[2]
            high = max(l[1][2] for l in landings) + gen.FIRE_ESCAPE_RAIL
            self.landings.append((cx, cy, low, high, LANDING_RADIUS, "FireEscape"))
        self.lamps = [(x, y) for x, y, _yaw, _s in gen.lamp_spots(district)]
        # The lamps' poles and heads block a line from the street as the clutter does.
        for x, y, yaw, _s in gen.lamp_spots(district):
            hx, hy = x + math.cos(math.radians(yaw)) * gen.LAMP_ARM, y + math.sin(math.radians(yaw)) * gen.LAMP_ARM
            self.props.append((x, y, 0.0, gen.LAMP_POLE_HEIGHT + 30.0, 20.0, "LampPole"))
            self.props.append((hx, hy, gen.LAMP_POLE_HEIGHT - 40.0, gen.LAMP_POLE_HEIGHT + 30.0, 40.0, "LampHead"))
        self.roads = []     # (a, b, half width, street name)
        for rec, paths in gen.road_paths(district):
            for path in paths:
                for a, b in zip(path, path[1:]):
                    self.roads.append((a, b, rec["width_m"] * 50.0, rec.get("name") or rec["id"]))

    def tops_near(self, x, y, reach):
        return [t for t in self.tops if not (t[2][0] > x + reach or t[2][2] < x - reach
                                             or t[2][1] > y + reach or t[2][3] < y - reach)]

    def building_at(self, pt):
        cell = (int(math.floor(pt[0] / GRID_CELL)), int(math.floor(pt[1] / GRID_CELL)))
        for t in self.grid.get(cell, ()):
            b = t[2]
            if b[0] <= pt[0] <= b[2] and b[1] <= pt[1] <= b[3] and geo.point_in_polygon(pt, t[1]):
                return t
        return None

    def _index(self):
        self.grid = {}
        for t in self.tops:
            b = t[2]
            for gx in range(int(math.floor(b[0] / GRID_CELL)), int(math.floor(b[2] / GRID_CELL)) + 1):
                for gy in range(int(math.floor(b[1] / GRID_CELL)), int(math.floor(b[3] / GRID_CELL)) + 1):
                    self.grid.setdefault((gx, gy), []).append(t)

    def roof_z(self, top):
        return top[0]["height_m"] * 100.0

    def props_near(self, x, y, reach):
        return [p for p in self.props if abs(p[0] - x) <= reach and abs(p[1] - y) <= reach]

    def landings_near(self, x, y, reach):
        return [p for p in self.landings if abs(p[0] - x) <= reach and abs(p[1] - y) <= reach]


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _dist3(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def _segment_point_distance(p, a, b):
    return gen.closest_point_on_polyline(p, [a, b])


def line_blocked(world, a3, b3, tops, ignore_ids=(), props=None):
    """Whether the straight line a3 -> b3 fails to clear a building (roof or parapet) or a prop."""
    blockers = [bid for bid in gen.archer_line_blockers(a3, b3, tops) if bid not in ignore_ids]
    if blockers:
        return True
    for x, y, z0, z1, radius, _kind in (props if props is not None else ()):
        d, q = _segment_point_distance((x, y), (a3[0], a3[1]), (b3[0], b3[1]))
        if d > radius + LINE_MARGIN:
            continue
        span = _dist2(a3, b3)
        t = _dist2(a3, q) / span if span > 1.0 else 0.0
        h = a3[2] + (b3[2] - a3[2]) * t
        if z0 - LINE_MARGIN < h < z1 + LINE_MARGIN:
            return True
    return False


def clear_of_props(world, pt, z, clearance, props):
    """No prop standing at this height within clearance (plus its radius) of pt."""
    for x, y, z0, z1, radius, _kind in props:
        if z0 - 50.0 <= z <= z1 + 50.0 and math.hypot(pt[0] - x, pt[1] - y) < radius + clearance:
            return False
    return True


def near_anchor(world, pt, z, clearance):
    for ax, ay, az, lx, ly, lz, _osm in world.anchors:
        if abs(lz - z) < 200.0 and (math.hypot(pt[0] - ax, pt[1] - ay) < clearance or math.hypot(pt[0] - lx, pt[1] - ly) < clearance):
            return True
    return False


# --------------------------------------------------------------------------------------------------
# archery
# --------------------------------------------------------------------------------------------------


def _edge_band_points(ring, inset, step):
    """(point, edge direction) inset cm inside every edge of ring, step cm apart."""
    pts = []
    n = len(ring)
    inward = 1.0 if geo.is_ccw(ring) else -1.0
    for i in range(n):
        ax, ay = ring[i]
        bx, by = ring[(i + 1) % n]
        length = math.hypot(bx - ax, by - ay)
        if length < 2.0 * inset:
            continue
        ux, uy = (bx - ax) / length, (by - ay) / length
        nx, ny = -uy * inward, ux * inward
        t = inset
        while t <= length - inset:
            pts.append(((ax + ux * t + nx * inset, ay + uy * t + ny * inset), (ux, uy)))
            t += step
    return pts


def _face_pitch(eye, centre):
    d = _dist2(eye, centre)
    pitch = math.degrees(math.atan2(eye[2] - centre[2], max(d, 1.0)))
    return max(-TARGET_PITCH_LIMIT, min(TARGET_PITCH_LIMIT, pitch))


def _target_lines_clear(world, eye, centre, tops, props):
    """The bow at eye sees the face's centre and its bottom edge past every building, prop and fire-escape
    landing (bar the one the target stands on)."""
    bottom = (centre[0], centre[1], centre[2] - TARGET_RADIUS * 0.8)
    near = world.landings_near((eye[0] + centre[0]) * 0.5, (eye[1] + centre[1]) * 0.5, _dist2(eye, centre) * 0.5 + 300.0)
    others = props + [l for l in near if math.hypot(l[0] - centre[0], l[1] - centre[1]) > 150.0]
    return not line_blocked(world, eye, centre, tops, props=others) and not line_blocked(world, eye, bottom, tops, props=others)


def archery_candidates(world, eye, own_id):
    """Roof targets (still and movable) and landing targets in range of a shooter's bow at eye."""
    reach = TARGET_MAX_DISTANCE + 500.0
    tops = world.tops_near(eye[0], eye[1], reach)
    props = world.props_near(eye[0], eye[1], reach)
    roof, movable, landing = [], [], []
    for top in tops:
        rec, ring, box = top[0], top[1], top[2]
        if rec["id"] == own_id or rec["id"] in world.taken or rec["height_m"] < gen.PARAPET_MIN_HEIGHT_M:
            continue
        roof_z = world.roof_z(top)
        for pt, along in _edge_band_points(ring, TARGET_EDGE_INSET, 250.0):
            d = _dist2(eye, pt)
            if d < TARGET_MIN_DISTANCE or d > TARGET_MAX_DISTANCE or gen._edge_clearance(pt, ring) < TARGET_EDGE_CLEAR:
                continue
            if not clear_of_props(world, pt, roof_z, PROP_CLEAR, props) or near_anchor(world, pt, roof_z, 120.0):
                continue
            centre = (pt[0], pt[1], roof_z + TARGET_FACE)
            if not _target_lines_clear(world, eye, centre, tops, props):
                continue
            cand = {"kind": "roof", "osm": rec["id"], "foot": (pt[0], pt[1], roof_z), "centre": centre, "distance": d}
            roof.append(cand)
            # Along the roof edge it stands behind, well across the line of fire, the whole track on
            # the roof and in sight.
            ax, ay = (pt[0] - eye[0]) / d, (pt[1] - eye[1]) / d
            sx, sy = along
            ok = abs(ax * sx + ay * sy) < TRACK_MAX_ALONG_FIRE
            for k in range(TRACK_SAMPLES if ok else 0):
                s = (k / (TRACK_SAMPLES - 1.0) - 0.5) * TRACK_LENGTH
                q = (pt[0] + sx * s, pt[1] + sy * s)
                if gen._edge_clearance(q, ring) < TARGET_EDGE_CLEAR or not clear_of_props(world, q, roof_z, PROP_CLEAR, props) \
                        or near_anchor(world, q, roof_z, 120.0) \
                        or not _target_lines_clear(world, eye, (q[0], q[1], centre[2]), tops, props):
                    ok = False
                    break
            if ok:
                movable.append(dict(cand, kind="moving", track=(sx, sy)))
    for osm, landings in world.escapes.items():
        if osm == own_id or osm not in world.by_id:
            continue
        for floor, (x, y, z), yaw, out in landings:
            foot = (x + out[0] * LANDING_OUT, y + out[1] * LANDING_OUT, z)
            d = _dist2(eye, foot)
            if d < TARGET_MIN_DISTANCE or d > TARGET_MAX_DISTANCE:
                continue
            if (eye[0] - foot[0]) * out[0] + (eye[1] - foot[1]) * out[1] < LANDING_FACING * d:
                continue   # behind the facade, or too far round to the side
            centre = (foot[0], foot[1], z + TARGET_FACE)
            if centre[2] - eye[2] > LANDING_TARGET_SLOPE * d:
                continue
            if not _target_lines_clear(world, eye, centre, tops, props):
                continue
            landing.append({"kind": "landing", "osm": osm, "floor": floor, "foot": foot, "centre": centre, "distance": d})
    return roof, movable, landing


def _pick_targets(roof, movable, landing):
    """Twelve spread targets: landings first, then movers, then still roof targets; None if short."""
    chosen = []

    def far_enough(cand, spacing):
        for other in chosen:
            gap = _dist3(cand["centre"], other["centre"])
            need = TARGET_MOVER_SPACING if "moving" in (cand["kind"], other["kind"]) else spacing
            if gap < need:
                return False
        return True

    def take(pool, count, per_building):
        used = {}
        # Round the distance bands so near and far both get some.
        ordered = sorted(pool, key=lambda cd: (int(cd["distance"] // 1000.0) % 3, cd["distance"], cd["centre"]))
        for cand in ordered:
            if sum(1 for ch in chosen if ch["kind"] == cand["kind"]) >= count:
                break
            if used.get(cand["osm"], 0) >= per_building or not far_enough(cand, TARGET_SPACING):
                continue
            chosen.append(cand)
            used[cand["osm"]] = used.get(cand["osm"], 0) + 1

    take(landing, ARCHERY_ON_ESCAPES, 2)
    take(movable, ARCHERY_MOVING, 1)
    take(roof, ARCHERY_TARGETS - len(chosen), 3)
    kinds = [ch["kind"] for ch in chosen]
    if len(chosen) < ARCHERY_TARGETS or kinds.count("moving") < ARCHERY_MIN_KIND or kinds.count("landing") < ARCHERY_MIN_KIND:
        return None
    return chosen[:ARCHERY_TARGETS]


def _stand_spots(world, top):
    rec, ring, box = top[0], top[1], top[2]
    roof_z = world.roof_z(top)
    props = world.props_near((box[0] + box[2]) * 0.5, (box[1] + box[3]) * 0.5, max(box[2] - box[0], box[3] - box[1]))
    spots = []
    x = box[0] + STAND_GRID * 0.5
    while x < box[2]:
        y = box[1] + STAND_GRID * 0.5
        while y < box[3]:
            pt = (x, y)
            y += STAND_GRID
            if gen._edge_clearance(pt, ring) < STAND_EDGE_CLEAR or not clear_of_props(world, pt, roof_z, PROP_CLEAR + 60.0, props):
                continue
            if near_anchor(world, pt, roof_z, 150.0):
                continue
            spots.append(pt)
        x += STAND_GRID
    return spots, props


def _landings_facing(world, top):
    """Fire-escape landings on other buildings in range of top's middle whose facade faces it."""
    cx, cy = geo.centroid(top[1])
    count = 0
    for osm, landings in world.escapes.items():
        if osm == top[0]["id"]:
            continue
        for _floor, (x, y, _z), _yaw, out in landings:
            d = math.hypot(cx - x, cy - y)
            if TARGET_MIN_DISTANCE <= d <= TARGET_MAX_DISTANCE and ((cx - x) * out[0] + (cy - y) * out[1]) >= LANDING_FACING * d:
                count += 1
    return count


def plan_archery(world):
    """[(challenge dict)] for up to ARCHERY_COUNT rooftop ranges: of the roofs near the PlayerStart with
    the most fire-escape landings facing them, nearest first."""
    roofs = [t for t in world.tops if ARCHERY_ROOF_HEIGHT[0] <= t[0]["height_m"] <= ARCHERY_ROOF_HEIGHT[1]
             and t[0]["id"] not in world.taken and gen.ring_distance(world.start, t[1]) <= SEARCH_RADIUS]
    facing = {t[0]["id"]: _landings_facing(world, t) for t in roofs}
    roofs = [t for t in roofs if facing[t[0]["id"]] >= ARCHERY_MIN_LANDINGS_FACING]
    roofs.sort(key=lambda t: (gen.ring_distance(world.start, t[1]), t[0]["id"]))
    plans = []
    for top in roofs:
        if len(plans) >= ARCHERY_COUNT:
            break
        rec, ring = top[0], top[1]
        roof_z = world.roof_z(top)
        spots, props = _stand_spots(world, top)
        best = None
        for pt in spots:
            if any(_dist2(pt, p["stand"]) < ARCHERY_START_APART for p in plans):
                continue
            eye = (pt[0], pt[1], roof_z + BOW_HEIGHT)
            roof_c, movable, landing = archery_candidates(world, eye, rec["id"])
            if len(roof_c) + len(landing) < ARCHERY_TARGETS or len(movable) < ARCHERY_MIN_KIND or len(landing) < ARCHERY_MIN_KIND:
                continue
            chosen = _pick_targets(roof_c, movable, landing)
            if chosen is None:
                continue
            # The pedestal stands behind her, away from the targets, on the roof and clear.
            cx = sum(t["centre"][0] for t in chosen) / len(chosen) - pt[0]
            cy = sum(t["centre"][1] for t in chosen) / len(chosen) - pt[1]
            length = math.hypot(cx, cy) or 1.0
            fx, fy = cx / length, cy / length
            pedestal = (pt[0] - fx * PEDESTAL_BACK, pt[1] - fy * PEDESTAL_BACK)
            if gen._edge_clearance(pedestal, ring) < STAND_EDGE_CLEAR or not clear_of_props(world, pedestal, roof_z, PROP_CLEAR, props):
                continue
            score = len(roof_c) + 2 * len(movable) + 2 * len(landing)
            if best is None or score > best[0]:
                best = (score, pt, pedestal, math.degrees(math.atan2(fy, fx)), chosen)
        if best is None:
            continue
        _score, pt, pedestal, yaw, chosen = best
        eye = (pt[0], pt[1], roof_z + BOW_HEIGHT)
        targets = []
        for t in chosen:
            to_eye = math.degrees(math.atan2(eye[1] - t["foot"][1], eye[0] - t["foot"][0]))
            targets.append({"foot": t["foot"], "yaw": to_eye, "pitch": _face_pitch(eye, t["centre"]),
                            "moving": t["kind"] == "moving", "kind": t["kind"], "osm": t["osm"],
                            "distance": t["distance"]})
        index = len(plans) + 1
        plans.append({"id": "archery_{0}".format(index), "type": "Archery", "name": "[Archery challenge {0}]".format(index),
                      "start": (pedestal[0], pedestal[1], roof_z), "yaw": yaw, "stand": pt, "osm": rec["id"],
                      "targets": targets})
    return plans


# --------------------------------------------------------------------------------------------------
# traversal
# --------------------------------------------------------------------------------------------------


def on_sidewalk(world, pt):
    """Street ground along a block: out of every building, within STREET_HUG of one, and no further
    into a carriageway than STREET_INTO_ROAD. The avenues' default 25 m width runs under the facades
    on Avenue A and B, so there the 'sidewalk' is the edge of the asphalt."""
    if world.building_at(pt) is not None:
        return False
    for a, b, half, _name in world.roads:
        if _segment_point_distance(pt, a, b)[0] - half < -STREET_INTO_ROAD:
            return False
    for top in world.tops_near(pt[0], pt[1], STREET_HUG + 100.0):
        if gen.ring_distance(pt, top[1]) <= STREET_HUG:
            return True
    return False


def street_clear(world, pt, clearance):
    for x, y in world.lamps:
        if math.hypot(pt[0] - x, pt[1] - y) < gen.LAMP_CLEAR + clearance * 0.5:
            return False
    for x, y, z0, z1, radius, kind in world.props:
        if z0 < 400.0 and math.hypot(pt[0] - x, pt[1] - y) < radius + clearance:
            return False
    return True


def street_path_clear(world, a, b):
    steps = max(2, int(_dist2(a, b) // 100.0))
    for k in range(steps + 1):
        t = k / float(steps)
        q = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        if not on_sidewalk(world, q) or not street_clear(world, q, PATH_PROP_CLEAR):
            return False
    return True


def roof_path_clear(world, a, b, roofs, props):
    """A run from a to b over the roofs in roofs (their tops), clear of props and the outer edges."""
    steps = max(2, int(_dist2(a, b) // 100.0))
    for k in range(steps + 1):
        t = k / float(steps)
        q = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        inside = [r for r in roofs if geo.point_in_polygon(q, r[1])]
        if not inside:
            # Between two neighbours: fine only where their outlines meet.
            if min(gen.ring_distance(q, r[1]) for r in roofs) > 40.0:
                return False
            continue
        roof = inside[0]
        if len(roofs) == 1 and gen._edge_clearance(q, roof[1]) < ROOF_PATH_CLEAR:
            return False
        if not clear_of_props(world, q, world.roof_z(roof), PATH_PROP_CLEAR, props):
            return False
    return True


def _lines_clear(world, a3, b3, ignore_ids, margin, offsets=(0.0,)):
    """Whether the straight line a3 -> b3 (and its copies offsets cm to either side) clears every building
    not in ignore_ids and every prop, by margin cm."""
    mid = ((a3[0] + b3[0]) * 0.5, (a3[1] + b3[1]) * 0.5)
    reach = _dist2(a3, b3) * 0.5 + 500.0
    tops = [t for t in world.tops_near(mid[0], mid[1], reach) if t[0]["id"] not in ignore_ids]
    props = [p for p in world.props_near(mid[0], mid[1], reach)
             if (world.building_at((p[0], p[1])) or ({"id": None},))[0]["id"] not in ignore_ids]
    props += world.landings_near(mid[0], mid[1], reach)
    span = _dist2(a3, b3)
    if span < 1.0:
        return True
    sx, sy = -(b3[1] - a3[1]) / span, (b3[0] - a3[0]) / span
    for off in offsets:
        a = (a3[0] + sx * off, a3[1] + sy * off, a3[2] - margin + LINE_MARGIN)
        b = (b3[0] + sx * off, b3[1] + sy * off, b3[2] - margin + LINE_MARGIN)
        if line_blocked(world, a, b, tops, props=props):
            return False
    return True


def zip_clear(world, feet3, landing3, anchor_osm, start_osm):
    """UGrappleComponent::IsZipClear by footprints: the capsule's feet run in a straight line from
    ZIP_LAUNCH above where she stood to the landing point, the capsule ZIP_CAPSULE_RADIUS wide. The
    anchor's building never blocks it; her own only past ZIP_START_RELEASE from where she stood."""
    launch = (feet3[0], feet3[1], feet3[2] + ZIP_LAUNCH)
    end = (landing3[0], landing3[1], landing3[2])
    sides = (-ZIP_CAPSULE_RADIUS, 0.0, ZIP_CAPSULE_RADIUS)
    if not _lines_clear(world, launch, end, {anchor_osm, start_osm}, ZIP_FEET_CLEAR, sides):
        return False
    if not start_osm:
        return True
    span = _dist3(launch, end)
    if span <= ZIP_START_RELEASE:
        return True
    t = ZIP_START_RELEASE / span
    release = tuple(launch[k] + (end[k] - launch[k]) * t for k in range(3))
    return _lines_clear(world, release, end, {anchor_osm}, ZIP_FEET_CLEAR, sides)


def camera_sees(world, feet3, marker3, anchor_osm):
    """The grapple only targets what the camera sees: a lens CAMERA_BACK behind her, CAMERA_UP above her
    feet, looking at the anchor's marker, with nothing but the anchor's building in the way."""
    dx, dy = marker3[0] - feet3[0], marker3[1] - feet3[1]
    d = math.hypot(dx, dy) or 1.0
    lens = (feet3[0] - dx / d * CAMERA_BACK, feet3[1] - dy / d * CAMERA_BACK, feet3[2] + CAMERA_UP)
    if world.building_at((lens[0], lens[1])) is not None and world.roof_z(world.building_at((lens[0], lens[1]))) > lens[2]:
        return False
    # The anchor's own building counts too, all but the stone right under the marker
    # (UGrappleComponent::HasLineOfSight's SightTolerance): an anchor on the far side of a roof is hidden.
    span = _dist3(lens, marker3)
    t = max(0.0, 1.0 - CAMERA_SIGHT_TOLERANCE / max(span, 1.0))
    short = tuple(lens[k] + (marker3[k] - lens[k]) * t for k in range(3))
    return _lines_clear(world, lens, short, set(), 0.0)


def grapples_from(world, here3, here_osm, exclude):
    """Anchors on other roofs a grapple from here3 (her feet) reaches with a clear line: [(anchor, top)]."""
    out = []
    for anchor in world.anchors:
        ax, ay, az, lx, ly, lz, osm = anchor
        if osm == here_osm or osm in exclude or osm in world.taken or osm not in world.by_id:
            continue
        reach = _dist3((here3[0], here3[1], here3[2] + 90.0), (ax, ay, az))
        if reach > GRAPPLE_RANGE - GRAPPLE_SLACK or _dist2(here3, (lx, ly)) < GRAPPLE_MIN:
            continue
        pitch = math.degrees(math.atan2(az - here3[2] - CAMERA_UP, max(_dist2(here3, (ax, ay)), 1.0)))
        if pitch > GRAPPLE_MAX_PITCH:
            continue
        if not zip_clear(world, here3, (lx, ly, lz), osm, here_osm) or not camera_sees(world, here3, (ax, ay, az), osm):
            continue
        out.append((reach, anchor, world.by_id[osm]))
    out.sort(key=lambda e: (e[0], e[1][6]))
    return [(a, t) for _r, a, t in out]


def neighbours(world, top):
    """Buildings touching top's outline (within 60 cm)."""
    rec, ring, box = top[0], top[1], top[2]
    out = []
    for other in world.tops_near((box[0] + box[2]) * 0.5, (box[1] + box[3]) * 0.5, max(box[2] - box[0], box[3] - box[1])):
        if other[0]["id"] == rec["id"]:
            continue
        if any(gen.ring_distance(p, other[1]) < 60.0 for p in ring):
            out.append(other)
    return out


def roof_spots(world, top, inset=150.0):
    """Standing spots on a roof, 150 cm grid, clear of its edges, props and anchors."""
    rec, ring, box = top[0], top[1], top[2]
    roof_z = world.roof_z(top)
    props = world.props_near((box[0] + box[2]) * 0.5, (box[1] + box[3]) * 0.5, max(box[2] - box[0], box[3] - box[1]) + 300.0)
    spots = []
    x = box[0] + 75.0
    while x < box[2]:
        y = box[1] + 75.0
        while y < box[3]:
            pt = (x, y)
            y += 150.0
            if gen._edge_clearance(pt, ring) < inset or not clear_of_props(world, pt, roof_z, PATH_PROP_CLEAR + 40.0, props):
                continue
            spots.append(pt)
        x += 150.0
    return spots, props


def roof_leg(world, top, frm, want_escape_next, exclude, depth_left):
    """From frm (feet on top's roof): a run, or a mantle onto a neighbour a step up, to a spot a grapple
    leaves from. [(leg, spot top, spot (x, y), grapple (anchor, top))] best first."""
    roof_z = world.roof_z(top)
    options = []
    # A step up onto a neighbour first: the mantle.
    for other in neighbours(world, top):
        step = world.roof_z(other) - roof_z
        if not (MANTLE_STEP[0] <= step <= MANTLE_STEP[1]) or other[0]["id"] in world.taken or other[0]["id"] in exclude:
            continue
        spots, props = roof_spots(world, other)
        for pt in spots:
            d = _dist2(frm, pt)
            if RUN_MIN <= d <= RUN_MAX and roof_path_clear(world, frm, pt, [top, other], props):
                options.append(("Mantle", other, pt, d))
    spots, props = roof_spots(world, top)
    for pt in spots:
        d = _dist2(frm, pt)
        if RUN_MIN <= d <= RUN_MAX and roof_path_clear(world, frm, pt, [top], props):
            options.append(("Run", top, pt, d))
    options.sort(key=lambda o: (0 if o[0] == "Mantle" else 1, -o[3], o[2]))
    results = []
    for leg, spot_top, pt, _d in options[:60]:
        here3 = (pt[0], pt[1], world.roof_z(spot_top))
        grapples = grapples_from(world, here3, spot_top[0]["id"], exclude | {top[0]["id"]})
        if want_escape_next:
            grapples = [g for g in grapples if g[1][0]["id"] in world.escapes]
        for grapple in grapples[:ROUTE_BRANCH]:
            results.append((leg, spot_top, pt, grapple))
            if len(results) >= ROUTE_OPTIONS:
                return results
    return results


def descent_spots(world, top):
    """(roof spot behind the top landing, street spot below) for top's fire escape, or None."""
    landings = world.escapes.get(top[0]["id"])
    if not landings:
        return None
    floor, (x, y, z), yaw, out = landings[-1]
    roof_z = world.roof_z(top)
    stand = (x - out[0] * DESCENT_BACK, y - out[1] * DESCENT_BACK)
    street = (x + out[0] * DESCENT_OUT, y + out[1] * DESCENT_OUT)
    props = world.props_near(x, y, 600.0)
    if not geo.point_in_polygon(stand, top[1]) or not clear_of_props(world, stand, roof_z, PATH_PROP_CLEAR + 40.0, props):
        return None
    if world.building_at(street) is not None or not street_clear(world, street, 60.0):
        return None
    return stand, street, floor


def street_corners(world):
    """Pedestal spots on the sidewalk at block corners, nearest the PlayerStart first: [(x, y, building top)]."""
    corners = []
    for top in world.tops:
        rec, ring = top[0], top[1]
        if gen.ring_distance(world.start, ring) > TRAVERSAL_SEARCH_RADIUS or rec["height_m"] < 8.0:
            continue
        n = len(ring)
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        for i in range(n):
            px, py = ring[i - 1]
            cx, cy = ring[i]
            nx, ny = ring[(i + 1) % n]
            ax, ay, bx, by = cx - px, cy - py, nx - cx, ny - cy
            la, lb = math.hypot(ax, ay), math.hypot(bx, by)
            if la < CORNER_EDGE or lb < CORNER_EDGE:
                continue
            ax, ay, bx, by = ax / la, ay / la, bx / lb, by / lb
            turn = math.degrees(math.atan2(ax * by - ay * bx, ax * bx + ay * by)) * inward
            if turn < CORNER_TURN:
                continue
            # Outward bisector: minus the sum of the two edges' inward normals.
            ux, uy = ay * inward + by * inward, -ax * inward - bx * inward
            ul = math.hypot(ux, uy)
            if ul < 1e-6:
                continue
            ux, uy = ux / ul, uy / ul
            spot = (cx + ux * CORNER_OUT, cy + uy * CORNER_OUT)
            near_roads = {name for a, b, half, name in world.roads if _segment_point_distance((cx, cy), a, b)[0] <= CORNER_ROADS}
            if len(near_roads) < 2 or not on_sidewalk(world, spot) or not street_clear(world, spot, 150.0):
                continue
            corners.append((gen.ring_distance(world.start, ring), spot, top, (ux, uy)))
    corners.sort(key=lambda e: (e[0], e[1]))
    return [(spot, top, out) for _d, spot, top, out in corners]


def _checkpoint(point3, towards3, leg):
    return {"at": (point3[0], point3[1], point3[2] + CHECKPOINT_UP), "towards": towards3, "leg": leg}


def _final_legs(world, roof3, landing3, used):
    """Ways from a zip's landing on roof3 to the spot over a fire escape's top landing: a run on roof3 if
    it has an escape, or a mantle onto a neighbour a step up that has one. [(leg, roof, stand, street)]."""
    out = []
    options = [("Run", roof3, [roof3])]
    for other in neighbours(world, roof3):
        step = world.roof_z(other) - world.roof_z(roof3)
        if MANTLE_STEP[0] <= step <= MANTLE_STEP[1] and other[0]["id"] not in world.taken and other[0]["id"] not in used:
            options.append(("Mantle", other, [roof3, other]))
    for leg, roof, roofs in options:
        descent = descent_spots(world, roof)
        if descent is None:
            continue
        stand, street, _floor = descent
        run = _dist2(landing3, stand)
        props = world.props_near(landing3[0], landing3[1], run + 300.0)
        if 300.0 <= run <= 2500.0 and roof_path_clear(world, landing3, stand, roofs, props):
            out.append((leg, roof, stand, street))
    return out


def plan_route(world, spot, corner_top, need_mantle):
    """The eight rings from a street-corner pedestal, or None. With need_mantle, only a route that
    steps up onto a neighbour's roof somewhere."""
    ground = gen.SIDEWALK_TOP
    for first_run in _street_runs(world, spot, corner_top):
        cp1 = (first_run[0], first_run[1], ground)
        for anchor1, roof1 in grapples_from(world, cp1, None, set())[:ROUTE_BRANCH]:
            l1 = (anchor1[3], anchor1[4], anchor1[5])
            for leg3, top3, pt3, (anchor2, roof2) in roof_leg(world, roof1, l1, False, set(), 2):
                l2 = (anchor2[3], anchor2[4], anchor2[5])
                cp3 = (pt3[0], pt3[1], world.roof_z(top3))
                used = {roof1[0]["id"], top3[0]["id"]}
                for leg5, top5, pt5, (anchor3, roof3) in roof_leg(world, roof2, l2, False, used, 1):
                    l3 = (anchor3[3], anchor3[4], anchor3[5])
                    for leg7, roof7, stand, street in _final_legs(world, roof3, l3, used | {roof2[0]["id"], top5[0]["id"]}):
                        cp5 = (pt5[0], pt5[1], world.roof_z(top5))
                        cp7 = (stand[0], stand[1], world.roof_z(roof7))
                        cp8 = (street[0], street[1], ground)
                        points = [cp1, l1, cp3, l2, cp5, l3, cp7, cp8]
                        if any(_dist2(spot, p) > ROUTE_REACH for p in points):
                            continue
                        legs = ["Run", "Grapple", leg3, "Grapple", leg5, "Grapple", leg7, "Descent"]
                        if need_mantle and "Mantle" not in legs:
                            continue
                        rings = []
                        prev = (spot[0], spot[1], ground)
                        for p, leg in zip(points, legs):
                            rings.append(_checkpoint(p, prev, leg))
                            prev = p
                        return {"rings": rings, "roofs": [roof1[0]["id"], top3[0]["id"], roof2[0]["id"], top5[0]["id"],
                                                          roof3[0]["id"], roof7[0]["id"]],
                                "first": first_run}
    return None


def _street_runs(world, spot, corner_top):
    """Sidewalk points STREET_RUN from the corner along either facade, with a clear run to them."""
    ring = corner_top[1]
    out = []
    for i in range(len(ring)):
        a, b = ring[i], ring[(i + 1) % len(ring)]
        d = _segment_point_distance(spot, a, b)[0]
        if d > CORNER_OUT + 200.0:
            continue
        length = _dist2(a, b)
        ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
        for sign in (1.0, -1.0):
            for run in (STREET_RUN[0], (STREET_RUN[0] + STREET_RUN[1]) * 0.5, STREET_RUN[1]):
                pt = (spot[0] + ux * run * sign, spot[1] + uy * run * sign)
                if on_sidewalk(world, pt) and street_clear(world, pt, 100.0) and street_path_clear(world, spot, pt):
                    out.append(pt)
    return out


def plan_traversal(world, archery):
    """Up to TRAVERSAL_COUNT routes from street corners, nearest the PlayerStart first: routes with a
    mantle before any without. None crosses an archery pedestal's roof or starts beside one."""
    world.taken = world.taken | {p["osm"] for p in archery}
    corners = [(spot, top) for spot, top, _out in street_corners(world)
               if all(_dist2(spot, p["start"]) >= ARCHERY_TRAVERSAL_APART for p in archery)]
    found = []
    for need_mantle in (True, False):
        for spot, top in corners:
            if len(found) >= TRAVERSAL_COUNT:
                break
            if any(_dist2(spot, f[0]) < TRAVERSAL_START_APART for f in found):
                continue
            route = plan_route(world, spot, top, need_mantle)
            if route is not None:
                found.append((spot, route))
    found.sort(key=lambda f: (_dist2(world.start, f[0]), f[0]))
    plans = []
    for spot, route in found:
        first = route["first"]
        index = len(plans) + 1
        plans.append({"id": "traversal_{0}".format(index), "type": "Traversal", "name": "[Traversal challenge {0}]".format(index),
                      "start": (spot[0], spot[1], gen.SIDEWALK_TOP),
                      "yaw": math.degrees(math.atan2(first[1] - spot[1], first[0] - spot[0])),
                      "rings": route["rings"], "roofs": route["roofs"]})
    return plans


_PLAN_CACHE = {}


def plan_challenges(district):
    """Every challenge's plan, archery first. Cached per district object."""
    key = id(district)
    if key not in _PLAN_CACHE:
        world = World(district)
        archery = plan_archery(world)
        _PLAN_CACHE[key] = (archery + plan_traversal(world, archery), world)
    return _PLAN_CACHE[key][0]


def planning_world(district):
    plan_challenges(district)
    return _PLAN_CACHE[id(district)][1]


# --------------------------------------------------------------------------------------------------
# assets
# --------------------------------------------------------------------------------------------------


def _enum(enum_name, value):
    """unreal.ChallengeReward.ARROW_REFILL from ("ChallengeReward", "ArrowRefill")."""
    enum = getattr(unreal, enum_name)
    return getattr(enum, re.sub(r"(?<!^)(?=[A-Z])", "_", value).upper())


def _target_spawn(t):
    spawn = unreal.ChallengeTargetSpawn()
    spawn.set_editor_property("transform", unreal.Transform(
        unreal.Vector(*t["foot"]), unreal.Rotator(0.0, t["pitch"], t["yaw"]), unreal.Vector(1.0, 1.0, 1.0)))
    spawn.set_editor_property("moving", bool(t["moving"]))
    spawn.set_editor_property("track_length", TRACK_LENGTH)
    spawn.set_editor_property("track_speed", TRACK_SPEED)
    return spawn


def _ring_transform(ring):
    at, towards = ring["at"], ring["towards"]
    yaw = math.degrees(math.atan2(at[1] - towards[1], at[0] - towards[0]))
    return unreal.Transform(unreal.Vector(*at), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(1.0, 1.0, 1.0))


def _fields(plan):
    """(property, value) for every field create_challenges sets, in order."""
    archery = plan["type"] == "Archery"
    medals = ARCHERY_MEDALS if archery else TRAVERSAL_MEDALS
    fields = [
        ("id", unreal.Name(plan["id"])),
        ("type", _enum("ChallengeType", plan["type"])),
        ("name", unreal.Text(plan["name"])),
        ("time_limit_seconds", ARCHERY_TIME if archery else TRAVERSAL_TIME),
        ("start_location", unreal.Vector(*plan["start"])),
        ("start_yaw", plan["yaw"]),
        ("area_radius", AREA_RADIUS),
        ("calm_thug_radius", CALM_RADIUS),
        ("checkpoint_radius", CHECKPOINT_RADIUS),
        ("bronze_value", medals[0]),
        ("silver_value", medals[1]),
        ("gold_value", medals[2]),
        ("reward", _enum("ChallengeReward", "ArrowRefill" if archery else "None")),
        ("targets", [_target_spawn(t) for t in plan.get("targets", [])]),
        ("checkpoints", [_ring_transform(r) for r in plan.get("rings", [])]),
        ("checkpoint_legs", [_enum("ChallengeLeg", r["leg"]) for r in plan.get("rings", [])]),
    ]
    return fields


def _same(current, wanted):
    if isinstance(wanted, float):
        return abs(float(current) - wanted) < 1e-3
    if isinstance(wanted, unreal.Vector):
        return current is not None and all(abs(getattr(current, k) - getattr(wanted, k)) < 0.05 for k in "xyz")
    if isinstance(wanted, unreal.Text):
        return str(current) == str(wanted)
    if isinstance(wanted, list):
        if current is None or len(current) != len(wanted):
            return False
        return all(_same_struct(a, b) for a, b in zip(current, wanted))
    return current == wanted


def _same_transform(a, b):
    ta, tb = a.translation, b.translation
    ra, rb = a.rotation.rotator(), b.rotation.rotator()
    return all(abs(getattr(ta, k) - getattr(tb, k)) < 0.05 for k in "xyz") and \
        all(abs(((getattr(ra, k) - getattr(rb, k)) + 180.0) % 360.0 - 180.0) < 0.05 for k in ("roll", "pitch", "yaw"))


def _same_struct(a, b):
    if isinstance(b, unreal.Transform):
        return _same_transform(a, b)
    if isinstance(b, unreal.ChallengeTargetSpawn):
        return _same_transform(a.get_editor_property("transform"), b.get_editor_property("transform")) and \
            all(_same(a.get_editor_property(p), b.get_editor_property(p)) for p in ("moving", "track_length", "track_speed"))
    return a == b


def ensure_challenge(plan):
    cls = c.find_class("ChallengeDefinition", "/Script/Hawkeye.ChallengeDefinition")
    if cls is None:
        c.log("FAILED", CHALLENGE_PATH, "UChallengeDefinition not exposed to Python; build the module")
        return None
    name = ASSET_PREFIX + plan["id"]
    full = c.asset_path(CHALLENGE_PATH, name)
    factory = c.new_factory("DataAssetFactory")
    c.set_props(factory, [("data_asset_class", cls)], "DataAssetFactory")
    asset, created = c.create_asset(name, CHALLENGE_PATH, cls, factory, quiet=True)
    if asset is None:
        return None
    changed = []
    for prop, value in _fields(plan):
        try:
            current = asset.get_editor_property(prop)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(full, prop), exc)
            continue
        if not _same(current, value):
            asset.set_editor_property(prop, value)
            changed.append(prop)
    if created or changed:
        c.save(asset)
    detail = "{0} targets".format(len(plan["targets"])) if plan["type"] == "Archery" else "{0} rings".format(len(plan["rings"]))
    c.log("created" if created else ("updated" if changed else "exists"), full,
          detail + (" (" + ", ".join(changed) + ")" if changed and not created else ""))
    return asset


def remove_stale(plans):
    """Challenge assets no plan wants any more (a smaller district, a retired challenge)."""
    wanted = {c.asset_path(CHALLENGE_PATH, ASSET_PREFIX + p["id"]) for p in plans}
    removed = 0
    if not unreal.EditorAssetLibrary.does_directory_exist(CHALLENGE_PATH):
        return 0
    for path in unreal.EditorAssetLibrary.list_assets(CHALLENGE_PATH, recursive=False):
        package = path.split(".")[0]
        if package.split("/")[-1].startswith(ASSET_PREFIX) and package not in wanted:
            unreal.EditorAssetLibrary.delete_asset(package)
            c.log("deleted", package, "no longer planned")
            removed += 1
    return removed


def describe(plan):
    if plan["type"] == "Archery":
        kinds = [t["kind"] for t in plan["targets"]]
        dists = [t["distance"] / 100.0 for t in plan["targets"]]
        return "{0} on osm {1} at ({2:.0f}, {3:.0f}, {4:.0f}): {5} targets ({6} moving, {7} on landings), {8:.0f} to {9:.0f} m".format(
            plan["id"], plan["osm"], plan["start"][0], plan["start"][1], plan["start"][2], len(kinds), kinds.count("moving"),
            kinds.count("landing"), min(dists), max(dists))
    legs = ", ".join(r["leg"] for r in plan["rings"])
    return "{0} at ({1:.0f}, {2:.0f}): {3} rings [{4}] over roofs {5}".format(
        plan["id"], plan["start"][0], plan["start"][1], len(plan["rings"]), legs, ", ".join(plan["roofs"]))


def run():
    if not gen.data_available():
        c.log("skipped", CHALLENGE_PATH, "no OSM data; run Tools\\fetch-osm.ps1 first")
        return []
    district = gen.District()
    plans = plan_challenges(district)
    archery = sum(1 for p in plans if p["type"] == "Archery")
    if archery < ARCHERY_COUNT or len(plans) - archery < TRAVERSAL_COUNT:
        c.log("FAILED", CHALLENGE_PATH, "planned {0} archery and {1} traversal challenges, want {2} and {3}".format(
            archery, len(plans) - archery, ARCHERY_COUNT, TRAVERSAL_COUNT))
    c.ensure_directory(CHALLENGE_PATH)
    for plan in plans:
        unreal.log("[Hawkeye] info  " + describe(plan))
        ensure_challenge(plan)
    remove_stale(plans)
    return plans


if __name__ == "__main__":
    run()
    c.print_summary("challenges")
