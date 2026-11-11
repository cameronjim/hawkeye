"""Create Act 1's boss: Tomas, his fight and the social club roof it happens on (claude-docs/gameplay-semantics.md,
"Bosses"; docs/chapters/ACT1.md, chapter 4).

    /Game/Missions/Bosses/DA_Boss_CH04_Tomas     UBossDefinition: his body, move set, three phases (his guys, him,
                                                 him and the loose Santa) with their objectives, crews, lines and the
                                                 confetti arrow, and the arena planned by _arena.plan_arena from the
                                                 district's OpenStreetMap records (DA_CH04_SocialClub.Boss)
    /Game/Blueprints/Bosses/BP_Boss_Tomas        child of BP_Thug: fists, a green tracksuit with gold stripes, no
                                                 ski mask (his face is the "mask" colour); UBossFightSubsystem scales
                                                 him 1.12 and gives him the phases at spawn
    /Game/Characters/Tomas/M_TomasTracksuit      the tracksuit graph in his colours; M_TomasTrim the chest patch
    /Game/Kit/Boss/SM_Kit_InflatableSanta        the arena hazard's body: a 4.3 m inflatable Santa (boots, belly,
                                                 belt, arms, beard, head, hat)
    /Game/Kit/Boss/SM_Kit_LightPole              a 3 m pole the string lights hang from
    /Game/Kit/Boss/SM_Kit_LightWire              1 m of wire, scaled along X to a span
    /Game/Kit/Boss/SM_Kit_Bulb_Warm/_Red/_Green  a bulb of the string lights, glowing
    /Game/Kit/Boss/SM_Kit_ClubSpeaker            the speaker ("Music, please")
    /Game/Kit/Boss/SM_Kit_FoldingTable           a folding table
    /Game/Kit/Boss/SM_Kit_Present                a wrapped present in the Santa's lane: it shoves them along
    /Game/Kit/Boss/SM_Kit_CandyCane              the crew's bat: a red and white candy cane, hook at the far end
    /Game/Blueprints/Bosses/BP_Thug_CandyCane    child of BP_Thug: the same thug, the cane in his bat slot
    /Game/Kit/Boss/MI_SantaLane                  the red glow of the Santa's lane while it telegraphs

UBossFightSubsystem spawns the props, the lights, the Santa, Tomas and his crew where the asset says when the chapter
starts; nothing is placed in the map. The lines are DT_Dialogue rows create_narrative.py writes (all drafts).

Idempotent: the meshes carry a recipe hash and the boss asset a plan hash; each is rewritten only when it changes.

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\create_boss.py" -unattended -nullrhi -nosplash -nop4 -stdout
"""

import hashlib
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _arena  # noqa: E402
import _common as c  # noqa: E402
import _materials as m  # noqa: E402

BOSS_PATH = "/Game/Missions/Bosses"
BOSS_NAME = "DA_Boss_CH04_Tomas"
BP_PATH = "/Game/Blueprints/Bosses"
TOMAS_BP = "BP_Boss_Tomas"
AI_PATH = "/Game/Blueprints/AI"
WEAPON_PATH = "/Game/Blueprints/Weapons"
TOMAS_MATERIAL_PATH = "/Game/Characters/Tomas"
M_TOMAS_SUIT = TOMAS_MATERIAL_PATH + "/M_TomasTracksuit"
M_TOMAS_TRIM = TOMAS_MATERIAL_PATH + "/M_TomasTrim"
TOMAS_MATERIAL_VERSION = "tomas-1"
TOMAS_SUIT = (0.025, 0.16, 0.06)        # bottle green
TOMAS_STRIPE = (0.75, 0.55, 0.12)       # gold
TOMAS_FACE = (0.42, 0.26, 0.18)         # no ski mask: the mask colour is his face
KIT_PATH = "/Game/Kit/Boss"
BUILD_TAG = "HawkeyeBuild"
MESH_VERSION = 1

# --- Tomas (numbers on Normal; gameplay-semantics.md, "Chapter 4's boss") ---------------------------------------
BOSS_DISPLAY = "Tomas"
MAX_HEALTH = 820.0     # 680 until 2026-11-11: with blows landing on contact the scripted fight fell to 80 to 105 s (floor 90)
BODY_SCALE = 1.12
# (name, damage, wind-up, recovery, range, knockback, lunge, unblockable). The jabs can be parried (a brief stagger,
# no finisher: a boss's finisher waits for his winded window); the haymaker cannot, it is dodged.
COMBO = [("tomas_jab", 12.0, 0.75, 0.5, 150.0, 0.0, 30.0, False),
         ("tomas_jab_2", 12.0, 0.6, 0.5, 150.0, 0.0, 30.0, False),
         ("tomas_haymaker", 22.0, 1.1, 0.8, 170.0, 200.0, 60.0, True)]
