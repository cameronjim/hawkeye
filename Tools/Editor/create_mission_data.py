"""Create the chapter data assets.

    /Game/Missions/DA_CH01_NightShift             UMissionDefinition: Act 1's first chapter, "Night Shift"
                                                  (docs/chapters/ACT1.md, CH01.md)
    /Game/Missions/DA_CH03_Grotto                 UMissionDefinition: Act 1's third chapter, "The Grotto"
                                                  (docs/chapters/CH03.md); its sites and Santa are create_grotto.py's
    /Game/Missions/DA_CH04_SocialClub             UMissionDefinition: Act 1's boss chapter, "The Social Club"
                                                  (docs/chapters/ACT1.md); its Boss is DA_Boss_CH04_Tomas
                                                  (create_boss.py, which runs first)
    /Game/Missions/DA_Scene_Placeholder           UMissionDefinition for L_Scene_Placeholder, the
                                                  playable scene the placeholder flashback ends in (no
                                                  chapter plays it now; the scene system stays for later)

DA_CH01_NightShift is started on L_District_EastVillage by BP_GameMode_EastVillage, which generate_city.py
makes. It opens mid-chase: its Chase is DA_Chase_CH01_NightShift (create_chase.py, which runs first), whose
waypoints complete chase_thief and cross_gap and whose catch completes catch_thief. No partner on screen and no
switching (Act 1: Clint is a voice on the phone). Its title, objective titles and end card line come from
docs/chapters/ACT1-script.md and are flagged as drafts (bDraftText, UMissionObjective::bDraft) for the owner to
edit. No flashback: the purple-arrow ending, its slides and the grey room are gone from the chapter. The old
DA_CH01_Rooftops is deleted (a save that names it starts a new game, the save rule for a stale chapter).

It grants DA_Bow_Kate with 30 standard arrows and the grapple (create_weapon_data.py makes those, and runs first):
Act 1 unlocks its trick arrows chapter by chapter (the crime laps hand themselves their bolas).

DA_CH02_ReindeerGames follows it (DA_CH01_NightShift.NextChapter): the toy drive on Avenue B, its site
DA_Site_CH02_ToyDrive (create_site.py, which runs first) completing reach_drive (the arrival volume), stop_crew (the
reindeer crew down) and return_toys (the three sacks to the table); examine_courier is optional and unmarked and raises
the campaign flag saw_courier. Its events (CH02_EVENTS) speak its lines and grant the sticky foam (3 putty arrows) on
arrival. Its flashback is the "Visiting hours" slides (DA_FB02_VisitingHours, create_narrative.py, made here first).

DA_CH03_Grotto follows that (DA_CH02_ReindeerGames.NextChapter): the mall Santa's grotto, its street site
DA_Site_CH03_Grotto and its interior site DA_Site_CH03_GrottoInside (L_Int_Grotto, so the chapter carries on inside it)
and Santa (DA_Boss_CH03_Santa), all create_grotto.py's. find_sleigh at the frontage, enter_grotto (a stealth objective:
its spotted title when a thug sees her) at the workshop door inside, open_vault at its keypad, beat_santa by the fight,
leave_roof on the building's roof the roof stairs come out on; examine_ornament is optional and unmarked, offered after
the roof, and raises found_ornament. Order is enforced. Its events (CH03_EVENTS) speak its lines, grant the boomerang (2)
at the stockroom shelf; the video call from the farm is a phone call after the roof (create_narrative.py).

DA_CH04_SocialClub follows that (DA_CH03_Grotto.NextChapter): chapter select opens it once The Grotto is complete. Its four objectives (meet_tomas, beat_crew, beat_tomas, survive_santa) are completed
by the boss fight; its title, objective titles and end card line are drafts. No partner, no chase, back to roaming at
the end. It grants the bow with 30 standard, the grapple, 2 foam and 2 boomerangs (chapters 2 and 3's unlocks, so chapter
select can open it cold); the fight's last phase adds 3 confetti arrows (slot 5).

Property names come from Source/Hawkeye/Mission/MissionDefinition.h and
Source/Hawkeye/Mission/MissionObjective.h.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402

MISSION_PATH = "/Game/Missions"
WEAPON_PATH = "/Game/Blueprints/Weapons"

# Spellings the objective-id field has had in C++; the first one that exists is used.
OBJECTIVE_ID_PROPS = ["objective_id", "objective_tag"]


def data_asset_factory(data_asset_class):
    factory = c.new_factory("DataAssetFactory")
    if factory is None:
        return None
    c.set_props(factory, [("data_asset_class", data_asset_class)], "DataAssetFactory")
    return factory


def objective_id(objective):
    for prop in OBJECTIVE_ID_PROPS:
        try:
            return str(objective.get_editor_property(prop))
        except Exception:  # noqa: BLE001 - try the next spelling
            continue
    return ""


# --------------------------------------------------------------------------------------
# chapter 1: Night Shift
# --------------------------------------------------------------------------------------

CH01_NAME = "DA_CH01_NightShift"
RETIRED = ["DA_CH01_Rooftops"]
CHASE = "/Game/Missions/Chases/DA_Chase_CH01_NightShift"

# Scalar fields, checked on every run so an existing asset is corrected rather than skipped. The title,
# subtitle and end card line are drafts (ACT1-script.md); the end card carries Kate's last line.
CH01_FIELDS = [
    ("mission_name", "Night Shift"),
    ("mission_number", 1),
    ("end_card_line", "\"Hawkeye's sidekick.\" I caught him. Me. Put that in the paper."),
    ("show_objective_text", True),
    # Act 1: Clint is never on screen; the partner is benched and the switch key does nothing.
    ("allow_switching", False),
    ("partner_present", False),
    ("opening_title", "Night Shift"),
    ("opening_subtitle", "Early December. The East Village."),
    ("opening_dialogue_sequence", "seq_ch01_open"),
    # No chapter 2 yet: after the end card, back to the district.
    ("return_to_roaming_at_end", True),
    ("chapter_complete_toast", "[Chapter complete]"),
    ("draft_text", True),
]

# The playable scene's own small mission: walk to the marker in the placeholder room.
SCENE_NAME = "DA_Scene_Placeholder"
SCENE_FIELDS = [
    ("mission_name", "[Scene placeholder]"),
    ("mission_number", 0),
    ("show_objective_text", True),
]
SCENE_OBJECTIVES = [
    ("reach_marker", "[Scene objective]", ""),
]

# (ObjectiveId, Title, Description), from ACT1-script.md, every one a draft. The chase completes them:
# chase_thief at her first grapple's stand, cross_gap when she lands across, catch_thief at the catch.
CH01_OBJECTIVES = [
    ("chase_thief", "Don't lose the bike thief", ""),
    ("cross_gap", "Get across. Use the grapple", ""),
    ("catch_thief", "Catch him", ""),
]
DRAFT_OBJECTIVES = {"chase_thief", "cross_gap", "catch_thief", "meet_tomas", "beat_crew", "beat_tomas", "survive_santa",
                    "reach_drive", "stop_crew", "return_toys", "examine_courier", "find_sleigh", "enter_grotto", "open_vault",
                    "beat_santa", "leave_roof", "examine_ornament"}
# Optional objectives never block their chapter; unmarked ones never show on the HUD (UMissionObjective).
OPTIONAL_OBJECTIVES = {"examine_courier", "examine_ornament"}
UNMARKED_OBJECTIVES = {"examine_courier", "examine_ornament"}
# The campaign flag an objective raises when it completes, for later acts (UHawkeyeCampaignState::CampaignFlags).
OBJECTIVE_FLAGS = {"examine_courier": "saw_courier", "examine_ornament": "found_ornament"}
# A stealth objective's title (and hint) once a thug has seen her (UMissionObjective::SpottedTitle): a draft.
SPOTTED_TITLES = {"enter_grotto": ("Get to the workshop door. They know you're here", "")}
# The actor (label or tag) an objective's marker follows where it is found; elsewhere its registered volume (UMissionObjective::
# MarkerActorLabel): the grotto's street door outside, the workshop door and the roof stairs inside.
MARKER_LABELS = {"enter_grotto": "Site_GrottoDoor", "open_vault": "door:workshop", "leave_roof": "exit:roof"}

# --------------------------------------------------------------------------------------
# chapter 2: Reindeer Games
# --------------------------------------------------------------------------------------

CH02_NAME = "DA_CH02_ReindeerGames"
SITE = "/Game/Missions/Sites/DA_Site_CH02_ToyDrive"
FLASHBACK_CH02 = "/Game/Flashbacks/Definitions/DA_FB02_VisitingHours"
CH02_FIELDS = [
    ("mission_name", "Reindeer Games"),
    ("mission_number", 2),
    ("end_card_line", "Hawkeye. Just the one word. Merry Christmas."),
    ("show_objective_text", True),
    ("allow_switching", False),
    ("partner_present", False),
    ("opening_title", "Reindeer Games"),
    ("opening_subtitle", "Somebody is robbing a toy drive."),
    ("return_to_roaming_at_end", True),
    ("chapter_complete_toast", "[Chapter complete]"),
    ("draft_text", True),
]
# From ACT1-script.md, every one a draft. The site completes them: the arrival volume, the crew going down, the table.
CH02_OBJECTIVES = [
    ("reach_drive", "Get to the toy drive", ""),
    ("stop_crew", "Stop the reindeer crew", ""),
    ("return_toys", "Bring the toys back", ""),
    ("examine_courier", "Look at what the police are standing around", ""),
]
# Act 1's arrows: chapter 1 the plain arrow and the grapple, chapter 2 the sticky foam (the putty arrow, granted by
# the arrival event below), chapter 4 the confetti (its boss's last phase). A chapter's grant holds the earlier unlocks.
CH02_ARROWS = [("DA_Arrow_Standard", 30), ("DA_Arrow_Grapple", 6)]
FOAM = ("DA_Arrow_Putty", 3)
# The chapter's events (UMissionDefinition::Events): (event, DT_Dialogue lines, arrows). Lines are create_narrative's
# CH02_LINES. Arriving at the drive: her lines, the crew's bark and her reply, then the foam unlocked.
CH02_EVENTS = [
    ("objective_completed:reach_drive", ["ch02_arrive", "ch02_masks", "ch02_bark", "ch02_bark_reply"], []),
    ("objective_completed:reach_drive", ["ch02_foam_unlocked"], [FOAM]),
    # Reactive: the fight's lines jump the arrival's queue, or are dropped when more than 3 s late.
    ("parry", ["ch02_parry"], [], True),
    ("arrow_loosed:DA_Arrow_Putty", ["ch02_foam_used"], [], True),
    ("objective_completed:stop_crew", ["ch02_won"], []),
    ("objective_completed:return_toys", ["ch02_toys_returned"], []),
    ("objective_completed:examine_courier", ["ch02_courier"], []),
]
# The returned toys say "Pick up the sacks" until she has them: the table asks for all three (create_site.py).
CH02_HINTS = {"return_toys": "Pick up the three sacks, then take them to the table"}

# --------------------------------------------------------------------------------------
# chapter 3: The Grotto
# --------------------------------------------------------------------------------------

CH03_NAME = "DA_CH03_Grotto"
CH03_SITE = "/Game/Missions/Sites/DA_Site_CH03_Grotto"
CH03_INSIDE = "/Game/Missions/Sites/DA_Site_CH03_GrottoInside"
CH03_BOSS = "/Game/Missions/Bosses/DA_Boss_CH03_Santa"
CH03_FIELDS = [
    ("mission_name", "The Grotto"),
    ("mission_number", 3),
    ("end_card_line", "Good grotto. Bad Santa. Home."),
    ("show_objective_text", True),
    ("allow_switching", False),
    ("partner_present", False),
    ("opening_title", "The Grotto"),
    ("opening_subtitle", "Somebody stole a sleigh."),
    ("enforce_order", True),
    ("return_to_roaming_at_end", True),
    ("chapter_complete_toast", "[Chapter complete]"),
    ("draft_text", True),
]
# From ACT1-script.md, every one a draft (beat_santa is ours: the fight needs an objective of its own).
CH03_OBJECTIVES = [
    ("find_sleigh", "Find the stolen sleigh display", ""),
    ("enter_grotto", "Get into Santa's grotto without being seen", ""),
    ("open_vault", "Find out what's behind the workshop door", ""),
    ("beat_santa", "Deal with Santa", ""),
    ("leave_roof", "Get out by the roof", ""),
    ("examine_ornament", "Look at the ornament", ""),
]
# Chapter 2's foam carried in; the boomerang is found in the grotto's stockroom (its trigger's event).
CH03_ARROWS = [("DA_Arrow_Standard", 30), ("DA_Arrow_Grapple", 6), ("DA_Arrow_Putty", 2)]
BOOMERANG = ("DA_Arrow_Boomerang", 2)
# (event, DT_Dialogue lines, arrows[, reactive]). Lines are create_narrative's CH03_LINES. Reactive lines (the takedown,
# the boomerang coming back) jump the queue and are dropped when late (UDialogueQueue::ReactiveMaxLateSeconds).
CH03_EVENTS = [
    ("objective_started:find_sleigh", ["ch03_hook"], []),
    ("objective_completed:find_sleigh", ["ch03_outside"], []),
    ("trigger:first_elf", ["ch03_first_guard"], []),
    ("trigger:overheard", ["ch03_overheard_01", "ch03_overheard_02"], []),
    ("takedown", ["ch03_takedown"], [], True),
    ("trigger:boomerang", ["ch03_boomerang_unlocked"], [BOOMERANG]),
    ("arrow_self_hit:DA_Arrow_Boomerang", ["ch03_boomerang_back"], [], True),
    ("spotted:enter_grotto", ["ch03_spotted"], [], True),
    ("objective_completed:open_vault", ["ch03_vault"], []),
    ("objective_completed:examine_ornament", ["ch03_ornament_01", "ch03_ornament_02"], []),
]

# --------------------------------------------------------------------------------------
# chapter 4: The Social Club
# --------------------------------------------------------------------------------------

CH04_NAME = "DA_CH04_SocialClub"
BOSS = "/Game/Missions/Bosses/DA_Boss_CH04_Tomas"
CH04_FIELDS = [
    ("mission_name", "The Social Club"),
    ("mission_number", 4),
    ("end_card_line", "Sloan money. That's Mom's. ...Not tonight."),
    ("show_objective_text", True),
    ("allow_switching", False),
    ("partner_present", False),
    ("opening_title", "The Social Club"),
    ("opening_subtitle", "Tomas would like a word. Then a fight."),
    # No chapter 5 yet: after the end card, back to the district.
    ("return_to_roaming_at_end", True),
    ("chapter_complete_toast", "[Chapter complete]"),
    ("draft_text", True),
]
# From ACT1-script.md, every one a draft; the boss fight completes each (gameplay-semantics.md, "Bosses").
CH04_OBJECTIVES = [
    ("meet_tomas", "Go to the social club roof", ""),
    ("beat_crew", "Deal with Tomas's guys", ""),
    ("beat_tomas", "Deal with Tomas", ""),
    ("survive_santa", "Don't get flattened by Santa", ""),
]
CH04_BOW = "DA_Bow_Kate"
# Chapter 2's foam carried in (chapter select can open it straight away); the confetti comes in its last phase.
CH04_ARROWS = [("DA_Arrow_Standard", 30), ("DA_Arrow_Grapple", 6), ("DA_Arrow_Putty", 2), ("DA_Arrow_Boomerang", 2)]

# The HUD's second line under an objective's title (UMissionObjective::Hint), by id.
OBJECTIVE_HINTS = dict(CH02_HINTS)


def _hint_value(obj):
    try:
        return _text_value(obj.get_editor_property("hint"))
    except Exception:  # noqa: BLE001
        return ""


# The chapter's starting quiver: the bow, then (arrow asset, count) per type. The trick arrows are
# here for testing (claude-docs/gameplay-semantics.md, "trick arrows"); explosive is left out, so
# slot 7 starts empty.
CH01_BOW = "DA_Bow_Kate"
CH01_ARROWS = [("DA_Arrow_Standard", 30), ("DA_Arrow_Grapple", 6)]


def _object_path(value):
    """Package path of an object or a soft reference to one ("" for none), so both compare equal."""
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


def _quiver_matches(asset, bow, arrows):
    try:
        if _object_path(asset.get_editor_property("starting_bow")) != _object_path(bow):
            return False
        current = list(asset.get_editor_property("starting_arrows") or [])
    except Exception:  # noqa: BLE001
        return False
    if len(current) != len(arrows):
        return False
    for grant, (arrow, count) in zip(current, arrows):
        same_arrow = _object_path(grant.get_editor_property("arrow")) == _object_path(arrow)
        if not same_arrow or int(grant.get_editor_property("count")) != count:
            return False
    return True


def _apply_quiver(asset, name, bow_name, arrow_names):
    """StartingBow and StartingArrows from a bow name and [(arrow name, count)]. Returns True when it changed."""
    bow = c.load_or_none(c.asset_path(WEAPON_PATH, bow_name))
    arrows = [(c.load_or_none(c.asset_path(WEAPON_PATH, arrow)), count) for arrow, count in arrow_names]
    if bow is None or any(arrow is None for arrow, _count in arrows):
        c.log("skipped", name + " quiver", "run create_weapon_data first")
        return False
    if _quiver_matches(asset, bow, arrows):
        return False
    grants = []
    for arrow, count in arrows:
        grant = unreal.HawkeyeArrowGrant()
        grant.set_editor_property("arrow", arrow)
        grant.set_editor_property("count", count)
        grants.append(grant)
    return bool(c.set_props(asset, [("starting_bow", bow), ("starting_arrows", grants)], name))


def _text_value(value):
    return str(value) if value is not None else ""


def _objectives_match(asset, wanted):
    try:
        current = list(asset.get_editor_property("objectives") or [])
    except Exception:  # noqa: BLE001
        return False
    if len(current) != len(wanted):
        return False
    for obj, (oid, title, description) in zip(current, wanted):
        if obj is None or objective_id(obj) != oid:
            return False
        if _text_value(obj.get_editor_property("title")) != title:
            return False
        if _text_value(obj.get_editor_property("description")) != description:
            return False
        if _hint_value(obj) != OBJECTIVE_HINTS.get(oid, ""):
            return False
        if bool(obj.get_editor_property("optional")) != (oid in OPTIONAL_OBJECTIVES):
            return False
        if bool(obj.get_editor_property("unmarked")) != (oid in UNMARKED_OBJECTIVES):
            return False
        if str(obj.get_editor_property("campaign_flag")) != OBJECTIVE_FLAGS.get(oid, "None"):
            return False
        if bool(obj.get_editor_property("draft")) != (oid in DRAFT_OBJECTIVES):
            return False
        spotted = SPOTTED_TITLES.get(oid, ("", ""))
        if _text_value(obj.get_editor_property("spotted_title")) != spotted[0]:
            return False
        if _text_value(obj.get_editor_property("spotted_hint")) != spotted[1]:
            return False
        if str(obj.get_editor_property("marker_actor_label")) != MARKER_LABELS.get(oid, "None"):
            return False
    return True


def _build_objectives(asset, objective_cls, wanted):
    objectives = []
    for oid, title, description in wanted:
        objective = unreal.new_object(objective_cls, outer=asset)
        c.set_first_prop(objective, OBJECTIVE_ID_PROPS, oid, "MissionObjective " + oid)
        c.set_props(objective, [("title", title), ("description", description), ("optional", oid in OPTIONAL_OBJECTIVES),
                                ("unmarked", oid in UNMARKED_OBJECTIVES),
                                ("campaign_flag", unreal.Name(OBJECTIVE_FLAGS.get(oid, "None"))),
                                ("draft", oid in DRAFT_OBJECTIVES)], "MissionObjective " + oid)
        if OBJECTIVE_HINTS.get(oid):
            c.set_props(objective, [("hint", OBJECTIVE_HINTS[oid])], "MissionObjective " + oid)
        if oid in SPOTTED_TITLES:
            c.set_props(objective, [("spotted_title", SPOTTED_TITLES[oid][0]), ("spotted_hint", SPOTTED_TITLES[oid][1])],
                        "MissionObjective " + oid)
        if oid in MARKER_LABELS:
            c.set_props(objective, [("marker_actor_label", unreal.Name(MARKER_LABELS[oid]))], "MissionObjective " + oid)
        objectives.append(objective)
    return objectives


def ensure_mission(name, fields, objectives, quiver=None):
    """A mission data asset. Creates it, or corrects whichever fields differ."""
    full = c.asset_path(MISSION_PATH, name)
    cls = c.find_class("MissionDefinition", "/Script/Hawkeye.MissionDefinition")
    objective_cls = c.find_class("MissionObjective", "/Script/Hawkeye.MissionObjective")
    if cls is None or objective_cls is None:
        c.log("FAILED", full, "UMissionDefinition / UMissionObjective not exposed to Python")
        return None

    asset, created = c.create_asset(name, MISSION_PATH, cls, data_asset_factory(cls), quiet=True)
    if asset is None:
        return None

    changed = []
    for prop, value in fields:
        try:
            current = asset.get_editor_property(prop)
        except Exception as exc:  # noqa: BLE001
            c.log_error("{0}.{1}".format(full, prop), exc)
            continue
        same = (bool(current) == value) if isinstance(value, bool) else (
            current == value if isinstance(value, int) else _text_value(current) == value)
        if not same and c.set_props(asset, [(prop, value)], name):
            changed.append(prop)

    if not _objectives_match(asset, objectives):
        if c.set_props(asset, [("objectives", _build_objectives(asset, objective_cls, objectives))], name):
            changed.append("objectives")

    if quiver is not None and _apply_quiver(asset, name, quiver[0], quiver[1]):
        changed.append("starting quiver")

    if created or changed:
        c.save(asset)
        c.log("created" if created else "updated", full,
              "{0} objectives".format(len(objectives)) if created else ", ".join(changed))
    else:
        c.log("exists", full)
    return asset


def _set_reference(asset, name, prop, path, missing):
    """prop = the asset at path (None for an empty path). Returns [prop] when it changed."""
    wanted = c.load_or_none(path) if path else None
    if path and wanted is None:
        c.log("skipped", "{0}.{1}".format(name, prop), missing)
        return []
    if _object_path(asset.get_editor_property(prop)) == _object_path(wanted):
        return []
    return [prop] if c.set_props(asset, [(prop, wanted)], name) else []


def _apply_interior_sites(asset, name, paths):
    """InteriorSites from asset paths. Returns ["interior sites"] when it changed."""
    wanted = [c.load_or_none(path) for path in paths]
    if any(w is None for w in wanted):
        c.log("skipped", name + ".interior_sites", "run create_grotto first")
        return []
    current = [_object_path(s) for s in list(asset.get_editor_property("interior_sites") or [])]
    if current == [_object_path(w) for w in wanted]:
        return []
    return ["interior sites"] if c.set_props(asset, [("interior_sites", wanted)], name) else []


def _apply_references(asset, name, chase, boss, next_chapter, site=None, flashback=None, interior_sites=()):
    """Chase, Boss, NextChapter, Site, InteriorSites and the flashback (none unless given). Returns what changed."""
    changed = _set_reference(asset, name, "chase", chase, "run create_chase first")
    changed += _set_reference(asset, name, "boss", boss, "run create_boss first")
    changed += _set_reference(asset, name, "next_chapter", next_chapter, "the next chapter is not made yet")
    changed += _set_reference(asset, name, "site", site, "run create_site first")
    changed += _apply_interior_sites(asset, name, list(interior_sites))
    if flashback:
        changed += _set_reference(asset, name, "flashback_to_play", flashback, "run create_narrative first")
    elif _object_path(asset.get_editor_property("flashback_to_play")):
        if c.set_props(asset, [("flashback_to_play", None)], name):
            changed.append("no flashback")
    return changed


def _events_key(events):
    """The chapter's events as plain data, to compare with what the asset holds."""
    return [(str(e[0]), [str(x) for x in e[1]], [(a, n) for a, n in e[2]], bool(e[3]) if len(e) > 3 else False) for e in events]


