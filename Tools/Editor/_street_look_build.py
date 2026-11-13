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
TREE_VERSION = 4
TAGS = ["City", "CityStreetLook"]
OWNED_PREFIXES = (PREFIX, TREE_PREFIX, "City_ShopLight_", "City_Car_", "City_Manhole_", "City_Steam_", "City_Sign_")

GS_PRIM = unreal.GeometryScript_Primitives


def _opts():
    return unreal.GeometryScriptPrimitiveOptions()


def _box_mesh(boxes):
    mesh = unreal.DynamicMesh()
    for cx, cy, z0, along, depth, height, yaw, tilt in boxes:
        xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(tilt, 0.0, yaw))
        GS_PRIM.append_box(mesh, _opts(), xf, along, depth, height, 0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    return mesh


def _write_mesh(name, build, spec, smooth=False):
    full = c.asset_path(STREET_DIR, name)
    existing = c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, HASH_TAG) == spec:
        return existing, False
    mesh = build()
    if smooth:
        unreal.GeometryScript_Normals.set_per_vertex_normals(mesh)
    else:
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


def _ensure_actor(existing, label, mesh, material, loc, yaw, scale, shadow=True):
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
    if bool(comp.get_editor_property("cast_shadow")) != shadow:
        comp.set_editor_property("cast_shadow", shadow)
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
SHOP_LIGHT_LUMENS = 240.0
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


def ensure(district, existing, start, records, avoid, keep=(), hydrants=()):
    """Build the hero block's relief and street dressing. ``keep`` [(x, y, radius)] is what parked cars keep clear of,
    ``hydrants`` [(x, y)]. Returns the number of changes."""
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
    bark = m.ensure_street_look()["bark"]
    variants = []
    for v in range(STREET_TREES + PARK_ELMS):
        elm = v >= STREET_TREES
        mesh, built = _write_mesh("{0}_{1}".format(TREE_MESH, v), lambda v=v, elm=elm: _build_tree3(1000 + v * 77, elm),
                                  geo.record_hash(TREE_VERSION, "tree3", v, elm), smooth=True)
        changes += int(built)
        variants.append(mesh)
    for i, (x, y, yaw, scale) in enumerate(trees):
        label = "{0}{1}".format(TREE_PREFIX, i)
        wanted.add(label)
        park = i >= len(street_trees)
        pick = STREET_TREES + i % PARK_ELMS if park else i % STREET_TREES
        changes += _ensure_actor(existing, label, variants[pick], bark, unreal.Vector(x, y, sl.SIDEWALK_TOP), yaw,
                                 scale * (0.8 if park else 1.0))
    changes += ensure_look3(district, existing, start, list(keep), street_trees, list(hydrants), avoid, wanted)
    import _look
    lights = sl.shop_lights(district, start, records, _look.SHOPS)
    for i, (x, y, z, yaw, warm) in enumerate(lights):
        label = "{0}{1}".format(SHOP_LIGHT_PREFIX, i)
        wanted.add(label)
        changes += _ensure_shop_light(existing, label, unreal.Vector(x, y, z), yaw, warm)
    for label, actor in list(existing.items()):
        if label.startswith(OWNED_PREFIXES) and label not in wanted:
            actor.destroy_actor()
            existing.pop(label, None)
            changes += 1
    old_tree = c.asset_path(STREET_DIR, TREE_MESH)
    if c.load_or_none(old_tree) is not None:
        # pass 2's stick tree, replaced by the variants: nothing points at it once the actors above are updated
        unreal.EditorAssetLibrary.delete_asset(old_tree)
        c.log("deleted", old_tree)
        changes += 1
    c.log("updated" if changes else "exists", "street look",
          "{0} relief and dressing meshes ({1} boxes), {2} trees, {3} shop lights".format(
              len(plan), sum(len(v) for v in plan.values()), len(trees), len(lights)))
    return changes


# ----------------------------------------------------------------------------------------------------------------------
# look pass 3: car bodies, trees, manholes, steam, snow piles, signs, mailboxes
# ----------------------------------------------------------------------------------------------------------------------

