"""Create the street crimes' assets and plan where they can start.

    /Game/Crimes/DA_Crime_<type>              UCrimeDefinition: mugging, robbery, ambush, rooftop
    /Game/Blueprints/AI/BP_Civilian           ACivilian: the mugging's victim, the old SK_Mannequin
    /Game/Characters/Civilian/M_Civilian      the victim's neutral grey (skeletal usage)

and ``plan_crime_spots(district)``, which generate_city.py places as City_CrimeSpot_<n> (ACrimeSpot):
fourteen to sixteen places a crime can start, planned from the same OpenStreetMap records the city is built from,
so nothing is placed by hand:

* eight on the street at block corners (create_challenges.street_corners: on the sidewalk 2.5 m out
  from a building's corner where two roads meet), nearest the PlayerStart first, each with an escape
  point on the sidewalk 60 m along one of its streets for the robbery;
* four on roofs 8 to 25 m tall with a grapple anchor, a spot 4.5 m clear of every edge and prop, and
  no chapter or challenge business on them;
* two to four in alleys: the middle of a 3 to 6 m gap between two footprints (a ray off each wall to
  the next building), 8 m past every carriageway's edge, 1.4 m from every wall, clear of the street
  clutter, and joined to a sidewalk by a walk never nearer than 1.4 m to a wall (a 50 cm grid search,
  at most 50 m; a rear yard with no way out but through a building is a dead end). A block with fewer
  than two such alleys gets two spots on the park's interior paths instead;
* none within 40 m of a safehouse door, a challenge pedestal or the interior entrance's doorstep, none
  within 25 m of chapter 1's thugs or archers, and every two at least 35 m apart.

Street corners take the mugging, robbery and ambush; alleys the mugging and ambush; rooftops the
rooftop crime. The names are
bracketed placeholders ("[Crime: mugging]"); civilians have no lines, only "[thank you]".

Idempotent: an asset is rewritten only when its planned values differ from what it holds.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\create_crimes.py" -unattended -nullrhi -nosplash -nop4 -stdout
"""

import math
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _geo as geo  # noqa: E402
import _materials as m  # noqa: E402
import create_blueprints as cb  # noqa: E402
from _geomutil import _segment_distance  # noqa: E402 - was a copy of the same formula here

CRIME_PATH = "/Game/Crimes"
ASSET_PREFIX = "DA_Crime_"
AI_PATH = "/Game/Blueprints/AI"
BOSS_PATH = "/Game/Blueprints/Bosses"
CIVILIAN_BP = "BP_Civilian"
CIVILIAN_MATERIAL_PATH = "/Game/Characters/Civilian"
M_CIVILIAN = CIVILIAN_MATERIAL_PATH + "/M_Civilian"
CIVILIAN_GREY = (0.32, 0.32, 0.31)
MANNEQUIN_MESH_PATH = "/Game/Mannequin/Character/Mesh/SK_Mannequin"
MANNEQUIN_IDLE_PATH = "/Game/Mannequin/Animations/ThirdPersonIdle"
MANNEQUIN_RUN_PATH = "/Game/Mannequin/Animations/ThirdPersonRun"
MESH_LOCATION = unreal.Vector(0.0, 0.0, -96.0)
MESH_ROTATION = unreal.Rotator(0.0, 0.0, -90.0)

# --- the spots --------------------------------------------------------------------------------------
STREET_SPOTS = 8
ROOF_SPOTS = 4
SEARCH_RADIUS = 25000.0         # cm from the PlayerStart: the block and its streets (one block at a time)
SAFEHOUSE_CLEAR = 4000.0        # cm from the safehouse door
PEDESTAL_CLEAR = 4000.0         # cm from any challenge pedestal
CHAPTER_THUG_CLEAR = 2500.0     # cm from chapter 1's thugs and archers
SPOT_APART = 3500.0             # cm between two spots
ROOF_HEIGHT = (8.0, 25.0)       # m
ROOF_EDGE_CLEAR = 450.0         # cm from any roof edge to a rooftop spot (the roster stands 3.5 m round it)
ESCAPE_DISTANCE = 6000.0        # cm from a street spot to the robbery's escape point
ESCAPE_TOLERANCE = 600.0        # cm either way the escape point may land from ESCAPE_DISTANCE
ESCAPE_STEP = 200.0             # cm between escape candidates along a street
INTERIOR_CLEAR = 4000.0         # cm from the interior entrance's doorstep

