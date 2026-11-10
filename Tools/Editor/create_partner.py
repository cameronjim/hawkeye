"""Create Clint, the partner's brain and the banter table (claude-docs/gameplay-semantics.md,
"partner and switching").

    /Game/Characters/Clint/M_ClintJacket, M_ClintTrim  matte near-black jacket with one small purple
                                                     chevron on the chest (no arm panels: Kate has
                                                     those, and the two read apart by height and colour)
    /Game/Blueprints/AI/Partner/ST_Partner           the partner StateTree, built headless by
                                                     UHawkeyePartnerTreeBuilder (C++, StateTree editor API)
    /Game/Blueprints/AI/BP_PartnerController         parent AHawkeyePartnerController, runs ST_Partner
    /Game/Blueprints/Player/BP_Clint                 same parent as BP_Kate (SandboxCharacter_CMC, so
                                                     motion-matched locomotion), 185 cm capsule, mannequin
                                                     at scale 1.0, DA_Bow_Clint with 30 standard and
                                                     4 grapple arrows of his own, possessed by
                                                     BP_PartnerController when placed or spawned
    /Game/Data/DT_Dialogue                           FHawkeyeDialogueLine rows: six lines each for Kate and
                                                     Clint in idle_roam, after_fight, objective_near and
                                                     low_health, plus the Scripted lines the dialogue
                                                     sequences name (Chapters 1, 2 and 4's, create_narrative.CH01_LINES, CH02_LINES, CH04_LINES)

Then: CharacterName on BP_Kate and BP_Clint, and on BP_HawkeyePlayerController the switch and mark
actions and the banter component's DialogueTable.

Runs after create_weapon_data (DA_Bow_Clint) and create_input_assets (IA_SwitchCharacter,
IA_PartnerMark). Idempotent: every value is compared before it is written, the StateTree and the
table carry a build tag and are rebuilt only when it changes.
"""

import hashlib
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402
import create_blueprints as cb  # noqa: E402

PLAYER_PATH = "/Game/Blueprints/Player"
AI_PATH = "/Game/Blueprints/AI"
STATE_TREE_PATH = "/Game/Blueprints/AI/Partner"   # not AI/StateTree: that folder is the sample's, git-ignored
WEAPON_PATH = "/Game/Blueprints/Weapons"
DATA_PATH = "/Game/Data"
INPUT_PATH = "/Game/Input"
CLINT_MATERIAL_PATH = "/Game/Characters/Clint"

CLINT_NAME = "BP_Clint"
CONTROLLER_NAME = "BP_PartnerController"
TREE_NAME = "ST_Partner"
TABLE_NAME = "DT_Dialogue"
BUILD_TAG = "HawkeyeBuild"
TREE_BUILD = "partner-tree-1"      # bump when UHawkeyePartnerTreeBuilder's tree changes

# 185 cm: the UEFN mannequin at scale 1.0 is about that tall, feet on the capsule's bottom.
CLINT_CAPSULE_RADIUS = 34.0
CLINT_CAPSULE_HALF_HEIGHT = 92.5
CLINT_MESH_SCALE = 1.0
CLINT_BOW = "DA_Bow_Clint"
CLINT_ARROWS = [("DA_Arrow_Standard", 30), ("DA_Arrow_Grapple", 4)]

# The jacket: matte, near-black, cooler than Kate's suit, and one small chevron high on the chest.
M_CLINT_JACKET = CLINT_MATERIAL_PATH + "/M_ClintJacket"
M_CLINT_TRIM = CLINT_MATERIAL_PATH + "/M_ClintTrim"
CLINT_JACKET_BLACK = (0.011, 0.012, 0.015)
CLINT_JACKET_ROUGHNESS = 0.9
CLINT_CHEVRON_COLOR = (0.16, 0.03, 0.30)
CLINT_CHEVRON_ROUGHNESS = 0.5
CLINT_CHEVRON_TIP_Z = 124.0          # bind-pose cm above the feet: higher and smaller than Kate's
CLINT_CHEVRON_SLOPE = 0.8
CLINT_CHEVRON_HALF_WIDTH = 1.8
CLINT_CHEVRON_MAX_X = 8.0
CLINT_CHEST_FRONT_Y = 3.0
CLINT_TRIM_COLOR = (0.03, 0.03, 0.035)


