"""Cuts the portfolio demo video from an edit decision list.

    python Tools/Demo/cut.py                      # docs/demo/edl.json -> Saved/Demo/hawkeye-kate-bishop.mp4
    python Tools/Demo/cut.py --placeholder        # a rough cut: black slates stand in for missing owner clips
    python Tools/Demo/cut.py --dry-run            # check the list and print the ffmpeg commands, run nothing

The list is JSON (docs/demo/edl.json, the shot list as data). Each shot is one of:

    card    a title or text card on a dark ground: "lines" (strings, or {"text", "size"})
    frames  a numbered frame sequence from the scripted capture (Saved/Demo/<shot>/frame_%05d.png, 60 fps):
            "source" is the folder, "start" the second to start at
    clip    a video file, an owner recording or a screen capture: "source", "start", optional "speed" (a
            time-lapse: 8 plays eight seconds of source in one)

Every shot has an "id" and a "duration" (seconds in the finished video), and may have a "caption" (a lower
third) and "fade_in" / "fade_out" seconds (dip to black; the default is a hard cut, cards fade 0.4 s). A clip with
"owner": true may be missing when --placeholder is given; anything else missing is an error, and so is a finished
length outside the list's min_seconds and max_seconds (75 to 90 by default).

Each shot is encoded alone, 1920x1080, 60 fps, H.264 yuv420p at the list's bitrate (12M), then the pieces are
joined without re-encoding. Text is drawn with ffmpeg's drawtext from text files, so no caption needs escaping.
The font is, in order: --font, the HAWKEYE_FONT environment variable, Roboto under Tools/Demo/fonts or
Tools/Fonts (not committed: add Roboto-Regular.ttf there to use it), then a system font (Segoe UI or Arial on
Windows, DejaVu Sans on Linux). Standard library only.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_EDL = REPO / "docs" / "demo" / "edl.json"
DEFAULT_OUT = REPO / "Saved" / "Demo" / "hawkeye-kate-bishop.mp4"

SHOT_TYPES = ("card", "frames", "clip")
CARD_FADE = 0.4
CARD_BACKGROUND = "0x0b0d12"

FONT_CANDIDATES = (
    "Tools/Demo/fonts/Roboto-Regular.ttf",
    "Tools/Fonts/Roboto-Regular.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
)


class EdlError(Exception):
    """The edit decision list is wrong or cannot be built."""


@dataclass
class Shot:
    id: str
    type: str
    duration: float
    caption: str = ""
    source: str = ""
    start: float = 0.0
    speed: float = 1.0
    owner: bool = False
    lines: list = field(default_factory=list)
    fade_in: float = 0.0
    fade_out: float = 0.0


@dataclass
class Edl:
    shots: list
    fps: int = 60
    width: int = 1920
    height: int = 1080
    bitrate: str = "12M"
    min_seconds: float = 75.0
    max_seconds: float = 90.0
    output: str = ""


# ---- parsing and arithmetic (pure) --------------------------------------------------------------------------


def _number(value, what, minimum=None, exclusive=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise EdlError(f"{what} must be a number, got {value!r}")
    if minimum is not None and (value <= minimum if exclusive else value < minimum):
        raise EdlError(f"{what} must be {'over' if exclusive else 'at least'} {minimum}, got {value}")
    return float(value)


def parse_shot(raw, fps):
    if not isinstance(raw, dict):
        raise EdlError(f"a shot must be an object, got {raw!r}")
    shot_id = raw.get("id")
    if not isinstance(shot_id, str) or not shot_id:
        raise EdlError(f"a shot has no id: {raw!r}")
    kind = raw.get("type")
    if kind not in SHOT_TYPES:
        raise EdlError(f"shot {shot_id}: type must be one of {', '.join(SHOT_TYPES)}, got {kind!r}")
    duration = _number(raw.get("duration"), f"shot {shot_id}: duration", 0, exclusive=True)
    if round(duration * fps) < 1:
        raise EdlError(f"shot {shot_id}: duration {duration} is under one frame")
    shot = Shot(id=shot_id, type=kind, duration=duration)
    shot.caption = str(raw.get("caption", ""))
    shot.owner = bool(raw.get("owner", False))
    shot.start = _number(raw.get("start", 0), f"shot {shot_id}: start", 0)
    shot.speed = _number(raw.get("speed", 1), f"shot {shot_id}: speed", 0, exclusive=True)
    fade_default = CARD_FADE if kind == "card" else 0.0
    shot.fade_in = _number(raw.get("fade_in", fade_default), f"shot {shot_id}: fade_in", 0)
    shot.fade_out = _number(raw.get("fade_out", fade_default), f"shot {shot_id}: fade_out", 0)
    if shot.fade_in + shot.fade_out > duration:
        raise EdlError(f"shot {shot_id}: the fades are longer than the shot")
    if kind == "card":
        lines = raw.get("lines")
        if not isinstance(lines, list) or not lines:
            raise EdlError(f"shot {shot_id}: a card needs a list of lines")
        for line in lines:
            if not (isinstance(line, str) or (isinstance(line, dict) and isinstance(line.get("text"), str))):
                raise EdlError(f"shot {shot_id}: a line is a string or an object with text, got {line!r}")
        shot.lines = lines
    else:
        source = raw.get("source")
        if not isinstance(source, str) or not source:
            raise EdlError(f"shot {shot_id}: a {kind} shot needs a source")
        shot.source = source
    return shot


def parse_edl(raw):
    if not isinstance(raw, dict):
        raise EdlError("the list must be a JSON object")
    fps = int(_number(raw.get("fps", 60), "fps", 0, exclusive=True))
    shots_raw = raw.get("shots")
    if not isinstance(shots_raw, list) or not shots_raw:
        raise EdlError("the list has no shots")
    shots = [parse_shot(item, fps) for item in shots_raw]
    seen = set()
    for shot in shots:
        if shot.id in seen:
            raise EdlError(f"shot id {shot.id} is used twice")
        seen.add(shot.id)
    edl = Edl(shots=shots, fps=fps)
    edl.width = int(_number(raw.get("width", 1920), "width", 0, exclusive=True))
    edl.height = int(_number(raw.get("height", 1080), "height", 0, exclusive=True))
    edl.bitrate = str(raw.get("bitrate", "12M"))
    edl.min_seconds = _number(raw.get("min_seconds", 75), "min_seconds", 0)
    edl.max_seconds = _number(raw.get("max_seconds", 90), "max_seconds", 0)
    edl.output = str(raw.get("output", ""))
    if edl.min_seconds > edl.max_seconds:
        raise EdlError("min_seconds is over max_seconds")
    return edl


def load_edl(path):
    try:
        with open(path, encoding="utf-8") as handle:
            raw = json.load(handle)
    except OSError as error:
        raise EdlError(f"cannot read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise EdlError(f"{path} is not valid JSON: {error}") from error
    return parse_edl(raw)


def shot_frames(shot, fps):
    """Frames a shot lasts in the video: its duration in whole frames."""
    return max(1, round(shot.duration * fps))


def total_seconds(edl):
    """The finished length, as the encoder will make it (each shot a whole number of frames)."""
    return sum(shot_frames(shot, edl.fps) for shot in edl.shots) / edl.fps


def check_length(edl, minimum=None, maximum=None):
    """Raises EdlError when the finished length is outside the range; returns it otherwise."""
    low = edl.min_seconds if minimum is None else minimum
    high = edl.max_seconds if maximum is None else maximum
    total = total_seconds(edl)
    if total < low or total > high:
        raise EdlError(f"the cut is {total:.2f} s; it must be {low:g} to {high:g} s")
    return total


def source_frames_needed(shot, fps):
    """The last frame number a frames shot reads, counting from 1."""
    return round(shot.start * fps) + shot_frames(shot, fps)


def clip_source_seconds(shot):
    """Seconds of a clip's source the shot reads."""
    return shot.duration * shot.speed


