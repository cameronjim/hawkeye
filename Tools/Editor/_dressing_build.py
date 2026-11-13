"""December dressing, the engine half (docs/plans/december-dressing.md): the hero block's plan drawn in the district.

generate_city.py calls this; it does not run on its own. It reads the committed plan
(Tools/Data/dressing/<block>.json, written by write_dressing_plan.py from the same OpenStreetMap records), and:

* builds the kit under /Game/City/EastVillage/Dressing: SM_Dress_Bulb, SM_Dress_Wreath, SM_Dress_Tree (unit shapes,
  _dressing_kit.MESH_UNITS) and SM_Dress_RoofSnow_<block> (every roof outline extruded by its thickness, world cm);
  each carries a ``CityHash`` of its recipe and is rebuilt only when that changes;
* makes the materials: M_DressGlow (Color x Intensity, usable on instanced meshes) with an MI_DressGlow_<key> per
  glow (bulbs, windows, candle flames), and MI_Prop_Dress<key> under M_Prop for the rest, so snow settles on top;
* turns ``_dressing_kit.instance_groups`` into clutter groups (kind ``Dress_<group>``) for DA_EastVillage_CityProps:
  ACityLedgeSpawner draws each as one hierarchical instanced mesh at load, exactly like the street clutter;
* places the few real lights (``City_DressLight_<name>``, movable point lights, no shadows) and the reference
  cameras (``City_DressShot_<name>``, CameraActors tagged DressShot and ``shot:<name>``) the Hawkeye.Screenshot.Dressing
  pass looks through.

Constraints (anchor lines, keep-outs, structures, props, clearances) are the plan's job, tested in
test_dressing.py; nothing here re-checks or moves a record.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _dressing_kit as kit  # noqa: E402
import _geo as geo  # noqa: E402
import _materials as m  # noqa: E402

HERO_BLOCK = "blk_R3363343"      # _dressing.hero_block_id on the committed data (test_dressing's GOLDEN)
DRESS_DIR = "/Game/City/EastVillage/Dressing"
HASH_TAG = "CityHash"
KIT_VERSION = 2                  # bump when a kit mesh recipe changes
M_DRESS_GLOW = m.MATERIALS_PATH + "/M_DressGlow"
GLOW_PREFIX = m.MATERIALS_PATH + "/MI_DressGlow_"
LIGHT_PREFIX = "City_DressLight_"
SHOT_PREFIX = "City_DressShot_"
TAGS = ["City", "CityDressing"]

# Glow_<key>: (colour, intensity before m.EMISSIVE_INTENSITY_FACTOR). The lamp heads are 2 and clip to white above
# it; a bulb is a few pixels across the street, so it runs hotter and blooms.
GLOWS = {
    "Red": ((1.0, 0.06, 0.03), 8.0),
    "Warm": ((1.0, 0.62, 0.28), 8.0),
    "Green": ((0.08, 1.0, 0.15), 8.0),
    "Blue": ((0.12, 0.25, 1.0), 10.0),
    "Flame": ((1.0, 0.55, 0.18), 6.0),
    # Near the painted windows (M_Facade's WindowGlow 0.04 times a 0.6..1 jitter) and a shade warmer: at 0.06 to
    # 0.16 the cards read as blank cream paper next to them.
    "WindowDim": ((1.0, 0.45, 0.16), 0.035),
    "WindowWarm": ((1.0, 0.52, 0.20), 0.05),
    "WindowBright": ((1.0, 0.60, 0.28), 0.07),
}
# MI_Prop_Dress<key>: (colour, roughness) under M_Prop's snow layer.
COLOURS = {
    "Wire": ((0.01, 0.01, 0.012), 0.5),
    "Evergreen": ((0.03, 0.12, 0.045), 0.85),
    "BowRed": ((0.65, 0.03, 0.04), 0.45),
    "CandleWax": ((0.75, 0.70, 0.60), 0.6),
    "Snow": (m.SNOW_COLOR, m.SNOW_ROUGHNESS),
    "SnowDirty": ((0.42, 0.40, 0.37), 0.8),
    "Lumber": ((0.25, 0.15, 0.08), 0.9),
    "TreeNetted": ((0.12, 0.16, 0.12), 0.9),
    "ConEdOrange": ((0.60, 0.15, 0.02), 0.5),
    "ConEdWhite": ((0.75, 0.75, 0.72), 0.5),
    "AwningPurple": ((0.18, 0.04, 0.25), 0.7),
    "AwningCream": ((0.70, 0.62, 0.45), 0.7),
    "AwningGrey": ((0.25, 0.25, 0.26), 0.7),
    "AwningBlack": ((0.02, 0.02, 0.02), 0.7),
    "SignBlank": ((0.03, 0.025, 0.035), 0.5),
}
ENGINE_MESHES = {"cube": "/Engine/BasicShapes/Cube", "cylinder": "/Engine/BasicShapes/Cylinder",
                 "sphere": "/Engine/BasicShapes/Sphere"}

GS_PRIM = unreal.GeometryScript_Primitives
CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER
BASE = unreal.GeometryScriptPrimitiveOriginMode.BASE


def plan_doc():
    """The committed plan for the hero block, or None (logged) when the file is missing."""
    path = kit.plan_path(HERO_BLOCK)
    if not os.path.exists(path):
        c.log("FAILED", path, "no dressing plan; run python -B Tools/Editor/write_dressing_plan.py")
        return None
    return kit.load_plan(path)


# --------------------------------------------------------------------------------------------------------------------
# meshes
# --------------------------------------------------------------------------------------------------------------------


def _opts():
    return unreal.GeometryScriptPrimitiveOptions()


def _build_bulb():
    mesh = unreal.DynamicMesh()
    GS_PRIM.append_sphere_lat_long(mesh, _opts(), unreal.Transform(), 50.0, 5, 8, CENTER)
    return mesh


def _build_wreath():
    """A ring round +Z: 37.5 cm to the tube's middle, the tube 12.5 cm thick in the plane and stretched to fill
    100 cm in Z, so the record's 10 cm Z scale makes it a fat, flat wreath."""
    mesh = unreal.DynamicMesh()
    GS_PRIM.append_torus(mesh, _opts(), unreal.Transform(scale=unreal.Vector(1.0, 1.0, 4.0)),
                         unreal.GeometryScriptRevolveOptions(), 37.5, 12.5, 20, 6, CENTER)
    return mesh


