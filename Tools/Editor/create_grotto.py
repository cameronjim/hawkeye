"""Create Act 1 chapter 3's content: the grotto on the street, the stealth interior's dressing, the elves, Santa and his
fight (claude-docs/gameplay-semantics.md, "Chapter sites", "Chapter interiors", "Bosses"; docs/chapters/CH03.md).

    /Game/Missions/Sites/DA_Site_CH03_Grotto        UChapterSiteDefinition for the district, planned by _grotto.plan_grotto
                                                    from the OSM records: the frontage (a "photos" sign over the door,
                                                    candy-striped posts, fake snow, a garland and its bulbs), the door into
                                                    L_Int_Grotto (AInteriorEntrance), its doorstep and the roof point the
                                                    roof stairs come out at, the find_sleigh and leave_roof volumes, and the
                                                    ornament by Safehouse 1's door (examine_ornament, after the roof)
    /Game/Missions/Sites/DA_Site_CH03_GrottoInside  UChapterSiteDefinition for L_Int_Grotto (its Map): the throne, the
                                                    queue's posts and the fake snow, the toys, four sleighs and the watches,
                                                    the keypad by the workshop door (open_vault), the enter_grotto volume,
                                                    the alarm box (a switch only the boomerang trips) and the triggers (the
                                                    first elf, the overheard talk, the boomerang on the stockroom shelf)
    /Game/Missions/Bosses/DA_Boss_CH03_Santa        UBossDefinition: Santa in the workshop (his Map is L_Int_Grotto), one
                                                    phase (beat_santa), a sack swing, a second and an overhead slam; met at
                                                    open_vault; his line before, Kate's after
    /Game/Blueprints/AI/BP_Thug_Elf                 child of BP_Thug: a green tracksuit with a red stripe, his face bare,
                                                    and an elf hat with ears and a radio earpiece (AThugCharacter::Headwear)
    /Game/Blueprints/Bosses/BP_Boss_Santa           child of BP_Thug: a red suit with white trim, a Santa hat and beard, a
                                                    sack in his bat slot
    /Game/Kit/Grotto/SM_Kit_*                       the greybox meshes

Nothing is placed in a map: UChapterSiteSubsystem dresses the street and the interior when DA_CH03_Grotto runs, and
UBossFightSubsystem stands Santa in the workshop. The interior's walls, doors and elves are generate_interior.py's,
from Tools/Interiors/Grotto.json (it needs BP_Thug_Elf, so this runs first).

Idempotent: meshes carry a recipe hash, the assets a plan hash; each is rewritten only when it changes.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript -script="Tools\\Editor\\create_grotto.py" -unattended -nullrhi
"""

import hashlib
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import _grotto  # noqa: E402
import _materials as m  # noqa: E402

SITE_PATH = "/Game/Missions/Sites"
SITE_NAME = "DA_Site_CH03_Grotto"
INSIDE_NAME = "DA_Site_CH03_GrottoInside"
BOSS_PATH = "/Game/Missions/Bosses"
BOSS_NAME = "DA_Boss_CH03_Santa"
KIT_PATH = "/Game/Kit/Grotto"
AI_PATH = "/Game/Blueprints/AI"
BP_PATH = "/Game/Blueprints/Bosses"
ELF_BP = "BP_Thug_Elf"
SANTA_BP = "BP_Boss_Santa"
CHAR_PATH = "/Game/Characters/Grotto"
M_ELF_SUIT = CHAR_PATH + "/M_ElfTracksuit"
M_ELF_TRIM = CHAR_PATH + "/M_ElfTrim"
M_SANTA_SUIT = CHAR_PATH + "/M_SantaSuit"
M_SANTA_TRIM = CHAR_PATH + "/M_SantaTrim"
MATERIAL_VERSION = "grotto-1"
ELF_SUIT = (0.03, 0.22, 0.06)           # elf green
ELF_STRIPE = (0.55, 0.04, 0.04)         # red
ELF_FACE = (0.55, 0.36, 0.26)           # no mask: the mask colour is his face
SANTA_SUIT = (0.5, 0.02, 0.025)
SANTA_TRIM = (0.82, 0.82, 0.8)
SANTA_FACE = (0.62, 0.4, 0.3)
HEADWEAR_OFFSET = (2.0, 0.0, 9.0)
SACK_SCALE = 0.85                       # drawn like the candy cane: built in true cm, 100 cm along Z
BUILD_TAG = "HawkeyeBuild"
MESH_VERSION = 1

