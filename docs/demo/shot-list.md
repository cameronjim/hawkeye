# Demo video: shot list

"Hawkeye: Kate Bishop". 80 seconds, 1920x1080, 60 fps, no voiceover, a caption on every shot. The hard cuts
are in `docs/demo/edl.json`, which `Tools/Demo/cut.py` reads; this file says why each shot is there. Keep the two in
step: the length check in the cutter fails outside 75 to 90 seconds.

The audience is a gameplay or tools programmer deciding whether to talk to me. Each shot has to prove one thing
they can check in the repo.

## How it is made

- **Scripted** shots are played by the lap bots and captured as frame sequences by `Hawkeye.Demo.*`
  (`Source/Hawkeye/Tests/DemoCaptureTest.cpp`, run by `Tools/demo-capture.ps1`). Same run, same frames.
  Each writes `Saved/Demo/<shot>/shot.json` with the frame number of every zip, landing, mantle and ring, so
  the `start` seconds below can be set from facts, not guesses.
- **Terminal** shots (`type: terminal` in the list) scroll the real output of a real run as text: `Tools/Demo/make_terminal.py`
  turns the log of `Tools
un-tests.ps1` (`tests`), the console of `generate-city.ps1 -Verify` (`city`) and a lap's log
  (`lap`) into the text file, nothing typed by hand, and the counts on the captions are read off those files.
- The **Traversal Debugger** shot is a still of the tab itself, drawn off screen by `Hawkeye.Editor.TraversalDebugger.Shot`
  (the editor's widget renderer, with the audit's real rows; the first time the tab has been seen) and pushed in on by
  `make_terminal.py still`.
- **Screen** shots (`Tools/demo-capture.ps1 -Screen <name>`, a gdigrab desktop recording) are no longer used by the list.
- **Owner** shots are recorded by hand (`docs/demo/recording-guide.md`) into `Saved/Demo/owner/`. Until they
  exist, `cut.py --placeholder` puts a labelled black slate in each slot so a rough cut can be checked.
- Text rules: no "Insomniac" in a title. The first card says solo, work in progress, C++ and Unreal Engine 5, and
  that it is a non-commercial fan project not affiliated with Marvel, Disney or Insomniac Games. The last card
  repeats the README's account of how it is built: Claude Code as a pair, I set the design and the story and decide
  what changes, the agent writes much of the code.

## The cut

| # | Shot id | Start | Len | Caption | Source | What it proves |
|---|---------|-------|-----|---------|--------|----------------|
| 1 | `title` | 0 | 5 | Hawkeye: Kate Bishop. Solo project. Work in progress. C++ and Unreal Engine 5. Fan-project line. | card | Scope and honesty up front: one person, unfinished, C++. |
| 2 | `establish` | 5 | 6 | A block of the East Village, built from OpenStreetMap data | scripted: `establish` | The look: a real block at night, snowing. Sets up the city pipeline. |
| 3 | `city_generation` | 11 | 5 | City generation: footprints and heights in, buildings, ledges and grapple anchors out | terminal: `city_generation` (the real console of `generate-city.ps1 -Verify`) | The city is a compiler, not hand placement: same input, same city. Tools and pipeline skill. |
| 4 | `zip_one` | 16 | 5 | Grapple traversal: real movement, you keep your speed into the zip | scripted: `grapple_run`, from 0 s | The zip is a custom movement mode with real velocity, not an animation on rails. |
| 5 | `zip_two` | 21 | 5 | The body leans into the pull and brakes on arrival | scripted: `grapple_run`, from 6 s | Procedural body pose on top of the movement: lean, braking arrival class. |
| 6 | `mantle_landing` | 26 | 4 | Mantles fitted to the measured ledge, and a heavy landing | scripted: `grapple_run`, from 12 s | Parkour that fits the wall that was measured; landing classes by fall speed. |
| 7 | `descent` | 30 | 5 | Fire escape descent: hang, drop, catch | scripted: `descent` | Traversal states chain: hang, drop, catch, land, all driven through real input. |
| 8 | `traversal_debugger` | 35 | 6 | Traversal Debugger: an editor tab that checks every grapple anchor and says why one fails | still: `traversal_debugger` (the tab, real audit rows) | Editor tooling: a Slate tab and a commandlet gate on the same reach code the game uses. |
| 9 | `lucky` | 41 | 5 | Lucky follows, trots and sits. The rules are tested pure functions | scripted: `lucky`, from 1 s | AI behaviour as pure rules with tests; a small companion that reads as alive. |
| 10 | `street_fight` | 46 | 8 | Combat: attack tokens decide who swings, so there are no stunlocks | owner: `street_fight.mp4` | Combat design that follows Insomniac's group fights: tokens, telegraphs, readable hits. |
| 11 | `stealth_takedown` | 54 | 6 | Stealth takedown | owner: `stealth_takedown.mp4` | A second play style. Stealth uses the same AI states. |
| 12 | `tomas_phase` | 60 | 8 | Boss fight: the Tomas phase change | owner: `tomas_phase.mp4` | Boss phases from data assets: thresholds, a protected transition, new rules for the arena. |
| 13 | `tests_run` | 68 | 5 | 655 automation tests, run headless | terminal: `tests_run` (the real log of `Tools\run-tests.ps1`) | Engineering discipline: rules have tests; the build is gated. |
| 14 | `lap_bot` | 73 | 4 | Lap bots play the chapter from the menu | terminal: `lap_bot` (the log of a real `Hawkeye.Lap.Campaign` run) | The game is played end to end by a bot on every change. |
| 15 | `end` | 77 | 4 | Hawkeye: Kate Bishop. How it is built (README wording). | card | Disclosure that matches the README. |

Total: 80.1 s. The suite count reads 655: 650 rules and content tests plus the four `Hawkeye.Demo.*` shots and the
`Hawkeye.Editor.TraversalDebugger.Shot` picture, which register and skip their bodies without `-HawkeyeDemo` (60 tests
in all skip their body headless; the caption says so). The earlier "654" is the same 650 plus the four demo shots,
counted before the tab's picture existed.

## Scripted capture windows

Frame sequences are 20 s at most for the grapple run (1200 frames), 15 s for the descent and Lucky (900), 12 s for
the establishing pan (720). The `start` values in the list are set from `shot.json` (zip 1 at frame 194, zip 2 at 344, zip 3 at 519, the vault at 565, the heavy landing at 631; Lucky stops at 300 and sits at 497).

| Capture | Plays | Look for in `shot.json` events |
|---------|-------|--------------------------------|
| `grapple_run` | `traversal_1` from its pedestal to the first fire-escape descent: runs, mantles, vaults, zips | `zip_start`, `zip_end`, `parkour_*`, `land_heavy_*`, `ring_*` |
| `descent` | The fire-escape leg: stand, crouch off the edge, hang, drop, catch, land | `parkour_*`, `land*` |
| `lucky` | Kate jogs 7 s down the street from the PlayerStart with Lucky after her, then stands | `kate_stops`, `lucky_*` |
| `establish` | Night pan round the block from 30 m up. Kate is hidden. Camera on a fixed path | none |

## What each capture hides

HUD off (the capture asks for no widgets), thugs not thinking, Clint frozen. The rings of the traversal challenge
are not shown because the challenge is never started: the bot drives its route as data. The snow is the game's own.

## Cut rules

- Hard cuts. Cards fade 0.4 s.
- Nothing longer than 8 s. A shot that needs longer is two shots.
- Captions are lower thirds, plain sentences, no jokes.
- Frame sequences go in at 60 fps untouched. Screen recordings and owner clips are scaled to fit and padded.
