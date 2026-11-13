"""Procedural material helpers for the Hawkeye art passes.

Everything here is built out of engine material expressions - no imported textures, no
downloads. create_all.py runs this module as its own step (the interior surface and lamp
materials), and generate_city.py and create_world_blueprints.py reuse its builders.

Conventions are the same as ``_common.py``: check before creating, save only what
changed, print one ``created`` / ``exists`` / ``updated`` / ``FAILED`` line per asset,
and never let one broken node graph abort the script.

Materials live in ``/Game/Materials`` per claude-docs/asset-conventions.md.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402

MATERIALS_PATH = "/Game/Materials"

# --- exposure preset -------------------------------------------------------------------
# The emissive instances are tuned against the interior exposure. The factor lives here so
# every script that asks for them agrees: when two disagreed, every run rewrote the MI_*
# assets to the other one's number and left them dirty in git forever.
#
#   default (HAWKEYE_BRIGHT unset or "0") -> ROOM_EXPOSURE_EV_NORMAL, the shipped look
#   HAWKEYE_BRIGHT=1                      -> ROOM_EXPOSURE_EV_TESTING, for playtesting
ROOM_EXPOSURE_EV_NORMAL = -3.0
ROOM_EXPOSURE_EV_TESTING = -1.5
BRIGHT = os.environ.get("HAWKEYE_BRIGHT", "0") == "1"
ROOM_EXPOSURE_EV = ROOM_EXPOSURE_EV_TESTING if BRIGHT else ROOM_EXPOSURE_EV_NORMAL

# The lamp instances were eyeballed at the old fixed -4.5 EV bias, so a brighter preset
# needs a proportionally dimmer emissive or the tubes blow out.
EMISSIVE_INTENSITY_FACTOR = 2 ** (ROOM_EXPOSURE_EV - (-4.5))

# Wall / floor / metal
M_CONCRETE = MATERIALS_PATH + "/M_Concrete"
M_CONCRETE_FLOOR = MATERIALS_PATH + "/M_ConcreteFloor"
M_STEEL_PAINTED = MATERIALS_PATH + "/M_SteelPainted"

# Emissive master + its instances
M_EMISSIVE = MATERIALS_PATH + "/M_Emissive"
M_FLUORESCENT_FLICKER = MATERIALS_PATH + "/M_FluorescentFlicker"
MI_FLUORESCENT_TUBE = MATERIALS_PATH + "/MI_FluorescentTube"
MI_RED_EMERGENCY = MATERIALS_PATH + "/MI_RedEmergency"
MI_MONITOR = MATERIALS_PATH + "/MI_Monitor"

EMISSIVE_COLOR_PARAM = "Color"
EMISSIVE_INTENSITY_PARAM = "Intensity"


# --------------------------------------------------------------------------------------
# tiny wrappers over MaterialEditingLibrary
# --------------------------------------------------------------------------------------


def _mel():
    return unreal.MaterialEditingLibrary


def expr(material, class_name, x=0, y=0, props=None, context=""):
    """Create one material expression. Returns None when the node type is unavailable.

    ``props`` is a list of (name, value) pairs handed to ``c.set_props``.
    """
    cls = getattr(unreal, class_name, None)
    if cls is None:
        unreal.log_warning("[Hawkeye] skipped   {0}: {1} not scriptable".format(
            context or material.get_name(), class_name))
        return None
    try:
        if isinstance(material, unreal.MaterialFunction):
            node = _mel().create_material_expression_in_function(material, cls, x, y)
        else:
            node = _mel().create_material_expression(material, cls, x, y)
    except Exception as exc:  # noqa: BLE001
        c.log_error("create_material_expression " + class_name, exc)
        return None
    if node is not None and props:
        c.set_props(node, props, context or class_name)
    return node


def connect(from_node, from_output, to_node, to_input):
    """Wire two expressions. False (with a warning) when either end is missing."""
    if from_node is None or to_node is None:
        return False
    try:
        return bool(_mel().connect_material_expressions(
            from_node, from_output, to_node, to_input))
    except Exception as exc:  # noqa: BLE001
        c.log_error("connect {0} -> {1}".format(from_output, to_input), exc)
        return False


def connect_property(from_node, material_property, from_output=""):
    if from_node is None:
        return False
    try:
        return bool(_mel().connect_material_property(
            from_node, from_output, material_property))
    except Exception as exc:  # noqa: BLE001
        c.log_error("connect_material_property " + str(material_property), exc)
        return False


def constant(material, value, x=0, y=0):
    return expr(material, "MaterialExpressionConstant", x, y, [("r", float(value))])


def constant3(material, rgb, x=0, y=0):
    return expr(
        material,
        "MaterialExpressionConstant3Vector",
        x, y,
        [("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))],
    )


def set_scalar_property(material, value, material_property, x=0, y=0):
    return connect_property(constant(material, value, x, y), material_property)


def noise(material, scale, x=0, y=0, out_min=0.0, out_max=1.0, levels=3, turbulence=True):
    """A MaterialExpressionNoise with its Position input left unconnected.

    Unconnected Position defaults to absolute world position, which is exactly what we
    want for world-aligned concrete - no UVs on the greybox cubes to worry about.
    """
    props = [
        ("scale", float(scale)),
        ("output_min", float(out_min)),
        ("output_max", float(out_max)),
        ("levels", int(levels)),
        ("turbulence", bool(turbulence)),
    ]
    node = expr(material, "MaterialExpressionNoise", x, y, props, "Noise")
    if node is not None:
        # Cheapest ALU noise; the enum member name has moved around between versions.
        fn = getattr(unreal, "NoiseFunction", None)
        if fn is not None:
            for member in ("NOISEFUNCTION_GRADIENT_ALU", "NOISEFUNCTION_VALUE_ALU"):
                value = getattr(fn, member, None)
                if value is not None:
                    c.set_props(node, [("noise_function", value)], "Noise")
                    break
    return node


def lerp(material, a_node, b_node, alpha_node, x=0, y=0, const_a=None, const_b=None):
    props = []
    if const_a is not None:
        props.append(("const_a", float(const_a)))
    if const_b is not None:
        props.append(("const_b", float(const_b)))
    node = expr(material, "MaterialExpressionLinearInterpolate", x, y, props, "Lerp")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    connect(alpha_node, "", node, "Alpha")
    return node


def multiply(material, a_node, b_node, x=0, y=0, const_b=None):
    props = [("const_b", float(const_b))] if const_b is not None else []
    node = expr(material, "MaterialExpressionMultiply", x, y, props, "Multiply")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    return node


def add(material, a_node, b_node, x=0, y=0, const_b=None):
    props = [("const_b", float(const_b))] if const_b is not None else []
    node = expr(material, "MaterialExpressionAdd", x, y, props, "Add")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    return node


def subtract(material, a_node, b_node, x=0, y=0, const_b=None):
    props = [("const_b", float(const_b))] if const_b is not None else []
    node = expr(material, "MaterialExpressionSubtract", x, y, props, "Subtract")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    return node


def divide(material, a_node, b_node, x=0, y=0, const_b=None):
    props = [("const_b", float(const_b))] if const_b is not None else []
    node = expr(material, "MaterialExpressionDivide", x, y, props, "Divide")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    return node


def clamp01(material, input_node, x=0, y=0):
    node = expr(
        material,
        "MaterialExpressionClamp",
        x, y,
        [("min_default", 0.0), ("max_default", 1.0)],
        "Clamp",
    )
    connect(input_node, "", node, "")
    return node


def frac(material, input_node, x=0, y=0):
    node = expr(material, "MaterialExpressionFrac", x, y, None, "Frac")
    connect(input_node, "", node, "")
    return node


def absolute(material, input_node, x=0, y=0):
    node = expr(material, "MaterialExpressionAbs", x, y, None, "Abs")
    connect(input_node, "", node, "")
    return node


def sine(material, input_node, x=0, y=0):
    node = expr(material, "MaterialExpressionSine", x, y, None, "Sine")
    connect(input_node, "", node, "")
    return node


def maximum(material, a_node, b_node, x=0, y=0):
    node = expr(material, "MaterialExpressionMax", x, y, None, "Max")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    return node


def component_mask(material, input_node, r=False, g=False, b=False, a=False, x=0, y=0):
    node = expr(
        material,
        "MaterialExpressionComponentMask",
        x, y,
        [("r", r), ("g", g), ("b", b), ("a", a)],
        "ComponentMask",
    )
    connect(input_node, "", node, "")
    return node


def world_position(material, x=0, y=0):
    return expr(material, "MaterialExpressionWorldPosition", x, y, None, "WorldPosition")


def time_node(material, x=0, y=0):
    return expr(material, "MaterialExpressionTime", x, y, None, "Time")


def step(material, input_node, edge, x=0, y=0, sharpness=200.0):
    """step(edge, x) as saturate((x - edge) * sharpness). No If node needed."""
    shifted = subtract(material, input_node, None, x, y, const_b=float(edge))
    scaled = multiply(material, shifted, None, x + 150, y, const_b=float(sharpness))
    return clamp01(material, scaled, x + 300, y)


def height_darken(material, floor_meters=3.0, floor_value=0.45, x=-900, y=400):
    """1.0 high up, ``floor_value`` at Z = 0 - grime pooling at the bottom of walls."""
    wp = world_position(material, x, y)
    z = component_mask(material, wp, b=True, x=x + 200, y=y)
    normalised = divide(material, z, None, x + 400, y, const_b=float(floor_meters) * 100.0)
    mask = clamp01(material, normalised, x + 600, y)
    return lerp(material, None, None, mask, x + 800, y,
                const_a=float(floor_value), const_b=1.0)


# --------------------------------------------------------------------------------------
# material / instance creation
# --------------------------------------------------------------------------------------


def _split(full_path):
    full_path = full_path.rstrip("/")
    return full_path.rsplit("/", 1)[0], full_path.rsplit("/", 1)[1]


def usage_flags(skeletal=False, nanite=False, instanced=False):
    """The bUsedWith* property names a material has to carry for a given kind of mesh."""
    flags = []
    if skeletal:
        flags.append("used_with_skeletal_mesh")
    if nanite:
        flags.append("used_with_nanite")
    if instanced:
        flags.append("used_with_instanced_static_meshes")
    return flags


def ensure_usage(material, flags, full_path=""):
    """Set the named bUsedWith* flags on a Material when they are not already set.

    A material missing the flag for the mesh it is on is swapped for the grey engine default
    and the log says ``missing usage flag ...! Default Material will be used in game``. That is
    what put Frank in white plastic sleeves, the guards in mannequin grey and the pistol in
    default grey the moment it was picked up. The flags live in the asset, so they have to be
    written and saved here; nothing at runtime can recover them.

    Safe on anything: a material instance has no such property and is left alone. Returns True
    only when the asset actually changed.
    """
    if material is None or not isinstance(material, unreal.Material) or not flags:
        return False

    label = full_path or c.safe_name(material)
    written = []
    for flag in flags:
        try:
            if bool(material.get_editor_property(flag)):
                continue
            material.set_editor_property(flag, True)
            written.append(flag)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0} {1}".format(flag, label), exc)

    if not written:
        return False

    c.save(material)
    c.log("updated", label, ", ".join(written) + " = True")
    return True


def ensure_material(full_path, build_fn, rebuild=False, skeletal=False, nanite=False, instanced=False):
    """Idempotent Material. ``build_fn(material)`` wires the graph on first creation.

    An existing material is returned untouched unless ``rebuild`` is True, in which case its
    expressions are cleared and ``build_fn`` runs again. ``skeletal`` and ``nanite`` mark the
    material as usable on those mesh types, and are checked on every run, not only on creation.
    """
    package_path, name = _split(full_path)
    flags = usage_flags(skeletal, nanite, instanced)
    existing = c.load_or_none(full_path)
    if existing is not None and not rebuild:
        if ensure_usage(existing, flags, full_path):
            return existing
        c.log("exists", full_path)
        return existing

    material = existing
    created = False
    try:
        if material is None:
            c.ensure_directory(package_path)
            factory = c.new_factory("MaterialFactoryNew")
            if factory is None:
                c.log("FAILED", full_path, "MaterialFactoryNew unavailable")
                return None
            material = c.asset_tools().create_asset(
                name, package_path, unreal.Material, factory)
            if material is None:
                c.log("FAILED", full_path, "create_asset returned None")
                return None
            created = True
        else:
            try:
                unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
            except Exception as exc:  # noqa: BLE001
                c.log_error("delete_all_material_expressions " + full_path, exc)

        build_fn(material)
        for flag in flags:
            try:
                material.set_editor_property(flag, True)
            except Exception as exc:  # noqa: BLE001
                c.log_error("{0} {1}".format(flag, full_path), exc)
        unreal.MaterialEditingLibrary.recompile_material(material)
        c.save(material)
        c.log("created" if created else "updated", full_path)
        return material
    except Exception as exc:  # noqa: BLE001
        c.log_error("ensure_material " + full_path, exc)
        return material


_COMPONENT_SETS = (("x", "y", "z", "w"), ("r", "g", "b", "a"), ("x", "y", "z"))


def _components(value):
    """Numeric components of a Vector / Vector4 / LinearColor / Color, or None."""
    for attrs in _COMPONENT_SETS:
        if all(hasattr(value, attr) for attr in attrs):
            try:
                return [float(getattr(value, attr)) for attr in attrs]
            except (TypeError, ValueError):
                return None
    return None


def same_value(current, wanted, tolerance=1e-4):
    """True when a property already holds ``wanted``.

    Everything numeric compares with a tolerance, because a float the editor stored as
    float32 never reads back exactly equal to the Python literal that wrote it - and an
    exact comparison would rewrite (and dirty) the asset on every run.
    """
    if current is None:
        return False
    if isinstance(wanted, bool) or isinstance(current, bool):
        return bool(current) == bool(wanted)
    if isinstance(wanted, (int, float)) and isinstance(current, (int, float)):
        return abs(float(current) - float(wanted)) <= tolerance

    mine, theirs = _components(current), _components(wanted)
    if mine is not None and theirs is not None and len(mine) == len(theirs):
        return all(abs(a - b) <= tolerance for a, b in zip(mine, theirs))

    try:
        return bool(current == wanted)
    except Exception:  # noqa: BLE001
        return False


def instance_vector(instance, param):
    try:
        return unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
            instance, param)
    except Exception:  # noqa: BLE001
        return None


def instance_scalar(instance, param):
    try:
        return unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            instance, param)
    except Exception:  # noqa: BLE001
        return None


def ensure_material_instance(full_path, parent, vectors=None, scalars=None):
    """Idempotent MaterialInstanceConstant. Parameters are only written when they differ."""
    package_path, name = _split(full_path)
    if parent is None:
        c.log("FAILED", full_path, "parent material is None")
        return None

    instance = c.load_or_none(full_path)
    created = False
    if instance is None:
        try:
            c.ensure_directory(package_path)
            factory = c.new_factory("MaterialInstanceConstantFactoryNew")
            if factory is None:
                c.log("FAILED", full_path, "MaterialInstanceConstantFactoryNew unavailable")
                return None
            instance = c.asset_tools().create_asset(
                name, package_path, unreal.MaterialInstanceConstant, factory)
            if instance is None:
                c.log("FAILED", full_path, "create_asset returned None")
                return None
            created = True
        except Exception as exc:  # noqa: BLE001
            c.log_error("ensure_material_instance " + full_path, exc)
            return None

    changed = created
    try:
        if instance.get_editor_property("parent") != parent:
            instance.set_editor_property("parent", parent)
            changed = True
    except Exception as exc:  # noqa: BLE001
        c.log_error("set parent on " + full_path, exc)

    for param, rgb in (vectors or []):
        wanted = unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)
        if same_value(instance_vector(instance, param), wanted):
            continue
        try:
            unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                instance, param, wanted)
            changed = True
        except Exception as exc:  # noqa: BLE001
            c.log_error("set vector param {0} on {1}".format(param, full_path), exc)

    for param, value in (scalars or []):
        if same_value(instance_scalar(instance, param), float(value)):
            continue
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                instance, param, float(value))
            changed = True
        except Exception as exc:  # noqa: BLE001
            c.log_error("set scalar param {0} on {1}".format(param, full_path), exc)

    if changed:
        c.save(instance)
    c.log("created" if created else ("updated" if changed else "exists"), full_path)
    return instance


# --------------------------------------------------------------------------------------
# the graphs
# --------------------------------------------------------------------------------------


def _build_concrete(material):
    """Two greys lerped by fine noise, multiplied by large-scale grime and a floor mask."""
    fine = noise(material, 0.05, -1200, -400, levels=4)
    dark = constant3(material, (0.18, 0.18, 0.18), -1200, -200)
    light = constant3(material, (0.30, 0.30, 0.30), -1200, -60)
    base = lerp(material, dark, light, fine, -900, -200)

    # Large, soft blotches: 0.55..1.0, so it only ever darkens.
    grime = noise(material, 0.004, -1200, 100, out_min=0.55, out_max=1.0, levels=2)
    with_grime = multiply(material, base, grime, -650, -100)

    floor_mask = height_darken(material, floor_meters=3.0, floor_value=0.45)
    final = multiply(material, with_grime, floor_mask, -350, -100)
    connect_property(final, unreal.MaterialProperty.MP_BASE_COLOR)

    set_scalar_property(material, 0.85, unreal.MaterialProperty.MP_ROUGHNESS, -350, 200)


def _build_concrete_floor(material):
    """Darker concrete with a world-aligned 100 cm grout grid."""
    connect_property(_concrete_floor_color(material), unreal.MaterialProperty.MP_BASE_COLOR)
    set_scalar_property(material, 0.95, unreal.MaterialProperty.MP_ROUGHNESS, 100, 300)


def _concrete_floor_color(material):
    """The concrete floor's base colour node: two greys, a 100 cm grout grid, dirt at the foot."""
    fine = noise(material, 0.05, -1600, -400, levels=4)
    dark = constant3(material, (0.10, 0.10, 0.10), -1600, -200)
    light = constant3(material, (0.16, 0.16, 0.16), -1600, -60)
    base = lerp(material, dark, light, fine, -1350, -200)

    # frac(worldXY / 100) -> distance from tile centre -> grout on the outer 3 %.
    wp = world_position(material, -1900, 300)
    xy = component_mask(material, wp, r=True, g=True, x=-1700, y=300)
    tiles = divide(material, xy, None, -1500, 300, const_b=100.0)
    f = frac(material, tiles, -1350, 300)
    centred = subtract(material, f, None, -1200, 300, const_b=0.5)
    dist = absolute(material, centred, -1050, 300)
    dx = component_mask(material, dist, r=True, x=-900, y=250)
    dy = component_mask(material, dist, g=True, x=-900, y=380)
    worst = maximum(material, dx, dy, -750, 300)
    grout_mask = step(material, worst, 0.47, -600, 300)

    grout = constant3(material, (0.045, 0.045, 0.05), -1350, 520)
    tiled = lerp(material, base, grout, grout_mask, -100, 0)

    floor_dirt = height_darken(material, floor_meters=3.0, floor_value=0.7, x=-900, y=700)
    return multiply(material, tiled, floor_dirt, 100, 0)


