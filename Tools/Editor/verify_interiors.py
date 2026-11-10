"""Check every interior map (L_Int_<Name>) against its layout in Tools/Interiors/, and the district's door into
it. Read-only. Prints one line per check and a final ``[Hawkeye] verify_interiors PASS`` or ``FAIL``:

* the layout parses and keeps its rules (_interior.py; the same rules Tools/test-interior.ps1 tests)
* the map uses BP_GameMode_Interior: no snow, the indoor camera arm, a navmesh built at start
* every actor the generator placed is labelled Int_<Room>_<Kind>_<n> and tagged Interior
* the shell: every room has a floor under its middle in its own surface (SurfaceWood or SurfaceCarpet tags)
  and a ceiling over it, and a trace from its middle outward on every side meets a wall or a rail
* every door, archway and exit is open where the layout says (a trace through the doorway at waist height
  meets nothing but the door's own leaf); every window has its glass
* doors: one ADoorActor per door with a leaf, locked ones to their keycard, whose pickup is in the map;
  every leaf leaves the navmesh alone and every opening under 140 cm has a nav link
* exits: an AInteriorExit per exit, each knowing the district to fall back on and its doorstep; the entrance's
  PlayerStart 150 cm inside it
* the stair: 2 x n steps and treads, no riser over 18 cm, the landings at half and full height
* every room with lights has at least one CityLamp light over it, every light movable
* every enemy's thug exists on his type's Blueprint with the layout's weapon, patrol points, alert group and
  carried keycards; he starts on the navmesh, every patrol point is on a path from him, and no capsule of
  his (at the start or any patrol point) overlaps a wall, a prop or a door
* every narrow door knows its nav link (a locked door keeps it out of the navmesh while locked)
* the grapple anchors sit on the rail top with their landing point on the mezzanine floor, and a traversable
  ledge runs along every rail
* the navmesh (built here, not saved) has a path from the PlayerStart to every room on every floor
* the district: City_InteriorEntrance_Sample opens the interior, its doorstep and roof return points are
  tagged with their labels, the roof point stands on its building's roof

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript -script="Tools\\Editor\\verify_interiors.py"
"""

import glob
import math
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _interior as it  # noqa: E402
import generate_city as gen  # noqa: E402
import generate_interior as gi  # noqa: E402

LABEL_RE = re.compile(r"^Int_[A-Za-z]+_[A-Za-z]+_\d+$")
NAV_EXTENT = unreal.Vector(50.0, 50.0, 150.0)

_failures = []


def check(ok, what, detail=""):
    line = "[Hawkeye] {0}  {1}{2}".format("PASS" if ok else "FAIL", what, "  (" + detail + ")" if detail else "")
    if ok:
        unreal.log(line)
    else:
        unreal.log_error(line)
        _failures.append(what)
    return ok


def tags_of(actor):
    return [str(t) for t in actor.get_editor_property("tags")]


def trace(world, start, end, ignore=None):
    """(hit actor or None, hit z) of a visibility line trace in the editor world."""
    channel = getattr(unreal.TraceTypeQuery, "ECC_VISIBILITY", None) or unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(*start), unreal.Vector(*end), channel, False,
                                                 ignore or [], unreal.DrawDebugTrace.NONE, True)
    if hit is None or not hasattr(hit, "to_tuple"):
        return None, None
    parts = hit.to_tuple()
    if not parts[0]:
        return None, None
    actor = next((p for p in parts if isinstance(p, unreal.Actor)), None)
    point = parts[5] if isinstance(parts[5], unreal.Vector) else None
    return actor, (point.z if point is not None else None)


def project(world, point):
    result = unreal.HawkeyeNavigationLibrary.project_to_navigation(world, point, NAV_EXTENT)
    if isinstance(result, tuple):
        return bool(result[0]), result[1]
    return result is not None, result


def check_game_mode(layout):
    ws = c.world_settings()
    gm = ws.get_editor_property("default_game_mode") if ws is not None else None
    cdo = unreal.get_default_object(gm) if gm is not None else None
    ok = cdo is not None and not cdo.get_editor_property("outdoor_weather") and cdo.get_editor_property("interior_camera") \
        and cdo.get_editor_property("build_navigation_at_start")
    check(ok and gi.GAME_MODE in c.class_name(gm), layout.name + ": BP_GameMode_Interior with no snow, the indoor arm and a navmesh",
          c.class_name(gm) if gm is not None else "none")