INTERIOR_MAP = _grotto.INTERIOR_MAP
DOOR_NAME = "[Santa's Grotto]"
KEYPAD_PROMPT = "[Open the workshop door]"
ORNAMENT_PROMPT = "[Look]"
# The examines' close-ups in their actor's frame (cm): (camera, looks at, interact zone's half extent).
KEYPAD_CLOSE_UP = ((170.0, 90.0, 160.0), (0.0, 0.0, 120.0), (150.0, 150.0, 150.0))
ORNAMENT_CLOSE_UP = ((110.0, -70.0, 150.0), (0.0, 0.0, 100.0), (160.0, 160.0, 150.0))
SWITCH_NOISE = _grotto.SWITCH_NOISE     # the corridor's guard, round the archway, comes to look
LIGHT_LUMENS = 140.0
LIGHT_RADIUS = 650.0

# --- Santa (numbers on Normal; gameplay-semantics.md, "Chapter 3's boss") --------------------------------------
BOSS_DISPLAY = "Santa"
MAX_HEALTH = 560.0     # 320 until 2026-11-10's difficulty pass: he went down in 25 s, a thug with a hat
BODY_SCALE = 1.15
# (name, damage, wind-up, recovery, range, knockback, lunge, unblockable): two sack swings he telegraphs, then a slam
# over his head that has to be dodged.
COMBO = [("santa_sack", 14.0, 0.8, 0.5, 160.0, 0.0, 30.0, False),
         ("santa_sack_2", 14.0, 0.7, 0.5, 160.0, 0.0, 30.0, False),
         ("santa_slam", 20.0, 1.1, 0.8, 170.0, 180.0, 50.0, True)]
MOVES = {"winded_after_swings": 4, "winded_seconds": 3.0, "winded_damage_scale": 1.5, "finisher_damage_fraction": 0.08,
         "stagger_threshold": 40.0, "swing_cooldown_seconds": 0.6, "rush_speed": 340.0, "finisher_only_when_winded": True,
         "parry_stagger_seconds": 0.6}
GUARD = 0.3
PRE_FIGHT_SEQUENCE = "seq_ch03_santa"
AFTER_SEQUENCE = "seq_ch03_santa_down"
MEET_OBJECTIVE = "open_vault"
PHASE_OBJECTIVE = "beat_santa"
DEFEAT_BEAT = (2.0, (60.0, 240.0, 170.0), 0.85, 60.0, [])
RIM_LIGHT = ((1.0, 0.92, 0.85), 0.0, 320.0)   # none: the workshop's tubes light him (a rim light washed the red out)

COLOURS = {
    "ElfGreen": ((0.04, 0.3, 0.08), 0.7),
    "ElfRed": ((0.6, 0.04, 0.05), 0.6),
    "White": ((0.85, 0.85, 0.83), 0.6),
    "Skin": ((0.58, 0.38, 0.27), 0.6),
    "Black": ((0.015, 0.015, 0.018), 0.4),
    "SantaRed": ((0.55, 0.02, 0.025), 0.5),
    "Burlap": ((0.42, 0.3, 0.16), 0.9),
    "Tie": ((0.8, 0.62, 0.18), 0.35),
    "CaneRed": ((0.6, 0.02, 0.03), 0.3),
    "CaneWhite": ((0.85, 0.85, 0.82), 0.3),
    "Snow": ((0.9, 0.92, 0.95), 0.75),
    "SignRed": ((0.5, 0.03, 0.04), 0.5),
    "Velvet": ((0.38, 0.02, 0.05), 0.85),
    "Gold": ((0.8, 0.6, 0.15), 0.3),
    "SleighRed": ((0.55, 0.03, 0.03), 0.35),
    "Wood": ((0.36, 0.23, 0.12), 0.8),
    "ToyBlue": ((0.05, 0.2, 0.6), 0.5),
    "ToyYellow": ((0.85, 0.65, 0.05), 0.5),
    "Grey": ((0.25, 0.25, 0.27), 0.5),
    "Garland": ((0.03, 0.18, 0.05), 0.85),
}
GLOWS = {
    "MI_GrottoKeypad": ((1.0, 0.05, 0.03), 0.8),
    "MI_GrottoBulb": ((1.0, 0.62, 0.25), 1.0),
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


def _cone(mesh, slot, cx, cy, z0, base, top, height, steps=14, pitch=0.0, roll=0.0):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(roll, pitch, 0.0))
    GS_PRIM.append_cone(mesh, _opts(slot), xf, base, top, height, steps, 0, True, BASE)


def _sphere(mesh, slot, cx, cy, cz, radius, scale=(1.0, 1.0, 1.0), steps=12):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, cz), scale=unreal.Vector(*scale))
    GS_PRIM.append_sphere_lat_long(mesh, _opts(slot), xf, radius, steps, steps + 2, CENTER)


# --------------------------------------------------------------------------------------------------
# meshes (origin at the middle of the base, +X forward, unless said otherwise)
# --------------------------------------------------------------------------------------------------