def _asset_events_key(asset):
    out = []
    for entry in list(asset.get_editor_property("events") or []):
        grants = [(_object_path(g.get_editor_property("arrow")).split("/")[-1], int(g.get_editor_property("count")))
                  for g in list(entry.get_editor_property("arrows") or [])]
        out.append((str(entry.get_editor_property("event")), [str(x) for x in entry.get_editor_property("lines")], grants,
                    bool(entry.get_editor_property("reactive"))))
    return out


def _apply_events(asset, name, events):
    """UMissionDefinition::Events from [(event, lines, [(arrow, count)])]. Returns [\"events\"] when it changed."""
    if _asset_events_key(asset) == _events_key(events):
        return []
    entries = []
    for spec in events:
        event, lines, arrows = spec[0], spec[1], spec[2]
        entry = unreal.HawkeyeChapterEvent()
        entry.set_editor_property("event", unreal.Name(event))
        entry.set_editor_property("lines", [unreal.Name(line) for line in lines])
        entry.set_editor_property("reactive", bool(spec[3]) if len(spec) > 3 else False)
        grants = []
        for arrow, count in arrows:
            definition = c.load_or_none(c.asset_path(WEAPON_PATH, arrow))
            if definition is None:
                c.log("skipped", name + " events", "run create_weapon_data first")
                return []
            grant = unreal.HawkeyeArrowGrant()
            grant.set_editor_property("arrow", definition)
            grant.set_editor_property("count", count)
            grants.append(grant)
        entry.set_editor_property("arrows", grants)
        entries.append(entry)
    return ["events"] if c.set_props(asset, [("events", entries)], name) else []