def check_labels(layout, actors):
    ours = {l: a for l, a in actors.items() if l.startswith("Int_")}
    bad = [l for l in ours if not LABEL_RE.match(l)]
    untagged = [l for l, a in ours.items() if gi.TAG not in tags_of(a)]
    check(not bad, layout.name + ": every generated actor is labelled Int_<Room>_<Kind>_<n>", ", ".join(bad[:6]))
    check(not untagged, layout.name + ": every generated actor is tagged Interior", ", ".join(untagged[:6]))
    unreal.log("[Hawkeye] info  {0}: {1} Int_ actors".format(layout.name, len(ours)))
    return ours


def room_samples(room, n=4):
    """Points over a room's floor on an n x n grid, clear of its walls: a prop or a stair can stand on some."""
    return [(room.x0 + (room.x1 - room.x0) * (i + 0.5) / n, room.y0 + (room.y1 - room.y0) * (j + 0.5) / n)
            for j in range(n) for i in range(n)]


def check_rooms(layout, world):
    floors, closed = [], []
    for room in layout.rooms:
        want = gi.surface_tags(room.surface)
        on_floor = None
        for px, py in room_samples(room):
            if layout.owner_at(room.floor, px, py) != room.id:
                continue
            floor_actor, floor_z = trace(world, (px, py, room.z0 + 150.0), (px, py, room.z0 - 50.0))
            tags = tags_of(floor_actor) if floor_actor is not None else []
            if floor_actor is not None and abs(floor_z - room.z0) <= 1.0 and all(t in tags for t in want)                     and "_Floor_" in floor_actor.get_actor_label():
                on_floor = (px, py)
                break
        if on_floor is None:
            floors.append(room.id + " has no floor of its own surface anywhere on a 4 x 4 grid")
            continue
        up, _z = trace(world, (on_floor[0], on_floor[1], room.z0 + 150.0), (on_floor[0], on_floor[1], room.z1 + 60.0))
        if up is None:
            floors.append(room.id + " has no ceiling")
        z = room.z0 + 150.0 if not room.overlooks else room.z0 + 50.0
        cx, cy = (room.x0 + room.x1) * 0.5, (room.y0 + room.y1) * 0.5
        for side in it.SIDES:
            axis, coord, lo, hi = room.side_line(side)
            blocked = 0
            for k in range(5):
                s = lo + (hi - lo) * (k + 0.5) / 5.0
                end = (coord, s, z) if axis == "x" else (s, coord, z)
                start = (cx, s, z) if axis == "x" else (s, cy, z)
                beyond = (end[0] + (80.0 if side == "E" else -80.0 if side == "W" else 0.0),
                          end[1] + (80.0 if side == "S" else -80.0 if side == "N" else 0.0), z)
                hit, _hz = trace(world, start, beyond)
                blocked += 1 if hit is not None else 0
            # Doors, archways and windows leave gaps; a side with no wall at all leaves nothing.
            if blocked < 3:
                closed.append("{0} {1} ({2} of 5 blocked)".format(room.id, side, blocked))
    check(not floors, layout.name + ": every room has its floor, in its surface, and a ceiling", "; ".join(floors))
    check(not closed, layout.name + ": every room is closed on every side (a wall, a rail or a prop)", ", ".join(closed))


