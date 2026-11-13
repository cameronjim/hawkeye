"""Check L_District_EastVillage against the OSM records it was generated from. Read-only.

Prints one line per check and a final ``[Hawkeye] verify_city PASS`` or ``FAIL``:

* one City_Bldg_<id> actor per building record, and no strays
* every building actor has a static mesh with collision (complex as simple) and the MI_Facade_<style>
  instance generate_city.facade_style picks for its record
* every building mesh's top is the record height within 1 cm (plus the parapet if it has one), and
  nothing in it stands past the footprint's bounds by more than 1 cm (the parapet is flush at corners)
* a PlayerStart, a NavMeshBoundsVolume, a directional light, a sky light, and no light with
  Static mobility
* handedness: the streets come out in the real order (1st Ave west of Ave A west of Ave B
  west of Ave C, East 6th south of East 11th), i.e. the map is not mirrored
* the BP_GameMode_EastVillage override starting DA_CH01_NightShift (its objectives the chase's three),
  and the objective chain (the Rooftops chapter's roofs, kept for the traversal audit and the tests): a City_Obj_* volume above the
  reach_roof and cross_block roofs, three City_Beacon_* (pole, emissive cap, movable
  300 lm light) on the objective roofs, the chapter end (City_ChapterEndTower on the find_arrow
  roof, City_ChapterEnd completing find_arrow with the arrow meshes, City_SceneReturn_FB00), the street lamps (light, pole, head) matching the
  generator, and no prison-build actors (thugs, keycards, doors, pickups)
* City_LedgeSpawner and its props asset (the ledges and anchors are data, spawned at load; this
  script calls SpawnAll() first, as BeginPlay does), and no ledge or anchor saved in the map
* the grapple anchors match the generator, and every anchor's landing point is on a roof: a
  trace from just above it down 50 cm hits a City_Bldg mesh; by the generator's reach model, every anchored
  roof with a wall on a street and a sidewalk in range has an anchor reachable from the sidewalk, and the
  chapter 1 roofs chain (reach_roof from the street, cross_block from it on the roofs, find_arrow from the
  street and from cross_block by the roofs or down a fire escape); the neighbour pairs are counted
* one City_Ledge_ BP_TraversableBlock per roof edge the generator considers, hidden and
  blocking only the Traversable channel, its Ledge_1 spline ending on the parapet's outer
  corners within 5 cm; the parkour test blocks and park walls at their heights
* the fire escapes match the generator (count, buildings 10 to 30 m, landings from 330 cm up at
  330 cm spacing, each on its building's facade line), none below 330 cm, none within the lamp
  clearance of a lamp pole or head, no two landings overlapping; each has its rail ledge
  (Ledge_1 on the outer top rail within 5 cm) and the bars are drawn in M_SteelPainted
* the partner: City_ClintStart and City_Clint (BP_Clint) 5 m behind the PlayerStart, on the navmesh.
* Lucky: City_Lucky (ALuckyCharacter) beside the PlayerStart, on the navmesh.
* chapter 1's fight: five City_Thug_ (one gunner, two bats, one fists, one heavy on BP_Thug_Heavy), the
  RoofPair on the cross_block roof, the street pair's two City_Patrol_ points 40 m apart and the
  heavy's two 20 m apart, the StreetGroup tag on the three street thugs, City_ThugGroup_clear_roof,
  and every thug's feet on the navmesh (the navmesh is built in the editor world first, not saved)
* Barney's archers: two City_Archer_ (BP_Archer, Bow, tagged ArcherPair) where the generator puts
  them, 15 to 25 m from the find_arrow beacon, feet on the navmesh, and a clear line from each one's
  eyes to Kate's chest at the beacon by a real trace against the level's collision
* the clutter in the props asset matches the generator kind by kind, SpawnAll draws every instance,
  and none stands within reach of the thug patrol (its points, the line between them, the thugs),
  on or against a fire-escape landing at the landing's height, within 3 m of an objective beacon,
  or within the anchor clearance of a grapple anchor or its landing point on the same roof
* the hero block's December dressing: the props asset's Dress_ groups hold exactly the instances _dressing_kit makes
  from the committed plan (mesh, instanced-usable material and collision per group), SpawnAll draws them all, none
  on the navmesh, and the few City_DressLight_ lights and the City_DressShot_ cameras are there
* the side challenges: three archery and three traversal definitions as create_challenges.py plans
  them, a City_Challenge_<id> pedestal on each start holding its definition, every archery target
  10 to 40 m out with a clear line, and every traversal ring reachable from the one before it
* the street crimes: four DA_Crime_ definitions, the City_CrimeSpot_ create_crimes.py plans (eight on
  street corners, four on roofs, two to four in alleys), none within 40 m of a safehouse, a pedestal or
  the interior door, all on the navmesh, every robbery escape point 50 to 70 m out with a walkable
  path, every alley 3 to 6 m across by the level's walls and joined to the streets on the navmesh
* the five tallest and five shortest buildings with their OSM ids and streets, to eyeball

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\verify_city.py" -unattended -nullrhi -nosplash -nop4 -stdout
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import generate_city as gen  # noqa: E402
import _dressing_build as dress  # noqa: E402
import _dressing_kit as kit  # noqa: E402
import _geo as geo  # noqa: E402
import _materials as m  # noqa: E402

HEIGHT_TOLERANCE_CM = 1.0

_failures = []


def check(ok, what, detail=""):
    line = "[Hawkeye] {0}  {1}{2}".format("PASS" if ok else "FAIL", what, "  (" + detail + ")" if detail else "")
    if ok:
        unreal.log(line)
    else:
        unreal.log_error(line)
        _failures.append(what)
    return ok


def tag_value(actor, prefix):
    for t in actor.get_editor_property("tags"):
        s = str(t)
        if s.startswith(prefix):
            return s[len(prefix):]
    return None


def mesh_top_cm(static_mesh):
    box = static_mesh.get_bounding_box()
    return box.max.z, box.min.z


def has_collision(component, static_mesh):
    try:
        if component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
            return False, "component collision disabled"
        body = static_mesh.get_editor_property("body_setup")
        if body is None:
            return False, "no body setup"
        flag = body.get_editor_property("collision_trace_flag")
        if flag != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            return False, "trace flag {0}".format(flag)
        return True, ""
    except Exception as exc:  # noqa: BLE001
        return False, "{0}: {1}".format(type(exc).__name__, exc)


LANDING_PROBE_UP = 20.0     # cm above the landing point the probe starts
LANDING_PROBE_DOWN = 50.0   # cm below it the probe must hit a roof by


def _of_type(result, cls):
    """The first value of type cls in a Geometry Script return (a value or a tuple of out-params)."""
    for item in (result if isinstance(result, tuple) else (result,)):
        if isinstance(item, cls):
            return item
    return None


class MeshProbe(object):
    """Ray casts against building meshes with Geometry Script's BVH. The editor world has no
    physics scene in a commandlet, so a collision trace from Python always misses; this casts
    the same ray against the same triangles the complex-as-simple collision is built from."""

    def __init__(self):
        self.cache = {}

    def _bvh(self, static_mesh):
        key = static_mesh.get_path_name()
        if key not in self.cache:
            mesh = unreal.DynamicMesh()
            unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh_v2(
                static_mesh, mesh, unreal.GeometryScriptCopyMeshFromAssetOptions(),
                unreal.GeometryScriptMeshReadLOD(), True)
            bvh = _of_type(unreal.GeometryScript_MeshSpatial.build_bvh_for_mesh(mesh),
                           unreal.GeometryScriptDynamicMeshBVH)
            self.cache[key] = (mesh, bvh)
        return self.cache[key]

    def hits_down(self, actor, point):
        """True when a ray from LANDING_PROBE_UP above point hits actor's mesh within
        LANDING_PROBE_DOWN below point."""
        static_mesh = actor.get_editor_property("static_mesh_component").get_editor_property("static_mesh")
        if static_mesh is None:
            return False
        mesh, bvh = self._bvh(static_mesh)
        if bvh is None:
            return False
        origin = point - actor.get_actor_location() + unreal.Vector(0.0, 0.0, LANDING_PROBE_UP)
        hit = _of_type(unreal.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(
            mesh, bvh, origin, unreal.Vector(0.0, 0.0, -1.0), unreal.GeometryScriptSpatialQueryOptions()),
            unreal.GeometryScriptRayHitResult)
        return bool(hit is not None and hit.hit and hit.ray_parameter <= LANDING_PROBE_UP + LANDING_PROBE_DOWN)


def check_anchors(district, actors):
    anchors = {label: a for label, a in actors.items() if label.startswith(gen.ANCHOR_PREFIX)}
    expected = gen.anchor_spots(district)
    labels_ok = set(anchors) == {gen.ANCHOR_PREFIX + str(i) for i in range(len(expected))}
    check(labels_ok, "grapple anchors match the generator",
          "{0} anchors, {1} expected, on {2} roofs".format(len(anchors), len(expected), len({s[6] for s in expected})))

    buildings = []
    for label, actor in actors.items():
        if label.startswith(gen.BUILDING_PREFIX):
            origin, extent = actor.get_actor_bounds(False)
            buildings.append((label, actor, origin, extent))

    probe = MeshProbe()
    off_roof = []
    heights = []
    for label, anchor in sorted(anchors.items()):
        landing = anchor.get_landing_point()
        point = landing.get_world_location() if landing is not None else anchor.get_actor_location()
        found = None
        for b_label, actor, origin, extent in buildings:
            if abs(point.x - origin.x) <= extent.x and abs(point.y - origin.y) <= extent.y \
                    and probe.hits_down(actor, point):
                found = b_label
                break
        if found is None:
            off_roof.append(label)
        heights.append(anchor.get_actor_location().z - point.z)
    check(anchors and not off_roof, "every anchor's landing point is on a City_Bldg roof (50 cm probe)",
          "{0} of {1} off a roof{2}".format(len(off_roof), len(anchors),
                                            ": " + ", ".join(off_roof[:5]) if off_roof else ""))
    if heights:
        unreal.log("[Hawkeye] info  anchors sit {0:.0f} to {1:.0f} cm above their landing points".format(
            min(heights), max(heights)))


def check_anchor_reach(district):
    """The anchor placement guarantees (gameplay-semantics.md, "Anchor placement"), by generate_city's reach
    model (the grapple's range, cone, camera sight and clear zip in plain geometry; Hawkeye.Grapple.Audit
    checks the same with the game's traces)."""
    rep = gen.anchor_reach_report(district)
    facade = sum(1 for s in gen.anchor_spots(district) if s[4] == gen.FACADE_ANCHOR_LANDING)
    unreal.log("[Hawkeye] info  {0} facade anchors; {1} anchored roofs out of the grapple's reach from any sidewalk "
               "(too tall, or no sidewalk within 25 m): {2}".format(
                   facade, len(rep["street_out_of_reach"]), ", ".join(rep["street_out_of_reach"][:12])))
    # A roof inside a block (no open wall on a road) may only be seen across other roofs; a street wall its fire
    # escape fills has nowhere to hold an anchor the sidewalk sees past the landings.
    check(not rep["no_street"], "every anchored roof with an open wall on a street, in range of the sidewalk, has an "
          "anchor reachable from it", "{0} without: {1}; inside their blocks {2}: {3}; street wall filled by its fire "
          "escape {4}: {5}".format(len(rep["no_street"]), ", ".join(rep["no_street"][:10]), len(rep["street_inside_block"]),
                                   ", ".join(rep["street_inside_block"]), len(rep["street_wall_escape"]),
                                   ", ".join(rep["street_wall_escape"])))
    missing = len(rep["pairs_missing"])
    unreal.log("[Hawkeye] info  roofs facing each other across a street or gap: {0} in range, {1} with no anchor the "
               "neighbour's roof spots reach, {2} out of the grapple's range".format(
                   rep["pairs"], missing, rep["pairs_out_of_range"]))
    lines = []
    ok = True
    for oid, osm, street, how in rep["objectives"]:
        lines.append("{0} {1}: street {2}, {3}".format(oid, osm, "yes" if street else "NO", how or "NOT REACHED"))
        ok = ok and osm is not None and street and bool(how)
    ok = ok and len(rep["objectives"]) == len(gen.OBJECTIVE_IDS) and rep["objectives"][1][3] == "roofs"
    check(ok, "the chapter 1 roofs chain: each reachable from the street, cross_block from reach_roof on the roofs, "
              "find_arrow from cross_block", "; ".join(lines))


LEDGE_TOLERANCE_CM = 5.0



closest_point_on_ring = gen._edge_distance   # was the same expression, written out here


def check_ledges(district, actors):
    """Every roof edge the generator considers has its City_Ledge_ block, and each block's Ledge_1
    spline runs corner to corner along the parapet top: both ends within LEDGE_TOLERANCE_CM of the
    expected corners, of the footprint outline (horizontally) and of the parapet top (vertically)."""
    expected = gen.ledge_spots(district)
    ledges = {label: a for label, a in actors.items() if label.startswith(gen.LEDGE_PREFIX)}
    labels_ok = set(ledges) == {s[0] for s in expected}
    check(labels_ok, "traversable ledges match the generator's roof edges",
          "{0} ledges, {1} roof edges of {2} cm or more on {3} buildings".format(
              len(ledges), len(expected), int(gen.LEDGE_MIN_EDGE), len({s[1] for s in expected})))

    rings = {}
    for rec in district.buildings:
        rings[rec["id"]] = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
    cls = gen.traversable_class()
    channel = gen.traversable_channel()
    wrong_class, no_spline, off_corner, off_edge, visible, bad_collision = [], [], [], [], [], []
    worst = 0.0
    for label, osm, _origin, _yaw, _scale, corner_a, corner_b, _spec in expected:
        actor = ledges.get(label)
        if actor is None:
            continue
        if cls is not None and actor.get_class() != cls:
            wrong_class.append(label)
            continue
        spline = next((comp for comp in actor.get_components_by_class(unreal.SplineComponent)
                       if comp.get_name() == gen.LEDGE_SPLINE), None)
        if spline is None or spline.get_number_of_spline_points() < 2:
            no_spline.append(label)
            continue
        ends = [spline.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)
                for i in (0, spline.get_number_of_spline_points() - 1)]
        for end, want in zip(ends, (corner_a, corner_b)):
            error = max(abs(end.x - want[0]), abs(end.y - want[1]), abs(end.z - want[2]))
            worst = max(worst, error)
            if error > LEDGE_TOLERANCE_CM:
                off_corner.append("{0} {1:.1f} cm".format(label, error))
            if closest_point_on_ring((end.x, end.y), rings[osm]) > LEDGE_TOLERANCE_CM:
                off_edge.append(label)
        for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
            if mesh.get_editor_property("visible"):
                visible.append(label)
            if channel is not None and (
                    mesh.get_collision_response_to_channel(channel) != unreal.CollisionResponseType.ECR_BLOCK
                    or mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)
                    != unreal.CollisionResponseType.ECR_IGNORE):
                bad_collision.append(label)
    check(not wrong_class, "every ledge is a BP_TraversableBlock (a LevelBlock_Traversable)", ", ".join(wrong_class[:5]))
    check(not no_spline, "every ledge has its " + gen.LEDGE_SPLINE + " spline", ", ".join(no_spline[:5]))
    check(not off_corner, "ledge spline ends on the parapet corners within {0:.0f} cm (worst {1:.2f} cm)".format(
        LEDGE_TOLERANCE_CM, worst), "; ".join(off_corner[:5]))
    check(not off_edge, "ledge spline ends on the roof outline within {0:.0f} cm".format(LEDGE_TOLERANCE_CM),
          ", ".join(off_edge[:5]))
    check(not visible, "ledge blocks are hidden", ", ".join(visible[:5]))
    check(not bad_collision, "ledge blocks block only the Traversable channel (not Pawn)", ", ".join(bad_collision[:5]))

    blocks = gen.test_block_spots(district, actors)
    missing = [s[0] for s in blocks if s[0] not in actors]
    check(not missing, "parkour test blocks present ({0})".format(", ".join(
        "{0} {1:.0f} cm".format(s[0], s[4][2] * 100.0) for s in blocks)), ", ".join(missing))
    wrong_height = []
    for label, _tag, origin, _yaw, scale in blocks:
        actor = actors.get(label)
        if actor is None:
            continue
        box_origin, extent = actor.get_actor_bounds(True)
        top = box_origin.z + extent.z
        if abs(top - (origin[2] + scale[2] * 100.0)) > 1.0:
            wrong_height.append("{0} top {1:.1f}".format(label, top))
    check(not wrong_height, "parkour test blocks stand their heights above the ground under them, within 1 cm",
          "; ".join(wrong_height))


