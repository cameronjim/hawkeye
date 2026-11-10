"""Plan a chapter's chase from the district's records (claude-docs/gameplay-semantics.md, "Chases").

``plan_chase(district)`` finds, in the same OpenStreetMap records generate_city.py builds the district from, a
rooftop route that teaches what Act 1's "Night Shift" teaches, in order (docs/chapters/ACT1.md):

* a run across a start roof with two low vent boxes on it (90 cm: a sprint vaults them),
* a mantle onto a neighbour a step up (30 to 110 cm above, over its parapet),
* a first grapple across a gap to another roof (the cross_gap objective, prompted),
* a grapple and a chain: a zip at an anchor and, part way along, a second press at an anchor on the next roof,
* the authored gap over an alley (a 3 to 8 m gap between two buildings, just in off a sidewalk): the grapple
  that goes wrong, with the dumpster on the alley floor under her line and the street a few metres away,
* the thief's way down that roof's fire escape to the street and along the sidewalk to where he is cornered.

Every grapple Kate is meant to make is checked with create_challenges' model of the game's grapple (range,
the hip camera's view, a clear zip, the anchor's landing on a roof): the same model the traversal challenges
and verify_city use. The thief's route follows Kate's lines across the gaps (a leap: from parapet to parapet),
the start roof, the mantle and the roofs on foot, and the fire escape down.

Pure: no unreal calls beyond what generate_city and create_challenges need at import (the golden test stubs
them). Cached per district object.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _geo as geo  # noqa: E402

SEARCH_RADIUS = 40000.0         # cm from the PlayerStart a beat alley may be
ROOF_HEIGHT = (8.0, 26.0)       # m: roofs the route may cross
PARAPET = 90.0                  # cm, _district.PARAPET_HEIGHT
EDGE_IN = 60.0                  # cm inside a roof edge the thief lands after a parapet
LEAP_MAX = 2200.0               # cm, the longest gap the thief leaps (Kate's grapple reaches; he does not)
BEAT_GAP_MAX = 800.0            # cm across the alley at the authored gap
LEAP_DROP = (-600.0, 250.0)     # cm the far roof of a leap may stand below (negative) or above (a cat leap and pull-up)
FIGHTER_CLEAR = 4500.0          # cm from chapter 1's archers to any point of the route
THUG_CLEAR = 3000.0             # cm from the placed thugs
CHAIN_AT = (0.5, 0.6, 0.7)      # fractions of the first line a chain is tried from
CHAIN_COS = 0.34                # the chained anchor no more than 70 degrees off the line she is on
CHAIN_MIN = 600.0               # cm from the chain point to the chained landing
RUN_START = 1000.0              # cm of run on the start roof before the mantle's party wall
PROP_AT = (300.0, 700.0)        # cm from Kate's start along the run: the two vent boxes
PROP_SIZE = (120.0, 50.0, 90.0)  # width across the run, depth along it, height
PROP_EDGE_CLEAR = 160.0         # cm from any roof edge to a prop's centre
BULKHEAD = (300.0, 250.0, 150.0)  # a roof's stair bulkhead: length along the run, width, height
BULKHEAD_AT = 1150.0            # cm from Kate's start to the bulkhead's middle, when no neighbour is a step up
BULKHEAD_RUN = 1800.0           # cm from her start to her grapple stand, at least, with a bulkhead on the way
STREET_RUN = (1800.0, 4200.0)   # cm from the fire escape's foot to where he is cornered
MOUTH_CLEAR = 600.0             # cm from the alley's mouth to where he is cornered
ALLEY_OUT_MAX = 1500.0          # cm from the dumpster's alley to its mouth on a sidewalk, as the crow flies
GAP_KERB = (450.0, 1600.0)      # cm from a gap's middle to the nearest carriageway's edge: just in off the sidewalk
DUMPSTER = (180.0, 110.0, 140.0)  # length along the wall, depth out from it, height
DUMPSTER_WALL_GAP = 15.0        # cm between the wall and the dumpster's back
DUMPSTER_FLOOR = 55.0           # cm: her feet in the paper, above the alley floor
EXIT_PAST = 95.0                # cm past the dumpster's front where she stands after climbing out
ALLEY_FLOOR = 0.0               # cm: the ground between buildings (create_crimes' alley spots stand at 0)
SPOTS_NEAR_GAP = 25             # roof spots tried per grapple leg, nearest the target first
BEAT_ZONE = (260.0, 260.0, 160.0)  # half sizes of the beat's zone round her stand, cm

_PLAN = {}
REJECTS = {}   # why assembled routes were refused, for the log


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _ok_roof(world, top, taken):
    rec = top[0]
    return rec["id"] not in taken and ROOF_HEIGHT[0] <= rec["height_m"] <= ROOF_HEIGHT[1]


def _near_tops(world, top, reach):
    b = top[2]
    return [t for t in world.tops_near((b[0] + b[2]) * 0.5, (b[1] + b[3]) * 0.5, max(b[2] - b[0], b[3] - b[1]) * 0.5 + reach)
            if t[0]["id"] != top[0]["id"]]


def line_cross(a_top, b_top, p, q):
    """Along p -> q: (gap cm between leaving a_top and entering b_top, last point in a, first point in b), or None."""
    n = max(2, int(_dist2(p, q) // 20.0))
    last_a = first_b = None
    for k in range(n + 1):
        t = k / float(n)
        x = (p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t)
        if first_b is None and geo.point_in_polygon(x, a_top[1]):
            last_a = x
        elif last_a is not None and first_b is None and geo.point_in_polygon(x, b_top[1]):
            first_b = x
    if last_a is None or first_b is None:
        return None
    return _dist2(last_a, first_b), last_a, first_b


def _toward(a, b, cm):
    d = _dist2(a, b) or 1.0
    return (a[0] + (b[0] - a[0]) / d * cm, a[1] + (b[1] - a[1]) / d * cm)


def _anchors_on(world, osm):
    """The anchors on one roof, indexed once per planning world."""
    index = getattr(world, "_chase_anchor_index", None)
    if index is None:
        index = {}
        for anchor in world.anchors:
            index.setdefault(anchor[6], []).append(anchor)
        world._chase_anchor_index = index
    return index.get(osm, [])


def grapple_ok(cc, world, here3, here_osm, anchor):
    """create_challenges.grapples_from's test for one anchor: range, the grapple's minimum, pitch, a clear zip
    and the hip camera's view."""
    ax, ay, az, lx, ly, lz, osm = anchor
    reach = cc._dist3((here3[0], here3[1], here3[2] + 90.0), (ax, ay, az))
    if reach > cc.GRAPPLE_RANGE - cc.GRAPPLE_SLACK or _dist2(here3, (lx, ly)) < cc.GRAPPLE_MIN:
        return False
    pitch = math.degrees(math.atan2(az - here3[2] - cc.CAMERA_UP, max(_dist2(here3, (ax, ay)), 1.0)))
    if pitch > cc.GRAPPLE_MAX_PITCH:
        return False
    return cc.zip_clear(world, here3, (lx, ly, lz), osm, here_osm) and cc.camera_sees(world, here3, (ax, ay, az), osm)