def check_openings(layout, world, actors):
    blocked, glass = [], []
    doors = [a for a in actors.values() if isinstance(a, unreal.DoorActor)]
    exits = [a for a in actors.values() if isinstance(a, unreal.InteriorExit)]
    for item, bd, at, _width in layout.door_spots() + layout.exit_spots():
        x, y = bd.point(at)
        z = bd.band * it.FLOOR_HEIGHT + 110.0
        a = (x - 60.0, y, z) if bd.axis == "x" else (x, y - 60.0, z)
        b = (x + 60.0, y, z) if bd.axis == "x" else (x, y + 60.0, z)
        hit, _z = trace(world, a, b)
        if hit is not None and hit not in doors and hit not in exits:
            blocked.append("{0} by {1}".format(item.get("id"), hit.get_actor_label()))
    check(not blocked, layout.name + ": every doorway and exit is open through the wall", "; ".join(blocked))
    for item, bd, at, _width in layout.window_spots():
        x, y = bd.point(at)
        z = bd.band * it.FLOOR_HEIGHT + float(item.get("sill", 90.0)) + float(item.get("height", 150.0)) * 0.5
        hit, _z = trace(world, (x - (30.0 if bd.axis == "x" else 0.0), y - (30.0 if bd.axis == "y" else 0.0), z),
                        (x + (30.0 if bd.axis == "x" else 0.0), y + (30.0 if bd.axis == "y" else 0.0), z))
        label = hit.get_actor_label() if hit is not None else ""
        if "_Glass_" not in label and "_Mullion_" not in label:
            glass.append("{0} {1} at {2:.0f} ({3})".format(item["room"], item["side"], at, label or "open"))
    check(not glass, layout.name + ": every window is glazed", "; ".join(glass))

    leaves = [d for d in layout.doors if d.get("leaf", "wood") != "none"]
    check(len(doors) == len(leaves), layout.name + ": one ADoorActor per door with a leaf", "{0} of {1}".format(len(doors), len(leaves)))
    locked = [d for d in doors if d.get_editor_property("locked")]
    keys = [str(a.get_editor_property("keycard_id")) for a in actors.values() if isinstance(a, unreal.PickupActor)]

    # A door an objective opens (ADoorActor::OpensOnObjective, the layout's "opens_on") needs no keycard.
    unmatched = [d.get_actor_label() for d in locked if str(d.get_editor_property("required_keycard_id")) not in keys
                 and str(d.get_editor_property("opens_on_objective")) == "None"]
    wrong_objective = [d.get_actor_label() for d in doors
                       if str(d.get_editor_property("opens_on_objective")) not in ("None",) + tuple(x.get("opens_on") for x in leaves if x.get("opens_on"))]
    check(not unmatched and not wrong_objective and len(locked) == sum(1 for d in leaves if d.get("locked")),
          layout.name + ": locked doors are locked, and each one's keycard is in the map or an objective opens it",
          ", ".join(unmatched + wrong_objective))
    nav = [d.get_actor_label() for d in doors if d.get_editor_property("door_mesh").get_editor_property("can_ever_affect_navigation")]
    check(not nav, layout.name + ": no door leaf cuts the navmesh", ", ".join(nav))
    narrow = [item for item, _bd, _at, width in layout.door_spots() if width < gi.NAV_LINK_WIDTH]
    links = [a for a in actors.values() if isinstance(a, unreal.NavLinkProxy)]
    check(len(links) == len(narrow), layout.name + ": a nav link through every opening under 140 cm",
          "{0} links, {1} narrow openings".format(len(links), len(narrow)))
    check(len(exits) == len(layout.exits), layout.name + ": an AInteriorExit per exit", "{0} of {1}".format(len(exits), len(layout.exits)))
    lost = [e.get_actor_label() for e in exits if not e.get_editor_property("fallback_district")
            or str(e.get_editor_property("fallback_return_point")) != layout.doorstep]
    check(not lost, layout.name + ": every exit knows the district and the doorstep", ", ".join(lost))
    start = [a for a in actors.values() if isinstance(a, unreal.PlayerStart)]
    want, _yaw = gi.entrance_transform(layout)
    ok = len(start) == 1 and want is not None and gen.same_vector(start[0].get_actor_location(), unreal.Vector(*want), 1.0)
    check(ok, layout.name + ": one PlayerStart, 150 cm inside the entrance", str(want))


def check_stairs(layout, actors):
    for stair in layout.stairs:
        parts = it.stair_parts(layout, stair)
        label = layout.by_id[stair["room"]].label
        treads = sorted((a for l, a in actors.items() if l.startswith("Int_{0}_Tread_".format(label))),
                        key=lambda a: int(a.get_actor_label().rsplit("_", 1)[1]))
        tops = [a.get_actor_location().z + a.get_actor_scale3d().z * 50.0 for a in treads]
        want = [s[5] for s in parts["steps"]]
        off = [i for i, (t, w) in enumerate(zip(tops, want)) if abs(t - w) > 0.5]
        check(len(treads) == len(want) and not off, "{0}: stair {1} has {2} treads at the planned heights".format(
            layout.name, stair["id"], len(want)), "{0} treads, off at {1}".format(len(treads), off[:5]))
        check(it.stair_climb_ok(parts) and parts["riser"] <= it.MAX_RISER, "{0}: stair {1} risers {2:.1f} cm, treads {3:.1f} cm".format(
            layout.name, stair["id"], parts["riser"], parts["tread"]))


