"""Create Act 1's chase: the thief's route and what stands along it (claude-docs/gameplay-semantics.md, "Chases").

    /Game/Missions/Chases/DA_Chase_CH01_NightShift   UChaseRouteDefinition, planned by _chase.plan_chase from the
                                                     district's OpenStreetMap records (DA_CH01_NightShift.Chase)
    /Game/Kit/Chase/SM_Kit_Dumpster                  the dumpster of wrapping paper the botched grapple drops
                                                     Kate into: an open steel bin, paper heaped in it
    /Game/Kit/Chase/SM_Kit_RoofVent                  the 90 cm vent box she vaults on the opening run
    /Game/Kit/Chase/SM_Kit_RoofBulkhead              a 150 cm stair bulkhead she mantles, where no neighbouring
                                                     roof stands a step up
    /Game/Kit/Chase/MI_Chase_*                       their colours (_materials' prop instances)

The paper burst is NS_PaperBurst (create_vfx.py). The thief is BP_Thug. UChaseSubsystem spawns the dumpster and
the vent boxes where the route says when the chapter starts; nothing is placed in the map.

Idempotent: the meshes carry a recipe hash and the route a plan hash; each is rewritten only when it changes.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\create_chase.py" -unattended -nullrhi -nosplash -nop4 -stdout
"""

import hashlib
import json
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _chase  # noqa: E402
import _common as c  # noqa: E402
import _materials as m  # noqa: E402

CHASE_PATH = "/Game/Missions/Chases"
CHASE_NAME = "DA_Chase_CH01_NightShift"
KIT_PATH = "/Game/Kit/Chase"
DUMPSTER_MESH = "SM_Kit_Dumpster"
VENT_MESH = "SM_Kit_RoofVent"
BULKHEAD_MESH = "SM_Kit_RoofBulkhead"
THIEF_BP = ("/Game/Blueprints/AI", "BP_Thug")
PAPER_VFX = "/Game/VFX/NS_PaperBurst"
BUILD_TAG = "HawkeyeBuild"
MESH_VERSION = 2

# Colours: (rgb, roughness). The bin's green steel; the paper in three loud colours.
COLOURS = {
    "ChaseDumpster": ((0.035, 0.11, 0.05), 0.55),
    "ChasePaperRed": ((0.62, 0.03, 0.04), 0.4),
    "ChasePaperGold": ((0.75, 0.52, 0.08), 0.25),
    "ChasePaperGreen": ((0.05, 0.38, 0.12), 0.4),
    "ChaseVent": ((0.42, 0.43, 0.45), 0.35),
    "ChaseBulkhead": ((0.30, 0.25, 0.22), 0.7),
}
DUMPSTER_SLOTS = ("ChaseDumpster", "ChasePaperRed", "ChasePaperGold", "ChasePaperGreen")
# The paper is plain colour, not M_Prop: its snow layer settled on the heap read as white blocks (2026-11-10).
UNSNOWED = {"ChasePaperRed", "ChasePaperGold", "ChasePaperGreen"}

# The thief's lines at the places of the route (create_narrative.py makes the sequences).
SEQUENCES = {
    "first_vault_sequence": "seq_ch01_vault",
    "pulls_ahead_sequence": "seq_ch01_ahead",
    "botch_sequence": "seq_ch01_botch",
    "dumpster_sequence": "seq_ch01_dumpster",
    "climb_out_sequence": "seq_ch01_climb",
    "cornered_sequence": "seq_ch01_cornered",
    "caught_sequence": "seq_ch01_caught",
}

GS_PRIM = unreal.GeometryScript_Primitives


def _hash(value):
    return hashlib.md5(json.dumps(value, sort_keys=True, default=str).encode("utf-8")).hexdigest()[:12]


# --------------------------------------------------------------------------------------------------
# meshes
# --------------------------------------------------------------------------------------------------


def _box(mesh, slot, cx, cy, z0, sx, sy, sz, yaw=0.0):
    options = unreal.GeometryScriptPrimitiveOptions()
    options.set_editor_property("material_id", slot)
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(0.0, 0.0, yaw))
    GS_PRIM.append_box(mesh, options, xf, sx, sy, sz, 0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.BASE)