def _build_elf_hat():
    """An elf hat round the middle of his head (+X his face, +Z up): a green cone leaning back with a red band and a white
    bobble, pointed ears, a radio earpiece with its aerial on his right; slots green, red, white, skin, black."""
    mesh = unreal.DynamicMesh()
    _cyl(mesh, 1, 0.0, 0.0, 4.0, 12.0, 4.0, 16)                     # the band
    _cone(mesh, 0, -3.0, 0.0, 8.0, 11.5, 1.5, 30.0, 14, pitch=-28.0)   # the hat, leaning back
    _sphere(mesh, 2, -17.0, 0.0, 34.0, 4.0, steps=8)                # the bobble
    for side in (-1.0, 1.0):
        _cone(mesh, 3, 0.0, side * 11.0, -2.0, 3.2, 0.3, 10.0, 8, roll=side * -70.0)   # the ears, pointing out and up
    _box(mesh, 4, 2.0, 12.5, -6.0, 4.0, 3.0, 5.0)                   # the earpiece
    _cyl(mesh, 4, 0.0, 13.5, -1.0, 0.5, 14.0, 6)                    # its aerial
    return mesh


def _build_santa_head():
    """A Santa hat and beard round his head: slots red, white."""
    mesh = unreal.DynamicMesh()
    _cyl(mesh, 1, 0.0, 0.0, 3.0, 12.5, 5.0, 16)                     # the brim
    _cone(mesh, 0, -2.0, 0.0, 8.0, 12.0, 2.0, 26.0, 14, pitch=-35.0)
    _sphere(mesh, 1, -16.0, 0.0, 26.0, 4.5, steps=8)
    _sphere(mesh, 1, 9.0, 0.0, -14.0, 11.0, (0.8, 1.15, 1.3))       # the beard
    _box(mesh, 1, 10.5, 0.0, -2.0, 4.0, 12.0, 2.5)                  # the moustache
    return mesh


def _build_sack():
    """Santa's sack 100 cm along Z centred on the origin (the bat slot's frame): a gathered neck he holds, tied, and the
    stuffed sack at the far (+Z) end; slots burlap, tie."""
    mesh = unreal.DynamicMesh()
    _cyl(mesh, 0, 0.0, 0.0, -50.0, 3.5, 52.0, 10)                   # the neck, in his fist
    _cyl(mesh, 1, 0.0, 0.0, 0.0, 5.0, 4.0, 12)                      # the tie
    _sphere(mesh, 0, 0.0, 0.0, 27.0, 23.0, (1.0, 1.0, 1.15))         # the sack
    return mesh


def _build_post():
    """A candy-striped post 110 cm tall, a ball on top; slots red, white."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 1, 0.0, 0.0, 0.0, 26.0, 26.0, 4.0)
    z, slot = 4.0, 0
    while z < 104.0:
        _cyl(mesh, slot, 0.0, 0.0, z, 5.0, 10.0, 12)
        z += 10.0
        slot = 1 - slot
    _sphere(mesh, 0, 0.0, 0.0, 110.0, 7.0, steps=10)
    return mesh


def _build_snow():
    """A heap of fake snow 120 cm across, 28 cm high; slot snow."""
    mesh = unreal.DynamicMesh()
    _sphere(mesh, 0, 0.0, 0.0, 0.0, 60.0, (1.0, 0.8, 0.45), steps=14)
    _sphere(mesh, 0, 30.0, 20.0, 0.0, 32.0, (1.0, 1.0, 0.6), steps=10)
    return mesh


def _build_sign():
    """The "photos" sign over the door, 200 x 70 cm facing +X: a red board in a white border and a white camera on it
    (no lettering in greybox); slots red, white, black."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 1, 0.0, 0.0, 0.0, 6.0, 210.0, 80.0)                  # the border
    _box(mesh, 0, 1.5, 0.0, 5.0, 6.0, 196.0, 70.0)                  # the board
    _box(mesh, 1, 4.0, -45.0, 20.0, 4.0, 64.0, 40.0)                # the camera's body
    _box(mesh, 1, 4.0, -60.0, 60.0, 4.0, 18.0, 8.0)                 # its flash
    _cyl(mesh, 2, 5.0, -45.0, 40.0, 13.0, 4.0, 16, pitch=90.0)      # its lens
    for k in range(4):                                              # four white bars where the word would go
        _box(mesh, 1, 4.0, 15.0 + k * 22.0, 30.0, 4.0, 14.0, 22.0)
    return mesh


def _build_garland():
    """1 m of garland along X centred on the origin; the prop's scale stretches it; slot green."""
    mesh = unreal.DynamicMesh()
    _cyl(mesh, 0, -50.0, 0.0, 0.0, 6.0, 100.0, 10, pitch=-90.0)
    return mesh


def _build_bulb():
    mesh = unreal.DynamicMesh()
    _sphere(mesh, 0, 0.0, 0.0, 0.0, 6.0, steps=8)
    return mesh