MOVES = {"winded_after_swings": 3, "winded_seconds": 3.0, "winded_damage_scale": 1.5, "finisher_damage_fraction": 0.05,
         "stagger_threshold": 40.0, "swing_cooldown_seconds": 0.6, "rush_speed": 380.0, "finisher_only_when_winded": True,
         "parry_stagger_seconds": 0.6}
HAZARD = {"first_delay_seconds": 2.0, "telegraph_seconds": 1.5, "sweep_seconds": 2.5, "rest_seconds": 3.5, "damage": 20.0,
          "hit_cooldown_seconds": 2.0, "half_width": _arena.SANTA_HALF_WIDTH, "half_length": 160.0, "knockback_speed": 700.0,
          "pin_seconds": 4.0, "pin_radius": 250.0}
# Each phase's solve (gameplay-semantics.md, "Bosses"): his guard (hits land at this fraction outside his winded window),
# what breaks it (trick arrow effects), whether the Santa's sweep catches him too, and the beat into the phase:
# (seconds, camera offset in the Kate-to-him frame (along, right, up) cm, look at him 0..1, field of view, lines).
SOLVES = {
    "Tomas": {"guard": 0.2, "breaks": ["Smoke"], "hazard_winds": False,
              "beat": (2.5, (-150.0, 380.0, 170.0), 0.65, 70.0, ["ch04_p2_start"])},
    "Santa": {"guard": 0.2, "breaks": ["Smoke"], "hazard_winds": True,
              "beat": (3.0, (-120.0, -420.0, 260.0), 0.55, 65.0, ["ch04_p3_loose_01", "ch04_p3_loose_02"])},
}
# The blow that puts him down: close, from above, on him on the floor.
DEFEAT_BEAT = (2.5, (60.0, 260.0, 190.0), 0.9, 60.0, ["ch04_p3_down"])
FINISHER_CAMERA = {"time_dilation": 0.25, "slow_seconds": 0.8, "push_distance": 110.0}
# He reads near black at night in bottle green: a cool light rides in front of him, over his shoulder.
RIM_LIGHT = ((0.75, 0.85, 1.0), 260.0, 380.0)
CANE_BP = "BP_Thug_CandyCane"
# The held weapon draws a 100 cm mesh at its component's Z scale; the cane is built in true centimetres and drawn at a
# uniform 0.85, so it is as long as the stock bat (0.85 of the engine cylinder) and its hook keeps its shape.
CANE_SCALE = 0.85
# (weapon, class folder, class). The bats carry candy canes; the heavy keeps his shield (a C++ slab, no mesh slot).
CREW = [("Bat", BP_PATH, CANE_BP), ("Bat", BP_PATH, CANE_BP), ("Fists", AI_PATH, "BP_Thug"), ("Shield", AI_PATH, "BP_Thug")]
CONFETTI = ("DA_Arrow_Confetti", 3)

# The phases: (name, threshold, entry, boss invulnerable, transition s, objective, fights, crew, hazard, arrows,
# start lines, end lines, barks [(event, lines)]). Line rows are create_narrative.CH04_LINES'.
PHASES = [
    ("Crew", 1.0, "HealthBelow", True, 0.0, "beat_crew", False, True, False, False,
     ["ch04_p1_start"], ["ch04_p1_end"],
     [("CrewSwings", ["ch04_p1_candy"]), ("PlayerParries", ["ch04_p1_parry"]), ("CrewMemberDown", ["ch04_p1_down"])]),
    ("Tomas", 1.0, "CrewDown", False, 1.5, "beat_tomas", True, False, False, False,
     [], ["ch04_p2_end"],
     [("BossHitsPlayer", ["ch04_p2_hit"]), ("PlayerDodges", ["ch04_p2_dodge"]), ("Winded", ["ch04_p2_winded"]),
      ("FinisherOnBoss", ["ch04_p2_finisher"])]),
    ("Santa", 0.4, "HealthBelow", False, 2.0, "survive_santa", True, False, True, True,
     ["ch04_p3_confetti"], [],
     [("GrantedArrowUsed", ["ch04_p3_confetti_used"])]),
]
MEET_OBJECTIVE = "meet_tomas"
PRE_FIGHT_SEQUENCE = "seq_ch04_prefight"
AFTER_SEQUENCE = "seq_ch04_after"