# Alleys: a passage 3 to 6 m wide between two footprints, off the street, that a 3 m wide walk joins to a
# sidewalk. The mugging and the ambush may start there.
ALLEY_SPOTS = (2, 4)            # at least, at most
ALLEY_WIDTH = (300.0, 600.0)    # cm between the two walls, across the passage
ALLEY_STEP = 100.0              # cm between the rays cast off each wall
ALLEY_END_INSET = 100.0         # cm from a wall's ends the rays start
ALLEY_CLEAR = 140.0             # cm the spot and every step of the walk out keep from any wall (half 3 m, less a margin)
ALLEY_KERB = 800.0              # cm past every carriageway's edge: out of the street and its 4 m sidewalk
ALLEY_SIDEWALK = 400.0          # cm from a carriageway's edge that counts as the sidewalk the walk reaches
ALLEY_PROP_CLEAR = 150.0        # cm from a bin, a car or a scaffold
ALLEY_CELL = 50.0               # cm; the walk out is searched on a grid this fine
ALLEY_WALK_CELLS = 12000        # cells searched before a passage is called a dead end
ALLEY_WALK_MAX = 5000.0         # cm of grid walk (4-way, so a little over the real one) out to the sidewalk, at most
PARK_PATH_SPOTS = 2             # the fallback when the block has no usable alley
PARK_PATH_INSET = 1500.0        # cm inside the park's edge a park-path spot stands

# --- the crimes -------------------------------------------------------------------------------------
# (id, type, roster [(weapon, count, blueprint)], radius, time to fail, alert on start)
CRIMES = [
    ("mugging", "Mugging", [("Fists", 1, "BP_Thug"), ("Bat", 1, "BP_Thug")], 160.0, 45.0, False),
    ("robbery", "Robbery", [("Fists", 1, "BP_Thug"), ("Bat", 1, "BP_Thug"), ("Pistol", 1, "BP_Thug")], 250.0, 40.0, False),
    ("ambush", "Ambush", [("Fists", 1, "BP_Thug"), ("Bat", 2, "BP_Thug"), ("Pistol", 1, "BP_Thug")], 500.0, 45.0, True),
    ("rooftop", "Rooftop", [("Fists", 1, "BP_Thug"), ("Bat", 1, "BP_Thug"), ("Bow", 1, "BP_Archer")], 350.0, 45.0, False),
]
STREET_CRIMES = ("mugging", "robbery", "ambush")
ALLEY_CRIMES = ("mugging", "ambush")
ROOF_CRIMES = ("rooftop",)
VICTIM_HITS = 3
# cm the roster may relocate from its spot (the archer's zip); a roof spot also keeps him on its roof. 0: free.
LEASH_RADIUS = {"rooftop": 2500.0}
REWARD_ARROWS = 5


def _enum(enum_name, value):
    """unreal.CrimeType.MUGGING from ("CrimeType", "Mugging")."""
    enum = getattr(unreal, enum_name)
    return getattr(enum, re.sub(r"(?<!^)(?=[A-Z])", "_", value).upper())


def _path(value):
    """Package path of an object, class or soft reference ("" for none), so reads compare with writes."""
    if value is None:
        return ""
    for getter in ("get_path_name", "export_text"):
        method = getattr(value, getter, None)
        if method is not None:
            try:
                return str(method()).split(".")[0]
            except Exception:  # noqa: BLE001
                pass
    return str(value).split(".")[0]


# --------------------------------------------------------------------------------------------------
# the spots
# --------------------------------------------------------------------------------------------------


def _dist2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _blockers(district, gen, cc):
    """[(x, y, clearance)] a spot must keep away from: the safehouses, every pedestal, chapter 1's fighters."""
    out = []
    for _label, safehouse in gen.safehouse_spots(district):
        out.append((safehouse["x"], safehouse["y"], SAFEHOUSE_CLEAR))
    for plan in cc.plan_challenges(district):
        out.append((plan["start"][0], plan["start"][1], PEDESTAL_CLEAR))
    thugs, _points, _roof = gen.thug_placements(district)
    for t in thugs:
        out.append((t[1], t[2], CHAPTER_THUG_CLEAR))
    for a in gen.archer_placements(district):
        out.append((a[1], a[2], CHAPTER_THUG_CLEAR))
    door = gen.interior_keepout(district)
    if door is not None:
        out.append((door[0], door[1], INTERIOR_CLEAR))
    return out


