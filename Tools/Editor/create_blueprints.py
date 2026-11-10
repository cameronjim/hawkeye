"""Create the player-framework Blueprints and wire their class defaults.

    /Game/Blueprints/Player/BP_HawkeyeCharacter        parent AHawkeyeCharacter
    /Game/Blueprints/SandboxCharacter_CMC             reparented onto BP_HawkeyeCharacter (the
                                                      Game Animation Sample's character; see
                                                      import_gasp.py)
    /Game/Blueprints/Player/BP_Kate                   parent SandboxCharacter_CMC (170 cm, purple
                                                      suit, UEFN mannequin, motion-matched ABP);
                                                      BP_HawkeyeCharacter when the sample is absent
    /Game/Characters/Kate/M_KateSuit, M_KateSuitDark  her stand-in materials (the suit: matte black
                                                      with purple arm panels and chest chevron)
    /Game/Blueprints/Player/BP_HawkeyePlayerController parent AHawkeyePlayerController
    /Game/Blueprints/Player/BP_HawkeyeGameMode         parent AHawkeyeGameMode
    /Game/Blueprints/UI/WBP_Flashback                 parent UFlashbackWidget
    /Game/Blueprints/UI/WBP_Pause                     parent UHawkeyePauseWidget
    /Game/Blueprints/UI/WBP_Settings                  parent UHawkeyeSettingsWidget
    /Game/Blueprints/UI/WBP_EndCard                   parent UMissionEndCardWidget
    /Game/Blueprints/UI/WBP_Hotbar                    parent UHawkeyeHotbarWidget
    /Game/Blueprints/UI/WBP_Inventory                 parent UHawkeyeInventoryWidget

Then, on the class default objects:

    BP_HawkeyeCharacter       DefaultMappingContext = IMC_Default, every IA_* property
                             that exists on AHawkeyeCharacter
    BP_HawkeyeGameMode        DefaultPawnClass = BP_Kate_C, PlayerControllerClass
    BP_HawkeyePlayerController FlashbackWidgetClass = WBP_Flashback_C,
                             PauseWidgetClass = WBP_Pause_C,
                             SettingsWidgetClass = WBP_Settings_C, PauseAction = IA_Pause,
                             PauseMappingContext = IMC_Default,
                             EndCardWidgetClass = WBP_EndCard_C, PhoneAction = IA_Phone, MapAction = IA_Map

Property names come from Source/Hawkeye/Player/HawkeyeCharacter.h and
Source/Hawkeye/HawkeyePlayerController.h. Anything not found on the class is reported and
skipped rather than aborting.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402

PLAYER_PATH = "/Game/Blueprints/Player"
UI_PATH = "/Game/Blueprints/UI"
INPUT_PATH = "/Game/Input"

# Third person: SK_Mannequin is the player's body on ACharacter's own Mesh, fully visible and
# animated by the idle/walk sequences. See AHawkeyeCharacter.
MANNEQUIN_MESH = "/Game/Mannequin/Character/Mesh/SK_Mannequin"
MANNEQUIN_IDLE = "/Game/Mannequin/Animations/ThirdPersonIdle"
MANNEQUIN_WALK = "/Game/Mannequin/Animations/ThirdPersonWalk"
MANNEQUIN_RUN = "/Game/Mannequin/Animations/ThirdPersonRun"
MANNEQUIN_FALL = "/Game/Mannequin/Animations/ThirdPersonJump_Loop"

# BP_Kate: the playable stand-in until a real Kate mesh exists. A child of BP_HawkeyeCharacter,
# so the input wiring and Hands stay in one place. 170 cm: the mannequin is about 183 cm, so it
# is scaled to fit a capsule of that height with its feet on the capsule's bottom.
KATE_NAME = "BP_Kate"
KATE_MATERIAL_PATH = "/Game/Characters/Kate"
M_KATE_SUIT = KATE_MATERIAL_PATH + "/M_KateSuit"
M_KATE_SUIT_DARK = KATE_MATERIAL_PATH + "/M_KateSuitDark"
KATE_SUIT_COLOR = (0.19, 0.035, 0.34)         # the purple panels (bluer, so warm lamps leave it purple)
KATE_SUIT_BLACK = (0.018, 0.016, 0.022)        # the matte black under-layer
KATE_SUIT_DARK_COLOR = (0.08, 0.08, 0.08)
KATE_SUIT_ROUGHNESS = 0.6
KATE_SUIT_BLACK_ROUGHNESS = 0.8
KATE_SUIT_PANEL_ROUGHNESS = 0.45
# The suit's panels, in the UEFN mannequin's bind pose (cm, mesh space: +Y forward, A-pose; the
# shoulders at |x| 18, the wrists at |x| about 46, the chest front at y > 3). The mannequin's one
# material slot has no usable UV layout for panels, so the mask comes from the pre-skinned position,
# which moves with the skin however she is posed.
KATE_ARM_PANEL_X = (19.0, 44.0)                # purple from the shoulder to the glove
KATE_ARM_PANEL_MIN_Z = 92.0
KATE_CHEVRON_TIP_Z = 116.0                     # the V's point, on the sternum
KATE_CHEVRON_SLOPE = 0.9                       # cm up per cm out
KATE_CHEVRON_HALF_WIDTH = 3.5                  # cm, measured up the body
KATE_CHEVRON_MAX_X = 17.0
KATE_CHEST_FRONT_Y = 3.0
KATE_CAPSULE_RADIUS = 30.0
KATE_CAPSULE_HALF_HEIGHT = 85.0
KATE_MESH_SCALE = 0.93

# The Game Animation Sample's CharacterMovement character and its motion-matched AnimBP, copied
# in by import_gasp.py. SandboxCharacter_CMC is reparented onto BP_HawkeyeCharacter so our systems,
# input and camera sit under its graph; BP_Kate derives from it and wears its UEFN mannequin.
GASP_CHARACTER_PATH = "/Game/Blueprints"
GASP_CHARACTER = "SandboxCharacter_CMC"
GASP_ANIM_BP = "SandboxCharacter_CMC_ABP"
GASP_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"

# AHawkeyeCharacter input property name -> IA asset name.
# Pause lives on AHawkeyePlayerController, not the pawn, so that Escape still works when the
# pawn is locked out or dead; IA_Skip is consumed by the flashback widget's key handler and
# has no property to bind to. Both are reported as skipped here, which is expected.
CHARACTER_INPUT_PROPERTIES = [
    ("move_action", "IA_Move"),
    ("look_action", "IA_Look"),
    ("look_stick_action", "IA_LookStick"),
    ("jump_action", "IA_Jump"),
    ("sprint_action", "IA_Sprint"),
    ("crouch_action", "IA_Crouch"),
    ("fire_action", "IA_Fire"),
    ("aim_action", "IA_Aim"),
    ("reload_action", "IA_Reload"),
    ("takedown_action", "IA_Takedown"),
    ("interact_action", "IA_Interact"),
    ("skip_action", "IA_Skip"),
    ("slot1_action", "IA_Slot1"),
    ("slot2_action", "IA_Slot2"),
    ("slot3_action", "IA_Slot3"),
    ("slot4_action", "IA_Slot4"),
    ("slot5_action", "IA_Slot5"),
    ("slot6_action", "IA_Slot6"),
    ("slot_scroll_action", "IA_SlotScroll"),
    ("inventory_action", "IA_Inventory"),
    ("grapple_action", "IA_Grapple"),
    ("melee_action", "IA_Melee"),
]


def make_blueprint(name, path, parent_class, factory_names, quiet=False):
    """Create a Blueprint with the given parent, or return the existing one.

    Never raises: a failure here must not stop the other Blueprints being made.
    """
    try:
        return _make_blueprint(name, path, parent_class, factory_names, quiet)
    except Exception as exc:  # noqa: BLE001
        c.log_error(c.asset_path(path, name), exc)
        return None, False


def _make_blueprint(name, path, parent_class, factory_names, quiet=False):
    full = c.asset_path(path, name)
    if parent_class is None:
        c.log("FAILED", full, "parent class not found")
        return None, False

    existing = c.load_or_none(full)
    if existing is not None:
        c.log("exists", full)
        return existing, False

    factory = c.new_factory(*factory_names)
    if factory is None:
        c.log("FAILED", full, "no factory class among " + ", ".join(factory_names))
        return None, False

    c.set_props(factory, [("parent_class", parent_class)], name + " factory")

    asset_class = unreal.Blueprint
    if "WidgetBlueprintFactory" in factory_names and hasattr(unreal, "WidgetBlueprint"):
        asset_class = unreal.WidgetBlueprint

    bp, created = c.create_asset(name, path, asset_class, factory, quiet=True)
    if bp is None:
        return None, False
    # parent_class is a Python type object, so get_name() on it is unbound - use class_name.
    c.log("created", full, "parent " + c.class_name(parent_class))
    return bp, created


def apply_defaults(bp, name, path, values):
    """set_editor_property on the Blueprint CDO, then compile + save. Returns applied names."""
    full = c.asset_path(path, name)
    cdo = c.blueprint_cdo(bp)
    if cdo is None:
        cdo = unreal.get_default_object(c.load_generated_class(path, name))
    if cdo is None:
        c.log("FAILED", full, "no class default object")
        return []

    wanted = []
    already = []
    for prop, value in values:
        if value is None:
            continue
        try:
            if cdo.get_editor_property(prop) == value:
                already.append(prop)
                continue
        except Exception:  # noqa: BLE001 - property missing; set_props will report it
            pass
        wanted.append((prop, value))

    missing_values = [prop for prop, value in values if value is None]
    applied = c.set_props(cdo, wanted, name)
    skipped = [prop for prop, _v in wanted if prop not in applied] + missing_values

    # Only compile and save when something actually changed, so a re-run is a true no-op.
    if applied:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", full, "set " + ", ".join(applied))
    elif already and not skipped:
        c.log("exists", full, "{0} defaults already set".format(len(already)))
    if skipped:
        c.log("skipped", full, "no such property / missing asset: " + ", ".join(skipped))
    return applied


def component_asset(component, prop_name):
    """The asset a component already has, by property or by its getter. None when unknown."""
    try:
        return component.get_editor_property(prop_name)
    except Exception:  # noqa: BLE001 - private UPROPERTY; try the getter instead
        pass

    getter = getattr(component, "get_" + prop_name, None)
    if getter is None:
        return None
    try:
        return getter()
    except Exception:  # noqa: BLE001
        return None


def set_component_asset(bp, component_name, setter_name, prop_name, asset, context):
    """Assign a mesh on an inherited component through the Blueprint CDO. Returns True if changed.

    The CDO's component instance is the template every spawned actor copies, which is what a
    designer edits in the Components panel.
    """
    if asset is None:
        c.log("skipped", context, "asset not found")
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

    if component_asset(component, prop_name) == asset:
        return False

    try:
        getattr(component, setter_name)(asset)
        c.log("updated", context, asset.get_name())
        return True
    except Exception as exc:  # noqa: BLE001
        c.log_error(context, exc)
        return False


# Packages a pre-pivot BP_HawkeyeCharacter still pulls in through the first-person components
# and materials it was saved with. Any of these in its dependencies means it has not been
# resaved since the viewmodel code was removed.
RETIRED_CHARACTER_DEPENDENCIES = (
    "/Game/Materials/M_FrankArms",
    "/Game/Materials/M_FrankGloves",
    "/Game/Materials/M_Pistol",
    "/Game/Weapons/Pistol/Meshes/SM_Pistol",
)


def package_dependencies(package_name):
    """Hard and soft package dependencies of package_name, as plain strings."""
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        options = unreal.AssetRegistryDependencyOptions(
            include_soft_package_references=True,
            include_hard_package_references=True,
            include_searchable_names=False,
            include_soft_management_references=False,
            include_hard_management_references=False,
        )
        return [str(dep) for dep in (registry.get_dependencies(package_name, options) or [])]
    except Exception as exc:  # noqa: BLE001
        c.log_error("get_dependencies " + package_name, exc)
        return []


def clear_retired_materials(component, context):
    """Drop material overrides that point at the retired first-person fatigues."""
    if component is None:
        return False
    try:
        overrides = list(component.get_editor_property("override_materials") or [])
    except Exception:  # noqa: BLE001
        return False
    if not any(mat is not None and mat.get_name().startswith("M_Frank") for mat in overrides):
        return False
    if c.set_props(component, [("override_materials", [])], context):
        c.log("updated", context, "M_Frank* overrides cleared; the mesh wears its own materials")
        return True
    return False


def configure_body(bp):
    """Give BP_HawkeyeCharacter its body: SK_Mannequin on ACharacter's Mesh, nothing else.

    Third person: the whole mannequin is visible and animated by IdleAnim/WalkAnim. The
    first-person arms, the camera-held pistol and Frank's fatigues are gone; a save made before
    the pivot still carries them, so the Blueprint is resaved once to shed those references.
    """
    if bp is None:
        return

    mannequin = c.load_or_none(MANNEQUIN_MESH)
    changed = set_component_asset(
        bp, "mesh", "set_skeletal_mesh_asset", "skeletal_mesh_asset", mannequin,
        "BP_HawkeyeCharacter.Mesh")

    cdo = c.blueprint_cdo(bp)
    body = None
    if cdo is not None:
        try:
            body = cdo.get_editor_property("mesh")
        except Exception:  # noqa: BLE001
            body = None
    changed = clear_retired_materials(body, "BP_HawkeyeCharacter.Mesh") or changed

    package = c.asset_path(PLAYER_PATH, "BP_HawkeyeCharacter")
    stale = [dep for dep in package_dependencies(package) if dep in RETIRED_CHARACTER_DEPENDENCIES]
    if stale:
        c.log("updated", package, "resaved past the first-person build ({0})".format(", ".join(stale)))
        changed = True

    if changed:
        c.compile_blueprint(bp)
        c.save(bp)


def _build_kate_suit(material):
    """Matte black with purple arm panels and a purple chevron on the chest, masked by the
    mannequin's pre-skinned (bind-pose) position, carried to the pixel shader by a vertex
    interpolator (the position is only available per vertex)."""
    import _materials as m  # noqa: PLC0415

    bind = m.expr(material, "MaterialExpressionPreSkinnedPosition", -2000, 0, None, "PreSkinnedPosition")
    carried = m.expr(material, "MaterialExpressionVertexInterpolator", -1850, 0, None, "VertexInterpolator")
    m.connect(bind, "", carried, "")
    x = m.absolute(material, m.component_mask(material, carried, r=True, x=-1700, y=-100), -1550, -100)
    y = m.component_mask(material, carried, g=True, x=-1700, y=0)
    z = m.component_mask(material, carried, b=True, x=-1700, y=100)

    arms = m.mul_all(material, [m.band(material, x, KATE_ARM_PANEL_X[0], KATE_ARM_PANEL_X[1], -1400, -300, 3.0),
                                m.step(material, z, KATE_ARM_PANEL_MIN_Z, -1400, -200, 3.0)], -900, -300)
    # The chevron: |z - (tip + slope * |x|)| under the half width, on the front of the chest.
    line = m.add(material, m.multiply(material, x, None, -1400, 100, const_b=KATE_CHEVRON_SLOPE), None, -1250, 100,
                 const_b=KATE_CHEVRON_TIP_Z)
    off = m.absolute(material, m.subtract(material, z, line, -1100, 100), -950, 100)
    chevron = m.mul_all(material, [m.below(material, off, KATE_CHEVRON_HALF_WIDTH, -800, 100, 3.0),
                                   m.below(material, x, KATE_CHEVRON_MAX_X, -800, 200, 3.0),
                                   m.step(material, y, KATE_CHEST_FRONT_Y, -800, 300, 3.0)], -500, 100)
    panel = m.maximum(material, arms, chevron, -300, 0)
    color = m.lerp(material, m.constant3(material, KATE_SUIT_BLACK, -300, -250),
                   m.constant3(material, KATE_SUIT_COLOR, -300, -150), panel, -100, -200)
    m.connect_property(color, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(m.lerp(material, None, None, panel, -100, 100, const_a=KATE_SUIT_BLACK_ROUGHNESS,
                              const_b=KATE_SUIT_PANEL_ROUGHNESS), unreal.MaterialProperty.MP_ROUGHNESS)


def ensure_kate_materials():
    """M_KateSuit (black with purple panels) and M_KateSuitDark at /Game/Characters/Kate, both
    skeletal-mesh usable."""
    import _materials as m  # noqa: PLC0415 - only this step needs the material helpers

    c.ensure_directory(KATE_MATERIAL_PATH)
    return (
        m.ensure_look_material(M_KATE_SUIT, _build_kate_suit, skeletal=True),
        m.ensure_material(
            M_KATE_SUIT_DARK, m._build_flat(KATE_SUIT_DARK_COLOR, KATE_SUIT_ROUGHNESS), skeletal=True),
    )


def cdo_component(bp, component_name):
    cdo = c.blueprint_cdo(bp)
    if cdo is None:
        return None
    try:
        return cdo.get_editor_property(component_name)
    except Exception:  # noqa: BLE001
        return None


def close_enough(current, wanted, tolerance=1e-3):
    """Equality that survives float32 round trips (0.93 reads back as 0.9300000071)."""
    if isinstance(wanted, float):
        return abs(float(current) - wanted) < tolerance
    if isinstance(wanted, unreal.Vector):
        return all(abs(getattr(current, axis) - getattr(wanted, axis)) < tolerance for axis in "xyz")
    return current == wanted


def set_if_different(component, values, context):
    """set_editor_property only for the values that differ. Returns the names written."""
    wanted = []
    for prop, value in values:
        try:
            if close_enough(component.get_editor_property(prop), value):
                continue
        except Exception:  # noqa: BLE001 - set_props reports a missing property
            pass
        wanted.append((prop, value))
    return c.set_props(component, wanted, context) if wanted else []


def set_material_slot(component, slot, material, context):
    if component is None or material is None:
        return False
    try:
        if component.get_material(slot) == material:
            return False
    except Exception:  # noqa: BLE001 - an empty slot reads back as None
        pass
    try:
        component.set_material(slot, material)
        c.log("updated", context, "slot {0} = {1}".format(slot, material.get_name()))
        return True
    except Exception as exc:  # noqa: BLE001
        c.log_error(context, exc)
        return False


def parent_class_name(bp):
    """Name of a Blueprint's parent class (e.g. BP_HawkeyeCharacter_C), or ''."""
    try:
        parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(bp)
        return parent.get_name() if parent is not None else ""
    except Exception as exc:  # noqa: BLE001
        c.log_error("parent of " + bp.get_name(), exc)
        return ""