# Colours: (rgb, roughness). Constant colours for the Santa (an inflatable: no snow on it), prop instances for the rest.
COLOURS = {
    "SantaRed": ((0.55, 0.02, 0.025), 0.35),
    "SantaWhite": ((0.8, 0.8, 0.78), 0.5),
    "SantaBlack": ((0.015, 0.015, 0.015), 0.4),
    "SantaSkin": ((0.6, 0.38, 0.28), 0.6),
    "BossPole": ((0.05, 0.05, 0.055), 0.4),
    "BossSpeaker": ((0.02, 0.02, 0.022), 0.55),
    "BossSpeakerCone": ((0.18, 0.18, 0.2), 0.35),
    "BossTableTop": ((0.62, 0.6, 0.55), 0.6),
    "PresentRed": ((0.5, 0.03, 0.04), 0.4),
    "PresentRibbon": ((0.8, 0.65, 0.2), 0.3),
    "CaneRed": ((0.6, 0.02, 0.03), 0.3),
    "CaneWhite": ((0.85, 0.85, 0.82), 0.3),
}
UNSNOWED = {"SantaRed", "SantaWhite", "SantaBlack", "SantaSkin", "BossPole", "PresentRed", "PresentRibbon", "CaneRed",
            "CaneWhite"}
# Emissive instances of M_Emissive: (rgb, intensity). Tuned against the night exposure (the challenge cap is 0.35).
GLOWS = {
    "MI_SantaLane": ((1.0, 0.0, 0.0), 0.25),
    "MI_BulbWarm": ((1.0, 0.62, 0.25), 1.0),
    "MI_BulbRed": ((1.0, 0.08, 0.06), 1.0),
    "MI_BulbGreen": ((0.15, 1.0, 0.25), 1.0),
}
BULB_COLOURS = [("Warm", (1.0, 0.72, 0.42)), ("Red", (1.0, 0.25, 0.18)), ("Green", (0.35, 1.0, 0.4))]
BULB_LUMENS = 120.0
BULB_RADIUS = 650.0
SPEAKER_GLOW = ((0.55, 0.25, 1.0), 220.0, 500.0)   # a purple glow at the speaker, so the club reads at night

GS_PRIM = unreal.GeometryScript_Primitives
BASE = unreal.GeometryScriptPrimitiveOriginMode.BASE
CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER


def _hash(value):
    return hashlib.md5(json.dumps(value, sort_keys=True, default=str).encode("utf-8")).hexdigest()[:12]


def _opts(slot):
    options = unreal.GeometryScriptPrimitiveOptions()
    options.set_editor_property("material_id", slot)
    return options


def _box(mesh, slot, cx, cy, z0, sx, sy, sz, yaw=0.0):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(0.0, 0.0, yaw))
    GS_PRIM.append_box(mesh, _opts(slot), xf, sx, sy, sz, 0, 0, 0, BASE)


def _cyl(mesh, slot, cx, cy, z0, radius, height, steps=16, pitch=0.0):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0), rotation=unreal.Rotator(0.0, pitch, 0.0))
    GS_PRIM.append_cylinder(mesh, _opts(slot), xf, radius, height, steps, 0, True, BASE)


def _sphere(mesh, slot, cx, cy, cz, radius, scale=(1.0, 1.0, 1.0), steps=14):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, cz), scale=unreal.Vector(*scale))
    GS_PRIM.append_sphere_lat_long(mesh, _opts(slot), xf, radius, steps, steps + 2, CENTER)


def _cone(mesh, slot, cx, cy, z0, base, top, height, steps=16):
    xf = unreal.Transform(location=unreal.Vector(cx, cy, z0))
    GS_PRIM.append_cone(mesh, _opts(slot), xf, base, top, height, steps, 0, True, BASE)


# --------------------------------------------------------------------------------------------------
# meshes (origin at the middle of the base, +X forward)
# --------------------------------------------------------------------------------------------------