def _clear(pt, blockers, chosen):
    return all(_dist2(pt, (x, y)) >= r for x, y, r in blockers) and all(_dist2(pt, s["at"]) >= SPOT_APART for s in chosen)


def escape_point(world, cc, spot):
    """A sidewalk point about ESCAPE_DISTANCE from spot along one of the streets it is on, or None."""
    best = None
    for a, b, _half, _name in world.roads:
        if cc._segment_point_distance(spot, a, b)[0] > cc.CORNER_ROADS:
            continue
        ux, uy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(ux, uy)
        if length < 1.0:
            continue
        ux, uy = ux / length, uy / length
        for sign in (1.0, -1.0):
            offset = -ESCAPE_TOLERANCE
            while offset <= ESCAPE_TOLERANCE:
                reach = ESCAPE_DISTANCE + offset
                offset += ESCAPE_STEP
                pt = (spot[0] + ux * sign * reach, spot[1] + uy * sign * reach)
                if not cc.on_sidewalk(world, pt) or not cc.street_clear(world, pt, 150.0):
                    continue
                miss = abs(_dist2(pt, spot) - ESCAPE_DISTANCE)
                if best is None or miss < best[0]:
                    best = (miss, pt)
    return best[1] if best is not None else None


def _street_spots(world, gen, cc, blockers):
    chosen = []
    for spot, top, out in cc.street_corners(world):
        if len(chosen) >= STREET_SPOTS:
            break
        if gen.ring_distance(world.start, top[1]) > SEARCH_RADIUS or not _clear(spot, blockers, chosen):
            continue
        escape = escape_point(world, cc, spot)
        if escape is None:
            continue
        # Facing in toward the corner's storefronts: where a robbery happens.
        yaw = math.degrees(math.atan2(-out[1], -out[0]))
        chosen.append({"at": spot, "z": gen.SIDEWALK_TOP, "yaw": yaw, "rooftop": False, "osm": top[0]["id"],
                       "escape": (escape[0], escape[1], gen.SIDEWALK_TOP), "kind": "corner"})
    return chosen


def _roof_spots(world, gen, cc, blockers, taken):
    anchored = {a[6] for a in world.anchors}
    tops = sorted(world.tops, key=lambda t: (gen.ring_distance(world.start, t[1]), t[0]["id"]))
    chosen = []
    for top in tops:
        if len(chosen) >= ROOF_SPOTS:
            break
        rec = top[0]
        if gen.ring_distance(world.start, top[1]) > SEARCH_RADIUS or rec["id"] in taken or rec["id"] not in anchored \
                or not ROOF_HEIGHT[0] <= rec["height_m"] <= ROOF_HEIGHT[1]:
            continue
        spots, _props = cc.roof_spots(world, top, inset=ROOF_EDGE_CLEAR)
        centre = geo.centroid(top[1])
        spots = [s for s in spots if _clear(s, blockers, chosen)]
        if not spots:
            continue
        spot = min(spots, key=lambda s: (_dist2(s, centre), s))
        yaw = math.degrees(math.atan2(world.start[1] - spot[1], world.start[0] - spot[0]))
        chosen.append({"at": spot, "z": world.roof_z(top), "yaw": yaw, "rooftop": True, "osm": rec["id"],
                       "escape": None, "kind": "roof"})
    return chosen


def _ray_hit(origin, direction, reach, tops):
    """Distance along direction from origin to the first footprint edge of tops it crosses, from 1 cm to reach,
    or None."""
    ox, oy = origin
    dx, dy = direction
    best = None
    for top in tops:
        ring = top[1]
        n = len(ring)
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            ex, ey = bx - ax, by - ay
            den = dx * ey - dy * ex
            if abs(den) < 1e-9:
                continue
            s = ((ax - ox) * ey - (ay - oy) * ex) / den
            t = ((ax - ox) * dy - (ay - oy) * dx) / den
            if 0.0 <= t <= 1.0 and 1.0 < s <= reach and (best is None or s < best):
                best = s
    return best


