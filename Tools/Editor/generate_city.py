"""Generate the greybox East Village district from OpenStreetMap data.

    /Game/Maps/L_District_EastVillage          the map (World Partition off for one block)
    /Game/City/EastVillage/Meshes/SM_City_*    one static mesh per building, road, sidewalk

Input is what Tools/fetch-osm.ps1 wrote: Tools/Data/osm/east_village.buildings.json and
east_village.streets.json (lat/lon; this script projects them with _geo.py).

What gets built, all with Geometry Script from Python (the GeometryScripting plugin):

* every building: the footprint extruded to its height (append_simple_extrude_polygon, ear
  clipped so concave footprints work; courtyard holes are ignored), a 30 x 90 cm parapet ring
  just inside the roof edge, per-face normals, complex-as-simple collision so the player can
  stand on roofs and walk into walls. Actor ``City_Bldg_<id>`` in the MI_Facade_<style> that
  facade_style picks (brick or brownstone tenements, stone over 30 m, painted grey garages): M_Facade
  draws the window grid, storefronts, cornice and lit windows from world position, snow on top.
* one ground slab under the whole district at z = 0 (``City_Ground``, snowy MI_Prop_Ground).
* per named road way: carriageway at z = +2 cm (``City_Road_<id>``, wet M_StreetAsphalt) and the 4 m
  sidewalks either side at z = +15 cm (``City_Sidewalk_<id>``, M_SidewalkSnow). Sidewalks are
  the road's buffer minus every carriageway, so they stop at the kerb of each crossing street.
* the park at z = +5 cm (``City_Park_<id>``, M_ParkSnow), minus the carriageways.
* a January night: a 0.3 lux blue moon (``Moon``, the sky atmosphere's light) 30 degrees up, the sky
  light, the height fog as night haze, stars on ``City_NightSky`` (M_NightStars on the engine
  sphere), fixed exposure and a purple-shadow, cream-highlight grade with a vignette; a PlayerStart on East 7th Street facing the tenement row across
  from Tompkins Square Park, a NavMeshBoundsVolume.
* street lamps every 30 m on both sidewalks of every road (``City_Lamp_<n>`` spot light plus
  ``City_LampPole_<n>`` and ``City_LampHead_<n>``); only the ones around the park cast shadows.
  The 20 nearest the park hum: ``City_LampBuzz_<n>`` (AAmbientSound, MS_Amb_LampBuzz, tag CityLamp so
  the EMP silences it too), 3 m up the pole.
* ``City_Ambience`` (AHawkeyeAmbience) at street level: rooftop wind and street hum, crossfaded by
  the player's height above it.
  The lit windows are in the facade material.
* chapter 1: the BP_GameMode_EastVillage override (starts DA_CH01_NightShift, whose chase spawns its own
  thief, dumpster and roof props at run time: create_chase.py); and the objective chain, first built for
  the Rooftops chapter and kept as the district's grapple benchmark: the traversal audit gates on these
  roofs chaining from the street, ThugGroupObjective and the archers use them, and about ten tests stand
  Kate on them. Night Shift has none of their ids, so they stay dark and quiet. Three objective roofs
  picked from the records, each with a 1 m ``City_Beacon_<objective>`` (pole, emissive purple
  ``City_BeaconTop_``, and a movable 300 lm purple ``City_BeaconLight_``) at the end of the roof
  nearest the start, ``City_Obj_<objective>`` trigger volumes on the first two, and on the
  find_arrow roof the chapter's end: ``City_ChapterEndTower`` (a water tower) with
  ``City_ChapterEnd`` (AChapterEndInteractable) stuck in its tank, a black arrow with emissive
  purple fletching that completes find_arrow when examined, and ``City_SceneReturn_FB00`` (an
  ATargetPoint in front of the tower) where the placeholder flashback's scene hands the player back.
* Barney's archers: two ``City_Archer_<n>`` (BP_Archer, tag ArcherPair) on the roofs of two other
  buildings 15 to 25 m from the find_arrow roof, each with a clear line to it (building footprints
  and heights, parapets included) and as far apart around it as the roofs allow. Their roofs, and
  every roof those lines cross, get no rooftop clutter. Aggressive only when Kate is within 30 m.
* chapter 1's first fight: five ``City_Thug_<n>``. A Fists and a Bat thug (BP_Thug) face each
  other on the cross_block roof (tag RoofPair, counted by ``City_ThugGroup_clear_roof``, which
  completes ``clear_roof``); a Bat thug and the gunner patrol the Avenue A sidewalk beside the
  park between ``City_Patrol_0`` and ``City_Patrol_1`` (ATargetPoints 40 m apart, tag StreetPair);
  ``City_Thug_4``, the heavy (BP_Thug_Heavy, shield), walks his own 20 m of the same sidewalk
  between ``City_Patrol_2`` and ``City_Patrol_3`` near the park's south corner. The three street
  thugs are also tagged StreetGroup.
* the partner: ``City_ClintStart`` (an ATargetPoint 5 m behind the PlayerStart, toward the park)
  and ``City_Clint`` (BP_Clint) standing on it; BP_PartnerController possesses him at load.
* Lucky: ``City_Lucky`` (ALuckyCharacter, C++) 3 m behind the PlayerStart and 1.5 m to its left, on the
  pavement; he follows Kate a short way (claude-docs/gameplay-semantics.md, "Lucky").
* grapple anchors (``City_Anchor_<n>``, BP_GrappleAnchor): on every building over 8 m, one on
  the parapet at each roof corner and one mid-edge on edges over 25 m, none within 4 m of
  another, each with its landing point on the roof clear of the parapet; then facade anchors (on
  the parapet's outer face, top flush with it): one on the wall facing the widest street where a
  sidewalk sees it, and one facing each roof across a street or gap that reaches none yet
  (anchor_spots; the reach model is GrappleReach, the report anchor_reach_report).
* traversable ledges (``City_Ledge_<id>_<edge>``): a hidden Game Animation Sample
  LevelBlock_Traversable along every roof edge of 1 m or more, its Ledge_1 spline on the
  parapet's outer top edge, so the sample's vault and mantle see the tenements.
  Neither the anchors nor the ledges are saved in the map (5,400 actors made it 72 MB): they go
  into ``/Game/City/EastVillage/DA_EastVillage_CityProps`` (UCityLedgeData) and one
  ``City_LedgeSpawner`` (ACityLedgeSpawner) spawns them at BeginPlay. Parkour test
  blocks: ``City_Test_Vault`` (90 cm) and ``City_Test_Mantle`` (150 cm) on the street by the
  PlayerStart, and three low ``City_ParkWall_<n>`` in the park.
* fire escapes (``City_FireEscape_<id>_<floor>``): on every building 10 to 30 m tall, on the
  longest of its edges nearest a road, a landing per floor (330 cm apart from the second floor to
  at least 180 cm under the roof): a 240 x 90 x 8 cm slab 5 cm off the facade, 90 cm rails on
  its outer three sides with a traversable ledge on the outer rail, and a ladder down to the
  landing below, alternating ends. Black iron (M_SteelPainted). A building whose escape would
  hit another building, a street lamp or another escape gets none. Data in the same props
  asset, spawned by the same spawner.
* clutter (see clutter_plan): water towers, HVAC boxes and chimneys on the roofs, hydrants, bins and
  bags, scaffolding and parked cars on the street; data in the props asset, drawn by the spawner as
  one instanced mesh per kind.
* the hero block's December dressing (_dressing_build.py, from the committed plan in Tools/Data/dressing/): string
  lights over the side streets, wreaths, lit windows and candles, snow on roofs, parapets, kerbs and cars, a tree
  stand, steam stacks, awnings; more clutter groups (Dress_*) in the same props asset, a few ``City_DressLight_``
  point lights and the ``City_DressShot_`` cameras Hawkeye.Screenshot.Dressing looks through.
* the world map (``/Game/City/EastVillage/DA_EastVillage_Map``, UCityMapData): every footprint and
  the park as a few-point 2D outline, every street piece as a simplified centre line with its width,
  and the ground rectangle's bounding box, all in world cm. UHawkeyeMapWidget draws it.

Idempotent: each mesh carries a ``CityHash`` metadata tag (hash of its source record and the
generator version); a mesh is rebuilt only when that hash changes. Actors are found by label
and compared before anything is set. The level is saved only when something changed. City
actors whose record disappeared are deleted.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\generate_city.py" -unattended -nullrhi -nosplash -nop4 -stdout

or Tools\\generate-city.ps1.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _geo as geo  # noqa: E402
import _materials as m  # noqa: E402
from _geomutil import (  # noqa: E402,F401 - re-exported for gen.X callers
    closest_point_on_polyline, _segments_cross, ring_distance, oriented_rect, _segment_distance,
    _edge_clearance, _segment_params, _edge_distance, _point_quad_distance, stable_hash, _near_segment)
from _district import (  # noqa: E402,F401 - re-exported for gen.X callers
    DISTRICT, GENERATOR_VERSION, PARAPET_HEIGHT, PARAPET_THICK, PARAPET_MIN_HEIGHT_M, PARAPET_MITER_LIMIT,
    ROAD_TOP, SIDEWALK_TOP, PARK_TOP, SLAB_THICK, GROUND_THICK, SIDEWALK_WIDTH, data_dir, data_paths,
    data_available, load_json, District, PAINTED_BUILDINGS, STONE_BUILDINGS, TENEMENT_STYLES, facade_style,
    ground_ring_cm, road_paths, LAMP_SPACING, LAMP_MIN_GAP, LAMP_KERB_INSET, LAMP_POLE_HEIGHT,
    LAMP_POLE_DIAMETER, LAMP_ARM, LAMP_SHADOW_DISTANCE, lamp_spots, FIRE_ESCAPE_MIN_HEIGHT_M,
    FIRE_ESCAPE_MAX_HEIGHT_M, MAP_LINE_TOLERANCE, simplify_line, map_street_lines)
from _fire_escapes import (  # noqa: E402,F401 - re-exported for gen.X callers
    FIRE_ESCAPE_PREFIX, FIRE_ESCAPE_FLOOR, FIRE_ESCAPE_ROOF_CLEARANCE, FIRE_ESCAPE_SLAB, FIRE_ESCAPE_GAP,
    FIRE_ESCAPE_RAIL, FIRE_ESCAPE_RAIL_THICK, FIRE_ESCAPE_MIN_EDGE, FIRE_ESCAPE_STREET_SLACK,
    FIRE_ESCAPE_FACING, FIRE_ESCAPE_LAMP_CLEARANCE, FIRE_ESCAPE_GAP_TO_OTHER, _landing_corners,
    _rects_overlap, fire_escape_spots)
from _reach import (  # noqa: E402,F401 - re-exported for gen.X callers
    ANCHOR_MIN_HEIGHT_M, ANCHOR_MIN_GAP, ANCHOR_LONG_EDGE, ANCHOR_CORNER_TURN, ANCHOR_MIN_INTERIOR,
    ANCHOR_LANDING_INBOARD, ANCHOR_LANDING_CLEARANCE, ANCHOR_BURIED_DISTANCE, _corner_anchor_spots,
    FACADE_ANCHOR_OUT, FACADE_ANCHOR_DOWN, FACADE_ANCHOR_LANDING, FACADE_ANCHOR_MIN_EDGE,
    FACADE_ANCHOR_END_CLEAR, FACADE_ANCHOR_MIN_GAP, FACADE_ANCHOR_ESCAPE_CLEAR, FACADE_ANCHOR_ROAD_REACH,
    FACADE_ANCHOR_SPOTS, REACH_RANGE, REACH_MIN_RANGE, REACH_RANGE_SLACK, REACH_CONE, REACH_CONE_SLACK,
    REACH_SIGHT_TOLERANCE, REACH_HALF_HEIGHT, REACH_CAPSULE_HALF, REACH_ZIP_LAUNCH, REACH_START_RELEASE,
    REACH_SUPPORT_RADIUS, REACH_FEET_CLEAR, REACH_PITCH, REACH_CAMERA, REACH_STREET_ROBUST, REACH_STREET_STEP,
    REACH_SIDEWALK_MIDDLE, REACH_SIDEWALK_BACKOFF, REACH_STAND_CLEAR, REACH_ROOF_SPOT_IN,
    REACH_ROOF_SPOT_EDGE, REACH_NEIGHBOUR_GAP, REACH_TOUCHING, REACH_WALKABLE_RISE, REACH_ESCAPE_MARGIN,
    REACH_CELL, _anchor_marker, _anchor_landing, GrappleReach, _outward_edges, _along_options, _facade_anchor,
    _street_edges, _roof_reached, grapple_reach, anchor_spots)
from _ledges import (  # noqa: E402,F401 - re-exported for gen.X callers
    LEDGE_PREFIX, LEDGE_MIN_EDGE, LEDGE_OUTSET, LEDGE_SLAB_HEIGHT, LEDGE_SPLINE, ledge_spots)

MAP_PATH = "/Game/Maps/L_District_EastVillage"
MESH_DIR = "/Game/City/EastVillage/Meshes"
KIT_MATERIALS = "/Game/Kit/Materials"
PLAYER_PATH = "/Game/Blueprints/Player"

LABEL_PREFIX = "City_"
BUILDING_PREFIX = "City_Bldg_"
ROAD_PREFIX = "City_Road_"
SIDEWALK_PREFIX = "City_Sidewalk_"
PARK_PREFIX = "City_Park_"
GROUND_LABEL = "City_Ground"
MESH_PREFIX = "SM_City_"
HASH_TAG = "CityHash"

# Bump when the building mesh recipe changes, so only the buildings rebuild (2: the parapet is one
# mitred ring flush with the facades, not a box per edge running 15 cm past each corner).
BUILDING_RECIPE = 2

NAV_Z_MIN = -500.0
NAV_Z_MAX = 7500.0

# The data (District, data_paths, road_paths, ...) and the shared dimensions are in _district.

# --------------------------------------------------------------------------------------
# small Geometry Script wrappers (Python returns out-params as tuples)
# --------------------------------------------------------------------------------------

GS_PRIM = unreal.GeometryScript_Primitives
GS_QUERY = unreal.GeometryScript_MeshQueries
GS_NORMALS = unreal.GeometryScript_Normals
GS_POLY = unreal.GeometryScript_PolygonList


def first(result):
    return result[0] if isinstance(result, tuple) else result


def v2(points):
    return [unreal.Vector2D(float(x), float(y)) for x, y in points]


def new_mesh():
    return unreal.DynamicMesh()


def mesh_volume(mesh):
    """Signed volume; negative means the triangles wind inside out."""
    res = GS_QUERY.get_mesh_volume_area(mesh)
    # (surface_area, volume)
    if isinstance(res, tuple):
        return float(res[1])
    return float(res)


def mesh_bounds(mesh):
    return first(GS_QUERY.get_mesh_bounding_box(mesh))


def fix_orientation(mesh):
    """Flip a closed mesh whose signed volume came out negative. Returns True if flipped."""
    if mesh_volume(mesh) < 0.0:
        GS_NORMALS.flip_normals(mesh)
        return True
    return False


def translate(mesh, offset):
    unreal.GeometryScript_MeshTransforms.translate_mesh(mesh, offset)


def polygon_list_from_path(path_cm, half_width):
    options = unreal.GeometryScriptOpenPathOffsetOptions()
    # Butt ends: a street that stops at an avenue stops at its centre line (the avenue covers
    # the rest), and nothing hangs past the edge of the ground slab.
    end = getattr(unreal.GeometryScriptPathOffsetEndType, "BUTT", None)
    if end is not None:
        options.set_editor_property("end_type", end)
    options.set_editor_property("join_type", unreal.GeometryScriptPolyOffsetJoinType.MITER)
    return first(GS_POLY.create_polygons_from_path_offset(v2(path_cm), options, float(half_width)))


def polygon_list_from_ring(ring_cm):
    simple = unreal.GeometryScript_SimplePolygon.conv_array_of_vector2d_to_geometry_script_simple_polygon(
        v2(ring_cm))
    return unreal.GeometryScript_PolygonList.create_polygon_list_from_simple_polygons([simple])


def polygon_list_area(polys):
    return float(GS_POLY.get_polygon_list_area(polys))


def slab_from_polygons(polys, top_z, thickness):
    """Triangulate a (holed) polygon list and extrude it down into a closed slab."""
    mesh = new_mesh()
    options = unreal.GeometryScriptPolygonsTriangulationOptions()
    first(GS_PRIM.append_polygon_list_triangulation(
        mesh, unreal.GeometryScriptPrimitiveOptions(), unreal.Transform(), polys, options))
    if GS_QUERY.get_num_triangle_i_ds(mesh) == 0:
        return None
    # The triangulation is an open patch, so its signed volume means nothing: look at a face.
    # It has to face up before it is pushed up, or the top of every slab is a back face.
    normal = first(GS_QUERY.get_triangle_face_normal(mesh, 0))
    if normal.z < 0.0:
        GS_NORMALS.flip_normals(mesh)
    extrude = unreal.GeometryScriptMeshLinearExtrudeOptions()
    extrude.set_editor_property("distance", float(thickness))
    extrude.set_editor_property("direction", unreal.Vector(0.0, 0.0, 1.0))
    extrude.set_editor_property("solids_to_shells", True)
    unreal.GeometryScript_MeshModeling.apply_mesh_linear_extrude_faces(
        mesh, extrude, unreal.GeometryScriptMeshSelection())
    box = mesh_bounds(mesh)
    translate(mesh, unreal.Vector(0.0, 0.0, top_z - box.max.z))
    GS_NORMALS.set_per_face_normals(mesh)
    return mesh


# --------------------------------------------------------------------------------------
# mesh recipes
# --------------------------------------------------------------------------------------


def parapet_ring(ring_local, height_cm):
    """The parapet as one closed ring: the footprint minus the footprint offset PARAPET_THICK inward
    (mitred), extruded PARAPET_HEIGHT up from the roof. Its outer faces are the facades themselves, so
    every corner, convex or concave, is closed flush with no box running past it (the old per-edge
    boxes stood 15 cm proud of the next facade at every corner). A footprint too narrow to offset is
    walled solid."""
    outer = polygon_list_from_ring(ring_local)
    options = unreal.GeometryScriptPolygonOffsetOptions()
    options.set_editor_property("join_type", unreal.GeometryScriptPolyOffsetJoinType.MITER)
    options.set_editor_property("miter_limit", PARAPET_MITER_LIMIT)
    result = GS_POLY.polygons_offset(outer, options, -PARAPET_THICK, False)
    inner = first(result)
    if isinstance(result, tuple) and len(result) > 1 and not result[1]:
        c.log("skipped", "parapet", "offset failed; walling the roof solid")
    ring = GS_POLY.polygons_difference(outer, inner)
    if GS_POLY.get_polygon_count(ring) == 0:
        return None
    return slab_from_polygons(ring, height_cm + PARAPET_HEIGHT, PARAPET_HEIGHT)


def building_mesh(ring_local, height_cm, parapet):
    """Extruded footprint (local cm, origin at the base centroid) plus the parapet ring."""
    mesh = new_mesh()
    GS_PRIM.append_simple_extrude_polygon(
        mesh, unreal.GeometryScriptPrimitiveOptions(), unreal.Transform(), v2(ring_local),
        float(height_cm), 0, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    if GS_QUERY.get_num_triangle_i_ds(mesh) == 0:
        return None
    fix_orientation(mesh)

    if parapet:
        ring = parapet_ring(ring_local, height_cm)
        if ring is not None:
            unreal.GeometryScript_MeshEdits.append_mesh(mesh, ring, unreal.Transform())

    GS_NORMALS.set_per_face_normals(mesh)
    return mesh


def write_static_mesh(mesh, name, spec_hash):
    """Create or overwrite /Game/City/.../<name> from ``mesh``.

    Returns ``(static_mesh, "created" | "updated")``, or ``(None, "failed")`` on failure.
    """
    full = c.asset_path(MESH_DIR, name)
    existing = c.load_or_none(full)
    if existing is None:
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", True)
        options.set_editor_property("enable_nanite", False)
        options.set_editor_property("enable_collision", True)
        options.set_editor_property("collision_mode", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        static_mesh = first(unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
            mesh, full, options))
        if static_mesh is None:
            c.log("FAILED", full, "create_new_static_mesh_asset_from_mesh returned None")
            return None, "failed"
        action = "created"
    else:
        static_mesh = existing
        options = unreal.GeometryScriptCopyMeshToAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", True)
        unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(
            mesh, static_mesh, options, unreal.GeometryScriptMeshWriteLOD())
        action = "updated"

    ensure_complex_collision(static_mesh)
    unreal.EditorAssetLibrary.set_metadata_tag(static_mesh, HASH_TAG, spec_hash)
    c.save(static_mesh)
    return static_mesh, action


def ensure_complex_collision(static_mesh):
    try:
        body = static_mesh.get_editor_property("body_setup")
        if body is None:
            return False
        want = unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
        if body.get_editor_property("collision_trace_flag") != want:
            body.set_editor_property("collision_trace_flag", want)
            return True
    except Exception as exc:  # noqa: BLE001
        c.log_error("collision " + c.safe_name(static_mesh), exc)
    return False


def stored_hash(name):
    asset = c.load_or_none(c.asset_path(MESH_DIR, name))
    if asset is None:
        return None, None
    try:
        return asset, unreal.EditorAssetLibrary.get_metadata_tag(asset, HASH_TAG)
    except Exception:  # noqa: BLE001
        return asset, None


# --------------------------------------------------------------------------------------
# the pieces: each is (label, mesh name, spec hash, location, build_fn, material, tags)
# --------------------------------------------------------------------------------------


class Piece(object):
    def __init__(self, label, mesh_name, spec_hash, location, build, material, tags=()):
        self.label = label
        self.mesh_name = mesh_name
        self.spec_hash = spec_hash
        self.location = location
        self.build = build
        self.material = material
        self.tags = list(tags)


def building_pieces(district, facades):
    """``facades`` maps a facade style to its MI_Facade_ instance."""
    pieces = []
    skipped_holes = 0
    for rec in district.buildings:
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3:
            c.log("skipped", BUILDING_PREFIX + rec["id"], "degenerate footprint")
            continue
        if rec.get("holes"):
            skipped_holes += 1
        cx, cy = geo.centroid(ring)
        local = [(x - cx, y - cy) for x, y in ring]
        height_cm = rec["height_m"] * 100.0
        parapet = rec["height_m"] >= PARAPET_MIN_HEIGHT_M
        spec = geo.record_hash(GENERATOR_VERSION, "bldg", [list(p) for p in local], height_cm, parapet, BUILDING_RECIPE)

        def build(local=local, height_cm=height_cm, parapet=parapet):
            return building_mesh(local, height_cm, parapet)

        style = facade_style(rec)
        material = facades.get(style)
        tags = ["City", "CityBuilding", "osm:" + rec["id"],
                "height_cm:{0:.1f}".format(height_cm), "parapet:{0}".format(int(parapet)), "facade:" + style]
        street = rec.get("tags", {}).get("addr:street")
        if street:
            tags.append("street:" + street)
        pieces.append(Piece(BUILDING_PREFIX + rec["id"], MESH_PREFIX + rec["id"], spec,
                            unreal.Vector(cx, cy, 0.0), build, material, tags))
    if skipped_holes:
        c.log("skipped", "courtyard holes", "{0} building(s) had inner rings; extruded solid".format(skipped_holes))
    return pieces


def polys_for_paths(paths, half_width):
    polys = None
    for path in paths:
        p = polygon_list_from_path(path, half_width)
        if polys is None:
            polys = p
        else:
            GS_POLY.append_polygon_list(polys, p)
    return GS_POLY.polygons_union(polys) if polys is not None else None


def centre_of(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return (min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5


def shifted(mesh, cx, cy):
    """Move a mesh built in world centimetres so its origin sits at (cx, cy, 0)."""
    if mesh is not None:
        translate(mesh, unreal.Vector(-cx, -cy, 0.0))
    return mesh


def street_pieces(district, materials):
    """Ground, roads, sidewalks and parks. Built in world cm, then shifted to a local origin."""
    pieces = []

    ground = ground_ring_cm(district)
    gx, gy = centre_of(ground)
    pieces.append(Piece(
        GROUND_LABEL, MESH_PREFIX + "Ground",
        geo.record_hash(GENERATOR_VERSION, "ground", ground),
        unreal.Vector(gx, gy, 0.0),
        lambda: shifted(slab_from_polygons(polygon_list_from_ring(ground), 0.0, GROUND_THICK), gx, gy),
        materials["ground"], ["City", "CityGround"]))

    roads = road_paths(district)
    all_road_spec = geo.record_hash([[r["id"], r["width_m"], p] for r, p in roads])

    # Every carriageway at once, for cutting the sidewalks and the park. Built lazily, once,
    # and only if some sidewalk or park actually needs rebuilding.
    cache = {}

    def carriageways():
        if "all" not in cache:
            polys = None
            for rec, paths in roads:
                p = polys_for_paths(paths, rec["width_m"] * 50.0)
                if p is None:
                    continue
                if polys is None:
                    polys = p
                else:
                    GS_POLY.append_polygon_list(polys, p)
            cache["all"] = GS_POLY.polygons_union(polys)
        return cache["all"]

    for rec, paths in roads:
        cx, cy = centre_of([p for path in paths for p in path])
        half = rec["width_m"] * 50.0
        loc = unreal.Vector(cx, cy, 0.0)
        name_tag = "street:" + (rec.get("name") or "")

        def build_road(paths=paths, half=half, cx=cx, cy=cy):
            return shifted(slab_from_polygons(polys_for_paths(paths, half), ROAD_TOP, SLAB_THICK), cx, cy)

        pieces.append(Piece(
            ROAD_PREFIX + rec["id"], MESH_PREFIX + "Road_" + rec["id"],
            geo.record_hash(GENERATOR_VERSION, "road", paths, half), loc, build_road,
            materials["asphalt"], ["City", "CityRoad", "osm:" + rec["id"], name_tag]))

        def build_sidewalk(paths=paths, half=half, cx=cx, cy=cy):
            outer = polys_for_paths(paths, half + SIDEWALK_WIDTH)
            walk = GS_POLY.polygons_difference(outer, carriageways())
            if polygon_list_area(walk) < 1.0:
                return None
            return shifted(slab_from_polygons(walk, SIDEWALK_TOP, SLAB_THICK), cx, cy)

        pieces.append(Piece(
            SIDEWALK_PREFIX + rec["id"], MESH_PREFIX + "Sidewalk_" + rec["id"],
            geo.record_hash(GENERATOR_VERSION, "sidewalk", paths, half, all_road_spec), loc,
            build_sidewalk, materials["sidewalk"],
            ["City", "CitySidewalk", "osm:" + rec["id"], name_tag]))

    for park in district.parks:
        ring = geo.clean_ring(district.ring_cm(park["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3:
            continue
        cx, cy = geo.centroid(ring)

        def build_park(ring=ring, cx=cx, cy=cy):
            grass = GS_POLY.polygons_difference(polygon_list_from_ring(ring), carriageways())
            return shifted(slab_from_polygons(grass, PARK_TOP, SLAB_THICK), cx, cy)

        pieces.append(Piece(
            PARK_PREFIX + park["id"], MESH_PREFIX + "Park_" + park["id"],
            geo.record_hash(GENERATOR_VERSION, "park", ring, all_road_spec),
            unreal.Vector(cx, cy, 0.0), build_park, materials["grass"],
            ["City", "CityPark", "osm:" + park["id"], "name:" + (park.get("name") or "")]))

    return pieces


# --------------------------------------------------------------------------------------
# actors
# --------------------------------------------------------------------------------------


def actors_by_label():
    out = {}
    for actor in c.all_level_actors():
        try:
            out[actor.get_actor_label()] = actor
        except Exception:  # noqa: BLE001
            continue
    return out


def same_vector(a, b, tol=0.05):
    return abs(a.x - b.x) <= tol and abs(a.y - b.y) <= tol and abs(a.z - b.z) <= tol


def ensure_piece_actor(piece, static_mesh, existing):
    """Place or fix one StaticMeshActor. Returns the number of changes made."""
    changes = 0
    actor = existing.get(piece.label)
    if actor is not None and not isinstance(actor, unreal.StaticMeshActor):
        actor.destroy_actor()
        actor = None
        changes += 1
    if actor is None:
        actor = c.spawn_actor(unreal.StaticMeshActor, piece.location, label=piece.label)
        if actor is None:
            c.log("FAILED", piece.label, "spawn_actor returned None")
            return 0
        existing[piece.label] = actor
        changes += 1

    component = actor.get_editor_property("static_mesh_component")
    if component.get_editor_property("mobility") != unreal.ComponentMobility.STATIC:
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        changes += 1
    if not same_vector(actor.get_actor_location(), piece.location):
        actor.set_actor_location(piece.location, False, True)
        changes += 1
    rot = actor.get_actor_rotation()
    if abs(rot.pitch) > 1e-3 or abs(rot.yaw) > 1e-3 or abs(rot.roll) > 1e-3:
        actor.set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)
        changes += 1
    if not same_vector(actor.get_actor_scale3d(), unreal.Vector(1.0, 1.0, 1.0), 1e-4):
        actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
        changes += 1
    if component.get_editor_property("static_mesh") != static_mesh:
        component.set_static_mesh(static_mesh)
        changes += 1
    if piece.material is not None:
        overrides = component.get_editor_property("override_materials")
        if len(overrides) < 1 or overrides[0] != piece.material:
            component.set_material(0, piece.material)
            changes += 1
    want_tags = [unreal.Name(t) for t in piece.tags]
    have = [str(t) for t in actor.get_editor_property("tags")]
    if have != piece.tags:
        actor.set_editor_property("tags", want_tags)
        changes += 1
    return changes


def build_pieces(pieces, existing):
    """Rebuild changed meshes, place actors. Returns (mesh changes, actor changes)."""
    c.ensure_directory(MESH_DIR)
    mesh_changes = 0
    actor_changes = 0
    unchanged = 0
    for piece in pieces:
        try:
            asset, current = stored_hash(piece.mesh_name)
            static_mesh = asset
            if asset is None or current != piece.spec_hash:
                mesh = piece.build()
                if mesh is None:
                    c.log("skipped", piece.label, "empty geometry")
                    continue
                static_mesh, action = write_static_mesh(mesh, piece.mesh_name, piece.spec_hash)
                if static_mesh is None:
                    continue
                mesh_changes += 1
                c.log(action, c.asset_path(MESH_DIR, piece.mesh_name))
            else:
                if ensure_complex_collision(asset):
                    c.save(asset)
                    mesh_changes += 1
                    c.log("updated", c.asset_path(MESH_DIR, piece.mesh_name), "collision -> complex as simple")
                unchanged += 1
            n = ensure_piece_actor(piece, static_mesh, existing)
            if n:
                actor_changes += 1
                c.log("updated", piece.label, "{0} actor change(s)".format(n))
        except Exception as exc:  # noqa: BLE001
            c.log_error("piece " + piece.label, exc)
    c.log("exists", "{0} city meshes".format(unchanged), "hash unchanged, not rebuilt")
    return mesh_changes, actor_changes


def remove_stale(pieces, existing):
    wanted = {p.label for p in pieces}
    removed = 0
    for label, actor in list(existing.items()):
        if label.startswith(LABEL_PREFIX) and label not in wanted and label.split("_")[1] in (
                "Bldg", "Road", "Sidewalk", "Park", "Ground"):
            actor.destroy_actor()
            existing.pop(label, None)
            removed += 1
            c.log("updated", label, "removed; no longer in the OSM records")
    return removed


# --------------------------------------------------------------------------------------
# lighting, player start, nav, game mode
# --------------------------------------------------------------------------------------

# A January night: the sun is down, a dim cool moon (the only directional light, and the sky
# atmosphere's light, so the atmosphere draws a moonlit sky and the moon's disc) lights the roofs,
# and the lamps and lit windows carry the streets.
MOON_LABEL = "Moon"
RETIRED_SUN_LABEL = "Sun"
MOON_ROTATION = unreal.Rotator(0.0, -30.0, -30.0)   # roll, pitch, yaw: 30 degrees up in the WSW
MOON_LUX = 0.3
MOON_COLOR = (0.55, 0.65, 1.0)
MOON_DISK_SCALE = (0.9, 0.93, 1.0)
SKY_INTENSITY = 3.0
FOG_DENSITY = 0.008
FOG_INSCATTERING = (0.035, 0.035, 0.06)   # night haze, a touch of purple
FOG_CUTOFF = 300000.0                     # cm; the star sphere (400 m further out) stays clear of fog
# Auto exposure is off project-wide (Config/DefaultEngine.ini), so exposure is fixed: the
# min/max brightness pair pins it at 1.0, and the bias is the knob.
EXPOSURE_BRIGHTNESS = 1.0
EXPOSURE_BIAS = 2.0
# Grading: purple in the shadows, cream in the highlights, a light vignette.
GAIN_SHADOWS = (0.92, 0.86, 1.10, 1.0)
GAIN_HIGHLIGHTS = (1.05, 1.0, 0.88, 1.0)
VIGNETTE = 0.3
STARS_LABEL = "City_NightSky"
STARS_RADIUS = 340000.0                   # cm
SPHERE = "/Engine/BasicShapes/Sphere"     # 100 cm across


def set_if_different(obj, prop, value, context, tol=1e-3):
    try:
        current = obj.get_editor_property(prop)
    except Exception as exc:  # noqa: BLE001
        c.log_error("{0}.{1}".format(context, prop), exc)
        return 0
    same = False
    if isinstance(value, float) and isinstance(current, (float, int)):
        same = abs(float(current) - value) <= tol
    elif isinstance(value, unreal.Rotator):
        same = (abs(current.pitch - value.pitch) <= tol and abs(current.yaw - value.yaw) <= tol
                and abs(current.roll - value.roll) <= tol)
    elif isinstance(value, (unreal.LinearColor, unreal.Vector4, unreal.Color)):
        same = m.same_value(current, value, tol)
    else:
        same = current == value
    if same:
        return 0
    obj.set_editor_property(prop, value)
    return 1


def _color(rgb):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)


def ensure_lighting(existing):
    """The night: ensure_lighting_core and the star sphere round the district."""
    changes = ensure_lighting_core(existing, make_movable=False)
    changes += ensure_night_sky(existing)
    return changes + _all_lights_movable()


def ensure_lighting_core(existing, make_movable=True, labels=None):
    """The moon, sky light, atmosphere, fog and PP_Global, by those labels. generate_interior.py uses it too,
    so an interior's windows show the district's night and the two grade alike. ``labels`` maps each of
    those five names to the label to use (an interior prefixes its own)."""
    labels = labels or {}
    moon_label = labels.get(MOON_LABEL, MOON_LABEL)
    sky_label = labels.get("SkyLight", "SkyLight")
    fog_label = labels.get("HeightFog", "HeightFog")
    pp_label = labels.get("PP_Global", "PP_Global")
    changes = 0
    old_sun = existing.pop(RETIRED_SUN_LABEL, None)
    if old_sun is not None:
        old_sun.destroy_actor()
        changes += 1
        c.log("updated", RETIRED_SUN_LABEL, "removed; the moon lights the night")
    moon, created = ensure_labelled(existing, unreal.DirectionalLight, moon_label, unreal.Vector(0, 0, 3000),
                                    MOON_ROTATION)
    changes += created
    if moon is not None:
        comp = moon.get_editor_property("directional_light_component")
        changes += set_if_different(comp, "mobility", unreal.ComponentMobility.MOVABLE, moon_label)
        changes += set_if_different(comp, "intensity", MOON_LUX, moon_label)
        changes += set_if_different(comp, "atmosphere_sun_light", True, moon_label)
        changes += set_if_different(comp, "use_temperature", False, moon_label)
        want = unreal.Color(r=int(MOON_COLOR[0] * 255), g=int(MOON_COLOR[1] * 255), b=int(MOON_COLOR[2] * 255), a=255)
        have = comp.get_editor_property("light_color")
        if (have.r, have.g, have.b) != (want.r, want.g, want.b):
            comp.set_editor_property("light_color", want)
            changes += 1
        changes += set_if_different(comp, "atmosphere_sun_disk_color_scale", _color(MOON_DISK_SCALE), moon_label)
        rot = moon.get_actor_rotation()
        if abs(rot.pitch - MOON_ROTATION.pitch) > 0.01 or abs(rot.yaw - MOON_ROTATION.yaw) > 0.01:
            moon.set_actor_rotation(MOON_ROTATION, False)
            changes += 1

    sky, created = ensure_labelled(existing, unreal.SkyLight, sky_label, unreal.Vector(0, 0, 2000))
    changes += created
    if sky is not None:
        comp = sky.get_editor_property("light_component")
        changes += set_if_different(comp, "mobility", unreal.ComponentMobility.MOVABLE, sky_label)
        changes += set_if_different(comp, "real_time_capture", True, sky_label)
        changes += set_if_different(comp, "source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE, sky_label)
        changes += set_if_different(comp, "intensity", SKY_INTENSITY, sky_label)

    atmosphere = c.find_class("SkyAtmosphere", "/Script/Engine.SkyAtmosphere")
    atmosphere_label = labels.get("SkyAtmosphere", "SkyAtmosphere")
    _a, created = ensure_labelled(existing, atmosphere, atmosphere_label, unreal.Vector(0, 0, 0))
    changes += created

    fog, created = ensure_labelled(existing, unreal.ExponentialHeightFog, fog_label, unreal.Vector(0, 0, 0))
    changes += created
    if fog is not None:
        comp = fog.get_editor_property("component")
        changes += set_if_different(comp, "fog_density", FOG_DENSITY, fog_label, 1e-5)
        changes += set_if_different(comp, "fog_inscattering_luminance", _color(FOG_INSCATTERING), fog_label, 1e-4)
        changes += set_if_different(comp, "fog_cutoff_distance", FOG_CUTOFF, fog_label, 1.0)

    pp, created = ensure_labelled(existing, unreal.PostProcessVolume, pp_label, unreal.Vector(0, 0, 0))
    changes += created
    if pp is not None:
        changes += set_if_different(pp, "unbound", True, pp_label)
        settings = pp.get_editor_property("settings")
        dirty = False
        for prop, value in (("override_auto_exposure_min_brightness", True),
                            ("override_auto_exposure_max_brightness", True),
                            ("auto_exposure_min_brightness", EXPOSURE_BRIGHTNESS),
                            ("auto_exposure_max_brightness", EXPOSURE_BRIGHTNESS),
                            ("override_auto_exposure_bias", True),
                            ("auto_exposure_bias", EXPOSURE_BIAS),
                            ("override_color_gain_shadows", True),
                            ("color_gain_shadows", unreal.Vector4(*GAIN_SHADOWS)),
                            ("override_color_gain_highlights", True),
                            ("color_gain_highlights", unreal.Vector4(*GAIN_HIGHLIGHTS)),
                            ("override_vignette_intensity", True),
                            ("vignette_intensity", VIGNETTE)):
            current = settings.get_editor_property(prop)
            if not m.same_value(current, value):
                settings.set_editor_property(prop, value)
                dirty = True
        if dirty:
            pp.set_editor_property("settings", settings)
            changes += 1
    return changes + (_all_lights_movable() if make_movable else 0)


def _all_lights_movable():
    """Nothing in these maps may bake: every light is movable (Lumen, no lightmass)."""
    changes = 0
    for actor in c.all_level_actors():
        if isinstance(actor, (unreal.Light, unreal.SkyLight)):
            root = actor.get_editor_property("root_component")
            if root is not None and root.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
                root.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
                changes += 1
    return changes


def ensure_night_sky(existing):
    """City_NightSky: the engine sphere scaled round the district, seen from inside, in M_NightStars.
    Not the engine's own starry sky (BP_Sky_Sphere): that replaces the sky atmosphere rather than
    adding to it."""
    sphere = c.load_or_none(SPHERE)
    stars = c.load_or_none(m.M_NIGHT_STARS)
    if sphere is None or stars is None:
        c.log("FAILED", STARS_LABEL, "no engine sphere or M_NightStars")
        return 0
    scale = STARS_RADIUS / 50.0
    changes = _ensure_mesh_actor(existing, STARS_LABEL, sphere, stars, unreal.Vector(0.0, 0.0, 0.0),
                                 unreal.Rotator(0.0, 0.0, 0.0), unreal.Vector(scale, scale, scale),
                                 ["City", "CityNightSky"])
    actor = existing.get(STARS_LABEL)
    if actor is not None:
        comp = actor.get_editor_property("static_mesh_component")
        changes += set_if_different(comp, "cast_shadow", False, STARS_LABEL)
        # No collision at all: everything in the district is inside this sphere, and its simple
        # sphere collider pushed Kate, the thugs and every ground trace out through the floor.
        if str(comp.get_collision_profile_name()) != "NoCollision":
            comp.set_collision_profile_name("NoCollision")
            changes += 1
        changes += set_if_different(comp, "affect_distance_field_lighting", False, STARS_LABEL)
    return changes


def ensure_labelled(existing, actor_class, label, location, rotation=None):
    """(actor, 1 if spawned else 0)."""
    actor = existing.get(label)
    if actor is not None:
        return actor, 0
    if actor_class is None:
        c.log("skipped", label, "class not available")
        return None, 0
    actor = c.spawn_actor(actor_class, location, rotation, label=label)
    if actor is None:
        c.log("FAILED", label, "spawn_actor returned None")
        return None, 0
    existing[label] = actor
    c.log("created", label)
    return actor, 1


PLAYER_START_STREET = "East 7th Street"


def player_start_transform(district):
    """On East 7th Street across from the park, looking at the tenement row to the south."""
    park = district.parks[0] if district.parks else None
    target = geo.centroid(district.ring_cm(park["outer"])) if park else (0.0, 0.0)
    best = None
    for rec in district.roads:
        if rec.get("name") != PLAYER_START_STREET:
            continue
        for piece in rec["pieces"]:
            hit = closest_point_on_polyline(target, district.ring_cm(piece))
            if hit and (best is None or hit[0] < best[0]):
                best = hit
    if best is None:
        return unreal.Vector(0.0, 0.0, 100.0), unreal.Rotator(0.0, 0.0, 0.0)
    q = best[1]
    away = (q[0] - target[0], q[1] - target[1])
    yaw = math.degrees(math.atan2(away[1], away[0]))
    # Stand 3 m on the park side of the centre line so the row fills the view.
    L = math.hypot(*away) or 1.0
    pos = (q[0] - away[0] / L * 300.0, q[1] - away[1] / L * 300.0)
    return unreal.Vector(pos[0], pos[1], 100.0), unreal.Rotator(0.0, 0.0, yaw)


def ensure_player_start(district, existing):
    loc, rot = player_start_transform(district)
    actor, created = ensure_labelled(existing, unreal.PlayerStart, "PlayerStart", loc, rot)
    changes = created
    if actor is not None and not created:
        if not same_vector(actor.get_actor_location(), loc, 1.0):
            actor.set_actor_location(loc, False, True)
            changes += 1
        if abs(actor.get_actor_rotation().yaw - rot.yaw) > 0.1:
            actor.set_actor_rotation(rot, False)
            changes += 1
    return changes


# Clint (docs/DESIGN.md, "The partner"): BP_Clint placed CLINT_BEHIND_START behind the PlayerStart,
# on the park side of East 7th Street, standing on whatever slab is under him. His AIController
# (BP_PartnerController) possesses him at load and he follows whoever the player is playing.
CLINT_START_LABEL = "City_ClintStart"
CLINT_LABEL = "City_Clint"
CLINT_BP_PATH = "/Game/Blueprints/Player"
CLINT_BP_NAME = "BP_Clint"
CLINT_BEHIND_START = 500.0         # cm
CLINT_HALF_HEIGHT = 92.5           # cm; BP_Clint's capsule
CLINT_MARKER_HEIGHT = 100.0        # cm above the pavement for the start point


def clint_start_transform(district):
    """(x, y, ground z, yaw): CLINT_BEHIND_START behind the PlayerStart, facing the way it faces."""
    loc, rot = player_start_transform(district)
    yaw = math.radians(rot.yaw)
    x = loc.x - math.cos(yaw) * CLINT_BEHIND_START
    y = loc.y - math.sin(yaw) * CLINT_BEHIND_START
    width_m = next((rec["width_m"] for rec in district.roads if rec.get("name") == PLAYER_START_STREET), 12.0)
    # A first guess only (ensure_clint traces the slab): the start stands 3 m off the centre line
    # toward the park, and behind it is further that way.
    off_road = 300.0 + CLINT_BEHIND_START > width_m * 50.0
    return x, y, SIDEWALK_TOP if off_road else ROAD_TOP, rot.yaw


def ensure_clint(district, existing):
    """City_ClintStart (a TargetPoint) and City_Clint (BP_Clint) on it. Idempotent by label."""
    clint_cls = c.load_generated_class(CLINT_BP_PATH, CLINT_BP_NAME)
    if clint_cls is None:
        c.log("FAILED", CLINT_LABEL, "BP_Clint missing; run create_partner.py")
        return 0
    x, y, guess, yaw = clint_start_transform(district)
    # The kerb makes a guess 13 cm out; the slab under him says where the pavement really is.
    ground = ground_z(x, y, guess, existing)
    marker, changes = _ensure_located(existing, CLINT_START_LABEL, unreal.TargetPoint,
                                      unreal.Vector(x, y, ground + CLINT_MARKER_HEIGHT), yaw)
    if marker is not None:
        changes += _ensure_tags(marker, ["City", "CityClintStart"])
    clint, n = _ensure_located(existing, CLINT_LABEL, clint_cls,
                               unreal.Vector(x, y, ground + CLINT_HALF_HEIGHT + 2.0), yaw)
    if clint is not None:
        n += _ensure_tags(clint, ["City", "CityPartner"])
    changes += n
    c.log("updated" if changes else "exists", CLINT_LABEL, "at ({0:.0f}, {1:.0f}, {2:.0f}) yaw {3:.0f}, {4:.0f} cm behind the start".format(
        x, y, ground, yaw, CLINT_BEHIND_START))
    return changes


# Lucky (claude-docs/gameplay-semantics.md, "Lucky"): the C++ ALuckyCharacter itself, no Blueprint: his mesh and clips
# are soft references to the git-ignored import (import_bodies.py), and a clone without them hides him.
LUCKY_LABEL = "City_Lucky"
LUCKY_BEHIND_START = 300.0         # cm behind the PlayerStart
LUCKY_LEFT_OF_START = 150.0        # cm to its left
LUCKY_HALF_HEIGHT = 36.0           # cm; ALuckyCharacter's capsule


def lucky_start_transform(district):
    """(x, y, yaw): LUCKY_BEHIND_START behind the PlayerStart and LUCKY_LEFT_OF_START to its left, facing its way."""
    loc, rot = player_start_transform(district)
    yaw = math.radians(rot.yaw)
    fwd, left = (math.cos(yaw), math.sin(yaw)), (-math.sin(yaw), math.cos(yaw))
    x = loc.x - fwd[0] * LUCKY_BEHIND_START + left[0] * LUCKY_LEFT_OF_START
    y = loc.y - fwd[1] * LUCKY_BEHIND_START + left[1] * LUCKY_LEFT_OF_START
    return x, y, rot.yaw


def ensure_lucky(district, existing):
    """City_Lucky (ALuckyCharacter) on the slab beside the PlayerStart. Idempotent by label."""
    cls = getattr(unreal, "LuckyCharacter", None)
    if cls is None:
        c.log("FAILED", LUCKY_LABEL, "no ALuckyCharacter class (build the Hawkeye module)")
        return 0
    x, y, yaw = lucky_start_transform(district)
    ground = ground_z(x, y, SIDEWALK_TOP, existing)
    lucky, changes = _ensure_located(existing, LUCKY_LABEL, cls, unreal.Vector(x, y, ground + LUCKY_HALF_HEIGHT + 2.0), yaw)
    if lucky is not None:
        changes += _ensure_tags(lucky, ["City", "CityLucky"])
    c.log("updated" if changes else "exists", LUCKY_LABEL, "at ({0:.0f}, {1:.0f}, {2:.0f}) yaw {3:.0f}".format(
        x, y, ground, yaw))
    return changes


def ensure_nav_volume(district, existing):
    ground = ground_ring_cm(district)
    xmin, ymin, xmax, ymax = geo.bounds(ground)
    centre = unreal.Vector((xmin + xmax) * 0.5, (ymin + ymax) * 0.5, (NAV_Z_MIN + NAV_Z_MAX) * 0.5)
    # A brush volume is 200 units per side at scale 1.
    scale = unreal.Vector((xmax - xmin) / 200.0, (ymax - ymin) / 200.0, (NAV_Z_MAX - NAV_Z_MIN) / 200.0)
    nav_class = c.find_class("NavMeshBoundsVolume", "/Script/NavigationSystem.NavMeshBoundsVolume")
    actor, created = ensure_labelled(existing, nav_class, "NavMeshBounds", centre)
    changes = created
    if actor is not None:
        if not same_vector(actor.get_actor_location(), centre, 1.0):
            actor.set_actor_location(centre, False, True)
            changes += 1
        if not same_vector(actor.get_actor_scale3d(), scale, 1e-3):
            actor.set_actor_scale3d(scale)
            changes += 1
    return changes


# --------------------------------------------------------------------------------------
# chapter 1: the district's own game mode, and the objective volumes on three roofs
# --------------------------------------------------------------------------------------

DISTRICT_GAME_MODE = "BP_GameMode_EastVillage"
MISSION_ASSET = "/Game/Missions/DA_CH01_NightShift"
OBJECTIVE_PREFIX = "City_Obj_"
OBJECTIVE_IDS = ("reach_roof", "cross_block", "find_arrow")
# The roofs with a trigger volume; find_arrow is completed by examining City_ChapterEnd instead.
VOLUME_OBJECTIVE_IDS = ("reach_roof", "cross_block")
OBJECTIVE_START_RADIUS = 6000.0   # cm: reach_roof is the tallest building this close to the start
OBJECTIVE_MIN_HEIGHT_M = 8.0      # cross_block and find_arrow skip sheds and garages
OBJECTIVE_ABOVE_ROOF = 50.0       # cm between the roof and the bottom of the volume
OBJECTIVE_HALF_HEIGHT = 150.0     # cm; tall enough to hold Kate's whole capsule

# The objective chain's ids, in order (first the Rooftops chapter's objectives); clear_roof has no volume (a thug
# group completes it). Its volumes, beacons, arrow and fighters stay in the district as the traversal audit's chain
# and the tests' roofs; the running chapter has none of these ids, so they keep quiet and the beacons stay dark
# (gameplay-semantics.md, "Chases").
MISSION_OBJECTIVE_IDS = ("reach_roof", "cross_block", "clear_roof", "find_arrow")

# The chapter the district starts (DA_CH01_NightShift), in order: its chase completes all three.
CHAPTER_OBJECTIVE_IDS = ("chase_thief", "cross_gap", "catch_thief")

# Actors the prison build put in its maps. None belong in the district, except the chapter's own
# thugs, which carry THUG_PREFIX labels.
PRISON_CLASS_WORDS = ("Thug", "Guard", "Keycard", "Pickup", "Door")


def ensure_district_game_mode():
    """BP_GameMode_EastVillage (child of BP_HawkeyeGameMode) starting DA_CH01_NightShift.

    Returns (generated class or None, number of changes)."""
    parent = c.load_generated_class(PLAYER_PATH, "BP_HawkeyeGameMode")
    if parent is None:
        c.log("skipped", DISTRICT_GAME_MODE, "BP_HawkeyeGameMode_C not found; run create_blueprints.py")
        return None, 0
    full = c.asset_path(PLAYER_PATH, DISTRICT_GAME_MODE)
    changes = 0
    bp = c.load_or_none(full)
    if bp is None:
        factory = c.new_factory("BlueprintFactory")
        if factory is None:
            c.log("FAILED", full, "BlueprintFactory unavailable")
            return None, 0
        c.set_props(factory, [("parent_class", parent)], DISTRICT_GAME_MODE + " factory")
        bp, _created = c.create_asset(DISTRICT_GAME_MODE, PLAYER_PATH, unreal.Blueprint, factory, quiet=True)
        if bp is None:
            return None, 0
        c.log("created", full, "parent BP_HawkeyeGameMode_C")
        changes += 1

    mission = c.load_or_none(MISSION_ASSET)
    cdo = c.blueprint_cdo(bp)
    if mission is None:
        c.log("skipped", full, "DA_CH01_NightShift missing; run create_mission_data.py")
    elif cdo is not None and cdo.get_editor_property("starting_mission") != mission:
        if c.set_props(cdo, [("starting_mission", mission)], DISTRICT_GAME_MODE):
            changes += 1
            c.log("updated", full, "starting_mission = DA_CH01_NightShift")
    if changes:
        c.compile_blueprint(bp)
        c.save(bp)
    else:
        c.log("exists", full)
    return c.load_generated_class(PLAYER_PATH, DISTRICT_GAME_MODE), changes


def ensure_game_mode():
    """The map's GameMode override: BP_GameMode_EastVillage, or BP_HawkeyeGameMode as a fallback."""
    game_mode, _changes = ensure_district_game_mode()
    name = DISTRICT_GAME_MODE + "_C"
    if game_mode is None:
        game_mode = c.load_generated_class(PLAYER_PATH, "BP_HawkeyeGameMode")
        name = "BP_HawkeyeGameMode_C"
    if game_mode is None:
        c.log("skipped", MAP_PATH, "no game mode class found; GameMode override unset")
        return 0
    ws = c.world_settings()
    if ws is None:
        return 0
    if ws.get_editor_property("default_game_mode") == game_mode:
        return 0
    ws.set_editor_property("default_game_mode", game_mode)
    c.log("updated", MAP_PATH, "GameMode override = " + name)
    return 1