def ensure_parent(bp, context, parent_class):
    """Reparent bp onto parent_class unless it is already there. Compiles and saves on a change."""
    if bp is None or parent_class is None:
        return False
    wanted = c.class_name(parent_class)
    current = parent_class_name(bp)
    if current == wanted:
        return False
    try:
        unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent_class)
    except Exception as exc:  # noqa: BLE001
        c.log_error("reparent " + context, exc)
        return False
    c.compile_blueprint(bp)
    c.save(bp)
    c.log("updated", context, "parent {0} -> {1}".format(current or "?", wanted))
    return True


def gasp_character():
    """SandboxCharacter_CMC reparented onto BP_HawkeyeCharacter, or None without the sample."""
    full = c.asset_path(GASP_CHARACTER_PATH, GASP_CHARACTER)
    if not c.exists(full):
        c.log("skipped", full, "Game Animation Sample not imported; BP_Kate keeps the clip switch")
        return None
    bp = c.load_or_none(full)
    base = c.load_generated_class(PLAYER_PATH, "BP_HawkeyeCharacter")
    if bp is None or base is None:
        c.log("FAILED", full, "could not load it or BP_HawkeyeCharacter_C")
        return None
    if not ensure_parent(bp, full, base):
        c.log("exists", full, "parent already BP_HawkeyeCharacter_C")
    return bp