def check_lights(layout, actors):
    lights = [a for a in actors.values() if isinstance(a, (unreal.PointLight, unreal.SpotLight)) and "CityLamp" in tags_of(a)]
    dark = []
    for room in layout.rooms:
        if room.light == "none":
            continue
        if not any(room.contains(l.get_actor_location().x, l.get_actor_location().y) for l in lights):
            dark.append(room.id)
    check(not dark, layout.name + ": every lit room has a CityLamp light the EMP can kill", ", ".join(dark))
    static = [a.get_actor_label() for a in actors.values() if isinstance(a, (unreal.Light, unreal.SkyLight))
              and a.get_editor_property("root_component").get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE]
    check(not static, layout.name + ": every light is movable", ", ".join(static))
    unreal.log("[Hawkeye] info  {0}: {1} lamp lights".format(layout.name, len(lights)))


def weapon_name(thug):
    return str(thug.get_editor_property("weapon")).split(".")[-1].split(":")[0].strip("<> ").upper()


DOOR_LEAF_CLEAR = 60.0      # cm (flat) from a door's threshold within which a capsule stands in its leaf


def solid_overlaps(world, centre, radius, half_height, ignore=None):
    """Labels of the walls, props, door leaves and climbable blocks a capsule at centre overlaps. A door's
    interaction zone is not solid: a door counts only when the capsule stands in its doorway."""
    types = [unreal.ObjectTypeQuery.ECC_WORLD_STATIC, unreal.ObjectTypeQuery.ECC_WORLD_DYNAMIC]
    result = unreal.SystemLibrary.capsule_overlap_actors(world, centre, radius, half_height, types, None, ignore or [])
    found = result[1] if isinstance(result, tuple) else (result or [])
    out = []
    for actor in found or []:
        if isinstance(actor, unreal.DoorActor):
            at = actor.get_actor_location()
            if math.hypot(at.x - centre.x, at.y - centre.y) < DOOR_LEAF_CLEAR + radius:
                out.append(actor.get_actor_label())
        elif isinstance(actor, unreal.StaticMeshActor) or "IntClimb" in tags_of(actor) or "IntLedge" in tags_of(actor):
            out.append(actor.get_actor_label())
    return out


def check_thugs(layout, world, actors):
    """Every enemy: his thug on the right Blueprint with the layout's weapon, route, crew and keycards; on
    the navmesh; every patrol point on a path from where he stands; nothing solid where he stands or at any
    point he walks to."""
    enemies = layout.enemies()
    thugs = {tag_value(a, "enemy:"): a for l, a in actors.items() if "_Thug_" in l}
    missing, wrong, off, unreachable, walled = [], [], [], [], []
    for enemy in enemies:
        thug = thugs.get(enemy["id"])
        if thug is None:
            missing.append(enemy["id"])
            continue
        _bp_path, bp_name = gi.ENEMY_BLUEPRINTS[enemy["type"]]
        points = [p for p in thug.get_editor_property("patrol_points") if p]
        cards = sorted(str(p.get_editor_property("keycard_id")) for p in thug.get_editor_property("carried_pickups") if p)
        group = str(thug.get_editor_property("alert_group"))
        problems = []
        if bp_name not in c.class_name(thug.get_class()):
            problems.append(c.class_name(thug.get_class()))
        if weapon_name(thug) != enemy["type"]:
            problems.append(weapon_name(thug))
        if len(points) != len(enemy["patrol"]):
            problems.append("{0} points".format(len(points)))
        if (group if group != "None" else "") != enemy["alert_group"]:
            problems.append("group " + group)
        if cards != sorted(enemy["keycards"]):
            problems.append("carries " + ",".join(cards))
        if thug.get_editor_property("archer_sees_all_round"):
            problems.append("sees all round")
        if problems:
            wrong.append("{0} ({1})".format(enemy["id"], ", ".join(problems)))
        feet = thug.get_actor_location() - unreal.Vector(0.0, 0.0, gi.THUG_HALF_HEIGHT)
        ok, _point = project(world, feet)
        if not ok:
            off.append(enemy["id"])
        start = feet + unreal.Vector(0.0, 0.0, 60.0)
        for i, point in enumerate(points):
            to = point.get_actor_location() - unreal.Vector(0.0, 0.0, gi.PATROL_POINT_HEIGHT - 60.0)
            # The point he starts on is reached already (a zero-length path reads as none).
            if math.hypot(to.x - start.x, to.y - start.y) > 50.0 and \
                    unreal.HawkeyeNavigationLibrary.find_path_length(world, start, to) < 0.0:
                unreachable.append("{0} point {1}".format(enemy["id"], i))
        for where, centre in [("start", thug.get_actor_location())] + [
                ("point {0}".format(i), p.get_actor_location() - unreal.Vector(0.0, 0.0, gi.PATROL_POINT_HEIGHT - gi.THUG_HALF_HEIGHT - 2.0))
                for i, p in enumerate(points)]:
            hits = solid_overlaps(world, centre, 34.0, gi.THUG_HALF_HEIGHT - 8.0, [thug])
            if hits:
                walled.append("{0} {1} in {2}".format(enemy["id"], where, ", ".join(hits[:3])))
        unreal.log("[Hawkeye] info  {0}: {1} {2} {3} at {4}, {5} patrol points, crew {6}, carries {7}".format(
            layout.name, enemy["id"], enemy["type"].lower(), c.class_name(thug.get_class()), thug.get_actor_location(),
            len(points), enemy["alert_group"] or "none", ",".join(cards) or "nothing"))
    check(not missing and len(thugs) == len(enemies), layout.name + ": a thug for every enemy", ", ".join(missing))
    check(not wrong, layout.name + ": each enemy's Blueprint, weapon, route, crew and keycards", "; ".join(wrong))
    check(not off, layout.name + ": every enemy starts on the navmesh", ", ".join(off))
    check(not unreachable, layout.name + ": every patrol point is on a navmesh path from its enemy", ", ".join(unreachable))
    check(not walled, layout.name + ": no enemy starts or walks inside a wall or a prop", "; ".join(walled))
    carried = [a for a in actors.values() if isinstance(a, unreal.PickupActor) and tag_value(a, "carried:")]
    check(len(carried) == sum(len(e["keycards"]) for e in enemies), layout.name + ": a pickup for every keycard an enemy carries",
          "{0} carried pickups".format(len(carried)))


