"""Plan chapter 3's grotto (claude-docs/gameplay-semantics.md, "Chapter sites", "Chapter interiors"; docs/chapters/CH03.md).

``plan_grotto(district)`` picks, from the same OpenStreetMap records generate_city.py builds the district from, where
Act 1's "The Grotto" happens on the street and lays out both halves of it:

* the frontage: a street door on a two-to-five storey building (a facade within reach of a street, the door slid along
  it clear of fire escapes), the pavement in front open, nobody else's spot near it: 40 m from the safehouses, the
  pedestals, the auction house's door and the arena's street stand, 35 m from every crime spot, 30 m from chapter 1's
  fighters, 25 m from the chase, 50 m from chapter 2's toy drive and courier; on a roof nobody else uses, with a point
  on it at least 2.5 m inside every edge for the roof stairs to come out at; 90 to 300 m from the PlayerStart, nearest
  the middle of that; dressed with candy-striped posts, fake snow and a "photos" sign over the door;
* the inside, in L_Int_Grotto's own frame from Tools/Interiors/Grotto.json: the throne, the queue's candy posts and the
  fake snow in the grotto, the toys in the stockroom, the four sleighs and the watches in the workshop, the keypad by
  the workshop door, the triggers (the first elf, the overheard talk, the boomerang on the shelf), the alarm box on the
  grotto's west wall that pulls the corridor's guard, and Santa's room as the boss's arena;
* the ornament: on the railing beside Safehouse 1's door (Kate's building), offered once the chapter is done.

Pure: no unreal calls beyond what generate_city and create_challenges need at import (test_grotto stubs them). Cached
per district object.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LAYOUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Interiors", "Grotto.json")
HEIGHT_M = (7.0, 17.0)          # two to five storeys
MIN_AREA_M2 = 120.0
START_RANGE = (9000.0, 30000.0) # cm from the PlayerStart
START_TARGET = 16000.0          # nearest this
DRIVE_CLEAR = 5000.0            # cm from chapter 2's drive and its courier
FRONT_OUT = 150.0               # cm out from the door the pavement is tried
FRONT_CLEAR = 220.0             # cm of street clutter kept off the pavement in front
ROOF_EDGE = 250.0               # cm the roof point keeps inside every edge
ROOF_ABOVE = 5.0                # cm above the roof the return point stands
DOORSTEP_OUT = 220.0            # cm out from the door Kate comes back out at
ARRIVE_OUT = 450.0              # cm out from the door the find_sleigh volume's middle is
ARRIVE_EXTENT = (650.0, 650.0, 300.0)
ROOF_EXTENT = (160.0, 160.0, 300.0)
ROOF_WALK = 300.0               # cm from the roof point the leave_roof volume's middle is: a few steps onto the roof
ROOF_WALK_EDGE = 200.0          # cm that middle keeps inside every roof edge
POSTS = ((160.0, 70.0), (-160.0, 70.0), (300.0, 70.0), (-300.0, 70.0))   # (cm along, cm out) of the candy posts
SNOW = ((230.0, 35.0, 1.0), (-230.0, 35.0, 1.0), (380.0, 40.0, 0.8), (-380.0, 40.0, 0.8))  # (along, out, size)
SIGN_HEIGHT = 300.0             # cm, the sign's bottom over the pavement
GARLAND_HEIGHT = 255.0
ORNAMENT_ALONG = 350.0          # cm along the safehouse's facade from its door (clear of the door's own zone)
ORNAMENT_OUT = 40.0
ORNAMENT_HEIGHT = 0.0           # its stand stands on the pavement

DOOR_TAG = "Site_GrottoDoor"
DOORSTEP = "City_InteriorReturn_Grotto"
ROOF_POINT = "City_InteriorRoof_Grotto"
INTERIOR_MAP = "/Game/Maps/L_Int_Grotto"

_PLAN = {}


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _yaw(u):
    return math.degrees(math.atan2(u[1], u[0]))


def load_layout():
    with open(LAYOUT, encoding="utf-8") as handle:
        return json.load(handle)


def _blockers(district, world, gen, cc):
    import _site  # noqa: PLC0415
    out = list(_site._blockers(district, world, gen, cc))
    drive = _site.plan_site(district)
    if drive is not None:
        out.append((drive["spot"][0], drive["spot"][1], DRIVE_CLEAR, "toy drive"))
        out.append((drive["courier"][0], drive["courier"][1], DRIVE_CLEAR, "courier"))
    return out


def _taken_roofs(district, world, cc, gen):
    import _arena  # noqa: PLC0415
    taken = set(_arena._taken(district, world, cc))
    taken |= {rec["id"] for rec in gen.objective_roofs(district).values()}
    taken |= {spot["rec"]["id"] for _label, spot in gen.safehouse_spots(district)}
    taken |= gen.interior_building_ids(district)
    arena = _arena.plan_arena(district)
    if arena is not None:
        taken.add(arena["roof"])
    end = gen.chapter_end_spot(district)
    if end is not None:
        taken.add(end["osm"])
    return taken


def _roof_point(gen, ring):
    """(x, y, edge distance) of the point on a 50 cm grid inside ring farthest from its edges, if ROOF_EDGE or more."""
    import _geo as geo  # noqa: PLC0415
    x0, y0, x1, y1 = geo.bounds(ring)
    best = None
    gx = x0 + 25.0
    while gx < x1:
        gy = y0 + 25.0
        while gy < y1:
            if geo.point_in_polygon((gx, gy), ring):
                edge = gen._edge_distance((gx, gy), ring)
                if best is None or edge > best[2] + 1e-6:
                    best = (gx, gy, edge)
            gy += 50.0
        gx += 50.0
    return best if best is not None and best[2] >= ROOF_EDGE else None


def plan_grotto(district):
    """The grotto's plan (a dict), or None when no building fits. Cached per district."""
    key = id(district)
    if key in _PLAN:
        return _PLAN[key]
    import generate_city as gen  # noqa: PLC0415
    import create_challenges as cc  # noqa: PLC0415
    import _geo as geo  # noqa: PLC0415
    world = cc.planning_world(district)
    blockers = _blockers(district, world, gen, cc)
    taken = _taken_roofs(district, world, cc, gen)
    start_xf, _rot = gen.player_start_transform(district)
    start = (start_xf.x, start_xf.y)
    escapes = [(f[2][0], f[2][1]) for f in gen.fire_escape_spots(district)]
    roads = [path for _rec, paths in gen.road_paths(district) for path in paths]
    candidates = []
    for rec in district.buildings:
        if rec["id"] in taken or not HEIGHT_M[0] <= rec["height_m"] <= HEIGHT_M[1]:
            continue
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3 or abs(geo.signed_area(ring)) / 10000.0 < MIN_AREA_M2:
            continue
        centre = geo.centroid(ring)
        hits = [gen.closest_point_on_polyline(centre, path) for path in roads]
        street = min((h for h in hits if h), key=lambda h: h[0], default=None)
        door = gen._door_clear_of(ring, street[1], escapes) if street is not None else None
        if door is None:
            continue
        out = (math.cos(math.radians(door[2])), math.sin(math.radians(door[2])))
        front = (door[0] + out[0] * FRONT_OUT, door[1] + out[1] * FRONT_OUT)
        if world.building_at(front) is not None or not cc.on_sidewalk(world, front) or not cc.street_clear(world, front, FRONT_CLEAR):
            continue
        d = _dist2(front, start)
        if not START_RANGE[0] <= d <= START_RANGE[1]:
            continue
        if not all(_dist2(front, (x, y)) >= r for x, y, r, _what in blockers):
            continue
        roof = _roof_point(gen, ring)
        if roof is None:
            continue
        candidates.append((round(abs(d - START_TARGET)), rec["id"], rec, ring, door, roof, d))
    candidates.sort(key=lambda c_: c_[:2])
    result = None
    for _score, _id, rec, ring, door, roof, d in candidates:
        walk = _roof_walk(gen, ring, roof, door)
        if walk is None:
            continue
        result = _assemble(gen, rec, door, roof, start, d, walk)
        if result is not None:
            break
    if result is not None:
        result["ornament"] = _ornament(gen, district)
        result["inside"] = plan_inside()
    _PLAN[key] = result
    return result