def configure_kate_body(body, gasp):
    """The UEFN mannequin and the sample's AnimBP on BP_Kate's Mesh when the sample is in.

    Set explicitly: a reparent keeps the old CDO's mesh values, so BP_Kate would otherwise carry
    on with the UE4 mannequin and no AnimBP.
    """
    if gasp is None or body is None:
        return []
    changed = []
    mesh = c.load_or_none(GASP_MESH)
    anim_class = c.load_generated_class(GASP_CHARACTER_PATH, GASP_ANIM_BP)
    if mesh is not None and component_asset(body, "skeletal_mesh_asset") != mesh:
        body.set_skeletal_mesh_asset(mesh)
        changed.append("mesh " + mesh.get_name())
    if body.get_editor_property("animation_mode") != unreal.AnimationMode.ANIMATION_BLUEPRINT:
        body.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
        changed.append("animation mode")
    if anim_class is not None and body.get_editor_property("anim_class") != anim_class:
        body.set_editor_property("anim_class", anim_class)
        changed.append("anim class " + anim_class.get_name())
    return changed


def configure_kate(bp, gasp=None):
    """Capsule 170 cm tall, the mannequin scaled to it, purple suit. Saves only on a change."""
    if bp is None:
        return
    context = c.asset_path(PLAYER_PATH, KATE_NAME)
    suit, suit_dark = ensure_kate_materials()

    changed = []
    capsule = cdo_component(bp, "capsule_component")
    if capsule is not None:
        changed += set_if_different(capsule, [
            ("capsule_radius", KATE_CAPSULE_RADIUS),
            ("capsule_half_height", KATE_CAPSULE_HALF_HEIGHT),
        ], KATE_NAME + ".CapsuleComponent")
    else:
        c.log("skipped", context, "no capsule component")

    body = cdo_component(bp, "mesh")
    if body is not None:
        changed += set_if_different(body, [
            ("relative_location", unreal.Vector(0.0, 0.0, -KATE_CAPSULE_HALF_HEIGHT)),
            ("relative_scale3d", unreal.Vector(KATE_MESH_SCALE, KATE_MESH_SCALE, KATE_MESH_SCALE)),
        ], KATE_NAME + ".Mesh")
        changed += configure_kate_body(body, gasp)
        # Slot 0 is the body in the suit, slot 1 the darker trim; any further slots wear the suit.
        try:
            slots = max(2, int(body.get_num_materials()))
        except Exception:  # noqa: BLE001
            slots = 2
        for slot in range(slots):
            if set_material_slot(body, slot, suit_dark if slot == 1 else suit, KATE_NAME + ".Mesh"):
                changed.append("material {0}".format(slot))
    else:
        c.log("skipped", context, "no mesh component")

    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", context, ", ".join(changed))
    else:
        c.log("exists", context, "capsule, mesh and suit already set")