# --------------------------------------------------------------------------------------
# DT_Dialogue: (row name, speaker, situation, text). Kate talks more; Clint says less and means it.
# --------------------------------------------------------------------------------------

SITUATIONS = {
    "idle_roam": "IdleRoam",
    "after_fight": "AfterFight",
    "objective_near": "ObjectiveNear",
    "low_health": "LowHealth",
}

LINES = {
    ("Kate", "idle_roam"): [
        "You know what this city needs? More rooftops. Fewer stairs.",
        "Is it weird I kind of miss the Tracksuits? No. Forget I said that.",
        "Lucky would love it up here. Lucky would also fall off.",
        "I've been practising the chimney trick shot. Don't ask how it's going.",
        "We should get pizza after. We should always get pizza after.",
        "Christmas lights in January. Somebody gave up, and honestly? Respect.",
    ],
    ("Clint", "idle_roam"): [
        "My knees hate rooftops.",
        "You don't have to fill every silence.",
        "I was supposed to be in Iowa by now.",
        "Ice on the parapets. Watch your feet.",
        "Stay off the skylights. Trust me.",
        "Less talking. More looking.",
    ],
    ("Kate", "after_fight"): [
        "Okay. Okay! That was good. Tell me that was good.",
        "Did you see the ricochet? Please tell me you saw the ricochet.",
        "Nobody's dead, everybody's tied up. Textbook.",
        "I think I pulled something. Worth it.",
        "Those guys really need a new hobby.",
        "Ten out of ten. Would get punched again.",
    ],
    ("Clint", "after_fight"): [
        "Not bad. Your left side's open.",
        "Pick up your arrows.",
        "That was the easy part.",
        "Breathe. Then move.",
        "You're dropping your elbow.",
        "Good. Don't let it go to your head.",
    ],
    ("Kate", "objective_near"): [
        "That's it, right there. I think. Pretty sure.",
        "Almost there. Try to look cool about it.",
        "Right up ahead. Want to race? You don't want to race.",
        "This is the spot. It has big spot energy.",
        "Close. Eyes open, Hawkeye.",
        "Just past this roof. Probably.",
    ],
    ("Clint", "objective_near"): [
        "Up ahead. Slow down.",
        "That's it. Check the corners.",
        "Almost there. Quiet now.",
        "Eyes up. We're close.",
        "There. Don't rush it.",
        "Hold a second. Look first.",
    ],
    ("Kate", "low_health"): [
        "I'm fine! I'm mostly fine.",
        "Ow. Ow. That one counted.",
        "Note to self: dodge.",
        "Little help? No rush. Some rush.",
        "I can walk it off. I think.",
        "That's going to bruise in a really interesting shape.",
    ],
    ("Clint", "low_health"): [
        "You're hurt. Back off.",
        "Get behind something.",
        "Find cover, kid.",
        "Breathe. I've got you.",
        "Don't be a hero.",
        "Slow down before you fall down.",
    ],
}


def scripted_lines():
    """Scripted lines (Situation Scripted: banter never picks them), played by name or from DT_DialogueSequences:
    Chapters 1, 2 and 4's, kept with their sequences in create_narrative.py, every one a draft.
    [(row, speaker, text, seconds)]."""
    import create_narrative  # noqa: PLC0415 - the chapter's words live with its sequences
    return create_narrative.CH01_LINES + create_narrative.CH02_LINES + create_narrative.CH04_LINES


def dialogue_rows():
    rows = []
    for (speaker, situation), texts in LINES.items():
        for i, text in enumerate(texts):
            rows.append({
                "Name": "{0}_{1}_{2:02d}".format(speaker.lower(), situation, i + 1),
                "Speaker": speaker,
                "Situation": SITUATIONS[situation],
                "Text": text,
            })
    for name, speaker, text, seconds in scripted_lines():
        rows.append({"Name": name, "Speaker": speaker, "Situation": "Scripted", "Text": text,
                     "DurationSeconds": seconds, "bDraft": True})
    return rows