def _build_throne():
    """Santa's throne facing +X: a red velvet seat and a tall rounded back, gold arms and trim; slots velvet, gold."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 1, 0.0, 0.0, 0.0, 110.0, 120.0, 14.0)                # the dais
    _box(mesh, 0, 0.0, 0.0, 14.0, 80.0, 90.0, 38.0)                 # the seat
    _box(mesh, 0, -38.0, 0.0, 14.0, 18.0, 96.0, 150.0)              # the back
    _sphere(mesh, 0, -38.0, 0.0, 164.0, 48.0, (0.2, 1.0, 0.55), steps=12)   # its rounded top
    for side in (-1.0, 1.0):
        _box(mesh, 1, 0.0, side * 48.0, 14.0, 78.0, 10.0, 62.0)     # the arms
    _box(mesh, 1, -28.0, 0.0, 120.0, 4.0, 98.0, 6.0)                # a gold rail across the back
    return mesh


def _build_sleigh():
    """An animated sleigh display, 240 x 110 cm facing +X: runners curling up at the front, a red body with a curved
    prow and a high back, gold trim; slots red, gold, black."""
    mesh = unreal.DynamicMesh()
    for side in (-1.0, 1.0):
        _box(mesh, 2, -10.0, side * 45.0, 0.0, 230.0, 6.0, 6.0)      # the runners
        _cyl(mesh, 2, 108.0, side * 45.0, 0.0, 3.0, 40.0, 8, pitch=-20.0)   # their front curl
        for x in (-70.0, 40.0):
            _box(mesh, 2, x, side * 45.0, 6.0, 6.0, 6.0, 28.0)       # the struts
    _box(mesh, 0, -20.0, 0.0, 34.0, 180.0, 100.0, 40.0)              # the body
    _sphere(mesh, 0, 70.0, 0.0, 60.0, 50.0, (0.7, 1.0, 0.75), steps=12)  # the prow
    _box(mesh, 0, -100.0, 0.0, 74.0, 22.0, 100.0, 60.0)              # the high back
    _box(mesh, 1, -20.0, 0.0, 72.0, 184.0, 104.0, 4.0)               # gold trim round the top
    return mesh


def _build_toys():
    """An open crate of toys, 60 x 45 x 40 cm: slots wood, blue, yellow, red."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 0.0, 60.0, 45.0, 34.0)
    _box(mesh, 1, -12.0, -8.0, 30.0, 18.0, 18.0, 16.0, yaw=20.0)
    _sphere(mesh, 2, 14.0, 6.0, 40.0, 10.0, steps=8)
    _box(mesh, 3, 4.0, -12.0, 32.0, 12.0, 12.0, 20.0, yaw=-15.0)
    return mesh


def _build_watch_tray():
    """A black velvet tray of gold watches, 60 x 30 cm; slots black, gold."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 0.0, 60.0, 30.0, 4.0)
    for i in range(5):
        for j in (-1.0, 1.0):
            _cyl(mesh, 1, -24.0 + i * 12.0, j * 7.0, 4.0, 3.5, 1.5, 10)
    return mesh


def _build_keypad():
    """A keypad on the wall facing +X, its back 5 cm behind the origin, 110 to 135 cm up: a grey box, a red lamp, keys;
    slots grey, black, lamp."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, -2.5, 0.0, 110.0, 5.0, 16.0, 25.0)
    for row in range(3):
        for col in (-1.0, 0.0, 1.0):
            _box(mesh, 1, 0.5, col * 4.0, 114.0 + row * 4.5, 1.0, 3.0, 3.0)
    _box(mesh, 2, 0.5, 0.0, 129.0, 1.0, 8.0, 2.0)
    return mesh