def run():
    c.ensure_directory(PLAYER_PATH)
    c.ensure_directory(UI_PATH)

    character_parent = c.find_class("HawkeyeCharacter", "/Script/Hawkeye.HawkeyeCharacter")
    controller_parent = c.find_class(
        "HawkeyePlayerController", "/Script/Hawkeye.HawkeyePlayerController"
    )
    game_mode_parent = c.find_class("HawkeyeGameMode", "/Script/Hawkeye.HawkeyeGameMode")
    widget_parent = c.find_class("FlashbackWidget", "/Script/Hawkeye.FlashbackWidget")

    bp_factories = ("BlueprintFactory",)
    wbp_factories = ("WidgetBlueprintFactory",)

    pause_parent = c.find_class("HawkeyePauseWidget", "/Script/Hawkeye.HawkeyePauseWidget")
    settings_parent = c.find_class(
        "HawkeyeSettingsWidget", "/Script/Hawkeye.HawkeyeSettingsWidget"
    )
    end_card_parent = c.find_class(
        "MissionEndCardWidget", "/Script/Hawkeye.MissionEndCardWidget"
    )

    wbp_flashback, _ = make_blueprint("WBP_Flashback", UI_PATH, widget_parent, wbp_factories)
    wbp_pause, _ = make_blueprint("WBP_Pause", UI_PATH, pause_parent, wbp_factories)
    wbp_settings, _ = make_blueprint("WBP_Settings", UI_PATH, settings_parent, wbp_factories)
    wbp_end_card, _ = make_blueprint("WBP_EndCard", UI_PATH, end_card_parent, wbp_factories)

    hotbar_parent = c.find_class("HawkeyeHotbarWidget", "/Script/Hawkeye.HawkeyeHotbarWidget")
    inventory_parent = c.find_class("HawkeyeInventoryWidget", "/Script/Hawkeye.HawkeyeInventoryWidget")
    wbp_hotbar, _ = make_blueprint("WBP_Hotbar", UI_PATH, hotbar_parent, wbp_factories)
    wbp_inventory, _ = make_blueprint("WBP_Inventory", UI_PATH, inventory_parent, wbp_factories)
    bp_character, _ = make_blueprint(
        "BP_HawkeyeCharacter", PLAYER_PATH, character_parent, bp_factories
    )
    bp_controller, _ = make_blueprint(
        "BP_HawkeyePlayerController", PLAYER_PATH, controller_parent, bp_factories
    )
    bp_game_mode, _ = make_blueprint(
        "BP_HawkeyeGameMode", PLAYER_PATH, game_mode_parent, bp_factories
    )

    # Newly created Blueprints need to exist on disk before load_class can find the _C.
    for bp in (
        wbp_flashback, wbp_pause, wbp_settings, wbp_end_card, wbp_hotbar, wbp_inventory,
        bp_character, bp_controller, bp_game_mode,
    ):
        if bp is not None:
            c.compile_blueprint(bp)
            c.save(bp, only_if_dirty=True)

    # --- BP_HawkeyeCharacter -------------------------------------------------------------
    if bp_character is not None:
        values = [("default_mapping_context", c.load_or_none(c.asset_path(INPUT_PATH, "IMC_Default")))]
        for prop, asset_name in CHARACTER_INPUT_PROPERTIES:
            values.append((prop, c.load_or_none(c.asset_path(INPUT_PATH, asset_name))))
        # The body plays the same two sequences the thugs do; there is no AnimBP.
        values.append(("idle_anim", c.load_or_none(MANNEQUIN_IDLE)))
        values.append(("walk_anim", c.load_or_none(MANNEQUIN_WALK)))
        values.append(("run_anim", c.load_or_none(MANNEQUIN_RUN)))
        values.append(("fall_anim", c.load_or_none(MANNEQUIN_FALL)))
        apply_defaults(bp_character, "BP_HawkeyeCharacter", PLAYER_PATH, values)
        configure_body(bp_character)

    # --- BP_Kate --------------------------------------------------------------------------
    # After the base is compiled and saved, so its generated class exists to derive from.
    # With the Game Animation Sample imported, Kate derives from its sandbox character (itself
    # reparented onto BP_HawkeyeCharacter): BP_Kate -> SandboxCharacter_CMC -> BP_HawkeyeCharacter.
    bp_kate = None
    if bp_character is not None:
        gasp = gasp_character()
        if gasp is not None:
            kate_parent = c.load_generated_class(GASP_CHARACTER_PATH, GASP_CHARACTER)
        else:
            kate_parent = c.load_generated_class(PLAYER_PATH, "BP_HawkeyeCharacter")
        bp_kate, kate_created = make_blueprint(KATE_NAME, PLAYER_PATH, kate_parent, bp_factories)
        if bp_kate is not None and kate_created:
            c.compile_blueprint(bp_kate)
            c.save(bp_kate)
        ensure_parent(bp_kate, c.asset_path(PLAYER_PATH, KATE_NAME), kate_parent)
        configure_kate(bp_kate, gasp)

    # --- BP_HawkeyePlayerController ------------------------------------------------------
    if bp_controller is not None:
        apply_defaults(
            bp_controller,
            "BP_HawkeyePlayerController",
            PLAYER_PATH,
            [
                ("flashback_widget_class", c.load_generated_class(UI_PATH, "WBP_Flashback")),
                ("pause_widget_class", c.load_generated_class(UI_PATH, "WBP_Pause")),
                ("settings_widget_class", c.load_generated_class(UI_PATH, "WBP_Settings")),
                ("pause_action", c.load_or_none(c.asset_path(INPUT_PATH, "IA_Pause"))),
                (
                    "pause_mapping_context",
                    c.load_or_none(c.asset_path(INPUT_PATH, "IMC_Default")),
                ),
                ("end_card_widget_class", c.load_generated_class(UI_PATH, "WBP_EndCard")),
                ("inventory_widget_class", c.load_generated_class(UI_PATH, "WBP_Inventory")),
                ("phone_action", c.load_or_none(c.asset_path(INPUT_PATH, "IA_Phone"))),
                ("map_action", c.load_or_none(c.asset_path(INPUT_PATH, "IA_Map"))),
            ],
        )

    # --- BP_HawkeyeGameMode --------------------------------------------------------------
    if bp_game_mode is not None:
        apply_defaults(
            bp_game_mode,
            "BP_HawkeyeGameMode",
            PLAYER_PATH,
            [
                # Kate is who you play; BP_HawkeyeCharacter stays the base she derives from.
                ("default_pawn_class", c.load_generated_class(PLAYER_PATH, KATE_NAME)),
                (
                    "player_controller_class",
                    c.load_generated_class(PLAYER_PATH, "BP_HawkeyePlayerController"),
                ),
            ],
        )

    return {
        "character": bp_character,
        "kate": bp_kate,
        "controller": bp_controller,
        "game_mode": bp_game_mode,
        "flashback_widget": wbp_flashback,
        "pause_widget": wbp_pause,
        "settings_widget": wbp_settings,
        "end_card_widget": wbp_end_card,
        "hotbar_widget": wbp_hotbar,
        "inventory_widget": wbp_inventory,
    }


