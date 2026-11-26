"""Tests for Tools/Gate/select.py and rules.json. No engine needed; CI and the gate run them.

    python -B -m unittest discover -s Tools/Gate -p "test_*.py" -v
"""

import glob
import importlib.util
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# select.py shares its name with a standard library module: load it by path under another name and take this
# folder back off sys.path (discovery put it there) so nothing else can import the wrong `select`.
_spec = importlib.util.spec_from_file_location("gate_select", os.path.join(HERE, "select.py"))
gate_select = importlib.util.module_from_spec(_spec)
sys.modules["gate_select"] = gate_select
_spec.loader.exec_module(gate_select)
sys.path[:] = [p for p in sys.path if os.path.abspath(p) != HERE]

DATA = gate_select.load_rules(os.path.join(HERE, "rules.json"))


def plan_for(*paths, read_file=None):
    return gate_select.select(list(paths), DATA, read_file)


def source_test_names():
    names = []
    for pattern in ("Source/**/*.cpp", "Plugins/KawaiiPhysics/Source/**/*.cpp"):
        for path in glob.glob(os.path.join(ROOT, pattern), recursive=True):
            with open(path, encoding="utf-8", errors="ignore") as handle:
                names.extend(gate_select.test_names_in(handle.read()))
    return names


class GlobTests(unittest.TestCase):
    def test_double_star_crosses_folders_and_star_does_not(self):
        rx = gate_select.glob_to_regex("Source/Hawkeye/Traversal/**")
        self.assertTrue(rx.match("Source/Hawkeye/Traversal/GrappleAudit.cpp"))
        self.assertTrue(rx.match("Source/Hawkeye/Traversal/Deep/Er/File.h"))
        self.assertFalse(rx.match("Source/Hawkeye/Traversal"))
        rx = gate_select.glob_to_regex("Tools/Editor/*.py")
        self.assertTrue(rx.match("Tools/Editor/_materials.py"))
        self.assertFalse(rx.match("Tools/Editor/sub/x.py"))

    def test_match_is_case_insensitive_and_slash_agnostic(self):
        plan = plan_for("DOCS\\Design.MD")
        self.assertTrue(plan["empty"])

    def test_leading_double_star_matches_the_root_too(self):
        self.assertTrue(gate_select.glob_to_regex("**/*.md").match("README.md"))
        self.assertTrue(gate_select.glob_to_regex("**/*.md").match("a/b/README.md"))