def remove_retired():
    for old in RETIRED:
        full = c.asset_path(MISSION_PATH, old)
        if not c.exists(full):
            continue
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        referencers = [str(r) for r in registry.get_referencers(full, unreal.AssetRegistryDependencyOptions())]
        if referencers:
            # BP_GameMode_EastVillage starts it until generate_city.py points the district at CH01_NAME.
            c.log("skipped", full, "still referenced by " + ", ".join(referencers))
            continue
        unreal.EditorAssetLibrary.delete_asset(full)
        # A commandlet can report the delete and leave the file; then the file goes by hand.
        disk = os.path.join(c.project_dir(), "Content", full[len("/Game/"):] + ".uasset")
        if os.path.exists(disk):
            os.remove(disk)
        c.log("deleted", full, "replaced by " + CH01_NAME)


def _chapter(name, fields, objectives, quiver, chase, boss, next_chapter, site=None, flashback=None, events=(), interior_sites=()):
    asset = ensure_mission(name, fields, objectives, quiver=quiver)
    if asset is not None:
        changed = _apply_references(asset, name, chase, boss, next_chapter, site, flashback, interior_sites)
        changed += _apply_events(asset, name, list(events))
        if changed:
            c.save(asset)
            c.log("updated", c.asset_path(MISSION_PATH, name), ", ".join(changed))
    return asset