# ---- sources ------------------------------------------------------------------------------------------------


def resolve_source(shot, root=REPO):
    path = Path(shot.source)
    return path if path.is_absolute() else root / path


def count_frames(folder):
    folder = Path(folder)
    if not folder.is_dir():
        return 0
    return sum(1 for item in folder.iterdir() if item.name.startswith("frame_") and item.suffix == ".png")


@dataclass
class Check:
    problems: list = field(default_factory=list)
    placeholders: set = field(default_factory=set)


def check_sources(edl, placeholder, root=REPO):
    """Which sources are missing or too short. Owner clips become placeholders when asked for."""
    result = Check()
    for shot in edl.shots:
        if shot.type == "card":
            continue
        path = resolve_source(shot, root)
        if shot.type == "frames":
            have = count_frames(path)
            need = source_frames_needed(shot, edl.fps)
            if have == 0:
                missing = True
            elif have < need:
                result.problems.append(f"{shot.id}: {path} has {have} frames, the shot reads up to frame {need}")
                continue
            else:
                continue
        else:
            missing = not path.is_file()
            if not missing:
                continue
        if shot.owner and placeholder:
            result.placeholders.add(shot.id)
        else:
            what = "no frames in" if shot.type == "frames" else "missing"
            result.problems.append(f"{shot.id}: {what} {path}")
    return result