def _build_ornament():
    """The little target ornament on a railing post beside her door, facing +X: the post and a hook, and a flat target
    (red, white, red rings) hanging at 100 cm; slots black, red, white, gold."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 0.0, 20.0, 20.0, 3.0)
    _cyl(mesh, 0, 0.0, 0.0, 3.0, 2.5, 112.0, 8)                      # the post
    _box(mesh, 0, 6.0, 0.0, 112.0, 14.0, 3.0, 3.0)                   # an arm out over the street
    _cyl(mesh, 3, 12.0, 0.0, 104.0, 0.4, 8.0, 6)                     # the thread
    for slot, radius, x in ((1, 7.0, 12.0), (2, 4.8, 12.6), (1, 2.4, 13.2)):
        _cyl(mesh, slot, x, 0.0, 97.0, radius, 0.8, 18, pitch=90.0)  # the rings, flat toward +X
    return mesh


def _material(key):
    if key in GLOWS:
        rgb, intensity = GLOWS[key]
        parent = m.ensure_material(m.M_EMISSIVE, m._build_emissive)
        return m.ensure_material_instance(c.asset_path(KIT_PATH, key), parent, vectors=[(m.EMISSIVE_COLOR_PARAM, rgb)],
                                          scalars=[(m.EMISSIVE_INTENSITY_PARAM, intensity)])
    rgb, rough = COLOURS[key]
    return c.ensure_constant_color_material("M_Grotto" + key, KIT_PATH, rgb, rough)


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
        "elf_hat": _write_mesh("SM_Kit_ElfHat", _build_elf_hat, ("ElfGreen", "ElfRed", "White", "Skin", "Black"), collision=False),
        "santa_head": _write_mesh("SM_Kit_SantaHead", _build_santa_head, ("SantaRed", "White"), collision=False),
        "sack": _write_mesh("SM_Kit_SantaSack", _build_sack, ("Burlap", "Tie"), collision=False),
        "post": _write_mesh("SM_Kit_CandyPost", _build_post, ("CaneRed", "CaneWhite")),
        "snow": _write_mesh("SM_Kit_FakeSnow", _build_snow, ("Snow",), collision=False),
        "sign": _write_mesh("SM_Kit_PhotosSign", _build_sign, ("SignRed", "White", "Black"), collision=False),
        "garland": _write_mesh("SM_Kit_Garland", _build_garland, ("Garland",), collision=False),
        "bulb": _write_mesh("SM_Kit_GrottoBulb", _build_bulb, ("MI_GrottoBulb",), collision=False),
        "throne": _write_mesh("SM_Kit_Throne", _build_throne, ("Velvet", "Gold")),
        "sleigh": _write_mesh("SM_Kit_Sleigh", _build_sleigh, ("SleighRed", "Gold", "Black")),
        "toys": _write_mesh("SM_Kit_ToyCrate", _build_toys, ("Wood", "ToyBlue", "ToyYellow", "SignRed")),
        "watches": _write_mesh("SM_Kit_WatchTray", _build_watch_tray, ("Black", "Gold"), collision=False),
        "keypad": _write_mesh("SM_Kit_Keypad", _build_keypad, ("Grey", "Black", "MI_GrottoKeypad"), collision=False),
        "ornament": _write_mesh("SM_Kit_TargetOrnament", _build_ornament, ("Black", "SignRed", "White", "Gold"), collision=False),
    }
    missing = [k for k, v in meshes.items() if v is None]
    if missing:
        c.log("FAILED", KIT_PATH, "could not build " + ", ".join(sorted(missing)))
        return None
    return meshes


# --------------------------------------------------------------------------------------------------
# the elf and Santa
# --------------------------------------------------------------------------------------------------


def _ensure_body(bp_name, bp_path, suit, trim, suit_rgb, stripe_rgb, face_rgb, headwear, weapon=None, bat=None):
    """A child of BP_Thug in these colours with this headwear (and a weapon, a mesh in his bat slot)."""
    import create_blueprints as cb  # noqa: PLC0415
    import create_enemies as ce  # noqa: PLC0415
    import create_world_blueprints as wb  # noqa: PLC0415

    full = c.asset_path(bp_path, bp_name)
    parent = c.load_generated_class(AI_PATH, "BP_Thug")
    if parent is None or headwear is None:
        c.log("skipped", full, "run create_world_blueprints first")
        return None
    c.ensure_directory(bp_path)
    c.ensure_directory(CHAR_PATH)
    bp, created = cb.make_blueprint(bp_name, bp_path, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.ensure_parent(bp, full, parent)
    suit_material = ce._versioned_material(suit, wb.tracksuit_builder(suit_rgb, stripe_rgb, face_rgb), MATERIAL_VERSION)
    trim_material = ce._versioned_material(trim, wb.trim_builder(stripe_rgb), MATERIAL_VERSION)
    changed = []
    body = cb.cdo_component(bp, "mesh")
    for slot, material in ((0, suit_material), (1, trim_material)):
        if cb.set_material_slot(body, slot, material, bp_name + ".Mesh"):
            changed.append("material {0}".format(slot))
    hat = cb.cdo_component(bp, "headwear")
    if hat is None:
        c.log("FAILED", full, "no Headwear component on AThugCharacter; build the module")
    elif cb.set_if_different(hat, [("static_mesh", headwear)], bp_name + ".Headwear"):
        changed.append("headwear")
    cdo = c.blueprint_cdo(bp)
    if cdo is not None and cb.set_if_different(cdo, [("headwear_offset", unreal.Vector(*HEADWEAR_OFFSET))], bp_name):
        changed.append("headwear offset")
    if cdo is not None and weapon is not None and cdo.get_editor_property("weapon") != weapon:
        c.set_props(cdo, [("weapon", weapon)], bp_name)
        changed.append("weapon")
    if cdo is not None and bat is not None and cdo.get_editor_property("bat_mesh") != bat:
        c.set_props(cdo, [("bat_mesh", bat)], bp_name)
        changed.append("bat mesh")
        held = cb.cdo_component(bp, "held_weapon_component")
        if held is not None:
            cb.set_if_different(held, [("relative_scale3d", unreal.Vector(SACK_SCALE, SACK_SCALE, SACK_SCALE))], bp_name + ".HeldWeapon")
    if changed or created:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated" if not created else "created", full, ", ".join(changed))
    else:
        c.log("exists", full, "look already set")
    return c.load_generated_class(bp_path, bp_name)


def ensure_elf(meshes):
    return _ensure_body(ELF_BP, AI_PATH, M_ELF_SUIT, M_ELF_TRIM, ELF_SUIT, ELF_STRIPE, ELF_FACE, meshes["elf_hat"])


def ensure_santa(meshes):
    return _ensure_body(SANTA_BP, BP_PATH, M_SANTA_SUIT, M_SANTA_TRIM, SANTA_SUIT, SANTA_TRIM, SANTA_FACE, meshes["santa_head"],
                        weapon=unreal.ThugWeapon.BAT, bat=meshes["sack"])


# --------------------------------------------------------------------------------------------------
# the sites
# --------------------------------------------------------------------------------------------------


def _vec(t):
    return unreal.Vector(float(t[0]), float(t[1]), float(t[2]))


def _prop(at, mesh, blocks, scale=(1.0, 1.0, 1.0)):
    prop = unreal.ChapterSiteProp()
    prop.set_editor_property("transform", unreal.Transform(location=_vec(at[:3]), rotation=unreal.Rotator(0.0, 0.0, at[3]),
                                                           scale=unreal.Vector(*scale)))
    prop.set_editor_property("mesh", mesh)
    prop.set_editor_property("blocks", blocks)
    return prop


def _light(at, rgb, lumens=LIGHT_LUMENS, radius=LIGHT_RADIUS):
    light = unreal.ChapterSiteLight()
    light.set_editor_property("location", _vec(at))
    light.set_editor_property("color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    light.set_editor_property("lumens", lumens)
    light.set_editor_property("radius", radius)
    return light


def _volume(objective, at, extent):
    volume = unreal.ChapterSiteVolume()
    volume.set_editor_property("objective_id", unreal.Name(objective))
    volume.set_editor_property("location", _vec(at))
    volume.set_editor_property("extent", _vec(extent))
    return volume


def _examine(objective, at, yaw, prompt, mesh, current, close_up, required=None):
    camera, look, zone = close_up
    spec = unreal.ChapterSiteExamine()
    for prop, value in (("objective_id", unreal.Name(objective)), ("location", _vec(at)), ("yaw", float(yaw)),
                        ("prompt", unreal.Text(prompt)), ("mesh", mesh), ("require_current_objective", current),
                        ("close_up_offset", _vec(camera)), ("close_up_look_at", _vec(look)), ("interact_zone_extent", _vec(zone)),
                        ("close_up_seconds", 2.5), ("required_objective", unreal.Name(required or "None"))):
        spec.set_editor_property(prop, value)
    return spec


def _return_point(label, at, yaw):
    point = unreal.ChapterSiteReturnPoint()
    point.set_editor_property("label", unreal.Name(label))
    point.set_editor_property("location", _vec(at))
    point.set_editor_property("yaw", float(yaw))
    return point


def _map_ready():
    """L_Int_Grotto exists (generate_interior.py makes it after this script's first run; create_all runs this again)."""
    return c.exists(INTERIOR_MAP)


def _soft_world(path):
    if not c.exists(path):
        c.log("skipped", path, "not generated yet; generate_interior.py makes it (create_all runs this script again after)")
        return None
    return c.load_or_none(path)


def _street_fields(plan, meshes):
    props = []
    for kind, at in plan["props"]:
        if kind == "post":
            props.append(_prop(at, meshes["post"], True))
        elif kind == "snow":
            props.append(_prop(at[:4], meshes["snow"], False, (at[4], at[4], at[4])))
        elif kind == "sign":
            props.append(_prop(at, meshes["sign"], False))
        elif kind == "garland":
            props.append(_prop(at[:4], meshes["garland"], False, (at[4] / 100.0, 1.0, 1.0)))
    lights = []
    for at, rgb in plan["lights"]:
        props.append(_prop((at[0], at[1], at[2] + 14.0, 0.0), meshes["bulb"], False))
        lights.append(_light(at, rgb))
    door = unreal.ChapterSiteInteriorDoor()
    for prop, value in (("interior", _soft_world(INTERIOR_MAP)), ("location", _vec(plan["door"])), ("yaw", float(plan["yaw"])),
                        ("display_name", unreal.Text(DOOR_NAME)), ("return_point_label", unreal.Name(_grotto.DOORSTEP)),
                        ("tag", unreal.Name(_grotto.DOOR_TAG))):
        door.set_editor_property(prop, value)
    examines = []
    if plan.get("ornament") is not None:
        ox, oy, oz, oyaw = plan["ornament"]
        examines.append(_examine("examine_ornament", (ox, oy, oz), oyaw, ORNAMENT_PROMPT, meshes["ornament"], False,
                                 ORNAMENT_CLOSE_UP, required="leave_roof"))
    return [
        ("map", None),
        ("player_start", _vec(plan["player_start"])),
        ("player_start_yaw", float(plan["player_yaw"])),
        ("props", props),
        ("lights", lights),
        ("volumes", [_volume("find_sleigh", plan["arrive"], plan["arrive_extent"]),
                     _volume("leave_roof", plan["roof_volume"], plan["roof_extent"])]),
        ("crew", []),
        ("crew_objective", unreal.Name("None")),
        ("crew_alert_objective", unreal.Name("None")),
        ("pickups", []),
        ("examines", examines),
        ("interior_doors", [door]),
        ("return_points", [_return_point(_grotto.DOORSTEP, plan["doorstep"], plan["yaw"]),
                           _return_point(_grotto.ROOF_POINT, plan["roof"], plan["yaw"] + 180.0)]),
        ("triggers", []),
        ("switches", []),
    ]


def _inside_fields(inside, meshes):
    props = []
    for kind, at in inside["props"]:
        if kind == "snow":
            props.append(_prop(at[:4], meshes["snow"], False, (at[4], at[4], at[4])))
        elif kind == "post":
            props.append(_prop(at, meshes["post"], True))
        else:
            props.append(_prop(at, meshes[kind], kind not in ("watches",)))
    lights = [_light(at, rgb, 220.0, 900.0) for at, rgb in inside["lights"]]
    triggers = []
    for event, at, extent, unseen in inside["triggers"]:
        trigger = unreal.ChapterSiteTrigger()
        for prop, value in (("event", unreal.Name(event)), ("location", _vec(at)), ("extent", _vec(extent)), ("only_unseen", unseen)):
            trigger.set_editor_property(prop, value)
        triggers.append(trigger)
    switch = unreal.ChapterSiteSwitch()
    sx, sy, sz, syaw = inside["switch"]
    for prop, value in (("id", unreal.Name(inside["switch_id"])), ("location", _vec((sx, sy, sz))), ("yaw", float(syaw)),
                        ("noise_loudness", SWITCH_NOISE), ("returning_arrows_only", True)):
        switch.set_editor_property(prop, value)
    kx, ky, kz, kyaw = inside["keypad"]
    return [
        ("map", _soft_world(INTERIOR_MAP)),
        ("player_start", _vec((0.0, 0.0, 0.0))),
        ("player_start_yaw", 0.0),
        ("props", props),
        ("lights", lights),
        ("volumes", [_volume("enter_grotto", inside["enter_volume"], inside["enter_extent"])]),
        ("crew", []),
        ("crew_objective", unreal.Name("None")),
        ("crew_alert_objective", unreal.Name("None")),
        ("pickups", []),
        ("examines", [_examine("open_vault", (kx, ky, kz), kyaw, KEYPAD_PROMPT, meshes["keypad"], True, KEYPAD_CLOSE_UP)]),
        ("interior_doors", []),
        ("return_points", []),
        ("triggers", triggers),
        ("switches", [switch]),
    ]


def _ensure_data_asset(path, name, class_name, build, describe, fields_fn):
    cls = c.find_class(class_name, "/Script/Hawkeye." + class_name)
    full = c.asset_path(path, name)
    if cls is None:
        c.log("FAILED", full, "U{0} not exposed to Python; build the module".format(class_name))
        return None
    factory = c.new_factory("DataAssetFactory")
    c.set_props(factory, [("data_asset_class", cls)], "DataAssetFactory")
    asset, created = c.create_asset(name, path, cls, factory, quiet=True)
    if asset is None:
        return None
    if not created and unreal.EditorAssetLibrary.get_metadata_tag(asset, BUILD_TAG) == build:
        c.log("exists", full, describe)
        return asset
    for prop, value in fields_fn():
        try:
            asset.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(full, prop), exc)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, BUILD_TAG, build)
    c.save(asset)
    c.log("created" if created else "updated", full, describe)
    return asset


