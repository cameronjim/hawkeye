"""Create the thug brain's assets and Barney's archer (docs/plans/03-core-systems.md, "Combat, finished").

    /Game/Blueprints/AI/Thug/ST_Thug          the thugs' StateTree, built headless by
                                              UHawkeyeThugTreeBuilder (C++, StateTree editor API):
                                              six top-level states by priority, the fight by weapon
                                              under Attack (claude-docs/architecture.md, "Thugs")
    /Game/Blueprints/AI/EQS_CoverPoints       the gunner's cover search: an EnvQuery built through its
                                              options in C++ (donut of candidates, a blocked trace from
                                              the target, nearest first); no EQS editor graph
    /Game/Characters/Archer/M_ArcherSuit      the tracksuit graph in dark grey with a purple stripe
    /Game/Characters/Archer/M_ArcherTrim      the chest patch in purple
    /Game/Blueprints/Bosses/BP_Archer         child of BP_Thug: EThugWeapon::Bow, its BowComponent carrying
                                              DA_Bow_Archer and DA_Arrow_Trickshot, the grey suit
    /Game/Characters/Thug/M_HeavyTracksuit    the red tracksuit graph with a black vest over the torso
    /Game/Blueprints/AI/BP_Thug_Heavy         child of BP_Thug: EThugWeapon::Shield (riot shield, bash, slow
                                              bat), 200 HP, walks at 300, the vest

Then BP_Thug.ThugStateTree = ST_Thug and BP_Thug.CoverQuery = EQS_CoverPoints (BP_Archer inherits
both).

Runs after create_world_blueprints (BP_Thug) and create_weapon_data (DA_Bow_Archer,
DA_Arrow_Trickshot). Idempotent: the tree and the query carry a build tag and are rebuilt only when
it changes; every other value is compared before it is written.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import create_blueprints as cb  # noqa: E402

AI_PATH = "/Game/Blueprints/AI"
TREE_PATH = AI_PATH + "/Thug"
BOSS_PATH = "/Game/Blueprints/Bosses"
WEAPON_PATH = "/Game/Blueprints/Weapons"
ARCHER_MATERIAL_PATH = "/Game/Characters/Archer"

TREE_NAME = "ST_Thug"
QUERY_NAME = "EQS_CoverPoints"
ARCHER_NAME = "BP_Archer"
BUILD_TAG = "HawkeyeBuild"
TREE_BUILD = "thug-tree-2"          # bump when UHawkeyeThugTreeBuilder's tree changes
QUERY_BUILD = "cover-query-1"       # bump when FillCoverQuery changes

M_ARCHER_SUIT = ARCHER_MATERIAL_PATH + "/M_ArcherSuit"
M_ARCHER_TRIM = ARCHER_MATERIAL_PATH + "/M_ArcherTrim"
ARCHER_MATERIAL_VERSION = "archer-1"
ARCHER_SUIT = (0.045, 0.045, 0.05)      # dark grey
ARCHER_STRIPE = (0.32, 0.06, 0.62)      # Barney's purple
ARCHER_MASK = (0.012, 0.012, 0.012)

HEAVY_NAME = "BP_Thug_Heavy"
M_HEAVY_SUIT = "/Game/Characters/Thug/M_HeavyTracksuit"
HEAVY_MATERIAL_VERSION = "heavy-1"
HEAVY_VEST = (0.01, 0.01, 0.012)        # black
HEAVY_HEALTH = 200.0
HEAVY_WALK_SPEED = 300.0


def _versioned_material(full_path, build_fn, version=ARCHER_MATERIAL_VERSION):
    existing = c.load_or_none(full_path)
    current = unreal.EditorAssetLibrary.get_metadata_tag(existing, BUILD_TAG) if existing is not None else None
    import _materials as m  # noqa: PLC0415
    material = m.ensure_material(full_path, build_fn, rebuild=existing is not None and current != version,
                                 skeletal=True)
    if material is not None and current != version:
        unreal.EditorAssetLibrary.set_metadata_tag(material, BUILD_TAG, version)
        c.save(material)
    return material


def ensure_state_tree():
    full = c.asset_path(TREE_PATH, TREE_NAME)
    builder = getattr(unreal, "HawkeyeThugTreeBuilder", None)
    if builder is None:
        c.log("FAILED", full, "UHawkeyeThugTreeBuilder not exposed; build the module")
        return None
    existing = c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, BUILD_TAG) == TREE_BUILD:
        c.log("exists", full, builder.describe_thug_state_tree(existing))
        return existing
    c.ensure_directory(TREE_PATH)
    tree = builder.build_thug_state_tree(full)
    if tree is None:
        c.log("FAILED", full, "the StateTree did not compile; see LogHawkeye")
        return existing
    unreal.EditorAssetLibrary.set_metadata_tag(tree, BUILD_TAG, TREE_BUILD)
    c.save(tree)
    c.log("updated" if existing is not None else "created", full,
          "schema StateTree AI Component: " + builder.describe_thug_state_tree(tree))
    return tree


def ensure_cover_query():
    full = c.asset_path(AI_PATH, QUERY_NAME)
    builder = getattr(unreal, "HawkeyeThugTreeBuilder", None)
    if builder is None:
        c.log("FAILED", full, "UHawkeyeThugTreeBuilder not exposed; build the module")
        return None
    existing = c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, BUILD_TAG) == QUERY_BUILD:
        c.log("exists", full, builder.describe_cover_query(existing))
        return existing
    query = builder.build_cover_query(full)
    if query is None:
        c.log("FAILED", full, "the cover query was not built; see LogHawkeye")
        return existing
    unreal.EditorAssetLibrary.set_metadata_tag(query, BUILD_TAG, QUERY_BUILD)
    c.save(query)
    c.log("updated" if existing is not None else "created", full, builder.describe_cover_query(query))
    return query


def _path(value):
    return value.get_path_name() if value is not None else ""


def wire_thug(tree, query):
    """BP_Thug runs ST_Thug and searches cover with EQS_CoverPoints."""
    bp = c.load_or_none(c.asset_path(AI_PATH, "BP_Thug"))
    cdo = c.blueprint_cdo(bp) if bp is not None else None
    if cdo is None:
        c.log("skipped", "BP_Thug brain", "run create_world_blueprints first")
        return
    changed = []
    for prop, value in (("thug_state_tree", tree), ("cover_query", query)):
        if value is not None and _path(cdo.get_editor_property(prop)) != _path(value):
            c.set_props(cdo, [(prop, value)], "BP_Thug")
            changed.append(prop)
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", c.asset_path(AI_PATH, "BP_Thug"), "set " + ", ".join(changed))
    else:
        c.log("exists", c.asset_path(AI_PATH, "BP_Thug"), "ST_Thug and EQS_CoverPoints already set")


def ensure_archer():
    import create_world_blueprints as wb  # noqa: PLC0415

    parent = c.load_generated_class(AI_PATH, "BP_Thug")
    if parent is None:
        c.log("skipped", c.asset_path(BOSS_PATH, ARCHER_NAME), "run create_world_blueprints first")
        return None
    c.ensure_directory(BOSS_PATH)
    bp, created = cb.make_blueprint(ARCHER_NAME, BOSS_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.ensure_parent(bp, c.asset_path(BOSS_PATH, ARCHER_NAME), parent)

    c.ensure_directory(ARCHER_MATERIAL_PATH)
    suit = _versioned_material(M_ARCHER_SUIT, wb.tracksuit_builder(ARCHER_SUIT, ARCHER_STRIPE, ARCHER_MASK))
    trim = _versioned_material(M_ARCHER_TRIM, wb.trim_builder(ARCHER_STRIPE))
    bow_def = c.load_or_none(c.asset_path(WEAPON_PATH, "DA_Bow_Archer"))
    arrow_def = c.load_or_none(c.asset_path(WEAPON_PATH, "DA_Arrow_Trickshot"))

    changed = []
    cdo = c.blueprint_cdo(bp)
    if cdo is not None:
        wanted = unreal.ThugWeapon.BOW
        if cdo.get_editor_property("weapon") != wanted:
            c.set_props(cdo, [("weapon", wanted)], ARCHER_NAME)
            changed.append("weapon Bow")
    bow = cb.cdo_component(bp, "bow_component")
    if bow is None or bow_def is None or arrow_def is None:
        c.log("skipped", ARCHER_NAME + ".BowComponent", "no bow component, or run create_weapon_data first")
    else:
        for prop, value in (("own_bow", bow_def), ("own_arrow", arrow_def)):
            if _path(bow.get_editor_property(prop)) != _path(value):
                c.set_props(bow, [(prop, value)], ARCHER_NAME + ".BowComponent")
                changed.append(prop)
    body = cb.cdo_component(bp, "mesh")
    for slot, material in ((0, suit), (1, trim)):
        if cb.set_material_slot(body, slot, material, ARCHER_NAME + ".Mesh"):
            changed.append("material {0}".format(slot))
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", c.asset_path(BOSS_PATH, ARCHER_NAME), ", ".join(changed))
    else:
        c.log("exists", c.asset_path(BOSS_PATH, ARCHER_NAME), "bow, arrows and grey suit already set")
    return bp


def ensure_heavy():
    """BP_Thug_Heavy: the shield weapon, 200 HP, 300 cm/s, the red tracksuit with a black vest."""
    import create_world_blueprints as wb  # noqa: PLC0415

    full = c.asset_path(AI_PATH, HEAVY_NAME)
    parent = c.load_generated_class(AI_PATH, "BP_Thug")
    if parent is None or not hasattr(unreal.ThugWeapon, "SHIELD"):
        c.log("skipped", full, "run create_world_blueprints first, and build the module (EThugWeapon::Shield)")
        return None
    bp, created = cb.make_blueprint(HEAVY_NAME, AI_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.ensure_parent(bp, full, parent)
    suit = _versioned_material(M_HEAVY_SUIT, wb.tracksuit_builder(wb.TRACKSUIT_RED, wb.TRACKSUIT_STRIPE, wb.SKI_MASK,
                                                                  HEAVY_VEST), HEAVY_MATERIAL_VERSION)

    changed = []
    cdo = c.blueprint_cdo(bp)
    if cdo is not None and cdo.get_editor_property("weapon") != unreal.ThugWeapon.SHIELD:
        c.set_props(cdo, [("weapon", unreal.ThugWeapon.SHIELD)], HEAVY_NAME)
        changed.append("weapon Shield")
    health = cb.cdo_component(bp, "health_component")
    if health is not None:
        changed += cb.set_if_different(health, [("max_health", HEAVY_HEALTH)],
                                       HEAVY_NAME + ".HealthComponent")
    movement = cb.cdo_component(bp, "character_movement")
    if movement is not None:
        changed += cb.set_if_different(movement, [("max_walk_speed", HEAVY_WALK_SPEED)], HEAVY_NAME + ".CharacterMovement")
    body = cb.cdo_component(bp, "mesh")
    if cb.set_material_slot(body, 0, suit, HEAVY_NAME + ".Mesh"):
        changed.append("material 0")
    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", full, ", ".join(changed))
    else:
        c.log("exists", full, "shield, 200 HP, 300 cm/s and the vest already set")
    return bp


def run():
    c.ensure_directory(AI_PATH)
    tree = ensure_state_tree()
    query = ensure_cover_query()
    wire_thug(tree, query)
    archer = ensure_archer()
    heavy = ensure_heavy()
    return {"tree": tree, "query": query, "archer": archer, "heavy": heavy}


if __name__ == "__main__":
    run()
    c.print_summary("enemies")
