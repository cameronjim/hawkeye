"""Plan chapter 2's toy drive from the district's records (claude-docs/gameplay-semantics.md, "Chapter sites";
docs/chapters/CH02.md).

``plan_site(district)`` picks, from the same OpenStreetMap records generate_city.py builds the district from, where
Act 1's "Reindeer Games" happens and lays it out:

* the drive: a street-level spot on Avenue B (the avenue's own pavement, or its asphalt by the facades where the
  25 m avenue runs under them), the table's back to a facade, with room in front for the street fight, and nobody
  else's: 40 m from the safehouses, the challenge pedestals, the interior's door and the boss arena's street stand,
  35 m from every crime spot, 30 m from chapter 1's fighters and 25 m from the chase's route; nearest the district's
  PlayerStart of those at least DRIVE_START_MIN away, so the chapter opens with a run across the district;
* the dressing: two folding tables end to end with boxes on them and stacked beside, a banner on two poles over them,
  string lights between the poles and two more in front, a volunteer behind the table;
* the crew: five places in the street in front of the table (two fists, two bats, a gunner further out), their middle
  the stop_crew marker; the arrival volume round the drive that wakes them;
* the sacks: three, where the crew had piled them at the kerb a few metres along;
* the courier: an examine on the pavement 25 to 60 m along the avenue (on the PlayerStart's side when it can be), a
  covered shape on the ground, two police officers standing over it and a cruiser at the kerb.

Pure: no unreal calls beyond what generate_city and create_challenges need at import (test_site stubs them). Cached
per district object.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

AVENUE = "Avenue B"
STEP = 300.0                    # cm between candidates along the avenue
OFFSETS = tuple(range(-1000, 650, 50))   # cm past the carriageway's half width a table spot is tried (B runs under its facades)
TABLE_CLEAR = 250.0             # cm of street clutter (cars, bins, lamps) kept off the table
DRIVE_START_MIN = 9000.0        # cm from the PlayerStart: the chapter opens with a run
DRIVE_START_MAX = 25000.0
SAFEHOUSE_CLEAR = 4000.0
PEDESTAL_CLEAR = 4000.0
INTERIOR_CLEAR = 4000.0
ARENA_CLEAR = 4000.0
CRIME_CLEAR = 3500.0
FIGHTER_CLEAR = 3000.0
CHASE_CLEAR = 2500.0

TABLE = (180.0, 75.0, 74.0)     # length, depth, height (the boss kit's folding table)
TABLE_OFF_WALL = 80.0           # cm from the table's spot toward the street its middle stands
BANNER_HEIGHT = 260.0           # cm, the banner's bottom
POLE_HEIGHT = 300.0
POLE_SPREAD = 260.0             # cm either side of the table's middle the poles stand
LIGHT_SPAN_OUT = 350.0          # cm in front of the table the second string of lights runs
CREW = (("Fists", 450.0, -180.0), ("Fists", 450.0, 200.0), ("Bat", 600.0, -380.0), ("Bat", 620.0, 360.0),
        ("Pistol", 900.0, 40.0))  # (weapon, cm out from the table, cm along the avenue)
CREW_CLEAR = 90.0               # cm of street clutter kept off a crew place
SACKS = ((260.0, 650.0), (330.0, 760.0), (230.0, 840.0))  # (cm out, cm along): piled at the kerb past the table's end
SACK_CLEAR = 60.0
ARRIVE_EXTENT = (1100.0, 1100.0, 300.0)
COURIER_RANGE = (2500.0, 6000.0)   # cm along the avenue from the drive
COURIER_CLEAR = 200.0
POLICE = ((160.0, -120.0), (140.0, 150.0))   # (cm out toward the street, cm along) from the body
CRUISER_OUT = 520.0             # cm out from the body the cruiser's middle is
VOLUNTEER_BEHIND = 70.0         # cm behind the table (toward the wall) the volunteer stands

_PLAN = {}


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _yaw(u):
    return math.degrees(math.atan2(u[1], u[0]))


def _blockers(district, world, gen, cc):
    """[(x, y, clearance, what)] the site keeps away from."""
    import create_crimes as cr  # noqa: PLC0415
    import _arena  # noqa: PLC0415
    import _chase  # noqa: PLC0415
    out = []
    for label, s in gen.safehouse_spots(district):
        out.append((s["x"], s["y"], SAFEHOUSE_CLEAR, label))
    for plan in cc.plan_challenges(district):
        out.append((plan["start"][0], plan["start"][1], PEDESTAL_CLEAR, "pedestal " + str(plan.get("id"))))
    door = gen.interior_keepout(district)
    if door is not None:
        out.append((door[0], door[1], INTERIOR_CLEAR, "interior door"))
    arena = _arena.plan_arena(district)
    if arena is not None:
        out.append((arena["player_start"][0], arena["player_start"][1], ARENA_CLEAR, "arena stand"))
    for spot in cr.plan_crime_spots(district):
        out.append((spot["at"][0], spot["at"][1], CRIME_CLEAR, "crime spot"))
    thugs, _points, _roof = gen.thug_placements(district)
    for t in thugs:
        out.append((t[1], t[2], FIGHTER_CLEAR, "chapter 1 thug"))
    for a in gen.archer_placements(district):
        out.append((a[1], a[2], FIGHTER_CLEAR, "chapter 1 archer"))
    chase = _chase.plan_chase(district)
    if chase is not None:
        for w in chase["waypoints"]:
            out.append((w["at"][0], w["at"][1], CHASE_CLEAR, "chase route"))
    return out


def _clear_of(pt, blockers):
    return all(_dist2(pt, (x, y)) >= r for x, y, r, _what in blockers)


def _street_point(world, cc, gen, pt, clearance):
    """On the street (not in a building), clear of clutter by clearance."""
    return world.building_at(pt) is None and cc.street_clear(world, pt, clearance)


def _avenue_frames(world):
    """[(point on the centre line, along unit, out unit for one side, half width)] every STEP along the avenue, both sides."""
    frames = []
    for a, b, half, name in world.roads:
        if name != AVENUE:
            continue
        length = _dist2(a, b)
        if length < 1.0:
            continue
        u = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
        s = STEP * 0.5
        while s < length:
            c = (a[0] + u[0] * s, a[1] + u[1] * s)
            for side in (1.0, -1.0):
                frames.append((c, u, (-u[1] * side, u[0] * side), half))
            s += STEP
    return frames


def _layout(world, cc, gen, spot, along, wall, half):
    """The drive laid out from its table spot, or None when something does not fit."""
    street = (-wall[0], -wall[1])

    def at(out_cm, along_cm):
        return (spot[0] + street[0] * out_cm + along[0] * along_cm, spot[1] + street[1] * out_cm + along[1] * along_cm)

    crew = []
    for weapon, out_cm, along_cm in CREW:
        pt = at(out_cm, along_cm)
        if not _street_point(world, cc, gen, pt, CREW_CLEAR):
            return None
        crew.append((weapon, pt))
    sacks = []
    for out_cm, along_cm in SACKS:
        pt = at(out_cm, along_cm)
        if not _street_point(world, cc, gen, pt, SACK_CLEAR):
            return None
        sacks.append(pt)
    poles = [at(TABLE_OFF_WALL, -POLE_SPREAD), at(TABLE_OFF_WALL, POLE_SPREAD)]
    front = [at(LIGHT_SPAN_OUT, -POLE_SPREAD), at(LIGHT_SPAN_OUT, POLE_SPREAD)]
    for pt in poles + front:
        if not _street_point(world, cc, gen, pt, 40.0):
            return None
    return crew, sacks, poles, front, at


def _courier(world, cc, gen, drive, along, wall, blockers, toward_start):
    """(body, the frame's along and wall) for the courier: on the avenue's pavement, 25 to 60 m from the drive."""
    best = None
    for c, u, out, half in _avenue_frames(world):
        for off in OFFSETS:
            pt = (c[0] + out[0] * (half + off), c[1] + out[1] * (half + off))
            d = _dist2(pt, drive)
            if not COURIER_RANGE[0] <= d <= COURIER_RANGE[1]:
                continue
            if not cc.on_sidewalk(world, pt) or not _street_point(world, cc, gen, pt, COURIER_CLEAR):
                continue
            if not _clear_of(pt, [b for b in blockers if b[3] != "chase route"]):
                continue
            ok = True
            for out_cm, along_cm in POLICE:
                q = (pt[0] - out[0] * out_cm + u[0] * along_cm, pt[1] - out[1] * out_cm + u[1] * along_cm)
                ok = ok and _street_point(world, cc, gen, q, 60.0)
            cruiser = (pt[0] - out[0] * CRUISER_OUT, pt[1] - out[1] * CRUISER_OUT)
            ok = ok and _street_point(world, cc, gen, cruiser, 150.0)
            if not ok:
                continue
            # Toward where she comes from, so she passes it on the way; then the nearer the middle of the range.
            score = (0 if toward_start(pt) else 1, abs(d - sum(COURIER_RANGE) * 0.5), round(pt[0]), round(pt[1]))
            if best is None or score < best[0]:
                best = (score, pt, u, out, cruiser)
            break
    if best is None:
        return None
    _score, body, u, out, cruiser = best
    police = [(body[0] - out[0] * o + u[0] * a, body[1] - out[1] * o + u[1] * a) for o, a in POLICE]
    return {"body": body, "along": u, "wall": out, "police": police, "cruiser": cruiser}


def plan_site(district):
    """The toy drive's plan (a dict), or None when no spot on the avenue fits. Cached per district."""
    key = id(district)
    if key in _PLAN:
        return _PLAN[key]
    import generate_city as gen  # noqa: PLC0415
    import create_challenges as cc  # noqa: PLC0415
    world = cc.planning_world(district)
    blockers = _blockers(district, world, gen, cc)
    start_xf, _rot = gen.player_start_transform(district)
    start = (start_xf.x, start_xf.y)
    candidates = []
    for c, u, out, half in _avenue_frames(world):
        for off in OFFSETS:
            pt = (c[0] + out[0] * (half + off), c[1] + out[1] * (half + off))
            if not cc.on_sidewalk(world, pt) or not _street_point(world, cc, gen, pt, TABLE_CLEAR):
                continue
            # The table's back to a facade: a building within a metre and a half behind it.
            behind = (pt[0] + out[0] * 150.0, pt[1] + out[1] * 150.0)
            if world.building_at(behind) is None:
                continue
            d = _dist2(pt, start)
            if not DRIVE_START_MIN <= d <= DRIVE_START_MAX or not _clear_of(pt, blockers):
                continue
            candidates.append((round(d), round(pt[0]), round(pt[1]), pt, u, out, half))
            break
    candidates.sort(key=lambda c: c[:3])
    result = None
    for d, _x, _y, pt, u, out, half in candidates:
        laid = _layout(world, cc, gen, pt, u, out, half)
        if laid is None:
            continue
        crew, sacks, poles, front, at = laid

        def toward_start(q, pt=pt):
            return _dist2(q, start) < _dist2(pt, start)

        courier = _courier(world, cc, gen, pt, u, out, blockers, toward_start)
        if courier is None:
            continue
        result = _assemble(gen, start, pt, u, out, crew, sacks, poles, front, at, courier, d)
        break
    _PLAN[key] = result
    return result


def _assemble(gen, start, spot, along, wall, crew, sacks, poles, front, at, courier, start_distance):
    z = gen.SIDEWALK_TOP
    street = (-wall[0], -wall[1])
    facing = _yaw(street)
    table_mid = at(TABLE_OFF_WALL, 0.0)
    props = [
        ("table", (table_mid[0] - along[0] * TABLE[0] * 0.5, table_mid[1] - along[1] * TABLE[0] * 0.5, z, _yaw(along))),
        ("table", (table_mid[0] + along[0] * TABLE[0] * 0.5, table_mid[1] + along[1] * TABLE[0] * 0.5, z, _yaw(along))),
    ]
    for k, along_cm in enumerate((-230.0, -40.0, 120.0)):
        pt = at(TABLE_OFF_WALL, along_cm)
        props.append(("box", (pt[0], pt[1], z + TABLE[2], facing + 17.0 * k)))
    for k, (out_cm, along_cm) in enumerate(((20.0, -420.0), (40.0, 400.0), (-10.0, 450.0))):
        pt = at(out_cm, along_cm)
        props.append(("box", (pt[0], pt[1], z, facing + 31.0 * k)))
    for pt in poles:
        props.append(("pole", (pt[0], pt[1], z, 0.0)))
    banner = at(TABLE_OFF_WALL, 0.0)
    props.append(("banner", (banner[0], banner[1], z + BANNER_HEIGHT, facing)))
    volunteer = at(TABLE_OFF_WALL - VOLUNTEER_BEHIND, 60.0)
    props.append(("volunteer", (volunteer[0], volunteer[1], z, facing)))
    wires = []
    bulbs = []
    for a, b in ((poles[0], poles[1]), (poles[0], front[0]), (poles[1], front[1]), (front[0], front[1])):
        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        span = _dist2(a, b)
        wires.append((mid[0], mid[1], z + POLE_HEIGHT - 20.0, _yaw(((b[0] - a[0]) / span, (b[1] - a[1]) / span)), span))
        for k in (1, 2, 3):
            t = k / 4.0
            bulbs.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, z + POLE_HEIGHT - 25.0))
    for pt in front:
        props.append(("pole", (pt[0], pt[1], z, 0.0)))
    crew_mid = (sum(p[0] for _w, p in crew) / len(crew), sum(p[1] for _w, p in crew) / len(crew))
    arrive = at(400.0, 0.0)
    body = courier["body"]
    cstreet = (-courier["wall"][0], -courier["wall"][1])
    police = [(p[0], p[1], z, _yaw((body[0] - p[0], body[1] - p[1]))) for p in courier["police"]]
    cruiser = courier["cruiser"]
    return {
        "avenue": AVENUE,
        "spot": (spot[0], spot[1], z),
        "facing": facing,
        "along": along,
        "start_distance": start_distance,
        "player_start": (start[0], start[1], z),
        "player_yaw": _yaw(((spot[0] - start[0]) / max(_dist2(spot, start), 1.0), (spot[1] - start[1]) / max(_dist2(spot, start), 1.0))),
        "table": (table_mid[0], table_mid[1], z),
        "props": props,
        "wires": wires,
        "bulbs": bulbs,
        "crew": [(w, (p[0], p[1], z), _yaw((spot[0] - p[0], spot[1] - p[1]))) for w, p in crew],
        "crew_middle": (crew_mid[0], crew_mid[1], z),
        "sacks": [(p[0], p[1], z) for p in sacks],
        "arrive": (arrive[0], arrive[1], z + ARRIVE_EXTENT[2] * 0.5),
        "arrive_extent": ARRIVE_EXTENT,
        "courier": (body[0], body[1], z),
        "courier_yaw": _yaw(courier["along"]),
        "police": police,
        "cruiser": (cruiser[0], cruiser[1], z, _yaw(courier["along"])),
        "courier_street": cstreet,
    }


def describe(plan):
    if plan is None:
        return "no spot on {0} fits the toy drive".format(AVENUE)
    return "drive on {0} at ({1:.0f}, {2:.0f}) facing {3:.0f}, {4:.0f} m from the PlayerStart; {5} crew, {6} sacks, " \
        "{7} props, {8} bulbs; courier at ({9:.0f}, {10:.0f}), {11:.0f} m from the table".format(
            plan["avenue"], plan["spot"][0], plan["spot"][1], plan["facing"], plan["start_distance"] / 100.0, len(plan["crew"]),
            len(plan["sacks"]), len(plan["props"]), len(plan["bulbs"]), plan["courier"][0], plan["courier"][1],
            _dist2(plan["courier"], plan["spot"]) / 100.0)
