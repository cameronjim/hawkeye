"""Unit tests for looks.py (the enemies' faces and builds as data). Standard library only, no Blender, no numpy:

    python Tools/Blender/test_looks.py
    (the engine's Python works too: Engine\\Binaries\\ThirdParty\\Python3\\Win64\\python.exe)
"""

import math
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import looks  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NUMERIC = [name for name in looks.FACE_RANGES]


def face_distance(a, b):
    """How far apart two faces are, in units of each parameter's own range (root mean square)."""
    total = 0.0
    for name, (lo, hi) in looks.FACE_RANGES.items():
        total += ((getattr(a, name) - getattr(b, name)) / (hi - lo)) ** 2
    return math.sqrt(total / len(looks.FACE_RANGES))


class TableTests(unittest.TestCase):
    def test_every_row_and_table_is_valid(self):
        self.assertEqual(looks.all_problems(), [])

    def test_ids_match_keys_and_families(self):
        for key, look in looks.LOOKS.items():
            self.assertEqual(key, look.id)
            self.assertEqual(look.face.id, look.id)
            self.assertIn(look.family, looks.FAMILIES)
            self.assertTrue(look.id.startswith(look.family))

    def test_default_ids_exist(self):
        # build_thug and build_heavy build 'thug' and 'heavy' when no --look is given, and import_bodies knows those two.
        self.assertEqual(looks.get_look("").id, "thug")
        self.assertEqual(looks.get_look("", default="heavy").id, "heavy")
        with self.assertRaises(KeyError):
            looks.get_look("nobody")

    def test_there_are_three_or_four_thug_faces_and_one_heavy(self):
        self.assertGreaterEqual(len(looks.look_ids("thug")), 3)
        self.assertLessEqual(len(looks.look_ids("thug")), 4)
        self.assertEqual(looks.look_ids("heavy"), ["heavy"])

    def test_faces_are_distinct(self):
        ids = looks.look_ids()
        for i, a in enumerate(ids):
            for b in ids[i + 1:]:
                d = face_distance(looks.LOOKS[a].face, looks.LOOKS[b].face)
                self.assertGreater(d, 0.18, "{0} and {1} are too alike ({2:.2f})".format(a, b, d))

    def test_thugs_differ_by_more_than_the_face(self):
        thugs = [looks.LOOKS[i].face for i in looks.look_ids("thug")]
        self.assertGreaterEqual(len({(f.hair_style, f.headwear, f.facial_hair) for f in thugs}), 3)
        self.assertGreaterEqual(len({f.skin for f in thugs}), 3)
        self.assertGreaterEqual(len({looks.LOOKS[i].build.id for i in looks.look_ids("thug")}), 2)

    def test_blueprint_rows_name_real_looks_and_start_with_their_own(self):
        for bp, ids in looks.BLUEPRINT_LOOKS.items():
            self.assertTrue(ids)
            self.assertEqual(len(set(ids)), len(ids))
            for look_id in ids:
                self.assertIn(look_id, looks.LOOKS)
        self.assertEqual(looks.BLUEPRINT_LOOKS["BP_Thug"][0], "thug")
        self.assertEqual(set(looks.BLUEPRINT_LOOKS["BP_Thug"]), set(looks.look_ids("thug")))

    def test_import_bodies_lists_every_look(self):
        # import_bodies.py needs the engine's `unreal`, so read it as text.
        with open(os.path.join(REPO, "Tools", "Editor", "import_bodies.py"), encoding="utf-8") as f:
            text = f.read()
        for look_id in looks.look_ids():
            self.assertRegex(text, r'"{0}"'.format(re.escape(look_id)), look_id + " is not in import_bodies.py")


class ValidationTests(unittest.TestCase):
    def test_out_of_range_is_reported(self):
        bad = looks.with_face(looks.LOOKS["thug"], jaw_width=2.0)
        self.assertTrue(any("jaw_width" in p for p in bad.problems()))

    def test_unknown_choice_is_reported(self):
        bad = looks.with_face(looks.LOOKS["thug"], hair_style="mohawk", skin="blue")
        text = " ".join(bad.problems())
        self.assertIn("hair_style", text)
        self.assertIn("skin", text)

    def test_stubble_needs_a_style(self):
        bad = looks.with_face(looks.LOOKS["heavy"], stubble=0.5)
        self.assertTrue(any("stubble" in p for p in bad.problems()))

    def test_bad_id_is_reported(self):
        look = looks.Look("Thug B", "thug", looks.FaceSpec("Thug B"), looks.BUILDS["lean"])
        self.assertTrue(look.problems())
        wrong_family = looks.Look("heavy_b", "thug", looks.FaceSpec("heavy_b"), looks.BUILDS["lean"])
        self.assertTrue(any("starts with" in p for p in wrong_family.problems()))

    def test_unknown_parameters_are_refused(self):
        with self.assertRaises(KeyError):
            looks.with_face(looks.LOOKS["thug"], nose_length=1.0)

    def test_bool_is_not_a_number(self):
        bad = looks.with_face(looks.LOOKS["thug"], age=True)
        self.assertTrue(any("not a number" in p for p in bad.problems()))

    def test_build_ranges(self):
        bad = looks.BodyBuild("bad", shoulder=1.3)
        self.assertTrue(bad.problems())


class BodyTests(unittest.TestCase):
    def test_every_build_is_smaller_than_the_pack_body(self):
        # The pack's Superhero is 1.0 everywhere; ordinary men are narrower through the shoulders, chest, arms, thighs.
        for build in looks.BUILDS.values():
            self.assertLess(build.shoulder, 1.0)
            self.assertLess(build.chest, 1.0)
            self.assertLess(build.arm, 1.0)
            self.assertLess(build.thigh, 1.0)

    def test_builds_are_ordered_lean_to_heavy(self):
        order = ["lean", "average", "stocky", "heavy"]
        for a, b in zip(order, order[1:]):
            self.assertLess(looks.BUILDS[a].shoulder, looks.BUILDS[b].shoulder)
            self.assertLess(looks.BUILDS[a].belly, looks.BUILDS[b].belly)
            self.assertLess(looks.BUILDS[a].ankle, looks.BUILDS[b].ankle)

    def test_torso_profiles(self):
        for build in looks.BUILDS.values():
            x, front, back = looks.torso_scale_profiles(build)
            for profile in (x, front, back):
                self.assertEqual(len(profile), len(looks.PROFILE_Z))
                self.assertTrue(all(0.7 < v <= 1.05 for v in profile), profile)
            self.assertEqual(x[-1], 1.0)                      # the neck is not reshaped
            self.assertEqual(front[0], 1.0)
            self.assertAlmostEqual(x[4], build.shoulder)       # 1.30 m: the chest
            self.assertAlmostEqual(front[4], build.chest)
        self.assertEqual(list(looks.PROFILE_Z), sorted(looks.PROFILE_Z))

    def test_ankles_taper_to_a_trouser_that_is_not_a_balloon(self):
        for build in looks.BUILDS.values():
            self.assertLessEqual(build.ankle, 0.07)
            self.assertGreaterEqual(build.trouser_taper, 0.5)


if __name__ == "__main__":
    unittest.main()