# --------------------------------------------------------------------------------------
# audio defaults (Tools/Editor/create_audio.py builds the sounds; this points the classes at them)
# --------------------------------------------------------------------------------------
# Runs as its own create_all step after every Blueprint and data asset exists. Children (BP_Kate,
# BP_Clint, BP_Archer, BP_Thug_Heavy) inherit from the parents set here.

AUDIO_AI_PATH = "/Game/Blueprints/AI"
AUDIO_WEAPON_PATH = "/Game/Blueprints/Weapons"

_BOW_SOUNDS = [
    ("draw_sound", "MS_Bow_Draw"),
    ("release_sound", "MS_Bow_Release"),
    ("whistle_sound", "MS_Arrow_Whistle"),
    ("impact_stone_sound", "MS_Arrow_Impact_Stone"),
    ("impact_wood_sound", "MS_Arrow_Impact_Wood"),
    ("impact_flesh_sound", "MS_Arrow_Impact_Flesh"),
    ("pickup_sound", "MS_Arrow_Pickup"),
]

# (Blueprint folder, Blueprint, component or None for the CDO, [(property, sound name or [names])])
AUDIO_DEFAULTS = [
    (PLAYER_PATH, "BP_HawkeyeCharacter", None, [
        ("footstep_sounds", ["MS_Foot_Snow_01", "MS_Foot_Snow_02", "MS_Foot_Snow_03", "MS_Foot_Snow_04"]),
        ("wood_footstep_sounds", ["MS_Footstep_Wood"]),
        ("carpet_footstep_sounds", ["MS_Footstep_Carpet"]),
        ("land_sound", "MS_Land"),
        ("roll_sound", "MS_Roll_Thump"),
        ("stagger_sound", "MS_Melee_Stagger"),
        ("parry_sound", "MS_Parry"),
    ]),
    (PLAYER_PATH, "BP_HawkeyeCharacter", "bow_component", _BOW_SOUNDS),
    (PLAYER_PATH, "BP_HawkeyeCharacter", "finisher_component", [("strike_sound", "MS_Melee_Heavy")]),
    (PLAYER_PATH, "BP_HawkeyeCharacter", "grapple_component", [
        ("fire_sound", "MS_Grapple_Fire"),
        ("zip_sound", "MS_Grapple_Zip"),
        ("land_sound", "MS_Grapple_Land"),
    ]),
    (PLAYER_PATH, "BP_HawkeyeCharacter", "parkour_component", [("effort_sound", "MS_Vault_Grunt")]),
    (PLAYER_PATH, "BP_HawkeyeCharacter", "melee_component", [
        ("hit_sound", "MS_Melee_Punch"),
        ("heavy_hit_sound", "MS_Melee_Heavy"),
    ]),
    (AUDIO_AI_PATH, "BP_Thug", None, [
        ("telegraph_sound", "MS_Thug_Telegraph"),
        ("hurt_sound", "MS_Thug_Hurt"),
        ("death_sound", "MS_Thug_Death"),
        ("block_sound", "MS_Melee_Block"),
        ("stagger_sound", "MS_Melee_Stagger"),
    ]),
    (AUDIO_AI_PATH, "BP_Thug", "melee_component", [
        ("windup_sound", "MS_Thug_Telegraph"),
        ("swing_sound", "MS_Thug_BatSwing"),
        ("hit_sound", "MS_Melee_Punch"),
    ]),
    (AUDIO_AI_PATH, "BP_Thug", "weapon_component", [("fire_sound", "MS_Thug_Gunshot")]),
    (AUDIO_AI_PATH, "BP_Thug", "bow_component", _BOW_SOUNDS),
    (PLAYER_PATH, "BP_HawkeyePlayerController", None, [
        ("ui_hover_sound", "MS_UI_Hover"),
        ("ui_click_sound", "MS_UI_Click"),
        ("objective_complete_sound", "MS_UI_ObjectiveComplete"),
        ("new_objective_sound", "MS_UI_NewObjective"),
        ("toast_sound", "MS_UI_Toast"),
        ("volume_mix", "@SMX_Settings"),
        ("master_sound_class", "@SCL_Master"),
        ("sfx_sound_class", "@SCL_SFX"),
        ("ambient_sound_class", "@SCL_Ambient"),
        ("ui_sound_class", "@SCL_UI"),
    ]),
]

