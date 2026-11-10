"""Plan a boss arena from the district's records (claude-docs/gameplay-semantics.md, "Bosses").

``plan_arena(district)`` picks, from the same OpenStreetMap records generate_city.py builds the district from, the
roof Act 1's "The Social Club" is fought on (docs/chapters/ACT1.md, chapter 4) and lays the fight out on it:

* the roof: 8 to 16 m up (so a grapple from the street reaches its parapet and it has one: parapets stand on every
  roof over 6 m), at least 300 square metres, with a clear circle at least 7 m in radius away from its edges and from
  every rooftop prop the generator puts there (water tanks, units), not a roof another system uses (the chase, the
  challenges, the rooftop crimes, chapter 1's archers and thugs, the safehouses), and not within 30 m of a placed
  fighter;
* the way up: a stand on the sidewalk from which one of the roof's parapet anchors is a grapple the game will take,
  checked with create_challenges' model of the grapple (range, pitch, a clear zip, the hip camera's view), the same
  model the chase and the challenges use;
* the fight: its centre (the clear circle's), Tomas's spot across it from where Kate lands, four crew places round
  the centre, the inflatable Santa's lane through the middle at right angles to the line from her landing to him, and
  the greybox dressing (string-light poles at the circle's diagonals with their bulbs strung between, a speaker behind
  him, two folding tables) kept off the lane and the fighters' places, and three presents in the lane for the Santa
  to shove along.

Pure: no unreal calls beyond what generate_city and create_challenges need at import (test_arena stubs them).
Cached per district object.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402

ROOF_HEIGHT = (8.0, 16.0)       # m
MIN_AREA = 300.0                # m2
MIN_RADIUS = 700.0              # cm, the clear circle the fight needs
MAX_RADIUS = 1100.0             # cm; a bigger circle is used no bigger than this
SEARCH_RADIUS = 35000.0         # cm from the PlayerStart
FIGHTER_CLEAR = 3000.0          # cm from chapter 1's archers and thugs
PROP_MARGIN = 60.0              # cm between a rooftop prop's radius and the fight circle
CENTRE_GRID = 100.0             # cm between centres tried on a roof
LANDING_ROOF_TOL = 60.0         # cm an approach anchor's landing may stand off the roof's floor
STAND_DISTANCES = tuple(range(500, 1900, 100))   # cm out from the anchor a street stand is tried
STAND_SIDEWAYS = (0.0, -250.0, 250.0, -500.0, 500.0)
STAND_CLEAR = 120.0             # cm from lamps and street clutter
BOSS_BACK = 0.6                 # of the radius, from the centre away from her landing
BOSS_BACK_MAX = 500.0
CREW_RING = 330.0               # cm from the centre
CREW_ANGLES = (45.0, -45.0, 115.0, -115.0)   # degrees off the line from the centre to her landing
LANE_INSET = 130.0              # cm in from the circle the Santa's lane ends
SANTA_HALF_WIDTH = 170.0        # the lane's half width (FBossHazardTuning.HalfWidth)
POLE_INSET = 70.0               # cm in from the circle the string-light poles stand
POLE_HEIGHT = 300.0
BULBS_PER_SPAN = 3
SPEAKER = (60.0, 45.0, 95.0)    # width, depth, height
SPEAKER_BEHIND = 230.0          # cm behind Tomas the speaker stands
TABLE = (180.0, 75.0, 74.0)
PROP_PERSON_CLEAR = 140.0       # cm from any prop to a fighter's place
PROP_LANE_CLEAR = 60.0          # cm from a blocking prop to the lane's edge
PRESENTS = (0.3, 0.55, 0.8)     # fractions along the lane: presents the Santa shoves, never passes through
PRESENT_ACROSS = (40.0, -60.0, 20.0)   # cm off the lane's line, so they do not sit in a row

_PLAN = {}


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _unit(a, b):
    d = _dist2(a, b) or 1.0
    return ((b[0] - a[0]) / d, (b[1] - a[1]) / d)


def _off(p, u, cm):
    return (p[0] + u[0] * cm, p[1] + u[1] * cm)


def _rot(u, degrees):
    r = math.radians(degrees)
    return (u[0] * math.cos(r) - u[1] * math.sin(r), u[0] * math.sin(r) + u[1] * math.cos(r))


def _yaw(u):
    return math.degrees(math.atan2(u[1], u[0]))


def _taken(district, world, cc):
    import create_crimes as cr  # noqa: PLC0415
    import _chase  # noqa: PLC0415
    taken = set(world.taken)
    for plan in cc.plan_challenges(district):
        taken |= set(plan.get("roofs", []))
        if plan.get("osm"):
            taken.add(plan["osm"])
    for spot in cr.plan_crime_spots(district):
        if spot["rooftop"]:
            taken.add(spot["osm"])
    chase = _chase.plan_chase(district)
    if chase is not None:
        taken |= set(chase["roofs"])
    return taken


def _fighters(district, gen):
    out = [(a[1], a[2]) for a in gen.archer_placements(district)]
    thugs, _points, _roof = gen.thug_placements(district)
    out += [(t[1], t[2]) for t in thugs]
    return out


def _clear_circle(world, gen, cc, top):
    """(centre, radius) of the biggest circle on the roof clear of its edges and props, on a CENTRE_GRID."""
    ring, box = top[1], top[2]
    z = world.roof_z(top)
    props = [p for p in world.props_near((box[0] + box[2]) * 0.5, (box[1] + box[3]) * 0.5,
                                         max(box[2] - box[0], box[3] - box[1]) + 300.0)
             if p[2] - 50.0 <= z <= p[3] + 50.0]
    best = None
    x = box[0] + CENTRE_GRID * 0.5
    while x < box[2]:
        y = box[1] + CENTRE_GRID * 0.5
        while y < box[3]:
            pt = (x, y)
            y += CENTRE_GRID
            if not geo.point_in_polygon(pt, ring):
                continue
            radius = gen._edge_clearance(pt, ring)
            for px, py, _z0, _z1, pr, _kind in props:
                radius = min(radius, math.hypot(pt[0] - px, pt[1] - py) - pr - PROP_MARGIN)
            if best is None or radius > best[1] + 1e-6:
                best = (pt, radius)
        x += CENTRE_GRID
    return best


def _approach(world, gen, cc, chase, top):
    """(anchor, stand (x, y)) for the roof's parapet anchor with a street stand the grapple takes, or None."""
    rec = top[0]
    z = world.roof_z(top)
    anchors = sorted((a for a in world.anchors if a[6] == rec["id"] and abs(a[5] - z) <= LANDING_ROOF_TOL),
                     key=lambda a: (round(a[0]), round(a[1])))
    for anchor in anchors:
        inward = _unit((anchor[0], anchor[1]), (anchor[3], anchor[4]))
        outward = (-inward[0], -inward[1])
        across = (-outward[1], outward[0])
        for d in STAND_DISTANCES:
            for side in STAND_SIDEWAYS:
                pt = _off(_off((anchor[0], anchor[1]), outward, float(d)), across, side)
                if not cc.on_sidewalk(world, pt) or not cc.street_clear(world, pt, STAND_CLEAR):
                    continue
                if chase.grapple_ok(cc, world, (pt[0], pt[1], gen.SIDEWALK_TOP), None, anchor):
                    return anchor, pt
    return None