GS_QUERY = unreal.GeometryScript_MeshQueries
BASE = unreal.GeometryScriptPrimitiveOriginMode.BASE
CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER
LOOK3_VERSION = 2

# Side profiles (x forward, z up, cm) of the lower body (full width) and the glasshouse (narrower), per body shape.
CAR_PROFILES = {
    "Sedan": ([(-235, 30), (235, 30), (238, 55), (229, 76), (100, 90), (-150, 92), (-229, 88), (-238, 58)],
              [(98, 90), (28, 138), (-82, 140), (-152, 92)], 26.0),
    "SUV": ([(-242, 36), (242, 36), (245, 72), (236, 100), (128, 110), (-238, 110), (-245, 72)],
            [(126, 110), (72, 173), (-218, 175), (-236, 112)], 22.0),
    "Van": ([(-260, 36), (260, 36), (262, 78), (252, 106), (172, 120), (-259, 120), (-262, 78)],
            [(170, 120), (118, 202), (-252, 204), (-259, 122)], 14.0),
}


def _first(result):
    return result[0] if isinstance(result, (list, tuple)) else result


def _orient(mesh):
    res = GS_QUERY.get_mesh_volume_area(mesh)
    volume = float(res[1]) if isinstance(res, (list, tuple)) else float(res)
    if volume < 0.0:
        unreal.GeometryScript_Normals.flip_normals(mesh)


def _extrude_side(profile, width):
    """The profile (x, z) extruded ``width`` across, centred on y = 0."""
    for roll in (90.0, -90.0):
        piece = unreal.DynamicMesh()
        GS_PRIM.append_simple_extrude_polygon(piece, _opts(), unreal.Transform(rotation=unreal.Rotator(roll, 0.0, 0.0)),
                                              [unreal.Vector2D(float(x), float(z)) for x, z in profile], width, 0, True, BASE)
        box = _first(GS_QUERY.get_mesh_bounding_box(piece))
        if box.min.z > -1.0:
            break
    unreal.GeometryScript_MeshTransforms.translate_mesh(piece, unreal.Vector(0.0, -(box.min.y + box.max.y) * 0.5, 0.0))
    _orient(piece)
    return piece


def _build_car_body(shape):
    lower, glass, inset = CAR_PROFILES[shape]
    length, width, base, radius = sl.CAR_SHAPES[shape][:4]
    mesh = unreal.DynamicMesh()
    for piece in (_extrude_side(lower, width), _extrude_side(glass, width - inset)):
        unreal.GeometryScript_MeshEdits.append_mesh(mesh, piece, unreal.Transform())
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            wheel = unreal.DynamicMesh()
            GS_PRIM.append_cylinder(wheel, _opts(), unreal.Transform(location=unreal.Vector(sx * base, sy * (width * 0.5 - 11.0), radius),
                                                                    rotation=unreal.Rotator(90.0, 0.0, 0.0)),
                                    radius, 22.0, 14, 0, True, CENTER)
            _orient(wheel)
            unreal.GeometryScript_MeshEdits.append_mesh(mesh, wheel, unreal.Transform())
    return mesh


def _cone(mesh, base, d, length, r0, r1, sides):
    """A tapered segment from ``base`` along ``d``; it starts a little behind its base and runs a little past its end,
    so a bend in the chain leaves no gap."""
    rot = unreal.MathLibrary.make_rot_from_z(unreal.Vector(d[0], d[1], d[2]))
    back = min(r0 * 0.8, length * 0.25)
    start = unreal.Vector(base[0] - d[0] * back, base[1] - d[1] * back, base[2] - d[2] * back)
    GS_PRIM.append_cone(mesh, _opts(), unreal.Transform(location=start, rotation=rot), r0, r1, length + back + r1 * 0.5,
                        sides, 1, False, BASE)
    return (base[0] + d[0] * length, base[1] + d[1] * length, base[2] + d[2] * length)