class PlanTests(unittest.TestCase):
    def test_docs_only_is_empty(self):
        plan = plan_for("docs/DESIGN.md", "claude-docs/testing.md", "CLAUDE.md", ".claude/settings.json", "README.md")
        self.assertTrue(plan["empty"])
        self.assertFalse(plan["build"])
        self.assertEqual(plan["estimateSeconds"], 0)
        self.assertEqual(plan["steps"], [])

    def test_no_changes_is_empty(self):
        self.assertTrue(plan_for()["empty"])

    def test_committed_assets_alone_run_nothing(self):
        self.assertTrue(plan_for("Content/Materials/M_SteelPainted.uasset", "Content/VFX/NS_ArrowTrail.uasset")["empty"])

    def test_unknown_path_goes_broad(self):
        plan = plan_for("Something/Unheard/Of.txt")
        self.assertTrue(plan["broad"])
        self.assertTrue(plan["build"])
        self.assertTrue(plan["wholeSuite"])
        self.assertTrue(plan["python"])
        self.assertEqual(plan["laps"], [])
        self.assertFalse(plan["risky"])

    def test_unknown_source_file_goes_broad(self):
        self.assertTrue(plan_for("Source/Hawkeye/Brandnew/Thing.cpp")["wholeSuite"])

    def test_config_uproject_build_and_plugin_files_are_broad(self):
        for path in ("Config/DefaultEngine.ini", "Hawkeye.uproject", "Source/Hawkeye/Hawkeye.Build.cs",
                     "Source/HawkeyeEditor.Target.cs", "Plugins/SPUD/Source/SPUD/Private/SpudState.cpp"):
            plan = plan_for(path)
            self.assertTrue(plan["broad"] and plan["wholeSuite"] and plan["build"], path)
            self.assertEqual(plan["laps"], [], path)

    def test_broad_does_not_add_laps_unless_risky(self):
        plan = plan_for("Config/DefaultGame.ini")
        self.assertEqual(plan["laps"], [])
        self.assertFalse(plan["risky"])

    def test_python_only(self):
        plan = plan_for("Tools/Editor/test_geo.py", "Tools/Gate/select.py", "Tools/gate.ps1", "ruff.toml")
        self.assertTrue(plan["python"])
        self.assertFalse(plan["build"])
        self.assertFalse(plan["content"])
        self.assertEqual(plan["tests"], [])
        self.assertEqual(plan["laps"], [])
        self.assertFalse(plan["broad"])
        self.assertEqual([s["id"] for s in plan["steps"]], ["python"])

    def test_any_tools_file_runs_python_alongside_its_own_rule(self):
        plan = plan_for("Tools/Editor/create_vfx.py")
        self.assertTrue(plan["python"])
        self.assertTrue(plan["content"])

    def test_clip_build_runs_the_anim_content_steps_and_pose_tests_but_no_laps(self):
        plan = plan_for("Tools/Blender/build_clips.py")
        self.assertTrue(plan["content"])
        self.assertEqual(plan["contentSteps"], ["authored clips", "traversal anims", "combat anims", "bow ik"])
        self.assertFalse(plan["build"])
        self.assertEqual(plan["laps"], [])
        self.assertFalse(plan["cityVerify"] or plan["audit"])
        for expected in ("Hawkeye.Animation", "Hawkeye.Ledge", "Hawkeye.BodyPose"):
            self.assertIn(expected, plan["tests"])

    def test_traversal_cpp(self):
        plan = plan_for("Source/Hawkeye/Traversal/GrappleReachQuery.cpp")
        self.assertTrue(plan["build"] and plan["audit"])
        self.assertEqual(plan["laps"], ["EastVillage", "CrimeRooftop"])
        for expected in ("Hawkeye.Traversal", "Hawkeye.Ledge", "Hawkeye.Grapple"):
            self.assertIn(expected, plan["tests"])
        self.assertFalse(plan["risky"])
        self.assertFalse(plan["content"])

    def test_player_ledge_parkour_and_grapple_files_are_traversal(self):
        for path in ("Source/Hawkeye/Player/ParkourComponent.cpp", "Source/Hawkeye/Player/GrappleComponent.h",
                     "Source/Hawkeye/Player/LedgeIKNode.cpp", "Source/Hawkeye/Player/BodyPose.cpp"):
            self.assertTrue(plan_for(path)["audit"], path)

    def test_player_fallback_does_not_apply_to_traversal_files(self):
        self.assertNotIn("Hawkeye.Movement", plan_for("Source/Hawkeye/Player/ParkourHang.cpp")["tests"])
        plan = plan_for("Source/Hawkeye/Player/HawkeyeMovementComponent.cpp")
        self.assertIn("Hawkeye.Movement", plan["tests"])
        self.assertFalse(plan["audit"])

    def test_combat_cpp(self):
        plan = plan_for("Source/Hawkeye/Combat/MeleeComponent.cpp")
        self.assertTrue(plan["build"])
        self.assertEqual(plan["laps"], ["CrimeMugging", "ReindeerGames"])
        self.assertIn("Hawkeye.Melee", plan["tests"])
        self.assertFalse(plan["audit"])

    def test_mission_cpp_is_risky_with_campaign(self):
        plan = plan_for("Source/Hawkeye/Mission/MissionSubsystem.cpp")
        self.assertTrue(plan["risky"] and plan["build"])
        self.assertIn("Campaign", plan["laps"])
        self.assertIn("Hawkeye.Mission", plan["tests"])

    def test_risky_areas(self):
        for path in ("Source/Hawkeye/Flashback/FlashbackSequencer.cpp", "Source/Hawkeye/Save/HawkeyeSaveSubsystem.cpp",
                     "Source/Hawkeye/Settings/HawkeyeSettingsSubsystem.cpp", "Source/Hawkeye/HawkeyeGameMode.cpp",
                     "Source/Hawkeye/HawkeyePlayerController.h", "Source/Hawkeye/World/ThugCharacter.cpp",
                     "Source/Hawkeye/World/ThugTreeBuilder.cpp", "Source/Hawkeye/Partner/PartnerTreeBuilder.cpp"):
            plan = plan_for(path)
            self.assertTrue(plan["risky"], path)
            self.assertIn("Campaign", plan["laps"], path)

    def test_thug_files_in_world_do_not_fall_to_the_world_fallback(self):
        plan = plan_for("Source/Hawkeye/World/ThugAIController.cpp")
        self.assertNotIn("Hawkeye.Door", plan["tests"])
        self.assertIn("Hawkeye.Thug", plan["tests"])

    def test_world_fallback(self):
        plan = plan_for("Source/Hawkeye/World/DoorActor.cpp")
        self.assertIn("Hawkeye.Door", plan["tests"])
        self.assertEqual(plan["laps"], [])

    def test_vfx_and_light_rig_build_without_laps(self):
        for path in ("Source/Hawkeye/Vfx/SnowfallComponent.cpp", "Source/Hawkeye/World/CharacterLightRig.cpp"):
            plan = plan_for(path)
            self.assertTrue(plan["build"], path)
            self.assertEqual(plan["laps"], [], path)
            self.assertFalse(plan["cityVerify"], path)

    def test_vfx_script_runs_just_its_content_steps(self):
        plan = plan_for("Tools/Editor/create_vfx.py")
        self.assertEqual(plan["contentSteps"], ["vfx", "vfx defaults"])
        self.assertFalse(plan["build"])
        self.assertEqual(plan["laps"], [])
        self.assertFalse(plan["cityVerify"])

    def test_materials_script_is_content_only(self):
        plan = plan_for("Tools/Editor/_materials.py")
        self.assertEqual(plan["contentSteps"], ["materials"])
        self.assertFalse(plan["cityVerify"] or plan["audit"] or plan["laps"])

    def test_generator_runs_city_verify_audit_and_the_roof_laps(self):
        plan = plan_for("Tools/Editor/generate_city.py")
        self.assertTrue(plan["cityVerify"] and plan["audit"])
        self.assertEqual(plan["laps"], ["EastVillage", "CrimeRooftop"])

    def test_dressing_is_city_verify_only(self):
        plan = plan_for("Tools/Editor/_dressing_build.py")
        self.assertTrue(plan["cityVerify"])
        self.assertFalse(plan["audit"])
        self.assertEqual(plan["laps"], [])

    def test_editor_modules(self):
        plan = plan_for("Source/HawkeyeEditor/STraversalDebuggerPanel.cpp")
        self.assertTrue(plan["build"])
        self.assertIn("Hawkeye.Editor", plan["tests"])
        plan = plan_for("Source/HawkeyeAnimEditor/AnimGraphNode_HawkeyeLedgeIK.cpp")
        self.assertTrue(plan["build"])
        self.assertIn("Hawkeye.Ledge", plan["tests"])

    def test_unlisted_editor_script_runs_content_once_in_full(self):
        plan = plan_for("Tools/Editor/_common.py")
        self.assertTrue(plan["content"])
        self.assertEqual(plan["contentSteps"], ["all"])

    def test_editor_test_files_are_not_content_scripts(self):
        self.assertFalse(plan_for("Tools/Editor/test_dressing.py")["content"])

    def test_a_test_file_selects_its_own_tests_and_its_laps(self):
        source = '''IMPLEMENT_SIMPLE_AUTOMATION_TEST(FA, "Hawkeye.Foo.Bar", Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FB,
    "Hawkeye.Lap.CrimeMugging", Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FC, "Hawkeye.Lap.StreetFight", Flags)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FD, "Hawkeye.Screenshot.Foo", Flags)'''
        plan = plan_for("Source/Hawkeye/Tests/FooTest.cpp", read_file=lambda p: source)
        self.assertEqual(plan["tests"], ["Hawkeye.Foo.Bar"])
        self.assertEqual(plan["laps"], ["CrimeMugging"])
        self.assertTrue(plan["build"])
        self.assertFalse(plan["broad"])

    def test_a_test_file_without_tests_is_a_shared_helper_so_broad(self):
        self.assertTrue(plan_for("Source/Hawkeye/Tests/HawkeyeTestUtils.cpp", read_file=lambda p: "int x;")["broad"])
        self.assertTrue(plan_for("Source/Hawkeye/Tests/LapBot.h")["broad"])

    def test_union_over_several_changes(self):
        plan = plan_for("docs/a.md", "Source/Hawkeye/Combat/HealthComponent.cpp", "Source/Hawkeye/Traversal/GrappleAudit.cpp",
                        "Tools/Editor/create_vfx.py")
        self.assertTrue(plan["build"] and plan["audit"] and plan["content"] and plan["python"])
        self.assertEqual(plan["laps"], ["CrimeMugging", "ReindeerGames", "EastVillage", "CrimeRooftop"])
        self.assertIn("Hawkeye.Health", plan["tests"])
        self.assertIn("Hawkeye.Grapple", plan["tests"])
        self.assertIn("Hawkeye.Vfx", plan["tests"])

    def test_union_with_one_unknown_path_goes_broad_but_keeps_laps(self):
        plan = plan_for("Source/Hawkeye/Combat/HealthComponent.cpp", "Unknown/File.bin")
        self.assertTrue(plan["wholeSuite"])
        self.assertEqual(plan["tests"], [])
        self.assertIn("CrimeMugging", plan["laps"])

    def test_content_all_beats_a_list(self):
        plan = plan_for("Tools/Editor/_common.py", "Tools/Editor/create_vfx.py")
        self.assertEqual(plan["contentSteps"], ["all"])

    def test_subsumed_filters_are_dropped(self):
        plan = plan_for("Tools/Blender/build_clips.py", "Source/Hawkeye/Traversal/GrappleAudit.cpp")
        self.assertIn("Hawkeye.Traversal", plan["tests"])
        self.assertNotIn("Hawkeye.Traversal.Pose", plan["tests"])

    def test_estimates_add_up_and_timings_override(self):
        plan = plan_for("Source/Hawkeye/Traversal/GrappleAudit.cpp")
        self.assertEqual(plan["estimateSeconds"], sum(s["seconds"] for s in plan["steps"]))
        self.assertGreater(plan["estimateSeconds"], 0)
        steps = gate_select.plan_steps(plan, DATA, {"build": 1, "lap:EastVillage": 7})
        by_id = {s["id"]: s["seconds"] for s in steps}
        self.assertEqual(by_id["build"], 1)
        self.assertEqual(by_id["lap:EastVillage"], 7)

    def test_each_matched_path_lists_its_rules(self):
        plan = plan_for("docs/a.md", "Tools/Gate/rules.json")
        self.assertEqual([m["path"] for m in plan["matched"]], ["docs/a.md", "Tools/Gate/rules.json"])
        self.assertTrue(all(m["rules"] for m in plan["matched"]))


class RulesFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.names = source_test_names()

    def test_there_are_tests_to_check_against(self):
        self.assertGreater(len(self.names), 300)

    def test_every_filter_matches_a_test_in_source(self):
        # The engine matches a filter as a substring of the test's path (a leading ^ or trailing $ anchors it).
        missing = []
        for rule in DATA["rules"]:
            for flt in rule.get("tests", []):
                core = flt.lstrip("^").rstrip("$")
                if not any(core in name for name in self.names):
                    missing.append((rule["name"], flt))
        self.assertEqual(missing, [])

    def test_filters_are_runnable_headless(self):
        for rule in DATA["rules"]:
            for flt in rule.get("tests", []):
                self.assertFalse(any(flt.startswith(p) for p in DATA["standaloneTestPrefixes"]), flt)
                self.assertNotIn("+", flt)
                self.assertNotIn(";", flt)
                self.assertNotIn(" ", flt)

    def test_every_lap_exists_and_is_one_of_the_full_gate_laps(self):
        lap_tests = {n[len("Hawkeye.Lap."):] for n in self.names if n.startswith("Hawkeye.Lap.")}
        for lap in DATA["fullLaps"]:
            self.assertIn(lap, lap_tests, lap)
        used = {lap for rule in DATA["rules"] for lap in rule.get("laps", [])}
        self.assertEqual(sorted(used - set(DATA["fullLaps"])), [])
        self.assertEqual(sorted(set(DATA["flakyLaps"]) - set(DATA["fullLaps"])), [])

    def test_there_are_thirteen_full_laps_and_four_flaky_ones(self):
        self.assertEqual(len(DATA["fullLaps"]), 13)
        self.assertEqual(len(set(DATA["fullLaps"])), 13)
        self.assertEqual(len(DATA["flakyLaps"]), 4)

    def test_laps_are_named_in_testing_md(self):
        with open(os.path.join(ROOT, "claude-docs", "testing.md"), encoding="utf-8") as handle:
            text = handle.read()
        for lap in DATA["fullLaps"]:
            self.assertTrue(("Hawkeye.Lap." + lap) in text, lap)

    def test_content_steps_are_create_all_step_titles(self):
        with open(os.path.join(ROOT, "Tools", "Editor", "create_all.py"), encoding="utf-8") as handle:
            text = handle.read()
        block = text[text.index("STEPS = ["):text.index("# Module names to leave out")]
        titles = set(re.findall(r'^\s*\("([^"]+)",', block, re.MULTILINE))
        self.assertGreater(len(titles), 20)
        for rule in DATA["rules"]:
            for title in rule.get("contentSteps", []):
                self.assertTrue(title == "all" or title in titles, (rule["name"], title))

    def test_a_rule_that_runs_content_names_its_steps(self):
        for rule in DATA["rules"]:
            if rule.get("content"):
                self.assertTrue(rule.get("contentSteps"), rule["name"])

    def test_every_rule_has_a_name_and_globs(self):
        names = [r["name"] for r in DATA["rules"]]
        self.assertEqual(len(names), len(set(names)))
        for rule in DATA["rules"]:
            self.assertTrue(rule["_match"], rule["name"])

    def test_expected_audit_numbers_are_the_documented_ones(self):
        self.assertEqual(DATA["expectedAudit"], {"anchors": 2457, "pairs": 24272})

    def test_every_source_file_is_matched_by_a_rule(self):
        # A new source folder with no rule would go broad: fine, but say so here so the rules grow with the tree.
        unmatched = []
        for path in glob.glob(os.path.join(ROOT, "Source", "**", "*.*"), recursive=True):
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            if not any(gate_select.rule_matches(r, rel) for r in DATA["rules"]):
                unmatched.append(rel)
        self.assertEqual(unmatched, [])


if __name__ == "__main__":
    unittest.main()
