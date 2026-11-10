"""Create Act 1 chapter 2's set piece: the toy drive on Avenue B, its reindeer crew and the courier on the corner
(claude-docs/gameplay-semantics.md, "Chapter sites"; docs/chapters/CH02.md).

    /Game/Missions/Sites/DA_Site_CH02_ToyDrive  UChapterSiteDefinition: the drive planned by _site.plan_site from the
                                                district's OpenStreetMap records: two folding tables, boxes, a banner on
                                                two poles, string lights, a volunteer; five reindeer in the street; the
                                                arrival volume (reach_drive) that wakes them; three toy sacks (keycard
                                                pickups toy_sack_1..3) the table asks for (return_toys); the courier under
                                                a tarp with two police officers and a cruiser (examine_courier, optional)
    /Game/Blueprints/AI/BP_Thug_Reindeer        child of BP_Thug: a brown tracksuit with a cream stripe, a dark brown
                                                mask, and antlers, ears and a red nose on his head (AThugCharacter::Headwear)
    /Game/Blueprints/World/BP_Pickup_ToySack    child of APickupActor: a red sack tied with gold, a keycard pickup
    /Game/Kit/Site/SM_Kit_*                     the greybox meshes: ToyBox, DriveBanner, ToySack, ReindeerHead,
                                                Figure_Volunteer, Figure_Police, CourierTarp, Cruiser

The folding table, poles, wire and bulbs are the boss kit's (create_boss.py runs first). UChapterSiteSubsystem spawns
everything when DA_CH02_ReindeerGames starts; nothing is placed in the map.

Idempotent: meshes carry a recipe hash, the site asset a plan hash; each is rewritten only when it changes.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\create_site.py" -unattended -nullrhi -nosplash -nop4 -stdout
"""

import hashlib
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _materials as m  # noqa: E402
import _site  # noqa: E402

SITE_PATH = "/Game/Missions/Sites"
SITE_NAME = "DA_Site_CH02_ToyDrive"
KIT_PATH = "/Game/Kit/Site"
BOSS_KIT = "/Game/Kit/Boss"
AI_PATH = "/Game/Blueprints/AI"
WORLD_PATH = "/Game/Blueprints/World"
REINDEER_BP = "BP_Thug_Reindeer"
SACK_BP = "BP_Pickup_ToySack"
REINDEER_MATERIAL_PATH = "/Game/Characters/Reindeer"
M_REINDEER_SUIT = REINDEER_MATERIAL_PATH + "/M_ReindeerTracksuit"
M_REINDEER_TRIM = REINDEER_MATERIAL_PATH + "/M_ReindeerTrim"
REINDEER_MATERIAL_VERSION = "reindeer-1"
REINDEER_SUIT = (0.16, 0.075, 0.03)     # reindeer brown
REINDEER_STRIPE = (0.75, 0.68, 0.52)    # cream
REINDEER_MASK = (0.06, 0.03, 0.015)     # the mask: dark brown
HEADWEAR_OFFSET = (2.0, 0.0, 9.0)       # cm from his head bone, his frame (forward, right, up)
BUILD_TAG = "HawkeyeBuild"
MESH_VERSION = 1

# (weapon) -> class: every one of the crew is the reindeer thug; the weapon comes from the site.
SACK_KEYCARDS = ("toy_sack_1", "toy_sack_2", "toy_sack_3")
TABLE_PROMPT = "[Return the toys]"
# The examines' close-ups, in their actor's frame (cm): (camera, looks at, interact zone's half extent). The table's looks
# from the side and above, so Kate in front of it is in the shot and not in the way; the courier's from above his head.
TABLE_CLOSE_UP = ((150.0, -270.0, 200.0), (0.0, 0.0, 80.0), (260.0, 260.0, 160.0))
COURIER_CLOSE_UP = ((-200.0, 0.0, 280.0), (30.0, 0.0, 10.0), (220.0, 220.0, 150.0))
COURIER_PROMPT = "[Look]"
BULB_COLOURS = [("Warm", (1.0, 0.72, 0.42)), ("Red", (1.0, 0.25, 0.18)), ("Green", (0.35, 1.0, 0.4))]
BULB_LUMENS = 110.0
BULB_RADIUS = 600.0
CRUISER_LIGHTS = (((1.0, 0.1, 0.08), 90.0, 450.0), ((0.15, 0.3, 1.0), 90.0, 450.0))