_GRAPPLES = {}


def grapples_into(cc, world, src, target, nspots=SPOTS_NEAR_GAP):
    """[(stand (x, y), anchor)] for spots on src (nearest target first) with a grapple onto target. Cached."""
    key = (id(world), src[0]["id"], target[0]["id"], nspots)
    if key in _GRAPPLES:
        return _GRAPPLES[key]
    out = []
    anchors = _anchors_on(world, target[0]["id"])
    if anchors and target[0]["id"] in world.by_id:
        spots, _props = cc.roof_spots(world, src)
        spots = sorted(spots, key=lambda s: (_ring_distance(s, target[1]), s))[:nspots]
        z = world.roof_z(src)
        for s in spots:
            here3 = (s[0], s[1], z)
            near = sorted((a for a in anchors if _dist2(here3, a) <= cc.GRAPPLE_RANGE),
                          key=lambda a: (cc._dist3((here3[0], here3[1], here3[2] + 90.0), a[:3]), a[:3]))
            for anchor in near:
                if grapple_ok(cc, world, here3, src[0]["id"], anchor):
                    out.append((s, anchor))
                    break
    _GRAPPLES[key] = out
    return out


def _gap(gen, a_top, b_top):
    """The narrowest gap between two footprints, cm (0 where they touch)."""
    return min(min(gen.ring_distance(p, b_top[1]) for p in a_top[1]), min(gen.ring_distance(p, a_top[1]) for p in b_top[1]))


def _ring_distance(pt, ring):
    import generate_city as gen  # noqa: PLC0415
    return gen.ring_distance(pt, ring)