def plan_arena(district):
    """The arena's plan (a dict), or None when no roof fits. Cached per district."""
    key = id(district)
    if key in _PLAN:
        return _PLAN[key]
    import generate_city as gen  # noqa: PLC0415
    import create_challenges as cc  # noqa: PLC0415
    import _chase as chase  # noqa: PLC0415
    world = cc.planning_world(district)
    taken = _taken(district, world, cc)
    fighters = _fighters(district, gen)
    anchored = {a[6] for a in world.anchors}
    candidates = []
    for top in world.tops:
        rec = top[0]
        if rec["id"] in taken or rec["id"] not in anchored or not ROOF_HEIGHT[0] <= rec["height_m"] <= ROOF_HEIGHT[1]:
            continue
        if gen.ring_distance(world.start, top[1]) > SEARCH_RADIUS:
            continue
        if abs(geo.signed_area(top[1])) / 10000.0 < MIN_AREA:
            continue
        circle = _clear_circle(world, gen, cc, top)
        if circle is None or circle[1] < MIN_RADIUS:
            continue
        if any(_dist2(circle[0], f) < FIGHTER_CLEAR for f in fighters):
            continue
        candidates.append((-min(circle[1], MAX_RADIUS), round(gen.ring_distance(world.start, top[1])), rec["id"], top, circle))
    candidates.sort(key=lambda c: c[:3])
    result = None
    for _r, _d, _id, top, circle in candidates:
        approach = _approach(world, gen, cc, chase, top)
        if approach is None:
            continue
        result = _layout(world, gen, top, circle, approach)
        if result is not None:
            break
    _PLAN[key] = result
    return result