COLOURS = {
    "Cardboard": ((0.32, 0.2, 0.1), 0.85),
    "Tape": ((0.75, 0.7, 0.55), 0.5),
    "BannerRed": ((0.5, 0.03, 0.04), 0.6),
    "BannerWhite": ((0.82, 0.82, 0.8), 0.6),
    "SackRed": ((0.55, 0.04, 0.05), 0.75),
    "SackTie": ((0.8, 0.62, 0.18), 0.35),
    "Antler": ((0.55, 0.42, 0.26), 0.7),
    "ReindeerFur": ((0.2, 0.1, 0.04), 0.9),
    "CoatRed": ((0.45, 0.04, 0.05), 0.7),
    "Skin": ((0.55, 0.36, 0.26), 0.6),
    "Jeans": ((0.08, 0.1, 0.18), 0.8),
    "PoliceNavy": ((0.025, 0.03, 0.07), 0.6),
    "PoliceCap": ((0.02, 0.02, 0.03), 0.4),
    "Tarp": ((0.32, 0.33, 0.3), 0.8),
    "CourierBag": ((0.7, 0.55, 0.05), 0.6),
    "CruiserWhite": ((0.75, 0.76, 0.78), 0.35),
    "CruiserNavy": ((0.03, 0.05, 0.15), 0.35),
    "Glass": ((0.02, 0.025, 0.03), 0.1),
}
GLOWS = {
    "MI_ReindeerNose": ((1.0, 0.05, 0.04), 0.6),
    "MI_LightBarRed": ((1.0, 0.05, 0.04), 1.2),
    "MI_LightBarBlue": ((0.1, 0.25, 1.0), 1.2),
}

GS_PRIM = unreal.GeometryScript_Primitives
BASE = unreal.GeometryScriptPrimitiveOriginMode.BASE
CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER


def _hash(value):
    return hashlib.md5(json.dumps(value, sort_keys=True, default=str).encode("utf-8")).hexdigest()[:12]


def _opts(slot):
    options = unreal.GeometryScriptPrimitiveOptions()
    options.set_editor_property("material_id", slot)
    return options


def _box(mesh, slot, cx, cy, z0, sx, sy, sz, yaw=0.0, pitch=0.0, roll=0.0):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(roll, pitch, yaw))
    GS_PRIM.append_box(mesh, _opts(slot), xf, sx, sy, sz, 0, 0, 0, BASE)


def _cyl(mesh, slot, cx, cy, z0, radius, height, steps=12, pitch=0.0, roll=0.0, yaw=0.0):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(roll, pitch, yaw))
    GS_PRIM.append_cylinder(mesh, _opts(slot), xf, radius, height, steps, 0, True, BASE)


def _sphere(mesh, slot, cx, cy, cz, radius, scale=(1.0, 1.0, 1.0), steps=12):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, cz), scale=unreal.Vector(*scale))
    GS_PRIM.append_sphere_lat_long(mesh, _opts(slot), xf, radius, steps, steps + 2, CENTER)


# --------------------------------------------------------------------------------------------------
# meshes (origin at the middle of the base, +X forward, unless said otherwise)
# --------------------------------------------------------------------------------------------------


def _build_box():
    """A 45 x 35 x 30 cm cardboard box, a tape strip over the top; slots cardboard, tape."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 0.0, 45.0, 35.0, 30.0)
    _box(mesh, 1, 0.0, 0.0, 29.5, 47.0, 6.0, 1.0)
    return mesh


def _build_banner():
    """A 320 x 70 cm banner facing +X, a white band across it; slots red, white."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 0.0, 3.0, 320.0, 70.0)
    _box(mesh, 1, 1.0, 0.0, 26.0, 3.0, 300.0, 16.0)
    return mesh


def _build_sack():
    """A stuffed sack 55 cm tall, gathered and tied at the top; slots sack, tie."""
    mesh = unreal.DynamicMesh()
    _sphere(mesh, 0, 0.0, 0.0, 22.0, 24.0, (1.0, 0.9, 0.95))
    _cyl(mesh, 0, 0.0, 0.0, 38.0, 9.0, 10.0)
    _cyl(mesh, 1, 0.0, 0.0, 44.0, 10.0, 3.0)
    _sphere(mesh, 0, 0.0, 0.0, 51.0, 8.0, (1.0, 1.0, 0.7))
    return mesh