def _carriageway_distance(world, pt):
    """cm from pt to the nearest carriageway's edge (negative inside one)."""
    return min((_segment_distance(pt, a, b) - half for a, b, half, _name in world.roads), default=1.0e9)


def _wall_clearance(gen, pt, tops):
    """cm from pt to the nearest footprint of tops (0 inside one)."""
    return min((gen.ring_distance(pt, t[1]) for t in tops), default=1.0e9)


def _in_park(district, pt):
    return any(geo.point_in_polygon(pt, district.ring_cm(p["outer"])) for p in district.parks)


def _walk_out(world, gen, spot):
    """Cells walked on an ALLEY_CELL grid from spot to the nearest sidewalk, never nearer than ALLEY_CLEAR to a
    wall, or None when the passage is a dead end (a rear yard with no way out but through a building)."""
    tops = world.tops_near(spot[0], spot[1], ALLEY_CELL * 200.0)
    walkable = {}

    def ok(cell):
        if cell not in walkable:
            pt = (spot[0] + cell[0] * ALLEY_CELL, spot[1] + cell[1] * ALLEY_CELL)
            near = [t for t in tops if not (t[2][0] > pt[0] + 300.0 or t[2][2] < pt[0] - 300.0
                                            or t[2][1] > pt[1] + 300.0 or t[2][3] < pt[1] - 300.0)]
            walkable[cell] = _wall_clearance(gen, pt, near) >= ALLEY_CLEAR
        return walkable[cell]

    frontier = [(0, 0)]
    steps = {(0, 0): 0}
    head = 0
    while head < len(frontier) and len(steps) < ALLEY_WALK_CELLS:
        cell = frontier[head]
        head += 1
        pt = (spot[0] + cell[0] * ALLEY_CELL, spot[1] + cell[1] * ALLEY_CELL)
        if _carriageway_distance(world, pt) <= ALLEY_SIDEWALK:
            return steps[cell]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nxt = (cell[0] + dx, cell[1] + dy)
            if nxt not in steps and ok(nxt):
                steps[nxt] = steps[cell] + 1
                frontier.append(nxt)
    return None


def alley_candidates(world, gen, district):
    """[(start distance, (x, y), yaw along the passage, width, osm a, osm b)]: the middles of every 3 to 6 m gap
    between two footprints within SEARCH_RADIUS, off the street and the park, clear of the walls and the street
    clutter. Nearest the PlayerStart first. Whether it has a way out is checked later (the slow part)."""
    out = []
    reach = ALLEY_WIDTH[1] + 50.0
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
            if length < 2.0 * ALLEY_END_INSET + ALLEY_STEP:
                continue
            ux, uy = (bx - ax) / length, (by - ay) / length
            nx, ny = (uy, -ux) if ccw else (-uy, ux)
            t = ALLEY_END_INSET
            while t <= length - ALLEY_END_INSET:
                p = (ax + ux * t, ay + uy * t)
                t += ALLEY_STEP
                others = [o for o in world.tops_near(p[0], p[1], reach) if o[0]["id"] != rec["id"]]
                if not others:
                    continue
                gap = _ray_hit(p, (nx, ny), reach, others)
                if gap is None or not ALLEY_WIDTH[0] <= gap <= ALLEY_WIDTH[1]:
                    continue
                if _ray_hit(p, (nx, ny), gap - 1.0, [top]) is not None:
                    continue    # its own outline turns back first: a notch in one building, not a passage
                mid = (p[0] + nx * gap * 0.5, p[1] + ny * gap * 0.5)
                if world.building_at(mid) is not None or _in_park(district, mid):
                    continue
                if _carriageway_distance(world, mid) < ALLEY_KERB:
                    continue
                if _wall_clearance(gen, mid, world.tops_near(mid[0], mid[1], 800.0)) < ALLEY_CLEAR:
                    continue
                if any(z0 < 400.0 and math.hypot(mid[0] - x, mid[1] - y) < r + ALLEY_PROP_CLEAR
                       for x, y, z0, _z1, r, _k in world.props_near(mid[0], mid[1], 1000.0)):
                    continue
                other = world.building_at((p[0] + nx * (gap + 20.0), p[1] + ny * (gap + 20.0)))
                yaw = math.degrees(math.atan2(uy, ux))
                out.append((_dist2(world.start, mid), mid, yaw, gap, rec["id"], other[0]["id"] if other else "?"))
    out.sort(key=lambda e: (e[0], e[1]))
    return out