def _chain_from(cc, world, g1, g2, g3, nspots):
    """[(stand on g1, anchor on g2, fraction, chain point, anchor on g3)]: zip at g2 and chain to g3 mid-air."""
    out = []
    z1 = world.roof_z(g1)
    for s1, a2 in grapples_into(cc, world, g1, g2, nspots):
        launch = (s1[0], s1[1], z1 + cc.ZIP_LAUNCH)
        l2 = (a2[3], a2[4], a2[5])
        for f in CHAIN_AT:
            mid = tuple(launch[k] + (l2[k] - launch[k]) * f for k in range(3))
            ux, uy = l2[0] - launch[0], l2[1] - launch[1]
            un = math.hypot(ux, uy) or 1.0
            for a3 in world.anchors:
                if a3[6] != g3[0]["id"] or (g2 is g3 and a3 is a2):
                    continue
                if cc._dist3((mid[0], mid[1], mid[2] + 90.0), (a3[0], a3[1], a3[2])) > cc.GRAPPLE_RANGE - cc.GRAPPLE_SLACK:
                    continue
                if _dist2(mid, (a3[3], a3[4])) < CHAIN_MIN:
                    continue
                vx, vy = a3[0] - mid[0], a3[1] - mid[1]
                if (ux * vx + uy * vy) / (un * (math.hypot(vx, vy) or 1.0)) < CHAIN_COS:
                    continue
                if not cc.zip_clear(world, mid, (a3[3], a3[4], a3[5]), g3[0]["id"], g2[0]["id"]):
                    continue
                if not cc.camera_sees(world, mid, (a3[0], a3[1], a3[2]), g3[0]["id"]):
                    continue
                out.append((s1, a2, f, mid, a3))
                break
            if out and out[-1][0] == s1:
                break
    return out


def _leap_heights_ok(world, near_top, far_top):
    """The thief can leap from near_top's roof to far_top's: level, a little up, or down (LEAP_DROP)."""
    return LEAP_DROP[0] <= world.roof_z(far_top) - world.roof_z(near_top) <= LEAP_DROP[1]


def _clear_of_fighters(points, fighters):
    return all(_dist2(p, (x, y)) >= r for p in points for x, y, r in fighters)


def _alley_mouth(cr, world, gen, mid):
    """Where the alley at mid comes out on a sidewalk: create_crimes' walk out, the point it reaches; or None."""
    tops = world.tops_near(mid[0], mid[1], cr.ALLEY_CELL * 200.0)
    walkable = {}

    def ok(cell):
        if cell not in walkable:
            pt = (mid[0] + cell[0] * cr.ALLEY_CELL, mid[1] + cell[1] * cr.ALLEY_CELL)
            walkable[cell] = cr._wall_clearance(gen, pt, tops) >= cr.ALLEY_CLEAR
        return walkable[cell]

    frontier, head, seen = [(0, 0)], 0, {(0, 0)}
    while head < len(frontier) and len(seen) < cr.ALLEY_WALK_CELLS:
        cell = frontier[head]
        head += 1
        pt = (mid[0] + cell[0] * cr.ALLEY_CELL, mid[1] + cell[1] * cr.ALLEY_CELL)
        if cr._carriageway_distance(world, pt) <= cr.ALLEY_SIDEWALK:
            return pt
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nxt = (cell[0] + dx, cell[1] + dy)
            if nxt not in seen and ok(nxt):
                seen.add(nxt)
                frontier.append(nxt)
    return None


def _street_end(cc, world, foot, mouth, facade_ring):
    """Where he is cornered: a sidewalk point STREET_RUN from the fire escape's foot along the facade it stands on,
    with a clear run to it, the one nearest the alley's mouth (but MOUTH_CLEAR from it), so that climbing out of
    the alley she comes up on him; or None."""
    import generate_city as gen  # noqa: PLC0415
    ring = list(facade_ring) + [facade_ring[0]]
    best_edge = None
    for a, b in zip(ring, ring[1:]):
        d, _q = gen.closest_point_on_polyline(foot, [a, b])
        if best_edge is None or d < best_edge[0]:
            best_edge = (d, a, b)
    _d, a, b = best_edge
    length = _dist2(a, b) or 1.0
    ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    best = None
    for sign in (1.0, -1.0):
        run = STREET_RUN[0]
        while run <= STREET_RUN[1]:
            pt = (foot[0] + ux * sign * run, foot[1] + uy * sign * run)
            run += 400.0
            if _dist2(pt, mouth) < MOUTH_CLEAR or not cc.on_sidewalk(world, pt) or not cc.street_clear(world, pt, 120.0):
                continue
            if not cc.street_path_clear(world, foot, pt):
                break
            score = (_dist2(pt, mouth), sign, run)
            if best is None or score < best[0]:
                best = (score, pt)
    return best[1] if best else None