def _layout(world, gen, top, circle, approach):
    rec, ring = top[0], top[1]
    z = world.roof_z(top)
    centre, radius = circle[0], min(circle[1], MAX_RADIUS)
    anchor, stand = approach
    landing = (anchor[3], anchor[4])
    toward_her = _unit(centre, landing) if _dist2(centre, landing) > 1.0 else (1.0, 0.0)
    away = (-toward_her[0], -toward_her[1])
    across = (-toward_her[1], toward_her[0])
    boss = _off(centre, away, min(radius * BOSS_BACK, BOSS_BACK_MAX))
    crew = [_off(centre, _rot(toward_her, a), min(CREW_RING, radius - 150.0)) for a in CREW_ANGLES]
    lane_half = radius - LANE_INSET
    lane = (_off(centre, across, lane_half), _off(centre, across, -lane_half))
    people = [boss, landing] + crew

    def lane_distance(pt):
        return abs((pt[0] - centre[0]) * toward_her[0] + (pt[1] - centre[1]) * toward_her[1])

    def fits(pt, half_extent):
        if lane_distance(pt) < SANTA_HALF_WIDTH + PROP_LANE_CLEAR + half_extent:
            return False
        if any(_dist2(pt, p) < PROP_PERSON_CLEAR + half_extent for p in people):
            return False
        return geo.point_in_polygon(pt, ring) and gen._edge_clearance(pt, ring) >= half_extent + 40.0

    poles = []
    for a in (45.0, 135.0, -135.0, -45.0):
        pt = _off(centre, _rot(toward_her, a), radius - POLE_INSET)
        if fits(pt, 10.0):
            poles.append(pt)
    bulbs = []
    for a, b in zip(poles, poles[1:] + poles[:1]):
        if len(poles) < 2:
            break
        for k in range(1, BULBS_PER_SPAN + 1):
            t = k / float(BULBS_PER_SPAN + 1)
            bulbs.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, z + POLE_HEIGHT - 25.0))
    props = []
    speaker = _off(boss, away, SPEAKER_BEHIND)
    if fits(speaker, max(SPEAKER[:2]) * 0.5):
        props.append(("speaker", (speaker[0], speaker[1], z, _yaw(toward_her))))
    for side in (1.0, -1.0):
        table = _off(_off(centre, away, radius * 0.55), across, side * (SANTA_HALF_WIDTH + 260.0))
        if fits(table, TABLE[0] * 0.5):
            props.append(("table", (table[0], table[1], z, _yaw(toward_her))))
    for pt in poles:
        props.append(("pole", (pt[0], pt[1], z, 0.0)))
    presents = []
    for t, side in zip(PRESENTS, PRESENT_ACROSS):
        pt = _off((lane[0][0] + (lane[1][0] - lane[0][0]) * t, lane[0][1] + (lane[1][1] - lane[0][1]) * t), toward_her, side)
        presents.append((pt[0], pt[1], z, _yaw(across) + 25.0 * len(presents)))
    wires = []
    for a, b in zip(poles, poles[1:] + poles[:1]):
        if len(poles) < 2:
            break
        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        wires.append((mid[0], mid[1], z + POLE_HEIGHT - 20.0, _yaw(_unit(a, b)), _dist2(a, b)))
    return {
        "roof": rec["id"],
        "roof_z": z,
        "height_m": rec["height_m"],
        "area_m2": round(abs(geo.signed_area(ring)) / 10000.0, 1),
        "outline": [(round(p[0], 1), round(p[1], 1)) for p in ring],
        "centre": (centre[0], centre[1], z),
        "radius": radius,
        "player_start": (stand[0], stand[1], gen.SIDEWALK_TOP),
        "player_yaw": _yaw(_unit(stand, (anchor[0], anchor[1]))),
        "anchor": (anchor[0], anchor[1], anchor[2]),
        "landing": (anchor[3], anchor[4], anchor[5]),
        "boss": (boss[0], boss[1], z),
        "boss_yaw": _yaw(toward_her),
        "crew": [(c[0], c[1], z) for c in crew],
        "lane": [(lane[0][0], lane[0][1], z), (lane[1][0], lane[1][1], z)],
        "props": props,
        "wires": wires,
        "bulbs": bulbs,
        "presents": presents,
        "grapple": round(math.sqrt((stand[0] - anchor[0]) ** 2 + (stand[1] - anchor[1]) ** 2
                                   + (gen.SIDEWALK_TOP + 90.0 - anchor[2]) ** 2)),
    }


def describe(plan):
    if plan is None:
        return "no roof fits a boss arena"
    return "roof {0} ({1:.1f} m up, {2:.0f} m2), fight circle {3:.1f} m at ({4:.0f}, {5:.0f}), stand ({6:.0f}, {7:.0f}) " \
        "grapples {8:.1f} m to ({9:.0f}, {10:.0f}), {11} props, {12} bulbs".format(
            plan["roof"], plan["height_m"], plan["area_m2"], plan["radius"] / 100.0, plan["centre"][0], plan["centre"][1],
            plan["player_start"][0], plan["player_start"][1], plan["grapple"] / 100.0, plan["anchor"][0], plan["anchor"][1],
            len(plan["props"]), len(plan["bulbs"]))