def _roof_walk(gen, ring, roof, door):
    """(x, y) of the leave_roof volume's middle: ROOF_WALK from the roof point toward the street door, else the first of
    the other three quarters that keeps ROOF_WALK_EDGE inside the roof; None when none does."""
    import _geo as geo  # noqa: PLC0415
    toward = (door[0] - roof[0], door[1] - roof[1])
    length = math.hypot(*toward) or 1.0
    u = (toward[0] / length, toward[1] / length)
    for d in (u, (-u[1], u[0]), (u[1], -u[0]), (-u[0], -u[1])):
        pt = (roof[0] + d[0] * ROOF_WALK, roof[1] + d[1] * ROOF_WALK)
        if geo.point_in_polygon(pt, ring) and gen._edge_distance(pt, ring) >= ROOF_WALK_EDGE:
            return pt
    return None


def _assemble(gen, rec, door, roof, start, start_distance, walk):
    z = gen.SIDEWALK_TOP
    yaw = door[2]
    out = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    along = (-out[1], out[0])

    def at(along_cm, out_cm):
        return (door[0] + along[0] * along_cm + out[0] * out_cm, door[1] + along[1] * along_cm + out[1] * out_cm)

    props = []
    for a, o in POSTS:
        x, y = at(a, o)
        props.append(("post", (x, y, z, 0.0)))
    for a, o, size in SNOW:
        x, y = at(a, o)
        props.append(("snow", (x, y, z, yaw + a, size)))
    sign = at(0.0, 12.0)
    props.append(("sign", (sign[0], sign[1], z + SIGN_HEIGHT, yaw)))
    garland = at(0.0, 70.0)
    props.append(("garland", (garland[0], garland[1], z + GARLAND_HEIGHT, yaw + 90.0, 320.0)))
    lights = []
    for a, rgb in ((-120.0, (1.0, 0.72, 0.42)), (0.0, (1.0, 0.25, 0.18)), (120.0, (0.35, 1.0, 0.4))):
        x, y = at(a, 80.0)
        lights.append(((x, y, z + GARLAND_HEIGHT - 20.0), rgb))
    doorstep = at(0.0, DOORSTEP_OUT)
    arrive = at(0.0, ARRIVE_OUT)
    roof_z = rec["height_m"] * 100.0
    tags = rec.get("tags", {})
    address = "{0} {1}".format(tags.get("addr:housenumber") or "", tags.get("addr:street") or "").strip()
    return {
        "osm": rec["id"],
        "address": address,
        "height_m": rec["height_m"],
        "door": (door[0], door[1], z),
        "yaw": yaw,
        "along": along,
        "doorstep": (doorstep[0], doorstep[1], z),
        "arrive": (arrive[0], arrive[1], z + ARRIVE_EXTENT[2] * 0.5),
        "arrive_extent": ARRIVE_EXTENT,
        "roof": (roof[0], roof[1], roof_z + ROOF_ABOVE),
        "roof_edge": roof[2],
        "roof_volume": (walk[0], walk[1], roof_z + ROOF_EXTENT[2] * 0.5),
        "roof_extent": ROOF_EXTENT,
        "props": props,
        "lights": lights,
        "player_start": (start[0], start[1], z),
        "player_yaw": _yaw(((door[0] - start[0]) / max(_dist2(door, start), 1.0), (door[1] - start[1]) / max(_dist2(door, start), 1.0))),
        "start_distance": start_distance,
    }