def _build_santa():
    """A 4.3 m inflatable Santa: slots red, white, black, skin."""
    mesh = unreal.DynamicMesh()
    for side in (-1.0, 1.0):
        _box(mesh, 2, 10.0, side * 45.0, 0.0, 80.0, 55.0, 45.0)            # boots
        _cyl(mesh, 0, 0.0, side * 45.0, 40.0, 34.0, 60.0)                  # legs
    _sphere(mesh, 0, 0.0, 0.0, 185.0, 115.0, (1.0, 1.05, 1.0))            # the belly
    _cyl(mesh, 2, 0.0, 0.0, 150.0, 112.0, 26.0, 24)                        # the belt
    _box(mesh, 1, 104.0, 0.0, 152.0, 14.0, 40.0, 22.0)                     # the buckle (white for now)
    for side in (-1.0, 1.0):
        _sphere(mesh, 0, 0.0, side * 120.0, 230.0, 40.0, (1.0, 1.0, 1.7))  # arms
        _sphere(mesh, 1, 0.0, side * 125.0, 160.0, 24.0)                   # mittens
    _sphere(mesh, 3, 0.0, 0.0, 330.0, 62.0)                                # head
    _sphere(mesh, 1, 32.0, 0.0, 300.0, 52.0, (0.8, 1.0, 1.1))             # beard
    _cyl(mesh, 1, 0.0, 0.0, 362.0, 64.0, 16.0, 24)                         # hat brim
    _cone(mesh, 0, 0.0, 0.0, 378.0, 58.0, 6.0, 48.0)                       # hat
    _sphere(mesh, 1, 0.0, 0.0, 430.0, 14.0)                                # pompom
    return mesh


def _build_pole():
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 0.0, 40.0, 40.0, 6.0)
    _cyl(mesh, 0, 0.0, 0.0, 6.0, 5.0, _arena.POLE_HEIGHT - 6.0, 10)
    return mesh