def check_spawner(district, actors):
    """City_LedgeSpawner exists, its props asset matches the generator, and SpawnAll puts every
    ledge and anchor out (as the game does at BeginPlay), so the checks after this see them."""
    spawner = actors.get(gen.SPAWNER_LABEL)
    check(spawner is not None, gen.SPAWNER_LABEL + " present")
    if spawner is None:
        return None
    data = spawner.get_editor_property("data")
    ledges = gen.ledge_spots(district)
    anchors = gen.anchor_spots(district)
    escapes = gen.fire_escape_spots(district)
    clutter, _plan = clutter_groups(district)
    doc = dress.plan_doc()
    if doc is not None:
        clutter = clutter + dress.loaded_groups(doc)
    want = gen.city_props_hash(ledges, anchors, spawner.get_editor_property("ledge_class"),
                               spawner.get_editor_property("anchor_class"), escapes, clutter)
    check(data is not None and str(data.get_editor_property("source_hash")) == want,
          "its props asset matches the generator's ledges and anchors",
          "{0}, hash {1}".format(c.safe_name(data), str(data.get_editor_property("source_hash")) if data else "-"))
    saved = [label for label in actors if label.startswith(gen.LEDGE_PREFIX)
             or (label.startswith(gen.ANCHOR_PREFIX) and label[len(gen.ANCHOR_PREFIX):].isdigit())]
    check(not saved, "no ledge or anchor actors saved in the map", "{0} saved".format(len(saved)))
    spawned = spawner.spawn_all()
    unreal.log("[Hawkeye] info  SpawnAll: {0} actors, ledges {1:.0f} ms, anchors {2:.0f} ms (editor world)".format(
        spawned, spawner.get_load_ledge_spawn_seconds() * 1000.0, spawner.get_anchor_spawn_seconds() * 1000.0))
    check(spawner.get_spawned_ledge_count() == len(ledges) and spawner.get_spawned_anchor_count() == len(anchors)
          and spawner.get_spawned_fire_escape_count() == len(escapes),
          "SpawnAll spawns every ledge, anchor and fire-escape landing",
          "{0}/{1} ledges, {2}/{3} anchors, {4}/{5} landings".format(
              spawner.get_spawned_ledge_count(), len(ledges), spawner.get_spawned_anchor_count(), len(anchors),
              spawner.get_spawned_fire_escape_count(), len(escapes)))
    return spawner


def clutter_groups(district):
    """The generator's clutter groups, from the meshes and instances already on disk (loads only)."""
    meshes = {name: c.load_or_none(c.asset_path(gen.MESH_DIR, name)) for name in gen.CLUTTER_MESHES}
    materials = {key: c.load_or_none(m.MATERIALS_PATH + "/MI_Prop_" + key) for key in gen.CLUTTER_MATERIALS}
    return gen.clutter_groups(district, meshes, materials)


def check_clutter(district, spawner):
    """Counts per kind, the spawned instances, and nothing in the way of the patrol, landings,
    beacons or anchors."""
    if spawner is None:
        check(False, "clutter", "no spawner")
        return
    data = spawner.get_editor_property("data")
    # The December dressing's groups (Dress_*) are checked by check_dressing.
    groups = [g for g in (data.get_editor_property("clutter") if data is not None else [])
              if not str(g.get_editor_property("kind")).startswith(kit.GROUP_PREFIX)]
    _want, plan = clutter_groups(district)
    have = {str(g.get_editor_property("kind")): list(g.get_editor_property("instances")) for g in groups}
    want = {k: len(v) for k, v in plan.items() if v}
    got = {k: len(v) for k, v in have.items()}
    check(got == want, "clutter groups match the generator",
          ", ".join("{0} {1}".format(v, k) for k, v in sorted(got.items())))
    total = sum(want.values())
    missing = [str(g.get_editor_property("kind")) for g in groups
               if g.get_editor_property("mesh") is None or g.get_editor_property("material") is None]
    drawn = sum(comp.get_instance_count() for comp in spawner.get_clutter_components()
                if not comp.get_name().startswith("Clutter_" + kit.GROUP_PREFIX))
    check(drawn == total and not missing,
          "SpawnAll draws every clutter instance, each group with its mesh and MI_Prop_ material",
          "{0}/{1} instances{2}".format(drawn, total, "; no mesh or material: " + ", ".join(missing) if missing else ""))
    prop = c.load_or_none(m.M_PROP)
    check(prop is not None and bool(prop.get_editor_property("used_with_instanced_static_meshes")),
          "M_Prop is usable on instanced meshes")

    thugs, points, _roof = gen.thug_placements(district)
    keep = [(t[1], t[2]) for t in thugs] + [(p[1], p[2]) for p in points]
    patrol_segments = [[(a[1], a[2]), (b[1], b[2])] for a, b in zip(points[0::2], points[1::2])]
    landings = []
    for rec in data.get_editor_property("fire_escapes") if data is not None else []:
        xf = rec.get_editor_property("transform")
        t = xf.translation
        landings.append((gen._landing_corners(t.x, t.y, xf.rotation.rotator().yaw),
                         t.z - gen.FIRE_ESCAPE_SLAB[2], t.z + gen.FIRE_ESCAPE_RAIL))
    beacons = [(x, y) for x, y, _z, _how in gen.beacon_spots(district).values()]
    anchors = []
    for x, y, z, yaw, forward, _drop, _osm in gen.anchor_spots(district):
        anchors.append((x, y, z))
        anchors.append((x + math.cos(math.radians(yaw)) * forward, y + math.sin(math.radians(yaw)) * forward, z))

    blocking = {"patrol": [], "landing": [], "beacon": [], "anchor": []}
    for kind, xforms in sorted(have.items()):
        radius = max(gen.CLUTTER_KINDS[kind][4], 60.0 if kind == "Scaffold" else 0.0)
        height = gen.CLUTTER_HEIGHTS.get(kind, 150.0)
        roof = kind in ("WaterTower", "HVAC", "Chimney")
        for xf in xforms:
            t = xf.translation
            pt = (t.x, t.y)
            if any(math.hypot(pt[0] - q[0], pt[1] - q[1]) < gen.PATROL_CLEAR + radius - 1.0 for q in keep) or (
                    any(gen.closest_point_on_polyline(pt, seg)[0] < gen.PATROL_CLEAR + radius - 1.0
                        for seg in patrol_segments)):
                blocking["patrol"].append(kind)
            if kind == "Scaffold":
                yaw = xf.rotation.rotator().yaw
                ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
                w, d_, _h = gen.SCAFFOLD_BAY
                foot = [(t.x, t.y), (t.x + ux * w, t.y + uy * w), (t.x + ux * w - uy * d_, t.y + uy * w + ux * d_),
                        (t.x - uy * d_, t.y + ux * d_)]
                if any(lo < t.z + height and hi > t.z and gen._rects_overlap(foot, quad) for quad, lo, hi in landings):
                    blocking["landing"].append(kind)
            elif any(lo < t.z + height and hi > t.z and gen._point_quad_distance(pt, quad) < radius
                     for quad, lo, hi in landings):
                blocking["landing"].append(kind)
            if any(math.hypot(pt[0] - b[0], pt[1] - b[1]) < 300.0 + radius for b in beacons):
                blocking["beacon"].append(kind)
            if roof and any(abs(a[2] - t.z) < gen.ANCHOR_SAME_ROOF and math.hypot(pt[0] - a[0], pt[1] - a[1]) < gen.ANCHOR_CLEAR + radius - 1.0
                            for a in anchors):
                blocking["anchor"].append(kind)
    for what, kinds in sorted(blocking.items()):
        check(not kinds, "no clutter in the way of the {0}".format(
            {"patrol": "thug patrol", "landing": "fire-escape landings", "beacon": "objective beacons",
             "anchor": "grapple anchors"}[what]),
              "{0} instance(s): {1}".format(len(kinds), ", ".join(sorted(set(kinds)))) if kinds else "")