def ensure_dialogue_table():
    """DT_Dialogue, refilled only when the lines above change (a hash in its build tag)."""
    full = c.asset_path(DATA_PATH, TABLE_NAME)
    row_struct = getattr(unreal, "HawkeyeDialogueLine", None)
    if row_struct is None:
        c.log("FAILED", full, "FHawkeyeDialogueLine not exposed; build the module")
        return None
    factory = c.new_factory("DataTableFactory")
    if factory is not None:
        c.set_props(factory, [("struct", row_struct.static_struct())], "DataTableFactory")
    table, created = c.create_asset(TABLE_NAME, DATA_PATH, unreal.DataTable, factory, quiet=True)
    if table is None:
        return None
    text = json.dumps(dialogue_rows(), indent=1, ensure_ascii=False)
    build = "lines-" + hashlib.md5(text.encode("utf-8")).hexdigest()[:12]
    if not created and unreal.EditorAssetLibrary.get_metadata_tag(table, BUILD_TAG) == build:
        c.log("exists", full, "{0} lines".format(len(dialogue_rows())))
        return table
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, text):
        c.log("FAILED", full, "fill_data_table_from_json_string refused the rows")
        return table
    unreal.EditorAssetLibrary.set_metadata_tag(table, BUILD_TAG, build)
    c.save(table)
    c.log("created" if created else "updated", full, "{0} lines".format(len(dialogue_rows())))
    return table


# --------------------------------------------------------------------------------------
# ST_Partner and BP_PartnerController
# --------------------------------------------------------------------------------------

def ensure_state_tree():
    full = c.asset_path(STATE_TREE_PATH, TREE_NAME)
    builder = getattr(unreal, "HawkeyePartnerTreeBuilder", None)
    if builder is None:
        c.log("FAILED", full, "UHawkeyePartnerTreeBuilder not exposed; build the module")
        return None
    existing = c.load_or_none(full)
    if existing is not None and unreal.EditorAssetLibrary.get_metadata_tag(existing, BUILD_TAG) == TREE_BUILD:
        c.log("exists", full, "{0} states".format(builder.count_partner_states(existing)))
        return existing
    c.ensure_directory(STATE_TREE_PATH)
    tree = builder.build_partner_state_tree(full)
    if tree is None:
        c.log("FAILED", full, "the StateTree did not compile; see LogHawkeye")
        return existing
    unreal.EditorAssetLibrary.set_metadata_tag(tree, BUILD_TAG, TREE_BUILD)
    c.save(tree)
    c.log("updated" if existing is not None else "created", full,
          "{0} states, schema StateTree AI Component".format(builder.count_partner_states(tree)))
    return tree