def _norm(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
    return (v[0] / n, v[1] / n, v[2] / n)


def _build_tree3(seed, elm):
    """A bare tree in five orders (trunk, limbs, branches, shoots, twigs), each a chain of tapered round segments that
    bends a little at every joint: limbs and a leader from the top of the trunk climbing toward the light, branches
    spread round their parent along its outer half, the outer orders arching out and down a little. A young street
    tree or, with ``elm``, a vase-shaped park elm. About 3 thousand triangles."""
    import random
    rnd = random.Random(seed)
    mesh = unreal.DynamicMesh()
    sides = (10, 7, 5, 4, 3)
    segs = (3, 3, 3, 2, 2)
    kids = (4, 4, 3, 3, 0)
    shrink = (0.0, 0.78, 0.72, 0.62, 0.55)

    def grow(base, d, length, radius, order, leader=False):
        points = []
        p, r = base, radius
        n = segs[order]
        for _i in range(n):
            bend = 0.12 if order == 0 else 0.26
            lift = 0.08 if order < 3 else -0.07
            d = _norm((d[0] + rnd.uniform(-1, 1) * bend, d[1] + rnd.uniform(-1, 1) * bend,
                       d[2] + rnd.uniform(-0.3, 0.3) * bend + lift))
            r1 = r * (0.86 if order == 0 else 0.74)
            q = _cone(mesh, p, d, length / n, r, max(r1, 0.3), sides[order])
            points.append((q, d, r1))
            p, r = q, r1
        if order + 1 >= len(sides):
            return
        count = kids[order] + (1 if order >= 2 and rnd.random() < 0.35 else 0)
        yaw0 = rnd.uniform(0.0, 360.0)
        for k in range(count):
            if order == 0:
                at = points[-1] if k < 2 else points[-2]
            else:
                at = points[-1] if k == 0 else points[rnd.randint(len(points) // 2, len(points) - 1)]
            q, pd, pr = at
            yaw = math.radians(yaw0 + k * 360.0 / count + rnd.uniform(-30.0, 30.0))
            if order == 0:
                tilt = rnd.uniform(18.0, 32.0) if elm else rnd.uniform(28.0, 48.0)
            else:
                tilt = rnd.uniform(30.0, 60.0)
            tilt = math.radians(tilt)
            side = (math.cos(yaw), math.sin(yaw), 0.0)
            cd = _norm((pd[0] * math.cos(tilt) + side[0] * math.sin(tilt), pd[1] * math.cos(tilt) + side[1] * math.sin(tilt),
                        pd[2] * math.cos(tilt) + (0.25 if order < 2 else 0.05)))
            grow(q, cd, length * shrink[order + 1] * rnd.uniform(0.85, 1.1), max(pr * 0.7, 0.35), order + 1)
        if order == 0 and not elm:
            # the leader: the trunk going on up, a limb's size
            q, pd, pr = points[-1]
            grow(q, _norm((pd[0], pd[1], pd[2] + 0.5)), length * 0.8, pr * 0.8, 1)

    if elm:
        grow((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), 340.0, 28.0, 0)
    else:
        grow((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), 270.0, 13.0, 0)
    return mesh


def _build_manhole():
    mesh = unreal.DynamicMesh()
    GS_PRIM.append_cylinder(mesh, _opts(), unreal.Transform(), 75.0, 1.0, 24, 0, True, BASE)
    return mesh


def _build_steam():
    mesh = unreal.DynamicMesh()
    GS_PRIM.append_cone(mesh, _opts(), unreal.Transform(), 30.0, 140.0, 320.0, 14, 4, False, BASE)
    GS_PRIM.append_cone(mesh, _opts(), unreal.Transform(rotation=unreal.Rotator(0.0, 0.0, 15.0)), 20.0, 95.0, 260.0, 10, 3,
                        False, BASE)
    return mesh


def _mound(seed):
    mesh = unreal.DynamicMesh()
    GS_PRIM.append_sphere_lat_long(mesh, _opts(), unreal.Transform(), 50.0, 10, 16, CENTER)
    options = unreal.GeometryScriptPerlinNoiseOptions()
    layer = unreal.GeometryScriptPerlinNoiseLayerOptions()
    layer.set_editor_property("magnitude", 11.0)
    layer.set_editor_property("frequency", 0.06)
    layer.set_editor_property("random_seed", seed)
    options.set_editor_property("base_layer", layer)
    unreal.GeometryScript_MeshDeformers.apply_perlin_noise_to_mesh2(mesh, unreal.GeometryScriptMeshSelection(), options)
    return mesh


def _build_snow_piles(piles):
    mesh = unreal.DynamicMesh()
    mounds = [_mound(s) for s in (3, 11, 29)]
    for i, (x, y, z, length, width, height, yaw) in enumerate(piles):
        xf = unreal.Transform(location=unreal.Vector(x, y, z), rotation=unreal.Rotator(0.0, 0.0, yaw),
                              scale=unreal.Vector(length / 100.0, width / 100.0, height / 50.0))
        unreal.GeometryScript_MeshEdits.append_mesh(mesh, mounds[i % len(mounds)], xf)
    return mesh


def _build_sign_poles(signs):
    mesh = unreal.DynamicMesh()
    for x, y, _yaw, kind in signs:
        GS_PRIM.append_cylinder(mesh, _opts(), unreal.Transform(location=unreal.Vector(x, y, sl.SIDEWALK_TOP)), 3.0,
                                330.0 if kind == "BusStop" else 290.0, 8, 0, True, BASE)
    return mesh


def _build_sign_plate():
    mesh = unreal.DynamicMesh()
    GS_PRIM.append_box(mesh, _opts(), unreal.Transform(location=unreal.Vector(0.0, 0.0, -30.0)), 2.0, 46.0, 60.0, 0, 0, 0, BASE)
    return mesh


def _build_mailboxes(boxes):
    mesh = unreal.DynamicMesh()
    for x, y, yaw in boxes:
        rot = unreal.Rotator(0.0, 0.0, yaw)
        _box_into(mesh, x, y, sl.SIDEWALK_TOP + 18.0, 52.0, 48.0, 92.0, rot)
        GS_PRIM.append_cylinder(mesh, _opts(), unreal.Transform(location=unreal.Vector(x, y, sl.SIDEWALK_TOP + 110.0),
                                                                rotation=unreal.Rotator(0.0, 90.0, yaw)),
                                24.0, 52.0, 12, 0, True, CENTER)
        for sx in (-1.0, 1.0):
            for sy in (-1.0, 1.0):
                lx, ly = sx * 20.0, sy * 18.0
                r = math.radians(yaw)
                _box_into(mesh, x + lx * math.cos(r) - ly * math.sin(r), y + lx * math.sin(r) + ly * math.cos(r),
                          sl.SIDEWALK_TOP, 5.0, 5.0, 18.0, rot)
    return mesh


def _box_into(mesh, x, y, z0, sx, sy, sz, rot):
    GS_PRIM.append_box(mesh, _opts(), unreal.Transform(location=unreal.Vector(x, y, z0), rotation=rot), sx, sy, sz, 0, 0, 0,
                       BASE)


STREET_TREES = 3        # mesh variants
PARK_ELMS = 2


def ensure_look3(district, existing, start, keep, trees, hydrants, avoid, wanted):
    """The third pass's dressing: parked cars on both kerbs (City_Car_<n>), manholes (City_Manhole_<n>) and steam
    (City_Steam_<n>), snow piles, sign poles and mailboxes (merged, City_Street_<key>), sign plates (City_Sign_<n>).
    All render only. Returns the number of changes; adds every label it keeps to ``wanted``."""
    changes = 0
    mats = m.ensure_street_look()
    cars = sl.parked_cars(district, start, keep)
    car_meshes = {}
    for shape in sorted(sl.CAR_SHAPES):
        mesh, built = _write_mesh("SM_Street_Car_" + shape, lambda shape=shape: _build_car_body(shape),
                                  geo.record_hash(LOOK3_VERSION, "car", shape, CAR_PROFILES[shape], sl.CAR_SHAPES[shape]))
        car_meshes[shape] = mesh
        changes += int(built)
    car_mats = {}
    for i, (x, y, yaw, shape, colour) in enumerate(cars):
        key = (shape, colour)
        if key not in car_mats:
            car_mats[key] = m.ensure_car_shape_instance(shape, colour, sl.CAR_COLOURS[colour], sl.CAR_SHAPES[shape])
        label = "City_Car_{0}".format(i)
        wanted.add(label)
        changes += _ensure_actor(existing, label, car_meshes[shape], car_mats[key], unreal.Vector(x, y, sl.ROAD_TOP), yaw, 1.0)
    holes = sl.manholes(district, start)
    hole_mesh, built = _write_mesh("SM_Street_Manhole", _build_manhole, geo.record_hash(LOOK3_VERSION, "manhole"))
    changes += int(built)
    steam_mesh, built = _write_mesh("SM_Street_Steam", _build_steam, geo.record_hash(LOOK3_VERSION, "steam"), smooth=True)
    changes += int(built)
    n_steam = 0
    for i, (x, y, steam) in enumerate(holes):
        label = "City_Manhole_{0}".format(i)
        wanted.add(label)
        changes += _ensure_actor(existing, label, hole_mesh, mats["manhole"], unreal.Vector(x, y, sl.ROAD_TOP + 0.3),
                                 (i * 37.0) % 360.0, 1.0)
        if steam:
            label = "City_Steam_{0}".format(n_steam)
            n_steam += 1
            wanted.add(label)
            changes += _ensure_actor(existing, label, steam_mesh, mats["steam"], unreal.Vector(x, y, sl.ROAD_TOP), 0.0, 1.0,
                                     shadow=False)
    piles = sl.snow_piles(district, start, trees, hydrants, cars)
    furniture = sl.street_furniture(district, start, avoid + [(c[0], c[1]) for c in cars] + [(t[0], t[1]) for t in trees])
    merged = [
        ("SnowPiles", lambda: _build_snow_piles(piles), piles, mats["snowpile"]),
        ("SignPoles", lambda: _build_sign_poles(furniture["sign"]), furniture["sign"],
         m.ensure_prop_instance("StreetSignPole", (0.12, 0.13, 0.12), 0.5)),
        ("Mailboxes", lambda: _build_mailboxes(furniture["mailbox"]), furniture["mailbox"],
         m.ensure_prop_instance("StreetMailbox", (0.02, 0.04, 0.16), 0.4)),
    ]
    for key, build, data, material in merged:
        if not data:
            continue
        mesh, built = _write_mesh("SM_Street_" + key, build, geo.record_hash(LOOK3_VERSION, key, data), smooth=key == "SnowPiles")
        changes += int(built)
        label = PREFIX + key
        wanted.add(label)
        changes += _ensure_actor(existing, label, mesh, material, unreal.Vector(0.0, 0.0, 0.0), 0.0, 1.0)
    plate, built = _write_mesh("SM_Street_SignPlate", _build_sign_plate, geo.record_hash(LOOK3_VERSION, "plate"))
    changes += int(built)
    for i, (x, y, yaw, kind) in enumerate(furniture["sign"]):
        label = "City_Sign_{0}".format(i)
        wanted.add(label)
        top = sl.SIDEWALK_TOP + (330.0 if kind == "BusStop" else 290.0)
        changes += _ensure_actor(existing, label, plate, mats["signs"][kind], unreal.Vector(x, y, top), yaw, 1.0)
    c.log("updated" if changes else "exists", "street look 3",
          "{0} parked cars, {1} manholes ({2} steaming), {3} snow piles, {4} signs, {5} mailboxes".format(
              len(cars), len(holes), n_steam, len(piles), len(furniture["sign"]), len(furniture["mailbox"])))
    return changes