def _build_reindeer_head():
    """Antlers, ears and a red nose round the middle of his head (+X his face, +Z up); slots antler, fur, nose."""
    mesh = unreal.DynamicMesh()
    for side in (-1.0, 1.0):
        # Each antler: a main beam up and out, two tines forward.
        _cyl(mesh, 0, -2.0, side * 6.0, 9.0, 1.4, 24.0, 8, roll=side * 25.0)
        _cyl(mesh, 0, -1.0, side * 11.0, 19.0, 1.1, 10.0, 8, pitch=-35.0, roll=side * 10.0)
        _cyl(mesh, 0, -2.0, side * 14.0, 26.0, 1.0, 9.0, 8, pitch=-30.0, roll=side * 35.0)
        # An ear, flat and pointed out.
        _sphere(mesh, 1, -1.0, side * 12.5, 4.0, 4.5, (0.5, 1.4, 0.8), steps=8)
    # A fur band round the brow, so the antlers read as worn.
    _cyl(mesh, 1, 0.0, 0.0, 5.0, 11.5, 3.5, 16)
    _sphere(mesh, 2, 12.5, 0.0, -2.0, 3.2, steps=10)
    return mesh


def _build_figure(police):
    """A greybox person 175 cm tall facing +X: slots coat, skin, legs, cap. The police wear a peaked cap."""
    mesh = unreal.DynamicMesh()
    for side in (-1.0, 1.0):
        _cyl(mesh, 2, 0.0, side * 10.0, 0.0, 7.5, 88.0)                          # legs
        _cyl(mesh, 0, 0.0, side * 25.0, 92.0, 5.5, 52.0, pitch=0.0, roll=side * 8.0)  # arms
    _cyl(mesh, 0, 0.0, 0.0, 85.0, 19.0, 62.0, 14)                                 # body
    _sphere(mesh, 1, 0.0, 0.0, 160.0, 11.0, (1.0, 0.9, 1.1))                       # head
    if police:
        _cyl(mesh, 3, 0.0, 0.0, 167.0, 11.5, 7.0, 14)
        _box(mesh, 3, 9.0, 0.0, 167.0, 10.0, 18.0, 1.5)
    else:
        _cyl(mesh, 0, 0.0, 0.0, 168.0, 10.5, 4.0, 14)                              # a red bobble hat
        _sphere(mesh, 3, 0.0, 0.0, 178.0, 5.0, steps=8)
    return mesh


def _build_tarp():
    """A tarp over someone on the pavement, 190 x 70 cm, and a courier's bag beside it; slots tarp, bag."""
    mesh = unreal.DynamicMesh()
    _sphere(mesh, 0, 0.0, 0.0, 8.0, 50.0, (1.9, 0.62, 0.3))
    _box(mesh, 0, 0.0, 0.0, 0.0, 195.0, 75.0, 3.0)
    _box(mesh, 1, 30.0, 70.0, 0.0, 40.0, 30.0, 32.0, yaw=20.0)
    return mesh