def check_door_links(layout, actors):
    """Every door with a leaf in a narrow opening knows the nav link through it (ADoorActor keeps a locked
    door's link Null, out of the navmesh, until the door opens), and every such link is a plain point link."""
    doors = [a for a in actors.values() if isinstance(a, unreal.DoorActor)]
    bad = []
    for door in doors:
        link = door.get_editor_property("nav_link")
        if link is not None and (link.get_editor_property("smart_link_is_relevant") or len(link.get_editor_property("point_links")) != 1):
            bad.append(door.get_actor_label())
    narrow = [d for d, _bd, _at, width in layout.door_spots() if width < gi.NAV_LINK_WIDTH and d.get("leaf", "wood") != "none"]
    linked = [d for d in doors if d.get_editor_property("nav_link") is not None]
    check(not bad and len(linked) == len(narrow), layout.name + ": every narrow door knows its nav link",
          ", ".join(bad) or "{0} of {1}".format(len(linked), len(narrow)))


def tag_value(actor, prefix):
    for t in tags_of(actor):
        if t.startswith(prefix):
            return t[len(prefix):]
    return ""


def check_anchors(layout, world, actors):
    anchors = [a for l, a in actors.items() if "_Anchor_" in l]
    bad = []
    for anchor in anchors:
        landing = anchor.get_landing_location()
        hit, z = trace(world, (landing.x, landing.y, landing.z + 40.0), (landing.x, landing.y, landing.z - 40.0))
        if hit is None or "_Floor_" not in hit.get_actor_label():
            bad.append("{0} lands on {1}".format(anchor.get_actor_label(), hit.get_actor_label() if hit else "nothing"))
    check(len(anchors) == len(layout.anchors) and not bad, layout.name + ": every grapple anchor on a rail lands on the mezzanine floor",
          "; ".join(bad) or "{0} anchors".format(len(anchors)))
    rails = [bd for bd in layout.all_boundaries() if bd.kind == "rail"]
    ledges = [a for l, a in actors.items() if "_Ledge_" in l]
    check(len(ledges) == len(rails), layout.name + ": a traversable ledge along every rail", "{0} ledges, {1} rails".format(len(ledges), len(rails)))