def create_chapter_four():
    """DA_CH04_SocialClub: the boss chapter, no chase, no next chapter yet."""
    return _chapter(CH04_NAME, CH04_FIELDS, CH04_OBJECTIVES, (CH04_BOW, CH04_ARROWS), None, BOSS, None)


def create_chapter_three():
    """DA_CH03_Grotto: the grotto (a street site and an interior one), Santa, the boomerang; then The Social Club."""
    return _chapter(CH03_NAME, CH03_FIELDS, CH03_OBJECTIVES, (CH01_BOW, CH03_ARROWS), None, CH03_BOSS,
                    c.asset_path(MISSION_PATH, CH04_NAME), site=CH03_SITE, events=CH03_EVENTS, interior_sites=[CH03_INSIDE])


def create_chapter_two():
    """DA_CH02_ReindeerGames: the toy drive (its site), the foam, the slides; then The Grotto."""
    import create_narrative  # noqa: PLC0415
    # The slides are create_narrative's (its lines); made here first so the chapter can name them on one run.
    flashback = FLASHBACK_CH02 if create_narrative.ensure_visiting_hours() is not None else None
    return _chapter(CH02_NAME, CH02_FIELDS, CH02_OBJECTIVES, (CH01_BOW, CH02_ARROWS), None, None,
                    c.asset_path(MISSION_PATH, CH03_NAME), site=SITE, flashback=flashback, events=CH02_EVENTS)


def create_chapter_one():
    """DA_CH01_NightShift, then Reindeer Games in the campaign's order."""
    return _chapter(CH01_NAME, CH01_FIELDS, CH01_OBJECTIVES, (CH01_BOW, CH01_ARROWS), CHASE, None,
                    c.asset_path(MISSION_PATH, CH02_NAME))


def create_scene_mission():
    """DA_Scene_Placeholder: no bow, one objective."""
    return ensure_mission(SCENE_NAME, SCENE_FIELDS, SCENE_OBJECTIVES)


def run():
    c.ensure_directory(MISSION_PATH)
    create_scene_mission()
    create_chapter_four()
    create_chapter_three()
    create_chapter_two()
    chapter = create_chapter_one()
    remove_retired()
    return chapter


if __name__ == "__main__":
    run()
    c.print_summary("mission data")
