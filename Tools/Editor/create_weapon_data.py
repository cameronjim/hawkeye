"""Create the weapon, bow and arrow data and wire it into the player.

    /Game/Blueprints/Weapons/DA_Weapon_Hands      melee, 15 damage: left click while no bow is owned
    /Game/Blueprints/Weapons/M_Bow                dark purple, the placeholder bow's material, with a
                                                  purple glow round the grip (|z| under 5 cm)
    /Game/Blueprints/Weapons/M_ArrowNock          a Color parameter that also glows: the nocks
    /Game/Blueprints/Weapons/SM_Bow_Placeholder   a 120 cm bow from three cylinders (riser and two
                                                  limbs raked back to tips at x -12, z +-60); +X is
                                                  where the arrow goes, grip at the origin. The
                                                  string is drawn at runtime by UBowComponent.
    /Game/Blueprints/Weapons/BP_Arrow_Standard    parent AArrowProjectile
    /Game/Blueprints/Weapons/BP_Arrow_Grapple     parent AGrappleArrowProjectile
    /Game/Blueprints/Weapons/DA_Bow_Kate          UBowDefinition, 0.8 s draw
    /Game/Blueprints/Weapons/DA_Bow_Clint         UBowDefinition, 1.0 s draw (unused until Clint)
    /Game/Blueprints/Weapons/DA_Bow_Archer        UBowDefinition for Barney's archers (BP_Archer): 1.2 s
                                                  draw, 5000 cm/s, no perfect bonus, no headshot bonus
    /Game/Blueprints/Weapons/DA_Arrow_Trickshot   UArrowDefinition, 30 damage, black shaft and purple
                                                  vanes; picked up as DA_Arrow_Standard, toast
                                                  "Trickshot's arrow" the first time
    /Game/Blueprints/Weapons/DA_Arrow_Standard    UArrowDefinition, slot 1, 40 damage, cap 30
    /Game/Blueprints/Weapons/DA_Arrow_Grapple     UArrowDefinition, slot 2, cap 6, OnHitEffect Grapple
    /Game/Blueprints/Weapons/DA_Arrow_Putty       slot 3, 10 damage, cap 4, Putty (claude-docs/
                                                  gameplay-semantics.md, "trick arrows")
    /Game/Blueprints/Weapons/DA_Arrow_Bola        slot 4, 10 damage, cap 4, Bola, recoverable
    /Game/Blueprints/Weapons/DA_Arrow_Smoke       slot 5, 0 damage, cap 3, Smoke
    /Game/Blueprints/Weapons/DA_Arrow_EMP         slot 6, 0 damage, cap 3, EMP
    /Game/Blueprints/Weapons/DA_Arrow_Explosive   slot 7, 80 damage (the blast's, falling off to 0 at
                                                  400 cm), cap 2, Explosive
    /Game/Blueprints/Weapons/DA_Arrow_Confetti    slot 5 (the smoke's: Act 1 never grants smoke), 0 damage,
                                                  cap 3, Smoke with EffectClass BP_Effect_Confetti: chapter 4's
                                                  unlock (claude-docs/gameplay-semantics.md, "Bosses")
    /Game/Blueprints/Weapons/BP_Effect_Confetti   child of ASmokeCloud retuned by its defaults: 350 cm for 2.5 s
                                                  (thugs in it blinded a moment); its look is NS_PaperBurst
    /Game/Blueprints/Weapons/M_ArrowFx            translucent Color/Opacity: the smoke cloud's puffs
    /Game/Blueprints/Weapons/M_ArrowGlow          additive unlit Color x Intensity: the EMP ring and
                                                  the explosive's fireball

Then:

    BP_HawkeyeCharacter.InventoryComponent.HandsDefinition         = DA_Weapon_Hands
    BP_HawkeyeCharacter.InventoryComponent.StandardArrowDefinition = DA_Arrow_Standard

Property names come from Source/Hawkeye/Combat/{Weapon,Bow,Arrow}Definition.h. Idempotent: an
existing asset keeps its values and is only re-saved when a field is actually different; the
mesh is built once and only its material is checked afterwards.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402

WEAPON_PATH = "/Game/Blueprints/Weapons"
PLAYER_PATH = "/Game/Blueprints/Player"

BOW_MATERIAL = "M_Bow"
BOW_MATERIAL_RGB = (0.10, 0.03, 0.16)
BOW_GRIP_HALF = 5.0                # cm either side of the grip centre (the mesh origin) that glows
BOW_GLOW_RGB = (0.55, 0.15, 1.0)
BOW_GLOW = 1.2                     # before _materials.EMISSIVE_INTENSITY_FACTOR
NOCK_MATERIAL = "M_ArrowNock"
NOCK_GLOW = 1.5                    # times the nock's Color, before the factor
FX_MATERIAL = "M_ArrowFx"
FX_SELF_LIGHT = 0.03               # smoke glows this much of its Color so it reads between lamps
FX_EDGE_EXPONENT = 2.0             # how fast a puff thins toward its silhouette (Fresnel exponent)
FX_DEPTH_FADE = 150.0              # cm over which a puff fades where it meets the ground or a wall
GLOW_MATERIAL = "M_ArrowGlow"
# Rebuilt when this changes, independent of the city look build.
FX_BUILD_TAG = "HawkeyeArrowFxBuild"
FX_BUILD = "fx-2"
BOW_MESH = "SM_Bow_Placeholder"

# Palm of the left hand on the UEFN mannequin (a socket on hand_l, found by introspecting
# SKM_UEFN_Mannequin): the grip sits in it while drawing.
KATE_HAND_SOCKET = "palm_l_Socket"


def enum_value(enum_name, member):
    enum = getattr(unreal, enum_name, None)
    return getattr(enum, member, None) if enum is not None else None


def bow_values(name, mesh):
    """(property, value) pairs for one bow, as a plain table so the numbers are readable."""
    common = [
        ("min_draw_fraction", 0.25),
        ("max_speed", 6000.0),
        ("min_speed_fraction", 0.4),
        ("min_damage_fraction", 0.4),
        ("min_spread", 0.5),
        ("max_spread", 4.0),
        ("perfect_window_seconds", 0.1),
        ("perfect_bonus", 0.25),
        ("headshot_multiplier", 3.0),
        ("bow_mesh", mesh),
        ("hand_socket", KATE_HAND_SOCKET),
    ]
    if name == "DA_Bow_Kate":
        return [("display_name", "Kate's recurve"), ("full_draw_seconds", 0.8)] + common
    if name == "DA_Bow_Clint":
        return [("display_name", "Clint's recurve"), ("full_draw_seconds", 1.0)] + common
    if name == "DA_Bow_Archer":
        # An AI release is always at full draw: no perfect bonus, and no headshot bonus so 30 is 30.
        # No hand socket: with no draw animation the thug's hand hangs at his hip, and arrows from there
        # hit his own parapet; without one they leave from the bow's launch offset at chest height.
        archer = dict(common)
        archer.update({"max_speed": 5000.0, "min_spread": 1.0, "perfect_bonus": 0.0, "headshot_multiplier": 1.0,
                       "hand_socket": "None"})
        return [("display_name", "Trickshot crew recurve"), ("full_draw_seconds", 1.2)] + list(archer.items())
    return []


# Barney's crew: black shafts, Kate-bright purple vanes and nock, so one stuck in a wall reads as his.
TRICKSHOT_SHAFT = (0.02, 0.02, 0.025)
TRICKSHOT_VANES = (0.55, 0.08, 0.95)


def arrow_values(name, projectile_classes):
    if name == "DA_Arrow_Standard":
        return [
            ("display_name", "Standard arrow"),
            ("short_name", "Arrow"),
            ("slot", 1),
            ("projectile_class", projectile_classes.get("BP_Arrow_Standard")),
            ("damage", 40.0),
            ("cap", 30),
            ("recoverable", True),
            ("on_hit_effect", enum_value("ArrowHitEffect", "NONE")),
        ]
    if name == "DA_Arrow_Grapple":
        return [
            ("display_name", "Grapple arrow"),
            ("short_name", "Grapple"),
            ("slot", 2),
            ("projectile_class", projectile_classes.get("BP_Arrow_Grapple")),
            ("damage", 0.0),
            ("cap", 6),
            ("recoverable", True),
            ("on_hit_effect", enum_value("ArrowHitEffect", "GRAPPLE")),
        ]
    if name == "DA_Arrow_Trickshot":
        return [
            ("display_name", "Trickshot's arrow"),
            ("short_name", "Trickshot"),
            ("slot", 1),
            ("projectile_class", projectile_classes.get("BP_Arrow_Standard")),
            ("damage", 30.0),
            ("cap", 30),
            ("recoverable", True),
            ("on_hit_effect", enum_value("ArrowHitEffect", "NONE")),
            ("recover_as", projectile_classes.get("DA_Arrow_Standard")),
            ("pickup_toast", "Trickshot's arrow"),
            ("override_colors", True),
            ("shaft_color", unreal.LinearColor(*TRICKSHOT_SHAFT, 1.0)),
            ("fletching_color", unreal.LinearColor(*TRICKSHOT_VANES, 1.0)),
            ("nock_color", unreal.LinearColor(*TRICKSHOT_VANES, 1.0)),
        ]
    trick = TRICK_ARROWS.get(name)
    if trick is not None:
        display, short, slot, damage, cap, recoverable, effect = trick
        values = [
            ("display_name", display),
            ("short_name", short),
            ("slot", slot),
            ("projectile_class", projectile_classes.get("BP_Arrow_Standard")),
            ("damage", damage),
            ("cap", cap),
            ("recoverable", recoverable),
            ("on_hit_effect", enum_value("ArrowHitEffect", effect)),
        ]
        if name in EFFECT_CHILDREN:
            values.append(("effect_class", projectile_classes.get(EFFECT_CHILDREN[name][0])))
        return values
    return []


# The trick arrows (claude-docs/gameplay-semantics.md, "trick arrows"), in quiver slot order. They
# fly like a standard arrow; what they do on landing is the AArrowEffect their OnHitEffect names.
# name: (display name, short name, slot, damage, cap, recoverable, EArrowHitEffect member)
TRICK_ARROWS = {
    # The story's "sticky foam" (docs/chapters/ACT1.md): the putty arrow, renamed on screen only; chapter 2 unlocks it.
    "DA_Arrow_Putty": ("Sticky foam arrow", "Foam", 3, 10.0, 4, False, "PUTTY"),
    "DA_Arrow_Bola": ("Bola arrow", "Bola", 4, 10.0, 4, True, "BOLA"),
    "DA_Arrow_Smoke": ("Smoke arrow", "Smoke", 5, 0.0, 3, False, "SMOKE"),
    "DA_Arrow_EMP": ("EMP arrow", "EMP", 6, 0.0, 3, False, "EMP"),
    "DA_Arrow_Explosive": ("Explosive arrow", "Explosive", 7, 80.0, 2, False, "EXPLOSIVE"),
    "DA_Arrow_Confetti": ("Confetti arrow", "Confetti", 5, 0.0, 3, False, "SMOKE"),
}
ARROW_NAMES = ["DA_Arrow_Standard", "DA_Arrow_Grapple"] + list(TRICK_ARROWS)

# Trick arrows whose effect is a Blueprint child of the stock class, retuned by its defaults: arrow -> (Blueprint,
# parent C++ class, [(property, value)]).
EFFECT_CHILDREN = {
    "DA_Arrow_Confetti": ("BP_Effect_Confetti", "SmokeCloud",
                          [("radius", 350.0), ("life_seconds", 2.5), ("grow_seconds", 0.2), ("fade_seconds", 0.6),
                           ("smoke_color", unreal.LinearColor(0.75, 0.2, 0.35, 1.0)), ("puff_opacity", 0.35)]),
}


def weapon_values(name):
    if name == "DA_Weapon_Hands":
        return [
            ("display_name", "Fists"),
            ("short_name", "Fists"),
            ("damage", 15.0),
            ("magazine_size", 0),
            ("default_reserve", 0),
            ("is_melee", True),
            ("melee_range", 120.0),
            ("melee_cooldown", 0.6),
            ("stagger_on_hit", True),
        ]
    return []


def data_asset_factory(data_asset_class):
    factory = c.new_factory("DataAssetFactory")
    if factory is None:
        return None
    c.set_props(factory, [("data_asset_class", data_asset_class)], "DataAssetFactory")
    return factory


def same_value(current, wanted):
    """Property comparison that copes with FText, soft pointers and enums reading back oddly."""
    if current is None:
        return wanted is None
    try:
        if current == wanted:
            return True
    except Exception:  # noqa: BLE001 - some struct types refuse ==
        pass

    # A float property round-trips through single precision, so 0.6 comes back as
    # 0.6000000238 and a string comparison would rewrite the asset on every run.
    if isinstance(wanted, float) and isinstance(current, (int, float)):
        return abs(float(current) - wanted) < 1e-4
    if isinstance(wanted, unreal.LinearColor) and isinstance(current, unreal.LinearColor):
        return all(abs(getattr(current, ch) - getattr(wanted, ch)) < 1e-4 for ch in ("r", "g", "b", "a"))
    # A hard object reference compares by path (two Python handles to one asset are not ==).
    if isinstance(wanted, unreal.Object) and isinstance(current, unreal.Object):
        return current.get_path_name() == wanted.get_path_name()

    return str(current) == str(wanted)


def create_data_asset(name, class_name, values):
    """Create or correct one data asset from (property, value) pairs. None values are skipped."""
    full = c.asset_path(WEAPON_PATH, name)
    cls = c.find_class(class_name, "/Script/Hawkeye." + class_name)
    if cls is None:
        c.log("FAILED", full, "U{0} not exposed to Python".format(class_name))
        return None

    asset, created = c.create_asset(name, WEAPON_PATH, cls, data_asset_factory(cls), quiet=True)
    if asset is None:
        return None

    wanted = [(prop, value) for prop, value in values if value is not None]
    changed = []
    for prop, value in wanted:
        try:
            if same_value(asset.get_editor_property(prop), value):
                continue
        except Exception:  # noqa: BLE001 - set_props reports a missing property
            pass
        if c.set_props(asset, [(prop, value)], name):
            changed.append(prop)

    if created:
        c.save(asset)
        c.log("created", full, "{0} fields".format(len(wanted)))
    elif changed:
        c.save(asset)
        c.log("updated", full, ", ".join(changed))
    else:
        c.log("exists", full)
    return asset


# --- the placeholder bow mesh ------------------------------------------------------------------


def first(result):
    return result[0] if isinstance(result, tuple) else result


def _build_bow(material):
    """Dark purple, roughness 0.45, and a band of purple glow round the grip in local space."""
    import _materials as m  # noqa: PLC0415

    m.connect_property(m.constant3(material, BOW_MATERIAL_RGB, -600, -200), unreal.MaterialProperty.MP_BASE_COLOR)
    m.set_scalar_property(material, 0.45, unreal.MaterialProperty.MP_ROUGHNESS, -600, 0)
    local = m.expr(material, "MaterialExpressionLocalPosition", -1300, 200, None, "LocalPosition")
    height = m.absolute(material, m.component_mask(material, local, b=True, x=-1150, y=200), -1000, 200)
    grip = m.below(material, height, BOW_GRIP_HALF, -850, 200, 2.0)
    glow = tuple(v * BOW_GLOW * m.EMISSIVE_INTENSITY_FACTOR for v in BOW_GLOW_RGB)
    m.connect_property(m.multiply(material, grip, m.constant3(material, glow, -500, 300), -300, 200),
                       unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _build_arrow_nock(material):
    """Color (the arrow sets it) as the base, and the same colour glowing."""
    import _materials as m  # noqa: PLC0415

    color = m.vector_param(material, "Color", (0.45, 0.1, 0.75), -700, -100)
    m.connect_property(color, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(m.multiply(material, color, None, -400, 100, const_b=NOCK_GLOW * m.EMISSIVE_INTENSITY_FACTOR),
                       unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    m.set_scalar_property(material, 0.4, unreal.MaterialProperty.MP_ROUGHNESS, -400, 250)


def _build_arrow_fx(material):
    """Lit translucent Color at Opacity with a little self light: the smoke's puffs. Each puff thins
    to nothing at its silhouette and where it meets the ground, so a cluster of spheres reads as one
    soft cloud rather than a heap of balls."""
    import _materials as m  # noqa: PLC0415

    c.set_props(material, [("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)], FX_MATERIAL)
    color = m.vector_param(material, "Color", (0.3, 0.31, 0.34), -900, -100)
    opacity = m.scalar_param(material, "Opacity", 0.5, -900, 200)
    m.connect_property(color, unreal.MaterialProperty.MP_BASE_COLOR)
    self_light = m.multiply(material, color, None, -400, 50, const_b=FX_SELF_LIGHT * m.EMISSIVE_INTENSITY_FACTOR)
    m.connect_property(self_light, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    fresnel = m.expr(material, "MaterialExpressionFresnel", -1100, 350, [("exponent", FX_EDGE_EXPONENT)], "Fresnel")
    facing = m.subtract(material, m.constant(material, 1.0, -950, 450), fresnel, -800, 350)
    soft = m.multiply(material, opacity, m.multiply(material, facing, facing, -650, 350), -500, 250)
    depth = m.expr(material, "MaterialExpressionDepthFade", -350, 250, [("fade_distance_default", FX_DEPTH_FADE)],
                   "DepthFade")
    m.connect(soft, "", depth, "Opacity")
    m.connect_property(depth, unreal.MaterialProperty.MP_OPACITY)
    m.set_scalar_property(material, 1.0, unreal.MaterialProperty.MP_ROUGHNESS, -400, 450)


def ensure_fx_material(full_path, build_fn):
    """An effect material, rebuilt whenever FX_BUILD changes; otherwise left alone."""
    import _materials as m  # noqa: PLC0415

    existing = c.load_or_none(full_path)
    try:
        current = unreal.EditorAssetLibrary.get_metadata_tag(existing, FX_BUILD_TAG) if existing else None
    except Exception:  # noqa: BLE001
        current = None
    if existing is not None and current == FX_BUILD:
        c.log("exists", full_path)
        return existing
    material = m.ensure_material(full_path, build_fn, rebuild=existing is not None)
    if material is not None:
        unreal.EditorAssetLibrary.set_metadata_tag(material, FX_BUILD_TAG, FX_BUILD)
        c.save(material)
    return material


def _build_arrow_glow(material):
    """Additive, unlit: Color times Intensity glowing. The EMP ring and the fireball."""
    import _materials as m  # noqa: PLC0415

    c.set_props(material, [("blend_mode", unreal.BlendMode.BLEND_ADDITIVE),
                           ("shading_model", unreal.MaterialShadingModel.MSM_UNLIT),
                           ("two_sided", True)], GLOW_MATERIAL)
    color = m.vector_param(material, "Color", (0.25, 0.6, 1.0), -900, -100)
    intensity = m.scalar_param(material, "Intensity", 1.0, -900, 150)
    glow = m.multiply(material, m.multiply(material, color, intensity, -650, 0), None, -400, 0,
                      const_b=m.EMISSIVE_INTENSITY_FACTOR)
    m.connect_property(glow, unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def build_bow_mesh():
    """Riser, two limbs raked back to the string tips. cm, +X forward, +Z up the bow."""
    prim = unreal.GeometryScript_Primitives
    options = unreal.GeometryScriptPrimitiveOptions()
    base = unreal.GeometryScriptPrimitiveOriginMode.BASE
    mesh = unreal.DynamicMesh()

    def cylinder(location, pitch, radius, height):
        xf = unreal.Transform(location=unreal.Vector(*location),
                              rotation=unreal.Rotator(roll=0.0, pitch=pitch, yaw=0.0))
        prim.append_cylinder(mesh, options, xf, radius, height, 8, 0, True, base)

    # Riser from z -18 to 18, the grip in the middle.
    cylinder((0.0, 0.0, -18.0), 0.0, 1.8, 36.0)
    # Limbs from the riser ends (z +-16) to the tips (x -12, z +-60): 45.6 cm, raked 15.3 degrees
    # back towards the archer. Pitch turns the cylinder's +Z towards -X.
    cylinder((0.0, 0.0, 16.0), 15.26, 1.1, 45.6)
    cylinder((0.0, 0.0, -16.0), 164.74, 1.1, 45.6)
    return mesh


def ensure_bow_mesh(material):
    full = c.asset_path(WEAPON_PATH, BOW_MESH)
    static_mesh = c.load_or_none(full)
    action = "exists"
    if static_mesh is None:
        options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property("enable_recompute_normals", False)
        options.set_editor_property("enable_recompute_tangents", True)
        options.set_editor_property("enable_nanite", False)
        options.set_editor_property("enable_collision", False)
        static_mesh = first(unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
            build_bow_mesh(), full, options))
        if static_mesh is None:
            c.log("FAILED", full, "create_new_static_mesh_asset_from_mesh returned None")
            return None
        action = "created"

    if material is not None:
        try:
            if static_mesh.get_material(0) != material:
                static_mesh.set_material(0, material)
                if action == "exists":
                    action = "updated"
        except Exception as exc:  # noqa: BLE001
            c.log_error("material on " + full, exc)

    if action != "exists":
        c.save(static_mesh)
    c.log(action, full)
    return static_mesh


# --- arrow projectile Blueprints ---------------------------------------------------------------


def ensure_projectile_blueprints():
    """BP_Arrow_Standard and BP_Arrow_Grapple. Returns {name: generated class}."""
    import create_blueprints as bps  # noqa: WPS433 - same folder; make_blueprint is shared

    parents = {
        "BP_Arrow_Standard": c.find_class("ArrowProjectile", "/Script/Hawkeye.ArrowProjectile"),
        "BP_Arrow_Grapple": c.find_class("GrappleArrowProjectile", "/Script/Hawkeye.GrappleArrowProjectile"),
    }
    classes = {}
    for name, parent in parents.items():
        bp, created = bps.make_blueprint(name, WEAPON_PATH, parent, ("BlueprintFactory",))
        if bp is not None and created:
            c.compile_blueprint(bp)
            c.save(bp)
        if bp is not None:
            bps.ensure_parent(bp, c.asset_path(WEAPON_PATH, name), parent)
        classes[name] = c.load_generated_class(WEAPON_PATH, name)
    return classes


def ensure_effect_children():
    """BP_Effect_* for EFFECT_CHILDREN, their defaults set. Returns {Blueprint name: generated class}."""
    import create_blueprints as bps  # noqa: PLC0415

    classes = {}
    for _arrow, (name, parent_name, values) in sorted(EFFECT_CHILDREN.items()):
        parent = c.find_class(parent_name, "/Script/Hawkeye." + parent_name)
        full = c.asset_path(WEAPON_PATH, name)
        if parent is None:
            c.log("FAILED", full, "A{0} not exposed; build the module".format(parent_name))
            continue
        bp, created = bps.make_blueprint(name, WEAPON_PATH, parent, ("BlueprintFactory",))
        if bp is None:
            continue
        bps.ensure_parent(bp, full, parent)
        cdo = c.blueprint_cdo(bp)
        changed = []
        for prop, value in values:
            try:
                if same_value(cdo.get_editor_property(prop), value):
                    continue
            except Exception:  # noqa: BLE001 - set_props reports a missing property
                pass
            if c.set_props(cdo, [(prop, value)], name):
                changed.append(prop)
        if created or changed:
            c.compile_blueprint(bp)
            c.save(bp)
            c.log("created" if created else "updated", full, ", ".join(changed) or "defaults")
        else:
            c.log("exists", full)
        classes[name] = c.load_generated_class(WEAPON_PATH, name)
    return classes


def set_component_property(bp_path, bp_name, component_name, prop, value, context):
    """Write one property on an inherited component template through the Blueprint CDO."""
    if value is None:
        c.log("skipped", context, "asset not found")
        return False

    bp = c.load_or_none(c.asset_path(bp_path, bp_name))
    if bp is None:
        c.log("skipped", context, "Blueprint not found")
        return False

    cdo = c.blueprint_cdo(bp)
    component = None
    if cdo is not None:
        try:
            component = cdo.get_editor_property(component_name)
        except Exception:  # noqa: BLE001
            component = None
    if component is None:
        c.log("skipped", context, "no component called " + component_name)
        return False

    try:
        if same_value(component.get_editor_property(prop), value):
            c.log("exists", context)
            return False
    except Exception:  # noqa: BLE001 - set_props reports a missing property
        pass

    if not c.set_props(component, [(prop, value)], context):
        return False

    c.compile_blueprint(bp)
    c.save(bp)
    c.log("updated", context, c.safe_name(value))
    return True


def run():
    c.ensure_directory(WEAPON_PATH)

    hands = create_data_asset("DA_Weapon_Hands", "WeaponDefinition", weapon_values("DA_Weapon_Hands"))

    import _materials as m  # noqa: PLC0415

    material = m.ensure_look_material(c.asset_path(WEAPON_PATH, BOW_MATERIAL), _build_bow)
    m.ensure_look_material(c.asset_path(WEAPON_PATH, NOCK_MATERIAL), _build_arrow_nock)
    ensure_fx_material(c.asset_path(WEAPON_PATH, FX_MATERIAL), _build_arrow_fx)
    ensure_fx_material(c.asset_path(WEAPON_PATH, GLOW_MATERIAL), _build_arrow_glow)
    mesh = ensure_bow_mesh(material)
    projectiles = ensure_projectile_blueprints()
    projectiles.update(ensure_effect_children())

    bows = {name: create_data_asset(name, "BowDefinition", bow_values(name, mesh))
            for name in ("DA_Bow_Kate", "DA_Bow_Clint", "DA_Bow_Archer")}
    arrows = {name: create_data_asset(name, "ArrowDefinition", arrow_values(name, projectiles))
              for name in ARROW_NAMES}
    # Trickshot's arrow goes back in the quiver as a standard one, so it comes after the standard.
    arrows["DA_Arrow_Trickshot"] = create_data_asset(
        "DA_Arrow_Trickshot", "ArrowDefinition",
        arrow_values("DA_Arrow_Trickshot", dict(projectiles, DA_Arrow_Standard=arrows.get("DA_Arrow_Standard"))))

    # Without these the inventory falls back to transient stand-ins, which work but are not the
    # assets a designer can tune.
    set_component_property(
        PLAYER_PATH, "BP_HawkeyeCharacter", "inventory_component", "hands_definition", hands,
        "BP_HawkeyeCharacter.InventoryComponent.hands_definition")
    set_component_property(
        PLAYER_PATH, "BP_HawkeyeCharacter", "inventory_component", "standard_arrow_definition",
        arrows.get("DA_Arrow_Standard"), "BP_HawkeyeCharacter.InventoryComponent.standard_arrow_definition")

    return {"hands": hands, "bows": bows, "arrows": arrows, "mesh": mesh}


if __name__ == "__main__":
    run()
    c.print_summary("weapon data")