STEEL_PAINTED_BUILD = "black-iron-snow-7"   # metadata tag CastleBuild (pre-rename key, kept); a different value rebuilds the graph


def _build_steel_painted(material):
    """Black iron (base 0.02) with noise scuffs showing duller metal: fire escapes, lamp poles, doors.
    Snow (MF_Snow) settles on its upward faces: fire-escape landings, rail tops."""
    scuff = noise(material, 0.6, -900, -400, levels=3)
    paint = constant3(material, (0.02, 0.02, 0.02), -900, -200)
    worn = constant3(material, (0.07, 0.07, 0.07), -900, -60)
    base = lerp(material, paint, worn, scuff, -600, -200)
    snow = snowed(material, base, constant(material, 0.5, -600, 150))
    metallic = lerp(material, None, None, None, -200, 280, const_a=0.6, const_b=0.0)
    connect(snow, "Mask", metallic, "Alpha")
    connect_property(metallic, unreal.MaterialProperty.MP_METALLIC)


def ensure_steel_painted():
    """M_SteelPainted as black iron, usable on instanced meshes. An older build (the green-grey
    paint) is rebuilt once, and tagged so the next run leaves it alone."""
    material = ensure_material(M_STEEL_PAINTED, _build_steel_painted)
    if material is None:
        return None
    try:
        tag = unreal.EditorAssetLibrary.get_metadata_tag(material, "CastleBuild")
    except Exception:  # noqa: BLE001
        tag = None
    if tag != STEEL_PAINTED_BUILD:
        material = ensure_material(M_STEEL_PAINTED, _build_steel_painted, rebuild=True)
        unreal.EditorAssetLibrary.set_metadata_tag(material, "CastleBuild", STEEL_PAINTED_BUILD)
        c.save(material)
    # The fire escapes draw it on instanced meshes; without the flag the game swaps in the default.
    ensure_usage(material, ["used_with_instanced_static_meshes"], M_STEEL_PAINTED)
    return material