def check_dressing(spawner, actors):
    """The hero block's December dressing (_dressing_build.py): the props asset's Dress_ groups hold exactly the
    instances _dressing_kit makes from the committed plan, each with its mesh, an instanced-usable material and its
    collision; SpawnAll draws them all, none of them on the navmesh; the few real lights and the shot cameras."""
    doc = dress.plan_doc()
    check(doc is not None and doc["block"] == dress.HERO_BLOCK, "the dressing plan is the hero block's",
          doc["block"] if doc else "no plan")
    if doc is None or spawner is None:
        return
    data = spawner.get_editor_property("data")
    groups = [g for g in (data.get_editor_property("clutter") if data is not None else [])
              if str(g.get_editor_property("kind")).startswith(kit.GROUP_PREFIX)]
    want = kit.group_counts(doc)
    got = {str(g.get_editor_property("kind")): len(g.get_editor_property("instances")) for g in groups}
    check(got == want, "dressing groups hold the plan's instances",
          "{0} records -> {1} instances in {2} groups{3}".format(
              len(doc["records"]), sum(got.values()), len(got),
              "" if got == want else "; want " + ", ".join("{0} {1}".format(k, v) for k, v in want.items())))
    kinds = {}
    for rec in doc["records"]:
        kinds[rec["kind"]] = kinds.get(rec["kind"], 0) + 1
    check(kinds == doc["counts"], "the plan's records match its counts",
          ", ".join("{0} {1}".format(k, v) for k, v in kinds.items()))
    bad = []
    for g in groups:
        kind = str(g.get_editor_property("kind"))
        _mesh, _mat, collision, _shadow = kit.GROUPS.get(kind, (None, None, None, None))
        material = g.get_editor_property("material")
        base = material
        while base is not None and not isinstance(base, unreal.Material):
            base = base.get_editor_property("parent")
        if g.get_editor_property("mesh") is None or material is None:
            bad.append(kind + " no mesh or material")
        elif base is None or not bool(base.get_editor_property("used_with_instanced_static_meshes")):
            bad.append(kind + " material not usable on instances")
        if bool(g.get_editor_property("collision")) != collision:
            bad.append(kind + " collision")
    check(not bad, "every dressing group has its mesh, an instanced material and its collision", "; ".join(bad))
    comps = [comp for comp in spawner.get_clutter_components()
             if comp.get_name().startswith("Clutter_" + kit.GROUP_PREFIX)]
    drawn = sum(comp.get_instance_count() for comp in comps)
    nav = []
    for comp in comps:
        try:
            if bool(comp.get_editor_property("can_ever_affect_navigation")):
                nav.append(comp.get_name())
        except Exception:  # noqa: BLE001 - not readable from Python on this engine: the spawner sets it in C++
            pass
    check(drawn == sum(want.values()) and len(comps) == len(want) and not nav,
          "SpawnAll draws every dressing instance, one instanced mesh per group, none on the navmesh",
          "{0}/{1} instances in {2} components{3}".format(drawn, sum(want.values()), len(comps),
                                                          "; on the navmesh: " + ", ".join(nav) if nav else ""))
    lights = {label: a for label, a in actors.items() if label.startswith(dress.LIGHT_PREFIX)}
    spots = kit.light_spots(doc)
    shadowed = [label for label, a in lights.items()
                if a.get_editor_property("point_light_component").get_editor_property("cast_shadows")]
    check(len(lights) == len(spots) and len(spots) <= 4 and not shadowed,
          "the dressing's real lights: a handful, movable, no shadows",
          "{0} lights, {1} planned, {2} casting shadows".format(len(lights), len(spots), len(shadowed)))
    shots = {label for label in actors if label.startswith(dress.SHOT_PREFIX)}
    check(shots == {dress.SHOT_PREFIX + s[0] for s in kit.shot_cameras(doc)}, "the dressing's shot cameras",
          ", ".join(sorted(shots)))


def _landing_quad(actor, margin=0.0):
    loc = actor.get_actor_location()
    return gen._landing_corners(loc.x, loc.y, actor.get_actor_rotation().yaw, margin)


