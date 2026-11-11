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
- **Combat difficulty pass (your note on multiple attackers).** Up to two melee thugs
  attack at once on Normal and three on Hard, sized to the group (two thugs share one
  token). Two at once must come from different sides (60 degrees apart on Normal), blows
  land at least 0.45 s apart, and nothing lands for 0.6 s after a hit. An attack from off
  screen shows a "!" on a ring around Kate. Waiting thugs shift and feint. Santa has 560
  health and lasts about 42 s (was 25). 576 tests pass; all 13 laps passed first try after
  one lap-bot fix. Tomas's crew now takes 28 s (was 10) and his phase 105 s (was 87).
  Rough: the lap bot still wins easily, so two-at-once lasts only 1 to 1.6 s per fight in
  the laps, and small fights (two melee thugs or fewer) cost it nothing. **Feel check:**
  fight four thugs on Normal and Hard. Fair, or a mess? Is the "!" ring noticeable?
- **Traversal Debugger (editor tool).** A second, editor-only module `HawkeyeEditor` adds
  a tab at Tools > Hawkeye > Traversal Debugger and a headless audit
  (`Tools\traversal-audit.ps1`) that answers "which roofs can be grappled from where"
  using the same reach code the game uses. The audit and the in-game test agree on every
  row (2,457 anchors, 24,420 reachable pairs). 570 tests pass; all 13 laps passed first
  try. The packaged game target was broken on main (IKRig linked for the editor only) and
  is fixed. **Nobody has seen the tab on screen**: open it and check the viewport drawing,
  toggles and buttons. The audit exits 1 because 14 street-facing roofs have no anchor
  reachable from the sidewalk; that is either a generator fix or an allow list, your call.
- **The hero block is dressed for December.** Avenue B to C, E 9th to 10th: ten strings of
  coloured bulbs across the street, 22 lit wreaths, lit windows and candles, snow on
  parapets, kerbs and cars, a tree stand with six trees. 1,012 planned records drawn as 25
  instanced groups; frame time did not move (8.0 ms). All 13 laps passed first try, so
  nothing blocks a route. Shots in `Saved/Screenshots/Dressing/`. Rough: the strings are
  the only part that reads from a distance, wreaths are small rings, snow banks are dark
  lumps outside the lamp pools, and it glows by day too (day was not looked at).
- **No more hardcoded paths.** Scripts find the engine and the sample through
  `Tools\hawkeye.env.ps1` (argument, `HAWKEYE_UE_ROOT`, `Tools\local.json`, registry,
  launcher). A wrong path is an error with a fix line, not a silent fallback.
- **Chapter 3 is The Grotto.** Act 1 now runs 1 → 2 → 3 → 4. Find the department store's
  grotto 208 m out, get through four elves unseen (being seen starts a fight, it does not
  fail you), open the workshop with the keypad, beat the mall Santa (a two-phase boss made
  of data only, no new boss code), leave by the roof. The boomerang arrow unlocks here:
  it hits up to two targets and comes back, and shooting a wall switch box pulls a guard
  away. After the end card the farm calls (Lila's line), and an ornament by the safehouse
  sets a save flag. Reactive lines more than 3 s late are now dropped, and subtitles wrap
  clear of the prompt. 553 tests pass; all 13 laps passed first try; the unseen lap takes
  149 s in game with zero alerts. Rough: all greybox, the sign has no lettering, the roof
  stairs are a door, catching the boomerang has no input or animation, Santa goes down in
  25 s, the "[E] Leave" prompt floats over the first room, and at the door the corner text
  already shows the next objective. **Feel check:** is sneaking past the elves a puzzle,
  or do you just walk behind them?
- **Chapter 4 is The Social Club.** Tomas's fight runs on a boss framework where a boss is
  data (`UBossDefinition`), so the next boss is new assets. Three phases on a roof picked
  from the district data: his four guys while he watches, then Tomas (three-swing combo,
  then winded for 3 s), then the inflatable Santa sweeps the roof under 40% health and
  Kate gets confetti arrows. Reach it from the safehouse chapter select or
  `hawkeye.StartChapter DA_CH04_SocialClub`. 534 tests pass; the scripted lap wins in
  about 110 s. Rough: too easy (every jab parries into a finisher), Tomas is near black at
  night, stock bat and shield instead of candy canes, the Santa passes through props and
  never hits Tomas.
- **Tomas, second pass (boss feel).** A parry only staggers him; he can be finished only
  while winded. He guards outside that window (hits land at 20%), and a confetti arrow
  breaks the guard. His haymaker is unblockable with its own "!!" telegraph and dodge
  prompt. The Santa's sweep winds him too and shoves the presents; a putty arrow pins it
  for 4 s. Each phase change and the final blow is a short locked-camera beat (skippable),
  and the phase-ending finisher gets a slow-motion push-in. He has 680 health on Normal.
  538 tests pass; the lap fights for 94 s, winds him 10 times and dodges 8 haymakers.
  Rough: luring him into the Santa's lane worked in 1 of 4 runs, the defeat beat's camera
  is weak, his head glows orange on a telegraph, and two of your lines moved into beats
  ("knees" and "Good try, me"). **Feel check:** does he play like a boss or a thug?
- **Chapter 2 is Reindeer Games.** Run 200 m to the toy drive on Avenue B, fight five
  thugs in antlers and red noses, carry three toy sacks back to the table. The foam arrow
  (the putty arrow, renamed on screen) unlocks on arrival. The courier scene is on the
  way, unmarked and optional, and sets a save flag for later acts. Then the end card, the
  "Visiting hours" slides and the phone threads. Order is now 1 → 2 → 4. 544 tests pass.
  Rough: the fight is over in 14 s with no hits taken, the arrival queues 14 s of lines so
  the first-parry line lands late, foam unlocks on arrival rather than after the first
  parry, the sacks read as dark balls, and the courier reads as "something on the ground".
- **Rooftop flake fixed.** It was a game bug: the crime's archer escaped by zip to anchors
  on other roofs. Thugs now have a leash; five runs in a row passed.
- **Santa lure fixed.** The lane is sized from Tomas's reach; the lap asserts a catch.
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