def ensure_partner_controller(tree):
    parent = c.find_class("HawkeyePartnerController", "/Script/Hawkeye.HawkeyePartnerController")
    bp, created = cb.make_blueprint(CONTROLLER_NAME, AI_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.apply_defaults(bp, CONTROLLER_NAME, AI_PATH, [("partner_state_tree", tree)])
    return bp


# --------------------------------------------------------------------------------------
# BP_Clint
# --------------------------------------------------------------------------------------

def _build_clint_jacket(material):
    """Near-black, matte, with one small chevron on the sternum masked by the bind-pose position
    (the same trick as Kate's suit, which is where the mannequin's single material slot leaves us)."""
    import _materials as m  # noqa: PLC0415

    bind = m.expr(material, "MaterialExpressionPreSkinnedPosition", -2000, 0, None, "PreSkinnedPosition")
    carried = m.expr(material, "MaterialExpressionVertexInterpolator", -1850, 0, None, "VertexInterpolator")
    m.connect(bind, "", carried, "")
    x = m.absolute(material, m.component_mask(material, carried, r=True, x=-1700, y=-100), -1550, -100)
    y = m.component_mask(material, carried, g=True, x=-1700, y=0)
    z = m.component_mask(material, carried, b=True, x=-1700, y=100)

    line = m.add(material, m.multiply(material, x, None, -1400, 100, const_b=CLINT_CHEVRON_SLOPE), None, -1250, 100,
                 const_b=CLINT_CHEVRON_TIP_Z)
    off = m.absolute(material, m.subtract(material, z, line, -1100, 100), -950, 100)
    chevron = m.mul_all(material, [m.below(material, off, CLINT_CHEVRON_HALF_WIDTH, -800, 100, 3.0),
                                   m.below(material, x, CLINT_CHEVRON_MAX_X, -800, 200, 3.0),
                                   m.step(material, y, CLINT_CHEST_FRONT_Y, -800, 300, 3.0)], -500, 100)
    color = m.lerp(material, m.constant3(material, CLINT_JACKET_BLACK, -300, -250),
                   m.constant3(material, CLINT_CHEVRON_COLOR, -300, -150), chevron, -100, -200)
    m.connect_property(color, unreal.MaterialProperty.MP_BASE_COLOR)
    m.connect_property(m.lerp(material, None, None, chevron, -100, 100, const_a=CLINT_JACKET_ROUGHNESS,
                              const_b=CLINT_CHEVRON_ROUGHNESS), unreal.MaterialProperty.MP_ROUGHNESS)


def ensure_clint_materials():
    import _materials as m  # noqa: PLC0415

    c.ensure_directory(CLINT_MATERIAL_PATH)
    return (
        m.ensure_look_material(M_CLINT_JACKET, _build_clint_jacket, skeletal=True),
        m.ensure_material(M_CLINT_TRIM, m._build_flat(CLINT_TRIM_COLOR, CLINT_JACKET_ROUGHNESS), skeletal=True),
    )


def _object_path(value):
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


def configure_clint_quiver(bp):
    """His own bow and arrows on the inventory component; the chapter's grant stays Kate's."""
    inventory = cb.cdo_component(bp, "inventory_component")
    bow = c.load_or_none(c.asset_path(WEAPON_PATH, CLINT_BOW))
    arrows = [(c.load_or_none(c.asset_path(WEAPON_PATH, name)), count) for name, count in CLINT_ARROWS]
    if inventory is None or bow is None or any(a is None for a, _n in arrows):
        c.log("skipped", CLINT_NAME + " quiver", "no inventory component, or run create_weapon_data first")
        return []
    changed = []
    if not inventory.get_editor_property("use_own_starting_quiver"):
        c.set_props(inventory, [("use_own_starting_quiver", True)], CLINT_NAME + ".InventoryComponent")
        changed.append("own quiver")
    if _object_path(inventory.get_editor_property("own_starting_bow")) != _object_path(bow):
        c.set_props(inventory, [("own_starting_bow", bow)], CLINT_NAME + ".InventoryComponent")
        changed.append(CLINT_BOW)
    current = [(_object_path(g.get_editor_property("arrow")), int(g.get_editor_property("count")))
               for g in (inventory.get_editor_property("own_starting_arrows") or [])]
    if current != [(_object_path(a), n) for a, n in arrows]:
        grants = []
        for arrow, count in arrows:
            grant = unreal.HawkeyeArrowGrant()
            grant.set_editor_property("arrow", arrow)
            grant.set_editor_property("count", count)
            grants.append(grant)
        c.set_props(inventory, [("own_starting_arrows", grants)], CLINT_NAME + ".InventoryComponent")
        changed.append("arrows " + ", ".join("{0} {1}".format(n, a.get_name()) for a, n in arrows))
    return changed


def configure_clint(bp, gasp, controller_class):
    context = c.asset_path(PLAYER_PATH, CLINT_NAME)
    jacket, trim = ensure_clint_materials()
    changed = []
    capsule = cb.cdo_component(bp, "capsule_component")
    if capsule is not None:
        changed += cb.set_if_different(capsule, [
            ("capsule_radius", CLINT_CAPSULE_RADIUS),
            ("capsule_half_height", CLINT_CAPSULE_HALF_HEIGHT),
        ], CLINT_NAME + ".CapsuleComponent")
    body = cb.cdo_component(bp, "mesh")
    if body is not None:
        changed += cb.set_if_different(body, [
            ("relative_location", unreal.Vector(0.0, 0.0, -CLINT_CAPSULE_HALF_HEIGHT)),
            ("relative_scale3d", unreal.Vector(CLINT_MESH_SCALE, CLINT_MESH_SCALE, CLINT_MESH_SCALE)),
        ], CLINT_NAME + ".Mesh")
        changed += cb.configure_kate_body(body, gasp)
        try:
            slots = max(2, int(body.get_num_materials()))
        except Exception:  # noqa: BLE001
            slots = 2
        for slot in range(slots):
            if cb.set_material_slot(body, slot, trim if slot == 1 else jacket, CLINT_NAME + ".Mesh"):
                changed.append("material {0}".format(slot))
    changed += configure_clint_quiver(bp)

    cdo = c.blueprint_cdo(bp)
    if cdo is not None:
        if str(cdo.get_editor_property("character_name")) != "Clint":
            c.set_props(cdo, [("character_name", "Clint")], CLINT_NAME)
            changed.append("name Clint")
        if controller_class is not None and cdo.get_editor_property("ai_controller_class") != controller_class:
            c.set_props(cdo, [("ai_controller_class", controller_class)], CLINT_NAME)
            changed.append("controller " + controller_class.get_name())
        wanted = unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED
        if cdo.get_editor_property("auto_possess_ai") != wanted:
            c.set_props(cdo, [("auto_possess_ai", wanted)], CLINT_NAME)
            changed.append("auto possess AI")

    if changed:
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", context, ", ".join(changed))
    else:
        c.log("exists", context, "185 cm, jacket, own quiver, partner controller already set")


def ensure_clint(controller_class):
    gasp = cb.gasp_character()
    if gasp is not None:
        parent = c.load_generated_class(cb.GASP_CHARACTER_PATH, cb.GASP_CHARACTER)
    else:
        parent = c.load_generated_class(PLAYER_PATH, "BP_HawkeyeCharacter")
    bp, created = cb.make_blueprint(CLINT_NAME, PLAYER_PATH, parent, ("BlueprintFactory",))
    if bp is None:
        return None
    if created:
        c.compile_blueprint(bp)
        c.save(bp)
    cb.ensure_parent(bp, c.asset_path(PLAYER_PATH, CLINT_NAME), parent)
    configure_clint(bp, gasp, controller_class)
    return bp


def configure_kate(controller_class):
    """Kate's name for the HUD, and the controller she gets while Clint is the one being played."""
    bp = c.load_or_none(c.asset_path(PLAYER_PATH, cb.KATE_NAME))
    if bp is None:
        c.log("skipped", cb.KATE_NAME, "run create_blueprints first")
        return
    cdo = c.blueprint_cdo(bp)
    values = [("ai_controller_class", controller_class)]
    if cdo is not None and str(cdo.get_editor_property("character_name")) != "Kate":
        values.append(("character_name", "Kate"))
    cb.apply_defaults(bp, cb.KATE_NAME, PLAYER_PATH, values)


def configure_player_controller(table):
    bp = c.load_or_none(c.asset_path(PLAYER_PATH, "BP_HawkeyePlayerController"))
    if bp is None:
        c.log("skipped", "BP_HawkeyePlayerController", "run create_blueprints first")
        return
    cb.apply_defaults(bp, "BP_HawkeyePlayerController", PLAYER_PATH, [
        ("switch_character_action", c.load_or_none(c.asset_path(INPUT_PATH, "IA_SwitchCharacter"))),
        ("partner_mark_action", c.load_or_none(c.asset_path(INPUT_PATH, "IA_PartnerMark"))),
    ])
    banter = cb.cdo_component(bp, "banter")
    if banter is None or table is None:
        c.log("skipped", "BP_HawkeyePlayerController.Banter", "no banter component or no DT_Dialogue")
        return
    if banter.get_editor_property("dialogue_table") != table:
        c.set_props(banter, [("dialogue_table", table)], "BP_HawkeyePlayerController.Banter")
        c.compile_blueprint(bp)
        c.save(bp)
        c.log("updated", "BP_HawkeyePlayerController.Banter", "dialogue table " + TABLE_NAME)
    else:
        c.log("exists", "BP_HawkeyePlayerController.Banter", "dialogue table already set")


def run():
    c.ensure_directory(AI_PATH)
    c.ensure_directory(DATA_PATH)
    tree = ensure_state_tree()
    controller = ensure_partner_controller(tree)
    controller_class = c.load_generated_class(AI_PATH, CONTROLLER_NAME) if controller is not None else None
    clint = ensure_clint(controller_class)
    configure_kate(controller_class)
    table = ensure_dialogue_table()
    configure_player_controller(table)
    return {"tree": tree, "controller": controller, "clint": clint, "table": table}


if __name__ == "__main__":
    run()
    c.print_summary("partner")