# ---- ffmpeg arguments (pure) --------------------------------------------------------------------------------


def find_font(explicit=None, root=REPO):
    """The font file to draw with, or None. See the module comment for the order."""
    candidates = []
    if explicit:
        candidates.append(explicit)
    if os.environ.get("HAWKEYE_FONT"):
        candidates.append(os.environ["HAWKEYE_FONT"])
    candidates.extend(FONT_CANDIDATES)
    for item in candidates:
        path = Path(item)
        if not path.is_absolute():
            path = root / path
        if path.is_file():
            return path
    return None


def fmt(value):
    """A number as ffmpeg likes it: no exponent, no trailing zeros."""
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    return text or "0"


def drawtext(textfile, size, y, enable=None, box=False):
    """One drawtext filter. y is an ffmpeg expression. The text is read from textfile (relative to the work folder)."""
    parts = [
        "drawtext=fontfile=font.ttf",
        f"textfile={textfile}",
        "expansion=none",
        "fontcolor=white",
        f"fontsize={size}",
        "x=(w-text_w)/2",
        f"y={y}",
    ]
    if box:
        parts.extend(["box=1", "boxcolor=black@0.6", "boxborderw=18"])
    if enable:
        parts.append(f"enable='{enable}'")
    return ":".join(parts)


def card_lines(shot):
    """(text, size) per line: the first is large, the rest medium, unless a line says its own size."""
    out = []
    for index, line in enumerate(shot.lines):
        if isinstance(line, dict):
            size = int(line.get("size", 84 if index == 0 else 40))
            out.append((line["text"], size))
        else:
            out.append((line, 84 if index == 0 else 40))
    return out


def card_layout(lines, height):
    """Top y of each line so the block is centred. Line height is 1.5 times its size."""
    heights = [round(size * 1.5) for _, size in lines]
    top = (height - sum(heights)) // 2
    tops = []
    for line_height in heights:
        tops.append(top)
        top += line_height
    return tops


@dataclass
class Segment:
    """One piece of the video: the ffmpeg arguments to make it and the text files they read, name to text."""

    name: str
    args: list
    files: dict


def _fade_filters(shot, duration):
    filters = []
    if shot.fade_in > 0:
        filters.append(f"fade=t=in:st=0:d={fmt(shot.fade_in)}")
    if shot.fade_out > 0:
        filters.append(f"fade=t=out:st={fmt(duration - shot.fade_out)}:d={fmt(shot.fade_out)}")
    return filters


def _caption_filter(shot, index, duration, files, height):
    if not shot.caption:
        return None
    name = f"caption_{index:02d}.txt"
    files[name] = shot.caption
    window = f"between(t,0.3,{fmt(max(duration - 0.3, 0.3))})"
    return drawtext(name, 40, f"{height}-{round(height * 0.14)}", enable=window, box=True)


