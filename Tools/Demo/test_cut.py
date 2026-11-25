"""Unit tests for cut.py. Plain unittest, no ffmpeg, no engine:

    python Tools/Demo/test_cut.py
    (or) python -B -m unittest discover -s Tools/Demo -p "test_*.py" -v
"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cut  # noqa: E402

REPO = Path(__file__).resolve().parents[2]


def shot(**kw):
    base = {"id": "a", "type": "card", "duration": 5, "lines": ["Title"]}
    base.update(kw)
    return base


def edl_of(*shots, **kw):
    raw = {"shots": list(shots)}
    raw.update(kw)
    return cut.parse_edl(raw)


class ParseTest(unittest.TestCase):
    def test_defaults(self):
        edl = edl_of(shot())
        self.assertEqual((edl.fps, edl.width, edl.height, edl.bitrate), (60, 1920, 1080, "12M"))
        self.assertEqual((edl.min_seconds, edl.max_seconds), (75, 90))

    def test_card_fades_by_default_and_others_do_not(self):
        edl = edl_of(shot(), shot(id="b", type="frames", source="x"))
        self.assertEqual(edl.shots[0].fade_in, cut.CARD_FADE)
        self.assertEqual(edl.shots[1].fade_in, 0.0)

    def test_rejects_bad_lists(self):
        bad = [
            {},
            {"shots": []},
            {"shots": [shot(type="gif")]},
            {"shots": [shot(duration=0)]},
            {"shots": [shot(duration="5")]},
            {"shots": [shot(lines=[])]},
            {"shots": [shot(), shot()]},
            {"shots": [shot(id="b", type="clip")]},
            {"shots": [shot(id="b", type="clip", source="x", speed=0)]},
            {"shots": [shot(fade_in=3, fade_out=3, duration=5)]},
            {"shots": [shot()], "min_seconds": 90, "max_seconds": 75},
            {"shots": [shot(duration=0.001)]},
        ]
        for raw in bad:
            with self.subTest(raw=raw), self.assertRaises(cut.EdlError):
                cut.parse_edl(raw)

    def test_missing_file_and_bad_json(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(cut.EdlError):
                cut.load_edl(Path(folder) / "none.json")
            broken = Path(folder) / "broken.json"
            broken.write_text("{", encoding="utf-8")
            with self.assertRaises(cut.EdlError):
                cut.load_edl(broken)


class DurationTest(unittest.TestCase):
    def test_frames_round_to_whole_frames(self):
        edl = edl_of(shot(duration=1.01))
        self.assertEqual(cut.shot_frames(edl.shots[0], 60), 61)
        self.assertAlmostEqual(cut.total_seconds(edl), 61 / 60)

    def test_total_is_the_sum(self):
        edl = edl_of(shot(duration=40), shot(id="b", duration=40.5))
        self.assertAlmostEqual(cut.total_seconds(edl), 80.5)

    def test_range_check(self):
        self.assertAlmostEqual(cut.check_length(edl_of(shot(duration=80))), 80.0)
        self.assertAlmostEqual(cut.check_length(edl_of(shot(duration=75))), 75.0)
        self.assertAlmostEqual(cut.check_length(edl_of(shot(duration=90))), 90.0)
        for seconds in (74.9, 90.1):
            with self.subTest(seconds=seconds), self.assertRaises(cut.EdlError):
                cut.check_length(edl_of(shot(duration=seconds)))

    def test_range_can_be_overridden(self):
        edl = edl_of(shot(duration=10))
        self.assertAlmostEqual(cut.check_length(edl, 5, 20), 10.0)
        with self.assertRaises(cut.EdlError):
            cut.check_length(edl, 11, 20)

    def test_source_reads(self):
        frames = edl_of(shot(id="f", type="frames", source="x", start=2, duration=3)).shots[0]
        self.assertEqual(cut.source_frames_needed(frames, 60), 120 + 180)
        clip = edl_of(shot(id="c", type="clip", source="x", speed=8, duration=5)).shots[0]
        self.assertEqual(cut.clip_source_seconds(clip), 40)

    def test_the_committed_list_is_in_range(self):
        edl = cut.load_edl(REPO / "docs" / "demo" / "edl.json")
        total = cut.check_length(edl)
        self.assertTrue(75 <= total <= 90, total)
        for item in edl.shots:
            if item.type == "card":
                self.assertTrue(item.lines, item.id)
        # The first card says what the project is, and neither card names the studio in the title.
        first = edl.shots[0]
        text = " ".join(line if isinstance(line, str) else line["text"] for line in first.lines)
        self.assertIn("Work in progress", text)
        self.assertIn("Unreal Engine 5", text)
        self.assertIn("Not affiliated with Marvel, Disney or Insomniac Games", text)
        self.assertNotIn("Insomniac", first.lines[0])
        owners = [item.id for item in edl.shots if item.owner]
        self.assertEqual(sorted(owners), ["stealth_takedown", "street_fight", "tomas_phase"])


class SourceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def make_frames(self, name, count):
        folder = self.root / name
        folder.mkdir(parents=True)
        for number in range(1, count + 1):
            (folder / f"frame_{number:05d}.png").write_bytes(b"x")

    def edl(self):
        return edl_of(
            shot(),
            shot(id="f", type="frames", source="seq", start=0, duration=1),
            shot(id="o", type="clip", source="owner/fight.mp4", owner=True),
            shot(id="s", type="clip", source="screen/run.mp4"),
        )

    def test_everything_missing(self):
        result = cut.check_sources(self.edl(), False, self.root)
        self.assertEqual(len(result.problems), 3)
        self.assertEqual(result.placeholders, set())

    def test_placeholder_covers_owner_clips_only(self):
        result = cut.check_sources(self.edl(), True, self.root)
        self.assertEqual(result.placeholders, {"o"})
        self.assertEqual(len(result.problems), 2)
        self.assertTrue(any(p.startswith("f:") for p in result.problems))
        self.assertTrue(any(p.startswith("s:") for p in result.problems))

    def test_present_sources_pass(self):
        self.make_frames("seq", 60)
        (self.root / "owner").mkdir()
        (self.root / "owner" / "fight.mp4").write_bytes(b"x")
        (self.root / "screen").mkdir()
        (self.root / "screen" / "run.mp4").write_bytes(b"x")
        result = cut.check_sources(self.edl(), False, self.root)
        self.assertEqual(result.problems, [])

    def test_too_few_frames(self):
        self.make_frames("seq", 59)
        result = cut.check_sources(self.edl(), True, self.root)
        self.assertTrue(any("59 frames" in p for p in result.problems))

    def test_frame_count_ignores_other_files(self):
        self.make_frames("seq", 3)
        (self.root / "seq" / "shot.json").write_text("{}", encoding="utf-8")
        self.assertEqual(cut.count_frames(self.root / "seq"), 3)
        self.assertEqual(cut.count_frames(self.root / "nothing"), 0)


class ArgsTest(unittest.TestCase):
    def build(self, raw, index=3, placeholder=False, root=REPO):
        edl = edl_of(raw)
        return cut.build_segment(edl.shots[0], index, edl, "ffmpeg", placeholder=placeholder, source_root=root)

    def vf(self, segment):
        return segment.args[segment.args.index("-vf") + 1]

    def test_encoding_settings(self):
        segment = self.build(shot())
        args = segment.args
        self.assertEqual(args[0], "ffmpeg")
        for flag, value in (("-c:v", "libx264"), ("-pix_fmt", "yuv420p"), ("-b:v", "12M"), ("-maxrate", "12M"),
                            ("-bufsize", "24M"), ("-r", "60"), ("-frames:v", "300")):
            self.assertEqual(args[args.index(flag) + 1], value, flag)
        self.assertIn("-an", args)
        self.assertEqual(args[-1], "seg_03_a.mp4")
        self.assertEqual(segment.name, "seg_03_a.mp4")

    def test_card_text_goes_through_files(self):
        segment = self.build(shot(lines=["Hawkeye: Kate Bishop", {"text": "100% fan project: no 'quotes'", "size": 28}]))
        self.assertEqual(set(segment.files.values()), {"Hawkeye: Kate Bishop", "100% fan project: no 'quotes'"})
        vf = self.vf(segment)
        self.assertIn("color=c=", " ".join(segment.args))
        self.assertIn("expansion=none", vf)
        self.assertNotIn("quotes", vf)
        self.assertIn("fontsize=84", vf)
        self.assertIn("fontsize=28", vf)
        self.assertIn("fade=t=in:st=0:d=0.4", vf)
        self.assertIn("fade=t=out:st=4.6:d=0.4", vf)
        self.assertTrue(vf.endswith("format=yuv420p"))

    def test_card_layout_is_centred(self):
        lines = [("a", 80), ("b", 40)]
        tops = cut.card_layout(lines, 1080)
        self.assertEqual(tops, [(1080 - 180) // 2, (1080 - 180) // 2 + 120])

    def test_frames_input(self):
        raw = shot(id="f", type="frames", source="Saved/Demo/lucky", start=1.5, duration=2, caption="Lucky")
        segment = self.build(raw)
        args = segment.args
        self.assertEqual(args[args.index("-start_number") + 1], "91")
        self.assertEqual(args[args.index("-framerate") + 1], "60")
        self.assertTrue(args[args.index("-i") + 1].endswith("Saved/Demo/lucky/frame_%05d.png"))
        self.assertEqual(args[args.index("-frames:v") + 1], "120")
        self.assertIn("scale=1920:1080", self.vf(segment))
        self.assertEqual(list(segment.files.values()), ["Lucky"])
        self.assertIn("between(t,0.3,1.7)", self.vf(segment))

    def test_clip_speed_and_fit(self):
        absolute = (Path(tempfile.gettempdir()) / "run.mp4").as_posix()
        raw = shot(id="c", type="clip", source=absolute, start=2, speed=8, duration=5)
        segment = self.build(raw)
        args = segment.args
        self.assertEqual(args[args.index("-ss") + 1], "2")
        self.assertEqual(args[args.index("-t") + 1], "40")
        self.assertEqual(args[args.index("-i") + 1], absolute)
        vf = self.vf(segment)
        self.assertIn("setpts=PTS/8", vf)
        self.assertIn("force_original_aspect_ratio=decrease", vf)
        self.assertIn("pad=1920:1080", vf)
        self.assertEqual(segment.files, {})

    def test_no_speed_filter_at_normal_speed(self):
        segment = self.build(shot(id="c", type="clip", source="screen/run.mp4"))
        self.assertNotIn("setpts", self.vf(segment))

    def test_placeholder_slate(self):
        raw = shot(id="o", type="clip", source="Saved/Demo/owner/street_fight.mp4", owner=True, duration=8, caption="Combat")
        segment = self.build(raw, placeholder=True)
        self.assertIn("color=c=black", " ".join(segment.args))
        self.assertIn("owner clip missing: street_fight.mp4", segment.files.values())
        self.assertIn("Combat", segment.files.values())
        self.assertEqual(segment.args[segment.args.index("-frames:v") + 1], "480")
        self.assertNotIn("-ss", segment.args)

    def test_durations_use_whole_frames(self):
        segment = self.build(shot(duration=1.01))
        self.assertEqual(segment.args[segment.args.index("-frames:v") + 1], "61")

    def test_buffer_size(self):
        self.assertEqual(cut._double_rate("12M"), "24M")
        self.assertEqual(cut._double_rate("8000k"), "16000k")
        self.assertEqual(cut._double_rate("1.5M"), "3M")

    def test_concat(self):
        text, args = cut.build_concat(["seg_01_a.mp4", "seg_02_b.mp4"], "out.mp4", "ffmpeg")
        self.assertEqual(text, "file 'seg_01_a.mp4'\nfile 'seg_02_b.mp4'\n")
        self.assertEqual(args[args.index("-c") + 1], "copy")
        self.assertEqual(args[-1], "out.mp4")
        self.assertIn("-safe", args)

    def test_fmt(self):
        self.assertEqual(fmt_all(0.3, 4.6, 12, 0.0), ["0.3", "4.6", "12", "0"])

    def test_terminal_shot_scrolls_the_file(self):
        edl = edl_of({"id": "t", "type": "terminal", "duration": 5, "source": "x/out.txt", "prompt": "PS> run"})
        with tempfile.TemporaryDirectory() as root:
            (Path(root) / "x").mkdir()
            (Path(root) / "x" / "out.txt").write_text("one" + chr(10) + "two" * 80 + chr(10), encoding="utf-8")
            seg = cut.build_segment(edl.shots[0], 1, edl, source_root=Path(root))
        self.assertIn("terminal_01.txt", seg.files)
        self.assertIn("prompt_01.txt", seg.files)
        text = seg.files["terminal_01.txt"]
        self.assertTrue(all(len(line) <= cut.TERMINAL_COLUMNS for line in text.splitlines()))
        joined = " ".join(seg.args)
        self.assertIn("fontfile=mono.ttf", joined)
        self.assertIn("text_h*min(t/3.8", joined)

    def test_font_lookup(self):
        with tempfile.TemporaryDirectory() as folder:
            font = Path(folder) / "f.ttf"
            font.write_bytes(b"x")
            self.assertEqual(cut.find_font(str(font)), font)


def fmt_all(*values):
    return [cut.fmt(v) for v in values]


class MainTest(unittest.TestCase):
    def test_a_short_list_fails_before_any_work(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "edl.json"
            path.write_text(json.dumps({"shots": [shot(duration=10)]}), encoding="utf-8")
            self.assertEqual(cut.main([str(path), "--dry-run"]), 1)

    def test_missing_source_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "edl.json"
            raw = {"min_seconds": 0, "shots": [shot(), shot(id="c", type="clip", source=str(Path(folder) / "none.mp4"))]}
            path.write_text(json.dumps(raw), encoding="utf-8")
            self.assertEqual(cut.main([str(path), "--dry-run"]), 1)

    def test_dry_run_with_placeholder_for_owner_clips(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "edl.json"
            raw = {"min_seconds": 0, "shots": [shot(), shot(id="c", type="clip", owner=True, source=str(Path(folder) / "none.mp4"))]}
            path.write_text(json.dumps(raw), encoding="utf-8")
            out = Path(folder) / "out.mp4"
            self.assertEqual(cut.main([str(path), "--dry-run", "--placeholder", "--out", str(out)]), 0)
            self.assertFalse(out.exists())

    def test_bad_json_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "edl.json"
            path.write_text("[]", encoding="utf-8")
            self.assertEqual(cut.main([str(path), "--dry-run"]), 1)


if __name__ == "__main__":
    unittest.main()