def _alley_spots(world, gen, district, blockers, taken):
    """Up to ALLEY_SPOTS[1] alley spots, nearest the PlayerStart first, each ALLEY_CLEAR from the walls all the way
    out to a sidewalk, 35 m from every other spot and outside every keep-out; and the counts behind them."""
    chosen = []
    candidates = alley_candidates(world, gen, district)
    stats = {"gaps": len(candidates), "kept_out": 0, "tried": 0, "dead_ends": 0, "too_deep": 0}
    passages = []
    for _d, mid, _yaw, _w, _a, _b in candidates:
        if not any(_dist2(mid, q) < 500.0 for q in passages):
            passages.append(mid)
    stats["passages"] = len(passages)
    tried = []
    for _d, mid, yaw, width, osm_a, osm_b in candidates:
        if len(chosen) >= ALLEY_SPOTS[1]:
            break
        if not _clear(mid, blockers, taken + chosen):
            stats["kept_out"] += 1
            continue
        if any(_dist2(mid, t) < 500.0 for t in tried):
            continue    # the same passage a metre along: its way out is already known
        tried.append(mid)
        stats["tried"] += 1
        walk = _walk_out(world, gen, mid)
        if walk is None or walk * ALLEY_CELL > ALLEY_WALK_MAX:
            stats["dead_ends" if walk is None else "too_deep"] += 1
            continue
        chosen.append({"at": mid, "z": 0.0, "yaw": yaw, "rooftop": False, "osm": osm_a, "escape": None,
                       "kind": "alley", "width": width, "between": (osm_a, osm_b), "walk": walk * ALLEY_CELL})
    return chosen, stats


def _park_path_spots(world, gen, cc, district, blockers, taken, count):
    """count spots inside the park, PARK_PATH_INSET from its edge on a 5 m grid, nearest the PlayerStart first:
    the fallback for a block with no alley."""
    chosen = []
    for park in district.parks:
        ring = district.ring_cm(park["outer"])
        x0, y0, x1, y1 = geo.bounds(ring)
        points = []
        gx = x0
        while gx <= x1:
            gy = y0
            while gy <= y1:
                pt = (gx, gy)
                if geo.point_in_polygon(pt, ring) and gen.closest_point_on_polyline(pt, list(ring) + [ring[0]])[0] >= PARK_PATH_INSET:
                    points.append((_dist2(world.start, pt), pt))
                gy += 500.0
            gx += 500.0
        for _d, pt in sorted(points):
            if len(chosen) >= count:
                break
            if _clear(pt, blockers, taken + chosen) and cc.street_clear(world, pt, 150.0):
                chosen.append({"at": pt, "z": gen.PARK_TOP, "yaw": 0.0, "rooftop": False, "osm": park["id"],
                               "escape": None, "kind": "park"})
    return chosen


_PLAN_CACHE = {}
_ALLEY_STATS = {}


def alley_stats(district):
    """{'gaps', 'passages', 'kept_out', 'tried', 'dead_ends', 'too_deep', 'alleys'}: how the alley search went."""
    plan_crime_spots(district)
    return _ALLEY_STATS.get(id(district), {})


def plan_crime_spots(district):
    """The twelve street and roof spots, then two to four alley (or park path) spots, each {'index', 'at', 'z', 'yaw', 'rooftop', 'osm', 'escape',
    'kind', 'crimes'}. Cached per district object."""
    key = id(district)
    if key in _PLAN_CACHE:
        return _PLAN_CACHE[key]
    import generate_city as gen  # noqa: PLC0415 - generate_city imports this lazily in turn
    import create_challenges as cc  # noqa: PLC0415
    world = cc.planning_world(district)
    blockers = _blockers(district, gen, cc)
    taken = set(world.taken)
    for plan in cc.plan_challenges(district):
        if plan.get("osm"):
            taken.add(plan["osm"])
        taken |= set(plan.get("roofs", []))
    street = _street_spots(world, gen, cc, blockers)
    roofs = _roof_spots(world, gen, cc, blockers + [(s["at"][0], s["at"][1], SPOT_APART) for s in street], taken)
    alleys, stats = _alley_spots(world, gen, district, blockers, street + roofs)
    stats["alleys"] = len(alleys)
    if len(alleys) < ALLEY_SPOTS[0]:
        # East Village tenements often abut: without enough alleys the park's paths stand in.
        alleys += _park_path_spots(world, gen, cc, district, blockers, street + roofs + alleys, PARK_PATH_SPOTS)
    _ALLEY_STATS[key] = stats
    spots = street + roofs + alleys
    for index, spot in enumerate(spots):
        spot["index"] = index
        spot["crimes"] = list(ROOF_CRIMES if spot["rooftop"] else (STREET_CRIMES if spot["kind"] == "corner" else ALLEY_CRIMES))
    _PLAN_CACHE[key] = spots
    return spots