# data asset -> (sound, follows the effect)
AUDIO_ARROWS = {
    "DA_Arrow_Putty": ("MS_Trick_Putty", False),
    "DA_Arrow_Bola": ("MS_Trick_Bola", False),
    "DA_Arrow_Smoke": ("MS_Trick_Smoke", True),
    "DA_Arrow_EMP": ("MS_Trick_Emp", False),
    "DA_Arrow_Explosive": ("MS_Trick_Explosion", False),
    "DA_Arrow_Confetti": ("MS_Trick_Putty", False),
}


def _audio_asset(name):
    """A sound by MS_ name, or a mix/class by @name."""
    import create_audio  # noqa: PLC0415 - only this step needs it

    if name.startswith("@"):
        return c.load_or_none(c.asset_path(create_audio.CLASSES_PATH, name[1:]))
    return c.load_or_none(create_audio.sound_path(name))


def _asset_key(value):
    """Package path of an object or soft reference (or a list of them), so reads compare with writes."""
    if isinstance(value, (list, tuple, unreal.Array)):
        return [_asset_key(v) for v in value]
    if value is None:
        return ""
    for getter in ("get_path_name", "export_text"):
        method = getattr(value, getter, None)
        if method is not None:
            try:
                return str(method()).split(".")[0]
            except Exception:  # noqa: BLE001
                pass
    return str(value).split(".")[0]


