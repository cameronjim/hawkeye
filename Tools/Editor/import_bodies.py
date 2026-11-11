"""Real bodies for the thugs, Clint and Lucky, from Quaternius's CC0 packs (claude-docs/animation.md, "Bodies").

The packs live outside the repo (HAWKEYE_ASSETS_ROOT, Tools\\local.json "assetsRoot", else
C:\\Users\\camer\\code\\hawkeye-assets) and are read as data only: their files are handed to the engine's importer, nothing
in them is run. Everything this script makes is git-ignored and rebuilt from those files, like the Game Animation Sample
copy and Kate's look; BP_Thug, BP_Thug_Heavy and BP_Clint (committed) carry only soft references, so a clone without
the packs keeps the mannequins and loads as before.

    /Game/Characters/Bodies/SK_Body_Male            Universal Base Characters' Superhero_Male (glTF), no physics asset:
                                                    the driver keeps the bodies (hit contact, hit reactions, the
                                                    ragdoll); its skeleton bears the UE mannequin's bone names
    /Game/Characters/Bodies/T_Body_Male_*, T_Eye_*  its textures (base colour, normal, roughness, eyes, brows)
    /Game/Characters/Bodies/M_BodyLook              skin from the texture, clothes painted over it from the bind pose
                                                    (top, legs, shoes, stripes, an optional ski mask and vest), plus
                                                    the HitFlash and Telegraph parameters AThugCharacter pulses
    /Game/Characters/Bodies/MI_Body_<Who>           the thug's dark red tracksuit and trainers, the heavy's vest, ski
                                                    mask and boots, Clint's black sleeveless vest, forearm guard,
                                                    chevron, trousers and boots, the eyes and brows
    /Game/Characters/Bodies/IK_Body_Male            IK Rig on it: the manifest's ue4 chains and a goal per limb
    /Game/Characters/<Who>/Look/RTG_<Who>_To_Body   from the driver's rig (IK_Thug_Mannequin, IK_Kate_UEFN)
    /Game/Characters/<Who>/Look/ABP_<Who>_Body      Retarget Pose From Mesh, the parent (driver) component
    BP_Thug, BP_Thug_Heavy, BP_Clint .BodyLook      the soft references; every other child of BP_Thug (the archer,
                                                    the elves, the reindeer, the bosses) is set to no look and keeps
                                                    its own mannequin colours
    /Game/Characters/Lucky/SK_Lucky, A_Lucky_*      a stand-in for Lucky (a golden retriever): the animals pack's Pug
                                                    (FBX, rigged, its own clips), tinted golden (M_Lucky_Coat,
                                                    MI_Lucky_Golden*). LUCKY_SOURCE swaps it. Nothing uses it yet:
                                                    there is no Lucky actor

Idempotent: an import is skipped when its asset exists and carries the source file's size and time in its build tag;
every built asset carries a build tag and is rebuilt only when it changes; a Blueprint is saved only when its BodyLook
differs.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _materials as m  # noqa: E402

BUILD_TAG = "HawkeyeBuild"
DEFAULT_ASSETS_ROOT = r"C:\Users\camer\code\hawkeye-assets"

BODIES = "/Game/Characters/Bodies"
LUCKY = "/Game/Characters/Lucky"
MALE_MESH = BODIES + "/SK_Body_Male"

BASE_DIR = ("quaternius-base-characters", "Universal Base Characters[Standard]", "Base Characters")
MALE_GLTF = BASE_DIR + ("Godot - UE", "Superhero_Male_FullBody.gltf")
TEXTURES = BASE_DIR + ("Textures",)
# Lucky is a golden retriever. No retriever mesh is on disk, so the animals pack's Pug stands in: this is the one line
# to change for a real one (and LUCKY_SCALE, and LUCKY_COATS' slot words, below).
LUCKY_SOURCE = ("quaternius-animals", "FBX", "Pug.fbx")
# The 2018 Pug comes in 264 cm tall and 351 cm long at scale 1; at 0.12 it stands about 32 cm at the head.
LUCKY_SCALE = 0.12

# Texture file -> (asset name, kind). Kind: color (sRGB), normal, linear (roughness).
MALE_TEXTURES = [
    ("T_Superhero_Male_Dark.png", "T_Body_Male_D", "color"),
    ("T_Superhero_Male_Normal.png", "T_Body_Male_N", "normal"),
    ("T_Superhero_Male_Roughness.png", "T_Body_Male_R", "linear"),
    ("T_Eye_Brown.png", "T_Body_Eye_D", "color"),
    ("T_Hair_1_BaseColor.png", "T_Body_Hair_D", "color"),
]


def assets_root():
    for candidate in (os.environ.get("HAWKEYE_ASSETS_ROOT", ""), DEFAULT_ASSETS_ROOT):
        if candidate and os.path.isdir(candidate):
            return candidate
    return os.environ.get("HAWKEYE_ASSETS_ROOT", "") or DEFAULT_ASSETS_ROOT


def source_file(parts):
    return os.path.join(assets_root(), *parts)


def _file_stamp(path):
    st = os.stat(path)
    return "{0}:{1}:{2}".format(os.path.basename(path), st.st_size, int(st.st_mtime))


def tag_of(asset):
    try:
        return unreal.EditorAssetLibrary.get_metadata_tag(asset, BUILD_TAG) or ""
    except Exception:  # noqa: BLE001
        return ""


def set_tag(asset, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, BUILD_TAG, value)


# --------------------------------------------------------------------------------------------------
# imports
# --------------------------------------------------------------------------------------------------


def _interchange_pipeline(materials, animations, scale=1.0):
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    if scale != 1.0:
        c.set_props(pipeline, [("import_offset_uniform_scale", scale)], "interchange pipeline")
    c.set_props(pipeline.get_editor_property("mesh_pipeline"), [
        ("import_static_meshes", False), ("import_skeletal_meshes", True), ("create_physics_asset", False),
    ], "interchange mesh pipeline")
    c.set_props(pipeline.get_editor_property("animation_pipeline"), [("import_animations", animations)],
                "interchange animation pipeline")
    material_pipeline = pipeline.get_editor_property("material_pipeline")
    c.set_props(material_pipeline, [("import_materials", materials)], "interchange material pipeline")
    # The glTF names textures (T_*_Normal_png.png) the pack does not ship; ours come from its Textures folder.
    c.set_props(material_pipeline.get_editor_property("texture_pipeline"), [("import_textures", materials)],
                "interchange texture pipeline")
    return pipeline


def _interchange_import(filename, destination, pipeline):
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    source = unreal.InterchangeManager.create_source_data(filename)
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
    result = manager.import_asset(destination, source, params)
    if isinstance(result, tuple):
        ok, objects = result[0], list(result[1] or [])
    else:
        ok, objects = bool(result), []
    return ok, objects


def _assets_under(folder):
    out = []
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False):
        asset = unreal.load_asset(path.split(".")[0])
        if asset is not None:
            out.append(asset)
    return out


def _rename(asset, wanted_path):
    current = asset.get_path_name().split(".")[0]
    if current == wanted_path:
        return asset
    if c.exists(wanted_path):
        unreal.EditorAssetLibrary.delete_asset(wanted_path)
    if unreal.EditorAssetLibrary.rename_asset(current, wanted_path):
        return c.load_or_none(wanted_path)
    c.log("FAILED", wanted_path, "could not rename " + current)
    return asset


def import_male():
    """SK_Body_Male from the glTF (no materials, no physics asset). Returns the mesh or None."""
    path = source_file(MALE_GLTF)
    if not os.path.isfile(path):
        c.log("skipped", MALE_MESH, "no " + path)
        return None
    stamp = "male-1|" + _file_stamp(path)
    existing = c.load_or_none(MALE_MESH)
    if existing is not None and tag_of(existing) == stamp:
        c.log("exists", MALE_MESH)
        return existing
    staging = BODIES + "/Import_Male"
    c.ensure_directory(staging)
    ok, objects = _interchange_import(path, staging, _interchange_pipeline(materials=False, animations=False))
    found = [a for a in _assets_under(staging)]
    mesh = next((a for a in found if isinstance(a, unreal.SkeletalMesh)), None)
    skeleton = next((a for a in found if isinstance(a, unreal.Skeleton)), None)
    if mesh is None:
        c.log("FAILED", MALE_MESH, "the import made no skeletal mesh (ok={0}, {1} objects, {2} in the folder)".format(
            ok, len(objects), len(found)))
        return None
    if skeleton is not None:
        skeleton = _rename(skeleton, BODIES + "/SKEL_Body_Male")
    mesh = _rename(mesh, MALE_MESH)
    for leftover in _assets_under(staging):
        unreal.EditorAssetLibrary.delete_asset(leftover.get_path_name().split(".")[0])
    unreal.EditorAssetLibrary.delete_directory(staging)
    mesh.set_editor_property("physics_asset", None)
    set_tag(mesh, stamp)
    c.save(mesh)
    if skeleton is not None:
        c.save(skeleton)
    c.log("imported", MALE_MESH, "from " + os.path.basename(path))
    return mesh


def _texture_settings(texture, kind):
    if kind == "normal":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property("srgb", False)
    elif kind == "linear":
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        texture.set_editor_property("srgb", False)
    else:
        texture.set_editor_property("srgb", True)


def import_textures():
    """{asset name: Texture2D} for MALE_TEXTURES, imported once per source file."""
    out = {}
    folder = source_file(TEXTURES)
    for filename, name, kind in MALE_TEXTURES:
        path = os.path.join(folder, filename)
        full = BODIES + "/" + name
        if not os.path.isfile(path):
            c.log("skipped", full, "no " + path)
            continue
        stamp = "tex-1|{0}|{1}".format(kind, _file_stamp(path))
        existing = c.load_or_none(full)
        if existing is not None and tag_of(existing) == stamp:
            out[name] = existing
            c.log("exists", full)
            continue
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = BODIES
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        c.asset_tools().import_asset_tasks([task])
        texture = c.load_or_none(full)
        if texture is None:
            c.log("FAILED", full, "the import made no texture from " + filename)
            continue
        _texture_settings(texture, kind)
        set_tag(texture, stamp)
        c.save(texture)
        c.log("imported", full, "{0} from {1}".format(kind, filename))
        out[name] = texture
    return out




def import_lucky():
    """SK_Lucky and its clips from LUCKY_SOURCE (a stand-in pug), rigged as it ships. Returns the mesh or None."""
    path = source_file(LUCKY_SOURCE)
    full = LUCKY + "/SK_Lucky"
    if not os.path.isfile(path):
        c.log("skipped", full, "no " + path)
        return None
    stamp = "lucky-2|{0}|{1}".format(LUCKY_SCALE, _file_stamp(path))
    existing = c.load_or_none(full)
    if existing is not None and tag_of(existing) == stamp:
        c.log("exists", full)
        return existing
    if existing is not None:
        # A stale import (another scale, a newer file): nothing outside the folder uses it yet.
        unreal.EditorAssetLibrary.delete_directory(LUCKY)
    staging = LUCKY + "/Import"
    c.ensure_directory(staging)
    _interchange_import(path, staging, _interchange_pipeline(materials=True, animations=True, scale=LUCKY_SCALE))
    found = _assets_under(staging)
    mesh = next((a for a in found if isinstance(a, unreal.SkeletalMesh)), None)
    if mesh is None:
        c.log("FAILED", full, "the import made no skeletal mesh ({0} assets)".format(len(found)))
        return None
    names = []
    for asset in found:
        if isinstance(asset, unreal.SkeletalMesh):
            continue
        if isinstance(asset, unreal.Skeleton):
            wanted = LUCKY + "/SKEL_Lucky"
        elif isinstance(asset, unreal.AnimSequence):
            wanted = LUCKY + "/A_Lucky_" + (asset.get_name().split("_")[-1] or "Clip")
        elif isinstance(asset, unreal.PhysicsAsset):
            wanted = LUCKY + "/PHYS_Lucky"
        elif isinstance(asset, unreal.MaterialInterface):
            wanted = LUCKY + "/MI_Lucky_" + asset.get_name().replace("MI_", "").replace("M_", "")
        else:
            wanted = LUCKY + "/" + asset.get_name()
        renamed = _rename(asset, wanted)
        if renamed is not None:
            c.save(renamed)
            names.append(renamed.get_name())
    mesh = _rename(mesh, full)
    unreal.EditorAssetLibrary.delete_directory(staging)
    set_tag(mesh, stamp)
    c.save(mesh)
    c.log("imported", full, "from {0}, with {1}".format(os.path.basename(path), ", ".join(sorted(names))))
    return mesh


# Lucky's coat: the stand-in's two colours (its beige body, its brown ears, muzzle and paws) turned golden.
M_LUCKY_COAT = LUCKY + "/M_Lucky_Coat"
LUCKY_COATS = [("Beige", "MI_Lucky_Golden", (0.36, 0.18, 0.045)), ("Brown", "MI_Lucky_GoldenDark", (0.16, 0.07, 0.02))]


def _build_coat(material):
    m.connect_property(m.vector_param(material, "Coat", (0.5, 0.3, 0.1), -500, -200), unreal.MaterialProperty.MP_BASE_COLOR)
    m.set_scalar_property(material, 0.85, unreal.MaterialProperty.MP_ROUGHNESS, -500, 0)
    m.set_scalar_property(material, 0.2, unreal.MaterialProperty.MP_SPECULAR, -500, 100)


def tint_lucky(mesh):
    """Golden instances on SK_Lucky's slots, matched by the stand-in's slot names. Saves the mesh only on a change."""
    coat = m.ensure_material(M_LUCKY_COAT, _build_coat, skeletal=True)
    if coat is None:
        return
    slots = list(mesh.get_editor_property("materials"))
    changed = []
    for slot in slots:
        name = str(slot.get_editor_property("material_slot_name"))
        current = slot.get_editor_property("material_interface")
        current_name = current.get_name() if current is not None else ""
        for word, mi_name, rgb in LUCKY_COATS:
            if word.lower() in name.lower() or word.lower() in current_name.lower():
                instance = m.ensure_material_instance(LUCKY + "/" + mi_name, coat, vectors=[("Coat", rgb)])
                if instance is not None and current != instance:
                    slot.set_editor_property("material_interface", instance)
                    changed.append("{0} = {1}".format(name, mi_name))
                break
    if changed:
        mesh.set_editor_property("materials", slots)
        c.save(mesh)
        c.log("updated", LUCKY + "/SK_Lucky", "golden: " + ", ".join(changed))
    else:
        c.log("exists", LUCKY + "/SK_Lucky materials", "golden")


def report_mesh(mesh):
    """Height, length and a few reference-pose bones for the log: the check for a 100x scale or a turned import."""
    skeleton = mesh.get_editor_property("skeleton")
    pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    bones = [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(pose)]
    lines = []
    for bone in ("pelvis", "Head", "hand_l", "foot_l", "ball_l"):
        if bone in bones:
            loc = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
            lines.append("{0} ({1:.0f}, {2:.0f}, {3:.0f})".format(bone, loc.x, loc.y, loc.z))
    bounds = mesh.get_bounds()
    c.log("info", mesh.get_path_name().split(".")[0], "{0} bones, top {1:.0f} cm, bottom {2:.0f} cm, {3:.0f} x {4:.0f} cm; {5}".format(
        len(bones), bounds.origin.z + bounds.box_extent.z, bounds.origin.z - bounds.box_extent.z,
        2.0 * bounds.box_extent.x, 2.0 * bounds.box_extent.y, "; ".join(lines) or ", ".join(bones[:8])))


# --------------------------------------------------------------------------------------------------
# materials: skin from the pack's texture, clothes painted from the bind pose
# --------------------------------------------------------------------------------------------------

M_BODY = BODIES + "/M_BodyLook"
MATERIAL_BUILD = "body-material-5"   # bump when the graph below changes

# Bind-pose cm on SK_Body_Male (a T-pose, facing +Y, arms along +X for the left, -X for the right; report_mesh
# prints the bones). The pack's Standard edition has only the Superhero male, so the clothes are what make him read
# as dressed: cloth is matte, low in specular and takes no normal map, so the sculpted muscles do not show through as
# skin detail.
COLLAR_Z = 154.0         # the top's collar, over the T-pose's shoulders; above it is head and neck (and the
                         # telegraph's glow)
MASK_Z = 153.0           # a ski mask's lower edge
WAIST_Z = 96.0           # the top's hem; below it is legs
BELT_LOW_Z, BELT_HIGH_Z = 93.0, 99.0
CUFF_X = 66.0            # sleeves end at the wrist (hand_l is at x 70.6)
TORSO_X = 28.0           # further out than this is an arm
ARMHOLE_X = 19.0         # a sleeveless top's armhole: bare from here out (the shoulder joint is at about 18)
GUARD_X = (43.0, 64.0)   # the forearm guard, on the left forearm (+x): elbow to wrist
STRIPE_MIN_X = 9.0       # no stripe nearer the middle than this (the crotch faces sideways too)
VEST_LOW_Z, VEST_HIGH_Z = 92.0, 146.0
STRIPE_FACING = 0.85
CHEVRON_TIP_Z, CHEVRON_SLOPE, CHEVRON_HALF, CHEVRON_MAX_X = 128.0, 0.8, 1.6, 7.0
BEARD_LOW_Z, BEARD_HIGH_Z = 155.0, 165.0   # the jaw below the mouth, front of the face only
# Painted short hair: above a hairline that runs up over the brow (y +10) and down to the nape (y -12).
HAIRLINE_SLOPE = 0.55

RED = (0.20, 0.012, 0.014)
# MI name -> vector and scalar parameters.
#   Thugs: the Tracksuit Mafia in dark red with light stripes, white trainers, buzzed near-black hair, faces bare.
#   The heavy: the same tracksuit under a black vest, a ski mask, black boots.
#   Clint: matte black, sleeveless tactical vest over bare arms, a guard on his left forearm, a small purple chevron
#   on the chest, a belt, black trousers, dark boots to mid-shin, brown hair, stubble. Not the Ronin look.
LOOKS = {
    "MI_Body_Thug": ([("TopColor", RED), ("LegsColor", RED), ("StripeColor", (0.72, 0.72, 0.72)),
                      ("ShoeColor", (0.62, 0.62, 0.6)), ("HairColor", (0.012, 0.010, 0.009))],
                     [("StripeOn", 1.0), ("MaskOn", 0.0), ("VestOn", 0.0), ("HairOn", 1.0), ("HairlineZ", 172.5),
                      ("BootTop", 16.0), ("ClothRoughness", 0.75)]),
    "MI_Body_Heavy": ([("TopColor", RED), ("LegsColor", RED), ("StripeColor", (0.72, 0.72, 0.72)),
                       ("ShoeColor", (0.018, 0.017, 0.016)), ("MaskColor", (0.012, 0.012, 0.012)),
                       ("VestColor", (0.015, 0.015, 0.016))],
                      [("StripeOn", 1.0), ("MaskOn", 1.0), ("VestOn", 1.0), ("HairOn", 0.0), ("BootTop", 24.0),
                       ("ClothRoughness", 0.75)]),
    "MI_Body_Clint": ([("TopColor", (0.013, 0.013, 0.014)), ("LegsColor", (0.009, 0.009, 0.010)),
                       ("ShoeColor", (0.035, 0.024, 0.016)), ("GuardColor", (0.024, 0.024, 0.026)),
                       ("ChevronColor", (0.16, 0.03, 0.30)), ("BeltColor", (0.016, 0.012, 0.009)),
                       ("HairColor", (0.07, 0.045, 0.028)), ("BeardColor", (0.09, 0.06, 0.045))],
                      [("StripeOn", 0.0), ("MaskOn", 0.0), ("VestOn", 0.0), ("HairOn", 1.0), ("HairlineZ", 169.5),
                       ("SleevesOn", 0.0), ("GuardOn", 1.0), ("ChevronOn", 1.0), ("BeltOn", 1.0), ("BeardOn", 0.55),
                       ("BootTop", 30.0), ("ClothRoughness", 0.9)]),
}
# The eyes and brows: their texture alone, tinted, no clothes, no skin detail. (texture, roughness, tint)
PLAIN_LOOKS = {
    "MI_Body_Eyes": ("T_Body_Eye_D", 0.25, (1.0, 1.0, 1.0)),
    "MI_Body_Brows": ("T_Body_Hair_D", 0.7, (0.12, 0.08, 0.06)),
}


def _tex_sample(material, texture, x, y, sampler=None, param=None):
    props = [("texture", texture)]
    if sampler is not None:
        props.append(("sampler_type", sampler))
    if param:
        props.append(("parameter_name", param))
        return m.expr(material, "MaterialExpressionTextureSampleParameter2D", x, y, props, param)
    return m.expr(material, "MaterialExpressionTextureSample", x, y, props, "TextureSample")


def _one_minus(material, node, x, y):
    return m.lerp(material, None, None, node, x, y, const_a=1.0, const_b=0.0)


def _on(material, name, default, x, y):
    return m.scalar_param(material, name, default, x, y)


def _build_body(textures):
    import create_world_blueprints as wb  # noqa: PLC0415 - the thug's pulse and pixel helpers

    def build(material):
        pos = wb._to_pixel(material, m.expr(material, "MaterialExpressionPreSkinnedPosition", -2600, -300, None,
                                            "PreSkinnedPosition"), -2450, -300)
        nrm = wb._to_pixel(material, m.expr(material, "MaterialExpressionPreSkinnedNormal", -2600, 100, None,
                                            "PreSkinnedNormal"), -2450, 100)
        px = m.component_mask(material, pos, r=True, x=-2250, y=-400)
        py = m.component_mask(material, pos, g=True, x=-2250, y=-150)
        z = m.component_mask(material, pos, b=True, x=-2250, y=-250)
        nx = m.component_mask(material, nrm, r=True, x=-2250, y=100)
        ny = m.component_mask(material, nrm, g=True, x=-2250, y=150)
        nz = m.component_mask(material, nrm, b=True, x=-2250, y=200)
        side = m.absolute(material, px, -2100, -400)

        base = _tex_sample(material, textures["T_Body_Male_D"], -1500, -900, param="BaseTex")
        normal = _tex_sample(material, textures["T_Body_Male_N"], -1500, 700,
                             sampler=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        rough = _tex_sample(material, textures["T_Body_Male_R"], -1500, 1000,
                            sampler=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        clothed = _on(material, "Clothed", 1.0, -1900, -1100)
        detail = _on(material, "Detail", 1.0, -1900, 900)

        head = m.step(material, z, COLLAR_Z, -1900, -250, sharpness=0.8)
        hands = m.step(material, side, CUFF_X, -1900, -400, sharpness=0.8)
        boot_top = _on(material, "BootTop", 14.0, -2100, -100)
        boots = m.below(material, m.subtract(material, z, boot_top, -1950, -100), 0.0, -1900, -100, sharpness=0.8)
        legs = m.below(material, z, WAIST_Z, -1900, 0, sharpness=0.8)
        arm = m.step(material, side, TORSO_X, -1900, 250, sharpness=0.5)
        sleeve = m.multiply(material, arm, _one_minus(material, hands, -1750, 300), -1600, 250)
        armhole = m.multiply(material, m.step(material, side, ARMHOLE_X, -1900, 1900, sharpness=0.8),
                             _one_minus(material, hands, -1750, 1950), -1600, 1950)
        guard = m.multiply(material, m.band(material, px, GUARD_X[0], GUARD_X[1], -1900, 1700, sharpness=0.8),
                           _on(material, "GuardOn", 0.0, -1600, 1750), -1450, 1700)
        # Bare arms when the top has no sleeves (Clint's vest), except under the guard.
        bare_arm = m.multiply(material, m.multiply(material, armhole, _one_minus(material, _on(material, "SleevesOn", 1.0,
                                                                                                -1750, 1850), -1600, 1850),
                                                   -1450, 1850),
                              _one_minus(material, guard, -1450, 1950), -1300, 1850)
        # Skin shows on the head, the hands and any bare arm; everything else is clothed (when Clothed is 1).
        skin_bits = m.maximum(material, m.maximum(material, head, hands, -1750, -350), bare_arm, -1600, -350)
        cloth = m.multiply(material, _one_minus(material, skin_bits, -1450, -350), clothed, -1300, -350)

        top = m.vector_param(material, "TopColor", RED, -1300, -700)
        legs_rgb = m.vector_param(material, "LegsColor", RED, -1300, -600)
        suit = m.lerp(material, top, legs_rgb, legs, -1100, -650)

        belt = m.multiply(material, m.band(material, z, BELT_LOW_Z, BELT_HIGH_Z, -1900, 2100, sharpness=0.8),
                          m.multiply(material, _on(material, "BeltOn", 0.0, -1600, 2150), _one_minus(material, arm, -1600,
                                                                                                       2250), -1450, 2150),
                          -1300, 2100)
        suit = m.lerp(material, suit, m.vector_param(material, "BeltColor", (0.03, 0.02, 0.01), -1100, -560), belt,
                      -1000, -650)

        # Side stripes down the torso and legs, and along the top of the arms (the T-pose's outside of the sleeve).
        side_sign = m.divide(material, px, m.add(material, side, None, -1900, 400, const_b=0.01), -1750, 400)
        outward = m.multiply(material, nx, side_sign, -1600, 400)
        body_stripe = m.lerp(material, m.step(material, outward, STRIPE_FACING, -1450, 400, sharpness=20.0), None, arm,
                             -1250, 400, const_b=0.0)
        body_stripe = m.multiply(material, body_stripe, m.step(material, side, STRIPE_MIN_X, -1450, 480, sharpness=0.5),
                                 -1150, 400)
        arm_stripe = m.multiply(material, m.step(material, nz, STRIPE_FACING, -1450, 550, sharpness=20.0), sleeve,
                                -1250, 550)
        stripe_on = m.multiply(material, _on(material, "StripeOn", 1.0, -1250, 300),
                               _one_minus(material, boots, -1250, 750), -1100, 300)
        stripe = m.multiply(material, m.maximum(material, body_stripe, arm_stripe, -1100, 450), stripe_on, -950, 400)
        suit = m.lerp(material, suit, m.vector_param(material, "StripeColor", (0.7, 0.7, 0.7), -1100, -500), stripe,
                      -850, -600)

        vest_band = m.band(material, z, VEST_LOW_Z, VEST_HIGH_Z, -1900, 1300, sharpness=0.5)
        vest = m.multiply(material, m.lerp(material, vest_band, None, arm, -1450, 1300, const_b=0.0),
                          _on(material, "VestOn", 0.0, -1450, 1450), -1300, 1300)
        suit = m.lerp(material, suit, m.vector_param(material, "VestColor", (0.015, 0.015, 0.015), -1100, -400), vest,
                      -750, -600)

        suit = m.lerp(material, suit, m.vector_param(material, "GuardColor", (0.02, 0.02, 0.02), -1100, -300), guard,
                      -650, -600)

        # A small chevron on the chest: a V by the bind pose's height against |x|, on the front only.
        line = m.add(material, m.multiply(material, side, None, -1900, 2400, const_b=CHEVRON_SLOPE), None, -1750, 2400,
                     const_b=CHEVRON_TIP_Z)
        off = m.absolute(material, m.subtract(material, z, line, -1600, 2400), -1450, 2400)
        chevron = m.mul_all(material, [m.below(material, off, CHEVRON_HALF, -1300, 2400, 3.0),
                                       m.below(material, side, CHEVRON_MAX_X, -1300, 2500, 3.0),
                                       m.step(material, ny, 0.3, -1300, 2600, 10.0),
                                       _on(material, "ChevronOn", 0.0, -1300, 2700)], -1100, 2450)
        suit = m.lerp(material, suit, m.vector_param(material, "ChevronColor", (0.16, 0.03, 0.30), -1100, -200), chevron,
                      -550, -600)

        suit = m.lerp(material, suit, m.vector_param(material, "ShoeColor", (0.02, 0.02, 0.02), -1100, -100), boots,
                      -450, -600)

        # Plain (eyes, brows): the texture under its tint. Clothed: short hair painted over the scalp, stubble on the jaw.
        plain_tint = m.lerp(material, m.vector_param(material, "PlainTint", (1.0, 1.0, 1.0), -1100, -1150), None,
                            clothed, -950, -1150, const_b=1.0)
        skin = m.multiply(material, base, plain_tint, -800, -1100)
        hairline = m.add(material, m.multiply(material, py, None, -1900, -1300, const_b=HAIRLINE_SLOPE),
                         _on(material, "HairlineZ", 170.5, -1900, -1400), -1750, -1300)
        hair = m.multiply(material, m.step(material, m.subtract(material, z, hairline, -1600, -1300), 0.0, -1450, -1300,
                                           sharpness=1.0),
                          m.multiply(material, _on(material, "HairOn", 0.0, -1450, -1450), clothed, -1300, -1450),
                          -1100, -1300)
        beard = m.mul_all(material, [m.band(material, z, BEARD_LOW_Z, BEARD_HIGH_Z, -1900, -1600, sharpness=0.6),
                                     m.step(material, py, 2.0, -1900, -1700, 0.5),
                                     _on(material, "BeardOn", 0.0, -1900, -1800), clothed], -1600, -1650)
        skin = m.lerp(material, skin, m.vector_param(material, "BeardColor", (0.08, 0.05, 0.04), -800, -1450), beard,
                      -700, -1150)
        skin = m.lerp(material, skin, m.vector_param(material, "HairColor", (0.03, 0.02, 0.015), -800, -1300), hair,
                      -600, -1150)
        color = m.lerp(material, skin, suit, cloth, -350, -700)

        mask_on = m.multiply(material, _on(material, "MaskOn", 0.0, -1450, -150), clothed, -1300, -150)
        mask = m.multiply(material, m.step(material, z, MASK_Z, -1900, -200, sharpness=0.8), mask_on, -1100, -200)
        color = m.lerp(material, color, m.vector_param(material, "MaskColor", (0.012, 0.012, 0.012), -350, -500), mask,
                       -200, -600)
        m.connect_property(color, unreal.MaterialProperty.MP_BASE_COLOR)

        # Skin keeps the texture's normals and roughness; cloth is flat (no muscle detail), matte and dull.
        flat = m.constant3(material, (0.0, 0.0, 1.0), -1100, 600)
        bump = m.multiply(material, _one_minus(material, cloth, -1100, 800), detail, -950, 800)
        m.connect_property(m.lerp(material, flat, normal, bump, -800, 650), unreal.MaterialProperty.MP_NORMAL)
        surface_rough = m.lerp(material, rough, _on(material, "ClothRoughness", 0.8, -1100, 1000), cloth, -950, 1000)
        m.connect_property(m.lerp(material, _on(material, "PlainRoughness", 0.5, -1100, 1150), surface_rough,
                                  detail, -800, 1000), unreal.MaterialProperty.MP_ROUGHNESS)
        m.connect_property(m.lerp(material, None, None, cloth, -800, 1250, const_a=0.5, const_b=0.15),
                           unreal.MaterialProperty.MP_SPECULAR)
        m.connect_property(wb._pulse_emissive(material, head, x=-1100, y=1600), unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    return build


def _set_texture(instance, param, texture, label):
    lib = unreal.MaterialEditingLibrary
    try:
        current = lib.get_material_instance_texture_parameter_value(instance, param)
    except Exception:  # noqa: BLE001
        current = None
    if current == texture:
        return False
    lib.set_material_instance_texture_parameter_value(instance, param, texture)
    c.save(instance)
    c.log("updated", label, "{0} = {1}".format(param, texture.get_name()))
    return True


def ensure_materials(textures):
    """{MI name: instance} for LOOKS and PLAIN_LOOKS, on M_BodyLook. None when a texture is missing."""
    missing = [name for _file, name, _kind in MALE_TEXTURES if name not in textures]
    if missing:
        c.log("skipped", M_BODY, "no " + ", ".join(missing))
        return None
    existing = c.load_or_none(M_BODY)
    rebuild = existing is not None and tag_of(existing) != MATERIAL_BUILD
    material = m.ensure_material(M_BODY, _build_body(textures), rebuild=rebuild, skeletal=True)
    if material is None:
        return None
    if tag_of(material) != MATERIAL_BUILD:
        set_tag(material, MATERIAL_BUILD)
        c.save(material)
    out = {}
    for name, (vectors, scalars) in LOOKS.items():
        out[name] = m.ensure_material_instance(BODIES + "/" + name, material, vectors=vectors,
                                               scalars=scalars + [("Clothed", 1.0), ("Detail", 1.0)])
    for name, (texture, roughness, tint) in PLAIN_LOOKS.items():
        instance = m.ensure_material_instance(BODIES + "/" + name, material, vectors=[("PlainTint", tint)],
                                              scalars=[("Clothed", 0.0), ("Detail", 0.0), ("PlainRoughness", roughness)])
        if instance is not None:
            _set_texture(instance, "BaseTex", textures[texture], BODIES + "/" + name)
        out[name] = instance
    return out


# --------------------------------------------------------------------------------------------------
# the retarget from each driver, and the Blueprints
# --------------------------------------------------------------------------------------------------

RIG = BODIES + "/IK_Body_Male"
THUG_LOOK = "/Game/Characters/Thug/Look"
CLINT_LOOK = "/Game/Characters/Clint/Look"
# Who wears the body: (Blueprint, the manifest target its driver is, look folder, short name, MI for the body slot).
WEARERS = [
    ("/Game/Blueprints/AI/BP_Thug", "thug", THUG_LOOK, "Thug", "MI_Body_Thug"),
    ("/Game/Blueprints/AI/BP_Thug_Heavy", "thug", THUG_LOOK, "Thug", "MI_Body_Heavy"),
    ("/Game/Blueprints/Player/BP_Clint", "uefn", CLINT_LOOK, "Clint", "MI_Body_Clint"),
]
# Children of BP_Thug that keep their own mannequin colours (the archer's grey, the elves, the reindeer, the bosses):
# each is written no look explicitly, or it would inherit BP_Thug's.
KEEP_MANNEQUIN = [
    "/Game/Blueprints/AI/BP_Thug_Elf",
    "/Game/Blueprints/AI/BP_Thug_Reindeer",
    "/Game/Blueprints/Bosses/BP_Archer",
    "/Game/Blueprints/Bosses/BP_Boss_Santa",
    "/Game/Blueprints/Bosses/BP_Boss_Tomas",
    "/Game/Blueprints/Bosses/BP_Thug_CandyCane",
]
# Slot name on SK_Body_Male -> MI (None: the wearer's body MI).
SLOT_LOOKS = {"MI_Hair_1": "MI_Body_Brows", "MI_Eyes": "MI_Body_Eyes"}


def _slot_materials(mesh, materials, body_mi):
    out = []
    for slot in mesh.get_editor_property("materials"):
        name = str(slot.get_editor_property("material_slot_name"))
        out.append(materials.get(SLOT_LOOKS.get(name) or body_mi))
    return out


def apply_look(bp_path, mesh, anim_class, materials):
    """BodyLook on a Blueprint's defaults (mesh None: no look). True when it changed."""
    import create_kate_look as kl  # noqa: PLC0415

    bp = c.load_or_none(bp_path)
    cdo = c.blueprint_cdo(bp) if bp is not None else None
    label = bp_path + ".BodyLook"
    if cdo is None:
        c.log("skipped", label, "no Blueprint")
        return False
    look = cdo.get_editor_property("body_look")
    wanted = (kl._path_of(mesh), kl._path_of(anim_class), [kl._path_of(x) for x in materials])
    have = (kl._path_of(look.get_editor_property("mesh")), kl._path_of(look.get_editor_property("anim_class")),
            [kl._path_of(x) for x in look.get_editor_property("materials")])
    what = "{0}, {1} materials".format(mesh.get_name(), len(materials)) if mesh is not None else "none: its own mannequin"
    if have == wanted:
        c.log("exists", label, what)
        return False
    new_look = unreal.HawkeyeBodyLook()
    if mesh is not None:
        new_look.set_editor_property("mesh", mesh)
        new_look.set_editor_property("anim_class", anim_class)
        new_look.set_editor_property("materials", list(materials))
    cdo.set_editor_property("body_look", new_look)
    c.compile_blueprint(bp)
    c.save(bp)
    c.log("updated", label, what)
    return True