def _build_tree():
    """Three stacked cones on a stub trunk, filling a 100 cm box centred on the origin."""
    mesh = unreal.DynamicMesh()
    GS_PRIM.append_cylinder(mesh, _opts(), unreal.Transform(location=unreal.Vector(0.0, 0.0, -50.0)),
                            7.0, 14.0, 8, 0, True, BASE)
    # Steep enough (side normals under M_Prop's snow threshold) that the snow only caps them.
    for z0, radius, height in ((-40.0, 46.0, 56.0), (-12.0, 37.0, 46.0), (12.0, 27.0, 38.0)):
        GS_PRIM.append_cone(mesh, _opts(), unreal.Transform(location=unreal.Vector(0.0, 0.0, z0)),
                            radius, 1.0, height, 10, 0, True, BASE)
    return mesh


def _build_roof_snow(outlines):
    """Every roof outline extruded up by its thickness from its own height, in world cm."""
    mesh = unreal.DynamicMesh()
    for points, thickness in outlines:
        piece = unreal.DynamicMesh()
        GS_PRIM.append_simple_extrude_polygon(
            piece, _opts(), unreal.Transform(location=unreal.Vector(0.0, 0.0, float(points[0][2]))),
            [unreal.Vector2D(float(p[0]), float(p[1])) for p in points], float(thickness), 0, True, BASE)
        if unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(piece) == 0:
            continue
        res = unreal.GeometryScript_MeshQueries.get_mesh_volume_area(piece)
        if (float(res[1]) if isinstance(res, tuple) else float(res)) < 0.0:
            unreal.GeometryScript_Normals.flip_normals(piece)
        unreal.GeometryScript_MeshEdits.append_mesh(mesh, piece, unreal.Transform())
    unreal.GeometryScript_Normals.set_per_face_normals(mesh)
    return mesh


def _write_mesh(name, build, spec, collision):
    full = c.asset_path(DRESS_DIR, name)
    existing = c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, HASH_TAG) == spec:
        return existing
    mesh = build()
    if existing is None:
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", True)
        options.set_editor_property("enable_nanite", False)
        options.set_editor_property("enable_collision", collision)
        if collision:
            options.set_editor_property("collision_mode", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, full, options)
        static_mesh = result[0] if isinstance(result, (list, tuple)) else result
        if static_mesh is None:
            c.log("FAILED", full, "create_new_static_mesh_asset_from_mesh returned None")
            return None
        action = "created"
    else:
        static_mesh = existing
        options = unreal.GeometryScriptCopyMeshToAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", True)
        unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(mesh, static_mesh, options,
                                                                 unreal.GeometryScriptMeshWriteLOD())
        action = "updated"
    unreal.EditorAssetLibrary.set_metadata_tag(static_mesh, HASH_TAG, spec)
    c.save(static_mesh)
    c.log(action, full)
    return static_mesh