def _build_cruiser():
    """A 470 x 190 cm cruiser facing +X: slots white, navy, glass, red bar, blue bar."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 28.0, 470.0, 190.0, 70.0)          # the body
    _box(mesh, 1, 0.0, 0.0, 50.0, 474.0, 194.0, 22.0)          # the navy band along it
    _box(mesh, 2, -20.0, 0.0, 98.0, 230.0, 170.0, 50.0)        # the cabin's glass
    _box(mesh, 0, -20.0, 0.0, 146.0, 220.0, 165.0, 6.0)        # the roof
    _box(mesh, 3, -10.0, -38.0, 152.0, 22.0, 70.0, 12.0)       # the light bar
    _box(mesh, 4, -10.0, 38.0, 152.0, 22.0, 70.0, 12.0)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            _cyl(mesh, 2, sx * 150.0, sy * 92.0, 0.0, 33.0, 22.0, 14, roll=90.0)   # wheels
    return mesh


def _material(key):
    if key in GLOWS:
        rgb, intensity = GLOWS[key]
        parent = m.ensure_material(m.M_EMISSIVE, m._build_emissive)
        return m.ensure_material_instance(c.asset_path(KIT_PATH, key), parent, vectors=[(m.EMISSIVE_COLOR_PARAM, rgb)],
                                          scalars=[(m.EMISSIVE_INTENSITY_PARAM, intensity)])
    rgb, rough = COLOURS[key]
    return c.ensure_constant_color_material("M_Site" + key, KIT_PATH, rgb, rough)


def _write_mesh(name, build, slots, collision=True):
    full = c.asset_path(KIT_PATH, name)
    materials = [_material(key) for key in slots]
    spec = "mesh-" + _hash([MESH_VERSION, name, list(slots), collision])
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
        options.set_editor_property("enable_collision", collision)
        if collision:
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


def ensure_meshes():
    meshes = {
        "box": _write_mesh("SM_Kit_ToyBox", _build_box, ("Cardboard", "Tape")),
        "banner": _write_mesh("SM_Kit_DriveBanner", _build_banner, ("BannerRed", "BannerWhite"), collision=False),
        "sack": _write_mesh("SM_Kit_ToySack", _build_sack, ("SackRed", "SackTie"), collision=True),
        "reindeer": _write_mesh("SM_Kit_ReindeerHead", _build_reindeer_head, ("Antler", "ReindeerFur", "MI_ReindeerNose"),
                                collision=False),
        "volunteer": _write_mesh("SM_Kit_Figure_Volunteer", lambda: _build_figure(False), ("CoatRed", "Skin", "Jeans", "BannerWhite")),
        "police": _write_mesh("SM_Kit_Figure_Police", lambda: _build_figure(True), ("PoliceNavy", "Skin", "PoliceNavy", "PoliceCap")),
        "tarp": _write_mesh("SM_Kit_CourierTarp", _build_tarp, ("Tarp", "CourierBag")),
        "cruiser": _write_mesh("SM_Kit_Cruiser", _build_cruiser, ("CruiserWhite", "CruiserNavy", "Glass", "MI_LightBarRed",
                                                                  "MI_LightBarBlue")),
    }
    for key, name in (("table", "SM_Kit_FoldingTable"), ("pole", "SM_Kit_LightPole"), ("wire", "SM_Kit_LightWire")):
        meshes[key] = c.load_or_none(c.asset_path(BOSS_KIT, name))
    for colour, _rgb in BULB_COLOURS:
        meshes["bulb_" + colour] = c.load_or_none(c.asset_path(BOSS_KIT, "SM_Kit_Bulb_" + colour))
    missing = [k for k, v in meshes.items() if v is None]
    if missing:
        c.log("skipped", KIT_PATH, "missing " + ", ".join(sorted(missing)) + " (run create_boss first)")
        return None
    return meshes


# --------------------------------------------------------------------------------------------------
# the reindeer and the sack
# --------------------------------------------------------------------------------------------------


def ensure_reindeer(head):
    import create_blueprints as cb  # noqa: PLC0415
    import create_enemies as ce  # noqa: PLC0415
    import create_world_blueprints as wb  # noqa: PLC0415

    full = c.asset_path(AI_PATH, REINDEER_BP)
    parent = c.load_generated_class(AI_PATH, "BP_Thug")
    if parent is None or head is None:
        c.log("skipped", full, "run create_world_blueprints first")
        return None
    c.ensure_directory(REINDEER_MATERIAL_PATH)
    bp, created = cb.make_blueprint(REINDEER_BP, AI_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.ensure_parent(bp, full, parent)
    suit = ce._versioned_material(M_REINDEER_SUIT, wb.tracksuit_builder(REINDEER_SUIT, REINDEER_STRIPE, REINDEER_MASK),
                                  REINDEER_MATERIAL_VERSION)
    trim = ce._versioned_material(M_REINDEER_TRIM, wb.trim_builder(REINDEER_STRIPE), REINDEER_MATERIAL_VERSION)
    changed = []
    body = cb.cdo_component(bp, "mesh")
    for slot, material in ((0, suit), (1, trim)):
        if cb.set_material_slot(body, slot, material, REINDEER_BP + ".Mesh"):
            changed.append("material {0}".format(slot))
    headwear = cb.cdo_component(bp, "headwear")
    if headwear is None:
        c.log("FAILED", full, "no Headwear component on AThugCharacter; build the module")
    elif cb.set_if_different(headwear, [("static_mesh", head)], REINDEER_BP + ".Headwear"):
        changed.append("antlers")
    cdo = c.blueprint_cdo(bp)
    offset = unreal.Vector(*HEADWEAR_OFFSET)
    if cdo is not None and cb.set_if_different(cdo, [("headwear_offset", offset)], REINDEER_BP):
        changed.append("headwear offset")
    if changed or created:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated" if not created else "created", full, ", ".join(changed))
    else:
        c.log("exists", full, "brown suit and antlers already set")
    return c.load_generated_class(AI_PATH, REINDEER_BP)


def ensure_sack(sack):
    import create_blueprints as cb  # noqa: PLC0415

    full = c.asset_path(WORLD_PATH, SACK_BP)
    parent = c.find_class("PickupActor", "/Script/Hawkeye.PickupActor")
    if parent is None or sack is None:
        c.log("FAILED", full, "APickupActor not exposed; build the module")
        return None
    bp, created = cb.make_blueprint(SACK_BP, WORLD_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    changed = []
    root = cb.cdo_component(bp, "mesh")
    if root is not None and cb.set_if_different(root, [("static_mesh", sack)], SACK_BP + ".Mesh"):
        changed.append("sack mesh")
    cdo = c.blueprint_cdo(bp)
    if cdo is not None:
        changed += cb.set_if_different(cdo, [("pickup_type", unreal.PickupType.KEYCARD), ("hover_height", 12.0),
                                             ("spin_degrees_per_second", 20.0), ("bob_amplitude", 2.0),
                                             ("prompt_override", unreal.Text("[Pick up the sack]"))], SACK_BP) or []
    if changed or created:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated" if not created else "created", full, ", ".join(str(x) for x in changed))
    else:
        c.log("exists", full, "the sack already set")
    return c.load_generated_class(WORLD_PATH, SACK_BP)


# --------------------------------------------------------------------------------------------------
# the site asset
# --------------------------------------------------------------------------------------------------


def _vec(t):
    return unreal.Vector(float(t[0]), float(t[1]), float(t[2]))


def _enum(name, value):
    import re  # noqa: PLC0415
    return getattr(getattr(unreal, name), re.sub(r"(?<!^)(?=[A-Z])", "_", value).upper())


def _prop(at, mesh, blocks, scale=(1.0, 1.0, 1.0)):
    prop = unreal.ChapterSiteProp()
    prop.set_editor_property("transform", unreal.Transform(location=_vec(at[:3]), rotation=unreal.Rotator(0.0, 0.0, at[3]),
                                                           scale=unreal.Vector(*scale)))
    prop.set_editor_property("mesh", mesh)
    prop.set_editor_property("blocks", blocks)
    return prop


def _light(at, rgb, lumens, radius):
    light = unreal.ChapterSiteLight()
    light.set_editor_property("location", _vec(at))
    light.set_editor_property("color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    light.set_editor_property("lumens", lumens)
    light.set_editor_property("radius", radius)
    return light


def _examine(objective, at, yaw, prompt, mesh, current, keycards, close_offset, look_at, zone):
    spec = unreal.ChapterSiteExamine()
    for prop, value in (("objective_id", unreal.Name(objective)), ("location", _vec(at)), ("yaw", float(yaw)),
                        ("prompt", unreal.Text(prompt)), ("mesh", mesh), ("require_current_objective", current),
                        ("required_keycards", [unreal.Name(k) for k in keycards]), ("close_up_offset", _vec(close_offset)),
                        ("close_up_look_at", _vec(look_at)), ("interact_zone_extent", _vec(zone)), ("close_up_seconds", 2.5)):
        spec.set_editor_property(prop, value)
    return spec


def _fields(plan, meshes, reindeer, sack):
    z = plan["spot"][2]
    props = []
    lights = []
    for kind, at in plan["props"]:
        blocks = kind in ("table", "box", "pole")
        props.append(_prop(at, meshes[kind], blocks))
    for x, y, wz, yaw, length in plan["wires"]:
        props.append(_prop((x, y, wz, yaw), meshes["wire"], False, (length / 100.0, 1.0, 1.0)))
    for index, (x, y, bz) in enumerate(plan["bulbs"]):
        colour, rgb = BULB_COLOURS[index % len(BULB_COLOURS)]
        props.append(_prop((x, y, bz, 0.0), meshes["bulb_" + colour], False))
        if index % 3 == 0:
            lights.append(_light((x, y, bz - 10.0), rgb, BULB_LUMENS, BULB_RADIUS))
    for x, y, pz, yaw in plan["police"]:
        props.append(_prop((x, y, pz, yaw), meshes["police"], True))
    cruiser = plan["cruiser"]
    props.append(_prop(cruiser, meshes["cruiser"], True))
    for (rgb, lumens, radius), side in zip(CRUISER_LIGHTS, (-1.0, 1.0)):
        lights.append(_light((cruiser[0], cruiser[1] + side * 30.0, cruiser[2] + 190.0), rgb, lumens, radius))

    volume = unreal.ChapterSiteVolume()
    volume.set_editor_property("objective_id", unreal.Name("reach_drive"))
    volume.set_editor_property("location", _vec(plan["arrive"]))
    volume.set_editor_property("extent", _vec(plan["arrive_extent"]))

    crew = []
    for weapon, at, yaw in plan["crew"]:
        member = unreal.ChapterSiteThug()
        member.set_editor_property("thug_class", reindeer)
        member.set_editor_property("weapon", _enum("ThugWeapon", weapon))
        member.set_editor_property("location", _vec(at))
        member.set_editor_property("yaw", float(yaw))
        crew.append(member)

    pickups = []
    for at, keycard in zip(plan["sacks"], SACK_KEYCARDS):
        pickup = unreal.ChapterSitePickup()
        pickup.set_editor_property("pickup_class", sack)
        pickup.set_editor_property("location", _vec(at))
        pickup.set_editor_property("keycard_id", unreal.Name(keycard))
        pickups.append(pickup)

    table = plan["table"]
    examines = [
        # The drive's table: the toys go back once the crew is down and she has every sack. The close-up looks from the
        # side and above, so Kate (in front of it) is in the shot, not in the way.
        _examine("return_toys", table, plan["facing"], TABLE_PROMPT, None, True, SACK_KEYCARDS, *TABLE_CLOSE_UP),
        # The courier: optional, offered whenever no fight is on, never marked.
        _examine("examine_courier", plan["courier"], plan["courier_yaw"], COURIER_PROMPT, meshes["tarp"], False, (), *COURIER_CLOSE_UP),
    ]
    return [
        ("player_start", _vec(plan["player_start"])),
        ("player_start_yaw", float(plan["player_yaw"])),
        ("props", props),
        ("lights", lights),
        ("volumes", [volume]),
        ("crew", crew),
        ("crew_objective", unreal.Name("stop_crew")),
        ("crew_alert_objective", unreal.Name("reach_drive")),
        ("pickups", pickups),
        ("examines", examines),
    ], z


def ensure_site(plan, meshes, reindeer, sack):
    cls = c.find_class("ChapterSiteDefinition", "/Script/Hawkeye.ChapterSiteDefinition")
    full = c.asset_path(SITE_PATH, SITE_NAME)
    if cls is None:
        c.log("FAILED", full, "UChapterSiteDefinition not exposed to Python; build the module")
        return None
    factory = c.new_factory("DataAssetFactory")
    c.set_props(factory, [("data_asset_class", cls)], "DataAssetFactory")
    asset, created = c.create_asset(SITE_NAME, SITE_PATH, cls, factory, quiet=True)
    if asset is None:
        return None
    build = "site-" + _hash([plan, SACK_KEYCARDS, TABLE_PROMPT, COURIER_PROMPT, TABLE_CLOSE_UP, COURIER_CLOSE_UP, BULB_COLOURS,
                             BULB_LUMENS, BULB_RADIUS,
                             CRUISER_LIGHTS, c.safe_name(reindeer), c.safe_name(sack),
                             sorted((k, c.safe_name(v)) for k, v in meshes.items())])
    if not created and unreal.EditorAssetLibrary.get_metadata_tag(asset, BUILD_TAG) == build:
        c.log("exists", full, _site.describe(plan))
        return asset
    fields, _z = _fields(plan, meshes, reindeer, sack)
    for prop, value in fields:
        try:
            asset.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(full, prop), exc)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, BUILD_TAG, build)
    c.save(asset)
    c.log("created" if created else "updated", full, _site.describe(plan))
    return asset


def run():
    import generate_city as gen  # noqa: PLC0415
    if not gen.data_available():
        c.log("skipped", SITE_PATH, "no OSM data; run Tools\\fetch-osm.ps1 first")
        return None
    c.ensure_directory(KIT_PATH)
    c.ensure_directory(SITE_PATH)
    meshes = ensure_meshes()
    if meshes is None:
        return None
    reindeer = ensure_reindeer(meshes["reindeer"])
    sack = ensure_sack(meshes["sack"])
    plan = _site.plan_site(gen.District())
    if plan is None:
        c.log("FAILED", c.asset_path(SITE_PATH, SITE_NAME), _site.describe(None))
        return None
    return ensure_site(plan, meshes, reindeer, sack)


if __name__ == "__main__":
    run()
    c.print_summary("chapter site")