def actor_class_name(actor):
    try:
        return actor.get_class().get_name()
    except Exception:  # noqa: BLE001
        return c.class_name(type(actor))


def is_chapter_actor(label):
    """True for the fight's own thugs and their group, which share class words with the prison's."""
    return label.startswith(THUG_PREFIX) or label == THUG_GROUP_LABEL


def remove_prison_actors(existing):
    """Delete anything the prison build would have placed (thugs, keycards, doors, pickups)."""
    removed = 0
    for label, actor in list(existing.items()):
        cls = actor_class_name(actor)
        if is_chapter_actor(label):
            continue
        if any(word in cls for word in PRISON_CLASS_WORDS):
            actor.destroy_actor()
            existing.pop(label, None)
            removed += 1
            c.log("updated", label, "removed prison-build actor ({0})".format(cls))
    if not removed:
        c.log("exists", MAP_PATH, "no thug, keycard, door or pickup actors")
    return removed


def streets_crossed(district, a, b):
    """Names of the road centre lines the segment a-b crosses."""
    names = set()
    for rec in district.roads:
        for piece in rec["pieces"]:
            path = district.ring_cm(piece)
            if any(_segments_cross(a, b, p, q) for p, q in zip(path, path[1:])):
                names.add(rec.get("name") or rec["id"])
    return names