def ensure_looks(mesh, materials):
    import create_kate_look as kl  # noqa: PLC0415
    import import_combat_anims as ica  # noqa: PLC0415

    manifest = ica.load_manifest()
    chain_maps = manifest["chain_maps"]
    c.ensure_directory(BODIES)
    rig = kl.ensure_rig(mesh, chain_maps["ue4"], path=RIG)
    if rig is None:
        return
    built = {}
    for bp_path, target_name, folder, who, body_mi in WEARERS:
        if who not in built:
            built[who] = None
            source = ica.ensure_target(target_name, manifest["targets"][target_name], chain_maps)
            if source is None:
                c.log("skipped", bp_path + ".BodyLook", "no driver rig for " + target_name)
                continue
            c.ensure_directory(folder)
            rtg = kl.ensure_retargeter(source["rig"], source["mesh"], rig, mesh,
                                       path="{0}/RTG_{1}_To_Body".format(folder, who))
            if rtg is not None:
                built[who] = kl.ensure_anim_bp(mesh, rtg, path="{0}/ABP_{1}_Body".format(folder, who))
        abp = built[who]
        if abp is not None:
            apply_look(bp_path, mesh, abp.generated_class(), _slot_materials(mesh, materials, body_mi))
    # After BP_Thug: a child compiled before its parent changes would take the parent's look back.
    for bp_path in KEEP_MANNEQUIN:
        apply_look(bp_path, None, None, [])


def run():
    root = assets_root()
    if not os.path.isdir(root):
        c.log("skipped", "bodies", "no asset packs at {0} (set HAWKEYE_ASSETS_ROOT); thugs and Clint keep the "
              "mannequins".format(root))
        return None
    c.ensure_directory(BODIES)
    lucky = import_lucky()
    if lucky is not None:
        tint_lucky(lucky)
        report_mesh(lucky)
    mesh = import_male()
    if mesh is None:
        return None
    report_mesh(mesh)
    materials = ensure_materials(import_textures())
    if materials is None:
        return None
    ensure_looks(mesh, materials)
    return mesh


if __name__ == "__main__":
    run()
    c.print_summary("bodies")
