# Overnight brief, 2026-11-10

For Cameron. Written as the night went; newest state is at the top of each section. Every
item marked "on main" passed its gate: clean build, the full headless suite, the
standalone laps, and screenshots read by eye.

## Play this first
1. **The grapple.** Zip to a roof with the stick held: she should land running. Zip again
   and let go mid-line with jump: she should be thrown along her path, not dropped.
2. **A sprint vault.** Sprint at a low wall. She should come out the far side at speed.
3. **A mantle at two heights.** Waist-high and head-high. The body should fit the wall,
   not stretch.

4. **Night Shift.** New game from the menu. Chase the bike thief over the roofs, take the
   grapple at the gap (it snaps, she lands in the dumpster), climb out, corner him.

Open the editor before we talk so the Unreal MCP tools can connect.

## On main
- **Chapter 1 is Night Shift.** The rooftop chase after the bike thief replaces the old
  Rooftops chapter and its purple-arrow ending. The thief runs a 152 m route planned from
  the district data and rubber-bands to stay in sight; fall 45 m behind for 6 s and you
  restart from the checkpoint. The grapple at the gap snaps and drops her in the dumpster
  of wrapping paper. Clint is benched for Act 1. 525 tests pass; the scripted chase takes
  23.7 s. Rough: the thief's first leap is 19 m, he wears the tracksuit thug body, lines
  land late (one about 7 s after its vault), the opening roof is too dark, the "corner" is
  open pavement, and there is no Lucky yet. **Feel check:** is the chase a chase, or do
  you just follow him?
- **Movement component.** `UHawkeyeMovementComponent` runs the grapple zip and every
  parkour move as custom movement modes with real velocity. Zip entry keeps her speed and
  accelerates; arrival carries 600 cm/s; a jump release keeps the zip's velocity plus a
  boost. Vaults leave at entry speed. Vault and mantle play the sample's clips with motion
  warping to the measured ledge, replacing the stretched 1 m clip.
- **Attack tokens.** Thugs take turns. One melee and one ranged attacker at a time on
  Story and Normal, two on Hard, with a gap between attacks (0.9 / 0.6 / 0.4 s). The rest
  hold a ring around her, spread by angle. A swing from behind winds up for at least a
  second. `hawkeye.DebugTokens 1` shows who holds and who waits. Every scripted fight is
  still won; the loud interior lap takes 39.9 s, up from 32.4. **Feel check:** fight four
  thugs and say whether the turn-taking reads as fair or as thugs standing around.
- **Kate is a character now.** She appears as Paragon's Sparrow, tinted purple, in place
  of the grey mannequin. The mannequin still animates unseen and Sparrow copies its pose
  each frame, so every system kept working; the game's own bow is the one shown. 511 tests
  pass; idle frame time went from 4.7 to 5.0 ms. Without the Paragon files (a fresh clone)
  she is the mannequin as before. Still off: the lighter cloth reads lavender, the boots
  are brown, the holstered bow crosses her body, and hair and skirt are rigid. Thugs and
  Clint are still mannequins.
- **Thug StateTree.** `ST_Thug` is now the only thing that decides what a thug does.
  The duplicate C++ mode chooser is deleted; the fight's steps (HoldRing, Swing, BackOff,
  TakeCover, Peek, Burst, Draw) are tree states you can read in the StateTree editor.
  `hawkeye.DebugThugs 1` draws each thug's active state over his head. 508 tests pass and
  every scripted fight is won in the same time as before. One flake to watch: the rooftop
  crew crime failed once in a batch (Kate fell off the roof) and passed twice after; not
  yet known whether it predates tonight. The partner AI still uses the old pattern.
- **Generator split.** `generate_city.py` went from 5,356 to 4,143 lines; the reach model,
  fire escapes, ledges, district data and geometry helpers are plain-Python modules with
  unit tests and golden hashes. Regenerating the city in the editor rebuilt nothing.
- **Hosted CI.** `.github/workflows/ci.yml`: Python tests, map-data determinism, lint.
- **Story.** `docs/STORY.md` (yours, verbatim), `docs/chapters/ACT1.md` (chapter split),
  `docs/chapters/ACT1-script.md` (draft lines for you to edit).
- **Small.** The false Epic copyright line is gone from all source; project descriptions
  no longer say first-person shooter.

## Needs you
- Feel checks above.
- Edit or replace the draft lines in `ACT1-script.md`.
- The download list below. One "yes" covers what you want; logins and licence clicks are yours.
- Confirm the internship's enrolment rule and deadline.
- Read the new `README.md`. Its last section says plainly that the code is written with
  Claude Code under your direction. I think that is the right call for a hiring reader, who
  will see `CLAUDE.md` and the commit pace anyway; it is yours to reword or cut.

## Download list
Nothing was downloaded. Already on disk and usable with no download: the Paragon Sparrow
mesh and skins (`Content/ParagonSparrow`), and the sample's mannequins.

| Need | Candidate | Source | Licence | Size | Login |
|------|-----------|--------|---------|------|-------|
| Lucky | LowPoly Animated Animals (the dog; no sit clip) | quaternius.itch.io/lowpoly-animated-animals | CC0 | 6.6 MB | no |
| Thug and boss bodies | Universal Base Characters (Standard) | quaternius.itch.io/universal-base-characters | CC0 | 122 MB | no |
| Extra animations | Universal Animation Library 2 (Standard) | quaternius.itch.io/universal-animation-library-2 | CC0 | 17 MB | no |
| Hang, shimmy, roll, get-up clips | Mixamo (names to check by eye) | mixamo.com | Mixamo terms | small | Adobe |
| Brick | `red_bricks_02`, `painted_brick` | polyhaven.com | CC0 | ~100 MB | no |
| Pavement, road | `concrete_pavement_03`, `asphalt_snow` | polyhaven.com | CC0 | ~100 MB | no |
| Snow | Snow010A | ambientcg.com | CC0 | ~24 MB | no |
| Christmas props | Holiday Kit | kenney.nl/assets/holiday-kit | CC0 | small | no |
| Hits and foley | Impact Sounds, RPG Audio | kenney.nl | CC0 | small | no |
| Bow shot stand-in | Crossbow firing and hitting target (384919) | freesound.org | CC0 | small | likely |
| Carols | "Hark! The Herald Angels Sing" (U.S. Army Band), "Deck the Halls" (USAF Band) | commons.wikimedia.org | Public domain | ~4 MB | no |

Not found free: an inflatable Santa, wreaths, string lights, a bow mesh, a tracksuit
outfit, dog bark, winter street ambience. Those get built by script or stay synthesized.
Sizes marked ~ are estimates. Fab showed no licence text on the listings checked.