def describe(spot):
    extra = ""
    if spot["kind"] == "alley":
        extra = ", {0:.1f} m wide between {1} and {2}, {3:.0f} m walk to the sidewalk".format(
            spot["width"] / 100.0, spot["between"][0], spot["between"][1], spot["walk"] / 100.0)
    elif spot["kind"] == "park":
        extra = ", on the park's paths"
    if spot["escape"] is not None:
        extra = ", escape {0:.0f} m to ({1:.0f}, {2:.0f})".format(_dist2(spot["at"], spot["escape"]) / 100.0, spot["escape"][0], spot["escape"][1])
    return "spot {0} {1} on osm {2} at ({3:.0f}, {4:.0f}, {5:.0f}) yaw {6:.0f}: {7}{8}".format(
        spot["index"], spot["kind"], spot["osm"], spot["at"][0], spot["at"][1], spot["z"], spot["yaw"], "/".join(spot["crimes"]), extra)


# --------------------------------------------------------------------------------------------------
# the victim
# --------------------------------------------------------------------------------------------------


def ensure_civilian_material():
    c.ensure_directory(CIVILIAN_MATERIAL_PATH)
    return m.ensure_material(M_CIVILIAN, m._build_flat(CIVILIAN_GREY, 0.75), skeletal=True)


def _mesh_component(cdo):
    try:
        return cdo.get_editor_property("mesh")
    except Exception:  # noqa: BLE001
        return None