def _mesh_key(meshes):
    return sorted((k, c.safe_name(v)) for k, v in meshes.items())


def ensure_sites(plan, meshes):
    street = _ensure_data_asset(SITE_PATH, SITE_NAME, "ChapterSiteDefinition",
                                "site-" + _hash([_map_ready(), plan, DOOR_NAME, ORNAMENT_PROMPT, ORNAMENT_CLOSE_UP, LIGHT_LUMENS, LIGHT_RADIUS,
                                                 _mesh_key(meshes), {k: v for k, v in plan.items() if k != "inside"}]),
                                _grotto.describe(plan), lambda: _street_fields(plan, meshes))
    inside = plan["inside"]
    indoors = _ensure_data_asset(SITE_PATH, INSIDE_NAME, "ChapterSiteDefinition",
                                 "site-" + _hash([_map_ready(), inside, KEYPAD_PROMPT, KEYPAD_CLOSE_UP, SWITCH_NOISE, _mesh_key(meshes)]),
                                 "{0} props, {1} triggers, a switch and the keypad in {2}".format(
                                     len(inside["props"]), len(inside["triggers"]), INTERIOR_MAP),
                                 lambda: _inside_fields(inside, meshes))
    return street, indoors


# --------------------------------------------------------------------------------------------------
# Santa's fight
# --------------------------------------------------------------------------------------------------