def ensure_meshes(doc):
    """{mesh key: StaticMesh} for every key _dressing_kit.GROUPS uses."""
    c.ensure_directory(DRESS_DIR)
    out = {key: c.load_or_none(path) for key, path in ENGINE_MESHES.items()}
    for key, name, build, collision in (("bulb", "SM_Dress_Bulb", _build_bulb, False),
                                        ("wreath", "SM_Dress_Wreath", _build_wreath, False),
                                        ("tree", "SM_Dress_Tree", _build_tree, True)):
        spec = geo.record_hash(KIT_VERSION, "dress-kit", name, collision)
        try:
            out[key] = _write_mesh(name, build, spec, collision)
        except Exception as exc:  # noqa: BLE001
            c.log_error("dressing mesh " + name, exc)
            out[key] = None
    outlines = kit.roof_snow_outlines(doc)
    if outlines:
        name = kit.roof_snow_mesh_name(doc)
        spec = geo.record_hash(KIT_VERSION, "dress-roof-snow", [[list(p) for p in pts] + [t] for pts, t in outlines])
        try:
            out["roof_snow"] = _write_mesh(name, lambda: _build_roof_snow(outlines), spec, False)
        except Exception as exc:  # noqa: BLE001
            c.log_error("dressing mesh " + name, exc)
            out["roof_snow"] = None
    return out


# --------------------------------------------------------------------------------------------------------------------
# materials
# --------------------------------------------------------------------------------------------------------------------


def ensure_materials():
    """{material key: MaterialInterface} for every key _dressing_kit.GROUPS uses."""
    glow = m.ensure_material(M_DRESS_GLOW, m._build_emissive, instanced=True)
    # The window cards draw M_Facade's window with its room behind the glass, always lit, in the card's warmth.
    window = m.ensure_look_material(m.M_WINDOW_LIT, m._build_window_lit, instanced=True)
    out = {}
    for key, (rgb, strength) in sorted(GLOWS.items()):
        out["Glow_" + key] = m.ensure_material_instance(
            GLOW_PREFIX + key, window if key.startswith("Window") else glow, vectors=[(m.EMISSIVE_COLOR_PARAM, rgb)],
            scalars=[(m.EMISSIVE_INTENSITY_PARAM, strength * m.EMISSIVE_INTENSITY_FACTOR)])
    for key, (rgb, rough) in sorted(COLOURS.items()):
        out[key] = m.ensure_prop_instance("Dress" + key, rgb, rough)
    return out


# --------------------------------------------------------------------------------------------------------------------
# the clutter groups, lights and cameras
# --------------------------------------------------------------------------------------------------------------------


def _transform(inst):
    x, y, z, pitch, yaw, roll, sx, sy, sz = inst
    return unreal.Transform(unreal.Vector(x, y, z), unreal.Rotator(roll, pitch, yaw), unreal.Vector(sx, sy, sz))


def clutter_groups(doc, meshes, materials):
    """[(kind, mesh, material, collision, shadow, [Transform])] in generate_city's clutter group shape, one per
    non-empty _dressing_kit group, kind = the group name (Dress_...)."""
    groups = []
    for key, instances in kit.instance_groups(doc).items():
        mesh_key, mat_key, collision, shadow = kit.GROUPS[key]
        groups.append((key, meshes.get(mesh_key), materials.get(mat_key), collision, shadow,
                       [_transform(i) for i in instances]))
    return groups


def dressing_groups():
    """(groups, doc): the hero block's clutter groups, building the kit and materials as needed. ([], None) with
    no plan."""
    doc = plan_doc()
    if doc is None:
        return [], None
    meshes = ensure_meshes(doc)
    materials = ensure_materials()
    groups = clutter_groups(doc, meshes, materials)
    missing = sorted(g[0] for g in groups if g[1] is None or g[2] is None)
    if missing:
        c.log("FAILED", DRESS_DIR, "no mesh or material for " + ", ".join(missing))
    return groups, doc


