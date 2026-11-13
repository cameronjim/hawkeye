"""The editor half of _street_look: meshes, materials and actors for the hero block's relief and street dressing.

``ensure(district, existing, start, records, avoid)`` builds one static mesh per material key (SM_Street_<key>, in
world cm, boxes from _street_look), one StaticMeshActor per mesh (City_Street_<key>), the tree mesh and a
City_Tree_<n> per tree spot. Everything is NoCollision (render only) and carries a CityHash recipe tag, so a rerun
on the same plan saves nothing.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _geo as geo  # noqa: E402
import _materials as m  # noqa: E402
import _street_look as sl  # noqa: E402

STREET_DIR = "/Game/City/EastVillage/Street"
HASH_TAG = "CityHash"
PREFIX = "City_Street_"
TREE_PREFIX = "City_Tree_"
TREE_MESH = "SM_Street_Tree"
TREE_VERSION = 1
TAGS = ["City", "CityStreetLook"]

GS_PRIM = unreal.GeometryScript_Primitives


def _opts():
    return unreal.GeometryScriptPrimitiveOptions()


def _box_mesh(boxes):
    mesh = unreal.DynamicMesh()
    for cx, cy, z0, along, depth, height, yaw, tilt in boxes:
        xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(tilt, 0.0, yaw))
        GS_PRIM.append_box(mesh, _opts(), xf, along, depth, height, 0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    return mesh


def _branch(mesh, base, direction, length, radius):
    d = unreal.Vector(direction[0], direction[1], direction[2])
    rot = unreal.MathLibrary.make_rot_from_z(d)
    GS_PRIM.append_cylinder(mesh, _opts(), unreal.Transform(location=unreal.Vector(*base), rotation=rot), radius, length,
                            6, 0, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    return (base[0] + direction[0] * length, base[1] + direction[1] * length, base[2] + direction[2] * length)


def _dir(yaw_deg, tilt_deg):
    y, t = math.radians(yaw_deg), math.radians(tilt_deg)
    return (math.cos(y) * math.sin(t), math.sin(y) * math.sin(t), math.cos(t))


def _build_tree():
    """A bare street tree (a young London plane or honey locust in winter): a 2.6 m trunk, four limbs, three
    branches on each, two twigs on each of those; about 9 m tall at scale 1."""
    mesh = unreal.DynamicMesh()
    top = _branch(mesh, (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), 270.0, 13.0)
    for i in range(4):
        yaw = i * 90.0 + 20.0 * math.sin(i * 1.7)
        end1 = _branch(mesh, top, _dir(yaw, 32.0 + 6.0 * math.sin(i)), 240.0, 7.0)
        for j in range(3):
            yaw2 = yaw + (j - 1) * 50.0 + 10.0 * math.sin(i + j)
            end2 = _branch(mesh, end1, _dir(yaw2, 48.0 - 8.0 * j), 160.0, 3.6)
            for k in range(2):
                _branch(mesh, end2, _dir(yaw2 + (k - 0.5) * 70.0, 55.0), 95.0, 1.8)
    return mesh


def _write_mesh(name, build, spec):
    full = c.asset_path(STREET_DIR, name)
    existing = c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, HASH_TAG) == spec:
        return existing, False
    mesh = build()
    unreal.GeometryScript_Normals.set_per_face_normals(mesh)
    if existing is None:
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", True)
        options.set_editor_property("enable_nanite", False)
        options.set_editor_property("enable_collision", False)
        result = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, full, options)
        static_mesh = result[0] if isinstance(result, (list, tuple)) else result
        if static_mesh is None:
            c.log("FAILED", full, "create_new_static_mesh_asset_from_mesh returned None")
            return None, False
    else:
        static_mesh = existing
        options = unreal.GeometryScriptCopyMeshToAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", True)
        unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(mesh, static_mesh, options,
                                                                 unreal.GeometryScriptMeshWriteLOD())
    unreal.EditorAssetLibrary.set_metadata_tag(static_mesh, HASH_TAG, spec)
    c.save(static_mesh)
    c.log("updated" if existing is not None else "created", full)
    return static_mesh, True


def _ensure_actor(existing, label, mesh, material, loc, yaw, scale):
    changes = 0
    actor = existing.get(label)
    if actor is not None and not isinstance(actor, unreal.StaticMeshActor):
        actor.destroy_actor()
        actor = None
    if actor is None:
        actor = c.spawn_actor(unreal.StaticMeshActor, loc, label=label)
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
    if material is not None and comp.get_material(0) != material:
        comp.set_material(0, material)
        changes += 1
    loc0 = actor.get_actor_location()
    if abs(loc0.x - loc.x) > 0.05 or abs(loc0.y - loc.y) > 0.05 or abs(loc0.z - loc.z) > 0.05:
        actor.set_actor_location(loc, False, True)
        changes += 1
    if abs(((actor.get_actor_rotation().yaw - yaw) + 180.0) % 360.0 - 180.0) > 0.01:
        actor.set_actor_rotation(unreal.Rotator(0.0, 0.0, yaw), False)
        changes += 1
    s = actor.get_actor_scale3d()
    if abs(s.x - scale) > 1e-4 or abs(s.z - scale) > 1e-4:
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        changes += 1
    if str(comp.get_collision_profile_name()) != "NoCollision":
        comp.set_collision_profile_name("NoCollision")
        changes += 1
    have = [str(t) for t in actor.get_editor_property("tags")]
    if have != TAGS:
        actor.set_editor_property("tags", [unreal.Name(t) for t in TAGS])
        changes += 1
    return changes


SHOP_LIGHT_PREFIX = "City_ShopLight_"
SHOP_LIGHT_LUMENS = 700.0
SHOP_LIGHT_RADIUS = 800.0
SHOP_LIGHT_PITCH = -50.0        # degrees: out of the glass and down onto the sidewalk
SHOP_LIGHT_CONE = (35.0, 72.0)
SHOP_WARM = (1.0, 0.78, 0.55)
SHOP_COOL = (0.85, 0.92, 1.0)


def _set(obj, prop, value, label, tol=1e-3):
    have = obj.get_editor_property(prop)
    if isinstance(value, float) and isinstance(have, float):
        if abs(have - value) <= tol:
            return 0
    elif have == value:
        return 0
    obj.set_editor_property(prop, value)
    return 1


def _ensure_shop_light(existing, label, loc, yaw, warm):
    """The light an open shop spills on the sidewalk at night: a spot just outside the glass, angled down and out, no
    shadows, tagged CityLamp so the time of day switches it with the street lamps (and the EMP with them)."""
    changes = 0
    rot = unreal.Rotator(0.0, SHOP_LIGHT_PITCH, yaw)
    actor = existing.get(label)
    if actor is not None and not isinstance(actor, unreal.SpotLight):
        actor.destroy_actor()
        actor = None
    if actor is None:
        actor = c.spawn_actor(unreal.SpotLight, loc, rot, label=label)
        if actor is None:
            c.log("FAILED", label, "spawn_actor returned None")
            return 0
        existing[label] = actor
        changes += 1
    have = actor.get_actor_location()
    if abs(have.x - loc.x) > 0.5 or abs(have.y - loc.y) > 0.5 or abs(have.z - loc.z) > 0.5:
        actor.set_actor_location(loc, False, True)
        changes += 1
    r = actor.get_actor_rotation()
    if abs(r.pitch - rot.pitch) > 0.05 or abs(((r.yaw - yaw) + 180.0) % 360.0 - 180.0) > 0.05:
        actor.set_actor_rotation(rot, False)
        changes += 1
    comp = actor.get_editor_property("spot_light_component")
    changes += _set(comp, "mobility", unreal.ComponentMobility.MOVABLE, label)
    changes += _set(comp, "intensity_units", unreal.LightUnits.LUMENS, label)
    changes += _set(comp, "intensity", SHOP_LIGHT_LUMENS, label, 0.5)
    changes += _set(comp, "attenuation_radius", SHOP_LIGHT_RADIUS, label, 0.5)
    changes += _set(comp, "inner_cone_angle", SHOP_LIGHT_CONE[0], label, 0.01)
    changes += _set(comp, "outer_cone_angle", SHOP_LIGHT_CONE[1], label, 0.01)
    changes += _set(comp, "cast_shadows", False, label)
    changes += _set(comp, "use_temperature", False, label)
    rgb = SHOP_WARM if warm else SHOP_COOL
    want = unreal.Color(r=int(rgb[0] * 255), g=int(rgb[1] * 255), b=int(rgb[2] * 255), a=255)
    have_c = comp.get_editor_property("light_color")
    if (have_c.r, have_c.g, have_c.b) != (want.r, want.g, want.b):
        comp.set_editor_property("light_color", want)
        changes += 1
    tags = ["City", "CityLamp", "CityShopLight"]
    if [str(t) for t in actor.get_editor_property("tags")] != tags:
        actor.set_editor_property("tags", [unreal.Name(t) for t in tags])
        changes += 1
    return changes


def ensure(district, existing, start, records, avoid):
    """Build the hero block's relief and street dressing. Returns the number of changes."""
    c.ensure_directory(STREET_DIR)
    street_trees = sl.tree_spots(district, start, avoid)
    plan = sl.merge(sl.relief(district, start, records), sl.crosswalks(district, start), sl.tree_pits(street_trees),
                    sl.news_boxes(street_trees))
    trees = street_trees + sl.park_tree_spots(district, start, avoid)
    materials = {key: m.ensure_prop_instance("Street" + key, rgb, rough) for key, (rgb, rough) in sorted(sl.COLOURS.items())}
    materials[sl.PAINT] = m.ensure_street_look()["paint"]
    changes = 0
    wanted = set()
    for key in sorted(plan):
        boxes = plan[key]
        name = "SM_Street_" + key
        spec = geo.record_hash(sl.PLAN_VERSION, "street", key, boxes)
        mesh, built = _write_mesh(name, lambda boxes=boxes: _box_mesh(boxes), spec)
        changes += int(built)
        if mesh is None:
            continue
        label = PREFIX + key
        wanted.add(label)
        changes += _ensure_actor(existing, label, mesh, materials.get(key), unreal.Vector(0.0, 0.0, 0.0), 0.0, 1.0)
    tree, built = _write_mesh(TREE_MESH, _build_tree, geo.record_hash(TREE_VERSION, "tree"))
    changes += int(built)
    for i, (x, y, yaw, scale) in enumerate(trees):
        label = "{0}{1}".format(TREE_PREFIX, i)
        wanted.add(label)
        changes += _ensure_actor(existing, label, tree, materials["TreeBark"], unreal.Vector(x, y, sl.SIDEWALK_TOP), yaw,
                                 scale)
    import _look
    lights = sl.shop_lights(district, start, records, _look.SHOPS)
    for i, (x, y, z, yaw, warm) in enumerate(lights):
        label = "{0}{1}".format(SHOP_LIGHT_PREFIX, i)
        wanted.add(label)
        changes += _ensure_shop_light(existing, label, unreal.Vector(x, y, z), yaw, warm)
    for label, actor in list(existing.items()):
        if (label.startswith(PREFIX) or label.startswith(TREE_PREFIX) or label.startswith(SHOP_LIGHT_PREFIX))                 and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            changes += 1
    c.log("updated" if changes else "exists", "street look",
          "{0} relief and dressing meshes ({1} boxes), {2} trees, {3} shop lights".format(
              len(plan), sum(len(v) for v in plan.values()), len(trees), len(lights)))
    return changes