def _roof_route(cc, world, top, a, b):
    """[points after a] for a run from a to b on one roof: straight when clear, else by one roof spot that sees
    both, the shortest such detour; None when neither works."""
    props = world.props_near((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, _dist2(a, b) + 600.0)
    if cc.roof_path_clear(world, a, b, [top], props):
        return [b]
    spots, _props = cc.roof_spots(world, top)
    for q in sorted(spots, key=lambda q: (_dist2(a, q) + _dist2(q, b), q)):
        if cc.roof_path_clear(world, a, q, [top], props) and cc.roof_path_clear(world, q, b, [top], props):
            return [q, b]
    return None


def _dumpster(world, g3, b_top, last_a, mid):
    """(centre, yaw, landing, exit) for the dumpster against g3's wall where her line leaves it, its back to the
    wall and its front toward the alley's middle, or None when it does not fit between the walls."""
    import generate_city as gen  # noqa: PLC0415
    _d, wall = gen.closest_point_on_polyline(last_a, list(g3[1]) + [g3[1][0]])
    ux, uy = mid[0] - wall[0], mid[1] - wall[1]
    un = math.hypot(ux, uy)
    if un < 1.0:
        return None
    ux, uy = ux / un, uy / un
    depth = DUMPSTER[1]
    centre = (wall[0] + ux * (DUMPSTER_WALL_GAP + depth * 0.5), wall[1] + uy * (DUMPSTER_WALL_GAP + depth * 0.5))
    exit_pt = (centre[0] + ux * (depth * 0.5 + EXIT_PAST), centre[1] + uy * (depth * 0.5 + EXIT_PAST))
    along = (-uy, ux)
    corners = [(centre[0] + sx * along[0] * DUMPSTER[0] * 0.5 + sy * ux * depth * 0.5,
                centre[1] + sx * along[1] * DUMPSTER[0] * 0.5 + sy * uy * depth * 0.5) for sx in (-1, 1) for sy in (-1, 1)]
    for pt in corners + [exit_pt, centre]:
        if world.building_at(pt) is not None:
            return None
    if gen.ring_distance(exit_pt, b_top[1]) < 45.0 or gen.ring_distance(exit_pt, g3[1]) < 45.0:
        return None
    yaw = math.degrees(math.atan2(along[1], along[0]))
    landing = (centre[0], centre[1], ALLEY_FLOOR + DUMPSTER_FLOOR)
    return (centre[0], centre[1], ALLEY_FLOOR), yaw, landing, (exit_pt[0], exit_pt[1], ALLEY_FLOOR)


def _gap_candidates(cr, world, gen, district):
    """[(start distance, (x, y), width, osm a, osm b)]: the middles of 3 to 8 m gaps between two footprints close
    to a street (their carriageway edge GAP_KERB away: the gap's mouth on the sidewalk, where a dumpster stands),
    off the park, clear of the walls and the street clutter. create_crimes.alley_candidates' search, kept near
    the street instead of 8 m back from it. Nearest the PlayerStart first."""
    out = []
    reach = BEAT_GAP_MAX + 50.0
    for top in world.tops:
        rec, ring = top[0], top[1]
        if gen.ring_distance(world.start, ring) > SEARCH_RADIUS:
            continue
        n = len(ring)
        ccw = geo.is_ccw(ring)
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            length = math.hypot(bx - ax, by - ay)
            if length < 2.0 * cr.ALLEY_END_INSET + cr.ALLEY_STEP:
                continue
            ux, uy = (bx - ax) / length, (by - ay) / length
            nx, ny = (uy, -ux) if ccw else (-uy, ux)
            t = cr.ALLEY_END_INSET
            while t <= length - cr.ALLEY_END_INSET:
                p = (ax + ux * t, ay + uy * t)
                t += cr.ALLEY_STEP * 2.0
                others = [o for o in world.tops_near(p[0], p[1], reach) if o[0]["id"] != rec["id"]]
                gap = cr._ray_hit(p, (nx, ny), reach, others) if others else None
                if gap is None or not 300.0 <= gap <= BEAT_GAP_MAX:
                    continue
                if cr._ray_hit(p, (nx, ny), gap - 1.0, [top]) is not None:
                    continue
                mid = (p[0] + nx * gap * 0.5, p[1] + ny * gap * 0.5)
                if world.building_at(mid) is not None or cr._in_park(district, mid):
                    continue
                if not GAP_KERB[0] <= cr._carriageway_distance(world, mid) <= GAP_KERB[1]:
                    continue
                if cr._wall_clearance(gen, mid, world.tops_near(mid[0], mid[1], 800.0)) < cr.ALLEY_CLEAR:
                    continue
                if any(z0 < 400.0 and math.hypot(mid[0] - x, mid[1] - y) < r + cr.ALLEY_PROP_CLEAR
                       for x, y, z0, _z1, r, _k in world.props_near(mid[0], mid[1], 1000.0)):
                    continue
                other = world.building_at((p[0] + nx * (gap + 20.0), p[1] + ny * (gap + 20.0)))
                if other is not None:
                    out.append((_dist2(world.start, mid), mid, gap, rec["id"], other[0]["id"]))
    out.sort(key=lambda e: (e[0], e[1]))
    return out


def _beats(cc, cr, gen, world, district, taken):
    """Authored gaps: (start distance, alley mid, g3, b, stand, anchor, crossing, dumpster, descent)."""
    out = []
    seen = []
    for dd, mid, _width, a, b in _gap_candidates(cr, world, gen, district):
        if dd > SEARCH_RADIUS or any(_dist2(mid, q) < 500.0 for q in seen):
            continue
        seen.append(mid)
        mouth = _alley_mouth(cr, world, gen, mid)
        if mouth is None or _dist2(mouth, mid) > ALLEY_OUT_MAX:
            continue
        for g3_id, b_id in ((a, b), (b, a)):
            g3, b_top = world.by_id.get(g3_id), world.by_id.get(b_id)
            if g3 is None or b_top is None or not _ok_roof(world, g3, taken) or not _ok_roof(world, b_top, taken):
                continue
            descent = cc.descent_spots(world, b_top)
            if descent is None:
                continue
            for stand, anchor in grapples_into(cc, world, g3, b_top, 40):
                cross = line_cross(g3, b_top, stand, (anchor[3], anchor[4]))
                if cross is None or cross[0] > BEAT_GAP_MAX or _dist2(cross[1], mid) > 900.0:
                    continue
                if not LEAP_DROP[0] <= world.roof_z(b_top) - world.roof_z(g3) <= LEAP_DROP[1]:
                    continue
                dump = _dumpster(world, g3, b_top, cross[1], mid)
                if dump is None:
                    continue
                out.append((dd, mid, g3, b_top, stand, anchor, cross, dump, descent, mouth))
                break
    out.sort(key=lambda e: (e[0], e[2][0]["id"]))
    return out


def _mantle_starts(cc, gen, world, rm, sm, taken, used):
    """[(rl, start (x, y), crossing on rl, crossing on rm)]: a run of RUN_START or more on a neighbour a step below
    rm, straight into the mantle and on to her grapple stand sm."""
    out = []
    z_m = world.roof_z(rm)
    for rl in cc.neighbours(world, rm):
        step = z_m - world.roof_z(rl)
        if not (cc.MANTLE_STEP[0] <= step <= cc.MANTLE_STEP[1]) or not _ok_roof(world, rl, taken) or rl[0]["id"] in used:
            continue
        spots, props = cc.roof_spots(world, rl, inset=PROP_EDGE_CLEAR)
        spots = sorted(spots, key=lambda s: (-_dist2(s, sm), s))
        for s in spots:
            cross = line_cross(rl, rm, s, sm)
            if cross is None or cross[0] > 60.0 or _dist2(s, cross[1]) < RUN_START:
                continue
            props_all = world.props_near(s[0], s[1], _dist2(s, sm) + 300.0)
            if not cc.roof_path_clear(world, s, sm, [rl, rm], props_all):
                continue
            # The two vent boxes must stand on rl, clear of its edges.
            ok = True
            for at in PROP_AT:
                p = _toward(s, sm, at)
                if not geo.point_in_polygon(p, rl[1]) or gen._edge_clearance(p, rl[1]) < PROP_EDGE_CLEAR:
                    ok = False
            if ok:
                out.append((rl, s, cross[1], cross[2]))
                break
    return out


def _bulkhead_start(cc, gen, world, rm, sm):
    """(start (x, y), bulkhead centre (x, y)) on rm: the fallback when no neighbour is a step up. A roof's stair
    bulkhead (BULKHEAD) BULKHEAD_AT along a straight run of BULKHEAD_RUN or more to her grapple stand sm, the run
    and the bulkhead clear of rm's edges and props; or None."""
    spots, props = cc.roof_spots(world, rm, inset=PROP_EDGE_CLEAR)
    z = world.roof_z(rm)
    half = math.hypot(BULKHEAD[0], BULKHEAD[1]) * 0.5
    for s in sorted(spots, key=lambda q: (-_dist2(q, sm), q)):
        if _dist2(s, sm) < BULKHEAD_RUN:
            break
        if not cc.roof_path_clear(world, s, sm, [rm], props):
            continue
        centre = _toward(s, sm, BULKHEAD_AT)
        if gen._edge_clearance(centre, rm[1]) < half + 60.0 or not cc.clear_of_props(world, centre, z, half + 40.0, props):
            continue
        if any(gen._edge_clearance(_toward(s, sm, at), rm[1]) < PROP_EDGE_CLEAR for at in PROP_AT):
            continue
        return s, centre
    return None


def plan_chase(district):
    """The chase's plan (a dict), or None when the district has no route that fits. Cached per district."""
    key = id(district)
    if key in _PLAN:
        return _PLAN[key]
    import generate_city as gen  # noqa: PLC0415
    import create_challenges as cc  # noqa: PLC0415
    import create_crimes as cr  # noqa: PLC0415
    world = cc.planning_world(district)
    taken = set(world.taken)
    for plan in cc.plan_challenges(district):
        taken |= set(plan.get("roofs", []))
        if plan.get("osm"):
            taken.add(plan["osm"])
    for spot in cr.plan_crime_spots(district):
        if spot["rooftop"]:
            taken.add(spot["osm"])
    fighters = [(a[1], a[2], FIGHTER_CLEAR) for a in gen.archer_placements(district)]
    thugs, _points, _roof = gen.thug_placements(district)
    fighters += [(t[1], t[2], THUG_CLEAR) for t in thugs]
    result = None
    for beat in _beats(cc, cr, gen, world, district, taken):
        result = _plan_from_beat(cc, gen, world, beat, taken, fighters)
        if result is not None:
            break
    _PLAN[key] = result
    return result


def _plan_from_beat(cc, gen, world, beat, taken, fighters):
    _dd, mid, g3, b_top, pb, ab, cross_b, dump, descent, _mouth = beat
    used = {g3[0]["id"], b_top[0]["id"]}
    g2s = [g3] + sorted((t for t in _near_tops(world, g3, cc.GRAPPLE_RANGE) if _ok_roof(world, t, taken)
                         and t[0]["id"] not in used and _gap(gen, t, g3) <= 1500.0), key=lambda t: (_gap(gen, t, g3), t[0]["id"]))
    for g2 in g2s:
        g1s = sorted((t for t in _near_tops(world, g2, cc.GRAPPLE_RANGE) if _ok_roof(world, t, taken)
                      and t[0]["id"] not in used | {g2[0]["id"]} and _gap(gen, t, g2) <= 1800.0),
                     key=lambda t: (_gap(gen, t, g2), t[0]["id"]))
        for g1 in g1s:
            if not _leap_heights_ok(world, g1, g2) or (g2 is not g3 and not _leap_heights_ok(world, g2, g3)):
                continue
            for s1, a2, frac, chain_pt, a3 in _chain_from(cc, world, g1, g2, g3, 12):
                l2, l3 = (a2[3], a2[4]), (a3[3], a3[4])
                if g2 is not g3 and line_cross(g2, g3, l2, l3) is None:
                    continue
                if _roof_route(cc, world, g3, l3, pb) is None:
                    continue
                if line_cross(g1, g2, s1, l2) is None:
                    continue
                plan = _plan_from_chain(cc, gen, world, beat, taken, fighters, used | {g1[0]["id"], g2[0]["id"]},
                                        (g1, g2, s1, a2, frac, chain_pt, a3))
                if plan is not None:
                    return plan
    return None


def _plan_from_chain(cc, gen, world, beat, taken, fighters, used, chain):
    g1, g2, s1, a2, frac, chain_pt, a3 = chain
    rms = sorted((t for t in _near_tops(world, g1, cc.GRAPPLE_RANGE) if _ok_roof(world, t, taken)
                  and t[0]["id"] not in used and 150.0 <= _gap(gen, t, g1) <= 1800.0), key=lambda t: (_gap(gen, t, g1), t[0]["id"]))
    for rm in rms:
        if not _leap_heights_ok(world, rm, g1):
            continue
        for sm, a1 in grapples_into(cc, world, rm, g1, 15):
            l1 = (a1[3], a1[4])
            cross1 = line_cross(rm, g1, sm, l1)
            if cross1 is None or cross1[0] > LEAP_MAX:
                continue
            if _roof_route(cc, world, g1, l1, s1) is None:
                continue
            mantles = [("step",) + m for m in _mantle_starts(cc, gen, world, rm, sm, taken, used | {rm[0]["id"]})]
            if not mantles:
                bulkhead = _bulkhead_start(cc, gen, world, rm, sm)
                mantles = [("bulkhead", rm, bulkhead[0], bulkhead[1], None)] if bulkhead else []
            for mantle in mantles:
                plan = _assemble(cc, gen, world, beat, chain, (rm, sm, a1, cross1), mantle)
                if plan is not None and _clear_of_fighters(plan["check_points"], fighters):
                    return plan
    return None


def _assemble(cc, gen, world, beat, chain, first, mantle):
    """The waypoints and everything placed, from the pieces the search found."""
    _dd, mid, g3, b_top, pb, ab, cross_b, dump, descent, _mouth = beat
    g1, g2, s1, a2, frac, chain_pt, a3 = chain
    rm, sm, a1, cross1 = first
    kind, rl, start, cross_l, cross_m = mantle
    z_l, z_m, z_1, z_2, z_3, z_b = (world.roof_z(t) for t in (rl, rm, g1, g2, g3, b_top))
    stand, foot, _floor = descent
    mouth = beat[9]
    end = _street_end(cc, world, foot, mouth, b_top[1])
    if end is None:
        REJECTS["no street end"] = REJECTS.get("no street end", 0) + 1
        return None
    to_escape = _roof_route(cc, world, b_top, (ab[3], ab[4]), stand)
    if to_escape is None:
        REJECTS["far roof to the fire escape"] = REJECTS.get("far roof to the fire escape", 0) + 1
        return None
    wp = []

    def add(xy, z, leg, objective="", sequence=""):
        wp.append({"at": (xy[0], xy[1], z), "leg": leg, "objective": objective, "sequence": sequence})

    def leap(near_top, far_top, z_near, z_far, cross, landing):
        _gap, last_a, first_b = cross
        add(_toward(last_a, landing, -EDGE_IN), z_near, "Run")
        add(last_a, z_near + PARAPET, "Vault")
        add(first_b, z_far + PARAPET, "JumpGap")
        add(_toward(first_b, landing, EDGE_IN), z_far, "Vault")

    # The start roof: the run over the two vent boxes, to the party wall and up onto the step.
    add(start, z_l, "Run")
    props = []
    for at in PROP_AT:
        p = _toward(start, sm, at)
        add(_toward(p, start, PROP_SIZE[1] * 0.5 + 70.0), z_l, "Run")
        add(_toward(p, sm, PROP_SIZE[1] * 0.5 + 70.0), z_l, "Vault")
        props.append(p)
    bulkheads = []
    if kind == "step":
        add(_toward(cross_l, start, EDGE_IN), z_l, "Run")
        add(cross_m, z_m + PARAPET, "Climb")
        add(_toward(cross_m, sm, EDGE_IN), z_m, "Vault")
    else:
        # Up onto the bulkhead, across its top and down off its far side.
        centre = cross_l
        near, far = _toward(centre, start, BULKHEAD[0] * 0.5), _toward(centre, sm, BULKHEAD[0] * 0.5)
        add(_toward(near, start, 70.0), z_l, "Run")
        add(_toward(near, sm, 40.0), z_l + BULKHEAD[2], "Climb")
        add(_toward(far, start, 40.0), z_l + BULKHEAD[2], "Run")
        add(_toward(far, sm, 90.0), z_l, "Drop")
        bulkheads.append(centre)
    add(sm, z_m, "Run", objective="chase_thief", sequence="seq_ch01_grapple")
    # The first gap (cross_gap): he leaps it, she grapples it.
    l1 = (a1[3], a1[4])
    leap(rm, g1, z_m, z_1, cross1, l1)
    add(l1, z_1, "Run", objective="cross_gap")
    for q in _roof_route(cc, world, g1, l1, s1):
        add(q, z_1, "Run")
    # The chain: g1 to g2 (and on to g3 when they differ).
    l2, l3 = (a2[3], a2[4]), (a3[3], a3[4])
    leap(g1, g2, z_1, z_2, line_cross(g1, g2, s1, l2), l2)
    add(l2, z_2, "Run")
    if g2 is not g3:
        leap(g2, g3, z_2, z_3, line_cross(g2, g3, l2, l3), l3)
    add(l3, z_3, "Run")
    # The authored gap: he leaps the alley; her grapple goes wrong.
    for q in _roof_route(cc, world, g3, l3, pb):
        add(q, z_3, "Run")
    lb = (ab[3], ab[4])
    leap(g3, b_top, z_3, z_b, cross_b, lb)
    beat_waypoint = len(wp) - 1
    add(lb, z_b, "Run")
    # Down the fire escape and along the sidewalk.
    for q in to_escape:
        add(q, z_b, "Run")
    edge = _toward(stand, foot, cc.DESCENT_BACK)
    add(edge, z_b + PARAPET, "Vault")
    add(foot, gen.SIDEWALK_TOP, "Drop")
    add(end, gen.SIDEWALK_TOP, "Run")
    for prev, nxt in zip(wp, wp[1:]):
        if nxt["leg"] == "Run" and abs(prev["at"][2] - nxt["at"][2]) > 30.0:
            REJECTS["a run changes level"] = REJECTS.get("a run changes level", 0) + 1
            return None
    yaw0 = math.degrees(math.atan2(sm[1] - start[1], sm[0] - start[0]))
    zone_yaw = math.degrees(math.atan2(ab[4] - pb[1], ab[3] - pb[0]))
    prop_yaw = yaw0                # the vent's depth (its X) along the run
    plan = {
        "waypoints": wp,
        "player_start": (start[0], start[1], z_l),
        "player_yaw": yaw0,
        "props": [(p[0], p[1], z_l, prop_yaw) for p in props],
        "bulkheads": [(b[0], b[1], z_l, yaw0) for b in bulkheads],
        "mantle": kind,
        "grapples": [
            {"stand": (sm[0], sm[1], z_m), "anchor": a1[:3], "chain": None, "at": 0.0, "beat": False},
            {"stand": (s1[0], s1[1], z_1), "anchor": a2[:3], "chain": a3[:3], "at": frac, "beat": False},
            {"stand": (pb[0], pb[1], z_3), "anchor": ab[:3], "chain": None, "at": 0.0, "beat": True},
        ],
        "beat": {"zone": (pb[0], pb[1], z_3 + BEAT_ZONE[2] * 0.5), "zone_extent": BEAT_ZONE, "zone_yaw": zone_yaw,
                 "anchor": ab[:3], "waypoint": beat_waypoint, "dumpster": dump[0], "dumpster_yaw": dump[1],
                 "landing": dump[2], "exit": dump[3], "alley": (mid[0], mid[1]), "mouth": (mouth[0], mouth[1])},
        "roofs": _unique([t[0]["id"] for t in (rl, rm, g1, g2, g3, b_top)]),
        "end": (end[0], end[1], gen.SIDEWALK_TOP),
        "leaps": [round(c[0]) for c in (cross1, line_cross(g1, g2, s1, l2), cross_b)],
    }
    plan["check_points"] = [w["at"] for w in wp] + [g["stand"] for g in plan["grapples"]]
    return plan


def _unique(ids):
    out = []
    for i in ids:
        if i not in out:
            out.append(i)
    return out


def route_length(plan):
    pts = [w["at"] for w in plan["waypoints"]]
    return sum(math.sqrt(sum((a[k] - b[k]) ** 2 for k in range(3))) for a, b in zip(pts, pts[1:]))


def describe(plan):
    if plan is None:
        return "no chase route fits the district"
    legs = {}
    for w in plan["waypoints"][1:]:
        legs[w["leg"]] = legs.get(w["leg"], 0) + 1
    return "{0} waypoints, {1:.0f} m, legs {2}, mantle by {11}, roofs {3}, leaps {4} cm, beat over the alley at ({5:.0f}, {6:.0f}), " \
        "dumpster at ({7:.0f}, {8:.0f}), cornered at ({9:.0f}, {10:.0f})".format(
            len(plan["waypoints"]), route_length(plan) / 100.0, ", ".join("{0} {1}".format(k, v) for k, v in sorted(legs.items())),
            " > ".join(plan["roofs"]), plan["leaps"], plan["beat"]["alley"][0], plan["beat"]["alley"][1],
            plan["beat"]["dumpster"][0], plan["beat"]["dumpster"][1], plan["end"][0], plan["end"][1], plan["mantle"])