def objective_roofs(district):
    """{objective id: building record plus 'ring' and 'centre'} for the three chapter-1 roofs.

    reach_roof: the tallest building within OBJECTIVE_START_RADIUS of the PlayerStart.
    cross_block: the building on the same block (no street centre line between the two
    centroids) farthest from it. find_arrow: the building nearest cross_block that is exactly
    one street away from it and off the first block.
    """
    start, _rot = player_start_transform(district)
    start = (start.x, start.y)
    buildings = []
    for rec in district.buildings:
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) >= 3:
            buildings.append(dict(rec, ring=ring, centre=geo.centroid(ring)))

    near = [b for b in buildings if ring_distance(start, b["ring"]) <= OBJECTIVE_START_RADIUS]
    if not near:
        return {}
    first = max(near, key=lambda b: (b["height_m"], b["id"]))

    def dist(a, b):
        return math.hypot(a["centre"][0] - b["centre"][0], a["centre"][1] - b["centre"][1])

    tall = [b for b in buildings if b["height_m"] >= OBJECTIVE_MIN_HEIGHT_M and b is not first]
    same_block = [b for b in tall if not streets_crossed(district, first["centre"], b["centre"])]
    if not same_block:
        return {"reach_roof": first}
    second = max(same_block, key=lambda b: (dist(first, b), b["id"]))

    out = {"reach_roof": first, "cross_block": second}
    candidates = sorted((b for b in tall if b is not second), key=lambda b: (dist(second, b), b["id"]))
    for b in candidates:
        if streets_crossed(district, first["centre"], b["centre"]) and \
                len(streets_crossed(district, second["centre"], b["centre"])) == 1:
            out["find_arrow"] = b
            break
    return out


def ensure_objective_volumes(district, existing):
    volume_class = c.find_class("ObjectiveTriggerVolume", "/Script/Hawkeye.ObjectiveTriggerVolume")
    if volume_class is None:
        c.log("skipped", OBJECTIVE_PREFIX + "*", "AObjectiveTriggerVolume not exposed; build the module")
        return 0
    roofs = objective_roofs(district)
    changes = 0
    wanted = set()
    for oid in VOLUME_OBJECTIVE_IDS:
        rec = roofs.get(oid)
        label = OBJECTIVE_PREFIX + oid
        if rec is None:
            c.log("FAILED", label, "no suitable roof found")
            continue
        wanted.add(label)
        cx, cy, half_l, half_w, yaw = oriented_rect(rec["ring"])
        roof_z = rec["height_m"] * 100.0
        loc = unreal.Vector(cx, cy, roof_z + OBJECTIVE_ABOVE_ROOF + OBJECTIVE_HALF_HEIGHT)
        rot = unreal.Rotator(0.0, 0.0, yaw)
        extent = unreal.Vector(half_l, half_w, OBJECTIVE_HALF_HEIGHT)
        tags = ["City", "CityObjective", "osm:" + rec["id"], "objective:" + oid]

        actor = existing.get(label)
        n = 0
        if actor is not None and not isinstance(actor, volume_class):
            actor.destroy_actor()
            actor = None
            n += 1
        if actor is None:
            actor = c.spawn_actor(volume_class, loc, rot, label=label)
            if actor is None:
                c.log("FAILED", label, "spawn_actor returned None")
                continue
            existing[label] = actor
            n += 1
        if not same_vector(actor.get_actor_location(), loc, 1.0):
            actor.set_actor_location(loc, False, True)
            n += 1
        if abs(((actor.get_actor_rotation().yaw - yaw) + 180.0) % 360.0 - 180.0) > 0.1:
            actor.set_actor_rotation(rot, False)
            n += 1
        if not same_vector(actor.get_actor_scale3d(), unreal.Vector(1.0, 1.0, 1.0), 1e-4):
            actor.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
            n += 1
        box = actor.get_editor_property("collision_component")
        if box is not None and not same_vector(box.get_editor_property("box_extent"), extent, 1.0):
            box.set_editor_property("box_extent", extent)
            n += 1
        if str(actor.get_editor_property("objective_id")) != oid:
            actor.set_editor_property("objective_id", unreal.Name(oid))
            n += 1
        if [str(t) for t in actor.get_editor_property("tags")] != tags:
            actor.set_editor_property("tags", [unreal.Name(t) for t in tags])
            n += 1
        changes += n
        tags_rec = rec.get("tags", {})
        address = "{0} {1}".format(tags_rec.get("addr:housenumber") or "", tags_rec.get("addr:street") or "").strip()
        c.log("updated" if n else "exists", label, "osm {0}, {1:.1f} m, {2}, roof box {3:.0f} x {4:.0f} m".format(
            rec["id"], rec["height_m"], address or "no address", half_l / 50.0, half_w / 50.0))

    for label, actor in list(existing.items()):
        if isinstance(actor, volume_class) and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            changes += 1
            c.log("updated", label, "removed stray objective volume")
    return changes


# Each objective roof also gets a beacon: a 1 m pole with an emissive purple cap and a faint
# purple point light, at the end of the roof nearest the start, so the roof reads from the street
# at night. Labels City_Beacon_<id> (pole), City_BeaconTop_<id> (cap), City_BeaconLight_<id>.
BEACON_PREFIX = "City_Beacon_"
BEACON_TOP_PREFIX = "City_BeaconTop_"
BEACON_LIGHT_PREFIX = "City_BeaconLight_"
BEACON_HEIGHT = 100.0             # cm, roof to the top of the cap
BEACON_CAP = 10.0                 # cm of the height that is the emissive cap
BEACON_POLE_DIAMETER = 6.0
BEACON_CAP_DIAMETER = 12.0
BEACON_INSET = 150.0              # cm in from the end of the roof nearest the start
BEACON_MIN_CLEARANCE = 100.0      # cm from any roof edge, else the beacon goes to the roof centre
BEACON_LUMENS = 300.0
BEACON_RADIUS = 1000.0
BEACON_COLOR = (0.62, 0.25, 1.0)  # Kate purple
BEACON_EMISSIVE = 4.0             # before m.EMISSIVE_INTENSITY_FACTOR
MI_BEACON = m.MATERIALS_PATH + "/MI_ObjectiveBeacon"


def beacon_spots(district):
    """{objective id: (x, y, roof z, how it was placed)} for every objective roof.

    On the roof's long axis, BEACON_INSET in from the end nearer the start: the side seen first,
    and clear of the RoofPair, who stand either side of the centre. The other end if that one is
    not BEACON_MIN_CLEARANCE inside the outline, the roof's centre if neither is."""
    start, _rot = player_start_transform(district)
    start = (start.x, start.y)
    out = {}
    for oid, rec in objective_roofs(district).items():
        ring = rec["ring"]
        cx, cy, half_l, _hw, yaw = oriented_rect(ring)
        ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        reach = max(half_l - BEACON_INSET, 0.0)
        ends = [(cx + ux * reach, cy + uy * reach), (cx - ux * reach, cy - uy * reach)]
        ends.sort(key=lambda p: math.hypot(p[0] - start[0], p[1] - start[1]))
        how, spot = next((("end", p) for p in ends if _edge_clearance(p, ring) >= BEACON_MIN_CLEARANCE),
                         ("centre", (cx, cy)))
        out[oid] = (spot[0], spot[1], rec["height_m"] * 100.0, how)
    return out


def _ensure_no_collision(actor, label):
    comp = actor.get_editor_property("static_mesh_component")
    if str(comp.get_collision_profile_name()) != "NoCollision":
        comp.set_collision_profile_name("NoCollision")
        return 1
    return 0


def _ensure_beacon_light(existing, label, loc, tags):
    changes = 0
    actor = existing.get(label)
    if actor is not None and not isinstance(actor, unreal.PointLight):
        actor.destroy_actor()
        actor = None
    if actor is None:
        actor = c.spawn_actor(unreal.PointLight, loc, label=label)
        if actor is None:
            c.log("FAILED", label, "spawn_actor returned None")
            return 0
        existing[label] = actor
        changes += 1
    if not same_vector(actor.get_actor_location(), loc, 0.5):
        actor.set_actor_location(loc, False, True)
        changes += 1
    comp = actor.get_editor_property("point_light_component")
    changes += set_if_different(comp, "mobility", unreal.ComponentMobility.MOVABLE, label)
    changes += set_if_different(comp, "intensity_units", unreal.LightUnits.LUMENS, label)
    changes += set_if_different(comp, "intensity", BEACON_LUMENS, label, 0.5)
    changes += set_if_different(comp, "attenuation_radius", BEACON_RADIUS, label, 0.5)
    changes += set_if_different(comp, "cast_shadows", False, label)
    changes += set_if_different(comp, "use_temperature", False, label)
    want = unreal.Color(r=int(BEACON_COLOR[0] * 255), g=int(BEACON_COLOR[1] * 255), b=int(BEACON_COLOR[2] * 255), a=255)
    have = comp.get_editor_property("light_color")
    if (have.r, have.g, have.b) != (want.r, want.g, want.b):
        comp.set_editor_property("light_color", want)
        changes += 1
    return changes + _ensure_tags(actor, tags)


def ensure_objective_beacons(district, existing):
    cylinder = c.load_or_none(CYLINDER)
    if cylinder is None:
        c.log("FAILED", BEACON_PREFIX + "*", "engine cylinder not found")
        return 0
    emissive = m.ensure_material(m.M_EMISSIVE, m._build_emissive)
    cap_mat = m.ensure_material_instance(
        MI_BEACON, emissive,
        vectors=[(m.EMISSIVE_COLOR_PARAM, BEACON_COLOR)],
        scalars=[(m.EMISSIVE_INTENSITY_PARAM, BEACON_EMISSIVE * m.EMISSIVE_INTENSITY_FACTOR)])
    pole_mat = m.ensure_steel_painted()
    spots = beacon_spots(district)
    changes = 0
    wanted = set()
    no_rot = unreal.Rotator(0.0, 0.0, 0.0)
    for oid in OBJECTIVE_IDS:
        spot = spots.get(oid)
        if spot is None:
            c.log("FAILED", BEACON_PREFIX + oid, "no objective roof")
            continue
        x, y, roof_z, how = spot
        tags = ["City", "CityBeacon", "objective:" + oid]
        pole_label, top_label, light_label = BEACON_PREFIX + oid, BEACON_TOP_PREFIX + oid, BEACON_LIGHT_PREFIX + oid
        wanted.update((pole_label, top_label, light_label))
        pole_h = BEACON_HEIGHT - BEACON_CAP
        n = _ensure_mesh_actor(
            existing, pole_label, cylinder, pole_mat, unreal.Vector(x, y, roof_z + pole_h * 0.5), no_rot,
            unreal.Vector(BEACON_POLE_DIAMETER / 100.0, BEACON_POLE_DIAMETER / 100.0, pole_h / 100.0), tags)
        n += _ensure_mesh_actor(
            existing, top_label, cylinder, cap_mat, unreal.Vector(x, y, roof_z + pole_h + BEACON_CAP * 0.5), no_rot,
            unreal.Vector(BEACON_CAP_DIAMETER / 100.0, BEACON_CAP_DIAMETER / 100.0, BEACON_CAP / 100.0), tags)
        for label in (pole_label, top_label):
            if label in existing:
                n += _ensure_no_collision(existing[label], label)
        n += _ensure_beacon_light(existing, light_label, unreal.Vector(x, y, roof_z + BEACON_HEIGHT + 20.0), tags)
        changes += n
        c.log("updated" if n else "exists", pole_label, "at {0:.0f}, {1:.0f} on a {2:.1f} m roof ({3})".format(
            x, y, roof_z / 100.0, how))

    for label, actor in list(existing.items()):
        if label.startswith((BEACON_PREFIX, BEACON_TOP_PREFIX, BEACON_LIGHT_PREFIX)) and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            changes += 1
            c.log("updated", label, "removed stray beacon")
    return changes


# --------------------------------------------------------------------------------------
# chapter 1's end: an arrow in the find_arrow roof's water tower (AChapterEndInteractable)
# --------------------------------------------------------------------------------------

CHAPTER_END_LABEL = "City_ChapterEnd"
CHAPTER_END_TOWER_LABEL = "City_ChapterEndTower"
SCENE_RETURN_LABEL = "City_SceneReturn_FB00"   # DA_FB00_Placeholder's ReturnPointLabel
CHAPTER_END_VERSION = 1                        # bump when an arrow mesh recipe changes
TOWER_TANK_RADIUS = 150.0                      # cm; SM_City_WaterTower's tank
TOWER_FOOTPRINT = 170.0                        # cm; its deck
TOWER_EDGE_CLEAR = 230.0                       # cm from the roof edge to the tower's centre
TOWER_BEACON_RANGE = (400.0, 900.0)            # cm from the find_arrow beacon
TOWER_BEACON_IDEAL = 550.0
TOWER_ARCHER_LINE_CLEAR = 320.0                # cm from each archer's line to the beacon
TOWER_GRID = 50.0
ARROW_HEIGHT = 320.0                           # cm above the roof: the low part of the tank
ARROW_PITCH = -10.0                            # tip down, as if loosed from a higher roof
ARROW_LENGTH = 90.0
ARROW_EMBED = 12.0
ARROW_SHAFT_RADIUS = 1.4
RETURN_OUT = TOWER_TANK_RADIUS + 170.0         # cm from the tower's centre to the return point
MI_ARROW_SHAFT_KEY = "ArrowShaft"
ARROW_SHAFT_COLOR = ((0.02, 0.02, 0.022), 0.35)