def _apply_sound_values(target, values, context):
    """Sets each (property, name) on target that differs. Returns the properties changed."""
    changed = []
    for prop, names in values:
        wanted = [_audio_asset(n) for n in names] if isinstance(names, list) else _audio_asset(names)
        if wanted is None or (isinstance(wanted, list) and any(w is None for w in wanted)):
            c.log("skipped", context + "." + prop, "sound not built; run create_audio")
            continue
        try:
            current = target.get_editor_property(prop)
        except Exception:  # noqa: BLE001 - set_props reports a missing property
            current = None
        if _asset_key(current) == _asset_key(wanted):
            continue
        if c.set_props(target, [(prop, wanted)], context):
            changed.append(prop)
    return changed


def apply_audio_defaults():
    """Points every class that plays a sound at its MetaSound. Saves only what changed."""
    by_blueprint = {}
    for path, name, component, values in AUDIO_DEFAULTS:
        by_blueprint.setdefault((path, name), []).append((component, values))

    for (path, name), entries in by_blueprint.items():
        full = c.asset_path(path, name)
        bp = c.load_or_none(full)
        cdo = c.blueprint_cdo(bp) if bp is not None else None
        if cdo is None:
            c.log("skipped", full + " audio", "Blueprint not found")
            continue
        changed = []
        for component, values in entries:
            target = cdo
            if component:
                try:
                    target = cdo.get_editor_property(component)
                except Exception:  # noqa: BLE001
                    target = None
            if target is None:
                c.log("skipped", "{0}.{1}".format(name, component), "no such component")
                continue
            changed += _apply_sound_values(target, values, name + ("." + component if component else ""))
        if changed:
            c.compile_blueprint(bp)
            c.save(bp)
            c.log("updated", full, "sounds: " + ", ".join(changed))
        else:
            c.log("exists", full, "sounds already set")

    for arrow, (sound, follows) in AUDIO_ARROWS.items():
        full = c.asset_path(AUDIO_WEAPON_PATH, arrow)
        asset = c.load_or_none(full)
        if asset is None:
            c.log("skipped", full + " audio", "data asset not found")
            continue
        changed = _apply_sound_values(asset, [("effect_sound", sound)], arrow)
        if bool(asset.get_editor_property("effect_sound_follows_effect")) != follows:
            c.set_props(asset, [("effect_sound_follows_effect", follows)], arrow)
            changed.append("effect_sound_follows_effect")
        if changed:
            c.save(asset)
            c.log("updated", full, "sounds: " + ", ".join(changed))
        else:
            c.log("exists", full, "sound already set")