def _build_dumpster():
    """An open steel bin 180 x 110 x 140 cm (origin at the middle of its base, length along X), heaped with paper."""
    mesh = unreal.DynamicMesh()
    length, depth, height = _chase.DUMPSTER
    wall = 6.0
    _box(mesh, 0, 0.0, 0.0, 0.0, length, depth, wall)
    for sy in (-1.0, 1.0):
        _box(mesh, 0, 0.0, sy * (depth - wall) * 0.5, 0.0, length, wall, height)
    for sx in (-1.0, 1.0):
        _box(mesh, 0, sx * (length - wall) * 0.5, 0.0, 0.0, wall, depth, height)
    # The lid thrown back against the wall side, and the rim's lip.
    _box(mesh, 0, 0.0, -depth * 0.5 - 4.0, height - 10.0, length, 4.0, 60.0)
    # Wrapping paper heaped to just under the rim: lumps in red, gold and green.
    lumps = [(-55, -25, 1, 45, 40, 48, 20), (-10, 20, 2, 50, 38, 56, 65), (40, -15, 3, 48, 42, 52, 35),
             (65, 25, 1, 38, 34, 44, 80), (-60, 28, 3, 40, 30, 50, 10), (10, -30, 2, 44, 30, 46, 55),
             (-25, -5, 1, 40, 36, 62, 30), (30, 10, 3, 42, 36, 66, 75)]
    for x, y, slot, sx, sy, top, yaw in lumps:
        _box(mesh, slot, float(x), float(y), 40.0, float(sx), float(sy), float(top) + 40.0, float(yaw))
    return mesh


def _build_vent():
    """A rooftop vent box 120 x 50 x 90 cm (width along Y, depth along X) with a cap."""
    mesh = unreal.DynamicMesh()
    width, deep, height = _chase.PROP_SIZE
    _box(mesh, 0, 0.0, 0.0, 0.0, deep, width, height - 8.0)
    _box(mesh, 0, 0.0, 0.0, height - 8.0, deep + 10.0, width + 10.0, 8.0)
    return mesh


def _build_bulkhead():
    """A stair bulkhead (length along X, width along Y, BULKHEAD high) with a flat coping round its top."""
    mesh = unreal.DynamicMesh()
    length, width, height = _chase.BULKHEAD
    _box(mesh, 0, 0.0, 0.0, 0.0, length, width, height - 10.0)
    _box(mesh, 0, 0.0, 0.0, height - 10.0, length + 10.0, width + 10.0, 10.0)
    return mesh


def _write_mesh(name, build, slots):
    full = c.asset_path(KIT_PATH, name)
    materials = [c.ensure_constant_color_material("M_" + key, KIT_PATH, COLOURS[key][0], COLOURS[key][1]) if key in UNSNOWED
                 else m.ensure_prop_instance(key, COLOURS[key][0], COLOURS[key][1]) for key in slots]
    spec = "mesh-" + _hash([MESH_VERSION, name, list(_chase.DUMPSTER), list(_chase.PROP_SIZE), list(_chase.BULKHEAD),
                            list(slots)])
    existing = c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, BUILD_TAG) == spec:
        c.log("exists", full)
        return existing
    mesh = build()
    if existing is None:
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property("enable_recompute_normals", True)
        options.set_editor_property("enable_recompute_tangents", True)
        options.set_editor_property("enable_nanite", False)
        options.set_editor_property("enable_collision", True)
        options.set_editor_property("collision_mode", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, full, options)
        static_mesh = result[0] if isinstance(result, (list, tuple)) else result
        if static_mesh is None:
            c.log("FAILED", full, "create_new_static_mesh_asset_from_mesh returned None")
            return None
    else:
        static_mesh = existing
        options = unreal.GeometryScriptCopyMeshToAssetOptions()
        options.set_editor_property("enable_recompute_normals", True)
        options.set_editor_property("enable_recompute_tangents", True)
        unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(mesh, static_mesh, options, unreal.GeometryScriptMeshWriteLOD())
    for index, material in enumerate(materials):
        if material is not None:
            static_mesh.set_material(index, material)
    unreal.EditorAssetLibrary.set_metadata_tag(static_mesh, BUILD_TAG, spec)
    c.save(static_mesh)
    c.log("created" if existing is None else "updated", full, "{0} material slot(s)".format(len(materials)))
    return static_mesh


# --------------------------------------------------------------------------------------------------
# the route
# --------------------------------------------------------------------------------------------------


def _enum(name, value):
    """unreal.ChaseLeg.JUMP_GAP from ("ChaseLeg", "JumpGap")."""
    return getattr(getattr(unreal, name), re.sub(r"(?<!^)(?=[A-Z])", "_", value).upper())


def _vec(t):
    return unreal.Vector(float(t[0]), float(t[1]), float(t[2]))


def _waypoints(plan):
    out = []
    for w in plan["waypoints"]:
        point = unreal.ChaseWaypoint()
        point.set_editor_property("location", _vec(w["at"]))
        point.set_editor_property("leg", _enum("ChaseLeg", w["leg"]))
        point.set_editor_property("completes_objective", unreal.Name(w["objective"] or "None"))
        point.set_editor_property("sequence", unreal.Name(w["sequence"] or "None"))
        out.append(point)
    return out