def _arrow_shaft_mesh():
    """Tip at the origin pointing +X, ARROW_EMBED of it inside the surface; shaft and nock along -X."""
    mesh = new_mesh()
    options = unreal.GeometryScriptPrimitiveOptions()
    # Pitch 90 turns the cylinder's +Z onto -X.
    along = unreal.Rotator(roll=0.0, pitch=90.0, yaw=0.0)
    GS_PRIM.append_cylinder(mesh, options, unreal.Transform(location=unreal.Vector(ARROW_EMBED, 0.0, 0.0), rotation=along),
                            ARROW_SHAFT_RADIUS, ARROW_LENGTH, 10, 0, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    tail = ARROW_EMBED - ARROW_LENGTH
    GS_PRIM.append_cylinder(mesh, options, unreal.Transform(location=unreal.Vector(tail, 0.0, 0.0), rotation=along),
                            ARROW_SHAFT_RADIUS + 0.5, 2.0, 10, 0, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    return mesh


def _arrow_fletching_mesh():
    """Three vanes 120 degrees apart near the tail."""
    mesh = new_mesh()
    options = unreal.GeometryScriptPrimitiveOptions()
    x = ARROW_EMBED - ARROW_LENGTH + 10.0
    r = ARROW_SHAFT_RADIUS + 2.6
    for i in range(3):
        angle = 120.0 * i
        rad = math.radians(angle)
        xf = unreal.Transform(location=unreal.Vector(x, -r * math.sin(rad), r * math.cos(rad)),
                              rotation=unreal.Rotator(roll=angle, pitch=0.0, yaw=0.0))
        GS_PRIM.append_box(mesh, options, xf, 16.0, 0.5, 5.5, 0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    return mesh


CHAPTER_END_MESHES = {
    "SM_City_ArrowShaft": _arrow_shaft_mesh,
    "SM_City_ArrowFletching": _arrow_fletching_mesh,
}


def ensure_chapter_end_meshes():
    out = {}
    for name, build in sorted(CHAPTER_END_MESHES.items()):
        spec = geo.record_hash(GENERATOR_VERSION, CHAPTER_END_VERSION, "chapter-end", name, ARROW_LENGTH,
                               ARROW_SHAFT_RADIUS, ARROW_EMBED)
        asset, current = stored_hash(name)
        if asset is not None and current == spec:
            out[name] = asset
            continue
        static_mesh, action = write_static_mesh(build(), name, spec)
        if static_mesh is not None:
            out[name] = static_mesh
            c.log(action, c.asset_path(MESH_DIR, name))
    return out


def chapter_end_spot(district):
    """{tower (x, y), direction to the beacon (dx, dy), roof z} for the find_arrow tower, or None.

    On the find_arrow roof, TOWER_BEACON_RANGE from its beacon (Kate walks from the beacon to it),
    TOWER_EDGE_CLEAR inside the roof, and clear of both archers' lines to the beacon so the tower
    never blocks their shot. The arrow faces the beacon; the return point sits between them."""
    rec = objective_roofs(district).get("find_arrow")
    spot = beacon_spots(district).get("find_arrow")
    if rec is None or spot is None:
        return None
    ring = rec["ring"]
    bx, by, roof_z, _how = spot
    lines = [((a[1], a[2]), (bx, by)) for a in archer_placements(district)]
    xs, ys = [p[0] for p in ring], [p[1] for p in ring]
    best = None
    gx = min(xs) + TOWER_GRID * 0.5
    while gx < max(xs):
        gy = min(ys) + TOWER_GRID * 0.5
        while gy < max(ys):
            pt = (gx, gy)
            gy += TOWER_GRID
            d = math.hypot(pt[0] - bx, pt[1] - by)
            clear = _edge_clearance(pt, ring)
            if not (TOWER_BEACON_RANGE[0] <= d <= TOWER_BEACON_RANGE[1]) or clear < TOWER_EDGE_CLEAR:
                continue
            if any(_segment_distance(pt, a, b) < TOWER_ARCHER_LINE_CLEAR for a, b in lines):
                continue
            ux, uy = (bx - pt[0]) / d, (by - pt[1]) / d
            back = (pt[0] + ux * RETURN_OUT, pt[1] + uy * RETURN_OUT)
            if _edge_clearance(back, ring) < 80.0:
                continue
            score = (abs(d - TOWER_BEACON_IDEAL) - 0.2 * clear, round(pt[0]), round(pt[1]))
            if best is None or score < best[0]:
                best = (score, pt, (ux, uy), d)
        gx += TOWER_GRID
    if best is None:
        return None
    return {"tower": best[1], "dir": best[2], "distance": best[3], "roof_z": roof_z, "osm": rec["id"]}


def _ensure_vector_prop(actor, prop, value, label):
    current = actor.get_editor_property(prop)
    if not same_vector(current, value, 0.05):
        actor.set_editor_property(prop, value)
        return 1
    return 0


def _ensure_component_mesh(actor, comp_name, mesh, material):
    comp = actor.get_editor_property(comp_name)
    n = 0
    if comp.get_editor_property("static_mesh") != mesh:
        comp.set_static_mesh(mesh)
        n += 1
    overrides = comp.get_editor_property("override_materials")
    if material is not None and (len(overrides) < 1 or overrides[0] != material):
        comp.set_material(0, material)
        n += 1
    return n


def ensure_chapter_end(district, existing):
    cls = c.find_class("ChapterEndInteractable", "/Script/Hawkeye.ChapterEndInteractable")
    if cls is None:
        c.log("FAILED", CHAPTER_END_LABEL, "AChapterEndInteractable not exposed; build the module")
        return 0
    spot = chapter_end_spot(district)
    if spot is None:
        c.log("FAILED", CHAPTER_END_LABEL, "no spot on the find_arrow roof for the tower")
        return 0
    meshes = ensure_chapter_end_meshes()
    tower_mesh = ensure_clutter_meshes().get("SM_City_WaterTower")
    tower_mat = m.ensure_prop_instance("WaterTower", *CLUTTER_MATERIALS["WaterTower"])
    shaft_mat = m.ensure_prop_instance(MI_ARROW_SHAFT_KEY, *ARROW_SHAFT_COLOR)
    fletch_mat = c.load_or_none(MI_BEACON)
    (tx, ty), (ux, uy), roof_z = spot["tower"], spot["dir"], spot["roof_z"]
    facing = math.degrees(math.atan2(uy, ux))
    tags = ["City", "CityChapterEnd", "osm:" + spot["osm"], "objective:find_arrow"]

    n = _ensure_mesh_actor(existing, CHAPTER_END_TOWER_LABEL, tower_mesh, tower_mat, unreal.Vector(tx, ty, roof_z),
                           unreal.Rotator(0.0, 0.0, facing), unreal.Vector(1.0, 1.0, 1.0), tags)

    # The arrow went in on the side facing the beacon, pointing into the tank.
    loc = unreal.Vector(tx + ux * TOWER_TANK_RADIUS, ty + uy * TOWER_TANK_RADIUS, roof_z + ARROW_HEIGHT)
    actor, k = _ensure_located(existing, CHAPTER_END_LABEL, cls, loc, facing + 180.0)
    n += k
    if actor is None:
        return n
    if str(actor.get_editor_property("objective_id")) != "find_arrow":
        actor.set_editor_property("objective_id", unreal.Name("find_arrow"))
        n += 1
    rot = actor.get_editor_property("prop_rotation")
    if abs(rot.pitch - ARROW_PITCH) > 0.01:
        actor.set_editor_property("prop_rotation", unreal.Rotator(roll=0.0, pitch=ARROW_PITCH, yaw=0.0))
        n += 1
    # Actor frame: +X into the tank, the shaft sticking out along -X toward where she stands. The
    # camera sits below and to the side, so the black shaft reads against the sky.
    n += _ensure_vector_prop(actor, "close_up_offset", unreal.Vector(-190.0, 100.0, -70.0), CHAPTER_END_LABEL)
    n += _ensure_vector_prop(actor, "close_up_look_at", unreal.Vector(-45.0, 0.0, 0.0), CHAPTER_END_LABEL)
    n += _ensure_vector_prop(actor, "interact_zone_offset", unreal.Vector(-160.0, 0.0, -ARROW_HEIGHT + 110.0), CHAPTER_END_LABEL)
    n += _ensure_vector_prop(actor, "interact_zone_extent", unreal.Vector(160.0, 200.0, 140.0), CHAPTER_END_LABEL)
    n += _ensure_component_mesh(actor, "prop", meshes.get("SM_City_ArrowShaft"), shaft_mat)
    n += _ensure_component_mesh(actor, "prop_accent", meshes.get("SM_City_ArrowFletching"), fletch_mat)
    n += _ensure_tags(actor, tags)

    back = unreal.Vector(tx + ux * RETURN_OUT, ty + uy * RETURN_OUT, roof_z + 100.0)
    point, k = _ensure_located(existing, SCENE_RETURN_LABEL, unreal.TargetPoint, back, facing + 180.0)
    n += k
    if point is not None:
        # Tagged with its label too: a packaged game has no labels to find it by.
        n += _ensure_tags(point, ["City", "CitySceneReturn", SCENE_RETURN_LABEL])
    c.log("updated" if n else "exists", CHAPTER_END_LABEL, "tower at ({0:.0f}, {1:.0f}) on osm {2}, {3:.0f} cm from the beacon".format(
        tx, ty, spot["osm"], spot["distance"]))
    return n


# --------------------------------------------------------------------------------------
# chapter 1's first fight: four Tracksuit thugs
# --------------------------------------------------------------------------------------

THUG_PREFIX = "City_Thug_"
PATROL_PREFIX = "City_Patrol_"
THUG_GROUP_LABEL = "City_ThugGroup_clear_roof"
THUG_BP_PATH = "/Game/Blueprints/AI"
THUG_BP_NAME = "BP_Thug"
THUG_HALF_HEIGHT = 96.0           # cm; BP_Thug's capsule
ROOF_PAIR_TAG = "RoofPair"
STREET_PAIR_TAG = "StreetPair"
STREET_GROUP_TAG = "StreetGroup"  # the pair and the heavy: the street fight
HEAVY_BP_NAME = "BP_Thug_Heavy"
HEAVY_PATROL_LENGTH = 2000.0      # cm between the heavy's two patrol points
HEAVY_CORNER_REACH = 4000.0       # cm from the sidewalk line a park vertex may be and still be its corner
HEAVY_PAIR_CLEAR = 500.0          # cm the heavy's patrol keeps from the pair's
HEAVY_CORNER_START = 600.0        # cm along the sidewalk from the corner the heavy's patrol starts at least
ROOF_PAIR_GAP = 300.0             # cm between the two arguing on the roof
ROOF_EDGE_CLEARANCE = 200.0       # cm from any roof edge, so neither stands in the parapet
PATROL_STREET = "Avenue A"
PATROL_LENGTH = 4000.0            # cm between the two patrol points
PATROL_POINT_HEIGHT = 100.0       # cm above the sidewalk
STREET_PAIR_SPACING = 150.0       # cm; the second walks this far ahead of the first
CROSSING_CLEARANCE = 300.0        # cm; a patrol point this far from any other road's carriageway

# Every thug keeps AThugCharacter's own "Thug" tag first: the takedown looks for it and a thug's
# swing skips anyone carrying it. The labels' other tags come after.
# (weapon, group tag) per thug, in label order: the roof pair (one fists, one bat), the street
# pair (one bat, one gunner), then the heavy on his own patrol.
THUG_LOADOUT = (("FISTS", ROOF_PAIR_TAG), ("BAT", ROOF_PAIR_TAG),
                ("BAT", STREET_PAIR_TAG), ("PISTOL", STREET_PAIR_TAG),
                ("SHIELD", STREET_GROUP_TAG))


def roof_pair_spots(district):
    """((x, y, z, yaw), (x, y, z, yaw), building record) for the two on the cross_block roof.

    They stand ROOF_PAIR_GAP apart along the roof's long axis through the centre of its oriented
    rectangle, facing each other; the short axis and then a smaller gap are tried if the long
    one puts either of them within ROOF_EDGE_CLEARANCE of an edge."""
    rec = objective_roofs(district).get("cross_block")
    if rec is None:
        return None
    cx, cy, _half_l, _half_w, yaw = oriented_rect(rec["ring"])
    z = rec["height_m"] * 100.0 + THUG_HALF_HEIGHT + 2.0
    for gap in (ROOF_PAIR_GAP, ROOF_PAIR_GAP * 0.6):
        for axis_yaw in (yaw, yaw + 90.0):
            ux, uy = math.cos(math.radians(axis_yaw)), math.sin(math.radians(axis_yaw))
            a = (cx - ux * gap * 0.5, cy - uy * gap * 0.5)
            b = (cx + ux * gap * 0.5, cy + uy * gap * 0.5)
            if min(_edge_clearance(a, rec["ring"]), _edge_clearance(b, rec["ring"])) >= ROOF_EDGE_CLEARANCE:
                return (a[0], a[1], z, axis_yaw), (b[0], b[1], z, axis_yaw + 180.0), rec
    return None


def _clear_of_crossings(pt, district, own):
    for rec in district.roads:
        if rec.get("name") == own:
            continue
        for piece in rec["pieces"]:
            hit = closest_point_on_polyline(pt, district.ring_cm(piece))
            if hit and hit[0] < rec["width_m"] * 50.0 + CROSSING_CLEARANCE:
                return False
    return True


def _park_sidewalk(district):
    """(q, d, n, offset): the Avenue A centre-line point nearest the park's centroid, the avenue's
    direction there, the normal toward the park, and how far the park-side sidewalk's middle is
    from the centre line. None without a park or the avenue."""
    park = district.parks[0] if district.parks else None
    if park is None:
        return None
    target = geo.centroid(district.ring_cm(park["outer"]))
    best = None
    for rec in district.roads:
        if rec.get("name") != PATROL_STREET:
            continue
        path = district.ring_cm(rec["pieces"][0]) if rec["pieces"] else []
        for i in range(len(path) - 1):
            hit = closest_point_on_polyline(target, path[i:i + 2])
            if hit and (best is None or hit[0] < best[0]):
                best = (hit[0], hit[1], path[i], path[i + 1], rec["width_m"])
    if best is None:
        return None
    _d, q, a, b, width_m = best
    length = math.hypot(b[0] - a[0], b[1] - a[1]) or 1.0
    d = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
    n = (-d[1], d[0])
    if (target[0] - q[0]) * n[0] + (target[1] - q[1]) * n[1] < 0.0:
        n = (-n[0], -n[1])
    return q, d, n, width_m * 50.0 + SIDEWALK_WIDTH * 0.5


def street_patrol(district):
    """(P0, P1, direction yaw) on the Avenue A sidewalk on the park side, PATROL_LENGTH apart.

    Centred on the point of the Avenue A centre line nearest the park's centroid, moved along
    the avenue until both points are clear of every crossing street."""
    walk = _park_sidewalk(district)
    if walk is None:
        return None
    q, d, n, offset = walk
    centre = (q[0] + n[0] * offset, q[1] + n[1] * offset)
    half = PATROL_LENGTH * 0.5
    for shift in (0.0, 500.0, -500.0, 1000.0, -1000.0, 1500.0, -1500.0, 2000.0, -2000.0):
        c0 = (centre[0] + d[0] * shift, centre[1] + d[1] * shift)
        p0 = (c0[0] - d[0] * half, c0[1] - d[1] * half)
        p1 = (c0[0] + d[0] * half, c0[1] + d[1] * half)
        if _clear_of_crossings(p0, district, PATROL_STREET) and _clear_of_crossings(p1, district, PATROL_STREET):
            return p0, p1, math.degrees(math.atan2(d[1], d[0]))
    return None


def heavy_patrol(district):
    """(P0, P1, direction yaw) for the heavy: HEAVY_PATROL_LENGTH of the same park-side sidewalk,
    starting as close to the park's south corner (its southernmost vertex, largest Y, on the Avenue A
    side) as the crossing streets allow and heading back toward the pair, clear of their patrol."""
    walk = _park_sidewalk(district)
    pair = street_patrol(district)
    if walk is None or pair is None:
        return None
    q, d, n, offset = walk
    base = (q[0] + n[0] * offset, q[1] + n[1] * offset)
    ring = district.ring_cm(district.parks[0]["outer"])
    near = [v for v in ring if abs((v[0] - base[0]) * n[0] + (v[1] - base[1]) * n[1]) <= HEAVY_CORNER_REACH]
    if not near:
        return None
    corner = max(near, key=lambda v: v[1])
    t_corner = (corner[0] - base[0]) * d[0] + (corner[1] - base[1]) * d[1]
    sign = -1.0 if t_corner > 0.0 else 1.0
    pair_seg = [pair[0], pair[1]]

    def at(t):
        return (base[0] + d[0] * t, base[1] + d[1] * t)

    step = HEAVY_CORNER_START
    while step <= abs(t_corner):
        t0 = t_corner + sign * step
        p0, p1 = at(t0), at(t0 + sign * HEAVY_PATROL_LENGTH)
        clear = (_clear_of_crossings(p0, district, PATROL_STREET) and _clear_of_crossings(p1, district, PATROL_STREET)
                 and closest_point_on_polyline(p0, pair_seg)[0] >= HEAVY_PAIR_CLEAR
                 and closest_point_on_polyline(p1, pair_seg)[0] >= HEAVY_PAIR_CLEAR)
        if clear:
            yaw = math.degrees(math.atan2(d[1] * sign, d[0] * sign))
            return p0, p1, yaw
        step += 250.0
    return None


def thug_placements(district):
    """([(label, x, y, z, yaw, weapon, tags, [patrol labels], blueprint)], [(label, x, y, z, tag)], roof record)."""
    roof = roof_pair_spots(district)
    patrol = street_patrol(district)
    heavy = heavy_patrol(district)
    thugs, points = [], []
    roof_rec = None
    if roof is not None:
        a, b, roof_rec = roof
        for i, spot in enumerate((a, b)):
            weapon, tag = THUG_LOADOUT[i]
            thugs.append((THUG_PREFIX + str(i), spot[0], spot[1], spot[2], spot[3], weapon,
                          ["Thug", "City", "CityThug", tag, "osm:" + roof_rec["id"]], [], THUG_BP_NAME))
    if patrol is not None:
        p0, p1, yaw = patrol
        z_walk = SIDEWALK_TOP
        points = [(PATROL_PREFIX + "0", p0[0], p0[1], z_walk + PATROL_POINT_HEIGHT, STREET_PAIR_TAG),
                  (PATROL_PREFIX + "1", p1[0], p1[1], z_walk + PATROL_POINT_HEIGHT, STREET_PAIR_TAG)]
        ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        for j in range(2):
            weapon, tag = THUG_LOADOUT[2 + j]
            along = STREET_PAIR_SPACING * j
            thugs.append((THUG_PREFIX + str(2 + j), p0[0] + ux * along, p0[1] + uy * along,
                          z_walk + THUG_HALF_HEIGHT + 2.0, yaw, weapon,
                          ["Thug", "City", "CityThug", tag, "street:" + PATROL_STREET, STREET_GROUP_TAG],
                          [PATROL_PREFIX + "1", PATROL_PREFIX + "0"], THUG_BP_NAME))
    if patrol is not None and heavy is not None:
        h0, h1, h_yaw = heavy
        z_walk = SIDEWALK_TOP
        points += [(PATROL_PREFIX + "2", h0[0], h0[1], z_walk + PATROL_POINT_HEIGHT, STREET_GROUP_TAG),
                   (PATROL_PREFIX + "3", h1[0], h1[1], z_walk + PATROL_POINT_HEIGHT, STREET_GROUP_TAG)]
        weapon, tag = THUG_LOADOUT[4]
        thugs.append((THUG_PREFIX + "4", h0[0], h0[1], z_walk + THUG_HALF_HEIGHT + 2.0, h_yaw, weapon,
                      ["Thug", "City", "CityThug", tag, "street:" + PATROL_STREET],
                      [PATROL_PREFIX + "3", PATROL_PREFIX + "2"], HEAVY_BP_NAME))
    return thugs, points, roof_rec


def _ensure_located(existing, label, cls, loc, yaw):
    """(actor, changes): spawned if missing or of the wrong class, moved and turned if off."""
    changes = 0
    # A Python wrapper type (unreal.TargetPoint) never equals the UClass get_class() returns.
    if isinstance(cls, type):
        cls = cls.static_class()
    actor = existing.get(label)
    if actor is not None and actor.get_class() != cls:
        actor.destroy_actor()
        actor = None
    if actor is None:
        actor = c.spawn_actor(cls, loc, unreal.Rotator(0.0, 0.0, yaw), label=label)
        if actor is None:
            c.log("FAILED", label, "spawn_actor returned None")
            return None, 0
        existing[label] = actor
        changes += 1
    if not same_vector(actor.get_actor_location(), loc, 0.5):
        actor.set_actor_location(loc, False, True)
        changes += 1
    if abs(((actor.get_actor_rotation().yaw - yaw) + 180.0) % 360.0 - 180.0) > 0.05:
        actor.set_actor_rotation(unreal.Rotator(0.0, 0.0, yaw), False)
        changes += 1
    return actor, changes


def _ensure_thug_props(actor, weapon, patrol_actors):
    changes = 0
    wanted = getattr(unreal.ThugWeapon, weapon)
    if actor.get_editor_property("weapon") != wanted:
        actor.set_editor_property("weapon", wanted)
        changes += 1
    current = [a.get_actor_label() if a else "" for a in actor.get_editor_property("patrol_points")]
    if current != [a.get_actor_label() for a in patrol_actors]:
        actor.set_editor_property("patrol_points", patrol_actors)
        changes += 1
    return changes


def ensure_thugs(district, existing):
    """City_Thug_<n>, City_Patrol_<n> and City_ThugGroup_clear_roof. Idempotent by label."""
    thug_cls = c.load_generated_class(THUG_BP_PATH, THUG_BP_NAME)
    group_cls = c.find_class("ThugGroupObjective", "/Script/Hawkeye.ThugGroupObjective")
    if thug_cls is None or group_cls is None or not hasattr(unreal, "ThugWeapon"):
        c.log("FAILED", THUG_PREFIX + "*", "BP_Thug or AThugGroupObjective missing; build and run create_world_blueprints.py")
        return 0
    thugs, points, roof_rec = thug_placements(district)
    if len(thugs) != len(THUG_LOADOUT) or len(points) != 4:
        c.log("FAILED", THUG_PREFIX + "*", "found {0} thug spots and {1} patrol points".format(len(thugs), len(points)))
    classes = {THUG_BP_NAME: thug_cls, HEAVY_BP_NAME: c.load_generated_class(THUG_BP_PATH, HEAVY_BP_NAME)}
    if classes[HEAVY_BP_NAME] is None:
        c.log("FAILED", THUG_PREFIX + "4", HEAVY_BP_NAME + " missing; run create_enemies.py")
    changes = 0
    wanted = set()
    patrol_actors = {}
    for label, x, y, z, tag in points:
        actor, n = _ensure_located(existing, label, unreal.TargetPoint, unreal.Vector(x, y, z), 0.0)
        if actor is None:
            continue
        wanted.add(label)
        patrol_actors[label] = actor
        changes += n + _ensure_tags(actor, ["City", "CityPatrol", tag])
    for label, x, y, z, yaw, weapon, tags, patrol, bp_name in thugs:
        cls = classes.get(bp_name)
        if cls is None:
            continue
        actor, n = _ensure_located(existing, label, cls, unreal.Vector(x, y, z), yaw)
        if actor is None:
            continue
        wanted.add(label)
        n += _ensure_thug_props(actor, weapon, [patrol_actors[p] for p in patrol if p in patrol_actors])
        n += _ensure_tags(actor, tags)
        changes += n
        c.log("updated" if n else "exists", label, "{0} {1} at ({2:.0f}, {3:.0f}, {4:.0f}) yaw {5:.0f}".format(
            weapon.lower(), tags[3], x, y, z, yaw))
    if roof_rec is not None:
        cx, cy = geo.centroid(roof_rec["ring"])
        loc = unreal.Vector(cx, cy, roof_rec["height_m"] * 100.0 + 100.0)
        actor, n = _ensure_located(existing, THUG_GROUP_LABEL, group_cls, loc, 0.0)
        if actor is not None:
            wanted.add(THUG_GROUP_LABEL)
            if str(actor.get_editor_property("objective_id")) != "clear_roof":
                actor.set_editor_property("objective_id", unreal.Name("clear_roof"))
                n += 1
            if str(actor.get_editor_property("group_tag")) != ROOF_PAIR_TAG:
                actor.set_editor_property("group_tag", unreal.Name(ROOF_PAIR_TAG))
                n += 1
            n += _ensure_tags(actor, ["City", "CityObjective", "osm:" + roof_rec["id"], "objective:clear_roof"])
            changes += n
    for label, actor in list(existing.items()):
        if (label.startswith(THUG_PREFIX) or label.startswith(PATROL_PREFIX)) and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            changes += 1
            c.log("updated", label, "removed stray")
    c.log("updated" if changes else "exists", "chapter 1 thugs",
          "{0} thugs, {1} patrol points, roof osm {2}".format(
              len(thugs), len(points), roof_rec["id"] if roof_rec else "none"))
    return changes


# --------------------------------------------------------------------------------------
# Barney's archers: two BP_Archer on roofs facing the find_arrow roof
# --------------------------------------------------------------------------------------

ARCHER_PREFIX = "City_Archer_"
ARCHER_BP_PATH = "/Game/Blueprints/Bosses"
ARCHER_BP_NAME = "BP_Archer"
ARCHER_PAIR_TAG = "ArcherPair"
ARCHER_MIN_DISTANCE = 1500.0      # cm from the find_arrow beacon (where Kate stands): the archer's band
ARCHER_MAX_DISTANCE = 2500.0
ARCHER_IDEAL_DISTANCE = 2000.0
ARCHER_GRID = 150.0               # cm between candidate spots on a roof
ARCHER_EDGE_CLEARANCE = 250.0     # cm inside the roof edge, so he never stands in the parapet
ARCHER_EYE = THUG_HALF_HEIGHT + 60.0     # cm above his roof
ARCHER_TARGET_CHEST = 130.0       # cm above the find_arrow roof: Kate's chest standing
ARCHER_LINE_MARGIN = 20.0         # cm the line must clear a roof or parapet by
ARCHER_PARAPET = 90.0             # cm; generate's parapets (skipped under 6 m)
ARCHER_MIN_SPREAD_DEG = 35.0      # the two archers this far apart round her
ARCHER_MIN_APART = 800.0          # cm between the two archers
ARCHER_ROOF_PAIR_CLEAR = 3000.0   # preferred distance from the RoofPair, so the first fight stays two on one


def _building_tops(district):
    """[(rec, ring, bounds, top z cm)] for every building, parapet included."""
    out = []
    for rec in district.buildings:
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3:
            continue
        top = rec["height_m"] * 100.0 + (ARCHER_PARAPET if rec["height_m"] >= 6.0 else 0.0)
        out.append((rec, ring, geo.bounds(ring), top))
    return out


def archer_line_blockers(a3, b3, tops):
    """Ids of the buildings whose roof or parapet the straight line a3 -> b3 does not clear."""
    a, b = (a3[0], a3[1]), (b3[0], b3[1])
    x0, x1 = min(a[0], b[0]), max(a[0], b[0])
    y0, y1 = min(a[1], b[1]), max(a[1], b[1])
    blocked = []
    for rec, ring, box, top in tops:
        if box[0] > x1 or box[2] < x0 or box[1] > y1 or box[3] < y0:
            continue
        ts = _segment_params(a, b, ring)
        if not ts:
            continue
        # The line is straight, so its lowest point over the footprint is at an entry or exit.
        low = min(a3[2] + (b3[2] - a3[2]) * t for t in ts)
        if low < top + ARCHER_LINE_MARGIN:
            blocked.append(rec["id"])
    return blocked


def archer_crossed_buildings(a3, b3, tops):
    """Ids of every building whose footprint the line a3 -> b3 passes over."""
    a, b = (a3[0], a3[1]), (b3[0], b3[1])
    return [rec["id"] for rec, ring, _box, _top in tops if _segment_params(a, b, ring)]


def archer_target(district):
    """(x, y, z) of Kate's chest at the find_arrow beacon, or None."""
    spot = beacon_spots(district).get("find_arrow")
    if spot is None:
        return None
    x, y, roof_z, _how = spot
    return (x, y, roof_z + ARCHER_TARGET_CHEST)


def archer_placements(district):
    """[(label, x, y, z, yaw, osm id, distance cm)] for the two archers, best first; [] if none fit.
    Worked out once per District and kept on it (as grapple_reach keeps its GrappleReach): a cache
    keyed on id() could hand a new District the placements of a collected one that had the same id."""
    cached = getattr(district, "_archer_placements", None)
    if cached is None:
        cached = _archer_placements(district)
        district._archer_placements = cached
    return cached


def _archer_placements(district):
    target = archer_target(district)
    roofs = objective_roofs(district)
    if target is None or "find_arrow" not in roofs:
        return []
    taken = {rec["id"] for rec in roofs.values()}
    safehouse = safehouse_spot(district)
    if safehouse is not None:
        taken.add(safehouse["rec"]["id"])
    cross = roofs.get("cross_block")
    cross_centre = cross["centre"] if cross else None
    reach = ARCHER_MAX_DISTANCE + 100.0
    # Every line runs between two points within reach of the target: nothing farther can block it.
    tops = [t for t in _building_tops(district) if not (t[2][0] > target[0] + reach or t[2][2] < target[0] - reach
                                                        or t[2][1] > target[1] + reach or t[2][3] < target[1] - reach)]
    best = {}   # osm id -> (score, x, y, z, yaw, distance)
    for rec, ring, box, _top in tops:
        if rec["id"] in taken or rec["height_m"] < OBJECTIVE_MIN_HEIGHT_M:
            continue
        if box[0] > target[0] + reach or box[2] < target[0] - reach or box[1] > target[1] + reach or box[3] < target[1] - reach:
            continue
        roof_z = rec["height_m"] * 100.0
        gx = box[0] + ARCHER_GRID * 0.5
        while gx < box[2]:
            gy = box[1] + ARCHER_GRID * 0.5
            while gy < box[3]:
                pt = (gx, gy)
                gy += ARCHER_GRID
                d = math.hypot(pt[0] - target[0], pt[1] - target[1])
                if d < ARCHER_MIN_DISTANCE or d > ARCHER_MAX_DISTANCE or _edge_clearance(pt, ring) < ARCHER_EDGE_CLEARANCE:
                    continue
                eye = (pt[0], pt[1], roof_z + ARCHER_EYE)
                if archer_line_blockers(eye, target, tops):
                    continue
                score = abs(d - ARCHER_IDEAL_DISTANCE)
                if cross_centre is not None and math.hypot(pt[0] - cross_centre[0], pt[1] - cross_centre[1]) < ARCHER_ROOF_PAIR_CLEAR:
                    score += 1000.0
                if rec["id"] not in best or score < best[rec["id"]][0]:
                    yaw = math.degrees(math.atan2(target[1] - pt[1], target[0] - pt[0]))
                    best[rec["id"]] = (score, pt[0], pt[1], roof_z + THUG_HALF_HEIGHT + 2.0, yaw, d)
            gx += ARCHER_GRID
    ranked = sorted(best.items(), key=lambda kv: (kv[1][0], kv[0]))
    if not ranked:
        return []
    first_id, first = ranked[0]
    picks = [(first_id, first)]

    def bearing(p):
        return math.degrees(math.atan2(p[2] - target[1], p[1] - target[0]))

    for spread in (ARCHER_MIN_SPREAD_DEG, 15.0, 0.0):
        for osm, cand in ranked[1:]:
            apart = math.hypot(cand[1] - first[1], cand[2] - first[2])
            turn = abs((bearing(cand) - bearing(first) + 180.0) % 360.0 - 180.0)
            if apart >= ARCHER_MIN_APART and turn >= spread:
                picks.append((osm, cand))
                break
        if len(picks) == 2:
            break
    return [(ARCHER_PREFIX + str(i), p[1], p[2], p[3], p[4], osm, p[5]) for i, (osm, p) in enumerate(picks)]


def archer_quiet_roofs(district):
    """Ids of the archers' roofs and every roof their lines to the find_arrow roof cross: no roof clutter."""
    target = archer_target(district)
    placements = archer_placements(district)
    if target is None or not placements:
        return set()
    tops = _building_tops(district)
    quiet = set()
    for _label, x, y, z, _yaw, osm, _d in placements:
        quiet.add(osm)
        quiet.update(archer_crossed_buildings((x, y, z - THUG_HALF_HEIGHT + ARCHER_EYE), target, tops))
    return quiet


def ensure_archers(district, existing):
    """City_Archer_<n> (BP_Archer), tagged ArcherPair. Idempotent by label."""
    cls = c.load_generated_class(ARCHER_BP_PATH, ARCHER_BP_NAME)
    if cls is None or not hasattr(unreal, "ThugWeapon"):
        c.log("FAILED", ARCHER_PREFIX + "*", "BP_Archer missing; build and run create_enemies.py")
        return 0
    placements = archer_placements(district)
    if len(placements) != 2:
        c.log("FAILED", ARCHER_PREFIX + "*", "found {0} archer spots facing find_arrow".format(len(placements)))
    changes = 0
    wanted = set()
    for label, x, y, z, yaw, osm, distance in placements:
        actor, n = _ensure_located(existing, label, cls, unreal.Vector(x, y, z), yaw)
        if actor is None:
            continue
        wanted.add(label)
        if actor.get_editor_property("weapon") != unreal.ThugWeapon.BOW:
            actor.set_editor_property("weapon", unreal.ThugWeapon.BOW)
            n += 1
        n += _ensure_tags(actor, ["Thug", "City", "CityArcher", ARCHER_PAIR_TAG, "osm:" + osm])
        changes += n
        c.log("updated" if n else "exists", label, "on osm {0} at ({1:.0f}, {2:.0f}, {3:.0f}) yaw {4:.0f}, {5:.0f} cm from find_arrow".format(
            osm, x, y, z, yaw, distance))
    for label, actor in list(existing.items()):
        if label.startswith(ARCHER_PREFIX) and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            changes += 1
            c.log("updated", label, "removed stray")
    return changes


# --------------------------------------------------------------------------------------
# street lamps
# --------------------------------------------------------------------------------------

# The lamps' layout (LAMP_SPACING, LAMP_ARM, LAMP_POLE_HEIGHT, ...) and lamp_spots are in _district.
LAMP_PREFIX = "City_Lamp_"          # the light; what verify counts
LAMP_POLE_PREFIX = "City_LampPole_"
LAMP_HEAD_PREFIX = "City_LampHead_"
LAMP_LUMENS = 1100.0
LAMP_RADIUS = 1800.0
LAMP_OUTER_CONE = 70.0              # degrees from straight down; the pool edge
LAMP_INNER_CONE = 35.0
LAMP_COLOR = (1.0, 0.75, 0.45)      # warm sodium
LAMP_EMISSIVE = 2.0                 # before m.EMISSIVE_INTENSITY_FACTOR; higher clips the head to white
MI_STREET_LAMP = m.MATERIALS_PATH + "/MI_StreetLamp"
CYLINDER = "/Engine/BasicShapes/Cylinder"
CUBE = "/Engine/BasicShapes/Cube"


def ensure_lamp_materials():
    # M_LampHead: the glow on the underside only, snow on top; same parameters as M_Emissive.
    parent = m.ensure_look_material(m.M_LAMP_HEAD, m._build_lamp_head)
    head = m.ensure_material_instance(
        MI_STREET_LAMP, parent,
        vectors=[(m.EMISSIVE_COLOR_PARAM, LAMP_COLOR)],
        scalars=[(m.EMISSIVE_INTENSITY_PARAM, LAMP_EMISSIVE * m.EMISSIVE_INTENSITY_FACTOR)])
    pole = m.ensure_steel_painted()
    return pole, head


def _ensure_tags(actor, tags):
    if [str(t) for t in actor.get_editor_property("tags")] != tags:
        actor.set_editor_property("tags", [unreal.Name(t) for t in tags])
        return 1
    return 0


def _ensure_mesh_actor(existing, label, mesh, material, loc, rot, scale, tags):
    changes = 0
    actor = existing.get(label)
    if actor is not None and not isinstance(actor, unreal.StaticMeshActor):
        actor.destroy_actor()
        actor = None
    if actor is None:
        actor = c.spawn_actor(unreal.StaticMeshActor, loc, rot, label=label)
        if actor is None:
            c.log("FAILED", label, "spawn_actor returned None")
            return 0
        existing[label] = actor
        changes += 1
    comp = actor.get_editor_property("static_mesh_component")
    if comp.get_editor_property("mobility") != unreal.ComponentMobility.STATIC:
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        changes += 1
    if comp.get_editor_property("static_mesh") != mesh:
        comp.set_static_mesh(mesh)
        changes += 1
    overrides = comp.get_editor_property("override_materials")
    if material is not None and (len(overrides) < 1 or overrides[0] != material):
        comp.set_material(0, material)
        changes += 1
    if not same_vector(actor.get_actor_location(), loc, 0.5):
        actor.set_actor_location(loc, False, True)
        changes += 1
    if abs(((actor.get_actor_rotation().yaw - rot.yaw) + 180.0) % 360.0 - 180.0) > 0.05:
        actor.set_actor_rotation(rot, False)
        changes += 1
    if not same_vector(actor.get_actor_scale3d(), scale, 1e-4):
        actor.set_actor_scale3d(scale)
        changes += 1
    return changes + _ensure_tags(actor, tags)


LAMP_DOWN = unreal.Rotator(0.0, -90.0, 0.0)   # roll, pitch, yaw: the spot points at the pavement


def _ensure_lamp_light(existing, label, loc, shadows):
    """A downward spot, not a point light: a point light under the head lights the wall above it
    and the head throws a hard dark wedge up the facade. UE rates spot lumens as if the light
    were a point light, so the pool is as bright as the 2500 lm point light it replaced."""
    changes = 0
    actor = existing.get(label)
    if actor is not None and not isinstance(actor, unreal.SpotLight):
        actor.destroy_actor()
        actor = None
    if actor is None:
        actor = c.spawn_actor(unreal.SpotLight, loc, LAMP_DOWN, label=label)
        if actor is None:
            c.log("FAILED", label, "spawn_actor returned None")
            return 0
        existing[label] = actor
        changes += 1
    if not same_vector(actor.get_actor_location(), loc, 0.5):
        actor.set_actor_location(loc, False, True)
        changes += 1
    if abs(actor.get_actor_rotation().pitch - LAMP_DOWN.pitch) > 0.05:
        actor.set_actor_rotation(LAMP_DOWN, False)
        changes += 1
    comp = actor.get_editor_property("spot_light_component")
    changes += set_if_different(comp, "mobility", unreal.ComponentMobility.MOVABLE, label)
    changes += set_if_different(comp, "intensity_units", unreal.LightUnits.LUMENS, label)
    changes += set_if_different(comp, "intensity", LAMP_LUMENS, label, 0.5)
    changes += set_if_different(comp, "attenuation_radius", LAMP_RADIUS, label, 0.5)
    changes += set_if_different(comp, "outer_cone_angle", LAMP_OUTER_CONE, label, 0.01)
    changes += set_if_different(comp, "inner_cone_angle", LAMP_INNER_CONE, label, 0.01)
    changes += set_if_different(comp, "cast_shadows", bool(shadows), label)
    changes += set_if_different(comp, "use_temperature", False, label)
    want = unreal.Color(r=int(LAMP_COLOR[0] * 255), g=int(LAMP_COLOR[1] * 255), b=int(LAMP_COLOR[2] * 255), a=255)
    have = comp.get_editor_property("light_color")
    if (have.r, have.g, have.b) != (want.r, want.g, want.b):
        comp.set_editor_property("light_color", want)
        changes += 1
    return changes + _ensure_tags(actor, ["City", "CityLamp", "shadows:{0}".format(int(bool(shadows)))])


def ensure_street_lamps(district, existing):
    """A lamp every LAMP_SPACING along both sidewalks of every road: pole, emissive head, and a
    movable warm spot light pointing down. Only the lamps around the park cast shadows."""
    cylinder = c.load_or_none(CYLINDER)
    cube = c.load_or_none(CUBE)
    if cylinder is None or cube is None:
        c.log("FAILED", "street lamps", "engine basic shapes not found")
        return 0
    pole_mat, head_mat = ensure_lamp_materials()
    spots = lamp_spots(district)
    changes = 0
    changed_lamps = 0
    tags = ["City", "CityLamp"]
    for i, (x, y, yaw, shadows) in enumerate(spots):
        rad = math.radians(yaw)
        ux, uy = math.cos(rad), math.sin(rad)
        rot = unreal.Rotator(0.0, 0.0, yaw)
        n = _ensure_mesh_actor(
            existing, LAMP_POLE_PREFIX + str(i), cylinder, pole_mat,
            unreal.Vector(x, y, SIDEWALK_TOP + LAMP_POLE_HEIGHT * 0.5), rot,
            unreal.Vector(LAMP_POLE_DIAMETER / 100.0, LAMP_POLE_DIAMETER / 100.0, LAMP_POLE_HEIGHT / 100.0), tags)
        n += _ensure_mesh_actor(
            existing, LAMP_HEAD_PREFIX + str(i), cube, head_mat,
            unreal.Vector(x + ux * LAMP_ARM * 0.5, y + uy * LAMP_ARM * 0.5, SIDEWALK_TOP + LAMP_POLE_HEIGHT - 7.5),
            rot, unreal.Vector((LAMP_ARM + LAMP_POLE_DIAMETER) / 100.0, 0.35, 0.15), tags)
        n += _ensure_lamp_light(
            existing, LAMP_PREFIX + str(i),
            unreal.Vector(x + ux * LAMP_ARM * 0.75, y + uy * LAMP_ARM * 0.75, SIDEWALK_TOP + LAMP_POLE_HEIGHT - 30.0),
            shadows)
        if n:
            changed_lamps += 1
        changes += n

    # Lamps past the end of the list (the roads changed) go.
    removed = 0
    for label, actor in list(existing.items()):
        for prefix in (LAMP_PREFIX, LAMP_POLE_PREFIX, LAMP_HEAD_PREFIX):
            rest = label[len(prefix):] if label.startswith(prefix) else ""
            if rest.isdigit() and int(rest) >= len(spots):
                actor.destroy_actor()
                existing.pop(label, None)
                removed += 1
                break
    changes += removed
    c.log("updated" if (changed_lamps or removed) else "exists", "street lamps",
          "{0} lamps, {1} casting shadows near the park, {2} changed, {3} actor(s) removed".format(
              len(spots), sum(1 for s in spots if s[3]), changed_lamps, removed))
    return changes


# --------------------------------------------------------------------------------------
# ambience: the wind and street bed, and the buzz on the lamps round the park
# --------------------------------------------------------------------------------------

AMBIENCE_LABEL = "City_Ambience"      # AHawkeyeAmbience at street level; the fade reads height above it
LAMP_BUZZ_PREFIX = "City_LampBuzz_"   # an AAmbientSound on each of the lamps nearest the park
LAMP_BUZZ_COUNT = 20
LAMP_BUZZ_HEIGHT = 300.0              # cm up the pole: ATT_Lamp's 400 cm reaches a head under the lamp
LAMP_BUZZ_TAGS = ["City", "CityLamp"]  # CityLamp: the EMP silences it with the light


def _audio_sound(name):
    import create_audio  # noqa: PLC0415 - the generator needs only the paths

    return c.load_or_none(create_audio.sound_path(name))


def _sound_key(value):
    return value.get_path_name().split(".")[0] if value is not None else ""


def _ensure_sound(obj, prop, sound, context):
    if sound is None:
        c.log("skipped", context + "." + prop, "sound not built; run create_audio")
        return 0
    if _sound_key(obj.get_editor_property(prop)) == _sound_key(sound):
        return 0
    c.set_props(obj, [(prop, sound)], context)
    return 1


def ensure_ambience(district, existing):
    """City_Ambience at the ground's centre, street level, with the wind and street loops."""
    cls = c.find_class("HawkeyeAmbience", "/Script/Hawkeye.HawkeyeAmbience")
    if cls is None:
        c.log("skipped", AMBIENCE_LABEL, "AHawkeyeAmbience not built")
        return 0
    cx, cy = geo.centroid(ground_ring_cm(district))
    actor, changes = _ensure_located(existing, AMBIENCE_LABEL, cls, unreal.Vector(cx, cy, 0.0), 0.0)
    if actor is None:
        return changes
    changes += _ensure_sound(actor, "wind_sound", _audio_sound("MS_Amb_Wind"), AMBIENCE_LABEL)
    changes += _ensure_sound(actor, "street_sound", _audio_sound("MS_Amb_Street"), AMBIENCE_LABEL)
    changes += _ensure_tags(actor, ["City"])
    c.log("updated" if changes else "exists", AMBIENCE_LABEL)
    return changes


def lamp_buzz_spots(district):
    """[(x, y)] of the sound on the LAMP_BUZZ_COUNT lamps nearest the park, nearest first."""
    if not district.parks:
        return []
    ring = district.ring_cm(district.parks[0]["outer"])
    ranked = sorted((ring_distance((x, y), ring), i, x, y) for i, (x, y, _yaw, _s) in enumerate(lamp_spots(district)))
    return [(x, y) for _d, _i, x, y in ranked[:LAMP_BUZZ_COUNT]]


def ensure_lamp_buzz(district, existing):
    buzz = _audio_sound("MS_Amb_LampBuzz")
    spots = lamp_buzz_spots(district)
    changes = 0
    for i, (x, y) in enumerate(spots):
        label = LAMP_BUZZ_PREFIX + str(i)
        loc = unreal.Vector(x, y, SIDEWALK_TOP + LAMP_BUZZ_HEIGHT)
        actor, n = _ensure_located(existing, label, unreal.AmbientSound, loc, 0.0)
        if actor is None:
            continue
        n += _ensure_sound(actor.get_editor_property("audio_component"), "sound", buzz, label)
        n += _ensure_tags(actor, LAMP_BUZZ_TAGS)
        changes += n
    removed = 0
    for label, actor in list(existing.items()):
        rest = label[len(LAMP_BUZZ_PREFIX):] if label.startswith(LAMP_BUZZ_PREFIX) else ""
        if rest.isdigit() and int(rest) >= len(spots):
            actor.destroy_actor()
            existing.pop(label, None)
            removed += 1
    changes += removed
    c.log("updated" if changes else "exists", "lamp buzz",
          "{0} lamps nearest the park, {1} removed".format(len(spots), removed))
    return changes


# --------------------------------------------------------------------------------------
# grapple anchors
# --------------------------------------------------------------------------------------

ANCHOR_PREFIX = "City_Anchor_"
ANCHOR_BP_PATH = "/Game/Blueprints/World"
ANCHOR_BP_NAME = "BP_GrappleAnchor"
# Where anchors go (and the reach model that decides it) is _reach: anchor_spots, grapple_reach.


def anchor_reach_report(district):
    """What verify_city and the generator's log say about the anchors: {"no_street": [osm of anchored
    roofs with a sidewalk spot in range of one of their anchors but none reachable from it],
    "street_out_of_reach": [anchored roofs no sidewalk spot is in range of], "street_inside_block": [in range but
    with no open wall on a road], "street_wall_escape": [every street wall too short to hold an anchor clear of its
    fire escape], "pairs": roofs facing each other
    across a street or gap (GrappleReach.across, the anchored one as the target), "pairs_missing": [(to osm, from osm)], "pairs_out_of_range": count, "objectives": [(id, osm, reachable
    from the street, how it is reached from the previous objective roof: "start", "roofs", "fire escape and
    street", or "" when it is not)]}."""
    reach = grapple_reach(district)
    spots = anchor_spots(district)
    by_osm = {}
    for spot in spots:
        by_osm.setdefault(spot[6], []).append(spot)
    street_cells = {}
    for feet in reach.street_spots:
        street_cells.setdefault((int(math.floor(feet[0] / REACH_CELL)), int(math.floor(feet[1] / REACH_CELL))), []).append(feet)
    out = {"no_street": [], "street_out_of_reach": [], "street_inside_block": [], "street_wall_escape": [], "pairs": 0,
           "pairs_missing": [], "pairs_out_of_range": 0, "objectives": []}
    escapes_at = {}
    for e_osm, floor, (ex, ey, _z), _yaw, _drop, _side in fire_escape_spots(district):
        if floor == 1:
            escapes_at.setdefault(e_osm, []).append((ex, ey))
    street_ok = set()
    for i, t in enumerate(reach.tops):
        osm = t[0]["id"]
        if t[0]["height_m"] <= ANCHOR_MIN_HEIGHT_M:
            continue
        in_range = False
        for spot in by_osm.get(osm, []):
            cx, cy = int(math.floor(spot[0] / REACH_CELL)), int(math.floor(spot[1] / REACH_CELL))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for feet in street_cells.get((cx + dx, cy + dy), ()):
                        marker = _anchor_marker(spot)
                        d = math.sqrt((marker[0] - feet[0]) ** 2 + (marker[1] - feet[1]) ** 2 +
                                      (marker[2] - feet[2] - REACH_HALF_HEIGHT) ** 2)
                        # In range as the audit counts it (FGrappleReachQuery::IsInRange, no slack).
                        if REACH_MIN_RANGE <= d <= REACH_RANGE:
                            in_range = True
                            if osm not in street_ok and reach.targetable(feet, spot):
                                street_ok.add(osm)
        if osm not in street_ok:
            # In range of a sidewalk and still not reachable is a placement failure; too tall for the
            # grapple's range from the pavement, with no sidewalk near, inside the block, or a street wall its
            # fire escape fills, is the geometry.
            street = _street_edges(reach, i)
            if not in_range:
                out["street_out_of_reach"].append(osm)
            elif not street:
                out["street_inside_block"].append(osm)
            elif all(e[3] < 2.0 * (FACADE_ANCHOR_ESCAPE_CLEAR + FACADE_ANCHOR_END_CLEAR) and any(
                    closest_point_on_polyline(x, [e[1], e[2]])[0] < 50.0 for x in escapes_at.get(osm, ())) for e in street):
                out["street_wall_escape"].append(osm)
            else:
                out["no_street"].append(osm)
    neighbours = reach.neighbours()
    edges = {}   # from index -> set of indices reached (grapple) or walked onto
    for i, t in enumerate(reach.tops):
        feet_list = reach.roof_spots[t[0]["id"]]
        for j, _gap, _p, _q in reach.across(i):
            osm_j = reach.tops[j][0]["id"]
            if reach.tops[j][0]["height_m"] <= ANCHOR_MIN_HEIGHT_M:
                continue
            if _roof_reached(reach, feet_list, by_osm.get(osm_j, []), osm_j):
                out["pairs"] += 1
                edges.setdefault(i, set()).add(j)
            elif reach.pair_in_range(i, j):
                out["pairs"] += 1
                out["pairs_missing"].append((osm_j, t[0]["id"]))
            else:
                # Farther than the grapple's range from every roof spot: a gap no zip crosses.
                out["pairs_out_of_range"] += 1
        # Roofs sharing a wall: walked or mantled onto a step up or down, else a grapple up.
        for j, gap in neighbours[i]:
            if gap > REACH_TOUCHING:
                continue
            if abs(reach.tops[j][4] - t[4]) <= REACH_WALKABLE_RISE:
                edges.setdefault(i, set()).add(j)
            elif reach.tops[j][0]["height_m"] > ANCHOR_MIN_HEIGHT_M:
                osm_j = reach.tops[j][0]["id"]
                if _roof_reached(reach, feet_list, by_osm.get(osm_j, []), osm_j):
                    edges.setdefault(i, set()).add(j)

    def roofs_from(a):
        seen, queue = {a}, [a]
        while queue:
            k = queue.pop(0)
            for n in sorted(edges.get(k, ())):
                if n not in seen:
                    seen.add(n)
                    queue.append(n)
        return seen

    escapes = {osm for osm, _f, _p, _y, _d, _s in fire_escape_spots(district)}
    roofs = objective_roofs(district)
    previous = None
    for oid in OBJECTIVE_IDS:
        rec = roofs.get(oid)
        if rec is None:
            out["objectives"].append((oid, None, False, ""))
            continue
        i = reach.index[rec["id"]]
        how = "start"
        if previous is not None:
            reached = roofs_from(previous)
            if i in reached:
                how = "roofs"
            elif rec["id"] in street_ok and any(reach.tops[k][0]["id"] in escapes for k in reached):
                # cross_block to find_arrow crosses a street wider than the grapple's range: down a fire
                # escape on the way and up from the sidewalk.
                how = "fire escape and street"
            else:
                how = ""
        out["objectives"].append((oid, rec["id"], rec["id"] in street_ok, how))
        previous = i
    return out


def anchor_class():
    cls = c.load_generated_class(ANCHOR_BP_PATH, ANCHOR_BP_NAME)
    if cls is None:
        c.log("skipped", ANCHOR_BP_NAME, "not found; falling back to AGrappleAnchor (run create_world_blueprints.py)")
        cls = c.find_class("GrappleAnchor", "/Script/Hawkeye.GrappleAnchor")
    return cls


# --------------------------------------------------------------------------------------
# traversable ledges and parkour test obstacles
# --------------------------------------------------------------------------------------

# Where the ledges go, and why the slab is shaped as it is, is _ledges (ledge_spots).
# BP_TraversableBlock (create_world_blueprints.py) is LevelBlock_Traversable with the sample's
# level-style lookup switched off; the sample's cast accepts it.
TRAVERSABLE_PATH = "/Game/Blueprints/World"
TRAVERSABLE_NAME = "BP_TraversableBlock"

# --------------------------------------------------------------------------------------
# fire escapes (where they go is _fire_escapes.fire_escape_spots)
# --------------------------------------------------------------------------------------


def fire_escape_class():
    """AFireEscapeLanding's UClass (the spawner's property wants the class object, not the Python type)."""
    return c.find_class("/Script/Hawkeye.FireEscapeLanding")


# The ledges, the anchors and the fire escapes are not saved in the map: generate_city writes
# them into a UCityLedgeData and City_LedgeSpawner (ACityLedgeSpawner) spawns them at BeginPlay.
CITY_PROPS_PATH = "/Game/City/EastVillage"
CITY_PROPS_NAME = "DA_EastVillage_CityProps"
SPAWNER_LABEL = "City_LedgeSpawner"

TEST_VAULT_LABEL = "City_Test_Vault"
TEST_MANTLE_LABEL = "City_Test_Mantle"
PARK_WALL_PREFIX = "City_ParkWall_"
TEST_BLOCKS = (
    # label, tag, along the street from the PlayerStart (cm), width, depth, height
    (TEST_VAULT_LABEL, "CityTestVault", 700.0, 250.0, 60.0, 90.0),
    (TEST_MANTLE_LABEL, "CityTestMantle", -700.0, 250.0, 150.0, 150.0),
)
PARK_WALLS = (
    # in from the park edge nearest the PlayerStart (cm), width, depth, height
    (600.0, 400.0, 40.0, 70.0),
    (1200.0, 400.0, 40.0, 90.0),
    (1800.0, 400.0, 40.0, 100.0),
)


def traversable_class():
    return c.load_generated_class(TRAVERSABLE_PATH, TRAVERSABLE_NAME)


def traversable_channel():
    """ECC_GameTraceChannel1 as Python names it: by its display name, which in this project is
    Weapon (the sample calls the same channel Traversable)."""
    for name in ("ECC_WEAPON", "ECC_GAME_TRACE_CHANNEL1", "ECC_TRAVERSABLE"):
        if hasattr(unreal.CollisionChannel, name):
            return getattr(unreal.CollisionChannel, name)
    return None


def _set_components(actor, visible, traversable_only, context):
    """Hide the block's height labels (and, for ledges, the block), and for ledges block only the
    Traversable channel so nothing but the sample's sweep ever meets them."""
    changes = 0
    for text in actor.get_components_by_class(unreal.TextRenderComponent):
        changes += set_if_different(text, "visible", False, context)
    channel = traversable_channel()
    for mesh in actor.get_components_by_class(unreal.StaticMeshComponent):
        changes += set_if_different(mesh, "visible", bool(visible), context)
        if not traversable_only or channel is None:
            continue
        if mesh.get_collision_enabled() != unreal.CollisionEnabled.QUERY_ONLY:
            mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
            changes += 1
        pawn = mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)
        trace = mesh.get_collision_response_to_channel(channel)
        seen = mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)
        if pawn != unreal.CollisionResponseType.ECR_IGNORE or seen != unreal.CollisionResponseType.ECR_IGNORE \
                or trace != unreal.CollisionResponseType.ECR_BLOCK:
            _set_trace_only_body(mesh, context)
            changes += 1
    return changes


LEDGE_IGNORED_CHANNELS = ("WorldStatic", "WorldDynamic", "Pawn", "Visibility", "Camera", "PhysicsBody",
                          "Vehicle", "Destructible")
LEDGE_BLOCKED_CHANNEL = "Weapon"   # ECC_GameTraceChannel1, the sample's Traversable


def _response_channel(name, response):
    entry = unreal.ResponseChannel()
    entry.set_editor_property("channel", name)
    entry.set_editor_property("response", response)
    return entry


def _set_trace_only_body(mesh, context):
    """Query only, ignore everything but the Traversable channel. Written through the body
    instance property (not the runtime setters) so it is recorded as an instance edit and
    survives the construction script re-running when the map loads."""
    try:
        body = mesh.get_editor_property("body_instance")
        body.set_editor_property("collision_profile_name", "Custom")
        body.set_editor_property("collision_enabled", unreal.CollisionEnabled.QUERY_ONLY)
        body.set_editor_property("object_type", unreal.CollisionChannel.ECC_WORLD_STATIC)
        responses = body.get_editor_property("collision_responses")
        ignore = unreal.CollisionResponseType.ECR_IGNORE
        array = [_response_channel(name, ignore) for name in LEDGE_IGNORED_CHANNELS]
        array.append(_response_channel(LEDGE_BLOCKED_CHANNEL, unreal.CollisionResponseType.ECR_BLOCK))
        responses.set_editor_property("response_array", array)
        body.set_editor_property("collision_responses", responses)
        mesh.set_editor_property("body_instance", body)
    except Exception as exc:  # noqa: BLE001
        c.log_error("collision " + context, exc)
    # And the live component, so the check above reads the new responses straight away.
    mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    mesh.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    mesh.set_collision_response_to_channel(traversable_channel(), unreal.CollisionResponseType.ECR_BLOCK)


def _ensure_block(existing, label, cls, origin, yaw, scale, tags, visible, traversable_only):
    """A BP_TraversableBlock at origin (its bottom corner), turned yaw, scaled. Returns changes."""
    loc = unreal.Vector(*origin)
    rot = unreal.Rotator(0.0, 0.0, yaw)
    want_scale = unreal.Vector(*scale)
    changes = 0
    actor = existing.get(label)
    if actor is not None and actor.get_class() != cls:
        actor.destroy_actor()
        actor = None
    if actor is None:
        actor = c.spawn_actor(cls, loc, rot, label=label)
        if actor is None:
            c.log("FAILED", label, "spawn_actor returned None")
            return 0
        existing[label] = actor
        changes += 1
    if not same_vector(actor.get_actor_location(), loc, 0.05):
        actor.set_actor_location(loc, False, True)
        changes += 1
    if abs(((actor.get_actor_rotation().yaw - yaw) + 180.0) % 360.0 - 180.0) > 0.01:
        actor.set_actor_rotation(rot, False)
        changes += 1
    if not same_vector(actor.get_actor_scale3d(), want_scale, 1e-4):
        actor.set_actor_scale3d(want_scale)
        changes += 1
    changes += _set_components(actor, visible, traversable_only, label)
    if actor.get_actor_label() != label:
        actor.set_actor_label(label)
        changes += 1
    return changes + _ensure_tags(actor, tags)


def _ledge_edge_index(label):
    return int(label.rsplit("_", 1)[1])


def _xform_key(x):
    """A clutter instance for the props hash: location, the whole rotation and the scale, to 0.1. The December
    dressing tilts and stretches its instances (wreaths, awnings, wires), so yaw and one scale are not enough."""
    r = x.rotation.rotator()
    return [round(v, 1) + 0.0 for v in (x.translation.x, x.translation.y, x.translation.z, r.yaw, r.pitch, r.roll,
                                        x.scale3d.x, x.scale3d.y, x.scale3d.z)]


def city_props_hash(ledges, anchors, ledge_cls, anchor_cls, fire_escapes=None, clutter=None):
    """What the props asset was written from: every ledge, anchor, fire-escape spot and clutter
    instance, and the classes, meshes and materials."""
    parts = [[[s[0], list(s[2]), s[3], list(s[4])] for s in ledges],
             [list(a) for a in anchors], c.safe_name(ledge_cls), c.safe_name(anchor_cls)]
    if fire_escapes:
        parts.append([[f[0], f[1], list(f[2]), f[3], f[4], f[5]] for f in fire_escapes])
        parts.append([list(FIRE_ESCAPE_SLAB), FIRE_ESCAPE_GAP, FIRE_ESCAPE_RAIL, FIRE_ESCAPE_RAIL_THICK])
    if clutter:
        parts.append([[kind, c.safe_name(mesh), c.safe_name(material), collision, shadow,
                       [_xform_key(x) for x in xforms]]
                      for kind, mesh, material, collision, shadow, xforms in clutter])
    return geo.record_hash(GENERATOR_VERSION, "props", *parts)


def _ledge_record(spot):
    label, osm, origin, yaw, scale, _a, _b, _spec = spot
    rec = unreal.CityLedgeRecord()
    rec.set_editor_property("transform", unreal.Transform(
        unreal.Vector(*origin), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(*scale)))
    rec.set_editor_property("edge_index", _ledge_edge_index(label))
    rec.set_editor_property("osm_id", osm)
    return rec


def _anchor_record(index, spot):
    x, y, z, yaw, forward, drop, osm = spot
    rec = unreal.CityAnchorRecord()
    rec.set_editor_property("transform", unreal.Transform(
        unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(1.0, 1.0, 1.0)))
    rec.set_editor_property("landing_offset", unreal.Vector(forward, 0.0, -drop))
    rec.set_editor_property("index", index)
    rec.set_editor_property("osm_id", osm)
    return rec


def _fire_escape_record(spot):
    osm, floor, (x, y, z), yaw, drop, side = spot
    rec = unreal.CityFireEscapeRecord()
    rec.set_editor_property("transform", unreal.Transform(
        unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(1.0, 1.0, 1.0)))
    rec.set_editor_property("slab_size", unreal.Vector(*FIRE_ESCAPE_SLAB))
    rec.set_editor_property("facade_gap", FIRE_ESCAPE_GAP)
    rec.set_editor_property("rail_height", FIRE_ESCAPE_RAIL)
    rec.set_editor_property("rail_thickness", FIRE_ESCAPE_RAIL_THICK)
    rec.set_editor_property("ladder_drop", drop)
    rec.set_editor_property("ladder_side", side)
    rec.set_editor_property("floor", floor)
    rec.set_editor_property("osm_id", osm)
    return rec


def ensure_city_props(district):
    """DA_EastVillage_CityProps: every roof-edge ledge and grapple anchor as data. Returns
    (asset, ledge class, anchor class), the asset None on failure. Saved only when its hash changes."""
    ledge_cls = traversable_class()
    anchor_cls = anchor_class()
    if ledge_cls is None:
        c.log("FAILED", LEDGE_PREFIX + "*",
              "BP_TraversableBlock not found; run Tools\\create-content.ps1 (import_gasp, world blueprints)")
    if anchor_cls is None:
        c.log("FAILED", ANCHOR_PREFIX + "*", "no grapple anchor class; build the module")
    data_cls = getattr(unreal, "CityLedgeData", None)
    if data_cls is None:
        c.log("FAILED", CITY_PROPS_NAME, "UCityLedgeData not found; build the module")
        return None, ledge_cls, anchor_cls

    ledges = ledge_spots(district)
    anchors = anchor_spots(district)
    escapes = fire_escape_spots(district)
    clutter, plan = clutter_groups(district, ensure_clutter_meshes(), ensure_clutter_materials())
    # The December dressing of the hero block rides along as more clutter groups (Dress_*): _dressing_build.py.
    dressing, dress_doc = dressing_groups()
    clutter = clutter + dressing
    want_hash = city_props_hash(ledges, anchors, ledge_cls, anchor_cls, escapes, clutter)
    full = c.asset_path(CITY_PROPS_PATH, CITY_PROPS_NAME)
    asset = c.load_or_none(full)
    if asset is None:
        factory = c.new_factory("DataAssetFactory")
        c.set_props(factory, [("data_asset_class", data_cls)], "DataAssetFactory")
        asset, _created = c.create_asset(CITY_PROPS_NAME, CITY_PROPS_PATH, data_cls, factory, quiet=True)
        if asset is None:
            return None, ledge_cls, anchor_cls
    summary = "{0} ledges on {1} buildings, {2} anchors ({7} on facades) on {3} roofs, {4} fire-escape landings on {5} buildings, clutter: {6}".format(
        len(ledges), len({s[1] for s in ledges}), len(anchors), len({a[6] for a in anchors}),
        len(escapes), len({f[0] for f in escapes}),
        ", ".join("{0} {1}".format(len(v), k) for k, v in sorted(plan.items()) if v),
        sum(1 for a in anchors if a[4] == FACADE_ANCHOR_LANDING))
    if dress_doc is not None:
        summary += "; dressing {0}: {1} records as {2} instances in {3} groups".format(
            dress_doc["block"], len(dress_doc["records"]), sum(len(g[5]) for g in dressing), len(dressing))
    if str(asset.get_editor_property("source_hash")) == want_hash \
            and len(asset.get_editor_property("ledges")) == len(ledges) \
            and len(asset.get_editor_property("anchors")) == len(anchors) \
            and len(asset.get_editor_property("fire_escapes")) == len(escapes) \
            and len(asset.get_editor_property("clutter")) == len(clutter):
        c.log("exists", full, summary + "; hash unchanged")
        return asset, ledge_cls, anchor_cls
    asset.set_editor_property("ledges", [_ledge_record(spot) for spot in ledges])
    asset.set_editor_property("anchors", [_anchor_record(i, spot) for i, spot in enumerate(anchors)])
    asset.set_editor_property("fire_escapes", [_fire_escape_record(spot) for spot in escapes])
    asset.set_editor_property("clutter", [_clutter_record(group) for group in clutter])
    asset.set_editor_property("source_hash", want_hash)
    c.save(asset)
    c.log("updated", full, summary)
    return asset, ledge_cls, anchor_cls


def ensure_ledge_spawner(district, existing):
    """City_LedgeSpawner (ACityLedgeSpawner) pointing at the props asset, and none of the ledges or
    anchors the map used to carry as saved actors. Returns changes."""
    asset, ledge_cls, anchor_cls = ensure_city_props(district)
    changes = 0
    removed = 0
    for label, actor in list(existing.items()):
        rest = label[len(ANCHOR_PREFIX):] if label.startswith(ANCHOR_PREFIX) else ""
        if label.startswith(LEDGE_PREFIX) or rest.isdigit():
            actor.destroy_actor()
            existing.pop(label, None)
            removed += 1
    changes += removed

    spawner_cls = c.find_class("CityLedgeSpawner", "/Script/Hawkeye.CityLedgeSpawner")
    if spawner_cls is None or asset is None:
        c.log("FAILED", SPAWNER_LABEL, "no ACityLedgeSpawner class or no props asset")
        return changes
    spawner, n = _ensure_located(existing, SPAWNER_LABEL, spawner_cls, unreal.Vector(0.0, 0.0, 0.0), 0.0)
    changes += n
    if spawner is not None:
        props = [("data", asset), ("ledge_class", ledge_cls), ("anchor_class", anchor_cls), ("spawn_in_editor", False),
                 ("fire_escape_cube", c.load_or_none(CUBE)), ("fire_escape_cylinder", c.load_or_none(CYLINDER)),
                 ("fire_escape_material", m.ensure_steel_painted())]
        escape_cls = fire_escape_class()
        if escape_cls is not None:
            props.append(("fire_escape_class", escape_cls))
        changed = []
        for prop, value in props:
            have = spawner.get_editor_property(prop)
            if have != value and not (have is not None and value is not None
                                      and hasattr(have, "get_path_name") and hasattr(value, "get_path_name")
                                      and have.get_path_name() == value.get_path_name()):
                spawner.set_editor_property(prop, value)
                changed.append(prop)
        changes += len(changed)
        changes += _ensure_tags(spawner, ["City", "CityLedgeSpawner"])
        if changed:
            removed_note = ", ".join(changed)
            c.log("updated", SPAWNER_LABEL + " properties", removed_note)
    c.log("updated" if changes else "exists", SPAWNER_LABEL,
          "spawns the props at load; {0} saved ledge/anchor actors removed".format(removed))
    return changes


# --------------------------------------------------------------------------------------
# the world map: footprints, the park, street centre lines and the bounds as 2D data
# --------------------------------------------------------------------------------------
# UHawkeyeMapWidget draws DA_EastVillage_Map (UCityMapData). Footprints and the park are cleaned
# harder than the meshes (edges under 1 m merged, vertices within 50 cm of the line through their
# neighbours dropped) so each is a few points; street pieces are Douglas-Peucker'd to 50 cm. Points
# are whole centimetres. Saved only when the hash of what it was written from changes.

CITY_MAP_NAME = "DA_EastVillage_Map"
MAP_MIN_EDGE = 100.0        # cm
MAP_COLLINEAR = 50.0        # cm
# MAP_LINE_TOLERANCE and the street lines (simplify_line, map_street_lines) are _district's: the reach model
# samples its sidewalk spots along the same lines the map stores and the traversal audit reads.


def _map_ring(ring_latlon, district):
    ring = geo.clean_ring(district.ring_cm(ring_latlon), min_edge=MAP_MIN_EDGE, collinear_tol=MAP_COLLINEAR)
    if len(ring) < 3:
        ring = geo.clean_ring(district.ring_cm(ring_latlon), min_edge=5.0, collinear_tol=2.0)
    return [(round(x), round(y)) for x, y in ring]


def city_map_records(district):
    """(bounds_min, bounds_max, footprints, parks, streets) in world cm. Footprints and parks are
    (osm id, height m, points); streets (name, width m, points)."""
    ground = ground_ring_cm(district)
    xs = [p[0] for p in ground]
    ys = [p[1] for p in ground]
    bounds_min = (round(min(xs)), round(min(ys)))
    bounds_max = (round(max(xs)), round(max(ys)))
    footprints = []
    for rec in district.buildings:
        ring = _map_ring(rec["outer"], district)
        if len(ring) >= 3:
            footprints.append((rec["id"], round(rec["height_m"], 1), ring))
    parks = []
    for park in district.parks:
        ring = _map_ring(park["outer"], district)
        if len(ring) >= 3:
            parks.append((park["id"], 0.0, ring))
    streets = [(rec.get("name") or "", width, line) for rec, width, line in map_street_lines(district)]
    return bounds_min, bounds_max, footprints, parks, streets


def _map_polygon(osm, height, ring):
    rec = unreal.CityMapPolygon()
    rec.set_editor_property("points", [unreal.Vector2D(float(x), float(y)) for x, y in ring])
    rec.set_editor_property("osm_id", osm)
    rec.set_editor_property("height_m", float(height))
    return rec


def _map_street(name, width, line):
    rec = unreal.CityMapStreet()
    rec.set_editor_property("points", [unreal.Vector2D(float(x), float(y)) for x, y in line])
    rec.set_editor_property("width_m", float(width))
    rec.set_editor_property("name", name)
    return rec


def ensure_city_map(district):
    """DA_EastVillage_Map: the world map's data. Returns 1 when it was written, else 0."""
    data_cls = getattr(unreal, "CityMapData", None)
    if data_cls is None:
        c.log("FAILED", CITY_MAP_NAME, "UCityMapData not found; build the module")
        return 0
    bounds_min, bounds_max, footprints, parks, streets = city_map_records(district)
    want_hash = geo.record_hash(GENERATOR_VERSION, "map", list(bounds_min), list(bounds_max),
                                [[f[0], f[1], [list(p) for p in f[2]]] for f in footprints],
                                [[p[0], [list(q) for q in p[2]]] for p in parks],
                                [[s[0], s[1], [list(p) for p in s[2]]] for s in streets])
    full = c.asset_path(CITY_PROPS_PATH, CITY_MAP_NAME)
    points = sum(len(f[2]) for f in footprints)
    summary = "{0} footprints ({1} points, {2:.1f} avg), {3} park(s), {4} street pieces ({5} points), bounds {6:.0f} x {7:.0f} m".format(
        len(footprints), points, points / max(len(footprints), 1), len(parks), len(streets),
        sum(len(s[2]) for s in streets), (bounds_max[0] - bounds_min[0]) / 100.0, (bounds_max[1] - bounds_min[1]) / 100.0)
    asset = c.load_or_none(full)
    if asset is None:
        factory = c.new_factory("DataAssetFactory")
        c.set_props(factory, [("data_asset_class", data_cls)], "DataAssetFactory")
        asset, _created = c.create_asset(CITY_MAP_NAME, CITY_PROPS_PATH, data_cls, factory, quiet=True)
        if asset is None:
            return 0
    if str(asset.get_editor_property("source_hash")) == want_hash \
            and len(asset.get_editor_property("footprints")) == len(footprints) \
            and len(asset.get_editor_property("streets")) == len(streets):
        c.log("exists", full, summary + "; hash unchanged")
        return 0
    asset.set_editor_property("bounds_min", unreal.Vector2D(float(bounds_min[0]), float(bounds_min[1])))
    asset.set_editor_property("bounds_max", unreal.Vector2D(float(bounds_max[0]), float(bounds_max[1])))
    asset.set_editor_property("footprints", [_map_polygon(*f) for f in footprints])
    asset.set_editor_property("parks", [_map_polygon(*p) for p in parks])
    asset.set_editor_property("streets", [_map_street(*s) for s in streets])
    asset.set_editor_property("source_hash", want_hash)
    c.save(asset)
    c.log("updated", full, summary)
    return 1


_GROUND_PREFIXES = (ROAD_PREFIX, SIDEWALK_PREFIX, PARK_PREFIX, GROUND_LABEL)
_GROUND_MESHES = {}


def _mesh_top_under(actor, x, y):
    """World Z where a ray straight down at (x, y) first meets actor's static mesh, or None.

    A commandlet's editor world has no physics scene, so a collision trace always misses; this
    casts against the mesh triangles with Geometry Script, like verify_city's MeshProbe."""
    static_mesh = actor.get_editor_property("static_mesh_component").get_editor_property("static_mesh")
    if static_mesh is None:
        return None
    key = static_mesh.get_path_name()
    if key not in _GROUND_MESHES:
        mesh = unreal.DynamicMesh()
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh_v2(
            static_mesh, mesh, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD(), True)
        result = unreal.GeometryScript_MeshSpatial.build_bvh_for_mesh(mesh)
        bvh = next((r for r in (result if isinstance(result, tuple) else (result,))
                    if isinstance(r, unreal.GeometryScriptDynamicMeshBVH)), None)
        _GROUND_MESHES[key] = (mesh, bvh)
    mesh, bvh = _GROUND_MESHES[key]
    if bvh is None:
        return None
    top = 5000.0
    origin = unreal.Vector(x, y, top) - actor.get_actor_location()
    result = unreal.GeometryScript_MeshSpatial.find_nearest_ray_intersection_with_mesh(
        mesh, bvh, origin, unreal.Vector(0.0, 0.0, -1.0), unreal.GeometryScriptSpatialQueryOptions())
    hit = next((r for r in (result if isinstance(result, tuple) else (result,))
                if isinstance(r, unreal.GeometryScriptRayHitResult)), None)
    if hit is None or not hit.hit:
        return None
    return top - hit.ray_parameter


def ground_z(x, y, fallback, existing):
    """Top of the street, sidewalk, park or ground slab under (x, y); fallback when none is. The
    kerbs make a guessed height wrong by the 13 cm between road and sidewalk."""
    best = None
    for label, actor in existing.items():
        if not label.startswith(_GROUND_PREFIXES):
            continue
        origin, extent = actor.get_actor_bounds(False)
        if abs(x - origin.x) > extent.x or abs(y - origin.y) > extent.y:
            continue
        z = _mesh_top_under(actor, x, y)
        if z is not None and (best is None or z > best):
            best = z
    return best if best is not None else fallback


def test_block_spots(district, existing=None):
    """[(label, tag, origin, yaw, scale)] for the street test blocks and the park walls. With
    ``existing`` (label -> actor) each block stands on the ground traced under its centre."""
    loc, rot = player_start_transform(district)
    yaw = rot.yaw
    across = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))    # out of the park, across the street
    along = (-across[1], across[0])                                          # along the street: local +Y
    def base(x, y, fallback):
        return ground_z(x, y, fallback, existing) if existing is not None else fallback

    spots = []
    for label, tag, offset, width, depth, height in TEST_BLOCKS:
        cx, cy = loc.x + along[0] * offset, loc.y + along[1] * offset
        ox = cx - across[0] * width * 0.5 - along[0] * depth * 0.5
        oy = cy - across[1] * width * 0.5 - along[1] * depth * 0.5
        spots.append((label, tag, (ox, oy, base(cx, cy, ROAD_TOP)), yaw,
                      (width / 100.0, depth / 100.0, height / 100.0)))

    park = district.parks[0] if district.parks else None
    if park is not None:
        ring = district.ring_cm(park["outer"])
        edge = closest_point_on_polyline((loc.x, loc.y), list(ring) + [ring[0]])[1]
        cx, cy = geo.centroid(ring)
        dx, dy = cx - edge[0], cy - edge[1]
        dl = math.hypot(dx, dy) or 1.0
        inward = (dx / dl, dy / dl)                                          # local +Y
        xaxis = (inward[1], -inward[0])
        wall_yaw = math.degrees(math.atan2(xaxis[1], xaxis[0]))
        for i, (distance, width, depth, height) in enumerate(PARK_WALLS):
            px, py = edge[0] + inward[0] * distance, edge[1] + inward[1] * distance
            ox = px - xaxis[0] * width * 0.5 - inward[0] * depth * 0.5
            oy = py - xaxis[1] * width * 0.5 - inward[1] * depth * 0.5
            spots.append((PARK_WALL_PREFIX + str(i), "CityParkWall", (ox, oy, base(px, py, PARK_TOP)), wall_yaw,
                          (width / 100.0, depth / 100.0, height / 100.0)))
    return spots