def _ornament(gen, district):
    """(x, y, z, yaw) of the ornament's stand beside Safehouse 1's door (Kate's building), facing the street."""
    spots = gen.safehouse_spots(district)
    if not spots:
        return None
    spot = spots[0][1]
    yaw = spot["yaw"]
    out = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    along = (-out[1], out[0])
    x = spot["x"] + along[0] * ORNAMENT_ALONG + out[0] * ORNAMENT_OUT
    y = spot["y"] + along[1] * ORNAMENT_ALONG + out[1] * ORNAMENT_OUT
    return (x, y, gen.SIDEWALK_TOP + ORNAMENT_HEIGHT, yaw)


# --------------------------------------------------------------------------------------------------
# the inside, in L_Int_Grotto's frame (Tools/Interiors/Grotto.json; +X east, +Y south, floor at z 0)
# --------------------------------------------------------------------------------------------------

# Room-relative places (cm from the room's x0, y0) of the dressing; kept clear of the elves' beats and of the throne's
# and sleighs' footprints by test_grotto.
THRONE = ("grotto", 700.0, 860.0, -90.0)          # facing north, at the entrance
QUEUE_POSTS = (("grotto", 600.0, 180.0), ("grotto", 800.0, 180.0), ("grotto", 600.0, 320.0), ("grotto", 800.0, 320.0))
GROTTO_SNOW = (("grotto", 520.0, 900.0, 1.2), ("grotto", 880.0, 900.0, 1.2), ("grotto", 150.0, 820.0, 1.0),
               ("grotto", 1250.0, 780.0, 1.0), ("grotto", 1000.0, 940.0, 0.8))
TOY_BOXES = (("stockroom", 520.0, 260.0), ("stockroom", 540.0, 560.0), ("stockroom", 300.0, 900.0))
SLEIGHS = (("workshop", 70.0, 180.0, 90.0), ("workshop", 70.0, 480.0, 90.0), ("workshop", 930.0, 180.0, -90.0),
           ("workshop", 930.0, 480.0, -90.0))
