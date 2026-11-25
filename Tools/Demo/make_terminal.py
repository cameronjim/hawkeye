"""Turns the real output of a real run into the text a terminal shot of the demo video scrolls.

    python Tools/Demo/make_terminal.py tests Saved/Logs/HawkeyeTests.log Saved/Demo/terminal/tests_run.txt
    python Tools/Demo/make_terminal.py text  <captured console output>    Saved/Demo/terminal/city_generation.txt

tests: reads the log Tools/run-tests.ps1 leaves (Saved/Logs/HawkeyeTests.log) and writes one "PASS <test path>" line
per "Test Completed" line, then the totals line in run-tests.ps1's own wording, counted from the same log with the
same patterns. Nothing is invented: a count that is not in the log is not in the text.

text: copies captured console output, dropping colour codes and blank runs.

city: the generator's and verifier's own lines from a captured generate-city.ps1 -Verify console (wrapped lines joined).
    python Tools/Demo/make_terminal.py city Saved/Demo/terminal/city_raw.txt Saved/Demo/terminal/city_generation.txt

lap: the LogHawkeye lines of a lap's log (Saved/Demo/laps/<Lap>.log from a -game run of Hawkeye.Lap.<Lap>) and its result.
    python Tools/Demo/make_terminal.py lap Saved/Demo/laps/Campaign.log Saved/Demo/terminal/lap_bot.txt

still: makes a clip of a picture, for a shot that is an image (the Traversal Debugger tab, drawn by
Hawkeye.Editor.TraversalDebugger.Shot): the picture on the card ground at 1920x1080, a slow push in on its upper half and a
drift down to the lists. Needs ffmpeg:
    python Tools/Demo/make_terminal.py still Saved/Demo/traversal_debugger/panel.png Saved/Demo/screen/traversal_debugger.mp4

Both print the line count and, for tests, the totals, so the caption can quote the number.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

DONE = re.compile(r"Test Completed\. Result=\{(?P<result>[A-Za-z]+)\} Name=\{(?P<name>[^}]*)\} Path=\{(?P<path>[^}]*)\}")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def tests_text(log_text, filter_name="Hawkeye"):
    """(text, passed, failed, skipped) from a run-tests log."""
    lines = []
    passed = failed = skipped = 0
    for raw in log_text.splitlines():
        match = DONE.search(raw)
        if not match:
            continue
        result = match.group("result")
        path = match.group("path")
        if result in ("Success", "Passed"):
            passed += 1
            lines.append(f"PASS  {path}")
        elif result in ("Fail", "Failed"):
            failed += 1
            lines.append(f"FAIL  {path}")
        else:
            skipped += 1
            lines.append(f"SKIP  {path}")
    lines.append("")
    lines.append(f"Hawkeye tests ({filter_name}): {passed} passed, {failed} failed, {skipped} skipped")
    return "\n".join(lines) + "\n", passed, failed, skipped


def clean_text(raw):
    out = []
    blank = 0
    for line in ANSI.sub("", raw).splitlines():
        line = line.rstrip()
        blank = blank + 1 if not line else 0
        if blank <= 1:
            out.append(line)
    return "\n".join(out).strip("\n") + "\n"


def still_clip(source, target, seconds=8, fps=60):
    """A zoompan clip of one picture: scaled to 1080 tall, pushed in on its top, then drifting down."""
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("ffmpeg not found")
        return 1
    frames = int(seconds * fps)
    # Work at 2160x2160 so the push does not soften; the picture is centred on a 1920x1080 frame.
    comma = chr(92) + ","
    zoom = "1.0+0.3*min(on/{0}{1}1)".format(frames, comma)
    pan_y = "(ih-ih/zoom)*min(on/{0}{1}1)*0.5".format(frames, comma)
    filters = (
        "scale=-1:2160:flags=lanczos,pad=3840:2160:(ow-iw)/2:0:color=0x0b0d12,"
        f"zoompan=z='{zoom}':x='(iw-iw/zoom)/2':y='{pan_y}':d={frames}:s=1920x1080:fps={fps},"
        "format=yuv420p"
    )
    target.parent.mkdir(parents=True, exist_ok=True)
    args = [ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-i", str(source), "-vf", filters, "-frames:v", str(frames),
            "-c:v", "libx264", "-crf", "14", "-pix_fmt", "yuv420p", str(target)]
    subprocess.run(args, check=True)
    print(f"wrote {target}: {seconds} s")
    return 0


def city_text(raw):
    """The generator's and the verifier's own lines from a captured Tools/generate-city.ps1 -Verify console.

    The console wrapped long lines at 120 columns; a line that does not start a log line continues the one before it.
    The material and asset "exists" lines and the per-spot crime info are dropped as noise; every PASS, FAIL and count stays.
    """
    joined = []
    for line in ANSI.sub("", raw).replace(chr(0xFEFF), "").splitlines():
        if joined and len(joined[-1]) >= 115 and not line.startswith(("Log", "City generation", "Running ")):
            joined[-1] += line
        else:
            joined.append(line)
    out = []
    for line in joined:
        if "[Hawkeye]" not in line and not line.startswith("City generation"):
            continue
        text = line.split("[Hawkeye]", 1)[1].strip() if "[Hawkeye]" in line else line
        if text.startswith("exists") or text.startswith("info  City_CrimeSpot") or " escape " in text:
            continue
        out.append(text)
    return chr(10).join(out) + chr(10)


LAP_SKIP = ("Hitch:", "Perf [", "placed at", "Camera audit")


def lap_text(raw):
    """The game's own subsystem lines (LogHawkeye: <Name>Subsystem_n: ...) between a lap's start and its result, timestamps cut, and the result line."""
    out = []
    started = False
    for line in raw.splitlines():
        if "Test Started." in line:
            started = True
            continue
        if not started:
            continue
        done = DONE.search(line)
        if done:
            out.append(f"Test Completed. Result={{{done.group('result')}}}  {done.group('path')}")
            break
        if "LogHawkeye: " not in line or any(skip in line for skip in LAP_SKIP):
            continue
        # The game's systems talking (the chase, missions, saves, the phone), not the load's asset chatter.
        if not re.search(r"LogHawkeye: \w*Subsystem_\d+:", line):
            continue
        message = line.split("LogHawkeye: ", 1)[1].strip()
        stamp = re.match(r"\[[0-9.]+-([0-9.]+):([0-9]+)\]", line)
        out.append(f"{stamp.group(1)} {message}" if stamp else message)
    return chr(10).join(out) + chr(10)


def main(argv):
    if len(argv) == 3 and argv[0] == "lap":
        text = lap_text(Path(argv[1]).read_text(encoding="utf-8", errors="replace"))
        Path(argv[2]).parent.mkdir(parents=True, exist_ok=True)
        Path(argv[2]).write_text(text, encoding="utf-8")
        print(f"wrote {argv[2]}: {len(text.splitlines())} lines")
        return 0
    if len(argv) == 3 and argv[0] == "city":
        text = city_text(Path(argv[1]).read_text(encoding="utf-8", errors="replace"))
        Path(argv[2]).parent.mkdir(parents=True, exist_ok=True)
        Path(argv[2]).write_text(text, encoding="utf-8")
        print(f"wrote {argv[2]}: {len(text.splitlines())} lines")
        return 0
    if len(argv) == 3 and argv[0] == "still":
        return still_clip(Path(argv[1]), Path(argv[2]))
    if len(argv) != 3 or argv[0] not in ("tests", "text"):
        print(__doc__)
        return 2
    mode, source, target = argv[0], Path(argv[1]), Path(argv[2])
    text_in = source.read_text(encoding="utf-8", errors="replace")
    if mode == "tests":
        text, passed, failed, skipped = tests_text(text_in)
        print(f"{passed} passed, {failed} failed, {skipped} skipped")
    else:
        text = clean_text(text_in)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    print(f"wrote {target}: {len(text.splitlines())} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