def ensure_test_blocks(district, existing):
    """City_Test_Vault (90 cm) and City_Test_Mantle (150 cm) on the street by the PlayerStart, and
    City_ParkWall_<n> low walls in the park: visible BP_TraversableBlocks to vault and mantle."""
    cls = traversable_class()
    if cls is None:
        c.log("FAILED", "parkour test blocks", "BP_TraversableBlock not found")
        return 0
    spots = test_block_spots(district, existing)
    changes = 0
    for label, tag, origin, yaw, scale in spots:
        changes += _ensure_block(existing, label, cls, origin, yaw, scale, ["City", "CityParkour", tag], True, False)
    c.log("updated" if changes else "exists", "parkour test blocks",
          "{0} blocks: {1}".format(len(spots), ", ".join(
              "{0} {1:.0f} cm on z {2:.0f}".format(s[0], s[4][2] * 100.0, s[2][2]) for s in spots)))
    return changes


# --------------------------------------------------------------------------------------
# clutter: rooftop water towers, HVAC boxes and chimneys; hydrants, bins and bags, scaffolding and
# parked cars on the street. Placed here from the OSM records, not by a PCG graph: PCG graphs can be
# built from Python (UPCGGraph.AddNodeOfType / AddEdge), but a PCG component only schedules its
# generation for a later world tick, and this headless commandlet never ticks, so the output could
# neither be saved nor checked by verify_city. The rules need the records (heights, roads, anchors,
# landings, patrol points) anyway. Every instance goes into the props asset as FCityClutterGroup and
# City_LedgeSpawner draws each group as one instanced mesh at load.
# --------------------------------------------------------------------------------------