def _build_flat(rgb, roughness, metallic=0.0):
    def build(material):
        connect_property(
            constant3(material, rgb, -600, -200), unreal.MaterialProperty.MP_BASE_COLOR)
        set_scalar_property(material, roughness, unreal.MaterialProperty.MP_ROUGHNESS, -600, 0)
        if metallic:
            set_scalar_property(
                material, metallic, unreal.MaterialProperty.MP_METALLIC, -600, 150)
    return build


def _emissive_params(material, x=-1100, y=-200):
    """The shared (Color, Intensity) parameter pair used by every light material."""
    color = expr(
        material,
        "MaterialExpressionVectorParameter",
        x, y,
        [
            ("parameter_name", EMISSIVE_COLOR_PARAM),
            ("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0)),
        ],
        "Color",
    )
    intensity = expr(
        material,
        "MaterialExpressionScalarParameter",
        x, y + 200,
        [("parameter_name", EMISSIVE_INTENSITY_PARAM), ("default_value", 8.0)],
        "Intensity",
    )
    return color, intensity


def _build_emissive(material):
    color, intensity = _emissive_params(material)
    connect_property(multiply(material, color, intensity, -800, -100),
                     unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_property(constant3(material, (0.02, 0.02, 0.02), -800, 250),
                     unreal.MaterialProperty.MP_BASE_COLOR)
    set_scalar_property(material, 0.4, unreal.MaterialProperty.MP_ROUGHNESS, -800, 400)


def _build_fluorescent_flicker(material):
    """emissive * (1 + 0.3 * sin(Time * 37) * step(0.6, frac(Time * 2.3)))

    A bad ballast: mostly steady, with bursts of buzz. The light actor itself stays
    steady until a UFlickerLightComponent exists on the C++ side.
    """
    color, intensity = _emissive_params(material)
    steady = multiply(material, color, intensity, -800, -200)

    t = time_node(material, -1400, 300)
    fast = multiply(material, t, None, -1200, 300, const_b=37.0)
    wave = sine(material, fast, -1050, 300)
    amount = multiply(material, wave, None, -900, 300, const_b=0.3)

    slow = multiply(material, t, None, -1200, 550, const_b=2.3)
    cycle = frac(material, slow, -1050, 550)
    gate = step(material, cycle, 0.6, -900, 550)

    burst = multiply(material, amount, gate, -300, 400)
    modulation = add(material, burst, None, -150, 400, const_b=1.0)

    connect_property(multiply(material, steady, modulation, 0, 0),
                     unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_property(constant3(material, (0.02, 0.02, 0.02), -800, 700),
                     unreal.MaterialProperty.MP_BASE_COLOR)
    set_scalar_property(material, 0.4, unreal.MaterialProperty.MP_ROUGHNESS, -800, 850)


# --------------------------------------------------------------------------------------
# the East Village night look: snow and facade material functions, the facade, prop, pavement,
# lamp-head and star materials. Colours are the Fraction and Aja palette (purple, cream, grey,
# black) with warm windows. generate_city.py, create_blueprints.py and create_weapon_data.py
# call these.
# --------------------------------------------------------------------------------------

# Metadata tag on every look asset. A different value rebuilds each one once (functions first).
# The HLSL's own hash is part of it, so an edit to _look rebuilds the graphs that carry it.
import _look  # noqa: E402
LOOK_BUILD = "block-1/" + _look.SOURCE_HASH
LOOK_TAG = "HawkeyeBuild"

MF_SNOW = MATERIALS_PATH + "/MF_Snow"
MF_FACADE = MATERIALS_PATH + "/MF_Facade"
M_FACADE = MATERIALS_PATH + "/M_Facade"
M_PROP = MATERIALS_PATH + "/M_Prop"
M_SIDEWALK_SNOW = MATERIALS_PATH + "/M_SidewalkSnow"
M_PARK_SNOW = MATERIALS_PATH + "/M_ParkSnow"
M_STREET_ASPHALT = MATERIALS_PATH + "/M_StreetAsphalt"
M_LAMP_HEAD = MATERIALS_PATH + "/M_LampHead"
M_NIGHT_STARS = MATERIALS_PATH + "/M_NightStars"
M_WINDOW_LIT = MATERIALS_PATH + "/M_WindowLit"     # the dressing's lit window cards: M_Facade's window, always lit

# Snow: world-aligned on faces whose normal is more than SNOW_NORMAL_Z up.
SNOW_COLOR = (0.62, 0.60, 0.55)      # cream white
SNOW_ROUGHNESS = 0.6
SNOW_NORMAL_Z = 0.7
SNOW_EDGE = 12.5                     # 1 / the normal-Z range the edge blends over (0.08)
SNOW_SPARKLE_CELL = 3.0              # cm; one glint cell
SNOW_SPARKLE_FRACTION = 0.0006       # of cells that glint
SNOW_SPARKLE = 1.0                   # emissive, before EMISSIVE_INTENSITY_FACTOR
SNOW_SPARKLE_DISTANCE = 1200.0       # cm; glints fade out by here, before they alias into grain

# Facade grid, cm, world-aligned.
FLOOR_PITCH = 330.0
BAY_PITCH = 160.0
WINDOW_W = 110.0
WINDOW_H = 150.0
WINDOW_SILL = 95.0                   # window bottom above its floor line
FRAME = 9.0                          # lintel/sill trim round each window
UPPER_FLOORS_FROM = 420.0            # z; below is the storefront band
STORE_PITCH = 320.0
STORE_W = 256.0
STORE_BOTTOM = 40.0
STORE_TOP = 300.0
SIGN_TOP = 395.0
STOREFRONT_MIN_TOP = 700.0           # only buildings this tall get storefronts and a cornice
CORNICE_FROM_TOP = (90.0, 145.0)     # cm under the top of the mesh (the parapet top)
CORNICE_INK = 13.0                   # the black line under the cornice
LIT_STORE_FRACTION = 0.4             # was 0.6; storefront bands read as a light board otherwise
WINDOW_GLASS = (0.05, 0.07, 0.12)     # faint dark-blue reflection, not pure black, so the grid still reads
WINDOW_LIT = (1.0, 0.62, 0.30)       # warm; also the "warm" bucket of WINDOW_LIT_COLORS below
WINDOW_LIT_COOL = (0.85, 0.9, 1.0)    # cool white bucket
WINDOW_LIT_TV = (0.5, 0.6, 1.0)       # TV-blue bucket
WINDOW_COOL_FRACTION = 0.20           # of lit windows: cool white
WINDOW_TV_FRACTION = 0.10            # of lit windows: TV-blue (the remaining 0.70 is warm)
WINDOW_JITTER_MIN = 0.6               # per-window brightness jitter range
WINDOW_JITTER_MAX = 1.0
ROOF_COLOR = (0.05, 0.05, 0.055)
SIGN_COLOR = (0.025, 0.02, 0.03)
INK = (0.008, 0.008, 0.01)

# name: (wall, trim, lit fraction). 2026-11-13: lit fractions 0.35 (painted 0.15), up from 0.22 / 0.09 now a lit window is a room,
# not a flat card (claude-docs/city-look.md). Wall colours bumped ~1.15-1.4x over the original so brick reads
# under lamp light instead of going flat black; lit fractions were 0.35 (0.15 for Painted) before
# windows read as a light board - scaled down by the same 22/35 ratio as the residential-floor spec.
FACADE_STYLES = {
    "BrickRed": ((0.32, 0.105, 0.077), (0.55, 0.50, 0.40), 0.35),
    "BrickBrown": ((0.24, 0.13, 0.084), (0.50, 0.46, 0.38), 0.35),
    "BrickPurple": ((0.15, 0.095, 0.085), (0.52, 0.48, 0.42), 0.35),   # dark ironspot brick (was a purple)
    "Brownstone": ((0.22, 0.13, 0.098), (0.09, 0.055, 0.04), 0.35),
    "Stone": ((0.30, 0.30, 0.29), (0.20, 0.20, 0.19), 0.35),    # concrete (the towers): _look.MASONRY
    "Painted": ((0.24, 0.24, 0.27), (0.03, 0.03, 0.035), 0.15),
}
WINDOW_GLOW = 0.04                    # before EMISSIVE_INTENSITY_FACTOR; was 0.7, then 0.175 (/4), then 0.08 - each
                                       # round's screenshot still read brighter than the lamp head, scaled down to
                                       # land near 60% of it on screen

STAR_CELLS = 250.0                   # per unit of view direction; a star is about 3 pixels at 720p (smaller
                                     # ones flicker between jitter samples and the upscaler drops them)
STAR_FRACTION = 0.003
STAR_BRIGHTNESS = 6.0
STAR_COLOR = (0.8, 0.85, 1.0)


def mi_facade_path(style):
    return MATERIALS_PATH + "/MI_Facade_" + style


def _tagged(asset):
    try:
        return unreal.EditorAssetLibrary.get_metadata_tag(asset, LOOK_TAG) == LOOK_BUILD
    except Exception:  # noqa: BLE001
        return False


def _tag(asset):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, LOOK_TAG, LOOK_BUILD)


def _clear_function(mf):
    """Delete every expression in a material function. delete_all_material_expressions_in_function
    skips some (old inputs survived each rebuild and every call grew duplicate pins), so this deletes
    one at a time until the function is empty."""
    for _attempt in range(10):
        remaining = list(_mel().get_material_function_expressions(mf))
        if not remaining:
            return
        for expression in remaining:
            _mel().delete_material_expression_in_function(mf, expression)
    c.log("FAILED", c.safe_name(mf), "{0} expressions would not delete".format(
        len(_mel().get_material_function_expressions(mf))))


def ensure_material_function(full_path, build_fn):
    """Idempotent MaterialFunction, rebuilt when its LOOK_BUILD tag is out of date."""
    package_path, name = _split(full_path)
    mf = c.load_or_none(full_path)
    if mf is not None and _tagged(mf):
        c.log("exists", full_path)
        return mf
    created = mf is None
    try:
        if created:
            c.ensure_directory(package_path)
            factory = c.new_factory("MaterialFunctionFactoryNew")
            mf = c.asset_tools().create_asset(name, package_path, unreal.MaterialFunction, factory)
            if mf is None:
                c.log("FAILED", full_path, "create_asset returned None")
                return None
        else:
            _clear_function(mf)
        build_fn(mf)
        _mel().update_material_function(mf)
        _tag(mf)
        c.save(mf)
        c.log("created" if created else "updated", full_path, LOOK_BUILD)
        return mf
    except Exception as exc:  # noqa: BLE001
        c.log_error("ensure_material_function " + full_path, exc)
        return mf


def ensure_look_material(full_path, build_fn, skeletal=False, instanced=False):
    """ensure_material, rebuilt once whenever LOOK_BUILD changes."""
    existing = c.load_or_none(full_path)
    if existing is not None and _tagged(existing):
        return ensure_material(full_path, build_fn, skeletal=skeletal, instanced=instanced)
    material = ensure_material(full_path, build_fn, rebuild=True, skeletal=skeletal, instanced=instanced)
    if material is not None:
        _tag(material)
        c.save(material)
    return material


def function_input(mf, name, scalar, sort, x, y, default):
    """A function input. Every caller here connects every input, so there is no default; ``default``
    documents the value the input was tuned with."""
    del default
    kind = unreal.FunctionInputType.FUNCTION_INPUT_SCALAR if scalar else unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3
    return expr(mf, "MaterialExpressionFunctionInput", x, y,
                [("input_name", name), ("input_type", kind), ("sort_priority", sort)], "FunctionInput " + name)


def function_output(mf, name, sort, source, x, y):
    node = expr(mf, "MaterialExpressionFunctionOutput", x, y,
                [("output_name", name), ("sort_priority", sort)], "FunctionOutput " + name)
    connect(source, "", node, "")
    return node


def function_call(material, mf, x=0, y=0):
    node = expr(material, "MaterialExpressionMaterialFunctionCall", x, y, None, "MaterialFunctionCall")
    if node is not None and mf is not None:
        c.set_props(node, [("material_function", mf)], "MaterialFunctionCall")
    return node


def vector_param(material, name, rgb, x=0, y=0):
    return expr(material, "MaterialExpressionVectorParameter", x, y, [
        ("parameter_name", name), ("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))], name)


def scalar_param(material, name, value, x=0, y=0):
    return expr(material, "MaterialExpressionScalarParameter", x, y, [
        ("parameter_name", name), ("default_value", float(value))], name)


def floor_node(material, input_node, x=0, y=0):
    node = expr(material, "MaterialExpressionFloor", x, y, None, "Floor")
    connect(input_node, "", node, "")
    return node


def dot(material, a_node, b_node, x=0, y=0):
    node = expr(material, "MaterialExpressionDotProduct", x, y, None, "Dot")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    return node


def below(material, input_node, edge, x=0, y=0, sharpness=2.0):
    """saturate((edge - x) * sharpness): 1 under the edge, 0 over it."""
    flipped = multiply(material, input_node, None, x, y, const_b=-float(sharpness))
    return clamp01(material, add(material, flipped, None, x + 150, y, const_b=float(edge) * float(sharpness)), x + 300, y)


def band(material, input_node, lo, hi, x=0, y=0, sharpness=2.0):
    """1 between lo and hi, soft over 1 / sharpness at each edge."""
    return multiply(material, step(material, input_node, lo, x, y, sharpness),
                    below(material, input_node, hi, x, y + 60, sharpness), x + 450, y)


def hash01(material, seed_node, x=0, y=0):
    """frac(sin(seed) * 43758.5453), twice: the usual shader hash, 0..1. The second pass works on a
    small argument; the GPU's sine of a large seed is coarse enough that the first alone left the
    top thousandth of the range (the stars) almost empty."""
    first_pass = frac(material, multiply(material, sine(material, seed_node, x, y), None, x + 100, y, const_b=43758.5453),
                      x + 200, y)
    second = add(material, multiply(material, first_pass, None, x + 300, y, const_b=91.7), None, x + 400, y, const_b=3.1)
    return frac(material, multiply(material, sine(material, second, x + 500, y), None, x + 600, y, const_b=43758.5453),
                x + 700, y)


def append(material, a_node, b_node, x=0, y=0):
    node = expr(material, "MaterialExpressionAppendVector", x, y, None, "Append")
    connect(a_node, "", node, "A")
    connect(b_node, "", node, "B")
    return node


def hash13(material, cells_node, x=0, y=0):
    """0..1 from a 3D integer cell, without sine (Dave Hoskins' hash13):
    p = frac(cell * 0.1031); p += dot(p, p.zyx + 31.32); frac((p.x + p.y) * p.z). The sine hash
    loses its top thousandth on the GPU, which is exactly the part stars and glints are cut from."""
    p = frac(material, multiply(material, cells_node, None, x, y, const_b=0.1031), x + 120, y)
    zyx = append(material, append(material, component_mask(material, p, b=True, x=x + 240, y=y + 60),
                                  component_mask(material, p, g=True, x=x + 240, y=y + 120), x + 360, y + 80),
                 component_mask(material, p, r=True, x=x + 240, y=y + 180), x + 480, y + 100)
    p = add(material, p, dot(material, p, add(material, zyx, None, x + 600, y + 100, const_b=31.32), x + 720, y + 60),
            x + 840, y)
    xy = add(material, component_mask(material, p, r=True, x=x + 960, y=y), component_mask(material, p, g=True, x=x + 960,
                                                                                          y=y + 60), x + 1080, y)
    return frac(material, multiply(material, xy, component_mask(material, p, b=True, x=x + 960, y=y + 120), x + 1200, y),
                x + 1320, y)


def mul_all(material, nodes, x=0, y=0):
    out = nodes[0]
    for i, node in enumerate(nodes[1:]):
        out = multiply(material, out, node, x + i * 120, y)
    return out


# --- functions -------------------------------------------------------------------------------


def _build_snow_function(mf):
    """BaseColor, Roughness, Coverage in; the same with snow on faces pointing up, plus the glints."""
    base = function_input(mf, "BaseColor", False, 0, -1600, -400, (0.2, 0.2, 0.2))
    rough = function_input(mf, "Roughness", True, 1, -1600, -250, 0.8)
    coverage = function_input(mf, "Coverage", True, 2, -1600, -100, 1.0)

    normal = expr(mf, "MaterialExpressionVertexNormalWS", -1600, 100)
    nz = component_mask(mf, normal, b=True, x=-1400, y=100)
    up = step(mf, nz, SNOW_NORMAL_Z, -1250, 100, sharpness=SNOW_EDGE)
    mask = multiply(mf, up, coverage, -700, 0)

    color = lerp(mf, base, constant3(mf, SNOW_COLOR, -800, -500), mask, -400, -400)
    roughness = lerp(mf, rough, None, mask, -400, -250, const_b=SNOW_ROUGHNESS)

    # Glints: a hash of 3 cm world cells, a few in a thousand lit.
    wp = world_position(mf, -1800, 400)
    cells = floor_node(mf, divide(mf, wp, None, -1650, 400, const_b=SNOW_SPARKLE_CELL), -1500, 400)
    glint = step(mf, hash13(mf, cells, -2900, 700), 1.0 - SNOW_SPARKLE_FRACTION, -750, 400, sharpness=2000.0)
    near = below(mf, expr(mf, "MaterialExpressionPixelDepth", -1200, 600, None, "PixelDepth"), SNOW_SPARKLE_DISTANCE,
                 -1000, 600, sharpness=1.0 / 500.0)
    glint = multiply(mf, glint, near, -600, 500)
    sparkle = SNOW_SPARKLE * EMISSIVE_INTENSITY_FACTOR
    emissive = multiply(mf, multiply(mf, glint, mask, -450, 400),
                        constant3(mf, (sparkle, sparkle, sparkle * 1.1), -450, 520), -300, 400)

    function_output(mf, "BaseColor", 0, color, 0, -400)
    function_output(mf, "Roughness", 1, roughness, 0, -250)
    function_output(mf, "Emissive", 2, emissive, 0, 400)
    function_output(mf, "Mask", 3, mask, 0, 100)


def _build_facade_function(mf):
    """A tenement facade from world position alone: window grid, storefront band, cornice.

    The horizontal facade coordinate is world XY projected on the face's own tangent (up crossed
    with the vertex normal), so every face of every footprint gets a straight grid whatever its
    angle and the grid restarts at each corner instead of smearing round it the way a world-aligned
    planar projection would. Z is world height. The top of the mesh (object bounds) puts the
    cornice under the parapet. Roofs and other flat faces get roof grey.
    """
    wall = function_input(mf, "WallColor", False, 0, -3000, -900, (0.2, 0.08, 0.06))
    trim = function_input(mf, "TrimColor", False, 1, -3000, -760, (0.55, 0.5, 0.4))
    lit_color = function_input(mf, "LitColor", False, 2, -3000, -620, WINDOW_LIT)
    lit_fraction = function_input(mf, "LitFraction", True, 3, -3000, -480, 0.35)
    glow = function_input(mf, "WindowGlow", True, 4, -3000, -340, 3.0)

    wp = world_position(mf, -3000, 0)
    x = component_mask(mf, wp, r=True, x=-2800, y=-60)
    y = component_mask(mf, wp, g=True, x=-2800, y=0)
    z = component_mask(mf, wp, b=True, x=-2800, y=60)
    normal = expr(mf, "MaterialExpressionVertexNormalWS", -3000, 250)
    nx = component_mask(mf, normal, r=True, x=-2800, y=200)
    ny = component_mask(mf, normal, g=True, x=-2800, y=260)
    nz = component_mask(mf, normal, b=True, x=-2800, y=320)

    # 1 on walls, 0 on roofs and parapet tops.
    vertical = below(mf, absolute(mf, nz, -2650, 320), 0.45, -2500, 320, sharpness=10.0)
    u = subtract(mf, multiply(mf, y, nx, -2600, 0), multiply(mf, x, ny, -2600, 80), -2450, 40)
    d = add(mf, multiply(mf, x, nx, -2600, 160), multiply(mf, y, ny, -2600, 220), -2450, 190)
    obj_z = component_mask(mf, expr(mf, "MaterialExpressionObjectPositionWS", -3000, 450), b=True, x=-2800, y=450)
    size_z = component_mask(mf, expr(mf, "MaterialExpressionObjectBounds", -3000, 520), b=True, x=-2800, y=520)
    top = add(mf, obj_z, multiply(mf, size_z, None, -2650, 520, const_b=0.5), -2500, 480)
    rel = subtract(mf, top, z, -2350, 400)
    tall = step(mf, top, STOREFRONT_MIN_TOP, -2350, 560)

    # Upper floors.
    du = multiply(mf, absolute(mf, subtract(mf, frac(mf, divide(mf, u, None, -2300, -200, const_b=BAY_PITCH), -2150, -200),
                                            None, -2000, -200, const_b=0.5), -1850, -200), None, -1700, -200, const_b=BAY_PITCH)
    vz = multiply(mf, frac(mf, divide(mf, z, None, -2300, -80, const_b=FLOOR_PITCH), -2150, -80), None, -2000, -80,
                  const_b=FLOOR_PITCH)
    win = multiply(mf, below(mf, du, WINDOW_W * 0.5, -1550, -240),
                   band(mf, vz, WINDOW_SILL, WINDOW_SILL + WINDOW_H, -1550, -120), -1000, -200)
    frame = multiply(mf, below(mf, du, WINDOW_W * 0.5 + FRAME, -1550, -360),
                     band(mf, vz, WINDOW_SILL - FRAME, WINDOW_SILL + WINDOW_H + FRAME * 1.5, -1550, -480), -1000, -400)
    upper = mul_all(mf, [step(mf, z, UPPER_FLOORS_FROM, -1550, 0), step(mf, rel, CORNICE_FROM_TOP[1] + 20.0, -1550, 60),
                         vertical], -1000, 0)

    # Ground floor: storefront glass and the sign band over it.
    sdu = multiply(mf, absolute(mf, subtract(mf, frac(mf, divide(mf, u, None, -2300, 700, const_b=STORE_PITCH), -2150, 700),
                                             None, -2000, 700, const_b=0.5), -1850, 700), None, -1700, 700, const_b=STORE_PITCH)
    ground = mul_all(mf, [below(mf, z, SIGN_TOP + 5.0, -1550, 900), tall, vertical], -1000, 900)
    store = mul_all(mf, [below(mf, sdu, STORE_W * 0.5, -1550, 700), band(mf, z, STORE_BOTTOM, STORE_TOP, -1550, 780), ground],
                    -800, 700)
    sign = mul_all(mf, [band(mf, z, STORE_TOP, SIGN_TOP, -1550, 1000), ground], -800, 1000)

    # Cornice under the parapet, a black ink line under it.
    cornice = mul_all(mf, [band(mf, rel, CORNICE_FROM_TOP[0], CORNICE_FROM_TOP[1], -1550, 1150), tall, vertical], -800, 1150)
    ink = mul_all(mf, [band(mf, rel, CORNICE_FROM_TOP[1], CORNICE_FROM_TOP[1] + CORNICE_INK, -1550, 1250), tall, vertical],
                  -800, 1250)

    # Each facade plane a little lighter or darker than its neighbours. The plane offset is floored to
    # a metre first: raw world position jitters in the low bits, and the hash turns that into noise.
    d_seed = multiply(mf, floor_node(mf, divide(mf, d, None, -2450, 1400, const_b=100.0), -2350, 1400), None, -2300, 1400,
                      const_b=1.37)
    tone = add(mf, multiply(mf, hash01(mf, d_seed, -2150, 1400), None, -1700, 1400, const_b=0.3), None, -1550, 1400,
               const_b=0.85)
    # A fine world-space noise reads as mortar joints/brick grain once the base wall colour is bright
    # enough to catch lamp light; a matching roughness ripple keeps it from looking like a flat tint.
    mortar = noise(mf, 0.02, -1550, 1480, out_min=0.88, out_max=1.15, levels=2, turbulence=False)
    tone = multiply(mf, tone, mortar, -1400, 1450)
    rough_ripple = noise(mf, 0.015, -1550, 1580, out_min=-0.08, out_max=0.12, levels=2, turbulence=False)
    brick_roughness = clamp01(mf, add(mf, rough_ripple, None, -1400, 1580, const_b=0.85), -1250, 1580)
    base = lerp(mf, constant3(mf, ROOF_COLOR, -1300, 1500), multiply(mf, wall, tone, -1300, 1400), vertical, -600, 1400)
    base = lerp(mf, base, trim, multiply(mf, frame, upper, -600, -400), -400, 1400)
    base = lerp(mf, base, trim, cornice, -250, 1400)
    base = lerp(mf, base, constant3(mf, INK, -400, 1560), ink, -100, 1400)
    base = lerp(mf, base, constant3(mf, SIGN_COLOR, -250, 1560), sign, 50, 1400)
    # A 30 cm band at the bottom of every floor, lighter than the field: a belt course/lintel line
    # so brick still reads as courses of masonry rather than a flat tinted slab in low light.
    belt = multiply(mf, band(mf, vz, 0.0, 30.0, -1550, 1350), vertical, -1350, 1350)
    base = lerp(mf, base, trim, multiply(mf, belt, None, -1200, 1350, const_b=0.4), 150, 1350)

    # Which windows are lit: a hash of the bay, the floor and the facade plane.
    bay = floor_node(mf, divide(mf, u, None, -2300, 1700, const_b=BAY_PITCH), -2150, 1700)
    storey = floor_node(mf, divide(mf, z, None, -2300, 1800, const_b=FLOOR_PITCH), -2150, 1800)
    seed = add(mf, add(mf, multiply(mf, bay, None, -2000, 1700, const_b=12.9898),
                       multiply(mf, storey, None, -2000, 1800, const_b=78.233), -1850, 1750), d_seed, -1700, 1750)
    lit_upper = clamp01(mf, multiply(mf, subtract(mf, lit_fraction, hash01(mf, seed, -1550, 1750), -1100, 1750), None, -950, 1750,
                                     const_b=1000.0), -800, 1750)
    shop = floor_node(mf, divide(mf, u, None, -2300, 1950, const_b=STORE_PITCH), -2150, 1950)
    shop_seed = add(mf, multiply(mf, shop, None, -2000, 1950, const_b=12.9898), d_seed, -1850, 1950)
    lit_shop = below(mf, hash01(mf, add(mf, shop_seed, None, -1700, 1950, const_b=3.1), -1550, 1950),
                     LIT_STORE_FRACTION, -1100, 1950, sharpness=1000.0)
    glass_upper = multiply(mf, win, upper, -800, -200)
    lit = add(mf, multiply(mf, glass_upper, lit_upper, -600, 1750), multiply(mf, store, lit_shop, -600, 1950), -450, 1850)
    glass = maximum(mf, glass_upper, store, -600, 1600)

    # Per-window warmth: a hash of the same bay/floor/plane seed picks 70% warm (LitColor), 20% cool
    # white, 10% TV-blue, decorrelated from the lit/unlit hash by a different constant offset. A second
    # hash jitters each lit window's brightness so no two are identical.
    color_seed = add(mf, seed, None, -1550, 2050, const_b=51.7)
    color_hash = hash01(mf, color_seed, -1400, 2050)
    cool_mask = band(mf, color_hash, 1.0 - WINDOW_COOL_FRACTION - WINDOW_TV_FRACTION, 1.0 - WINDOW_TV_FRACTION,
                     -1100, 2050)
    tv_mask = step(mf, color_hash, 1.0 - WINDOW_TV_FRACTION, -1100, 2150)
    win_color = lerp(mf, lit_color, constant3(mf, WINDOW_LIT_COOL, -900, 2100), cool_mask, -700, 2050)
    win_color = lerp(mf, win_color, constant3(mf, WINDOW_LIT_TV, -700, 2150), tv_mask, -500, 2100)

    jitter_seed = add(mf, seed, None, -1550, 2250, const_b=137.3)
    jitter = add(mf, multiply(mf, hash01(mf, jitter_seed, -1400, 2250), None, -1200, 2250,
                              const_b=WINDOW_JITTER_MAX - WINDOW_JITTER_MIN), None, -1050, 2250,
                const_b=WINDOW_JITTER_MIN)

    glass_color = lerp(mf, constant3(mf, WINDOW_GLASS, -450, 1650), multiply(mf, win_color, None, -450, 1700, const_b=0.3),
                       clamp01(mf, lit, -300, 1850), -250, 1650)
    base = lerp(mf, base, glass_color, glass, 200, 1400)
    emissive = multiply(mf, multiply(mf, multiply(mf, win_color, glow, -250, 2000), jitter, -50, 2050), lit, 100, 2000)
    roughness = lerp(mf, brick_roughness, None, glass, 200, 1600, const_b=0.2)

    function_output(mf, "BaseColor", 0, base, 500, 1400)
    function_output(mf, "Roughness", 1, roughness, 500, 1600)
    function_output(mf, "Emissive", 2, emissive, 500, 2000)


def ensure_snow_function():
    return ensure_material_function(MF_SNOW, _build_snow_function)


def ensure_facade_function():
    return ensure_material_function(MF_FACADE, _build_facade_function)


def snowed(material, color, roughness, coverage=None, extra_emissive=None, x=-400, y=0):
    """Runs (color, roughness) through MF_Snow and connects the result to the material's outputs."""
    snow = function_call(material, ensure_snow_function(), x, y)
    connect(color, "", snow, "BaseColor")
    connect(roughness, "", snow, "Roughness")
    connect(coverage if coverage is not None else constant(material, 1.0, x - 250, y + 150), "", snow, "Coverage")
    connect_property(snow, unreal.MaterialProperty.MP_BASE_COLOR, "BaseColor")
    connect_property(snow, unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")
    if extra_emissive is not None:
        total = add(material, extra_emissive, None, x + 250, y + 200)
        connect(snow, "Emissive", total, "B")
        connect_property(total, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        connect_property(snow, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive")
    return snow


# --- materials -------------------------------------------------------------------------------


def _patchy(material, scale, low, high, x, y):
    """World-noise coverage, clamped: low/high are the noise range before the clamp to 0..1. Plain
    (not turbulent) noise: turbulence folds it into thin ridges that read as cracks."""
    return clamp01(material, noise(material, scale, x, y, out_min=low, out_max=high, levels=2, turbulence=False),
                   x + 200, y)


def _build_facade(material):
    """The procedural facade (_look.FACADE_HLSL): masonry, windows with rooms behind them, storefronts, cornice; roofs
    go on through MF_Snow (mostly snow, dark patches where it has blown off or been cleared). MF_Facade, the painted
    grid this replaced, is no longer built."""
    import _look
    _look.build_facade(sys.modules[__name__], material)


def _build_window_lit(material):
    """The dressing's lit window cards: the facade's window, always lit, Color x Intensity like M_Emissive."""
    import _look
    _look.build_facade(sys.modules[__name__], material, card=True)


def _build_prop(material):
    """Flat Color / Roughness parameters under a snow layer: every clutter prop and the ground."""
    color = vector_param(material, "Color", (0.2, 0.2, 0.2), -900, -100)
    rough = scalar_param(material, "Roughness", 0.7, -900, 50)
    snowed(material, color, rough)


def _build_sidewalk_snow(material):
    """Concrete flags, a granite kerb, a trodden path with snow either side (_look.SIDEWALK_HLSL)."""
    import _look
    _look.build_ground(sys.modules[__name__], material, "sidewalk")


def _build_park_snow(material):
    """Drifted snow, the odd patch of grass showing (_look.PARK_HLSL)."""
    import _look
    _look.build_ground(sys.modules[__name__], material, "park")


def _build_street_asphalt(material):
    """Wet asphalt, wheel tracks through compacted snow, a plowed ridge at the kerb (_look.ROAD_HLSL)."""
    import _look
    _look.build_ground(sys.modules[__name__], material, "road")


def _build_lamp_head(material):
    """The street lamp head: the Color/Intensity glow on its underside only, black iron elsewhere,
    snow on top. Same parameter names as M_Emissive, so MI_StreetLamp only changes parent."""
    color, intensity = _emissive_params(material, -1300, -200)
    normal = expr(material, "MaterialExpressionVertexNormalWS", -1300, 200)
    down = below(material, component_mask(material, normal, b=True, x=-1100, y=200), -0.6, -950, 200, sharpness=10.0)
    glow = multiply(material, multiply(material, color, intensity, -900, -100), down, -700, 0)
    snowed(material, constant3(material, (0.02, 0.02, 0.02), -700, -300), constant(material, 0.5, -700, -200),
           extra_emissive=glow)


def _build_night_stars(material):
    """Additive stars on the inside of a sky sphere: a hash of the view direction in fine cells, so
    they hold still as the camera turns, fading out toward the horizon."""
    c.set_props(material, [("blend_mode", unreal.BlendMode.BLEND_ADDITIVE),
                           ("shading_model", unreal.MaterialShadingModel.MSM_UNLIT),
                           ("two_sided", True)], "M_NightStars")
    view = multiply(material, expr(material, "MaterialExpressionCameraVectorWS", -1500, 0), None, -1350, 0, const_b=-1.0)
    cells = floor_node(material, multiply(material, view, None, -1200, 0, const_b=STAR_CELLS), -1050, 0)
    star = step(material, hash13(material, cells, -2600, -300), 1.0 - STAR_FRACTION, -300, 0, sharpness=5000.0)
    # Some brighter than others.
    brightness = add(material, multiply(material, hash13(material, add(material, cells, None, -1050, 200, const_b=17.0),
                                                         -2600, 300), None, -300, 200, const_b=0.8), None, -150, 200,
                     const_b=0.2)
    fade = clamp01(material, add(material, multiply(material, component_mask(material, view, b=True, x=-1200, y=400),
                                                    None, -1050, 400, const_b=3.0), None, -900, 400, const_b=-0.15), -750, 400)
    tint = constant3(material, tuple(v * STAR_BRIGHTNESS for v in STAR_COLOR), -300, 500)
    connect_property(mul_all(material, [star, brightness, fade, tint], 0, 200), unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def ensure_city_look():
    """Every city look material and the facade instances. Returns a dict keyed by short name."""
    import _look
    c.ensure_directory(MATERIALS_PATH)
    ensure_snow_function()
    out = {
        "window_lit": ensure_look_material(M_WINDOW_LIT, _build_window_lit, instanced=True),
        "facade": ensure_look_material(M_FACADE, _build_facade),
        "prop": ensure_look_material(M_PROP, _build_prop, instanced=True),
        "sidewalk": ensure_look_material(M_SIDEWALK_SNOW, _build_sidewalk_snow),
        "park": ensure_look_material(M_PARK_SNOW, _build_park_snow),
        "asphalt": ensure_look_material(M_STREET_ASPHALT, _build_street_asphalt),
        "lamp_head": ensure_look_material(M_LAMP_HEAD, _build_lamp_head),
        "stars": ensure_look_material(M_NIGHT_STARS, _build_night_stars),
    }
    facades = {}
    for style, (wall, trim, lit_fraction) in sorted(FACADE_STYLES.items()):
        facades[style] = ensure_material_instance(
            mi_facade_path(style), out["facade"],
            vectors=[("WallColor", wall), ("TrimColor", trim), ("LitColor", WINDOW_LIT)],
            scalars=[("LitFraction", lit_fraction), ("WindowGlow", WINDOW_GLOW * EMISSIVE_INTENSITY_FACTOR),
                     ("Masonry", _look.MASONRY[style][0]), ("Painted", _look.MASONRY[style][1])])
    out["facades"] = facades
    return out


def ensure_prop_instance(name, rgb, roughness):
    """MI_Prop_<name> under M_Prop."""
    parent = ensure_look_material(M_PROP, _build_prop, instanced=True)
    return ensure_material_instance(MATERIALS_PATH + "/MI_Prop_" + name, parent,
                                    vectors=[("Color", rgb)], scalars=[("Roughness", roughness)])


# --------------------------------------------------------------------------------------
# interiors: the chapter interiors' walls, floors, furniture, glass and lamps (generate_interior.py).
# Same comic palette as the streets: cream and plum walls, dark boards, a deep red carpet, black
# iron, warm pendants and cool tubes. No snow: M_Prop's snow layer would settle on every desk.
# --------------------------------------------------------------------------------------

INTERIOR_BUILD = "interior-1"          # a different value rebuilds every interior master once
INTERIOR_TAG = "HawkeyeInteriorBuild"

M_INT_PLASTER = MATERIALS_PATH + "/M_IntPlaster"
M_INT_WOOD_FLOOR = MATERIALS_PATH + "/M_IntWoodFloor"
M_INT_CARPET = MATERIALS_PATH + "/M_IntCarpet"
M_INT_CONCRETE = M_CONCRETE_FLOOR      # the existing concrete, grout grid and all
M_INT_WOOD = MATERIALS_PATH + "/M_IntWood"
M_INT_GLASS = MATERIALS_PATH + "/M_IntGlass"
M_INT_PROP = MATERIALS_PATH + "/M_IntProp"

PLASTER_CREAM = (0.62, 0.53, 0.38)
PLANK_WIDTH = 20.0                     # cm across a board, along world Y
PLANK_LENGTH = 120.0                   # cm along a board, along world X; rows are staggered by a hash
PLANK_DARK = (0.10, 0.045, 0.02)
PLANK_LIGHT = (0.22, 0.11, 0.045)
PLANK_SEAM = (0.02, 0.01, 0.006)
CARPET_RED = (0.16, 0.018, 0.026)

# Wall paints, furniture woods, props and lamps: (name, colour, roughness). MI_IntPlaster_<name> etc.
INTERIOR_PAINTS = {
    "Cream": (PLASTER_CREAM, 0.85),
    "Plum": ((0.12, 0.045, 0.14), 0.8),
    "Slate": ((0.16, 0.17, 0.2), 0.85),
    "Stone": ((0.2, 0.19, 0.18), 0.9),
    "Outside": ((0.05, 0.04, 0.045), 0.9),
}
INTERIOR_WOODS = {
    "Door": ((0.2, 0.09, 0.035), 0.5),
    "Dark": ((0.07, 0.035, 0.02), 0.45),
    "Honey": ((0.36, 0.2, 0.08), 0.5),
}
INTERIOR_PROPS = {
    "Black": ((0.015, 0.015, 0.018), 0.4),
    "Iron": ((0.03, 0.03, 0.033), 0.35),
    "Cream": ((0.55, 0.48, 0.36), 0.8),
    "Purple": ((0.2, 0.06, 0.32), 0.6),
    "Velvet": ((0.1, 0.012, 0.02), 1.0),
    "Crate": ((0.25, 0.16, 0.08), 0.9),
    "Cardboard": ((0.3, 0.21, 0.12), 0.95),
    "Brass": ((0.45, 0.3, 0.1), 0.3),
    "Linen": ((0.5, 0.48, 0.44), 0.95),
    "Shade": ((0.03, 0.025, 0.03), 0.6),
    "Leaf": ((0.025, 0.07, 0.03), 0.8),
}
# Emissive strengths before EMISSIVE_INTENSITY_FACTOR, in the street lamps' range (MI_StreetLamp is 2).
INTERIOR_LAMPS = {
    "TubeCool": ((0.8, 0.9, 1.0), 1.5),
    "PendantWarm": ((1.0, 0.66, 0.34), 1.2),
    "ExitSign": ((0.15, 1.0, 0.35), 2.0),
    "CaseGlow": ((1.0, 0.86, 0.62), 0.6),
    "StageWash": ((0.62, 0.25, 1.0), 2.0),
}


def mi_int_path(kind, name):
    """MI_IntPlaster_Cream, MI_IntWood_Door, MI_IntProp_Iron, MI_IntLamp_TubeCool."""
    return MATERIALS_PATH + "/MI_Int{0}_{1}".format(kind, name)


def _int_tagged(asset):
    try:
        return unreal.EditorAssetLibrary.get_metadata_tag(asset, INTERIOR_TAG) == INTERIOR_BUILD
    except Exception:  # noqa: BLE001
        return False


def ensure_interior_material(full_path, build_fn):
    """ensure_material, rebuilt once whenever INTERIOR_BUILD changes."""
    existing = c.load_or_none(full_path)
    if existing is not None and _int_tagged(existing):
        return ensure_material(full_path, build_fn)
    material = ensure_material(full_path, build_fn, rebuild=True)
    if material is not None:
        unreal.EditorAssetLibrary.set_metadata_tag(material, INTERIOR_TAG, INTERIOR_BUILD)
        c.save(material)
    return material


def _build_int_plaster(material):
    """Color, very slightly mottled: fine grain and big soft patches, both only ever darkening a little."""
    color = vector_param(material, "Color", PLASTER_CREAM, -1000, -200)
    fine = noise(material, 0.08, -1000, 0, out_min=0.93, out_max=1.0, levels=2, turbulence=False)
    patches = noise(material, 0.006, -1000, 200, out_min=0.88, out_max=1.0, levels=2, turbulence=False)
    connect_property(mul_all(material, [color, fine, patches], -600, 0), unreal.MaterialProperty.MP_BASE_COLOR)
    rough = scalar_param(material, "Roughness", 0.85, -600, 250)
    connect_property(rough, unreal.MaterialProperty.MP_ROUGHNESS)


def _build_int_wood_floor(material):
    """Boards laid along world X: PLANK_WIDTH rows along Y, each row's joints shifted by a hash of the row,
    each board its own tone between PLANK_DARK and PLANK_LIGHT, dark seams, a satin finish."""
    wp = world_position(material, -2200, 0)
    x = component_mask(material, wp, r=True, x=-2000, y=-100)
    y = component_mask(material, wp, g=True, x=-2000, y=100)
    rows = divide(material, y, None, -1850, 100, const_b=PLANK_WIDTH)
    row = floor_node(material, rows, -1700, 100)
    shift = multiply(material, hash01(material, add(material, row, None, -1600, 250, const_b=0.37), -1500, 250), None,
                     -700, 250, const_b=PLANK_LENGTH)
    along = divide(material, add(material, x, shift, -550, -100), None, -400, -100, const_b=PLANK_LENGTH)
    board = floor_node(material, along, -250, -100)
    tone = hash01(material, add(material, multiply(material, board, None, -150, -250, const_b=7.13),
                                multiply(material, row, None, -150, -350, const_b=1.7), 0, -300), 100, -300)
    base = lerp(material, constant3(material, PLANK_DARK, 800, -500), constant3(material, PLANK_LIGHT, 800, -400), tone,
                1000, -400)
    grain = noise(material, 0.12, 800, -200, out_min=0.8, out_max=1.0, levels=2, turbulence=False)
    stained = multiply(material, base, grain, 1150, -300)
    seam_y = step(material, absolute(material, subtract(material, frac(material, rows, -1550, 450), None, -1400, 450,
                                                        const_b=0.5), -1250, 450), 0.46, -1100, 450)
    seam_x = step(material, absolute(material, subtract(material, frac(material, along, -250, 50), None, -100, 50,
                                                        const_b=0.5), 50, 50), 0.493, 200, 50)
    seam = maximum(material, seam_y, seam_x, 600, 200)
    connect_property(lerp(material, stained, constant3(material, PLANK_SEAM, 1150, 100), seam, 1350, -100),
                     unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(lerp(material, None, None, seam, 1350, 250, const_a=0.42, const_b=0.9),
                     unreal.MaterialProperty.MP_ROUGHNESS)


def _build_int_carpet(material):
    """Deep red pile: a fine grain and a faint large mottle, fully rough."""
    fine = noise(material, 0.35, -900, 0, out_min=0.75, out_max=1.0, levels=2, turbulence=False)
    mottle = noise(material, 0.01, -900, 200, out_min=0.85, out_max=1.0, levels=2, turbulence=False)
    color = vector_param(material, "Color", CARPET_RED, -900, -200)
    connect_property(mul_all(material, [color, fine, mottle], -500, 0), unreal.MaterialProperty.MP_BASE_COLOR)
    set_scalar_property(material, 1.0, unreal.MaterialProperty.MP_ROUGHNESS, -500, 250)


def _build_int_wood(material):
    """Painted or stained wood for doors and furniture: Color with a soft grain."""
    color = vector_param(material, "Color", INTERIOR_WOODS["Door"][0], -900, -200)
    grain = noise(material, 0.06, -900, 0, out_min=0.78, out_max=1.0, levels=3, turbulence=False)
    connect_property(multiply(material, color, grain, -500, -100), unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(scalar_param(material, "Roughness", 0.5, -500, 200), unreal.MaterialProperty.MP_ROUGHNESS)


def _build_int_glass(material):
    """Display-case and window glass: translucent, a pale blue-green tint, glossy, a whisper of emissive
    so the edges still read against a dark room."""
    c.set_props(material, [("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT), ("two_sided", True)], "M_IntGlass")
    try:
        material.set_editor_property("translucency_lighting_mode",
                                     unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    except Exception as exc:  # noqa: BLE001
        c.log_error("translucency_lighting_mode M_IntGlass", exc)
    connect_property(constant3(material, (0.55, 0.7, 0.72), -600, -200), unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(constant3(material, (0.004, 0.006, 0.007), -600, -60), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    set_scalar_property(material, 0.05, unreal.MaterialProperty.MP_ROUGHNESS, -600, 80)
    set_scalar_property(material, 0.9, unreal.MaterialProperty.MP_SPECULAR, -600, 180)
    set_scalar_property(material, 0.18, unreal.MaterialProperty.MP_OPACITY, -600, 280)


def _build_int_prop(material):
    """Flat Color / Roughness, no snow: every indoor prop."""
    connect_property(vector_param(material, "Color", (0.2, 0.2, 0.2), -600, -200), unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(scalar_param(material, "Roughness", 0.7, -600, 0), unreal.MaterialProperty.MP_ROUGHNESS)


def ensure_interior_materials():
    """Every interior master and instance. Returns {"plaster": {name: MI}, "wood": {...}, "prop": {...},
    "lamp": {...}, "wood_floor": M, "carpet": M, "concrete": M, "glass": M}."""
    c.ensure_directory(MATERIALS_PATH)
    out = {
        "wood_floor": ensure_interior_material(M_INT_WOOD_FLOOR, _build_int_wood_floor),
        "carpet": ensure_interior_material(M_INT_CARPET, _build_int_carpet),
        "concrete": ensure_material(M_INT_CONCRETE, _build_concrete_floor),
        "glass": ensure_interior_material(M_INT_GLASS, _build_int_glass),
    }
    plaster = ensure_interior_material(M_INT_PLASTER, _build_int_plaster)
    wood = ensure_interior_material(M_INT_WOOD, _build_int_wood)
    prop = ensure_interior_material(M_INT_PROP, _build_int_prop)
    emissive = ensure_material(M_EMISSIVE, _build_emissive)
    for key, parent, table, kind in (("plaster", plaster, INTERIOR_PAINTS, "Plaster"),
                                     ("wood", wood, INTERIOR_WOODS, "Wood"),
                                     ("prop", prop, INTERIOR_PROPS, "Prop")):
        out[key] = {name: ensure_material_instance(mi_int_path(kind, name), parent, vectors=[("Color", rgb)],
                                                   scalars=[("Roughness", rough)])
                    for name, (rgb, rough) in sorted(table.items())}
    out["lamp"] = {name: ensure_material_instance(mi_int_path("Lamp", name), emissive,
                                                  vectors=[(EMISSIVE_COLOR_PARAM, rgb)],
                                                  scalars=[(EMISSIVE_INTENSITY_PARAM, strength * EMISSIVE_INTENSITY_FACTOR)])
                   for name, (rgb, strength) in sorted(INTERIOR_LAMPS.items())}
    return out


# --------------------------------------------------------------------------------------
# public entry points
# --------------------------------------------------------------------------------------



def ensure_surface_materials():
    """M_Concrete / M_ConcreteFloor / M_SteelPainted. Returns a dict keyed by short name."""
    out = {}
    for key, path, build in (
        ("concrete", M_CONCRETE, _build_concrete),
        ("concrete_floor", M_CONCRETE_FLOOR, _build_concrete_floor),
        ("steel", M_STEEL_PAINTED, _build_steel_painted),
    ):
        try:
            out[key] = ensure_material(path, build)
        except Exception as exc:  # noqa: BLE001
            c.log_error("ensure_surface_materials " + path, exc)
            out[key] = None
    return out


def ensure_light_materials(intensity_factor=None):
    """M_Emissive + M_FluorescentFlicker and the three lamp instances.

    ``intensity_factor`` scales the tuned strengths below. Leave it out: the default is
    EMISSIVE_INTENSITY_FACTOR, which is the whole point of that constant living up there.
    """
    if intensity_factor is None:
        intensity_factor = EMISSIVE_INTENSITY_FACTOR
    out = {}
    try:
        out["emissive"] = ensure_material(M_EMISSIVE, _build_emissive)
    except Exception as exc:  # noqa: BLE001
        c.log_error("ensure_light_materials " + M_EMISSIVE, exc)
        out["emissive"] = None

    try:
        out["flicker"] = ensure_material(M_FLUORESCENT_FLICKER, _build_fluorescent_flicker)
    except Exception as exc:  # noqa: BLE001
        c.log_error("ensure_light_materials " + M_FLUORESCENT_FLICKER, exc)
        out["flicker"] = None

    instances = (
        # Emissive strength is in the same ballpark as the lights themselves, because the
        # scene is graded several stops down - a tube at 8 would read as dark plastic.
        # These are the -4.5 EV values; intensity_factor scales them for other presets.
        ("tube", MI_FLUORESCENT_TUBE, (0.85, 0.90, 1.00), 150.0),
        ("red", MI_RED_EMERGENCY, (1.00, 0.05, 0.02), 250.0),
        # A screen left on: bright enough to glow, too dim to light the room.
        ("monitor", MI_MONITOR, (0.20, 0.55, 1.00), 25.0),
    )
    for key, path, rgb, strength in instances:
        try:
            out[key] = ensure_material_instance(
                path,
                out.get("emissive"),
                vectors=[(EMISSIVE_COLOR_PARAM, rgb)],
                scalars=[(EMISSIVE_INTENSITY_PARAM, strength * intensity_factor)],
            )
        except Exception as exc:  # noqa: BLE001
            c.log_error("ensure_light_materials " + path, exc)
            out[key] = None
    return out


def ensure_all(intensity_factor=None):
    """Every interior surface and lamp material, in one dict."""
    materials = {}
    materials.update(ensure_surface_materials())
    materials.update(ensure_light_materials(intensity_factor))
    return materials


def run(intensity_factor=None):
    c.ensure_directory(MATERIALS_PATH)
    return ensure_all(intensity_factor)


if __name__ == "__main__":
    run()
    c.print_summary("materials")