# --------------------------------------------------------------------------------------
# effect defaults (Tools/Editor/create_vfx.py builds the systems; this points the classes at them)
# --------------------------------------------------------------------------------------
# Its own create_all step after create_vfx and every Blueprint and data asset. Children inherit.

_BOW_VFX = [
    ("release_vfx", "NS_BowRelease"),
    ("arrow_trail_vfx", "NS_ArrowTrail"),
    ("impact_stone_vfx", "NS_ArrowImpact_Stone"),
    ("impact_wood_vfx", "NS_ArrowImpact_Wood"),
    ("hit_spark_vfx", "NS_HitSpark"),
]

VFX_DEFAULTS = [
    (PLAYER_PATH, "BP_HawkeyeCharacter", None, [
        ("footstep_vfx", "NS_FootstepSnow"),
        ("landing_vfx", "NS_LandingSnow"),
        ("screen_pulse_material", "M_PP_EmpAberration"),
        ("parry_vfx", "NS_ParryRing"),
    ]),
    (PLAYER_PATH, "BP_HawkeyeCharacter", "bow_component", _BOW_VFX),
    (PLAYER_PATH, "BP_HawkeyeCharacter", "grapple_component", [
        ("zip_line_vfx", "NS_ZipLine"),
        ("anchor_sparks_vfx", "NS_AnchorSparks"),
    ]),
    (AUDIO_AI_PATH, "BP_Thug", "weapon_component", [
        ("muzzle_flash_vfx", "NS_MuzzleFlash"),
        ("tracer_vfx", "NS_Tracer"),
    ]),
    (AUDIO_AI_PATH, "BP_Thug", "bow_component", _BOW_VFX),
    (PLAYER_PATH, "BP_HawkeyePlayerController", "snowfall", [("snow_system", "NS_Snowfall")]),
]

# data asset -> [(property, asset)]
VFX_ARROWS = {
    "DA_Arrow_Putty": [("effect_vfx", "NS_PuttySplat")],
    "DA_Arrow_Bola": [("flight_vfx", "NS_BolaTrail")],
    "DA_Arrow_Smoke": [("effect_vfx", "NS_SmokeCloud")],
    "DA_Arrow_EMP": [("effect_vfx", "NS_EmpPulse")],
    "DA_Arrow_Explosive": [("effect_vfx", "NS_Explosion"), ("ground_decal", "M_Decal_Scorch")],
    "DA_Arrow_Confetti": [("effect_vfx", "NS_PaperBurst")],
}


def _vfx_asset(name):
    """A Niagara system by NS_ name, or a material from /Game/VFX/Materials."""
    import create_vfx  # noqa: PLC0415 - only this step needs it

    if name.startswith("NS_"):
        return c.load_or_none(c.asset_path(create_vfx.VFX_PATH, name))
    return c.load_or_none(c.asset_path(create_vfx.MATERIALS_PATH, name))


def _apply_vfx_values(target, values, context):
    """Sets each (property, name) on target that differs. Returns the properties changed."""
    changed = []
    for prop, name in values:
        wanted = _vfx_asset(name)
        if wanted is None:
            c.log("skipped", context + "." + prop, name + " not built; run create_vfx")
            continue
        try:
            current = target.get_editor_property(prop)
        except Exception:  # noqa: BLE001 - set_props reports a missing property
            current = None
        if _asset_key(current) == _asset_key(wanted):
            continue
        if c.set_props(target, [(prop, wanted)], context):
            changed.append(prop)
    return changed


def apply_vfx_defaults():
    """Points every class that spawns an effect at its Niagara system. Saves only what changed."""
    by_blueprint = {}
    for path, name, component, values in VFX_DEFAULTS:
        by_blueprint.setdefault((path, name), []).append((component, values))

    for (path, name), entries in by_blueprint.items():
        full = c.asset_path(path, name)
        bp = c.load_or_none(full)
        cdo = c.blueprint_cdo(bp) if bp is not None else None
        if cdo is None:
            c.log("skipped", full + " effects", "Blueprint not found")
            continue
        changed = []
        for component, values in entries:
            target = cdo
            if component:
                try:
                    target = cdo.get_editor_property(component)
                except Exception:  # noqa: BLE001
                    target = None
            if target is None:
                c.log("skipped", "{0}.{1}".format(name, component), "no such component")
                continue
            changed += _apply_vfx_values(target, values, name + ("." + component if component else ""))
        if changed:
            c.compile_blueprint(bp)
            c.save(bp)
            c.log("updated", full, "effects: " + ", ".join(changed))
        else:
            c.log("exists", full, "effects already set")

    for arrow, values in VFX_ARROWS.items():
        full = c.asset_path(AUDIO_WEAPON_PATH, arrow)
        asset = c.load_or_none(full)
        if asset is None:
            c.log("skipped", full + " effects", "data asset not found")
            continue
        changed = _apply_vfx_values(asset, values, arrow)
        if changed:
            c.save(asset)
            c.log("updated", full, "effects: " + ", ".join(changed))
        else:
            c.log("exists", full, "effects already set")


if __name__ == "__main__":
    run()
    c.print_summary("blueprints")