CLUTTER_VERSION = 1                    # bump when a clutter mesh recipe changes
WATER_TOWER_MIN_HEIGHT_M = 15.0
WATER_TOWER_ONE_IN = 4
HVAC_ONE_IN = 2
CHIMNEY_ONE_IN = 2
CLUTTER_ROOF_MIN_HEIGHT_M = 8.0        # sheds and garages get nothing on the roof
ROOF_GRID = 100.0                      # cm; candidate spots on a roof
ROOF_EDGE_CLEAR = 90.0                 # cm from any roof edge to the prop's footprint
CHIMNEY_EDGE = (60.0, 110.0)           # a chimney's centre this far in from an edge
ANCHOR_SAME_ROOF = 200.0               # cm; anchors within this height of a roof count for it
ANCHOR_CLEAR = 100.0                   # cm from an anchor or its landing point to a prop's footprint
PROP_GAP = 80.0                        # cm between two props on one roof
HYDRANT_SPACING = 6000.0               # cm along each road, alternating sides
HYDRANT_KERB_INSET = 45.0
CROSSING_CLEAR = 500.0                 # cm from another road's carriageway: no hydrant, car or bin there
LAMP_CLEAR = 150.0                     # cm from a lamp pole
PATROL_CLEAR = 150.0                   # cm from the thug patrol line, its points and the thugs
TEST_BLOCK_CLEAR = 300.0               # cm from the parkour test blocks and the PlayerStart
BIN_ONE_IN = 3
BIN_FACADE_OUT = 55.0                  # cm out from the facade to a bin's centre
BIN_CORNER_IN = 90.0                   # cm along the facade from the corner
SCAFFOLD_BUILDINGS = 3
SCAFFOLD_BAY = (200.0, 110.0, 200.0)   # width along the facade, depth, height per level
SCAFFOLD_MIN_EDGE = 500.0
SCAFFOLD_MAX_HEIGHT = 1200.0           # cm; the work is on the lower floors
CAR_SIZE = (450.0, 180.0, 140.0)
CAR_SPACING = 700.0
CAR_KERB_GAP = 20.0
CAR_GAP_FRACTION = 0.3                 # of car spaces left empty
HYDRANT_NO_PARKING = 460.0             # cm either side of a hydrant (15 ft)

# kind: (mesh name, material key, collision, casts shadow, footprint radius cm)
CLUTTER_KINDS = {
    "WaterTower": ("SM_City_WaterTower", "WaterTower", True, True, 170.0),
    "HVAC": ("SM_City_HVAC", "HVAC", True, True, 120.0),
    "Chimney": ("SM_City_Chimney", "Chimney", True, True, 60.0),
    "Hydrant": ("SM_City_Hydrant", "Hydrant", True, False, 25.0),
    "Bin": ("SM_City_Bin", "Bin", True, False, 32.0),
    "TrashBag": ("SM_City_TrashBag", "TrashBag", True, False, 36.0),
    "Scaffold": ("SM_City_ScaffoldBay", "Scaffold", False, True, 0.0),
    "ParkedCar_Black": ("SM_City_ParkedCar", "CarBlack", True, True, 245.0),
    "ParkedCar_Purple": ("SM_City_ParkedCar", "CarPurple", True, True, 245.0),
    "ParkedCar_Grey": ("SM_City_ParkedCar", "CarGrey", True, True, 245.0),
    "ParkedCar_Navy": ("SM_City_ParkedCar", "CarNavy", True, True, 245.0),
}
# cm from the base to the top, for the fire-escape landing check.
CLUTTER_HEIGHTS = {"WaterTower": 710.0, "HVAC": 130.0, "Chimney": 170.0, "Scaffold": SCAFFOLD_BAY[2]}
LANDING_CLEAR = 30.0                   # cm between a prop and a fire-escape landing it shares height with
CAR_KINDS = ("ParkedCar_Black", "ParkedCar_Purple", "ParkedCar_Grey", "ParkedCar_Navy")

# MI_Prop_<key>: (colour, roughness), from the palette: purple, cream, grey, black.
CLUTTER_MATERIALS = {
    "WaterTower": ((0.12, 0.08, 0.06), 0.9),
    "HVAC": ((0.30, 0.30, 0.31), 0.5),
    "Chimney": ((0.20, 0.07, 0.05), 0.9),
    "Hydrant": ((0.35, 0.05, 0.035), 0.45),
    "Bin": ((0.03, 0.06, 0.045), 0.6),
    "TrashBag": ((0.012, 0.012, 0.014), 0.25),
    "Scaffold": ((0.22, 0.22, 0.23), 0.5),
    "CarBlack": ((0.015, 0.015, 0.017), 0.3),
    "CarPurple": ((0.07, 0.025, 0.10), 0.3),
    "CarGrey": ((0.10, 0.10, 0.11), 0.35),
    "CarNavy": ((0.02, 0.03, 0.07), 0.3),
    "Ground": ((0.10, 0.10, 0.10), 0.9),
}


def _bx(mesh, cx, cy, z0, sx, sy, sz, yaw=0.0):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(0.0, 0.0, yaw))
    GS_PRIM.append_box(mesh, unreal.GeometryScriptPrimitiveOptions(), xf, sx, sy, sz, 0, 0, 0,
                       unreal.GeometryScriptPrimitiveOriginMode.BASE)


def _cyl(mesh, cx, cy, z0, radius, height, steps=12, roll=0.0):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(roll, 0.0, 0.0))
    GS_PRIM.append_cylinder(mesh, unreal.GeometryScriptPrimitiveOptions(), xf, radius, height, steps, 0, True,
                            unreal.GeometryScriptPrimitiveOriginMode.BASE)


def _build_water_tower():
    mesh = new_mesh()
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            _bx(mesh, sx * 95.0, sy * 95.0, 0.0, 18.0, 18.0, 260.0)
    _bx(mesh, 0.0, 0.0, 260.0, 330.0, 330.0, 12.0)
    _cyl(mesh, 0.0, 0.0, 272.0, 150.0, 320.0, 16)
    GS_PRIM.append_cone(mesh, unreal.GeometryScriptPrimitiveOptions(),
                        unreal.Transform(location=unreal.Vector(0.0, 0.0, 592.0)), 165.0, 8.0, 110.0, 16, 0, True,
                        unreal.GeometryScriptPrimitiveOriginMode.BASE)
    return mesh


def _build_hvac():
    mesh = new_mesh()
    _bx(mesh, 0.0, 0.0, 0.0, 200.0, 130.0, 110.0)
    _cyl(mesh, 40.0, 0.0, 110.0, 45.0, 18.0, 16)
    return mesh


def _build_chimney():
    mesh = new_mesh()
    _bx(mesh, 0.0, 0.0, 0.0, 70.0, 70.0, 160.0)
    _bx(mesh, 0.0, 0.0, 160.0, 86.0, 86.0, 10.0)
    return mesh


def _build_hydrant():
    mesh = new_mesh()
    _cyl(mesh, 0.0, 0.0, 0.0, 18.0, 6.0, 12)
    _cyl(mesh, 0.0, 0.0, 6.0, 13.0, 52.0, 12)
    _cyl(mesh, 0.0, 0.0, 58.0, 16.0, 8.0, 12)
    _cyl(mesh, 0.0, 0.0, 66.0, 8.0, 8.0, 8)
    _bx(mesh, 0.0, 0.0, 32.0, 44.0, 10.0, 10.0)
    return mesh


def _build_bin():
    mesh = new_mesh()
    _cyl(mesh, 0.0, 0.0, 0.0, 30.0, 85.0, 12)
    return mesh