def check_fire_escapes(district, actors, spawner):
    """Fire escapes: generator match, heights, clearances, rail ledges, material."""
    expected = gen.fire_escape_spots(district)
    records = {r["id"]: r for r in district.buildings}
    landings = list(spawner.get_spawned_fire_escapes()) if spawner is not None else []
    rail_ledges = list(spawner.get_spawned_fire_escape_ledges()) if spawner is not None else []
    want_labels = {gen.FIRE_ESCAPE_PREFIX + "{0}_{1}".format(e[0], e[1]) for e in expected}
    labels = {a.get_actor_label() for a in landings}
    heights = sorted({records[e[0]]["height_m"] for e in expected})
    check(labels == want_labels and len(landings) == len(expected),
          "fire-escape landings match the generator",
          "{0} landings on {1} buildings ({2} expected); buildings {3:.1f} to {4:.1f} m tall{5}".format(
              len(landings), len({e[0] for e in expected}), len(expected),
              heights[0] if heights else 0.0, heights[-1] if heights else 0.0,
              "; unexpected " + ", ".join(sorted(labels - want_labels)[:3]) + "; missing "
              + ", ".join(sorted(want_labels - labels)[:3]) if labels != want_labels else ""))
    eligible = sum(1 for r in district.buildings
                   if gen.FIRE_ESCAPE_MIN_HEIGHT_M <= r["height_m"] <= gen.FIRE_ESCAPE_MAX_HEIGHT_M)
    unreal.log("[Hawkeye] info  fire escapes on {0} of {1} buildings 10 to 30 m tall; {2} parts drawn".format(
        len({e[0] for e in expected}), eligible, spawner.get_fire_escape_instance_count() if spawner else 0))

    rings = {r["id"]: geo.clean_ring(district.ring_cm(r["outer"]), min_edge=5.0, collinear_tol=2.0)
             for r in district.buildings}
    low, off_floor, off_facade, high = [], [], [], []
    by_building = {}
    for a in landings:
        osm = tag_value(a, "osm:")
        floor = int(tag_value(a, "floor:") or 0)
        loc = a.get_actor_location()
        if loc.z < gen.FIRE_ESCAPE_FLOOR - 0.5:
            low.append("{0} {1:.0f}".format(a.get_actor_label(), loc.z))
        if abs(loc.z - floor * gen.FIRE_ESCAPE_FLOOR) > 0.5:
            off_floor.append(a.get_actor_label())
        if osm in rings and closest_point_on_ring((loc.x, loc.y), rings[osm]) > 5.0:
            off_facade.append(a.get_actor_label())
        if osm in records and loc.z > records[osm]["height_m"] * 100.0 - gen.FIRE_ESCAPE_ROOF_CLEARANCE + 0.5:
            high.append(a.get_actor_label())
        by_building.setdefault(osm, []).append(floor)
    gaps = [osm for osm, floors in by_building.items() if sorted(floors) != list(range(1, len(floors) + 1))]
    check(landings and not low, "no fire-escape landing below 330 cm (the outer edge clears the sidewalk)",
          "; ".join(low[:5]))
    check(not off_floor and not gaps, "landings every 330 cm from the second floor, no floor missing",
          ", ".join((off_floor + gaps)[:5]))
    check(not off_facade, "every landing sits on its building's facade line (within 5 cm)", ", ".join(off_facade[:5]))
    check(not high, "every top landing is at least {0:.0f} cm under its roof".format(gen.FIRE_ESCAPE_ROOF_CLEARANCE),
          ", ".join(high[:5]))

    # Lamps: every pole and head against every landing at or below the pole's height.
    poles = [(a.get_actor_location(), a) for l, a in actors.items() if l.startswith(gen.LAMP_POLE_PREFIX)]
    heads = [(a.get_actor_location(), a) for l, a in actors.items() if l.startswith(gen.LAMP_HEAD_PREFIX)]
    near_lamp = []
    for a in landings:
        loc = a.get_actor_location()
        quad = _landing_quad(a)
        for p, pole in poles:
            if loc.z - gen.FIRE_ESCAPE_SLAB[2] <= p.z + gen.LAMP_POLE_HEIGHT and abs(p.x - loc.x) < 400 and abs(p.y - loc.y) < 400 \
                    and gen._point_quad_distance((p.x, p.y), quad) < gen.LAMP_POLE_DIAMETER * 0.5 + 5.0:
                near_lamp.append("{0} / {1}".format(a.get_actor_label(), pole.get_actor_label()))
        for h, head in heads:
            if abs(h.z - (loc.z + gen.FIRE_ESCAPE_RAIL * 0.5)) < gen.FIRE_ESCAPE_RAIL + 30.0 \
                    and abs(h.x - loc.x) < 400 and abs(h.y - loc.y) < 400 \
                    and gen._point_quad_distance((h.x, h.y), quad) < 25.0:
                near_lamp.append("{0} / {1}".format(a.get_actor_label(), head.get_actor_label()))
    check(not near_lamp, "no fire-escape landing intersects a street lamp", "; ".join(near_lamp[:5]))

    # Landings against each other: same floor height, different buildings, bucketed by 5 m.
    grid = {}
    for a in landings:
        loc = a.get_actor_location()
        grid.setdefault((int(loc.x // 500), int(loc.y // 500), int(round(loc.z))), []).append(a)
    overlaps = []
    for (gx, gy, gz), items in grid.items():
        for a in items:
            qa = _landing_quad(a)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for b in grid.get((gx + dx, gy + dy, gz), []):
                        if b.get_actor_label() <= a.get_actor_label():
                            continue
                        if gen._rects_overlap(qa, _landing_quad(b)):
                            overlaps.append("{0} / {1}".format(a.get_actor_label(), b.get_actor_label()))
    check(not overlaps, "no two fire-escape landings intersect", "; ".join(overlaps[:5]))

    # The rail ledges: one per landing, Ledge_1 on the outer top rail (2 cm proud), hidden, trace only.
    wrong = []
    channel = gen.traversable_channel()
    for landing, ledge in zip(landings, rail_ledges):
        if ledge is None:
            wrong.append(landing.get_actor_label() + " no ledge")
            continue
        spline = next((comp for comp in ledge.get_components_by_class(unreal.SplineComponent)
                       if comp.get_name() == gen.LEDGE_SPLINE), None)
        if spline is None or spline.get_number_of_spline_points() < 2:
            wrong.append(landing.get_actor_label() + " no spline")
            continue
        t = landing.get_actor_transform()
        outer = gen.FIRE_ESCAPE_GAP + gen.FIRE_ESCAPE_SLAB[1] + 2.0
        half = gen.FIRE_ESCAPE_SLAB[0] * 0.5
        want = [t.transform_location(unreal.Vector(half, outer, gen.FIRE_ESCAPE_RAIL)),
                t.transform_location(unreal.Vector(-half, outer, gen.FIRE_ESCAPE_RAIL))]
        ends = [spline.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)
                for i in (0, spline.get_number_of_spline_points() - 1)]
        error = max(max(abs(e.x - w.x), abs(e.y - w.y), abs(e.z - w.z)) for e, w in zip(ends, want))
        if error > LEDGE_TOLERANCE_CM:
            wrong.append("{0} {1:.1f} cm".format(landing.get_actor_label(), error))
        for mesh in ledge.get_components_by_class(unreal.StaticMeshComponent):
            if mesh.get_editor_property("visible") or (channel is not None and mesh.get_collision_response_to_channel(
                    unreal.CollisionChannel.ECC_PAWN) != unreal.CollisionResponseType.ECR_IGNORE):
                wrong.append(landing.get_actor_label() + " ledge visible or blocks Pawn")
                break
    check(len(rail_ledges) == len(landings) and not wrong,
          "every landing has its traversable ledge on the outer top rail (within 5 cm), hidden, trace only",
          "; ".join(wrong[:5]))
    material = spawner.get_editor_property("fire_escape_material") if spawner is not None else None
    check(material is not None and "M_SteelPainted" in c.safe_name(material) and spawner.get_fire_escape_instance_count() > 0,
          "fire-escape bars drawn in M_SteelPainted", c.safe_name(material))


NAV_QUERY_EXTENT = unreal.Vector(50.0, 50.0, 150.0)   # cm; how far off a foot may be from the navmesh


def check_beacons(district, actors, records):
    poles = sorted(l for l in actors if l.startswith(gen.BEACON_PREFIX))
    tops = sorted(l for l in actors if l.startswith(gen.BEACON_TOP_PREFIX))
    lights = sorted(l for l in actors if l.startswith(gen.BEACON_LIGHT_PREFIX))
    want = [gen.BEACON_PREFIX + oid for oid in gen.OBJECTIVE_IDS]
    check(sorted(want) == poles, "{0} objective beacons".format(len(want)),
          "{0} found: {1}".format(len(poles), ", ".join(poles)))

    spots = gen.beacon_spots(district)
    cap = c.load_or_none(gen.MI_BEACON)
    problems = []
    for oid in gen.OBJECTIVE_IDS:
        pole = actors.get(gen.BEACON_PREFIX + oid)
        top = actors.get(gen.BEACON_TOP_PREFIX + oid)
        light = actors.get(gen.BEACON_LIGHT_PREFIX + oid)
        spot = spots.get(oid)
        if pole is None or top is None or light is None or spot is None:
            problems.append("{0} incomplete".format(oid))
            continue
        base = pole.get_actor_location().z - (gen.BEACON_HEIGHT - gen.BEACON_CAP) * 0.5
        if abs(base - spot[2]) > 1.0 or abs(pole.get_actor_location().x - spot[0]) > 1.0                 or abs(pole.get_actor_location().y - spot[1]) > 1.0:
            problems.append("{0} pole not on its roof spot".format(oid))
        overrides = top.get_editor_property("static_mesh_component").get_editor_property("override_materials")
        if cap is None or len(overrides) < 1 or overrides[0] != cap:
            problems.append("{0} cap not MI_ObjectiveBeacon".format(oid))
        comp = light.get_editor_property("point_light_component")
        if comp.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
            problems.append("{0} light not movable".format(oid))
        if comp.get_editor_property("intensity_units") != unreal.LightUnits.LUMENS                 or abs(comp.get_editor_property("intensity") - gen.BEACON_LUMENS) > 0.5:
            problems.append("{0} light not {1:.0f} lm".format(oid, gen.BEACON_LUMENS))
    check(not problems and len(tops) == len(want) and len(lights) == len(want),
          "each beacon has its emissive cap and a movable {0:.0f} lm light on its roof".format(gen.BEACON_LUMENS),
          "; ".join(problems) or "{0} caps, {1} lights".format(len(tops), len(lights)))


def build_navigation():
    """Builds the district's navmesh in this editor world (blocking), as the game mode does at
    BeginPlay, through UHawkeyeNavigationLibrary (the editor's async-load lock would refuse a
    plain RebuildNavigation here). Returns (world, built). Nothing is saved."""
    world = c.editor_world()
    lib = getattr(unreal, "HawkeyeNavigationLibrary", None)
    if world is None or lib is None:
        return world, False
    return world, bool(lib.build_navigation_now(world))


def weapon_name(value):
    """ThugWeapon.FISTS -> FISTS, whatever the enum's repr looks like."""
    name = getattr(value, "name", None)
    return str(name if name else value).split(".")[-1].split(":")[0].strip("<> ").upper()


def check_thugs(district, actors, records):
    """Five City_Thug_<n> with one gunner and one heavy, the street pair's patrol points 40 m apart and
    the heavy's 20 m apart, the roof pair on the cross_block roof, the clear_roof group, and every thug's
    feet on the navmesh."""
    thugs = {l: a for l, a in actors.items() if l.startswith(gen.THUG_PREFIX)}
    points = {l: a for l, a in actors.items() if l.startswith(gen.PATROL_PREFIX)}
    wanted, wanted_points, roof_rec = gen.thug_placements(district)
    check(sorted(thugs) == sorted(t[0] for t in wanted) and len(thugs) == 5, "five chapter-1 thugs placed",
          "{0} thugs: {1}".format(len(thugs), ", ".join(sorted(thugs))))
    check(len(points) == 4 and all(isinstance(a, unreal.TargetPoint) for a in points.values()),
          "four patrol points (ATargetPoint)", ", ".join(sorted(points)))
    for first, second, length, what in ((0, 1, gen.PATROL_LENGTH, "street pair's patrol points 40 m apart"),
                                        (2, 3, gen.HEAVY_PATROL_LENGTH, "heavy's patrol points 20 m apart")):
        la, lb = gen.PATROL_PREFIX + str(first), gen.PATROL_PREFIX + str(second)
        if la in points and lb in points:
            a, b = points[la].get_actor_location(), points[lb].get_actor_location()
            gap = math.hypot(a.x - b.x, a.y - b.y)
            check(abs(gap - length) <= 1.0, what, "{0:.1f} cm".format(gap))
        else:
            check(False, what, "missing " + la + " or " + lb)

    weapons = {}
    detail = []
    for label, actor in sorted(thugs.items()):
        weapon = weapon_name(actor.get_editor_property("weapon"))
        weapons[weapon] = weapons.get(weapon, 0) + 1
        tags = [str(t) for t in actor.get_editor_property("tags")]
        patrol = [p.get_actor_label() for p in actor.get_editor_property("patrol_points") if p]
        loc = actor.get_actor_location()
        detail.append("{0} {1} {2} ({3:.0f}, {4:.0f}, {5:.0f}){6}".format(
            label, weapon.lower(), "/".join(t for t in tags if t.endswith("Pair") or t.endswith("Group")), loc.x, loc.y, loc.z,
            " patrol " + ">".join(patrol) if patrol else ""))
    unreal.log("[Hawkeye] info  thugs: " + "; ".join(detail))
    untagged = [l for l, a in thugs.items() if "Thug" not in [str(t) for t in a.get_editor_property("tags")]]
    check(not untagged, "every thug keeps the Thug tag (takedowns and friendly swings use it)", ", ".join(untagged))
    check(weapons.get("PISTOL", 0) == 1 and weapons.get("BAT", 0) == 2 and weapons.get("FISTS", 0) == 1
          and weapons.get("SHIELD", 0) == 1, "one gunner, two bats, one fists, one heavy", str(weapons))
    heavy = thugs.get(gen.THUG_PREFIX + "4")
    heavy_class = heavy.get_class().get_name() if heavy is not None else ""
    check(heavy_class.startswith(gen.HEAVY_BP_NAME), "City_Thug_4 is BP_Thug_Heavy", heavy_class)
    street = sorted(l for l, a in thugs.items() if gen.STREET_GROUP_TAG in [str(t) for t in a.get_editor_property("tags")])
    check(street == sorted(t[0] for t in wanted if gen.STREET_GROUP_TAG in t[6]) and len(street) == 3,
          "the bat, the gunner and the heavy are tagged StreetGroup", ", ".join(street))

    roof_pair = [a for a in thugs.values() if "RoofPair" in [str(t) for t in a.get_editor_property("tags")]]
    on_roof = roof_rec is not None and len(roof_pair) == 2 and all(
        abs(a.get_actor_location().z - (roof_rec["height_m"] * 100.0 + gen.THUG_HALF_HEIGHT + 2.0)) <= 1.0
        and geo.point_in_polygon((a.get_actor_location().x, a.get_actor_location().y), roof_rec["ring"])
        for a in roof_pair)
    check(on_roof, "the RoofPair stands on the cross_block roof",
          "osm {0}".format(roof_rec["id"] if roof_rec else "none"))
    group = actors.get(gen.THUG_GROUP_LABEL)
    check(group is not None and str(group.get_editor_property("objective_id")) == "clear_roof"
          and str(group.get_editor_property("group_tag")) == "RoofPair",
          gen.THUG_GROUP_LABEL + " completes clear_roof for the RoofPair")

    world, built = build_navigation()
    check(built, "navmesh builds in the editor world (UHawkeyeNavigationLibrary)")
    if not built:
        return
    off = []
    for label, actor in sorted(thugs.items()):
        feet = actor.get_actor_location() - unreal.Vector(0.0, 0.0, gen.THUG_HALF_HEIGHT)
        result = unreal.HawkeyeNavigationLibrary.project_to_navigation(world, feet, NAV_QUERY_EXTENT)
        # Python hands a bool-returning function with an out parameter back either as
        # (ok, point) or as the point itself / None; take both.
        if isinstance(result, tuple):
            ok, point = bool(result[0]), result[1]
        else:
            ok, point = result is not None, result
        if not ok:
            off.append(label)
        else:
            unreal.log("[Hawkeye] info  {0} feet {1:.0f} -> navmesh ({2:.0f}, {3:.0f}, {4:.0f})".format(
                label, feet.z, point.x, point.y, point.z))
    check(not off, "every thug starts on the navmesh", ", ".join(off))


def check_chapter_end(district, actors):
    """City_ChapterEndTower on the find_arrow roof, City_ChapterEnd in its tank completing find_arrow
    with the arrow meshes, and City_SceneReturn_FB00 tagged with its own label."""
    spot = gen.chapter_end_spot(district)
    tower = actors.get(gen.CHAPTER_END_TOWER_LABEL)
    arrow = actors.get(gen.CHAPTER_END_LABEL)
    back = actors.get(gen.SCENE_RETURN_LABEL)
    cls = c.find_class("ChapterEndInteractable", "/Script/Hawkeye.ChapterEndInteractable")
    problems = []
    if spot is None:
        problems.append("no spot on the find_arrow roof")
    if tower is None or arrow is None or back is None:
        problems.append("missing: " + ", ".join(l for l, a in ((gen.CHAPTER_END_TOWER_LABEL, tower),
                                                             (gen.CHAPTER_END_LABEL, arrow),
                                                             (gen.SCENE_RETURN_LABEL, back)) if a is None))
    if not problems:
        tx, ty = spot["tower"]
        loc = tower.get_actor_location()
        if abs(loc.x - tx) > 1.0 or abs(loc.y - ty) > 1.0 or abs(loc.z - spot["roof_z"]) > 1.0:
            problems.append("tower not on its roof spot")
        mesh = tower.get_editor_property("static_mesh_component").get_editor_property("static_mesh")
        if mesh is None or "SM_City_WaterTower" not in mesh.get_name():
            problems.append("tower is not SM_City_WaterTower")
        if cls is None or not isinstance(arrow, cls):
            problems.append("City_ChapterEnd is not an AChapterEndInteractable")
        elif str(arrow.get_editor_property("objective_id")) != "find_arrow":
            problems.append("City_ChapterEnd does not complete find_arrow")
        else:
            prop = arrow.get_editor_property("prop").get_editor_property("static_mesh")
            accent = arrow.get_editor_property("prop_accent").get_editor_property("static_mesh")
            if prop is None or accent is None or "ArrowShaft" not in prop.get_name() or "ArrowFletching" not in accent.get_name():
                problems.append("arrow meshes not set")
            dist = math.hypot(arrow.get_actor_location().x - tx, arrow.get_actor_location().y - ty)
            if abs(dist - gen.TOWER_TANK_RADIUS) > 1.0:
                problems.append("arrow not on the tank ({0:.0f} cm from its axis)".format(dist))
        if gen.SCENE_RETURN_LABEL not in [str(t) for t in back.get_editor_property("tags")]:
            problems.append("return point not tagged with its label")
    check(not problems, "the chapter end: the arrow in the find_arrow water tower, and the scene return point",
          "; ".join(problems))


def check_archers(district, actors):
    """Two City_Archer_<n> (BP_Archer) where the generator puts them, on the navmesh, with a traced
    line to the find_arrow roof."""
    archers = {l: a for l, a in actors.items() if l.startswith(gen.ARCHER_PREFIX)}
    wanted = gen.archer_placements(district)
    check(sorted(archers) == sorted(w[0] for w in wanted) and len(wanted) == 2, "two archers placed",
          ", ".join(sorted(archers)))
    target = gen.archer_target(district)
    world = c.editor_world()
    detail = []
    bad = []
    for label, x, y, z, _yaw, osm, distance in wanted:
        actor = archers.get(label)
        if actor is None:
            bad.append(label + " missing")
            continue
        loc = actor.get_actor_location()
        tags = [str(t) for t in actor.get_editor_property("tags")]
        ok = (gen.ARCHER_BP_NAME in c.class_name(actor.get_class()) and weapon_name(actor.get_editor_property("weapon")) == "BOW"
              and gen.ARCHER_PAIR_TAG in tags and "Thug" in tags and abs(loc.x - x) <= 1.0 and abs(loc.y - y) <= 1.0
              and abs(loc.z - z) <= 1.0 and gen.ARCHER_MIN_DISTANCE <= distance <= gen.ARCHER_MAX_DISTANCE)
        # The generator's line is from footprints; this one is the level's own collision.
        eye = unreal.Vector(loc.x, loc.y, loc.z - gen.THUG_HALF_HEIGHT + gen.ARCHER_EYE)
        chest = unreal.Vector(target[0], target[1], target[2])
        visibility = getattr(unreal.TraceTypeQuery, "ECC_VISIBILITY", None) or unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
        hit = unreal.SystemLibrary.line_trace_single(world, eye, chest, visibility, False,
                                                     [actor], unreal.DrawDebugTrace.NONE, True)
        blocked = hit is not None and bool(hit.to_tuple()[0]) if hasattr(hit, "to_tuple") else bool(hit)
        blocker = ""
        if blocked and hasattr(hit, "to_tuple"):
            parts = hit.to_tuple()
            blocker = next((p.get_actor_label() for p in parts if isinstance(p, unreal.Actor)), "?")
        if not ok or blocked:
            bad.append("{0}{1}".format(label, " line blocked by " + blocker if blocked else " misplaced"))
        detail.append("{0} osm {1} at ({2:.0f}, {3:.0f}, {4:.0f}) {5:.0f} cm{6}".format(
            label, osm, loc.x, loc.y, loc.z, distance, " BLOCKED" if blocked else ""))
    unreal.log("[Hawkeye] info  archers: " + "; ".join(detail))
    check(not bad, "archers are BP_Archer with a bow, 15 to 25 m from find_arrow, with a clear traced line", ", ".join(bad))
    world, built = build_navigation()
    if not built:
        return
    off = []
    for label, actor in sorted(archers.items()):
        feet = actor.get_actor_location() - unreal.Vector(0.0, 0.0, gen.THUG_HALF_HEIGHT)
        result = unreal.HawkeyeNavigationLibrary.project_to_navigation(world, feet, NAV_QUERY_EXTENT)
        ok = bool(result[0]) if isinstance(result, tuple) else result is not None
        if not ok:
            off.append(label)
    check(not off, "every archer starts on the navmesh", ", ".join(off))


def check_clint(district, actors):
    """City_ClintStart and City_Clint (BP_Clint) 5 m behind the PlayerStart, his feet on the navmesh."""
    marker = actors.get(gen.CLINT_START_LABEL)
    clint = actors.get(gen.CLINT_LABEL)
    check(isinstance(marker, unreal.TargetPoint), gen.CLINT_START_LABEL + " placed (ATargetPoint)")
    check(clint is not None and gen.CLINT_BP_NAME in c.class_name(clint.get_class()), gen.CLINT_LABEL + " is BP_Clint",
          c.class_name(clint.get_class()) if clint is not None else "missing")
    start = next((a for a in c.all_level_actors() if isinstance(a, unreal.PlayerStart)), None)
    if clint is None or start is None:
        return
    loc, s_loc = clint.get_actor_location(), start.get_actor_location()
    gap = math.hypot(loc.x - s_loc.x, loc.y - s_loc.y)
    check(abs(gap - gen.CLINT_BEHIND_START) <= 1.0, "Clint 5 m from the PlayerStart", "{0:.1f} cm".format(gap))
    world, built = build_navigation()
    if not built:
        return
    feet = loc - unreal.Vector(0.0, 0.0, gen.CLINT_HALF_HEIGHT)
    result = unreal.HawkeyeNavigationLibrary.project_to_navigation(world, feet, NAV_QUERY_EXTENT)
    ok = bool(result[0]) if isinstance(result, tuple) else result is not None
    check(ok, "Clint starts on the navmesh", "feet z {0:.0f}".format(feet.z))


def check_lucky(district, actors):
    """City_Lucky (ALuckyCharacter) beside the PlayerStart, his feet on the navmesh."""
    lucky = actors.get(gen.LUCKY_LABEL)
    check(lucky is not None and "LuckyCharacter" in c.class_name(lucky.get_class()), gen.LUCKY_LABEL + " is ALuckyCharacter",
          c.class_name(lucky.get_class()) if lucky is not None else "missing")
    if lucky is None:
        return
    x, y, _yaw = gen.lucky_start_transform(district)
    loc = lucky.get_actor_location()
    off = math.hypot(loc.x - x, loc.y - y)
    check(off <= 1.0, "Lucky on his spot beside the PlayerStart", "{0:.1f} cm off".format(off))
    world, built = build_navigation()
    if not built:
        return
    feet = loc - unreal.Vector(0.0, 0.0, gen.LUCKY_HALF_HEIGHT)
    result = unreal.HawkeyeNavigationLibrary.project_to_navigation(world, feet, NAV_QUERY_EXTENT)
    ok = bool(result[0]) if isinstance(result, tuple) else result is not None
    check(ok, "Lucky starts on the navmesh", "feet z {0:.0f}".format(feet.z))


def _check_one_safehouse(actor, spot, label, safehouse_id, name, what):
    """The checks both safehouses share: placed on its facade point, facing out, on the pavement,
    tagged, the purple door, its id, its placeholder name and its address. Returns the location."""
    ok = check(actor is not None and "Safehouse" in c.class_name(actor.get_class()), label + " placed (ASafehouse)",
               c.class_name(actor.get_class()) if actor is not None else "missing")
    check(spot is not None, what)
    if not ok or spot is None:
        return None
    loc = actor.get_actor_location()
    off = math.hypot(loc.x - spot["x"], loc.y - spot["y"])
    check(off <= 1.0, label + " door on its facade point", "{0:.1f} cm off, osm {1}".format(off, spot["rec"]["id"]))
    check(abs(((actor.get_actor_rotation().yaw - spot["yaw"]) + 180.0) % 360.0 - 180.0) <= 0.5,
          label + " faces out of the building", "yaw {0:.1f}".format(actor.get_actor_rotation().yaw))
    check(gen.SIDEWALK_TOP - 30.0 <= loc.z <= gen.SIDEWALK_TOP + 30.0, label + " stands on the pavement",
          "z {0:.0f}".format(loc.z))
    tags = [str(t) for t in actor.get_editor_property("tags")]
    check("osm:" + spot["rec"]["id"] in tags, label + " tagged with its building", ", ".join(tags))
    door = actor.get_editor_property("door")
    mats = door.get_editor_property("override_materials") if door is not None else []
    check(len(mats) >= 1 and mats[0] is not None and mats[0].get_name() == gen.MI_BEACON.split("/")[-1],
          label + " door is the purple beacon material", mats[0].get_name() if len(mats) >= 1 and mats[0] else "none")
    check(str(actor.get_editor_property("safehouse_id")) == safehouse_id, label + " id " + safehouse_id)
    shown = str(actor.get_editor_property("display_name"))
    check(shown == name, label + " is named " + name, shown)
    address = str(actor.get_editor_property("address"))
    check(bool(address), label + " has an address subtitle", address or "empty")
    unreal.log("[Hawkeye] info  {0}: osm {1} ({2}) at ({3:.0f}, {4:.0f}, {5:.0f}) yaw {6:.0f}".format(
        label, spot["rec"]["id"], address, loc.x, loc.y, loc.z, actor.get_actor_rotation().yaw))
    return loc


def check_safehouse(district, actors):
    """City_Safehouse on the storefront facing the park and City_Safehouse_2 on Avenue B, east of the
    avenue in the north half, at least 250 m from the first; both with the door, sign and light."""
    first = _check_one_safehouse(actors.get(gen.SAFEHOUSE_LABEL), gen.safehouse_spot(district), gen.SAFEHOUSE_LABEL,
                                 gen.SAFEHOUSE_ID, gen.SAFEHOUSE_NAME, "a building fronting the park qualifies for the safehouse")
    spot2 = gen.safehouse2_spot(district)
    second = _check_one_safehouse(actors.get(gen.SAFEHOUSE2_LABEL), spot2, gen.SAFEHOUSE2_LABEL, gen.SAFEHOUSE2_ID,
                                  gen.SAFEHOUSE2_NAME, "an Avenue B storefront qualifies for the second safehouse")
    if first is None or second is None:
        return
    apart = math.hypot(first.x - second.x, first.y - second.y)
    check(apart >= gen.SAFEHOUSE2_MIN_APART, "the safehouses are at least 250 m apart", "{0:.1f} m".format(apart / 100.0))
    _pieces, mid, north, east = gen._avenue_frame(district, gen.SAFEHOUSE2_AVENUE)
    rel = (second.x - mid[0], second.y - mid[1])
    along, across = rel[0] * north[0] + rel[1] * north[1], rel[0] * east[0] + rel[1] * east[1]
    check(across > 0.0 and along > 0.0, "the second is east of Avenue B in the district's north half",
          "{0:.0f} m north of the avenue's middle, {1:.0f} m east of its line".format(along / 100.0, across / 100.0))
    escapes = [f for f in gen.fire_escape_spots(district)
               if math.hypot(f[2][0] - second.x, f[2][1] - second.y) < gen.SAFEHOUSE2_ESCAPE_CLEAR]
    check(not escapes, "no fire escape over the second safehouse's sign", ", ".join(f[0] for f in escapes))
    ids = [str(a.get_editor_property("safehouse_id")) for label, a in actors.items() if label.startswith(gen.SAFEHOUSE_LABEL)]
    check(len(ids) == 2 and len(set(ids)) == 2, "two safehouses with different ids", ", ".join(ids))


def _vec3(v):
    return (v.x, v.y, v.z)


def check_challenges(district, actors):
    """The side challenges: three archery ranges and three traversal routes as create_challenges.py plans
    them, a City_Challenge_<id> pedestal at each start holding its definition, and every target and
    checkpoint in reach: targets 10 to 40 m from where she shoots with clear lines to their faces;
    each ring within 2500 cm of the one before on foot, or at the landing point of an anchor in grapple
    range with a clear line (the footprint rule of the lap's anchor survey), or at the foot of the fire
    escape of the roof before it."""
    import create_challenges as cc  # noqa: E402 - imports generate_city, already loaded
    definitions = gen.challenge_definitions()
    plans = {p["id"]: p for p in cc.plan_challenges(district)}
    world = cc.planning_world(district)
    kinds = [weapon_name(d.get_editor_property("type")) for d in definitions.values()]
    archery_n = sum(1 for k in kinds if k.endswith("ARCHERY"))
    check(archery_n == cc.ARCHERY_COUNT and len(definitions) - archery_n == cc.TRAVERSAL_COUNT,
          "{0} archery and {1} traversal challenges".format(cc.ARCHERY_COUNT, cc.TRAVERSAL_COUNT),
          "{0} and {1}: {2}".format(archery_n, len(definitions) - archery_n, ", ".join(sorted(definitions))))
    check(sorted(definitions) == sorted(plans), "challenge assets match the plan of create_challenges",
          "assets {0}, planned {1}".format(sorted(definitions), sorted(plans)))

    pedestals = {l: a for l, a in actors.items() if l.startswith(gen.CHALLENGE_PREFIX)}
    bad = []
    for cid, definition in sorted(definitions.items()):
        actor = pedestals.get(gen.CHALLENGE_PREFIX + cid)
        start = definition.get_editor_property("start_location")
        if actor is None or "ChallengeStart" not in c.class_name(actor.get_class()):
            bad.append(cid + " missing")
            continue
        loc = actor.get_actor_location()
        if actor.get_editor_property("definition") != definition:
            bad.append(cid + " holds another definition")
        if math.hypot(loc.x - start.x, loc.y - start.y) > 1.0 or abs(loc.z - start.z) > 30.0:
            bad.append("{0} at ({1:.0f}, {2:.0f}, {3:.0f}), start ({4:.0f}, {5:.0f}, {6:.0f})".format(
                cid, loc.x, loc.y, loc.z, start.x, start.y, start.z))
        if abs(((actor.get_actor_rotation().yaw - definition.get_editor_property("start_yaw")) + 180.0) % 360.0 - 180.0) > 0.5:
            bad.append(cid + " turned wrong")
    check(len(pedestals) == len(definitions) and not bad,
          "one City_Challenge_ pedestal per challenge, on its start, holding it",
          "{0} pedestals; {1}".format(len(pedestals), "; ".join(bad) or "all placed"))

    for cid, definition in sorted(definitions.items()):
        if weapon_name(definition.get_editor_property("type")) == "ARCHERY":
            _check_archery(cc, world, cid, definition)
        else:
            _check_traversal(cc, world, cid, definition)


CRIME_ROSTERS = {"mugging": 2, "robbery": 3, "ambush": 4, "rooftop": 3}


def _project(world, point):
    result = unreal.HawkeyeNavigationLibrary.project_to_navigation(world, point, NAV_QUERY_EXTENT)
    if isinstance(result, tuple):
        return bool(result[0]), result[1]
    return result is not None, result


def check_crimes(district, actors):
    """The street crimes: four DA_Crime_ definitions with their rosters, twelve City_CrimeSpot_ as
    create_crimes.py plans them (eight street, four roof), each holding its crimes, none within 40 m of
    the safehouse or a challenge pedestal, every two 35 m apart, every spot on the navmesh, and every
    street spot's escape point on the navmesh with a walkable path from the spot."""
    import create_crimes as cr  # noqa: E402 - imports generate_city, already loaded
    definitions = cr.crime_definitions()
    rosters = {}
    for cid, definition in definitions.items():
        roster = definition.get_editor_property("roster") or []
        rosters[cid] = sum(int(e.get_editor_property("count")) for e in roster)
    check(rosters == CRIME_ROSTERS, "four crimes: mugging 2 thugs, robbery 3, ambush 4, rooftop 3", str(rosters))
    rooftop = definitions.get("rooftop")
    bows = [e for e in (rooftop.get_editor_property("roster") if rooftop else [])
            if weapon_name(e.get_editor_property("weapon")) == "BOW"]
    check(len(bows) == 1 and "BP_Archer" in cr._path(bows[0].get_editor_property("thug_class")),
          "the rooftop crime has one archer (BP_Archer)")
    mugging = definitions.get("mugging")
    victim = cr._path(mugging.get_editor_property("victim_class")) if mugging else ""
    check(victim.endswith(cr.CIVILIAN_BP), "the mugging's victim is BP_Civilian", victim)

    plan = cr.plan_crime_spots(district)
    spots = {l: a for l, a in actors.items() if l.startswith(gen.CRIME_SPOT_PREFIX)}
    street = [s for s in plan if s["kind"] == "corner"]
    roofs = [s for s in plan if s["rooftop"]]
    alleys = [s for s in plan if s["kind"] in ("alley", "park")]
    stats = cr.alley_stats(district)
    check(len(street) == 8 and len(roofs) == 4 and cr.ALLEY_SPOTS[0] <= len(alleys) <= cr.ALLEY_SPOTS[1]
          and len(spots) == len(plan),
          "crime spots: eight on street corners, four on roofs, two to four in alleys (or on the park's paths)",
          "{0} planned ({1} corner, {2} roof, {3} alley/park), {4} placed; alley search {5}".format(
              len(plan), len(street), len(roofs), len(alleys), len(spots), stats))
    check(all(s["kind"] == "alley" for s in alleys) or stats.get("alleys", 0) < cr.ALLEY_SPOTS[0],
          "the park's paths stand in only when the block has fewer than two alleys", str(stats))
    bad = []
    for s in plan:
        actor = spots.get(gen.CRIME_SPOT_PREFIX + str(s["index"]))
        if actor is None:
            bad.append("{0} missing".format(s["index"]))
            continue
        loc = actor.get_actor_location()
        held = sorted(str(d.get_editor_property("id")) for d in actor.get_editor_property("crimes") if d)
        if math.hypot(loc.x - s["at"][0], loc.y - s["at"][1]) > 1.0 or held != sorted(s["crimes"]) \
                or bool(actor.get_editor_property("rooftop")) != s["rooftop"] \
                or bool(actor.get_editor_property("alley")) != (s["kind"] == "alley"):
            bad.append("{0} off its plan or holding {1}".format(s["index"], held))
        unreal.log("[Hawkeye] info  crime " + cr.describe(s))
    check(not bad, "every crime spot on its plan, holding its crimes", "; ".join(bad))
    check(all(sorted(s["crimes"]) == sorted(cr.ALLEY_CRIMES) for s in alleys), "alley spots take the mugging and the ambush")

    keep_off = []
    for house_label in (gen.SAFEHOUSE_LABEL, gen.SAFEHOUSE2_LABEL):
        house = actors.get(house_label)
        if house is not None:
            keep_off.append((house_label, house.get_actor_location()))
    for label, actor in actors.items():
        if label.startswith(gen.CHALLENGE_PREFIX):
            keep_off.append((label, actor.get_actor_location()))
    door = gen.interior_keepout(district)
    if door is not None:
        keep_off.append(("the interior door", unreal.Vector(door[0], door[1], 0.0)))
    near = []
    placed = sorted(spots.items())
    for label, actor in placed:
        loc = actor.get_actor_location()
        for what, at in keep_off:
            if math.hypot(loc.x - at.x, loc.y - at.y) < 4000.0:
                near.append("{0} {1:.0f} m from {2}".format(label, math.hypot(loc.x - at.x, loc.y - at.y) / 100.0, what))
    check(not near and len(keep_off) >= 3, "no crime spot within 40 m of a safehouse, a pedestal or the interior door",
          "; ".join(near) or "{0} kept clear of".format(len(keep_off)))
    close = []
    for i, (la, a) in enumerate(placed):
        for lb, b in placed[i + 1:]:
            pa, pb = a.get_actor_location(), b.get_actor_location()
            if math.hypot(pa.x - pb.x, pa.y - pb.y) < cr.SPOT_APART - 1.0:
                close.append(la + "/" + lb)
    check(not close, "every two crime spots at least 35 m apart", ", ".join(close))

    world, built = build_navigation()
    if not built:
        check(False, "navmesh for the crime spots")
        return
    off, routes, walks = [], [], []
    corners = [a for l, a in placed if l in {gen.CRIME_SPOT_PREFIX + str(s["index"]) for s in street}]
    for label, actor in placed:
        ok, _point = _project(world, actor.get_actor_location())
        if not ok:
            off.append(label)
        escape = actor.get_editor_property("escape_location")
        if bool(actor.get_editor_property("rooftop")):
            continue
        if actor not in corners:
            # An alley (or park) spot: no robbery, so no escape point; it must join the streets on the navmesh.
            here = actor.get_actor_location()
            nearest = min(corners, key=lambda a: math.hypot(a.get_actor_location().x - here.x, a.get_actor_location().y - here.y),
                          default=None)
            length = unreal.HawkeyeNavigationLibrary.find_path_length(world, here, nearest.get_actor_location()) if nearest else -1.0
            if length < 0.0:
                walks.append("{0} (no path to {1})".format(label, nearest.get_actor_label() if nearest else "any corner"))
            else:
                unreal.log("[Hawkeye] info  {0} walks {1:.0f} m to {2}".format(label, length / 100.0, nearest.get_actor_label()))
            continue
        length = unreal.HawkeyeNavigationLibrary.find_path_length(world, actor.get_actor_location(), escape)
        straight = math.hypot(escape.x - actor.get_actor_location().x, escape.y - actor.get_actor_location().y)
        if length < 0.0 or not 5000.0 <= straight <= 7000.0:
            routes.append("{0} ({1:.0f} m straight, path {2:.0f} m)".format(label, straight / 100.0, length / 100.0))
        else:
            unreal.log("[Hawkeye] info  {0} escape {1:.0f} m away, {2:.0f} m on foot".format(label, straight / 100.0, length / 100.0))
    check(not off, "every crime spot on the navmesh", ", ".join(off))
    check(not routes, "every street spot's escape point 50 to 70 m out with a walkable path", "; ".join(routes))
    check(not walks, "every alley spot joined to the streets on the navmesh", "; ".join(walks))
    _check_alley_widths(world, plan, spots)


ALLEY_TRACE_STEP = 25.0


def _alley_reach(world, centre, across, sign):
    """cm of open ground from centre along sign * across (a 2D unit), 1 m up, on the level's own collision."""
    visibility = getattr(unreal.TraceTypeQuery, "ECC_VISIBILITY", None) or unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
    reach = 0.0
    while reach < 800.0:
        step = reach + ALLEY_TRACE_STEP
        a = unreal.Vector(centre.x + across[0] * sign * reach, centre.y + across[1] * sign * reach, centre.z + 100.0)
        b = unreal.Vector(centre.x + across[0] * sign * step, centre.y + across[1] * sign * step, centre.z + 100.0)
        hit = unreal.SystemLibrary.line_trace_single(world, a, b, visibility, False, [], unreal.DrawDebugTrace.NONE, True)
        blocked = bool(hit.to_tuple()[0]) if hasattr(hit, "to_tuple") else bool(hit)
        if blocked:
            break
        reach = step
    return reach


def _check_alley_widths(world, plan, spots):
    """Each alley spot stands in 3 to 6 m of open ground across its passage, measured on the level's walls (25 cm
    steps either side, so the measure may read up to 50 cm under the true gap)."""
    import create_crimes as cr  # noqa: E402 - imports generate_city, already loaded
    bad, detail = [], []
    for s in plan:
        actor = spots.get(gen.CRIME_SPOT_PREFIX + str(s["index"]))
        if s["kind"] != "alley" or actor is None:
            continue
        yaw = math.radians(s["yaw"])
        across = (-math.sin(yaw), math.cos(yaw))
        width = sum(_alley_reach(world, actor.get_actor_location(), across, sign) for sign in (1.0, -1.0))
        detail.append("{0} {1:.1f} m".format(actor.get_actor_label(), width / 100.0))
        if not cr.ALLEY_WIDTH[0] - 2.0 * ALLEY_TRACE_STEP <= width <= cr.ALLEY_WIDTH[1] + 1.0:
            bad.append("{0} {1:.1f} m across".format(actor.get_actor_label(), width / 100.0))
    unreal.log("[Hawkeye] info  alley widths: " + ", ".join(detail))
    check(not bad, "every alley spot 3 to 6 m wide across its passage, by the level's walls", "; ".join(bad))


def _check_archery(cc, world, cid, definition):
    start = definition.get_editor_property("start_location")
    yaw = math.radians(definition.get_editor_property("start_yaw"))
    stand = (start.x + math.cos(yaw) * cc.PEDESTAL_BACK, start.y + math.sin(yaw) * cc.PEDESTAL_BACK)
    eye = (stand[0], stand[1], start.z + cc.BOW_HEIGHT)
    tops = world.tops_near(eye[0], eye[1], cc.TARGET_MAX_DISTANCE + 500.0)
    props = world.props_near(eye[0], eye[1], cc.TARGET_MAX_DISTANCE + 500.0)
    landing_feet = [(x + out[0] * cc.LANDING_OUT, y + out[1] * cc.LANDING_OUT, z)
                    for landings in world.escapes.values() for _f, (x, y, z), _yaw, out in landings]
    targets = list(definition.get_editor_property("targets") or [])
    moving, on_landings, problems, dists = 0, 0, [], []
    for index, spawn in enumerate(targets):
        xf = spawn.get_editor_property("transform")
        foot = _vec3(xf.translation)
        centre = (foot[0], foot[1], foot[2] + cc.TARGET_FACE)
        d = math.hypot(foot[0] - eye[0], foot[1] - eye[1])
        dists.append(d / 100.0)
        if bool(spawn.get_editor_property("moving")):
            moving += 1
        if any(math.hypot(foot[0] - lf[0], foot[1] - lf[1]) < 60.0 and abs(foot[2] - lf[2]) < 5.0 for lf in landing_feet):
            on_landings += 1
        if not (cc.TARGET_MIN_DISTANCE - 1.0 <= d <= cc.TARGET_MAX_DISTANCE + 1.0):
            problems.append("target {0} at {1:.0f} m".format(index, d / 100.0))
        if not cc._target_lines_clear(world, eye, centre, tops, props):
            problems.append("target {0} out of sight".format(index))
    check(len(targets) == cc.ARCHERY_TARGETS and moving >= cc.ARCHERY_MIN_KIND and on_landings >= cc.ARCHERY_MIN_KIND and not problems,
          "{0}: 12 targets 10 to 40 m out with clear lines, movers and landing targets".format(cid),
          "{0} targets, {1} moving, {2} on landings, {3:.0f} to {4:.0f} m{5}".format(
              len(targets), moving, on_landings, min(dists) if dists else 0.0, max(dists) if dists else 0.0,
              "; " + "; ".join(problems[:4]) if problems else ""))


def _check_traversal(cc, world, cid, definition):
    start = definition.get_editor_property("start_location")
    rings = [_vec3(t.translation) for t in definition.get_editor_property("checkpoints") or []]
    legs = [weapon_name(l) for l in definition.get_editor_property("checkpoint_legs") or []]
    problems = []
    how = []
    prev = (start.x, start.y, start.z + cc.CHECKPOINT_UP)
    for index, (ring, leg) in enumerate(zip(rings, legs)):
        feet = (ring[0], ring[1], ring[2] - cc.CHECKPOINT_UP)
        prev_feet = (prev[0], prev[1], prev[2] - cc.CHECKPOINT_UP)
        step = math.hypot(ring[0] - prev[0], ring[1] - prev[1])
        if math.hypot(ring[0] - start.x, ring[1] - start.y) > cc.AREA_RADIUS:
            problems.append("ring {0} outside the area".format(index + 1))
        if leg.endswith("GRAPPLE"):
            under = world.building_at((prev[0], prev[1]))
            found = None
            for ax, ay, az, lx, ly, lz, osm in world.anchors:
                if math.hypot(lx - feet[0], ly - feet[1]) > 50.0 or abs(lz - feet[2]) > 5.0:
                    continue
                reach = math.sqrt((ax - prev_feet[0]) ** 2 + (ay - prev_feet[1]) ** 2 + (az - prev_feet[2] - 90.0) ** 2)
                if reach <= cc.GRAPPLE_RANGE and cc.zip_clear(world, prev_feet, (lx, ly, lz), osm, under[0]["id"] if under else None)                         and cc.camera_sees(world, prev_feet, (ax, ay, az), osm):
                    found = (osm, reach)
                    break
            if found is None:
                problems.append("ring {0}: no anchor in range with a clear line".format(index + 1))
            else:
                how.append("{0} zip {1:.0f} cm to {2}".format(index + 1, found[1], found[0]))
        elif leg.endswith("DESCENT"):
            under = world.building_at((prev[0], prev[1]))
            escape = world.escapes.get(under[0]["id"]) if under else None
            ok = False
            if escape:
                _f, (x, y, _z), _yaw, out = escape[-1]
                foot = (x + out[0] * cc.DESCENT_OUT, y + out[1] * cc.DESCENT_OUT)
                ok = math.hypot(foot[0] - ring[0], foot[1] - ring[1]) < 400.0 and feet[2] < 300.0
            if not ok:
                problems.append("ring {0}: not at the foot of the fire escape of the roof before it".format(index + 1))
            else:
                how.append("{0} down the escape of {1}".format(index + 1, under[0]["id"]))
        else:
            if step > cc.GRAPPLE_RANGE:
                problems.append("ring {0} is {1:.0f} cm on foot from the one before".format(index + 1, step))
            if leg.endswith("MANTLE"):
                a, b = world.building_at((prev[0], prev[1])), world.building_at((ring[0], ring[1]))
                rise = (world.roof_z(b) - world.roof_z(a)) if a and b else -1.0
                if not (a and b and a is not b and cc.MANTLE_STEP[0] <= rise <= cc.MANTLE_STEP[1]):
                    problems.append("ring {0}: no step up onto a neighbour".format(index + 1))
                else:
                    how.append("{0} mantle +{1:.0f} cm".format(index + 1, rise))
        prev = ring
    check(len(rings) == 8 and len(legs) == 8 and "GRAPPLE" in legs and "MANTLE" in legs and "DESCENT" in legs
          and not problems,
          "{0}: 8 rings, every one reachable (grapple, mantle and fire-escape legs)".format(cid),
          "; ".join(problems[:4]) if problems else ", ".join(how))


def run():
    if not gen.data_available():
        check(False, "OSM data present", "run Tools\\fetch-osm.ps1")
        return False
    if not c.exists(gen.MAP_PATH):
        check(False, "map exists", gen.MAP_PATH)
        return False
    c.level_editor_subsystem().load_level(gen.MAP_PATH)

    district = gen.District()
    records = {b["id"]: b for b in district.buildings}
    spawner = check_spawner(district, gen.actors_by_label())
    actors = gen.actors_by_label()
    if spawner is not None:
        # Spawned transient, so the editor's actor listing leaves them out; add them by label.
        for actor in list(spawner.get_spawned_ledges()) + list(spawner.get_spawned_anchors()):
            actors[actor.get_actor_label()] = actor
        # Fire escapes go by their own lists (check_fire_escapes); keep their ledges out of the roof-ledge check.
    buildings = {label[len(gen.BUILDING_PREFIX):]: a for label, a in actors.items()
                 if label.startswith(gen.BUILDING_PREFIX)}

    missing = sorted(set(records) - set(buildings))
    strays = sorted(set(buildings) - set(records))
    check(len(buildings) == len(records) and not missing and not strays,
          "building actors match records",
          "{0} actors, {1} records, {2} missing, {3} stray{4}".format(
              len(buildings), len(records), len(missing), len(strays),
              ": " + ", ".join((missing + strays)[:5]) if missing or strays else ""))

    facades = {style: c.load_or_none(m.mi_facade_path(style)) for style in m.FACADE_STYLES}
    no_mesh, no_collision, wrong_height, wrong_material, overhang = [], [], [], [], []
    rows = []
    for rid, actor in sorted(buildings.items()):
        rec = records.get(rid)
        if rec is None:
            continue
        comp = actor.get_editor_property("static_mesh_component")
        sm = comp.get_editor_property("static_mesh")
        if sm is None:
            no_mesh.append(rid)
            continue
        ok, why = has_collision(comp, sm)
        if not ok:
            no_collision.append("{0} ({1})".format(rid, why))
        overrides = comp.get_editor_property("override_materials")
        facade = facades.get(gen.facade_style(rec))
        if facade is None or len(overrides) < 1 or overrides[0] != facade:
            wrong_material.append(rid)
        top, bottom = mesh_top_cm(sm)
        parapet = tag_value(actor, "parapet:") == "1"
        expected = rec["height_m"] * 100.0 + (gen.PARAPET_HEIGHT if parapet else 0.0)
        z0 = actor.get_actor_location().z
        if abs(top - expected) > HEIGHT_TOLERANCE_CM or abs(bottom) > HEIGHT_TOLERANCE_CM or abs(z0) > 0.01:
            wrong_height.append("{0} top {1:.1f} want {2:.1f} base {3:.1f}".format(rid, top, expected, bottom + z0))
        # Nothing stands past the footprint: the parapet's outer faces are the facades (no box overhangs a corner).
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) >= 3:
            loc = actor.get_actor_location()
            x0, y0, x1, y1 = geo.bounds(ring)
            box = sm.get_bounding_box()
            worst = max(x0 - (box.min.x + loc.x), (box.max.x + loc.x) - x1, y0 - (box.min.y + loc.y), (box.max.y + loc.y) - y1)
            if worst > HEIGHT_TOLERANCE_CM:
                overhang.append("{0} {1:.1f} cm".format(rid, worst))
        rows.append((rec["height_m"], rid, rec.get("tags", {}).get("addr:street") or "?",
                     rec.get("tags", {}).get("addr:housenumber") or ""))

    check(not no_mesh, "every building has a static mesh", ", ".join(no_mesh[:5]))
    check(not no_collision, "every building mesh has complex-as-simple collision", ", ".join(no_collision[:5]))
    check(not wrong_height, "building heights match records within 1 cm", "; ".join(wrong_height[:5]))
    check(not wrong_material, "every building wears its MI_Facade_ style", ", ".join(wrong_material[:5]))
    check(not overhang, "no building mesh (parapet included) stands past its footprint by more than 1 cm",
          "{0}: {1}".format(len(overhang), ", ".join(overhang[:5])))

    # Streets and ground
    roads = [a for label, a in actors.items() if label.startswith(gen.ROAD_PREFIX)]
    walks = [a for label, a in actors.items() if label.startswith(gen.SIDEWALK_PREFIX)]
    parks = [a for label, a in actors.items() if label.startswith(gen.PARK_PREFIX)]
    check(gen.GROUND_LABEL in actors, "ground slab present")
    check(len(roads) == len(district.roads), "road actors match records",
          "{0} actors, {1} records".format(len(roads), len(district.roads)))
    check(len(walks) >= len(district.roads) * 0.9, "sidewalk actors present", str(len(walks)))
    check(len(parks) == len(district.parks), "park actors match records", str(len(parks)))

    # Scene
    all_actors = c.all_level_actors()
    nav_class = c.find_class("NavMeshBoundsVolume", "/Script/NavigationSystem.NavMeshBoundsVolume")
    check(any(isinstance(a, unreal.PlayerStart) for a in all_actors), "PlayerStart present")
    check(nav_class is not None and any(isinstance(a, nav_class) for a in all_actors), "NavMeshBoundsVolume present")
    check(any(isinstance(a, unreal.DirectionalLight) for a in all_actors), "directional light present")
    check(any(isinstance(a, unreal.SkyLight) for a in all_actors), "sky light present")
    static_lights = []
    for a in all_actors:
        if isinstance(a, (unreal.Light, unreal.SkyLight)):
            root = a.get_editor_property("root_component")
            if root is not None and root.get_editor_property("mobility") == unreal.ComponentMobility.STATIC:
                static_lights.append(a.get_actor_label())
    check(not static_lights, "no static-mobility lights", ", ".join(static_lights))

    ws = c.world_settings()
    gm = ws.get_editor_property("default_game_mode") if ws else None
    check(gm is not None and gen.DISTRICT_GAME_MODE in c.class_name(gm),
          "GameMode override is " + gen.DISTRICT_GAME_MODE, c.class_name(gm))
    mission = c.load_or_none(gen.MISSION_ASSET)
    starting = None
    if gm is not None:
        try:
            starting = unreal.get_default_object(gm).get_editor_property("starting_mission")
        except Exception:  # noqa: BLE001
            starting = None
    check(mission is not None and starting == mission, "its StartingMission is DA_CH01_NightShift",
          c.safe_name(starting) if starting is not None else "None")

    # Chapter 1 objective volumes: one per objective id, each above a roof.
    volume_class = c.find_class("ObjectiveTriggerVolume", "/Script/Hawkeye.ObjectiveTriggerVolume")
    volumes = [a for a in all_actors if volume_class is not None and isinstance(a, volume_class)]
    by_id = {}
    for v in volumes:
        by_id.setdefault(str(v.get_editor_property("objective_id")), []).append(v)
    mission_ids = []
    if mission is not None:
        for obj in mission.get_editor_property("objectives") or []:
            mission_ids.append(str(obj.get_editor_property("objective_id")))
    check(mission_ids == list(gen.CHAPTER_OBJECTIVE_IDS), "DA_CH01_NightShift objectives are "
          + ", ".join(gen.CHAPTER_OBJECTIVE_IDS), ", ".join(mission_ids))
    detail = []
    ok = len(volumes) == len(gen.VOLUME_OBJECTIVE_IDS)
    for oid in gen.VOLUME_OBJECTIVE_IDS:
        found = by_id.get(oid, [])
        if len(found) != 1:
            ok = False
            detail.append("{0} x{1}".format(oid, len(found)))
            continue
        v = found[0]
        osm = tag_value(v, "osm:")
        rec = records.get(osm)
        z = v.get_actor_location().z
        above = rec is not None and abs(z - (rec["height_m"] * 100.0 + gen.OBJECTIVE_ABOVE_ROOF
                                             + gen.OBJECTIVE_HALF_HEIGHT)) <= 1.0
        ok = ok and above and v.get_actor_label() == gen.OBJECTIVE_PREFIX + oid
        detail.append("{0} on {1}{2}".format(oid, osm, "" if above else " NOT above its roof"))
    check(ok, "objective volumes on reach_roof and cross_block (find_arrow is the chapter end)", "; ".join(detail))
    check_chapter_end(district, actors)

    # Objective beacons: a pole, an emissive cap and a movable 300 lm purple light per volume.
    check_beacons(district, actors, records)

    # Street lamps: each light has its pole and head; shadows only near the park.
    lights = {l[len(gen.LAMP_PREFIX):] for l in actors if l.startswith(gen.LAMP_PREFIX)}
    poles = {l[len(gen.LAMP_POLE_PREFIX):] for l in actors if l.startswith(gen.LAMP_POLE_PREFIX)}
    heads = {l[len(gen.LAMP_HEAD_PREFIX):] for l in actors if l.startswith(gen.LAMP_HEAD_PREFIX)}
    shadowed = sum(1 for l, a in actors.items() if l.startswith(gen.LAMP_PREFIX)
                   and a.get_editor_property("spot_light_component").get_editor_property("cast_shadows"))
    expected = len(gen.lamp_spots(district))
    check(len(lights) == expected and lights == poles == heads, "street lamps match the generator",
          "{0} lights, {1} poles, {2} heads, {3} expected, {4} casting shadows".format(
              len(lights), len(poles), len(heads), expected, shadowed))

    check_anchors(district, actors)
    check_anchor_reach(district)
    check_ledges(district, actors)
    check_fire_escapes(district, actors, spawner)
    check_clutter(district, spawner)
    check_dressing(spawner, actors)
    sky = actors.get(gen.STARS_LABEL)
    sky_comp = sky.get_editor_property("static_mesh_component") if sky is not None else None
    check(sky_comp is not None and sky_comp.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
          and str(sky_comp.get_collision_profile_name()) == "NoCollision",
          gen.STARS_LABEL + " present in M_NightStars with no collision",
          str(sky_comp.get_collision_profile_name()) if sky_comp is not None else "missing")
    check_thugs(district, actors, records)
    check_archers(district, actors)
    check_clint(district, actors)
    check_lucky(district, actors)
    check_safehouse(district, actors)
    check_challenges(district, actors)
    check_crimes(district, actors)

    prison = [a.get_actor_label() for a in all_actors
              if not gen.is_chapter_actor(a.get_actor_label())
              and any(w in gen.actor_class_name(a) for w in gen.PRISON_CLASS_WORDS)]
    check(not prison, "no thug, keycard, door or pickup actors", ", ".join(prison[:5]))
    try:
        wp = ws.get_world_partition() if ws and hasattr(ws, "get_world_partition") else None
    except Exception:  # noqa: BLE001
        wp = None
    unreal.log("[Hawkeye] info  World Partition: {0}".format("ON" if wp else "off"))

    # Handedness: mean X of each avenue and mean Y of the two boundary streets, in Unreal cm.
    def mean_xy(name):
        pts = [district.cm(p) for r in district.roads if r.get("name") == name
               for piece in r["pieces"] for p in piece]
        if not pts:
            return None
        return sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)

    avenues = [mean_xy(n) for n in ("1st Avenue", "Avenue A", "Avenue B", "Avenue C")]
    e6, e11 = mean_xy("East 6th Street"), mean_xy("East 11th Street")
    if all(avenues) and e6 and e11:
        xs = [a[0] for a in avenues]
        order_ok = xs == sorted(xs) and e6[1] > e11[1]
        check(order_ok, "handedness: avenues run 1st, A, B, C west to east and E 6th is south of E 11th",
              "avenue X {0}; E6 Y {1:.0f} vs E11 Y {2:.0f}".format(
                  ", ".join("{0:.0f}".format(x) for x in xs), e6[1], e11[1]))
    else:
        check(False, "handedness streets found")

    rows.sort()
    unreal.log("[Hawkeye] info  tallest: " + "; ".join(
        "{0} {1:.1f} m ({2})".format(r[1], r[0], (r[3] + " " + r[2]).strip() if r[2] != "?" else "no address") for r in reversed(rows[-5:])))
    unreal.log("[Hawkeye] info  shortest: " + "; ".join(
        "{0} {1:.1f} m ({2})".format(r[1], r[0], (r[3] + " " + r[2]).strip() if r[2] != "?" else "no address") for r in rows[:5]))

    if _failures:
        unreal.log_error("[Hawkeye] verify_city FAIL ({0} check(s): {1})".format(len(_failures), "; ".join(_failures)))
        return False
    unreal.log("[Hawkeye] verify_city PASS")
    return True


if __name__ == "__main__":
    run()
