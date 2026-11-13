"""Create the stage-2 world, AI and HUD Blueprints and wire their class defaults.

    /Game/Blueprints/UI/WBP_Hud                parent UHawkeyeHudWidget, HotbarWidgetClass
    /Game/Blueprints/AI/BP_Thug                parent AThugCharacter, mannequin mesh in a red
                                               tracksuit (M_ThugTracksuit, M_ThugTrim), idle,
                                               walk and run clips, a bat for Bat thugs
    /Game/Blueprints/World/BP_GrappleAnchor    parent AGrappleAnchor, 40 cm dark steel cube
    /Game/Blueprints/World/BP_TraversableBlock parent the Game Animation Sample's
                                               LevelBlock_Traversable, level-style lookup off

Then:

    BP_HawkeyePlayerController.HudWidgetClass = WBP_Hud_C

Meshes are /Engine/BasicShapes/Cube scaled into shape, so nothing here needs art. Every step
is idempotent: an existing Blueprint is loaded and only missing defaults are set.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _materials as m  # noqa: E402
import create_blueprints as cb  # noqa: E402

WORLD_PATH = "/Game/Blueprints/World"
AI_PATH = "/Game/Blueprints/AI"
UI_PATH = "/Game/Blueprints/UI"
PLAYER_PATH = "/Game/Blueprints/Player"

# The UE4 mannequin, copied out of the engine's Standard/Mannequin feature pack. Its assets
# hard-reference /Game/Mannequin/..., so the folder keeps that name rather than moving under
# Content/Characters. Only the sequences are used: the pack's AnimBP does not compile in a
# headless editor, so thugs drive ThirdPersonIdle / ThirdPersonWalk directly.
MANNEQUIN_MESH_PATH = "/Game/Mannequin/Character/Mesh/SK_Mannequin"
MANNEQUIN_PHYSICS_ASSET_PATH = "/Game/Mannequin/Character/Mesh/SK_Mannequin_PhysicsAsset"
MANNEQUIN_IDLE_PATH = "/Game/Mannequin/Animations/ThirdPersonIdle"
MANNEQUIN_WALK_PATH = "/Game/Mannequin/Animations/ThirdPersonWalk"
MANNEQUIN_RUN_PATH = "/Game/Mannequin/Animations/ThirdPersonRun"

THUG_MATERIAL_PATH = "/Game/Characters/Thug"

# The Tracksuit Mafia look (docs/DESIGN.md, Cast). SK_Mannequin has two material slots: the body
# (head included) and the chest logo patch. So the ski mask and the side stripes are drawn by the
# body material from the mesh's own bind-pose (pre-skinned) position and normal, and the second
# slot is the light trim. Both carry the HitFlash and Telegraph parameters AThugCharacter pulses.
M_THUG_TRACKSUIT = THUG_MATERIAL_PATH + "/M_ThugTracksuit"
M_THUG_TRIM = THUG_MATERIAL_PATH + "/M_ThugTrim"
THUG_MATERIAL_VERSION = "tracksuit-4"     # bump to rebuild both graphs on the next run
THUG_VERSION_TAG = "CastleVersion"  # pre-rename key, kept: saved materials carry it
TRACKSUIT_RED = (0.6, 0.05, 0.05)
TRACKSUIT_STRIPE = (0.85, 0.85, 0.85)
SKI_MASK = (0.012, 0.012, 0.012)
MASK_BOTTOM_CM = 152.0      # bind-pose height above the feet where the black ski mask starts
VEST_BOTTOM_CM = 92.0       # bind-pose heights of the heavy's vest: the belt and the collar
VEST_TOP_CM = 146.0
STRIPE_FACING = 0.85        # how squarely a surface must face out sideways to be stripe
STRIPE_MAX_SIDE_CM = 28.0   # further out than this is an arm (bind pose is a T), never stripe
HIT_FLASH_COLOR = (1.0, 0.85, 0.7)
HIT_FLASH_INTENSITY = 3.0
TELEGRAPH_COLOR = (1.0, 0.04, 0.01)
TELEGRAPH_INTENSITY = 3.0

# The bat: the engine cylinder (100 cm tall, 100 across) scaled to 85 x 6 cm, in the right hand.
CYLINDER_PATH = "/Engine/BasicShapes/Cylinder"
BAT_SCALE = unreal.Vector(0.06, 0.06, 0.85)
# The cylinder's length (its Z) laid along hand_r's X, which on this mannequin's right side points
# back up the arm, so the bat hangs down past the fingers, gripped at its top.
BAT_LOCATION = unreal.Vector(-36.0, 0.0, 0.0)
BAT_ROTATION = unreal.Rotator(0.0, -90.0, 0.0)

# The template's own offsets: the mesh hangs from the capsule centre and faces +X.
THUG_MESH_LOCATION = unreal.Vector(0.0, 0.0, -96.0)
THUG_MESH_ROTATION = unreal.Rotator(0.0, 0.0, -90.0)


def _scalar_param(material, name, x, y):
    return m.expr(material, "MaterialExpressionScalarParameter", x, y,
                  [("parameter_name", name), ("default_value", 0.0)], name)


def _pulse_emissive(material, head_mask, x=-700, y=500):
    """HitFlash (a warm white over everything) plus Telegraph (red, only where head_mask is 1)."""
    flash = _scalar_param(material, "HitFlash", x, y)
    flash_color = m.constant3(material, HIT_FLASH_COLOR, x, y + 150)
    flash_rgb = m.multiply(material, flash_color, flash, x + 250, y)
    flash_rgb = m.multiply(material, flash_rgb, None, x + 450, y, const_b=HIT_FLASH_INTENSITY)
    if head_mask is None:
        return flash_rgb
    tele = _scalar_param(material, "Telegraph", x, y + 300)
    tele_color = m.constant3(material, TELEGRAPH_COLOR, x, y + 450)
    tele_on = m.multiply(material, tele, head_mask, x + 250, y + 300)
    tele_rgb = m.multiply(material, tele_color, tele_on, x + 450, y + 300)
    tele_rgb = m.multiply(material, tele_rgb, None, x + 650, y + 300, const_b=TELEGRAPH_INTENSITY)
    return m.add(material, flash_rgb, tele_rgb, x + 850, y + 150)


def _to_pixel(material, vertex_node, x, y):
    """A MaterialExpressionVertexInterpolator fed vertex_node: its output is usable per pixel."""
    node = m.expr(material, "MaterialExpressionVertexInterpolator", x, y, None, "VertexInterpolator")
    m.connect(vertex_node, "", node, "VS")
    return node


def tracksuit_builder(suit_rgb, stripe_rgb, mask_rgb, vest_rgb=None):
    """A build function for the tracksuit graph in these colours (the archer wears it in grey and purple).

    vest_rgb, when given, paints a vest over the torso (the heavy's black one), arms and stripes left bare."""
    return lambda material: _build_tracksuit(material, suit_rgb, stripe_rgb, mask_rgb, vest_rgb)


def _build_thug_tracksuit(material):
    """Red tracksuit, light stripes down the outside of the legs and body, a black ski mask."""
    _build_tracksuit(material, TRACKSUIT_RED, TRACKSUIT_STRIPE, SKI_MASK)


def _build_tracksuit(material, suit_rgb, stripe_rgb, mask_rgb, vest_rgb=None):
    """Suit colour, stripes down the outside of the legs and body, a ski mask over the head, and
    optionally a vest over the torso."""
    # Pre-skinned position and normal exist only in the vertex shader; a vertex interpolator
    # carries them to the pixel shader so the mask and stripe edges stay crisp per pixel.
    pos = _to_pixel(material, m.expr(
        material, "MaterialExpressionPreSkinnedPosition", -1900, -300, None, "PreSkinnedPosition"), -1750, -300)
    nrm = _to_pixel(material, m.expr(
        material, "MaterialExpressionPreSkinnedNormal", -1900, 0, None, "PreSkinnedNormal"), -1750, 0)
    z = m.component_mask(material, pos, b=True, x=-1400, y=-400)
    head = m.step(material, z, MASK_BOTTOM_CM, -1200, -400, sharpness=0.5)
    px = m.component_mask(material, pos, r=True, x=-1400, y=-200)
    nx = m.component_mask(material, nrm, r=True, x=-1400, y=0)
    # sign(px) as px / (|px| + e): +1 on her right half, -1 on her left.
    side_abs = m.absolute(material, px, -1250, -250)
    side_sign = m.divide(material, px, m.add(material, side_abs, None, -1100, -250, const_b=0.01), -950, -250)
    outward = m.multiply(material, nx, side_sign, -800, -100)
    facing_out = m.step(material, outward, STRIPE_FACING, -650, -100, sharpness=20.0)
    arm = m.step(material, side_abs, STRIPE_MAX_SIDE_CM, -650, 50, sharpness=0.5)
    stripe = m.lerp(material, facing_out, None, arm, -450, -50, const_b=0.0)

    red = m.constant3(material, suit_rgb, -700, -500)
    white = m.constant3(material, stripe_rgb, -700, -350)
    mask = m.constant3(material, mask_rgb, -700, -200)
    suit = m.lerp(material, red, white, stripe, -500, -400)
    if vest_rgb is not None:
        # Torso between the belt and the collar, arms excluded (bind pose is a T, so the arms are
        # everything further out than STRIPE_MAX_SIDE_CM).
        above_belt = m.step(material, z, VEST_BOTTOM_CM, -1200, -700, sharpness=0.5)
        below_collar = m.step(material, z, VEST_TOP_CM, -1200, -850, sharpness=0.5)
        torso = m.lerp(material, above_belt, None, below_collar, -1000, -750, const_b=0.0)
        vest_mask = m.lerp(material, torso, None, arm, -850, -750, const_b=0.0)
        vest = m.constant3(material, vest_rgb, -700, -650)
        suit = m.lerp(material, suit, vest, vest_mask, -500, -600)
    base = m.lerp(material, suit, mask, head, -300, -300)
    m.connect_property(base, unreal.MaterialProperty.MP_BASE_COLOR)
    m.set_scalar_property(material, 0.45, unreal.MaterialProperty.MP_ROUGHNESS, -300, 0)
    m.connect_property(_pulse_emissive(material, head), unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def trim_builder(rgb):
    """A build function for the chest-patch graph in this colour."""
    return lambda material: _build_trim(material, rgb)


def _build_thug_trim(material):
    """The chest patch: the same light grey as the stripes, flashing with the body."""
    _build_trim(material, TRACKSUIT_STRIPE)


def _build_trim(material, rgb):
    m.connect_property(m.constant3(material, rgb, -700, -200),
                       unreal.MaterialProperty.MP_BASE_COLOR)
    m.set_scalar_property(material, 0.45, unreal.MaterialProperty.MP_ROUGHNESS, -700, 0)
    m.connect_property(_pulse_emissive(material, None), unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _ensure_versioned_material(full_path, build_fn):
    """ensure_material, rebuilt when its CastleVersion metadata is not THUG_MATERIAL_VERSION."""
    existing = c.load_or_none(full_path)
    current = None
    if existing is not None:
        current = unreal.EditorAssetLibrary.get_metadata_tag(existing, THUG_VERSION_TAG)
    rebuild = existing is not None and current != THUG_MATERIAL_VERSION
    material = m.ensure_material(full_path, build_fn, rebuild=rebuild, skeletal=True)
    if material is not None and current != THUG_MATERIAL_VERSION:
        unreal.EditorAssetLibrary.set_metadata_tag(material, THUG_VERSION_TAG, THUG_MATERIAL_VERSION)
        c.save(material)
    return material


def ensure_thug_materials():
    """M_ThugTracksuit and M_ThugTrim at /Game/Characters/Thug. Idempotent.

    Both go on SK_Mannequin, so both need bUsedWithSkeletalMesh; without it the thugs wore the
    grey engine default and the log filled with "missing usage flag SkeletalMesh!". The prison
    build's M_ThugBody and M_ThugVisor are left on disk, unused.
    """
    c.ensure_directory(THUG_MATERIAL_PATH)
    return {
        "body": _ensure_versioned_material(M_THUG_TRACKSUIT, _build_thug_tracksuit),
        "visor": _ensure_versioned_material(M_THUG_TRIM, _build_thug_trim),
    }


def set_component_material(component, slot, material, context):
    """component.set_material(slot, material) when it is not already that. Returns True if set."""
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


def set_thug_materials(bp, materials):
    """Tracksuit on slot 0 (body and head), trim on slot 1 (the chest logo patch)."""
    cdo = c.blueprint_cdo(bp)
    component = None
    if cdo is not None:
        try:
            component = cdo.get_editor_property("mesh")
        except Exception:  # noqa: BLE001
            component = None
    if component is None:
        c.log("skipped", "BP_Thug.Mesh materials", "no inherited mesh component")
        return False

    changed = set_component_material(component, 0, materials.get("body"), "BP_Thug.Mesh")
    changed = set_component_material(component, 1, materials.get("visor"), "BP_Thug.Mesh") or changed
    return changed


M_ANCHOR_STEEL = m.MATERIALS_PATH + "/M_AnchorSteel"
ANCHOR_NAME = "BP_GrappleAnchor"


def _build_anchor_steel(material):
    """Dark, worn steel: reads as a fitting against the grey roofs without shouting."""
    color = m.constant3(material, (0.035, 0.037, 0.04), -400, -200)
    m.connect_property(color, unreal.MaterialProperty.MP_BASE_COLOR)
    m.set_scalar_property(material, 0.45, unreal.MaterialProperty.MP_ROUGHNESS, -400, 0)
    m.set_scalar_property(material, 0.9, unreal.MaterialProperty.MP_METALLIC, -400, 150)


def make_grapple_anchor():
    """BP_GrappleAnchor: AGrappleAnchor's 40 cm cube in dark steel. The C++ sets mesh and size;
    only the material is data. generate_city.py places it on the district's roofs."""
    parent = c.find_class("GrappleAnchor", "/Script/Hawkeye.GrappleAnchor")
    bp, created = cb.make_blueprint(ANCHOR_NAME, WORLD_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)

    steel = m.ensure_material(M_ANCHOR_STEEL, _build_anchor_steel)
    cdo = c.blueprint_cdo(bp)
    component = None
    if cdo is not None:
        try:
            component = cdo.get_editor_property("mesh")
        except Exception:  # noqa: BLE001
            component = None
    if component is None:
        c.log("skipped", ANCHOR_NAME + ".Mesh", "no mesh component")
        return bp
    if set_component_material(component, 0, steel, ANCHOR_NAME + ".Mesh"):
        c.compile_blueprint(bp)
        c.save(bp)
    else:
        c.log("exists", c.asset_path(WORLD_PATH, ANCHOR_NAME), "dark steel already set")
    return bp


TRAVERSABLE_BLOCK_NAME = "BP_TraversableBlock"
TRAVERSABLE_PARENT = "/Game/Levels/LevelPrototyping/LevelBlock_Traversable.LevelBlock_Traversable_C"
# LevelBlock's construction script looks up the sample's LevelVisuals actor (sky, sun, fog and
# post process for the sample's own map) for its grid colours unless UseLevelVisualsColor is off,
# and renames the actor after its height unless AutoNameFromHeight is off. The district has no
# LevelVisuals and its labels are generated, so both are off here.
TRAVERSABLE_DEFAULTS = (("UseLevelVisualsColor", False), ("AutoNameFromHeight", False))


def make_traversable_block():
    """BP_TraversableBlock: the sample's traversable block for generate_city.py's roof-edge ledges
    and parkour test blocks. Skipped when the sample is not imported."""
    try:
        parent = unreal.load_class(None, TRAVERSABLE_PARENT)
    except Exception:  # noqa: BLE001 - the sample is optional
        parent = None
    if parent is None:
        c.log("skipped", c.asset_path(WORLD_PATH, TRAVERSABLE_BLOCK_NAME), "LevelBlock_Traversable not imported")
        return None
    bp, created = cb.make_blueprint(TRAVERSABLE_BLOCK_NAME, WORLD_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
    cdo = c.blueprint_cdo(bp)
    changed = created
    for prop, value in TRAVERSABLE_DEFAULTS:
        try:
            if cdo.get_editor_property(prop) != value:
                cdo.set_editor_property(prop, value)
                changed = True
        except Exception as exc:  # noqa: BLE001
            c.log_error(TRAVERSABLE_BLOCK_NAME + "." + prop, exc)
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", c.asset_path(WORLD_PATH, TRAVERSABLE_BLOCK_NAME), "level-style lookup and auto name off")
    return bp


def make_hud():
    widget_parent = c.find_class("HawkeyeHudWidget", "/Script/Hawkeye.HawkeyeHudWidget")
    bp, _ = cb.make_blueprint("WBP_Hud", UI_PATH, widget_parent, ("WidgetBlueprintFactory",))
    if bp is not None:
        c.compile_blueprint(bp)
        c.save(bp, only_if_dirty=True)
        # The hotbar lives inside the HUD's own overlay, so the HUD is what holds its class.
        cb.apply_defaults(bp, "WBP_Hud", UI_PATH,
                          [("hotbar_widget_class", c.load_generated_class(UI_PATH, "WBP_Hotbar"))])
    return bp


def make_thug():
    parent = c.find_class("ThugCharacter", "/Script/Hawkeye.ThugCharacter")
    bp, _ = cb.make_blueprint("BP_Thug", AI_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None

    c.compile_blueprint(bp)
    c.save(bp, only_if_dirty=True)

    # apply_defaults compares with ==, which is false for two handles to the same UClass, so
    # the controller class would be re-set (and the asset re-saved) on every run. Compare names.
    controller_class = c.find_class("ThugAIController", "/Script/Hawkeye.ThugAIController")
    values = [("auto_possess_ai", unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)]
    cdo = c.blueprint_cdo(bp)
    current = None
    if cdo is not None:
        try:
            current = cdo.get_editor_property("ai_controller_class")
        except Exception:  # noqa: BLE001
            current = None
    if c.class_name(current) != c.class_name(controller_class):
        values.insert(0, ("ai_controller_class", controller_class))

    changed = bool(cb.apply_defaults(bp, "BP_Thug", AI_PATH, values))

    cdo = c.blueprint_cdo(bp)
    if cdo is not None:
        try:
            capsule = cdo.get_editor_property("capsule_component")
            capsule.set_capsule_size(34.0, 96.0)
        except Exception as exc:  # noqa: BLE001
            unreal.log_warning("[Hawkeye] skipped   BP_Thug capsule size ({0})".format(exc))
        try:
            movement = cdo.get_editor_property("character_movement")
            movement.set_editor_property("max_walk_speed", 300.0)
        except Exception as exc:  # noqa: BLE001
            unreal.log_warning("[Hawkeye] skipped   BP_Thug walk speed ({0})".format(exc))

    # Idle, walk and run as plain sequences: AThugCharacter swaps between them in Tick, because
    # the pack's AnimBP does not compile headless and left every thug in a T-pose. Run is the
    # melee rush.
    changed = bool(cb.apply_defaults(
        bp, "BP_Thug", AI_PATH,
        [
            ("idle_anim", c.load_or_none(MANNEQUIN_IDLE_PATH)),
            ("walk_anim", c.load_or_none(MANNEQUIN_WALK_PATH)),
            ("run_anim", c.load_or_none(MANNEQUIN_RUN_PATH)),
            ("bat_mesh", c.load_or_none(CYLINDER_PATH)),
        ])) or changed
    changed = set_thug_bat(bp) or changed

    changed = set_thug_mesh(bp) or changed
    changed = set_thug_materials(bp, ensure_thug_materials()) or changed
    changed = remove_thug_body(bp) or changed
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
    return bp


def set_thug_bat(bp):
    """Shape the HeldWeapon component into a bat: scale and grip offset on the CDO template."""
    cdo = c.blueprint_cdo(bp)
    component = None
    if cdo is not None:
        try:
            component = cdo.get_editor_property("held_weapon_component")
        except Exception:  # noqa: BLE001
            component = None
    if component is None:
        c.log("skipped", "BP_Thug.HeldWeapon", "no held weapon component; build the module")
        return False
    changed = []
    for prop, value in (("relative_scale3d", BAT_SCALE), ("relative_location", BAT_LOCATION),
                        ("relative_rotation", BAT_ROTATION)):
        try:
            if component.get_editor_property(prop) == value:
                continue
        except Exception:  # noqa: BLE001
            pass
        if c.set_props(component, [(prop, value)], "BP_Thug.HeldWeapon"):
            changed.append(prop)
    if changed:
        c.log("updated", "BP_Thug.HeldWeapon", ", ".join(changed))
    return bool(changed)


def subobject_object(sds, handle, bp):
    """The UObject behind a subobject handle, across the spellings UE 5.x has used."""
    data = sds.k2_find_subobject_data_from_handle(handle)
    if data is None:
        return None
    lib = getattr(unreal, "SubobjectDataBlueprintFunctionLibrary", None)
    for getter, args in (
        (getattr(lib, "get_object_for_blueprint", None), (data, bp)),
        (getattr(lib, "get_object", None), (data,)),
    ):
        if getter is None:
            continue
        try:
            found = getter(*args)
            if found is not None:
                return found
        except Exception:  # noqa: BLE001 - try the next spelling
            continue
    return None


def find_subobject_handle(sds, bp, name):
    """Handle of the Blueprint component called ``name``, or None."""
    handles = sds.k2_gather_subobject_data_for_blueprint(bp)
    for handle in handles or []:
        found = subobject_object(sds, handle, bp)
        if found is not None and name in found.get_name():
            return handle, found
    return None, None


def set_mannequin_physics_asset():
    """Re-point SK_Mannequin at its physics asset.

    The mesh's PhysicsAsset reference does not survive the file copy out of the feature pack,
    and without it AThugCharacter::GoLimp refuses to ragdoll and only logs a warning.
    """
    mesh_asset = c.load_or_none(MANNEQUIN_MESH_PATH)
    physics_asset = c.load_or_none(MANNEQUIN_PHYSICS_ASSET_PATH)
    if mesh_asset is None or physics_asset is None:
        c.log("skipped", "SK_Mannequin.physics_asset", "mesh or physics asset not found")
        return False

    try:
        if mesh_asset.get_editor_property("physics_asset") == physics_asset:
            c.log("exists", "SK_Mannequin.physics_asset", "already SK_Mannequin_PhysicsAsset")
            return False
    except Exception:  # noqa: BLE001 - set_props reports a missing property
        pass

    if not c.set_props(mesh_asset, [("physics_asset", physics_asset)], "SK_Mannequin"):
        return False

    c.save(mesh_asset)
    c.log("updated", "SK_Mannequin.physics_asset", "SK_Mannequin_PhysicsAsset")
    return True


def set_thug_mesh(bp):
    """Point BP_Thug's inherited SkeletalMeshComponent at the mannequin.

    Written on the Blueprint CDO's component template, which is what every spawned thug
    copies - the same thing a designer does in the Components panel.
    """
    mesh_asset = c.load_or_none(MANNEQUIN_MESH_PATH)
    if mesh_asset is None:
        c.log("skipped", "BP_Thug.Mesh", MANNEQUIN_MESH_PATH + " not found")
        return False

    cdo = c.blueprint_cdo(bp)
    component = None
    if cdo is not None:
        try:
            component = cdo.get_editor_property("mesh")
        except Exception:  # noqa: BLE001
            component = None
    if component is None:
        c.log("skipped", "BP_Thug.Mesh", "no inherited mesh component")
        return False

    changed = []

    try:
        if component.get_editor_property("skeletal_mesh_asset") != mesh_asset:
            component.set_skeletal_mesh_asset(mesh_asset)
            changed.append("skeletal_mesh_asset")
    except Exception as exc:  # noqa: BLE001
        c.log_error("BP_Thug.Mesh skeletal_mesh_asset", exc)

    for prop, value in (
        ("relative_location", THUG_MESH_LOCATION),
        ("relative_rotation", THUG_MESH_ROTATION),
    ):
        try:
            if component.get_editor_property(prop) == value:
                continue
        except Exception:  # noqa: BLE001 - set_props reports a missing property
            pass
        if c.set_props(component, [(prop, value)], "BP_Thug.Mesh"):
            changed.append(prop)

    # No AnimBP: the mannequin pack's ThirdPerson_AnimBP does not compile in a headless editor,
    # so every thug drove nothing and stood in a T-pose. AThugCharacter plays IdleAnim and
    # WalkAnim on the single-node slot instead, which needs this mode set on the template.
    try:
        if component.get_editor_property("animation_mode") != unreal.AnimationMode.ANIMATION_SINGLE_NODE:
            if c.set_props(
                component,
                [("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)],
                "BP_Thug.Mesh",
            ):
                changed.append("animation_mode")
    except Exception as exc:  # noqa: BLE001
        c.log_error("BP_Thug.Mesh animation_mode", exc)

    if changed:
        c.log("updated", "BP_Thug.Mesh", ", ".join(changed))
        return True

    c.log("exists", "BP_Thug.Mesh", "mannequin already assigned")
    return False


def remove_thug_body(bp):
    """Delete the grey cylinder stand-in now that BP_Thug has a real skeletal mesh.

    The component was called GuardBody before the pivot, and an old save may still carry it.
    """
    getter = getattr(unreal, "get_engine_subsystem", None)
    if getter is None or not hasattr(unreal, "SubobjectDataSubsystem"):
        return False

    try:
        sds = getter(unreal.SubobjectDataSubsystem)
        handle, found = find_subobject_handle(sds, bp, "GuardBody")
        if handle is None or found is None:
            return False

        sds.delete_subobject(handle, handle, bp)
        c.log("updated", "BP_Thug.GuardBody", "removed; the mannequin replaces it")
        return True
    except Exception as exc:  # noqa: BLE001
        c.log_error("BP_Thug.ThugBody removal", exc)
        return False


def wire_hud_into_controller():
    bp = c.load_or_none(c.asset_path(PLAYER_PATH, "BP_HawkeyePlayerController"))
    if bp is None:
        c.log("skipped", "BP_HawkeyePlayerController.hud_widget_class", "Blueprint not found")
        return
    hud_class = c.load_generated_class(UI_PATH, "WBP_Hud")
    cb.apply_defaults(
        bp, "BP_HawkeyePlayerController", PLAYER_PATH, [("hud_widget_class", hud_class)]
    )


def run():
    for path in (WORLD_PATH, AI_PATH, UI_PATH):
        c.ensure_directory(path)

    make_hud()
    set_mannequin_physics_asset()
    make_thug()
    make_grapple_anchor()
    make_traversable_block()
    wire_hud_into_controller()


if __name__ == "__main__":
    run()
    c.print_summary("world blueprints")