def _build_trash_bag():
    mesh = new_mesh()
    GS_PRIM.append_sphere_lat_long(mesh, unreal.GeometryScriptPrimitiveOptions(),
                                   unreal.Transform(location=unreal.Vector(0.0, 0.0, 26.0), scale=unreal.Vector(1.0, 1.0, 0.75)),
                                   35.0, 8, 10, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    return mesh


def _build_scaffold_bay():
    """One bay: local X along the facade (0..W), Y out of it (0..D), Z up (0..H); deck on top."""
    w, d, h = SCAFFOLD_BAY
    mesh = new_mesh()
    for px in (2.5, w - 2.5):
        for py in (2.5, d - 2.5):
            _bx(mesh, px, py, 0.0, 5.0, 5.0, h)
    for py in (2.5, d - 2.5):
        _bx(mesh, w * 0.5, py, h * 0.5, w, 4.0, 4.0)
    _bx(mesh, w * 0.5, d - 2.5, h - 100.0, w, 4.0, 4.0)
    _bx(mesh, w * 0.5, d * 0.5, h - 4.0, w, d, 4.0)
    return mesh


def _build_parked_car():
    """A dark block car, 4.5 x 1.8 x 1.4 m: body, cabin set back, four wheels. Origin at the ground,
    centred, +X forward."""
    L, W, H = CAR_SIZE
    mesh = new_mesh()
    _bx(mesh, 0.0, 0.0, 28.0, L, W, 67.0)
    _bx(mesh, -20.0, 0.0, 95.0, 250.0, W - 16.0, H - 95.0)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            xf = unreal.Transform(location=unreal.Vector(sx * 140.0, sy * (W * 0.5 - 11.0), 33.0),
                                  rotation=unreal.Rotator(90.0, 0.0, 0.0))
            GS_PRIM.append_cylinder(mesh, unreal.GeometryScriptPrimitiveOptions(), xf, 33.0, 22.0, 12, 0, True,
                                    unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    return mesh


CLUTTER_MESHES = {
    "SM_City_WaterTower": _build_water_tower,
    "SM_City_HVAC": _build_hvac,
    "SM_City_Chimney": _build_chimney,
    "SM_City_Hydrant": _build_hydrant,
    "SM_City_Bin": _build_bin,
    "SM_City_TrashBag": _build_trash_bag,
    "SM_City_ScaffoldBay": _build_scaffold_bay,
    "SM_City_ParkedCar": _build_parked_car,
}


def ensure_clutter_meshes():
    """{mesh name: StaticMesh}; each rebuilt only when CLUTTER_VERSION changes."""
    out = {}
    c.ensure_directory(MESH_DIR)
    for name, build in sorted(CLUTTER_MESHES.items()):
        spec = geo.record_hash(GENERATOR_VERSION, CLUTTER_VERSION, "clutter", name, list(CAR_SIZE), list(SCAFFOLD_BAY))
        asset, current = stored_hash(name)
        if asset is not None and current == spec:
            out[name] = asset
            continue
        try:
            static_mesh, action = write_static_mesh(build(), name, spec)
        except Exception as exc:  # noqa: BLE001
            c.log_error("clutter mesh " + name, exc)
            static_mesh, action = None, "failed"
        if static_mesh is not None:
            out[name] = static_mesh
            c.log(action, c.asset_path(MESH_DIR, name))
    c.log("exists", "clutter meshes", "{0} of {1} present".format(len(out), len(CLUTTER_MESHES)))
    return out


def ensure_clutter_materials():
    return {key: m.ensure_prop_instance(key, rgb, rough) for key, (rgb, rough) in sorted(CLUTTER_MATERIALS.items())}


def clutter_keepouts(district):
    """What street clutter keeps clear of: [(kind, (x, y), radius)] points and patrol segments."""
    points = []
    for x, y, _yaw, _shadows in lamp_spots(district):
        points.append(("lamp", (x, y), LAMP_CLEAR))
    thugs, patrol_points, _roof = thug_placements(district)
    for t in thugs:
        points.append(("thug", (t[1], t[2]), PATROL_CLEAR))
    for p in patrol_points:
        points.append(("patrol", (p[1], p[2]), PATROL_CLEAR))
    loc, _rot = player_start_transform(district)
    points.append(("start", (loc.x, loc.y), TEST_BLOCK_CLEAR))
    door = safehouse_keepout(district)
    if door is not None:
        points.append(("safehouse", door, SAFEHOUSE_KEEPOUT))
    door2 = safehouse2_keepout(district)
    if door2 is not None:
        points.append(("safehouse", door2, SAFEHOUSE_KEEPOUT))
    door3 = interior_keepout(district)
    if door3 is not None:
        points.append(("interior", door3, INTERIOR_KEEPOUT))
    for label, _tag, origin, yaw, scale in test_block_spots(district):
        ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        cx = origin[0] + ux * scale[0] * 50.0 - uy * scale[1] * 50.0
        cy = origin[1] + uy * scale[0] * 50.0 + ux * scale[1] * 50.0
        points.append(("test_block", (cx, cy), TEST_BLOCK_CLEAR + max(scale[0], scale[1]) * 50.0))
    segments = []
    for a, b in zip(patrol_points[0::2], patrol_points[1::2]):
        segments.append(((a[1], a[2]), (b[1], b[2])))
    return points, segments


def clutter_plan(district):
    """{kind: [(x, y, z, yaw, scale)]} in a stable order, plus notes for the log. The rules:

    * roofs of buildings over CLUTTER_ROOF_MIN_HEIGHT_M, never the three objective roofs (their
      beacons, volumes and the RoofPair stay clear): a water tower on 1 in WATER_TOWER_ONE_IN roofs
      over WATER_TOWER_MIN_HEIGHT_M, an HVAC box on 1 in HVAC_ONE_IN, one or two chimneys near an
      edge on 1 in CHIMNEY_ONE_IN; each footprint ROOF_EDGE_CLEAR inside the roof, ANCHOR_CLEAR from
      every grapple anchor and landing point, PROP_GAP from the others
    * a hydrant every HYDRANT_SPACING along each road at the kerb, sides alternating
    * a bin and two or three bags at a street corner of 1 in BIN_ONE_IN buildings, against the facade
    * scaffolding up to SCAFFOLD_MAX_HEIGHT over the street facade of SCAFFOLD_BUILDINGS buildings with
      no fire escape
    * parked cars every CAR_SPACING along the side of each road away from the park, none near a
      crossing or within HYDRANT_NO_PARKING of a hydrant, CAR_GAP_FRACTION left empty
    Street clutter keeps clear of lamps, the thug patrol, the PlayerStart and the parkour test blocks.
    """
    plan = {kind: [] for kind in CLUTTER_KINDS}
    # The archers' roofs and the roofs their lines cross stay bare, so nothing stands in the duel.
    objective_ids = {rec["id"] for rec in objective_roofs(district).values()} | archer_quiet_roofs(district)
    # The interior's roof door comes out on its building's roof: nothing stands on it.
    objective_ids |= interior_building_ids(district)
    anchors = anchor_spots(district)
    anchor_pts = []   # (x, y, z): every anchor and its landing point
    for x, y, z, yaw, forward, _drop, _osm in anchors:
        lx, ly = x + math.cos(math.radians(yaw)) * forward, y + math.sin(math.radians(yaw)) * forward
        anchor_pts.extend([(x, y, z), (lx, ly, z)])
    escapes = fire_escape_spots(district)
    escape_ids = {f[0] for f in escapes}
    landing_quads = [_landing_corners(f[2][0], f[2][1], f[3], FIRE_ESCAPE_GAP_TO_OTHER) for f in escapes]
    landings = [(quad, f[2][2] - FIRE_ESCAPE_SLAB[2], f[2][2] + FIRE_ESCAPE_RAIL, geo.bounds(quad))
                for quad, f in zip(landing_quads, escapes)]

    def landings_near(box, pad=400.0):
        x0, y0, x1, y1 = box
        return [l for l in landings if l[3][0] < x1 + pad and l[3][2] > x0 - pad and l[3][1] < y1 + pad and l[3][3] > y0 - pad]

    def hits_landing(px, py, radius, z0, z1, near):
        return any(lo < z1 and hi > z0 and _point_quad_distance((px, py), quad) < radius + LANDING_CLEAR
                   for quad, lo, hi, _box in near)

    buildings = []
    for rec in sorted(district.buildings, key=lambda r: r["id"]):
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) >= 3:
            buildings.append((rec, ring, geo.bounds(ring)))

    # --- roofs ---
    for rec, ring, (x0, y0, x1, y1) in buildings:
        h = rec["height_m"]
        if h < CLUTTER_ROOF_MIN_HEIGHT_M or rec["id"] in objective_ids:
            continue
        roof_z = h * 100.0
        # This roof's anchors and any neighbour's at about the same height (theirs sit on a shared
        # parapet line, and Kate lands on this roof from them).
        avoid = [(ax, ay) for ax, ay, az in anchor_pts if abs(az - roof_z) < ANCHOR_SAME_ROOF
                 and x0 - 400.0 <= ax <= x1 + 400.0 and y0 - 400.0 <= ay <= y1 + 400.0]
        near = landings_near((x0, y0, x1, y1))
        placed = []   # (x, y, radius)
        grid = []
        gx = x0 + ROOF_GRID * 0.5
        while gx < x1:
            gy = y0 + ROOF_GRID * 0.5
            while gy < y1:
                if geo.point_in_polygon((gx, gy), ring):
                    grid.append((gx, gy, _edge_distance((gx, gy), ring)))
                gy += ROOF_GRID
            gx += ROOF_GRID

        def free(px, py, radius, edge):
            if edge < radius + ROOF_EDGE_CLEAR:
                return False
            if any(math.hypot(px - ax, py - ay) < radius + ANCHOR_CLEAR for ax, ay in avoid):
                return False
            return all(math.hypot(px - qx, py - qy) >= radius + qr + PROP_GAP for qx, qy, qr in placed)

        def pick(kind, salt, want_edge=None):
            radius = CLUTTER_KINDS[kind][4]
            options = []
            for px, py, edge in grid:
                if hits_landing(px, py, radius, roof_z, roof_z + CLUTTER_HEIGHTS[kind], near):
                    continue
                if want_edge is not None:
                    if not (want_edge[0] <= edge <= want_edge[1]):
                        continue
                    if any(math.hypot(px - ax, py - ay) < radius + ANCHOR_CLEAR for ax, ay in avoid):
                        continue
                    if not all(math.hypot(px - qx, py - qy) >= radius + qr + PROP_GAP for qx, qy, qr in placed):
                        continue
                elif not free(px, py, radius, edge):
                    continue
                options.append((px, py))
            if not options:
                return None
            px, py = options[int(stable_hash(rec["id"], kind, salt) * len(options)) % len(options)]
            placed.append((px, py, radius))
            return px, py

        yaw0 = oriented_rect(ring)[4]
        if h >= WATER_TOWER_MIN_HEIGHT_M and stable_hash(rec["id"], "tower") < 1.0 / WATER_TOWER_ONE_IN:
            spot = pick("WaterTower", 0)
            if spot:
                s = 0.8 + 0.25 * stable_hash(rec["id"], "tower-scale")
                plan["WaterTower"].append((spot[0], spot[1], roof_z, yaw0 + 45.0 * stable_hash(rec["id"], "ty"), s))
        if stable_hash(rec["id"], "hvac") < 1.0 / HVAC_ONE_IN:
            spot = pick("HVAC", 0)
            if spot:
                plan["HVAC"].append((spot[0], spot[1], roof_z, yaw0, 1.0))
        if stable_hash(rec["id"], "chimney") < 1.0 / CHIMNEY_ONE_IN:
            for n in range(1 + int(stable_hash(rec["id"], "chimneys") * 2.0)):
                spot = pick("Chimney", n, CHIMNEY_EDGE)
                if spot:
                    plan["Chimney"].append((spot[0], spot[1], roof_z, yaw0, 1.0))

    # --- street ---
    roads = sorted(road_paths(district), key=lambda rp: rp[0]["id"])
    def road_key(rec):
        return rec.get("name") or rec["id"]

    carriageway = [(p, q, rec["width_m"] * 50.0, road_key(rec)) for rec, paths in roads
                   for path in paths for p, q in zip(path, path[1:])]
    ground = ground_ring_cm(district)
    parks = [district.ring_cm(p["outer"]) for p in district.parks]
    points, segments = clutter_keepouts(district)

    def in_carriageway(pt, pad=30.0, skip_road=None):
        return any(_near_segment(pt, p, q) < half + pad for p, q, half, rid in carriageway if rid != skip_road)

    def near_crossing(pt, own_road):
        return in_carriageway(pt, CROSSING_CLEAR, own_road)

    def in_building(pt, clearance):
        for rec, ring, (x0, y0, x1, y1) in buildings:
            if x0 - clearance <= pt[0] <= x1 + clearance and y0 - clearance <= pt[1] <= y1 + clearance:
                if geo.point_in_polygon(pt, ring) or ring_distance(pt, ring) < clearance:
                    return True
        return False

    def kept_out(pt, radius):
        if any(math.hypot(pt[0] - q[0], pt[1] - q[1]) < r + radius for _k, q, r in points):
            return True
        return any(_near_segment(pt, a, b) < PATROL_CLEAR + radius for a, b in segments)

    def street_ok(pt, radius, road_id=None, crossing=True):
        if not geo.point_in_polygon(pt, ground) or in_building(pt, radius + 5.0) or kept_out(pt, radius):
            return False
        if any(geo.point_in_polygon(pt, park) for park in parks):
            return False
        return not (crossing and road_id is not None and near_crossing(pt, road_id))

    # Hydrants at the kerb.
    hydrants = []
    for rec, paths in roads:
        offset = rec["width_m"] * 50.0 + HYDRANT_KERB_INSET
        side = 1.0
        for path in paths:
            s_next = HYDRANT_SPACING * 0.5
            walked = 0.0
            for (ax, ay), (bx, by) in zip(path, path[1:]):
                L = math.hypot(bx - ax, by - ay)
                if L < 1.0:
                    continue
                ux, uy = (bx - ax) / L, (by - ay) / L
                while s_next <= walked + L:
                    t = s_next - walked
                    px, py = ax + ux * t, ay + uy * t
                    for try_side in (side, -side):
                        pt = (px - uy * offset * try_side, py + ux * offset * try_side)
                        if in_carriageway(pt, 10.0) or not street_ok(pt, CLUTTER_KINDS["Hydrant"][4], road_key(rec)):
                            continue
                        if any(math.hypot(pt[0] - q[0], pt[1] - q[1]) < HYDRANT_SPACING * 0.3 for q in hydrants):
                            continue
                        hydrants.append(pt)
                        plan["Hydrant"].append((pt[0], pt[1], SIDEWALK_TOP, math.degrees(math.atan2(uy, ux)), 1.0))
                        break
                    side = -side
                    s_next += HYDRANT_SPACING
                walked += L
    for x, y, *_rest in plan["Hydrant"]:
        points.append(("hydrant", (x, y), 60.0))

    # A bin and bags at one street corner of some buildings.
    segments_all = [(p, q) for p, q, _h, _r in carriageway]
    for rec, ring, _box in buildings:
        if rec["height_m"] < CLUTTER_ROOF_MIN_HEIGHT_M or stable_hash(rec["id"], "bin") >= 1.0 / BIN_ONE_IN:
            continue
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        n = len(ring)
        best = None
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            L = math.hypot(bx - ax, by - ay)
            if L < 2.0 * BIN_CORNER_IN + 100.0:
                continue
            ox, oy = (by - ay) / L * inward, -(bx - ax) / L * inward
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            dist = min(_near_segment((mx, my), p, q) for p, q in segments_all)
            if best is None or dist < best[0]:
                best = (dist, (ax, ay), (bx, by), (ox, oy), L)
        if best is None:
            continue
        _d, a, b, (ox, oy), L = best
        ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
        end = 1.0 if stable_hash(rec["id"], "bin-end") < 0.5 else -1.0
        corner = b if end > 0 else a
        along = -end
        cx = corner[0] + ux * along * BIN_CORNER_IN + ox * BIN_FACADE_OUT
        cy = corner[1] + uy * along * BIN_CORNER_IN + oy * BIN_FACADE_OUT
        if in_carriageway((cx, cy), 150.0) or not street_ok((cx, cy), CLUTTER_KINDS["Bin"][4]):
            continue
        yaw = math.degrees(math.atan2(uy, ux))
        plan["Bin"].append((cx, cy, SIDEWALK_TOP, yaw, 1.0))
        bags = 2 + int(stable_hash(rec["id"], "bags") * 2.0)
        for k in range(bags):
            t = (k + 1) * 60.0
            bx_, by_ = cx + ux * along * t + ox * (5.0 * (k % 2)), cy + uy * along * t + oy * (5.0 * (k % 2))
            if street_ok((bx_, by_), CLUTTER_KINDS["TrashBag"][4]) and not in_carriageway((bx_, by_), 150.0):
                plan["TrashBag"].append((bx_, by_, SIDEWALK_TOP, yaw + 70.0 * k, 0.85 + 0.3 * stable_hash(rec["id"], k)))

    # Scaffolding on a few tenements without fire escapes.
    candidates = []
    for rec, ring, _box in buildings:
        h = rec["height_m"]
        if h < CLUTTER_ROOF_MIN_HEIGHT_M or rec["id"] in escape_ids or rec["id"] in objective_ids:
            continue
        inward = 1.0 if geo.is_ccw(ring) else -1.0
        n = len(ring)
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            L = math.hypot(bx - ax, by - ay)
            if L < SCAFFOLD_MIN_EDGE:
                continue
            ox, oy = (by - ay) / L * inward, -(bx - ax) / L * inward
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            probe = (mx + ox * (SCAFFOLD_BAY[1] + 50.0), my + oy * (SCAFFOLD_BAY[1] + 50.0))
            if in_carriageway(probe, 50.0) or in_building(probe, 20.0):
                continue
            if min(_near_segment((mx, my), p, q) for p, q in segments_all) > 1500.0:
                continue
            candidates.append((stable_hash(rec["id"], "scaffold"), rec, (ax, ay), (bx, by), (ox, oy), L))
            break
    candidates.sort(key=lambda cand: cand[0])
    chosen = 0
    for _h, rec, a, b, (ox, oy), L in candidates:
        if chosen >= SCAFFOLD_BUILDINGS:
            break
        ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
        bays = int((L - 60.0) // SCAFFOLD_BAY[0])
        levels = int(min(rec["height_m"] * 100.0 - 60.0, SCAFFOLD_MAX_HEIGHT) // SCAFFOLD_BAY[2])
        start = (L - bays * SCAFFOLD_BAY[0]) * 0.5
        yaw = math.degrees(math.atan2(uy, ux))
        # The bay's local +Y must point out of the facade: flip the run if yaw turns it inward.
        if (-math.sin(math.radians(yaw))) * ox + math.cos(math.radians(yaw)) * oy < 0.0:
            a, b, ux, uy = b, a, -ux, -uy
            yaw = math.degrees(math.atan2(uy, ux))
        gap = 8.0
        quad = [(a[0] + ox * gap, a[1] + oy * gap), (b[0] + ox * gap, b[1] + oy * gap),
                (b[0] + ox * (gap + SCAFFOLD_BAY[1]), b[1] + oy * (gap + SCAFFOLD_BAY[1])),
                (a[0] + ox * (gap + SCAFFOLD_BAY[1]), a[1] + oy * (gap + SCAFFOLD_BAY[1]))]
        if any(_rects_overlap(quad, other) for other in landing_quads):
            continue
        steps = int(L // 100.0) + 1
        edge_pts = [(quad[0][0] + (quad[1][0] - quad[0][0]) * k / steps + (quad[3][0] - quad[0][0]) * f,
                     quad[0][1] + (quad[1][1] - quad[0][1]) * k / steps + (quad[3][1] - quad[0][1]) * f)
                    for k in range(steps + 1) for f in (0.0, 1.0)]
        if any(kept_out(pt, 60.0) for pt in edge_pts):
            continue
        chosen += 1
        for i in range(bays):
            for j in range(levels):
                bx_ = a[0] + ux * (start + i * SCAFFOLD_BAY[0]) + ox * gap
                by_ = a[1] + uy * (start + i * SCAFFOLD_BAY[0]) + oy * gap
                plan["Scaffold"].append((bx_, by_, SIDEWALK_TOP + j * SCAFFOLD_BAY[2], yaw, 1.0))

    # Parked cars along the side of each road away from the park.
    for rec, paths in roads:
        half = rec["width_m"] * 50.0
        offset = half - CAR_SIZE[1] * 0.5 - CAR_KERB_GAP
        for pi, path in enumerate(paths):
            if len(path) < 2:
                continue
            (ax, ay), (bx, by) = path[0], path[-1]
            L0 = math.hypot(bx - ax, by - ay) or 1.0
            nx0, ny0 = -(by - ay) / L0, (bx - ax) / L0
            mid = ((ax + bx) * 0.5, (ay + by) * 0.5)
            side = 1.0 if stable_hash(rec["id"], "car-side") < 0.5 else -1.0
            if parks:
                d_plus = min(ring_distance((mid[0] + nx0 * offset, mid[1] + ny0 * offset), p) for p in parks)
                d_minus = min(ring_distance((mid[0] - nx0 * offset, mid[1] - ny0 * offset), p) for p in parks)
                if min(d_plus, d_minus) < 3000.0:
                    side = 1.0 if d_plus > d_minus else -1.0
            walked = 0.0
            s_next = CAR_SPACING * 0.5
            for (sx, sy), (ex, ey) in zip(path, path[1:]):
                L = math.hypot(ex - sx, ey - sy)
                if L < 1.0:
                    continue
                ux, uy = (ex - sx) / L, (ey - sy) / L
                nx, ny = -uy, ux
                while s_next <= walked + L:
                    t = s_next - walked
                    px, py = sx + ux * t + nx * offset * side, sy + uy * t + ny * offset * side
                    s_next += CAR_SPACING
                    slot = "{0}:{1}:{2:.0f}".format(rec["id"], pi, s_next)
                    if stable_hash(slot, "gap") < CAR_GAP_FRACTION:
                        continue
                    nose = (px + ux * CAR_SIZE[0] * 0.5, py + uy * CAR_SIZE[0] * 0.5)
                    tail = (px - ux * CAR_SIZE[0] * 0.5, py - uy * CAR_SIZE[0] * 0.5)
                    if not all(geo.point_in_polygon(q, ground) for q in (nose, tail)):
                        continue
                    if any(in_carriageway(q, CROSSING_CLEAR * 0.6, road_key(rec)) for q in (nose, tail, (px, py))):
                        continue
                    if any(math.hypot(px - hx, py - hy) < HYDRANT_NO_PARKING for hx, hy in hydrants):
                        continue
                    if in_building((px, py), CAR_SIZE[1]) or kept_out((px, py), CLUTTER_KINDS[CAR_KINDS[0]][4]):
                        continue
                    kind = CAR_KINDS[int(stable_hash(slot, "colour") * len(CAR_KINDS)) % len(CAR_KINDS)]
                    yaw = math.degrees(math.atan2(uy, ux)) + (180.0 if stable_hash(slot, "dir") < 0.15 else 0.0)
                    plan[kind].append((px, py, ROAD_TOP, yaw, 1.0))
                walked += L
    return plan


def clutter_groups(district, meshes, materials):
    """[(kind, mesh, material, collision, shadow, [Transform])] for the props asset, and the plan."""
    plan = clutter_plan(district)
    groups = []
    for kind in sorted(plan):
        mesh_name, mat_key, collision, shadow, _radius = CLUTTER_KINDS[kind]
        xforms = [unreal.Transform(unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(s, s, s))
                  for x, y, z, yaw, s in plan[kind]]
        if xforms:
            groups.append((kind, meshes.get(mesh_name), materials.get(mat_key), collision, shadow, xforms))
    return groups, plan


def dressing_groups():
    """(clutter groups, plan document) of the hero block's December dressing (_dressing_build.py)."""
    import _dressing_build as dress  # noqa: PLC0415 - it imports this module back for its actor helpers
    return dress.dressing_groups()


def ensure_dressing_actors(existing):
    """The dressing's few real lights and its reference cameras (_dressing_build.py). Returns changes."""
    import _dressing_build as dress  # noqa: PLC0415
    doc = dress.plan_doc()
    return dress.ensure_lights(existing, doc) + dress.ensure_cameras(existing, doc)


def _clutter_record(group):
    kind, mesh, material, collision, shadow, xforms = group
    rec = unreal.CityClutterGroup()
    rec.set_editor_property("kind", kind)
    rec.set_editor_property("mesh", mesh)
    rec.set_editor_property("material", material)
    rec.set_editor_property("collision", collision)
    rec.set_editor_property("cast_shadow", shadow)
    rec.set_editor_property("instances", xforms)
    return rec


# --------------------------------------------------------------------------------------
# level
# --------------------------------------------------------------------------------------


# --------------------------------------------------------------------------------------
# the safehouse: a lit purple door in a storefront across from the park
# --------------------------------------------------------------------------------------

# ASafehouse (Source/Hawkeye/World/Safehouse.h) on the ground floor of a building that fronts
# Tompkins Square Park: SAFEHOUSE_PARK_REACH of the park, at least SAFEHOUSE_MIN_HEIGHT_M tall, not
# an objective roof, and more than SAFEHOUSE_START_CLEAR from the PlayerStart so the opening
# street shots keep their facades. Of those, the one nearest the start (a short walk). The door
# goes on its facade at the point nearest the park, facing out, standing on the sidewalk.
SAFEHOUSE_LABEL = "City_Safehouse"
SAFEHOUSE_ID = "ch01_east_7th"
SAFEHOUSE_NAME = "[Safehouse 1]"   # placeholder until the story names it; the address is the subtitle
SAFEHOUSE_PARK_REACH = 3000.0       # cm between the building and the park's edge
SAFEHOUSE_MIN_HEIGHT_M = 6.0
SAFEHOUSE_START_CLEAR = 2500.0      # cm
SAFEHOUSE_EDGE_MIN = 400.0          # cm; the facade the door is in must be at least this long
SAFEHOUSE_EDGE_INSET = 150.0        # cm from either end of that facade
SAFEHOUSE_KEEPOUT = 250.0           # cm of pavement in front of the door that clutter leaves clear
SAFEHOUSE_CLASS = "/Script/Hawkeye.Safehouse"


def _safehouse_door(ring, target):
    """(x, y, yaw, edge length) of the door on ring's edge nearest target, or None."""
    best = None
    n = len(ring)
    for i in range(n):
        a, b = ring[i], ring[(i + 1) % n]
        hit = closest_point_on_polyline(target, [a, b])
        if hit is not None and (best is None or hit[0] < best[0]):
            best = (hit[0], i)
    if best is None:
        return None
    a, b = ring[best[1]], ring[(best[1] + 1) % n]
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    if length < SAFEHOUSE_EDGE_MIN:
        return None
    ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    t = (target[0] - a[0]) * ux + (target[1] - a[1]) * uy
    t = max(SAFEHOUSE_EDGE_INSET, min(length - SAFEHOUSE_EDGE_INSET, t))
    x, y = a[0] + ux * t, a[1] + uy * t
    nx, ny = uy, -ux
    if geo.point_in_polygon((x + nx * 20.0, y + ny * 20.0), ring):
        nx, ny = -nx, -ny
    return x, y, math.degrees(math.atan2(ny, nx)), length


def safehouse_spot(district):
    """{'rec', 'x', 'y', 'yaw', 'address'} for the safehouse door, or None."""
    if not district.parks:
        return None
    park = district.ring_cm(district.parks[0]["outer"])
    park_centre = geo.centroid(park)
    start, _rot = player_start_transform(district)
    start = (start.x, start.y)
    taken = {rec["id"] for rec in objective_roofs(district).values()}
    candidates = []
    for rec in district.buildings:
        if rec["id"] in taken or rec["height_m"] < SAFEHOUSE_MIN_HEIGHT_M:
            continue
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3 or ring_distance(start, ring) <= SAFEHOUSE_START_CLEAR:
            continue
        reach = min(ring_distance(p, ring) for p in park)
        if reach > SAFEHOUSE_PARK_REACH:
            continue
        door = _safehouse_door(ring, park_centre)
        if door is None:
            continue
        candidates.append((ring_distance(start, ring), rec["id"], rec, door))
    if not candidates:
        return None
    _d, _id, rec, door = min(candidates, key=lambda c_: (c_[0], c_[1]))
    tags = rec.get("tags", {})
    address = "{0} {1}".format(tags.get("addr:housenumber") or "", tags.get("addr:street") or "").strip()
    return {"rec": rec, "x": door[0], "y": door[1], "yaw": door[2], "address": address}


def safehouse_keepout(district):
    """(x, y) of the pavement in front of the safehouse door, or None."""
    spot = safehouse_spot(district)
    if spot is None:
        return None
    yaw = math.radians(spot["yaw"])
    return spot["x"] + math.cos(yaw) * 150.0, spot["y"] + math.sin(yaw) * 150.0


def _ensure_part_material(actor, prop, material, label):
    comp = actor.get_editor_property(prop)
    if comp is None or material is None:
        return 0
    overrides = comp.get_editor_property("override_materials")
    if len(overrides) >= 1 and overrides[0] == material:
        return 0
    comp.set_material(0, material)
    c.log("updated", label, "{0} material = {1}".format(prop, material.get_name()))
    return 1


def _ensure_safehouse_names(actor, name, address):
    changes = 0
    for prop, value in (("display_name", name), ("address", address)):
        if str(actor.get_editor_property(prop)) != value:
            actor.set_editor_property(prop, unreal.Text(value))
            changes += 1
    return changes


def ensure_safehouse(district, existing):
    """City_Safehouse (ASafehouse) on its storefront. Idempotent by label."""
    cls = c.find_class("Safehouse", SAFEHOUSE_CLASS)
    if cls is None:
        c.log("skipped", SAFEHOUSE_LABEL, "ASafehouse not exposed; build the module")
        return 0
    spot = safehouse_spot(district)
    if spot is None:
        c.log("FAILED", SAFEHOUSE_LABEL, "no building fronting the park qualifies")
        return 0
    x, y, yaw = spot["x"], spot["y"], spot["yaw"]
    ground = ground_z(x + math.cos(math.radians(yaw)) * 60.0, y + math.sin(math.radians(yaw)) * 60.0,
                      SIDEWALK_TOP, existing)
    actor, changes = _ensure_located(existing, SAFEHOUSE_LABEL, cls, unreal.Vector(x, y, ground), yaw)
    if actor is None:
        return changes
    changes += _ensure_tags(actor, ["City", "CitySafehouse", "osm:" + spot["rec"]["id"]])
    if str(actor.get_editor_property("safehouse_id")) != SAFEHOUSE_ID:
        actor.set_editor_property("safehouse_id", unreal.Name(SAFEHOUSE_ID))
        changes += 1
    changes += _ensure_safehouse_names(actor, SAFEHOUSE_NAME, spot["address"] or "Tompkins Square")
    name = spot["address"]
    door = c.load_or_none(MI_BEACON)
    trim = m.ensure_steel_painted()
    changes += _ensure_part_material(actor, "door", door, SAFEHOUSE_LABEL)
    changes += _ensure_part_material(actor, "door_frame", trim, SAFEHOUSE_LABEL)
    changes += _ensure_part_material(actor, "sign_board", trim, SAFEHOUSE_LABEL)
    c.log("updated" if changes else "exists", SAFEHOUSE_LABEL, "osm {0} ({1}), door at ({2:.0f}, {3:.0f}, {4:.0f}) yaw {5:.0f}".format(
        spot["rec"]["id"], name, x, y, ground, yaw))
    return changes


# --------------------------------------------------------------------------------------
# the second safehouse: the same door on an Avenue B storefront across the district
# --------------------------------------------------------------------------------------

# The fast-travel destination. Same rules as the first (at least SAFEHOUSE_MIN_HEIGHT_M tall, not an
# objective roof, a facade edge of SAFEHOUSE_EDGE_MIN or more) but on the far side of the district:
# east of Avenue B's centre line and in the north half of its length, fronting the avenue (within
# SAFEHOUSE2_AVENUE_REACH of it, door on the facade point nearest it), at least SAFEHOUSE2_MIN_APART
# from the first door, and with no fire-escape landing within SAFEHOUSE2_ESCAPE_CLEAR of the door (the
# first landing would cut through the sign). Of those, the one nearest the first safehouse.
SAFEHOUSE2_LABEL = "City_Safehouse_2"
SAFEHOUSE2_ID = "avenue_b"
SAFEHOUSE2_NAME = "[Safehouse 2]"
SAFEHOUSE2_AVENUE = "Avenue B"
SAFEHOUSE2_AVENUE_REACH = 1500.0    # cm from the building to the avenue's centre line
SAFEHOUSE2_MIN_APART = 25000.0      # cm from the first safehouse's door
SAFEHOUSE2_ESCAPE_CLEAR = 400.0     # cm from the door to any fire-escape landing's facade point


def _avenue_frame(district, name):
    """(pieces in cm, origin, north unit, east unit) for a named avenue, or None.

    North is along the avenue toward smaller Y (East 11th Street has a smaller Y than East 6th);
    east is across it toward larger X (Avenue C has a larger X than Avenue B).
    """
    pieces = [district.ring_cm(p) for rec in district.roads if rec.get("name") == name for p in rec["pieces"]]
    points = [p for piece in pieces for p in piece]
    if len(points) < 2:
        return None
    a, b = max(points, key=lambda p: p[1]), min(points, key=lambda p: p[1])
    L = math.hypot(b[0] - a[0], b[1] - a[1]) or 1.0
    north = ((b[0] - a[0]) / L, (b[1] - a[1]) / L)
    east = (-north[1], north[0]) if -north[1] > 0 else (north[1], -north[0])
    mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
    return pieces, mid, north, east


def safehouse2_spot(district):
    """{'rec', 'x', 'y', 'yaw', 'address', 'apart'} for the second safehouse door, or None."""
    first = safehouse_spot(district)
    frame = _avenue_frame(district, SAFEHOUSE2_AVENUE)
    if first is None or frame is None:
        return None
    pieces, mid, north, east = frame
    taken = {rec["id"] for rec in objective_roofs(district).values()}
    taken.add(first["rec"]["id"])
    escapes = [(f[2][0], f[2][1]) for f in fire_escape_spots(district)]
    candidates = []
    for rec in district.buildings:
        if rec["id"] in taken or rec["height_m"] < SAFEHOUSE_MIN_HEIGHT_M:
            continue
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3:
            continue
        cx, cy = geo.centroid(ring)
        rel = (cx - mid[0], cy - mid[1])
        if rel[0] * east[0] + rel[1] * east[1] <= 0.0 or rel[0] * north[0] + rel[1] * north[1] <= 0.0:
            continue
        hits = [closest_point_on_polyline((cx, cy), piece) for piece in pieces]
        hit = min((h for h in hits if h), key=lambda h: h[0], default=None)
        if hit is None or ring_distance(hit[1], ring) > SAFEHOUSE2_AVENUE_REACH:
            continue
        door = _safehouse_door(ring, hit[1])
        if door is None:
            continue
        apart = math.hypot(door[0] - first["x"], door[1] - first["y"])
        if apart < SAFEHOUSE2_MIN_APART:
            continue
        if any(math.hypot(ex - door[0], ey - door[1]) < SAFEHOUSE2_ESCAPE_CLEAR for ex, ey in escapes):
            continue
        candidates.append((apart, rec["id"], rec, door))
    if not candidates:
        return None
    apart, _id, rec, door = min(candidates, key=lambda c_: (c_[0], c_[1]))
    tags = rec.get("tags", {})
    address = "{0} {1}".format(tags.get("addr:housenumber") or "", tags.get("addr:street") or "").strip()
    return {"rec": rec, "x": door[0], "y": door[1], "yaw": door[2], "address": address or SAFEHOUSE2_AVENUE,
            "apart": apart}


def safehouse_spots(district):
    """[(label, spot)] for every safehouse that has a building, first safehouse first."""
    out = []
    for label, fn in ((SAFEHOUSE_LABEL, safehouse_spot), (SAFEHOUSE2_LABEL, safehouse2_spot)):
        spot = fn(district)
        if spot is not None:
            out.append((label, spot))
    return out


def safehouse2_keepout(district):
    """(x, y) of the pavement in front of the second safehouse door, or None."""
    spot = safehouse2_spot(district)
    if spot is None:
        return None
    yaw = math.radians(spot["yaw"])
    return spot["x"] + math.cos(yaw) * 150.0, spot["y"] + math.sin(yaw) * 150.0


def ensure_safehouse2(district, existing):
    """City_Safehouse_2 (ASafehouse) on its Avenue B storefront. Idempotent by label."""
    cls = c.find_class("Safehouse", SAFEHOUSE_CLASS)
    if cls is None:
        c.log("skipped", SAFEHOUSE2_LABEL, "ASafehouse not exposed; build the module")
        return 0
    spot = safehouse2_spot(district)
    if spot is None:
        c.log("FAILED", SAFEHOUSE2_LABEL, "no Avenue B storefront east and north qualifies")
        return 0
    x, y, yaw = spot["x"], spot["y"], spot["yaw"]
    ground = ground_z(x + math.cos(math.radians(yaw)) * 60.0, y + math.sin(math.radians(yaw)) * 60.0,
                      SIDEWALK_TOP, existing)
    actor, changes = _ensure_located(existing, SAFEHOUSE2_LABEL, cls, unreal.Vector(x, y, ground), yaw)
    if actor is None:
        return changes
    changes += _ensure_tags(actor, ["City", "CitySafehouse", "osm:" + spot["rec"]["id"]])
    if str(actor.get_editor_property("safehouse_id")) != SAFEHOUSE2_ID:
        actor.set_editor_property("safehouse_id", unreal.Name(SAFEHOUSE2_ID))
        changes += 1
    changes += _ensure_safehouse_names(actor, SAFEHOUSE2_NAME, spot["address"])
    door = c.load_or_none(MI_BEACON)
    trim = m.ensure_steel_painted()
    changes += _ensure_part_material(actor, "door", door, SAFEHOUSE2_LABEL)
    changes += _ensure_part_material(actor, "door_frame", trim, SAFEHOUSE2_LABEL)
    changes += _ensure_part_material(actor, "sign_board", trim, SAFEHOUSE2_LABEL)
    c.log("updated" if changes else "exists", SAFEHOUSE2_LABEL,
          "osm {0} ({1}), door at ({2:.0f}, {3:.0f}, {4:.0f}) yaw {5:.0f}, {6:.0f} m from the first".format(
              spot["rec"]["id"], spot["address"], x, y, ground, yaw, spot["apart"] / 100.0))
    return changes


# --------------------------------------------------------------------------------------
# the interior entrance: a door on a three-storey building by the park into L_Int_Sample
# --------------------------------------------------------------------------------------

# AInteriorEntrance (Source/Hawkeye/World/InteriorEntrance.h) on the facade nearest the park of the
# building picked here, on its street frontage (the facade nearest a road's centre line): three storeys
# (INTERIOR_MIN_HEIGHT_M to INTERIOR_MAX_HEIGHT_M), within
# INTERIOR_PARK_REACH of the park, not an objective roof, a safehouse or the chapter end's building, a facade
# edge of SAFEHOUSE_EDGE_MIN or more, no fire-escape landing within INTERIOR_ESCAPE_CLEAR of the door, and
# INTERIOR_SAFEHOUSE_CLEAR from both safehouse doors. Of those, the one nearest the park. Two return points:
# City_InteriorReturn_Sample on the pavement INTERIOR_RETURN_OUT out from the door (the front exit comes
# out there) and City_InteriorRoof_Sample on its roof (the roof door does). Each is tagged with its own
# label too, since a packaged build has no labels. Roof clutter leaves that roof bare.
INTERIOR_LABEL = "City_InteriorEntrance_Sample"
INTERIOR_RETURN_LABEL = "City_InteriorReturn_Sample"
INTERIOR_ROOF_LABEL = "City_InteriorRoof_Sample"
INTERIOR_MAP = "/Game/Maps/L_Int_Sample"
INTERIOR_NAME = "[Auction house]"
INTERIOR_CLASS = "/Script/Hawkeye.InteriorEntrance"
INTERIOR_MIN_HEIGHT_M = 9.5         # three storeys: 3 x 330 cm, less than four
INTERIOR_MAX_HEIGHT_M = 12.5
INTERIOR_PARK_REACH = 4000.0        # cm; the park's own frontage is taken by taller tenements
INTERIOR_SAFEHOUSE_CLEAR = 3000.0   # cm from either safehouse door
INTERIOR_ESCAPE_CLEAR = 400.0       # cm from the door to a fire-escape landing's facade point
INTERIOR_RETURN_OUT = 220.0         # cm out from the door to the doorstep point
INTERIOR_ROOF_EDGE = 250.0          # cm; the roof point is at least this far inside every roof edge
INTERIOR_KEEPOUT = 250.0
PAWN_HALF_HEIGHT = 100.0            # cm above the floor a return point stands (a capsule's centre)


def interior_spot(district):
    """{'rec', 'x', 'y', 'yaw', 'address', 'roof': (x, y, z), 'reach'} for the interior entrance, or None."""
    if not district.parks:
        return None
    park = district.ring_cm(district.parks[0]["outer"])
    start, _rot = player_start_transform(district)
    start = (start.x, start.y)
    taken = {rec["id"] for rec in objective_roofs(district).values()}
    doors = [spot for _label, spot in safehouse_spots(district)]
    taken |= {spot["rec"]["id"] for spot in doors}
    end = chapter_end_spot(district)
    if end is not None:
        taken.add(end["osm"])
    escapes = [(f[2][0], f[2][1]) for f in fire_escape_spots(district)]
    roads = [path for _rec, paths in road_paths(district) for path in paths]
    candidates = []
    for rec in district.buildings:
        if rec["id"] in taken or not INTERIOR_MIN_HEIGHT_M <= rec["height_m"] <= INTERIOR_MAX_HEIGHT_M:
            continue
        ring = geo.clean_ring(district.ring_cm(rec["outer"]), min_edge=5.0, collinear_tol=2.0)
        if len(ring) < 3 or ring_distance(start, ring) <= SAFEHOUSE_START_CLEAR:
            continue
        reach = min(ring_distance(p, ring) for p in park)
        if reach > INTERIOR_PARK_REACH:
            continue
        centre = geo.centroid(ring)
        hits = [closest_point_on_polyline(centre, path) for path in roads]
        street = min((h for h in hits if h), key=lambda h: h[0], default=None)
        door = _door_clear_of(ring, street[1], escapes) if street is not None else None
        if door is None:
            continue
        if any(math.hypot(d["x"] - door[0], d["y"] - door[1]) < INTERIOR_SAFEHOUSE_CLEAR for d in doors):
            continue
        roof = _roof_point(ring)
        if roof is None:
            continue
        candidates.append((reach, rec["id"], rec, door, roof))
    if not candidates:
        return None
    reach, _id, rec, door, roof = min(candidates, key=lambda c_: (c_[0], c_[1]))
    tags = rec.get("tags", {})
    address = "{0} {1}".format(tags.get("addr:housenumber") or "", tags.get("addr:street") or "").strip()
    return {"rec": rec, "x": door[0], "y": door[1], "yaw": door[2], "address": address, "reach": reach,
            "roof": (roof[0], roof[1], rec["height_m"] * 100.0)}


def _door_clear_of(ring, target, avoid):
    """(x, y, yaw, edge length) of a door on ring's edge nearest target, slid along that edge (SAFEHOUSE_EDGE_INSET
    from its ends, 50 cm steps) to the spot farthest from every point in avoid; None when the edge is short or no
    spot is INTERIOR_ESCAPE_CLEAR from them (a fire escape usually hangs on the same street facade)."""
    first = _safehouse_door(ring, target)
    if first is None:
        return None
    n = len(ring)
    best_edge = min(range(n), key=lambda i: closest_point_on_polyline(target, [ring[i], ring[(i + 1) % n]])[0])
    a, b = ring[best_edge], ring[(best_edge + 1) % n]
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    ux, uy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
    best = None
    t = SAFEHOUSE_EDGE_INSET
    while t <= length - SAFEHOUSE_EDGE_INSET + 0.01:
        x, y = a[0] + ux * t, a[1] + uy * t
        clear = min([math.hypot(px - x, py - y) for px, py in avoid] or [1.0e9])
        if best is None or clear > best[0] + 0.5:
            best = (clear, x, y)
        t += 50.0
    if best is None or best[0] < INTERIOR_ESCAPE_CLEAR:
        return None
    return best[1], best[2], first[2], length


def _roof_point(ring):

    """The point of a 50 cm grid inside ring farthest from its edges, if that is INTERIOR_ROOF_EDGE or more."""
    x0, y0, x1, y1 = geo.bounds(ring)
    best = None
    gx = x0 + 25.0
    while gx < x1:
        gy = y0 + 25.0
        while gy < y1:
            if geo.point_in_polygon((gx, gy), ring):
                edge = _edge_distance((gx, gy), ring)
                if best is None or edge > best[2] + 1e-6:
                    best = (gx, gy, edge)
            gy += 50.0
        gx += 50.0
    return best if best is not None and best[2] >= INTERIOR_ROOF_EDGE else None


def interior_building_ids(district):
    spot = interior_spot(district)
    return {spot["rec"]["id"]} if spot is not None else set()


def interior_keepout(district):
    """(x, y) of the pavement in front of the interior entrance, or None."""
    spot = interior_spot(district)
    if spot is None:
        return None
    yaw = math.radians(spot["yaw"])
    return spot["x"] + math.cos(yaw) * 150.0, spot["y"] + math.sin(yaw) * 150.0


def ensure_interior_entrance(district, existing):
    """City_InteriorEntrance_Sample and its two return points. Idempotent by label."""
    cls = c.find_class("InteriorEntrance", INTERIOR_CLASS)
    if cls is None:
        c.log("skipped", INTERIOR_LABEL, "AInteriorEntrance not exposed; build the module")
        return 0
    spot = interior_spot(district)
    if spot is None:
        c.log("FAILED", INTERIOR_LABEL, "no three-storey building by the park qualifies")
        return 0
    x, y, yaw = spot["x"], spot["y"], spot["yaw"]
    ux, uy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    ground = ground_z(x + ux * 60.0, y + uy * 60.0, SIDEWALK_TOP, existing)
    actor, changes = _ensure_located(existing, INTERIOR_LABEL, cls, unreal.Vector(x, y, ground), yaw)
    if actor is None:
        return changes
    changes += _ensure_tags(actor, ["City", "CityInterior", "osm:" + spot["rec"]["id"]])
    world = c.load_or_none(INTERIOR_MAP)
    current = actor.get_editor_property("interior")
    current_path = str(current.get_path_name()).split(".")[0] if current is not None else ""
    if world is not None and current_path != INTERIOR_MAP:
        actor.set_editor_property("interior", world)
        changes += 1
    elif world is None:
        c.log("FAILED", INTERIOR_LABEL, INTERIOR_MAP + " missing; run generate_interior.py first")
    if str(actor.get_editor_property("return_point_label")) != INTERIOR_RETURN_LABEL:
        actor.set_editor_property("return_point_label", unreal.Name(INTERIOR_RETURN_LABEL))
        changes += 1
    if str(actor.get_editor_property("display_name")) != INTERIOR_NAME:
        actor.set_editor_property("display_name", unreal.Text(INTERIOR_NAME))
        changes += 1
    wood = c.load_or_none(m.mi_int_path("Wood", "Door"))
    trim = m.ensure_steel_painted()
    changes += _ensure_part_material(actor, "door", wood or trim, INTERIOR_LABEL)
    changes += _ensure_part_material(actor, "door_frame", trim, INTERIOR_LABEL)
    changes += _ensure_part_material(actor, "sign_board", trim, INTERIOR_LABEL)

    out = (x + ux * INTERIOR_RETURN_OUT, y + uy * INTERIOR_RETURN_OUT)
    step = ground_z(out[0], out[1], SIDEWALK_TOP, existing)
    point, n = _ensure_located(existing, INTERIOR_RETURN_LABEL, unreal.TargetPoint,
                               unreal.Vector(out[0], out[1], step + PAWN_HALF_HEIGHT), yaw)
    changes += n
    if point is not None:
        changes += _ensure_tags(point, ["City", "CityInteriorReturn", INTERIOR_RETURN_LABEL])
    rx, ry, rz = spot["roof"]
    roof, n = _ensure_located(existing, INTERIOR_ROOF_LABEL, unreal.TargetPoint,
                              unreal.Vector(rx, ry, rz + PAWN_HALF_HEIGHT), yaw)
    changes += n
    if roof is not None:
        changes += _ensure_tags(roof, ["City", "CityInteriorReturn", INTERIOR_ROOF_LABEL])
    c.log("updated" if changes else "exists", INTERIOR_LABEL,
          "osm {0} ({1}, {2:.1f} m, {3:.0f} m from the park), door at ({4:.0f}, {5:.0f}, {6:.0f}) yaw {7:.0f}".format(
              spot["rec"]["id"], spot["address"] or "no address", spot["rec"]["height_m"], spot["reach"] / 100.0,
              x, y, ground, yaw))
    return changes


# --------------------------------------------------------------------------------------
# side challenges: a pedestal at each challenge's start
# --------------------------------------------------------------------------------------

# AChallengeStart (Source/Hawkeye/Challenge/ChallengeStart.h) at the StartLocation of every
# UChallengeDefinition create_challenges.py wrote under /Game/Challenges: three on rooftops for the
# archery ranges, three at street corners for the traversal routes. The pedestal stands on whatever
# is under it (the roof, the sidewalk or the edge of an avenue's asphalt) and faces StartYaw.
CHALLENGE_PREFIX = "City_Challenge_"
CHALLENGE_PATH = "/Game/Challenges"
CHALLENGE_ASSET_PREFIX = "DA_Challenge_"
CHALLENGE_CLASS = "/Script/Hawkeye.ChallengeStart"


def challenge_definitions():
    """{id: UChallengeDefinition} for every DA_Challenge_ asset, by id."""
    out = {}
    if not unreal.EditorAssetLibrary.does_directory_exist(CHALLENGE_PATH):
        return out
    for path in sorted(unreal.EditorAssetLibrary.list_assets(CHALLENGE_PATH, recursive=False)):
        package = path.split(".")[0]
        if not package.split("/")[-1].startswith(CHALLENGE_ASSET_PREFIX):
            continue
        asset = c.load_or_none(package)
        if asset is not None:
            out[str(asset.get_editor_property("id"))] = asset
    return out


def ensure_challenge_starts(district, existing):
    """City_Challenge_<id> (AChallengeStart) per challenge definition. Idempotent by label; a pedestal
    whose challenge is gone is removed."""
    cls = c.find_class("ChallengeStart", CHALLENGE_CLASS)
    if cls is None:
        c.log("skipped", CHALLENGE_PREFIX + "*", "AChallengeStart not exposed; build the module")
        return 0
    definitions = challenge_definitions()
    if not definitions:
        c.log("skipped", CHALLENGE_PREFIX + "*", "no challenge definitions; run create_challenges.py")
    changes = 0
    for label, actor in list(existing.items()):
        if label.startswith(CHALLENGE_PREFIX) and label[len(CHALLENGE_PREFIX):] not in definitions:
            actor.destroy_actor()
            existing.pop(label, None)
            c.log("updated", label, "removed; its challenge is gone")
            changes += 1
    for cid, definition in sorted(definitions.items()):
        label = CHALLENGE_PREFIX + cid
        start = definition.get_editor_property("start_location")
        yaw = definition.get_editor_property("start_yaw")
        # The feet on the slab under the start: a roof for archery, the street for traversal.
        z = start.z
        kind = definition.get_editor_property("type")
        if str(getattr(kind, "name", kind)).upper().endswith("TRAVERSAL"):
            z = ground_z(start.x, start.y, start.z, existing)
        actor, moved = _ensure_located(existing, label, cls, unreal.Vector(start.x, start.y, z), yaw)
        if actor is None:
            continue
        changed = moved
        if actor.get_editor_property("definition") != definition:
            actor.set_editor_property("definition", definition)
            changed += 1
        changed += _ensure_tags(actor, ["City", "CityChallenge", "challenge:" + cid])
        c.log("updated" if changed else "exists", label, "{0} at ({1:.0f}, {2:.0f}, {3:.0f}) yaw {4:.0f}".format(
            str(definition.get_editor_property("name")), start.x, start.y, z, yaw))
        changes += changed
    return changes


# --------------------------------------------------------------------------------------
# street crimes: the spots they can start at
# --------------------------------------------------------------------------------------

# ACrimeSpot (Source/Hawkeye/Crime/CrimeSpot.h) at each of the places create_crimes.py plans: eight at
# street corners (the mugging, robbery and ambush), four on roofs (the rooftop crime), two to four in
# alleys (the mugging and ambush), none within 40 m of a safehouse, a pedestal or the interior door.
# UCrimeSubsystem starts crimes at them while she roams.
CRIME_SPOT_PREFIX = "City_CrimeSpot_"
CRIME_SPOT_CLASS = "/Script/Hawkeye.CrimeSpot"


def _same_assets(current, wanted):
    return [c.safe_name(a) for a in (current or [])] == [c.safe_name(a) for a in wanted]


def ensure_crime_spots(district, existing):
    """City_CrimeSpot_<n> (ACrimeSpot) per planned spot, holding the crimes that may start there.
    Idempotent by label; spots past the plan are removed."""
    import create_crimes as cr  # noqa: PLC0415 - it imports this module in turn
    cls = c.find_class("CrimeSpot", CRIME_SPOT_CLASS)
    if cls is None:
        c.log("skipped", CRIME_SPOT_PREFIX + "*", "ACrimeSpot not exposed; build the module")
        return 0
    definitions = cr.crime_definitions()
    if not definitions:
        c.log("skipped", CRIME_SPOT_PREFIX + "*", "no crime definitions; run create_crimes.py")
        return 0
    spots = cr.plan_crime_spots(district)
    fixed = cr.STREET_SPOTS + cr.ROOF_SPOTS
    if not fixed + cr.ALLEY_SPOTS[0] <= len(spots) <= fixed + cr.ALLEY_SPOTS[1]:
        c.log("FAILED", CRIME_SPOT_PREFIX + "*", "planned {0} crime spots, want {1} to {2}".format(
            len(spots), fixed + cr.ALLEY_SPOTS[0], fixed + cr.ALLEY_SPOTS[1]))
    unreal.log("[Hawkeye] info     {0}*  (alley search: {1})".format(CRIME_SPOT_PREFIX, cr.alley_stats(district)))
    wanted = {CRIME_SPOT_PREFIX + str(s["index"]) for s in spots}
    changes = 0
    for label, actor in list(existing.items()):
        if label.startswith(CRIME_SPOT_PREFIX) and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            c.log("updated", label, "removed; no longer planned")
            changes += 1
    for spot in spots:
        label = CRIME_SPOT_PREFIX + str(spot["index"])
        x, y = spot["at"]
        z = spot["z"] if spot["rooftop"] else ground_z(x, y, spot["z"], existing)
        actor, changed = _ensure_located(existing, label, cls, unreal.Vector(x, y, z), spot["yaw"])
        if actor is None:
            continue
        crimes = [definitions[cid] for cid in spot["crimes"] if cid in definitions]
        if not _same_assets(actor.get_editor_property("crimes"), crimes):
            actor.set_editor_property("crimes", crimes)
            changed += 1
        if bool(actor.get_editor_property("rooftop")) != spot["rooftop"]:
            actor.set_editor_property("rooftop", spot["rooftop"])
            changed += 1
        if bool(actor.get_editor_property("alley")) != (spot["kind"] == "alley"):
            actor.set_editor_property("alley", spot["kind"] == "alley")
            changed += 1
        escape = unreal.Vector(0.0, 0.0, 0.0)
        if spot["escape"] is not None:
            ex, ey, ez = spot["escape"]
            escape = unreal.Vector(ex, ey, ground_z(ex, ey, ez, existing))
        if not same_vector(actor.get_editor_property("escape_location"), escape, 0.5):
            actor.set_editor_property("escape_location", escape)
            changed += 1
        changed += _ensure_tags(actor, ["City", "CityCrimeSpot", "crime:" + spot["kind"], "osm:" + spot["osm"]])
        c.log("updated" if changed else "exists", label, cr.describe(spot))
        changes += changed
    return changes


def open_or_create_map():
    """(ok, created)."""
    subsystem = c.level_editor_subsystem()
    if c.exists(MAP_PATH):
        return bool(subsystem.load_level(MAP_PATH)), False
    c.ensure_directory("/Game/Maps")
    try:
        ok = subsystem.new_level(MAP_PATH, False)   # is_partitioned_world = False
    except TypeError:
        ok = subsystem.new_level(MAP_PATH)
    if not ok:
        c.log("FAILED", MAP_PATH, "new_level returned false")
    return bool(ok), bool(ok)


def ensure_materials():
    """The night look (_materials.ensure_city_look): facade instances per style, snowy sidewalks,
    park and ground, wet asphalt."""
    look = m.ensure_city_look()
    materials = {
        "facades": look["facades"],
        "ground": m.ensure_prop_instance("Ground", *CLUTTER_MATERIALS["Ground"]),
        "asphalt": look["asphalt"],
        "grass": look["park"],
        "sidewalk": look["sidewalk"],
        "stars": look["stars"],
    }
    return materials


def run():
    if not data_available():
        c.log("skipped", MAP_PATH, "no OSM data; run Tools\\fetch-osm.ps1 first")
        return False

    district = District()
    materials = ensure_materials()

    ok, created = open_or_create_map()
    if not ok:
        return False

    pieces = building_pieces(district, materials["facades"]) + street_pieces(district, materials)
    existing = actors_by_label()

    changes = remove_stale(pieces, existing)
    mesh_changes, actor_changes = build_pieces(pieces, existing)
    changes += actor_changes
    changes += ensure_lighting(existing)
    changes += ensure_player_start(district, existing)
    changes += ensure_nav_volume(district, existing)
    changes += ensure_game_mode()
    changes += remove_prison_actors(existing)
    changes += ensure_objective_volumes(district, existing)
    changes += ensure_objective_beacons(district, existing)
    changes += ensure_chapter_end(district, existing)
    changes += ensure_safehouse(district, existing)
    changes += ensure_safehouse2(district, existing)
    changes += ensure_interior_entrance(district, existing)
    changes += ensure_challenge_starts(district, existing)
    changes += ensure_crime_spots(district, existing)
    changes += ensure_thugs(district, existing)
    changes += ensure_archers(district, existing)
    changes += ensure_clint(district, existing)
    changes += ensure_lucky(district, existing)
    changes += ensure_street_lamps(district, existing)
    changes += ensure_ambience(district, existing)
    changes += ensure_lamp_buzz(district, existing)
    changes += ensure_ledge_spawner(district, existing)
    changes += ensure_dressing_actors(existing)
    changes += ensure_test_blocks(district, existing)
    # The world map data is its own asset, not part of the level.
    ensure_city_map(district)

    if created or changes:
        c.level_editor_subsystem().save_current_level()
        c.log("created" if created else "updated", MAP_PATH,
              "{0} buildings, {1} pieces total, {2} mesh rebuild(s), {3} level change(s)".format(
                  len(district.buildings), len(pieces), mesh_changes, changes))
    else:
        c.log("exists", MAP_PATH, "nothing changed ({0} mesh rebuild(s)); level not saved".format(mesh_changes))
    return True


if __name__ == "__main__":
    run()
    c.print_summary("city")