def loaded_groups(doc):
    """The same groups from the meshes and materials already on disk (loads only; verify_city)."""
    meshes = {key: c.load_or_none(path) for key, path in ENGINE_MESHES.items()}
    for key, name in (("bulb", "SM_Dress_Bulb"), ("wreath", "SM_Dress_Wreath"), ("tree", "SM_Dress_Tree"),
                      ("roof_snow", kit.roof_snow_mesh_name(doc))):
        meshes[key] = c.load_or_none(c.asset_path(DRESS_DIR, name))
    materials = {"Glow_" + key: c.load_or_none(GLOW_PREFIX + key) for key in GLOWS}
    materials.update({key: c.load_or_none(m.MATERIALS_PATH + "/MI_Prop_Dress" + key) for key in COLOURS})
    return clutter_groups(doc, meshes, materials)


def _remove_stale(existing, prefix, wanted):
    removed = 0
    for label, actor in list(existing.items()):
        if label.startswith(prefix) and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            removed += 1
    return removed


def ensure_lights(existing, doc):
    """The dressing's real lights: a few movable point lights from _dressing_kit.light_spots. Returns changes."""
    import generate_city as gen  # noqa: PLC0415
    spots = kit.light_spots(doc) if doc is not None else []
    changes = 0
    wanted = set()
    for name, (x, y, z), rgb, lumens, radius in spots:
        label = LIGHT_PREFIX + name
        wanted.add(label)
        loc = unreal.Vector(x, y, z)
        actor = existing.get(label)
        if actor is not None and not isinstance(actor, unreal.PointLight):
            actor.destroy_actor()
            actor = None
        if actor is None:
            actor = c.spawn_actor(unreal.PointLight, loc, unreal.Rotator(0.0, 0.0, 0.0), label=label)
            if actor is None:
                c.log("FAILED", label, "spawn_actor returned None")
                continue
            existing[label] = actor
            changes += 1
        if not gen.same_vector(actor.get_actor_location(), loc, 0.5):
            actor.set_actor_location(loc, False, True)
            changes += 1
        comp = actor.get_editor_property("point_light_component")
        changes += gen.set_if_different(comp, "mobility", unreal.ComponentMobility.MOVABLE, label)
        changes += gen.set_if_different(comp, "intensity_units", unreal.LightUnits.LUMENS, label)
        changes += gen.set_if_different(comp, "intensity", lumens, label, 0.5)
        changes += gen.set_if_different(comp, "attenuation_radius", radius, label, 0.5)
        changes += gen.set_if_different(comp, "cast_shadows", False, label)
        changes += gen.set_if_different(comp, "use_temperature", False, label)
        want = unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255)
        have = comp.get_editor_property("light_color")
        if (have.r, have.g, have.b) != (want.r, want.g, want.b):
            comp.set_editor_property("light_color", want)
            changes += 1
        changes += gen._ensure_tags(actor, TAGS + ["CityDressingLight"])
    changes += _remove_stale(existing, LIGHT_PREFIX, wanted)
    c.log("updated" if changes else "exists", LIGHT_PREFIX + "*", "{0} light(s)".format(len(spots)))
    return changes


def ensure_cameras(existing, doc):
    """City_DressShot_<name> CameraActors at _dressing_kit.shot_cameras. Returns changes."""
    import generate_city as gen  # noqa: PLC0415
    shots = kit.shot_cameras(doc) if doc is not None else []
    changes = 0
    wanted = set()
    for name, (x, y, z), pitch, yaw in shots:
        label = SHOT_PREFIX + name
        wanted.add(label)
        loc = unreal.Vector(x, y, z)
        rot = unreal.Rotator(0.0, pitch, yaw)
        actor = existing.get(label)
        if actor is not None and not isinstance(actor, unreal.CameraActor):
            actor.destroy_actor()
            actor = None
        if actor is None:
            actor = c.spawn_actor(unreal.CameraActor, loc, rot, label=label)
            if actor is None:
                c.log("FAILED", label, "spawn_actor returned None")
                continue
            existing[label] = actor
            changes += 1
        if not gen.same_vector(actor.get_actor_location(), loc, 0.5):
            actor.set_actor_location(loc, False, True)
            changes += 1
        have = actor.get_actor_rotation()
        if abs(have.pitch - pitch) > 0.05 or abs(((have.yaw - yaw) + 180.0) % 360.0 - 180.0) > 0.05:
            actor.set_actor_rotation(rot, False)
            changes += 1
        changes += gen._ensure_tags(actor, TAGS + ["DressShot", "shot:" + name])
    changes += _remove_stale(existing, SHOT_PREFIX, wanted)
    c.log("updated" if changes else "exists", SHOT_PREFIX + "*", "{0} camera(s)".format(len(shots)))
    return changes