def _configure_civilian_mesh(bp, material):
    cdo = c.blueprint_cdo(bp)
    component = _mesh_component(cdo) if cdo is not None else None
    mesh_asset = c.load_or_none(MANNEQUIN_MESH_PATH)
    if component is None or mesh_asset is None:
        c.log("skipped", CIVILIAN_BP + ".Mesh", "no mesh component or no SK_Mannequin")
        return False
    changed = []
    if component.get_editor_property("skeletal_mesh_asset") != mesh_asset:
        component.set_skeletal_mesh_asset(mesh_asset)
        changed.append("skeletal_mesh_asset")
    for prop, value in (("relative_location", MESH_LOCATION), ("relative_rotation", MESH_ROTATION),
                        ("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)):
        if component.get_editor_property(prop) != value and c.set_props(component, [(prop, value)], CIVILIAN_BP + ".Mesh"):
            changed.append(prop)
    for slot in (0, 1):
        try:
            if component.get_material(slot) != material:
                component.set_material(slot, material)
                changed.append("material {0}".format(slot))
        except Exception as exc:  # noqa: BLE001
            c.log_error(CIVILIAN_BP + ".Mesh material", exc)
    if changed:
        c.log("updated", CIVILIAN_BP + ".Mesh", ", ".join(changed))
    return bool(changed)


def ensure_civilian():
    """BP_Civilian: ACivilian on the old mannequin in M_Civilian, idle and run on the single-node slot."""
    parent = c.find_class("Civilian", "/Script/Hawkeye.Civilian")
    if parent is None:
        c.log("FAILED", c.asset_path(AI_PATH, CIVILIAN_BP), "ACivilian not exposed; build the module")
        return None
    bp, _created = cb.make_blueprint(CIVILIAN_BP, AI_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    material = ensure_civilian_material()
    changed = bool(cb.apply_defaults(bp, CIVILIAN_BP, AI_PATH, [
        ("idle_anim", c.load_or_none(MANNEQUIN_IDLE_PATH)),
        ("run_anim", c.load_or_none(MANNEQUIN_RUN_PATH)),
    ]))
    if _configure_civilian_mesh(bp, material):
        changed = True
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
    else:
        c.log("exists", c.asset_path(AI_PATH, CIVILIAN_BP), "mannequin, M_Civilian, idle and run set")
    return c.load_generated_class(AI_PATH, CIVILIAN_BP)


# --------------------------------------------------------------------------------------------------
# the definitions
# --------------------------------------------------------------------------------------------------


def _thug_class(name):
    return c.load_generated_class(BOSS_PATH if name == "BP_Archer" else AI_PATH, name)


def _roster(entries):
    out = []
    for weapon, count, blueprint in entries:
        entry = unreal.CrimeRosterEntry()
        entry.set_editor_property("weapon", _enum("ThugWeapon", weapon))
        entry.set_editor_property("count", count)
        entry.set_editor_property("thug_class", _thug_class(blueprint))
        out.append(entry)
    return out


def _roster_key(entries):
    return [(str(e.get_editor_property("weapon")), int(e.get_editor_property("count")), _path(e.get_editor_property("thug_class")))
            for e in entries or []]


def _fields(crime, civilian_class):
    cid, ctype, roster, radius, time_to_fail, alert = crime
    return [
        ("id", unreal.Name(cid)),
        ("type", _enum("CrimeType", ctype)),
        ("name", unreal.Text("[Crime: {0}]".format(cid))),
        ("roster", _roster(roster)),
        ("radius", radius),
        ("time_to_fail_seconds", time_to_fail),
        ("victim_hits_to_fail", VICTIM_HITS),
        ("victim_class", civilian_class if ctype == "Mugging" else None),
        ("escape_distance", ESCAPE_DISTANCE),
        ("alert_on_start", alert),
        ("leash_radius", LEASH_RADIUS.get(cid, 0.0)),
        ("reward", _enum("CrimeReward", "ArrowRefill")),
        ("reward_arrows", REWARD_ARROWS),
    ]


def _same(prop, current, wanted):
    if prop == "roster":
        return _roster_key(current) == _roster_key(wanted)
    if prop == "victim_class":
        return _path(current) == _path(wanted)
    if isinstance(wanted, float):
        return abs(float(current) - wanted) < 1e-3
    if isinstance(wanted, unreal.Text):
        return str(current) == str(wanted)
    return current == wanted


def ensure_crime(crime, civilian_class):
    cls = c.find_class("CrimeDefinition", "/Script/Hawkeye.CrimeDefinition")
    if cls is None:
        c.log("FAILED", CRIME_PATH, "UCrimeDefinition not exposed to Python; build the module")
        return None
    name = ASSET_PREFIX + crime[0]
    full = c.asset_path(CRIME_PATH, name)
    factory = c.new_factory("DataAssetFactory")
    c.set_props(factory, [("data_asset_class", cls)], "DataAssetFactory")
    asset, created = c.create_asset(name, CRIME_PATH, cls, factory, quiet=True)
    if asset is None:
        return None
    changed = []
    for prop, value in _fields(crime, civilian_class):
        try:
            current = asset.get_editor_property(prop)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(full, prop), exc)
            continue
        if not _same(prop, current, value):
            asset.set_editor_property(prop, value)
            changed.append(prop)
    if created or changed:
        c.save(asset)
    thugs = sum(n for _w, n, _b in crime[2])
    c.log("created" if created else ("updated" if changed else "exists"), full,
          "{0} thugs{1}".format(thugs, " (" + ", ".join(changed) + ")" if changed and not created else ""))
    return asset


def crime_definitions():
    """{id: UCrimeDefinition} for every DA_Crime_ asset, by id."""
    out = {}
    if not unreal.EditorAssetLibrary.does_directory_exist(CRIME_PATH):
        return out
    for path in sorted(unreal.EditorAssetLibrary.list_assets(CRIME_PATH, recursive=False)):
        package = path.split(".")[0]
        if not package.split("/")[-1].startswith(ASSET_PREFIX):
            continue
        asset = c.load_or_none(package)
        if asset is not None:
            out[str(asset.get_editor_property("id"))] = asset
    return out


def run():
    c.ensure_directory(CRIME_PATH)
    civilian = ensure_civilian()
    assets = [ensure_crime(crime, civilian) for crime in CRIMES]
    return [a for a in assets if a is not None]


if __name__ == "__main__":
    run()
    c.print_summary("crimes")