WATCH_TRAYS = (("workshop", 300.0, 640.0), ("workshop", 550.0, 640.0))
KEYPAD = ("corridor", 1330.0, 285.0, -90.0)       # on the corridor's side of the workshop wall, right of its door
ENTER_VOLUME = ("corridor", 1200.0, 150.0, (260.0, 140.0, 200.0))   # in front of the workshop door
SWITCH = ("grotto", 12.0, 700.0, 150.0, 0.0)      # the alarm box on the grotto's west wall, facing in
SWITCH_ID = "grotto_alarm"
SWITCH_NOISE = 1.3               # 15.6 m of hearing (HearingRange 1200 x 1.3): the corridor's guard round the archway, not
                                # the stockroom's two
TRIGGERS = (
    # (event, room, dx, dy, extent, only unseen)
    ("trigger:first_elf", "grotto", 700.0, 140.0, (320.0, 140.0, 200.0), False),
    ("trigger:overheard", "stockroom", 300.0, 220.0, (300.0, 220.0, 200.0), True),
    ("trigger:boomerang", "stockroom", 420.0, 260.0, (180.0, 200.0, 200.0), False),
)
SANTA_SPOT = ("workshop", 500.0, 470.0, -90.0)    # by the back wall, facing the door
ARENA_ROOM = "workshop"
ARENA_RADIUS = 300.0


def _room_point(layout, room_id, dx, dy):
    for room in layout["rooms"]:
        if room["id"] == room_id:
            return room["rect"][0] + dx, room["rect"][1] + dy
    raise KeyError(room_id)


def plan_inside(layout=None):
    """The inside's plan in the interior map's frame: dressing, triggers, the switch, the keypad, the volume, the arena."""
    layout = layout or load_layout()

    def pt(spec):
        return _room_point(layout, spec[0], spec[1], spec[2])

    props = []
    x, y = pt(THRONE)
    props.append(("throne", (x, y, 0.0, THRONE[3])))
    for spec in QUEUE_POSTS:
        x, y = pt(spec)
        props.append(("post", (x, y, 0.0, 0.0)))
    for spec in GROTTO_SNOW:
        x, y = pt(spec)
        props.append(("snow", (x, y, 0.0, 17.0 * spec[3], spec[3])))
    for spec in TOY_BOXES:
        x, y = pt(spec)
        props.append(("toys", (x, y, 0.0, 0.0)))
    for spec in SLEIGHS:
        x, y = pt(spec)
        props.append(("sleigh", (x, y, 0.0, spec[3])))
    for spec in WATCH_TRAYS:
        x, y = pt(spec)
        props.append(("watches", (x, y, 96.0, 0.0)))
    kx, ky = pt(KEYPAD)
    vx, vy = pt(ENTER_VOLUME)
    sx, sy = pt(SWITCH)
    triggers = []
    for event, room, dx, dy, extent, unseen in TRIGGERS:
        tx, ty = _room_point(layout, room, dx, dy)
        triggers.append((event, (tx, ty, extent[2] * 0.5), extent, unseen))
    arena = next(r for r in layout["rooms"] if r["id"] == ARENA_ROOM)
    x0, y0, x1, y1 = arena["rect"]
    bx, by = pt(SANTA_SPOT)
    return {
        "map": INTERIOR_MAP,
        "props": props,
        "lights": [((700.0, 700.0, 260.0), (1.0, 0.55, 0.35)), ((1200.0, 1650.0, 260.0), (1.0, 0.85, 0.6))],
        "keypad": (kx, ky, 0.0, KEYPAD[3]),
        "enter_volume": (vx, vy, ENTER_VOLUME[3][2] * 0.5),
        "enter_extent": ENTER_VOLUME[3],
        "switch": (sx, sy, SWITCH[3], SWITCH[4]),
        "switch_id": SWITCH_ID,
        "triggers": triggers,
        "arena": {
            "outline": [(x0, y0), (x1, y0), (x1, y1), (x0, y1)],
            "floor": 0.0,
            "centre": ((x0 + x1) * 0.5, (y0 + y1) * 0.5, 0.0),
            "radius": ARENA_RADIUS,
            "boss": (bx, by, 0.0),
            "boss_yaw": SANTA_SPOT[3],
            "room": ARENA_ROOM,
        },
    }


def describe(plan):
    if plan is None:
        return "no building fits the grotto"
    return "grotto door on {0} ({1}, {2:.0f} m) at ({3:.0f}, {4:.0f}) facing {5:.0f}, {6:.0f} m from the PlayerStart; roof " \
           "point ({7:.0f}, {8:.0f}, {9:.0f}), {10:.0f} cm in; {11} props outside, {12} inside".format(
               plan["address"] or "an unnamed building", plan["osm"], plan["height_m"], plan["door"][0], plan["door"][1], plan["yaw"],
               plan["start_distance"] / 100.0, plan["roof"][0], plan["roof"][1], plan["roof"][2], plan["roof_edge"],
               len(plan["props"]), len(plan["inside"]["props"]))