def check_navigation(layout, world, actors):
    start = next((a for a in actors.values() if isinstance(a, unreal.PlayerStart)), None)
    if start is None:
        check(False, layout.name + ": a PlayerStart to path from")
        return
    built = bool(unreal.HawkeyeNavigationLibrary.build_navigation_now(world))
    check(built, layout.name + ": the navmesh builds")
    unreachable = []
    for room in layout.rooms:
        reached = None
        for px, py in room_samples(room):
            if layout.owner_at(room.floor, px, py) != room.id:
                continue
            length = unreal.HawkeyeNavigationLibrary.find_path_length(world, start.get_actor_location(),
                                                                       unreal.Vector(px, py, room.z0 + 60.0))
            if length >= 0.0:
                reached = length
                break
        if reached is None:
            unreachable.append(room.id)
        else:
            unreal.log("[Hawkeye] info  {0}: {1} is {2:.0f} m from the entrance on foot".format(layout.name, room.id, reached / 100.0))
    check(not unreachable, layout.name + ": every room is on a navmesh path from the entrance", ", ".join(unreachable))


def check_interior(path):
    try:
        layout = it.parse(path)
    except it.LayoutError as exc:
        check(False, os.path.basename(path) + " parses and keeps its rules", "; ".join(exc.errors))
        return
    check(True, os.path.basename(path) + " parses and keeps its rules",
          "{0} rooms, {1} doors, {2} exits, {3} windows".format(len(layout.rooms), len(layout.doors), len(layout.exits),
                                                               len(layout.windows)))
    if not c.exists(layout.map_path) or not c.level_editor_subsystem().load_level(layout.map_path):
        check(False, layout.map_path + " opens")
        return
    world = c.editor_world()
    actors = gen.actors_by_label()
    # Building the navmesh first also brings the level's collision up for the traces below.
    unreal.HawkeyeNavigationLibrary.build_navigation_now(world)
    check_game_mode(layout)

    check_labels(layout, actors)
    check_rooms(layout, world)
    check_openings(layout, world, actors)
    check_stairs(layout, actors)
    check_lights(layout, actors)
    check_anchors(layout, world, actors)
    check_navigation(layout, world, actors)
    check_thugs(layout, world, actors)
    check_door_links(layout, actors)


def check_district():
    if not c.level_editor_subsystem().load_level(gen.MAP_PATH):
        check(False, gen.MAP_PATH + " opens")
        return
    world = c.editor_world()
    actors = gen.actors_by_label()
    door = actors.get(gen.INTERIOR_LABEL)
    interior = door.get_editor_property("interior") if door is not None else None
    check(door is not None and interior is not None and str(interior.get_path_name()).split(".")[0] == gen.INTERIOR_MAP
          and str(door.get_editor_property("return_point_label")) == gen.INTERIOR_RETURN_LABEL
          and str(door.get_editor_property("display_name")) == gen.INTERIOR_NAME,
          gen.INTERIOR_LABEL + " opens " + gen.INTERIOR_MAP + " and comes back to its doorstep")
    for label in (gen.INTERIOR_RETURN_LABEL, gen.INTERIOR_ROOF_LABEL):
        point = actors.get(label)
        check(point is not None and label in tags_of(point), label + " is placed and tagged with its label")
    district = gen.District()
    spot = gen.interior_spot(district)
    roof = actors.get(gen.INTERIOR_ROOF_LABEL)
    if spot is not None and roof is not None:
        loc = roof.get_actor_location()
        hit, z = trace(world, (loc.x, loc.y, loc.z), (loc.x, loc.y, loc.z - 300.0))
        on = hit is not None and ("osm:" + spot["rec"]["id"]) in tags_of(hit)
        check(on and abs(z - spot["rec"]["height_m"] * 100.0) < 5.0, gen.INTERIOR_ROOF_LABEL + " stands on its building's roof",
              "osm {0}, {1}".format(spot["rec"]["id"], hit.get_actor_label() if hit else "nothing below"))
        step = actors.get(gen.INTERIOR_RETURN_LABEL).get_actor_location()
        hit, z = trace(world, (step.x, step.y, step.z), (step.x, step.y, step.z - 300.0))
        check(hit is not None and z is not None and step.z - z < 120.0, gen.INTERIOR_RETURN_LABEL + " stands on the pavement",
              "{0} at {1}".format(hit.get_actor_label() if hit else "nothing", z))
    else:
        check(False, "the interior entrance has a building", "no spot")


def run():
    paths = sorted(glob.glob(os.path.join(gi.LAYOUT_DIR, "*.json")))
    check(bool(paths), "at least one interior layout in Tools/Interiors")
    for path in paths:
        check_interior(path)
    check_district()
    if _failures:
        unreal.log_error("[Hawkeye] verify_interiors FAIL ({0} check(s): {1})".format(len(_failures), "; ".join(_failures)))
        return False
    unreal.log("[Hawkeye] verify_interiors PASS")
    return True


if __name__ == "__main__":
    run()