def _build_wire():
    """1 m of wire along X centred on the origin; the prop's scale stretches it to its span."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, -1.0, 100.0, 2.0, 2.0)
    return mesh


def _build_bulb():
    mesh = unreal.DynamicMesh()
    _sphere(mesh, 0, 0.0, 0.0, 0.0, 7.0, steps=8)
    return mesh


def _build_speaker():
    """A 60 x 45 x 95 cm speaker facing +X, two cones on its front."""
    mesh = unreal.DynamicMesh()
    width, depth, height = _arena.SPEAKER
    _box(mesh, 0, 0.0, 0.0, 0.0, depth, width, height)
    for z, r in ((28.0, 19.0), (70.0, 10.0)):
        _cyl(mesh, 1, depth * 0.5 - 2.0, 0.0, z, r, 5.0, 16, pitch=90.0)
    return mesh


def _build_table():
    """A folding table 180 x 75 cm, 74 cm high; slots top, legs."""
    mesh = unreal.DynamicMesh()
    length, width, height = _arena.TABLE
    _box(mesh, 0, 0.0, 0.0, height - 4.0, length, width, 4.0)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            _box(mesh, 1, sx * (length * 0.5 - 12.0), sy * (width * 0.5 - 8.0), 0.0, 4.0, 4.0, height - 4.0)
    return mesh


def _build_present():
    """A 50 x 50 x 40 cm wrapped present, a ribbon each way and a bow; slots paper, ribbon."""
    mesh = unreal.DynamicMesh()
    _box(mesh, 0, 0.0, 0.0, 0.0, 50.0, 50.0, 40.0)
    _box(mesh, 1, 0.0, 0.0, 0.0, 52.0, 8.0, 41.0)
    _box(mesh, 1, 0.0, 0.0, 0.0, 8.0, 52.0, 41.0)
    _sphere(mesh, 1, 0.0, 0.0, 45.0, 8.0, (1.4, 0.8, 0.7), steps=8)
    return mesh


def _build_candy_cane():
    """A candy cane 100 cm long along Z, centred on the origin like the engine cylinder, red and white bands, the
    hook bending over toward +X at the far (+Z) end; slots red, white."""
    import math  # noqa: PLC0415
    mesh = unreal.DynamicMesh()
    radius, length, band = 3.5, 100.0, 7.0
    shaft = length - 9.0
    z, slot = -length * 0.5, 0
    while z < -length * 0.5 + shaft - 0.01:
        h = min(band, -length * 0.5 + shaft - z)
        _cyl(mesh, slot, 0.0, 0.0, z, radius, h, 12)
        z += h
        slot = 1 - slot
    # The hook: short segments round an arc over the top and down the far side.
    hook, top = 7.0, -length * 0.5 + shaft
    points = []
    for step in range(9):
        t = math.radians(180.0 - step * 25.0)
        points.append((hook + hook * math.cos(t), top + hook * math.sin(t)))
    for index, ((x0, z0), (x1, z1)) in enumerate(zip(points, points[1:])):
        dx, dz = x1 - x0, z1 - z0
        seg = math.hypot(dx, dz)
        pitch = -math.degrees(math.atan2(dx, dz))
        xf = unreal.Transform(location=unreal.Vector(x0, 0.0, z0), rotation=unreal.Rotator(0.0, pitch, 0.0))
        GS_PRIM.append_cylinder(mesh, _opts(index % 2), xf, radius, seg + 0.6, 12, 0, True, BASE)
    return mesh


def _material(key):
    if key in GLOWS:
        rgb, intensity = GLOWS[key]
        parent = m.ensure_material(m.M_EMISSIVE, m._build_emissive)
        return m.ensure_material_instance(c.asset_path(KIT_PATH, key), parent, vectors=[(m.EMISSIVE_COLOR_PARAM, rgb)],
                                          scalars=[(m.EMISSIVE_INTENSITY_PARAM, intensity)])
    rgb, rough = COLOURS[key]
    if key in UNSNOWED:
        return c.ensure_constant_color_material("M_" + key, KIT_PATH, rgb, rough)
    return m.ensure_prop_instance(key, rgb, rough)


def _write_mesh(name, build, slots, collision=True):
    full = c.asset_path(KIT_PATH, name)
    materials = [_material(key) for key in slots]
    spec = "mesh-" + _hash([MESH_VERSION, name, list(slots), collision, list(_arena.SPEAKER), list(_arena.TABLE),
                            _arena.POLE_HEIGHT])
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
        "santa": _write_mesh("SM_Kit_InflatableSanta", _build_santa, ("SantaRed", "SantaWhite", "SantaBlack", "SantaSkin"),
                             collision=False),
        "pole": _write_mesh("SM_Kit_LightPole", _build_pole, ("BossPole",)),
        "wire": _write_mesh("SM_Kit_LightWire", _build_wire, ("BossPole",), collision=False),
        "speaker": _write_mesh("SM_Kit_ClubSpeaker", _build_speaker, ("BossSpeaker", "BossSpeakerCone")),
        "table": _write_mesh("SM_Kit_FoldingTable", _build_table, ("BossTableTop", "BossPole")),
        "present": _write_mesh("SM_Kit_Present", _build_present, ("PresentRed", "PresentRibbon"), collision=False),
        "cane": _write_mesh("SM_Kit_CandyCane", _build_candy_cane, ("CaneRed", "CaneWhite"), collision=False),
    }
    for colour, _rgb in BULB_COLOURS:
        meshes["bulb_" + colour] = _write_mesh("SM_Kit_Bulb_" + colour, _build_bulb, ("MI_Bulb" + colour,), collision=False)
    meshes["lane"] = _material("MI_SantaLane")
    return meshes


# --------------------------------------------------------------------------------------------------
# Tomas's body
# --------------------------------------------------------------------------------------------------


def ensure_tomas():
    import create_blueprints as cb  # noqa: PLC0415
    import create_enemies as ce  # noqa: PLC0415
    import create_world_blueprints as wb  # noqa: PLC0415

    full = c.asset_path(BP_PATH, TOMAS_BP)
    parent = c.load_generated_class(AI_PATH, "BP_Thug")
    if parent is None:
        c.log("skipped", full, "run create_world_blueprints first")
        return None
    c.ensure_directory(BP_PATH)
    c.ensure_directory(TOMAS_MATERIAL_PATH)
    bp, created = cb.make_blueprint(TOMAS_BP, BP_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.ensure_parent(bp, full, parent)
    suit = ce._versioned_material(M_TOMAS_SUIT, wb.tracksuit_builder(TOMAS_SUIT, TOMAS_STRIPE, TOMAS_FACE),
                                  TOMAS_MATERIAL_VERSION)
    trim = ce._versioned_material(M_TOMAS_TRIM, wb.trim_builder(TOMAS_STRIPE), TOMAS_MATERIAL_VERSION)
    changed = []
    cdo = c.blueprint_cdo(bp)
    if cdo is not None and cdo.get_editor_property("weapon") != unreal.ThugWeapon.FISTS:
        c.set_props(cdo, [("weapon", unreal.ThugWeapon.FISTS)], TOMAS_BP)
        changed.append("weapon Fists")
    body = cb.cdo_component(bp, "mesh")
    for slot, material in ((0, suit), (1, trim)):
        if cb.set_material_slot(body, slot, material, TOMAS_BP + ".Mesh"):
            changed.append("material {0}".format(slot))
    if changed or created:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated" if not created else "created", full, ", ".join(changed))
    else:
        c.log("exists", full, "fists and the green suit already set")
    return c.load_generated_class(BP_PATH, TOMAS_BP)


def ensure_cane_thug(cane):
    """BP_Thug_CandyCane: BP_Thug with the candy cane in his bat slot, drawn at CANE_SCALE."""
    import create_blueprints as cb  # noqa: PLC0415

    full = c.asset_path(BP_PATH, CANE_BP)
    parent = c.load_generated_class(AI_PATH, "BP_Thug")
    if parent is None or cane is None:
        c.log("skipped", full, "run create_world_blueprints first")
        return None
    bp, created = cb.make_blueprint(CANE_BP, BP_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.ensure_parent(bp, full, parent)
    changed = []
    cdo = c.blueprint_cdo(bp)
    if cdo is not None and cdo.get_editor_property("bat_mesh") != cane:
        c.set_props(cdo, [("bat_mesh", cane)], CANE_BP)
        changed.append("bat_mesh")
    held = cb.cdo_component(bp, "held_weapon_component")
    if held is not None and cb.set_if_different(held, [("relative_scale3d", unreal.Vector(CANE_SCALE, CANE_SCALE, CANE_SCALE))],
                                                 CANE_BP + ".HeldWeapon"):
        changed.append("cane scale")
    if changed or created:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated" if not created else "created", full, ", ".join(changed))
    else:
        c.log("exists", full, "the cane already in his bat slot")
    return c.load_generated_class(BP_PATH, CANE_BP)


# --------------------------------------------------------------------------------------------------
# the boss asset
# --------------------------------------------------------------------------------------------------


def _enum(name, value):
    import re  # noqa: PLC0415
    return getattr(getattr(unreal, name), re.sub(r"(?<!^)(?=[A-Z])", "_", value).upper())


def _vec(t):
    return unreal.Vector(float(t[0]), float(t[1]), float(t[2]))


def _names(rows):
    return [unreal.Name(r) for r in rows]


def _attack(spec):
    name, damage, windup, recover, reach, knockback, lunge, unblockable = spec
    attack = unreal.HawkeyeMeleeAttack()
    for prop, value in (("name", unreal.Name(name)), ("damage", damage), ("windup_seconds", windup),
                        ("recover_seconds", recover), ("range", reach), ("radius", 45.0), ("stagger", True),
                        ("knockback_distance", knockback), ("lunge_distance", lunge), ("lunge_seconds", 0.15),
                        ("unblockable", unblockable)):
        attack.set_editor_property(prop, value)
    return attack


def _moves():
    moves = unreal.BossMoveSet()
    moves.set_editor_property("combo", [_attack(s) for s in COMBO])
    for prop, value in sorted(MOVES.items()):
        moves.set_editor_property(prop, value)
    return moves


def _hazard():
    tuning = unreal.BossHazardTuning()
    for prop, value in sorted(HAZARD.items()):
        tuning.set_editor_property(prop, value)
    return tuning


def _beat(spec):
    beat = unreal.BossBeat()
    if spec is None:
        return beat
    seconds, offset, look, fov, lines = spec
    for prop, value in (("seconds", seconds), ("camera_offset", _vec(offset)), ("look_at_boss", look), ("field_of_view", fov),
                        ("blend_seconds", 0.3), ("lines", _names(lines))):
        beat.set_editor_property(prop, value)
    return beat


def _finisher_camera():
    camera = unreal.BossFinisherCamera()
    for prop, value in sorted(FINISHER_CAMERA.items()):
        camera.set_editor_property(prop, value)
    return camera


def _crew(plan):
    out = []
    for (weapon, cls_path, cls_name), at in zip(CREW, plan["crew"]):
        member = unreal.BossCrewMember()
        member.set_editor_property("thug_class", c.load_generated_class(cls_path, cls_name))
        member.set_editor_property("weapon", _enum("ThugWeapon", weapon))
        member.set_editor_property("location", _vec(at))
        out.append(member)
    return out


def _phases(plan, confetti):
    out = []
    for (name, threshold, entry, invulnerable, transition, objective, fights, crew, hazard, arrows, start, end,
         barks) in PHASES:
        rule = unreal.BossPhase()
        rule.set_editor_property("phase_name", unreal.Name(name))
        rule.set_editor_property("behavior_tag", unreal.Name(name))
        rule.set_editor_property("health_threshold_percent", threshold)
        rule.set_editor_property("entry", _enum("BossPhaseEntry", entry))
        rule.set_editor_property("boss_invulnerable", invulnerable)
        rule.set_editor_property("invulnerable_during_transition", transition > 0.0)
        rule.set_editor_property("transition_seconds", transition)
        phase = unreal.BossPhaseDefinition()
        phase.set_editor_property("phase", rule)
        phase.set_editor_property("objective_id", unreal.Name(objective))
        phase.set_editor_property("boss_fights", fights)
        phase.set_editor_property("crew", _crew(plan) if crew else [])
        phase.set_editor_property("hazard_active", hazard)
        phase.set_editor_property("start_lines", _names(start))
        phase.set_editor_property("end_lines", _names(end))
        entries = []
        for event, lines in barks:
            bark = unreal.BossBark()
            bark.set_editor_property("event", _enum("BossBarkEvent", event))
            bark.set_editor_property("lines", _names(lines))
            bark.set_editor_property("once", True)
            entries.append(bark)
        phase.set_editor_property("barks", entries)
        grants = []
        if arrows and confetti is not None:
            grant = unreal.HawkeyeArrowGrant()
            grant.set_editor_property("arrow", confetti)
            grant.set_editor_property("count", CONFETTI[1])
            grants.append(grant)
        phase.set_editor_property("arrow_grants", grants)
        solve = SOLVES.get(name, {})
        phase.set_editor_property("guard_damage_fraction", solve.get("guard", 1.0))
        phase.set_editor_property("guard_break_effects", [_enum("ArrowHitEffect", e) for e in solve.get("breaks", [])])
        phase.set_editor_property("hazard_winds_boss", solve.get("hazard_winds", False))
        phase.set_editor_property("transition_beat", _beat(solve.get("beat")))
        out.append(phase)
    return out


def _prop(at, mesh, blocks, scale=(1.0, 1.0, 1.0)):
    prop = unreal.BossArenaProp()
    prop.set_editor_property("transform", unreal.Transform(location=_vec(at[:3]), rotation=unreal.Rotator(0.0, 0.0, at[3]),
                                                           scale=unreal.Vector(*scale)))
    prop.set_editor_property("mesh", mesh)
    prop.set_editor_property("blocks", blocks)
    return prop


def _light(at, rgb, lumens, radius):
    light = unreal.BossArenaLight()
    light.set_editor_property("location", _vec(at))
    light.set_editor_property("color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    light.set_editor_property("lumens", lumens)
    light.set_editor_property("radius", radius)
    return light


def _arena_struct(plan, meshes):
    arena = unreal.BossArena()
    props = []
    for kind, at in plan["props"]:
        props.append(_prop(at, meshes[kind], True))
    for at in plan.get("presents", []):
        present = _prop(at, meshes["present"], False)
        present.set_editor_property("hazard_pushes", True)
        props.append(present)
    for x, y, z, yaw, length in plan["wires"]:
        props.append(_prop((x, y, z, yaw), meshes["wire"], False, (length / 100.0, 1.0, 1.0)))
    lights = []
    for index, (x, y, z) in enumerate(plan["bulbs"]):
        colour, rgb = BULB_COLOURS[index % len(BULB_COLOURS)]
        props.append(_prop((x, y, z, 0.0), meshes["bulb_" + colour], False))
        # Every other bulb lights the roof; the rest only glow.
        if index % 2 == 0:
            lights.append(_light((x, y, z - 10.0), rgb, BULB_LUMENS, BULB_RADIUS))
    for kind, at in plan["props"]:
        if kind == "speaker":
            lights.append(_light((at[0], at[1], at[2] + 140.0), SPEAKER_GLOW[0], SPEAKER_GLOW[1], SPEAKER_GLOW[2]))
    for prop, value in (
            ("roof_id", unreal.Name(plan["roof"])),
            ("outline", [unreal.Vector2D(float(p[0]), float(p[1])) for p in plan["outline"]]),
            ("roof_z", float(plan["roof_z"])),
            ("centre", _vec(plan["centre"])),
            ("fight_radius", float(plan["radius"])),
            ("player_start", _vec(plan["player_start"])),
            ("player_start_yaw", float(plan["player_yaw"])),
            ("approach_anchor", _vec(plan["anchor"])),
            ("boss_spot", _vec(plan["boss"])),
            ("boss_yaw", float(plan["boss_yaw"])),
            ("hazard_start", _vec(plan["lane"][0])),
            ("hazard_end", _vec(plan["lane"][1])),
            ("hazard_mesh", meshes["santa"]),
            ("hazard_lane_material", meshes["lane"]),
            ("props", props),
            ("lights", lights)):
        arena.set_editor_property(prop, value)
    return arena


def ensure_boss(plan, meshes, tomas):
    cls = c.find_class("BossDefinition", "/Script/Hawkeye.BossDefinition")
    full = c.asset_path(BOSS_PATH, BOSS_NAME)
    if cls is None:
        c.log("FAILED", full, "UBossDefinition not exposed to Python; build the module")
        return None
    confetti = c.load_or_none(c.asset_path(WEAPON_PATH, CONFETTI[0]))
    if confetti is None:
        c.log("skipped", full + " confetti", "run create_weapon_data first")
    factory = c.new_factory("DataAssetFactory")
    c.set_props(factory, [("data_asset_class", cls)], "DataAssetFactory")
    asset, created = c.create_asset(BOSS_NAME, BOSS_PATH, cls, factory, quiet=True)
    if asset is None:
        return None
    build = "boss-" + _hash([plan, COMBO, sorted(MOVES.items()), sorted(HAZARD.items()), CREW, PHASES, CONFETTI,
                             sorted((k, sorted(v.items())) for k, v in SOLVES.items()), DEFEAT_BEAT,
                             sorted(FINISHER_CAMERA.items()), RIM_LIGHT,
                             MEET_OBJECTIVE, PRE_FIGHT_SEQUENCE, AFTER_SEQUENCE, MAX_HEALTH, BODY_SCALE, BULB_COLOURS,
                             BULB_LUMENS, BULB_RADIUS, SPEAKER_GLOW, c.safe_name(tomas), c.safe_name(confetti),
                             sorted((k, c.safe_name(v)) for k, v in meshes.items())])
    if not created and unreal.EditorAssetLibrary.get_metadata_tag(asset, BUILD_TAG) == build:
        c.log("exists", full, _arena.describe(plan))
        return asset
    fields = [
        ("boss_name", unreal.Text(BOSS_DISPLAY)),
        ("boss_class", tomas),
        ("max_health", MAX_HEALTH),
        ("body_scale", BODY_SCALE),
        ("moves", _moves()),
        ("phases", _phases(plan, confetti)),
        ("arena", _arena_struct(plan, meshes)),
        ("hazard", _hazard()),
        ("meet_objective", unreal.Name(MEET_OBJECTIVE)),
        ("pre_fight_sequence", unreal.Name(PRE_FIGHT_SEQUENCE)),
        ("intro_max_seconds", 45.0),
        ("after_sequence", unreal.Name(AFTER_SEQUENCE)),
        ("after_max_seconds", 60.0),
        ("defeat_beat", _beat(DEFEAT_BEAT)),
        ("finisher_camera", _finisher_camera()),
        ("rim_light_color", unreal.LinearColor(RIM_LIGHT[0][0], RIM_LIGHT[0][1], RIM_LIGHT[0][2], 1.0)),
        ("rim_light_lumens", RIM_LIGHT[1]),
        ("rim_light_radius", RIM_LIGHT[2]),
        ("draft_text", True),
    ]
    for prop, value in fields:
        try:
            asset.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(full, prop), exc)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, BUILD_TAG, build)
    c.save(asset)
    c.log("created" if created else "updated", full, _arena.describe(plan))
    return asset


def run():
    import generate_city as gen  # noqa: PLC0415
    if not gen.data_available():
        c.log("skipped", BOSS_PATH, "no OSM data; run Tools\\fetch-osm.ps1 first")
        return None
    c.ensure_directory(KIT_PATH)
    c.ensure_directory(BOSS_PATH)
    meshes = ensure_meshes()
    tomas = ensure_tomas()
    ensure_cane_thug(meshes.get("cane"))
    plan = _arena.plan_arena(gen.District())
    if plan is None:
        c.log("FAILED", c.asset_path(BOSS_PATH, BOSS_NAME), _arena.describe(None))
        return None
    return ensure_boss(plan, meshes, tomas)


if __name__ == "__main__":
    run()
    c.print_summary("boss")
