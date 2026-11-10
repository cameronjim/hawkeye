"""Create every starter asset, in dependency order.

    1. create_input_assets        IA_* and IMC_Default
    1b. create_audio              every sound, synthesised as MetaSounds (MS_*), the sound classes,
                                  the settings mix and the attenuations, under /Game/Audio
    1c. create_vfx                every particle effect (NS_*) and its materials under /Game/VFX, the
                                  Niagara plugin's template emitters tuned by UHawkeyeVfxBuilder
    2. import_gasp                the Game Animation Sample's sandbox character, AnimBP and
                                  UEFN mannequin, copied from the local GASP install (skipped
                                  when the sample is not installed; before BP_Kate derives from it)
    3. create_blueprints          BP_Hawkeye*, BP_Kate and WBP_Flashback
    3b. _materials                interior surface and lamp materials (M_Concrete, M_SteelPainted,
                                  M_Emissive and its instances)
    3c. create_world_blueprints   WBP_Hud, BP_Thug, BP_GrappleAnchor, BP_TraversableBlock
    3d. create_weapon_data        DA_Weapon_Hands, the bows and arrows
    4. create_mission_data        DA_CH01_Rooftops
    4b. create_partner            BP_Clint, ST_Partner, BP_PartnerController, DT_Dialogue (after the
                                  weapon data, for DA_Bow_Clint)
    4c. create_enemies            ST_Thug, EQS_CoverPoints (wired into BP_Thug) and BP_Archer (after
                                  the world blueprints and the weapon data)
    4d. create_bow_ik             ABP_BowIK_Post(_Thug), the bow hands post-process AnimBPs, set on
                                  BP_Kate, BP_Clint and BP_Archer (after the partner and the enemies)
    4d1. create_kate_look         Kate drawn as Paragon's Sparrow over the hidden mannequin: IK rig,
                                  retargeter, retarget AnimBP, purple instances, BP_Kate.BodyLook
                                  (skipped, one line, when Sparrow is not in Content)
    4d2. create_combat_anims      DA_AnimSet_Kate/_Clint/_Thug/_Archer filled from whatever AM_ combat
                                  montages Tools/import-anims.ps1 has made (none on a fresh clone), set
                                  on BP_Kate, BP_Clint, BP_Thug and BP_Archer (after the bow IK graph,
                                  whose slots the clips play in)
    4e. apply_audio_defaults      (create_blueprints) the sounds on BP_HawkeyeCharacter, BP_Thug,
                                  BP_HawkeyePlayerController and the trick arrows' data assets
    4f. apply_vfx_defaults        (create_blueprints) the effects on the same classes and data assets
    5. fixup_redirectors          resave past any redirector the GASP copy brought in, then
                                  delete it
    5b. create_narrative          DT_Messages, DT_DialogueSequences, L_Scene_Placeholder and its game
                                  mode, DA_FB00_Placeholder (after the mission data and the partner's
                                  DT_Dialogue); it opens its own map, so it runs just before the city
    5c. create_challenges         the side challenges (DA_Challenge_*) under /Game/Challenges, planned
                                  from the same OpenStreetMap records the city is (skipped, like the
                                  city, until the records exist); generate_city places their pedestals
    5d. create_crimes            the street crimes (DA_Crime_*) under /Game/Crimes and BP_Civilian, the
                                  mugging's victim; generate_city places the City_CrimeSpot_ spots they
                                  start at, planned by create_crimes from the same records
    5e. generate_interior         every chapter interior, L_Int_<Name>, from its layout in Tools/Interiors/
                                  (BP_GameMode_Interior, the M_Int* materials); before the city, whose
                                  City_InteriorEntrance_ doors point at them
    6. generate_city              L_District_EastVillage from OpenStreetMap, after everything
                                  else and only when Tools/Data/osm/east_village.buildings.json
                                  exists (Tools/fetch-osm.ps1 writes it)

Run headless:

    UnrealEditor-Cmd.exe Hawkeye.uproject -run=pythonscript ^
        -script="Tools\\Editor\\create_all.py" -unattended -nullrhi -nosplash -nop4 -stdout

or via Tools\\create-content.ps1. Every step is idempotent, so re-running only fills gaps.
"""

import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _common as c  # noqa: E402

# (title, module, function). The function defaults to run().
STEPS = [
    ("input assets", "create_input_assets"),
    ("audio", "create_audio"),
    ("vfx", "create_vfx"),
    ("gasp import", "import_gasp"),
    ("blueprints", "create_blueprints"),
    ("materials", "_materials"),
    ("world blueprints", "create_world_blueprints"),
    ("weapon data", "create_weapon_data"),
    ("mission data", "create_mission_data"),
    ("partner", "create_partner"),
    ("enemies", "create_enemies"),
    ("bow ik", "create_bow_ik"),
    ("kate look", "create_kate_look"),
    ("combat anims", "create_combat_anims"),
    ("audio defaults", "create_blueprints", "apply_audio_defaults"),
    ("vfx defaults", "create_blueprints", "apply_vfx_defaults"),
    ("fix up redirectors", "fixup_redirectors"),
    ("narrative", "create_narrative"),
    ("challenges", "create_challenges"),
    ("crimes", "create_crimes"),
    ("interiors", "generate_interior"),
    ("city", "generate_city"),
]

# Module names to leave out of this run, comma separated, e.g. HAWKEYE_SKIP_STEPS=generate_city.
SKIP_STEPS = set(
    name.strip() for name in os.environ.get("HAWKEYE_SKIP_STEPS", "").split(",") if name.strip())

# Steps that need downloaded data: skipped (not failed) until the file exists.
_OSM_RECORDS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "Data", "osm", "east_village.buildings.json")
STEP_REQUIRES = {
    "create_challenges": _OSM_RECORDS,
    "generate_city": _OSM_RECORDS,
}


def main():
    c.reset_summary()
    unreal.log("[Hawkeye] ==== creating starter content ====")

    failures = []
    for step in STEPS:
        title, module_name = step[0], step[1]
        function_name = step[2] if len(step) > 2 else "run"
        unreal.log("[Hawkeye] ---- {0} ----".format(title))
        if module_name in SKIP_STEPS:
            c.log("skipped", title, "HAWKEYE_SKIP_STEPS")
            continue
        required = STEP_REQUIRES.get(module_name)
        if required and not os.path.exists(required):
            c.log("skipped", title, "missing " + required)
            continue
        try:
            module = __import__(module_name)
            getattr(module, function_name)()
        except Exception as exc:  # noqa: BLE001 - one broken step must not stop the rest
            failures.append(title)
            unreal.log_error(
                "[Hawkeye] FAILED   step '{0}' ({1}: {2})".format(title, type(exc).__name__, exc)
            )
            unreal.log_error(traceback.format_exc())

    unreal.log("[Hawkeye] ==== summary ====")
    for action, path, extra in c.summary():
        unreal.log("[Hawkeye] {0:<8} {1}{2}".format(action, path, "  (" + extra + ")" if extra else ""))

    counts = c.print_summary("content creation complete")
    if failures:
        unreal.log_error("[Hawkeye] steps that raised: " + ", ".join(failures))
    if counts.get("FAILED"):
        unreal.log_error("[Hawkeye] {0} asset(s) failed".format(counts["FAILED"]))
    return counts


main()