def _grapples(plan):
    out = []
    for g in plan["grapples"]:
        grapple = unreal.ChaseGrapple()
        grapple.set_editor_property("stand", _vec(g["stand"]))
        grapple.set_editor_property("anchor", _vec(g["anchor"]))
        grapple.set_editor_property("chain_anchor", _vec(g["chain"]) if g["chain"] else unreal.Vector(0.0, 0.0, 0.0))
        grapple.set_editor_property("chain_at", float(g["at"]))
        grapple.set_editor_property("beat", bool(g["beat"]))
        out.append(grapple)
    return out


def _prop(at, mesh):
    prop = unreal.ChaseProp()
    prop.set_editor_property("transform", unreal.Transform(location=_vec(at[:3]), rotation=unreal.Rotator(0.0, 0.0, at[3])))
    prop.set_editor_property("mesh", mesh)
    return prop


def _fields(plan, dumpster, vent, bulkhead, thief, paper):
    beat = plan["beat"]
    props = [_prop(p, vent) for p in plan["props"]] + [_prop(b, bulkhead) for b in plan["bulkheads"]]
    fields = [
        ("waypoints", _waypoints(plan)),
        ("player_start", _vec(plan["player_start"])),
        ("player_start_yaw", float(plan["player_yaw"])),
        ("thief_class", thief),
        ("thief_objectives", [unreal.Name("chase_thief"), unreal.Name("catch_thief")]),
        ("catch_objective", unreal.Name("catch_thief")),
        ("lost_heading", unreal.Text("[He got away]")),
        ("has_beat", True),
        ("beat_zone_centre", _vec(beat["zone"])),
        ("beat_zone_extent", _vec(beat["zone_extent"])),
        ("beat_zone_yaw", float(beat["zone_yaw"])),
        ("beat_anchor", _vec(beat["anchor"])),
        ("beat_waypoint", int(beat["waypoint"])),
        ("dumpster_location", _vec(beat["dumpster"])),
        ("dumpster_yaw", float(beat["dumpster_yaw"])),
        ("beat_landing", _vec(beat["landing"])),
        ("beat_exit", _vec(beat["exit"])),
        ("dumpster_mesh", dumpster),
        ("paper_vfx", paper),
        ("props", props),
        ("grapples", _grapples(plan)),
        ("roof_ids", [unreal.Name(r) for r in plan["roofs"]]),
    ]
    fields += [(prop, unreal.Name(seq)) for prop, seq in sorted(SEQUENCES.items())]
    return fields


def ensure_route(plan, dumpster, vent, bulkhead):
    cls = c.find_class("ChaseRouteDefinition", "/Script/Hawkeye.ChaseRouteDefinition")
    full = c.asset_path(CHASE_PATH, CHASE_NAME)
    if cls is None:
        c.log("FAILED", full, "UChaseRouteDefinition not exposed to Python; build the module")
        return None
    thief = c.load_generated_class(*THIEF_BP)
    paper = c.load_or_none(PAPER_VFX)
    if thief is None or paper is None:
        c.log("FAILED", full, "BP_Thug or NS_PaperBurst missing; run create_world_blueprints and create_vfx first")
        return None
    factory = c.new_factory("DataAssetFactory")
    c.set_props(factory, [("data_asset_class", cls)], "DataAssetFactory")
    asset, created = c.create_asset(CHASE_NAME, CHASE_PATH, cls, factory, quiet=True)
    if asset is None:
        return None
    clean = dict(plan)
    clean.pop("check_points", None)
    build = "plan-" + _hash([clean, sorted(SEQUENCES.items()), c.safe_name(thief), c.safe_name(paper),
                             c.safe_name(dumpster), c.safe_name(vent), c.safe_name(bulkhead)])
    if not created and unreal.EditorAssetLibrary.get_metadata_tag(asset, BUILD_TAG) == build:
        c.log("exists", full, _chase.describe(plan))
        return asset
    for prop, value in _fields(plan, dumpster, vent, bulkhead, thief, paper):
        try:
            asset.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(full, prop), exc)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, BUILD_TAG, build)
    c.save(asset)
    c.log("created" if created else "updated", full, _chase.describe(plan))
    return asset


def run():
    import generate_city as gen  # noqa: PLC0415
    if not gen.data_available():
        c.log("skipped", CHASE_PATH, "no OSM data; run Tools\\fetch-osm.ps1 first")
        return None
    c.ensure_directory(KIT_PATH)
    c.ensure_directory(CHASE_PATH)
    dumpster = _write_mesh(DUMPSTER_MESH, _build_dumpster, DUMPSTER_SLOTS)
    vent = _write_mesh(VENT_MESH, _build_vent, ("ChaseVent",))
    bulkhead = _write_mesh(BULKHEAD_MESH, _build_bulkhead, ("ChaseBulkhead",))
    plan = _chase.plan_chase(gen.District())
    if plan is None:
        c.log("FAILED", c.asset_path(CHASE_PATH, CHASE_NAME), "no chase route fits the district")
        return None
    return ensure_route(plan, dumpster, vent, bulkhead)


if __name__ == "__main__":
    run()
    c.print_summary("chase")