def build_segment(shot, index, edl, ffmpeg="ffmpeg", placeholder=False, source_root=REPO):
    """The ffmpeg command that makes one shot (run with the work folder as the current folder).

    With placeholder true the shot becomes a labelled black slate, whatever its type: the owner's clip is not there yet.
    """
    frames = shot_frames(shot, edl.fps)
    duration = frames / edl.fps
    width, height, fps = edl.width, edl.height, edl.fps
    out_name = f"seg_{index:02d}_{shot.id}.mp4"
    files = {}
    filters = []
    inputs = []

    if placeholder:
        label = f"owner clip missing: {Path(shot.source).name}"
        files[f"slate_{index:02d}_a.txt"] = label
        files[f"slate_{index:02d}_b.txt"] = shot.caption or shot.id
        inputs = ["-f", "lavfi", "-i", f"color=c=black:s={width}x{height}:r={fps}"]
        filters.append(drawtext(f"slate_{index:02d}_a.txt", 56, f"({height}-text_h)/2-40"))
        filters.append(drawtext(f"slate_{index:02d}_b.txt", 36, f"({height}-text_h)/2+50"))
    elif shot.type == "card":
        inputs = ["-f", "lavfi", "-i", f"color=c={CARD_BACKGROUND}:s={width}x{height}:r={fps}"]
        lines = card_lines(shot)
        for line_number, ((text, size), top) in enumerate(zip(lines, card_layout(lines, height))):
            name = f"card_{index:02d}_{line_number}.txt"
            files[name] = text
            filters.append(drawtext(name, size, str(top)))
    elif shot.type == "frames":
        folder = resolve_source(shot, source_root)
        first = 1 + round(shot.start * fps)
        pattern = (folder / "frame_%05d.png").as_posix()
        inputs = ["-framerate", str(fps), "-start_number", str(first), "-i", pattern]
        filters.append(f"scale={width}:{height}:flags=lanczos")
    else:  # clip
        path = resolve_source(shot, source_root).as_posix()
        inputs = ["-ss", fmt(shot.start), "-t", fmt(clip_source_seconds(shot)), "-i", path]
        if shot.speed != 1.0:
            filters.append(f"setpts=PTS/{fmt(shot.speed)}")
        filters.append(f"fps={fps}")
        filters.append(f"scale={width}:{height}:force_original_aspect_ratio=decrease:flags=lanczos")
        filters.append(f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2")

    filters.append("setsar=1")
    if shot.type != "card" or placeholder:
        caption = None if placeholder else _caption_filter(shot, index, duration, files, height)
        if caption:
            filters.append(caption)
    filters.extend(_fade_filters(shot, duration))
    filters.append("format=yuv420p")

    args = [ffmpeg, "-y", "-hide_banner", "-loglevel", "error", *inputs, "-vf", ",".join(filters), "-frames:v", str(frames)]
    args += [
        "-r", str(fps),
        "-an",
        "-c:v", "libx264",
        "-preset", "slow",
        "-b:v", edl.bitrate,
        "-maxrate", edl.bitrate,
        "-bufsize", _double_rate(edl.bitrate),
        "-g", str(fps),
        "-pix_fmt", "yuv420p",
        out_name,
    ]
    return Segment(name=out_name, args=args, files=files)


def _double_rate(rate):
    """12M -> 24M, 8000k -> 16000k: the rate-control buffer."""
    digits = "".join(ch for ch in rate if ch.isdigit() or ch == ".")
    unit = rate[len(digits):]
    return f"{fmt(float(digits) * 2)}{unit}"


def build_concat(segment_names, out_path, ffmpeg="ffmpeg", list_name="concat.txt"):
    """(the list file's text, the ffmpeg arguments) joining the pieces without re-encoding."""
    text = "".join(f"file '{name}'\n" for name in segment_names)
    args = [ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", list_name,
            "-c", "copy", "-movflags", "+faststart", str(out_path)]
    return text, args


# ---- running ------------------------------------------------------------------------------------------------


def run(args, cwd):
    try:
        subprocess.run(args, cwd=cwd, check=True)
    except FileNotFoundError as error:
        raise EdlError(f"cannot run {args[0]}: {error}") from error
    except subprocess.CalledProcessError as error:
        raise EdlError(f"ffmpeg failed (exit {error.returncode}): {' '.join(args)}") from error


def make_video(edl, out_path, ffmpeg, font, placeholders, dry_run=False, keep_work=False, source_root=REPO):
    """Builds every segment, joins them. Returns the finished length in seconds."""
    if font is None and not dry_run:
        raise EdlError("no font found: pass --font or put Roboto-Regular.ttf in Tools/Demo/fonts (see cut.py's header)")
    work = Path(tempfile.mkdtemp(prefix="hawkeye-cut-"))
    try:
        if font is not None:
            shutil.copyfile(font, work / "font.ttf")
        names = []
        for index, shot in enumerate(edl.shots, start=1):
            segment = build_segment(shot, index, edl, ffmpeg, placeholder=shot.id in placeholders, source_root=source_root)
            for name, text in segment.files.items():
                (work / name).write_text(text, encoding="utf-8")
            print(f"[{index}/{len(edl.shots)}] {shot.id} ({shot.type}, {shot_frames(shot, edl.fps) / edl.fps:.2f} s)"
                  f"{' [placeholder]' if shot.id in placeholders else ''}")
            if dry_run:
                print("   " + " ".join(segment.args))
            else:
                run(segment.args, work)
            names.append(segment.name)
        out_path = Path(out_path)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        text, args = build_concat(names, out_path.resolve(), ffmpeg)
        (work / "concat.txt").write_text(text, encoding="utf-8")
        if dry_run:
            print("   " + " ".join(args))
        else:
            run(args, work)
    finally:
        if keep_work:
            print(f"work folder: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)
    return total_seconds(edl)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Cut the Hawkeye demo video from an edit decision list.")
    parser.add_argument("edl", nargs="?", default=str(DEFAULT_EDL), help="the list (default docs/demo/edl.json)")
    parser.add_argument("--out", help="the video (default the list's output, else Saved/Demo/hawkeye-kate-bishop.mp4)")
    parser.add_argument("--placeholder", action="store_true", help="a labelled black slate for each missing owner clip")
    parser.add_argument("--dry-run", action="store_true", help="check everything and print the ffmpeg commands, run none")
    parser.add_argument("--keep-work", action="store_true", help="keep the pieces and text files")
    parser.add_argument("--ffmpeg", default=os.environ.get("FFMPEG") or "ffmpeg", help="the ffmpeg to run")
    parser.add_argument("--font", help="a .ttf file to draw text with")
    parser.add_argument("--min", type=float, help="override the list's minimum length, seconds")
    parser.add_argument("--max", type=float, help="override the list's maximum length, seconds")
    options = parser.parse_args(argv)

    try:
        edl = load_edl(options.edl)
        total = check_length(edl, options.min, options.max)
        problems = check_sources(edl, options.placeholder)
        if problems.problems:
            raise EdlError("sources:\n  " + "\n  ".join(problems.problems))
        ffmpeg = shutil.which(options.ffmpeg) or options.ffmpeg
        if not options.dry_run and not shutil.which(options.ffmpeg) and not Path(options.ffmpeg).is_file():
            raise EdlError(f"ffmpeg not found ({options.ffmpeg}); put it on PATH or pass --ffmpeg")
        out = Path(options.out) if options.out else (Path(edl.output) if edl.output else DEFAULT_OUT)
        if not out.is_absolute():
            out = REPO / out
        if problems.placeholders:
            print("placeholders: " + ", ".join(sorted(problems.placeholders)))
        make_video(edl, out, ffmpeg, find_font(options.font), problems.placeholders, options.dry_run, options.keep_work)
    except EdlError as error:
        print(f"cut.py: {error}", file=sys.stderr)
        return 2 if "ffmpeg failed" in str(error) else 1
    print(f"{'would write' if options.dry_run else 'wrote'} {out}: {total:.2f} s, {math.ceil(total * edl.fps)} frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