def _boss_fields(inside, santa):
    import create_boss as cbo  # noqa: PLC0415
    arena = unreal.BossArena()
    a = inside["arena"]
    for prop, value in (("roof_id", unreal.Name("L_Int_Grotto:" + a["room"])),
                        ("outline", [unreal.Vector2D(float(p[0]), float(p[1])) for p in a["outline"]]),
                        ("roof_z", float(a["floor"])), ("centre", _vec(a["centre"])), ("fight_radius", float(a["radius"])),
                        ("player_start", _vec((0.0, 0.0, 0.0))), ("boss_spot", _vec(a["boss"])), ("boss_yaw", float(a["boss_yaw"])),
                        ("hazard_start", _vec((0.0, 0.0, 0.0))), ("hazard_end", _vec((0.0, 0.0, 0.0))), ("props", []), ("lights", [])):
        arena.set_editor_property(prop, value)
    moves = unreal.BossMoveSet()
    moves.set_editor_property("combo", [cbo._attack(spec) for spec in COMBO])
    for prop, value in sorted(MOVES.items()):
        moves.set_editor_property(prop, value)
    rule = unreal.BossPhase()
    for prop, value in (("phase_name", unreal.Name("Santa")), ("behavior_tag", unreal.Name("Santa")), ("health_threshold_percent", 1.0),
                        ("entry", cbo._enum("BossPhaseEntry", "HealthBelow")), ("boss_invulnerable", False),
                        ("invulnerable_during_transition", False), ("transition_seconds", 0.0)):
        rule.set_editor_property(prop, value)
    phase = unreal.BossPhaseDefinition()
    for prop, value in (("phase", rule), ("objective_id", unreal.Name(PHASE_OBJECTIVE)), ("boss_fights", True), ("crew", []),
                        ("hazard_active", False), ("start_lines", []), ("end_lines", []), ("barks", []), ("arrow_grants", []),
                        ("guard_damage_fraction", GUARD), ("guard_break_effects", []), ("hazard_winds_boss", False),
                        ("transition_beat", cbo._beat(None))):
        phase.set_editor_property(prop, value)
    return [
        ("boss_name", unreal.Text(BOSS_DISPLAY)),
        ("boss_class", santa),
        ("max_health", MAX_HEALTH),
        ("body_scale", BODY_SCALE),
        ("moves", moves),
        ("phases", [phase]),
        ("arena", arena),
        ("map", _soft_world(INTERIOR_MAP)),
        ("meet_objective", unreal.Name(MEET_OBJECTIVE)),
        ("pre_fight_sequence", unreal.Name(PRE_FIGHT_SEQUENCE)),
        ("intro_max_seconds", 20.0),
        ("after_sequence", unreal.Name(AFTER_SEQUENCE)),
        ("after_max_seconds", 30.0),
        ("defeat_beat", cbo._beat(DEFEAT_BEAT)),
        ("finisher_camera", cbo._finisher_camera()),
        ("rim_light_color", unreal.LinearColor(RIM_LIGHT[0][0], RIM_LIGHT[0][1], RIM_LIGHT[0][2], 1.0)),
        ("rim_light_lumens", RIM_LIGHT[1]),
        ("rim_light_radius", RIM_LIGHT[2]),
        ("draft_text", True),
    ]


def ensure_boss(inside, santa):
    c.ensure_directory(BOSS_PATH)
    build = "boss-" + _hash([_map_ready(), inside["arena"], COMBO, sorted(MOVES.items()), GUARD, PRE_FIGHT_SEQUENCE, AFTER_SEQUENCE, MEET_OBJECTIVE,
                             PHASE_OBJECTIVE, DEFEAT_BEAT, RIM_LIGHT, MAX_HEALTH, BODY_SCALE, c.safe_name(santa), INTERIOR_MAP])
    return _ensure_data_asset(BOSS_PATH, BOSS_NAME, "BossDefinition", build,
                              "Santa in {0}'s {1}, {2:.0f} health".format(INTERIOR_MAP, inside["arena"]["room"], MAX_HEALTH),
                              lambda: _boss_fields(inside, santa))


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
    ensure_elf(meshes)
    santa = ensure_santa(meshes)
    plan = _grotto.plan_grotto(gen.District())
    if plan is None:
        c.log("FAILED", c.asset_path(SITE_PATH, SITE_NAME), _grotto.describe(None))
        return None
    sites = ensure_sites(plan, meshes)
    ensure_boss(plan["inside"], santa)
    return sites


if __name__ == "__main__":
    run()
    c.print_summary("grotto")
