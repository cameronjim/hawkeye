# Melee feel audit, 2026-11-11

Cameron, after playing: "fighting isn't smooth - punches don't land properly, ragdoll physics are off". No other
specifics, and no playtest capture from the session. The logic tests were green; this turns the complaint into
measured defects before anything is changed, then measures again after the fixes.

## How it was measured
`Hawkeye.Audit.MeleeFeel` (`Source/Hawkeye/Tests/MeleeFeelAuditTest.cpp`), a standalone `-game` run on the district
street by the PlayerStart (a flat stretch, no kerb), AI off, a fresh fists thug per trial:

- Kate's light and heavy from 60, 100, 150, 200 and 250 cm, at 0, 30 and 60 degrees off her facing (30 trials), her
  three-light chain at 100 cm, the thug's fists on her from 80 to 200 cm and at 45 degrees.
- At the frame the damage lands (sampled after every actor ticked): the nearest of the striker's fists and feet to the
  struck body's surface. "Driver" is the animated mannequins against the physics asset (what the game's sweep uses);
  "seen" is the bodies people look at (Sparrow on Kate, the built thug on the thugs: their bone segments less a flesh
  radius, so about +-5 cm). Then, frame by frame after it: the hit-stop, the reaction clip and its side, the physical
  reaction, the hit flash and the sound. Also how far each seen body's bones sit off its driver's.
- Five knockouts (a light, the heavy, the finisher, an arrow, the heavy off a 150 cm ledge), three times each after the
  fixes: the throw, the pelvis's speed before and after, how far and how fast the body went, joint stretch, depth below the
  floor, the seen body against the driver, motion after 1.5 s, and the settle. Frames in `Saved/Screenshots/MeleeAudit/`.
- A wall between Kate and a thug 220 cm off; a thug on the street 240 cm out with Kate on a 150 cm block; her path
  frame by frame through a light from 200 cm and through a buffered chain; her clips' blend times.
- Numbers JSON: `Saved/Automation/melee_feel_audit.json`. Input latency and the camera kick were read from the code.
- `-MeleeAuditOnly=thug_fists,ko_heavy` runs a subset. It fails a run where a landed blow's limb is over 25 cm off the
  body or a seen body comes over 30 cm off its driver.

## Findings, before and after

| # | What | Before | After | Good looks like | Verdict |
|---|------|--------|-------|-----------------|---------|
| 1 | Kate's light: fist to his body at the damage frame, 60 to 250 cm, 0 to 60 deg | driver 0 to 16 cm, seen 1 to 13; lands at 0.25 s everywhere; facing 1 to 2 deg | same: 15 of 15 land, driver 0 to 16, seen -10 to 15, 0.25 to 0.26 s | under ~20 cm, facing him | OK before: the contact warp works |
| 2 | Kate's heavy (roundhouse), same grid | driver 0 to 13, seen 0 to 13; 0.39 to 0.40 s | same, 15 of 15 | as above | OK |
| 3 | Her chain at 100 cm | light2 8 cm (facing 7 deg), light3 15 | light2 8 to 12, light3 15 | as above | OK |
| 4 | Thug's fists on Kate, 80 to 160 cm and 45 deg | driver 14 to 16, 0.64 to 0.66 s; 200 cm whiffs (past his 110 cm slide) | same | lands unless she moves | OK |
| 5 | Magnetism through a wall | picked the thug behind a 30 cm wall, moved 65 cm into it, and the forgiveness landed with her fist 87 cm off him | not picked, moved 2 cm, no hit | no pick, no pull, no forgiveness | **Fixed** |
| 6 | Magnetism over a ledge | lunged off a 150 cm block to a thug on the street and hit him | not picked, moved 5 cm, stays on the block | no pull over a drop | **Fixed** |
| 7 | The thug's physical reaction | the kick refused on every blow ("has to have 'CollisionEnabled' ... to AddImpulse", 76 lines in the run); his bodies only blended against the clip | 0 warnings; the struck body is given its kick, reaction from frame 6 to 12 (after the stop) | the struck part snaps with the blow | **Fixed** |
| 8 | Which of his bodies a blow moves | nearest the fist: hand_l, lowerarm_l, spine_02, thigh_l (his guarding hand for a jab at the face) | the region aimed at: head for a jab, spine_03 for the roundhouse | the region aimed at | **Fixed** |
| 9 | Kate after his punches | her look has no pelvis body, so its root body is a spine body inside the reaction's chain: the reaction cancelled on its first tick, left her chest simulating, and the look component followed that body away. Her look's head 108, 184, 261 cm off her after 1, 2, 3 punches; in `kate_after_his_punches.png` only her bow is left on the street | look 0.0 cm off her driver after every punch; she is in the picture. Her physical reaction is now off: with the drift fixed it ran, and crashed the game now and then inside the engine's physics blend on her look ("index 29 from an array of size 0": one lap in thirteen, three audit runs); on her driver it once stalled the game. Her hit clip (frame 0, side right) and lean remain | she reacts and stays herself | **Fixed** (likely the "ragdoll physics are off"); the physical flinch is not there |
| 10 | Hit-stop | light 35 to 46 ms, heavy 72 to 77, at frame 0; back-to-back stops unguarded | light 61 to 69 ms, heavy 90 to 98 (frame rounding at dilation 0.1), frame 0; a second blow within 0.15 s gets none | 50 to 90 ms by weight, never stacking | **Fixed** |
| 11 | Reaction clip, flash, sound | all on frame 0; his clip side Front for straight punches, Right for her hook; hers Front | same | within 1 to 2 frames, side matches | OK |
| 12 | Physical reaction timing | frame 4 (light), 9 (heavy): it waits out the stop (on thugs only, and with no kick) | thugs: frame 6 to 8 (light), 11 to 12 (heavy), with the kick; Kate: none (9) | after the stop | OK |
| 13 | Knockout throw | light: bone 302 / body 127 cm/s; heavy 520 / 219; arrow 164 / 63; finisher 900 + 450 up on every body | second pass: bone 260 at weight 1 (cap 350), body 200 + 40 up (cap 300); the finisher at spine_01, 0.6 of the bone throw and 0.25 of the lift | capped, along the blow | **Fixed** |
| 14 | Velocity at the knockout | first sim frame 443 to 747 cm/s for strikes (1,271 finisher), an arrow 79: the killing blow starts the hit-stop, and at its 0.1 dilation the solver's first joint corrections came out at 4 to 9 m/s | the ragdoll waits asleep until the stop is over, then takes its throw: peak 271 to 305 cm/s (light, heavy), finisher 495 to 532 (released in its 0.25 slow motion), arrow 204 to 229 | the throw, no pop | **Fixed** (second pass) |
| 15 | Finisher's travel | 790 and 829 cm, over head first, legs high | 112 to 113 cm, down on his back | a man falls 1 to 2 m | **Fixed** |
| 16 | Travel (pelvis from his feet) | light 81 to 105 cm, heavy 87 to 269, arrow 44, ledge 146 to 237 | light 108 to 116, heavy 138 to 143, arrow 51 to 60, ledge 150 to 161 (with the 150 cm drop) | 1 to 2 m | **Fixed** |
| 17 | Still moving late | 1.5 s in: 14 to 290 cm/s; slid 35 to 138 cm on the ground; settled 2.0 to 3.9 s | 1.5 s in: 0 to 18 cm/s; slid 18 to 33 cm; settled 1.06 to 1.86 s | still by ~2 s | **Fixed** (second pass) |
| 18 | Stretch, tunnelling, pop | stretch 1 to 2 cm; 0.1 cm under the floor; seen body 1.7 to 2.0 cm off the driver's pelvis | stretch under 2 cm; 0 cm under the floor; seen body 1.7 to 2.0 cm | none | OK |
| 19 | Ragdoll mass | SK_Mannequin_PhysicsAsset (a feature-pack asset, no script builds it): 19 bodies of 1 kg, 19 kg in all; depenetration unlimited | 78 kg set on the bodies as a reaction or ragdoll starts (`RagdollMassKg`, the heavy 100), pelvis 8.6 kg, hands 2 kg (by `BodyMassShares`); depenetration capped at 120 cm/s. The anthropometric spread (pelvis 20 hands) made the joints jitter and stood a body up a metre in the air: the spread is held to about 4.5 to 1 | believable mass by part | **Fixed** (second pass) |
| 20 | Input to swing | the light starts on the button's release (a 0.4 s hold is the heavy): a tap adds its own length (typically 70 to 120 ms); then 0.25 s to the hit | unchanged | under ~50 ms to the first frame | **Design**: left for Cameron |
| 21 | Buffering | a light in a chain's recovery goes 0.08 s later; a light pressed in the heavy's recovery was dropped | the chain's the same (0.08 s); one in the last 0.3 s of any recovery goes as it ends | buffered | **Fixed** |
| 22 | Slide and snap | light from 200 cm: top speed 1,389 cm/s, worst one-frame speed change 404 to 463 cm/s | top 1,389 cm/s, worst change 274; closing capped at 1,100 cm/s on average (thugs 550) | no glide | **Partly**: the cap is data, the clip's own root curve still peaks |
| 23 | Clip blends | Light1-3 and Heavy: in 0.10 s, out 0.20 s | unchanged | 0.05 to 0.15 in | OK |

The short version: punches did reach the body, and on time. What made them read as not landing is that nothing
happened when they did: the thug's physical kick was thrown away on every blow (7) and aimed at his hand (8), Kate's look
slid off her a metre per punch she took and was gone after three (9), and her light's hit-stop was two frames (10). The
ragdolls were not exploding or tunnelling; the finisher threw them 8 m (15) and they rolled and slid on (17). Magnetism
went through walls and off ledges (5, 6).

## Fixes (priority order from the brief)
- **A. Magnetism.** Who a strike goes at is a rule (`UHawkeyeMeleeRules::SelectStrikeTarget`): the stick's cone, then the
  camera's, then the nearest within 150 cm. Every pull (Kate's warp and lunge, a thug's slide) is checked for a sight line
  and a walkable floor (`UMeleeComponent::IsStrikePathClear`, `IsSlideFloorWalkable`) and capped at a closing speed
  (`FHawkeyeStrikeMagnetism` on each melee component: Kate 1,100 cm/s, thugs 550). The forgiveness never goes through a wall.
  Motion warping (the engine plugin, already on Kate from the Game Animation Sample) still does the moving.
- **B. Hit confirmation.** Hit-stop `Lerp(0.05, 0.09, weight)`, none for a blow within 0.15 s of the last stop. The kick is
  set on the struck body directly; the struck body is the region aimed at; a reaction never simulates a body with no body
  above it (what carried Kate's look away). Kate's and Clint's physical reaction is then switched off
  (`bPhysicalReaction`) because running it crashed the engine now and then.
- **C. Ragdoll.** The finisher throws through the knockout rule, low on the back; bodies damp to 4 / 8 once down (never
  mid-air); settle after 0.3 s still, 3 s at most. Second pass: a thug's ragdoll weighs 78 kg (the heavy 100) spread by
  body part; the struck body falls back to the first bone above it with a body (the mannequin has none on spine_03); the
  ragdoll is held asleep until the hit-stop is over, then thrown, with the capsule's motion and not the bodies' own; the
  throw at the bone 260 cm/s and the lift 40 (were 520 and 90: tuned against the held release and the masses);
  depenetration capped at 120 cm/s.
- **D. Smoothness.** A light pressed late in any recovery is kept.
- Also: `Hawkeye.Screenshot.Bodies` stands its subjects on level ground (it straddled the kerb).

## Seen in the frames (read by eye, second pass)
kate_after_his_punches.png: before the fix only her bow on the street; after, Kate standing, Lucky and the thug beside
her. ko_heavy_00: folding back off his feet at hip height; ko_heavy_04: on his back about 1.4 m off. ko_finisher_00
and _01: flat on his back, about 1.1 m off, no flip. ko_light_01: buckling forward and down. ko_ledge_02: dropping
off the block's edge; settled on the street below. ko_arrow_03: lying where he fell. The first frames before the
masses and the held release (ko_heavy_02 in that run) showed him upright a metre in the air. The audit no longer slows
the world for a capture: its crawl (0.02) disturbed the ragdolls it caught the same way the hit-stop did. Frame labels
read 0.6 s for the first two: the audit starts capturing once it has watched the blow 0.6 s.

## SocialClub pacing (second pass)
The lap's player throws every third strike it makes as a heavy (`Strikes % 3`), and Tomas is winded after every third
swing. The two lock: on some runs the heavy lands as nearly every winded window opens (heavy and finisher, about twice a
window's damage), on others a light does and only the finisher lands. Per run, counted from the log: windows that opened on
a full heavy against the fight's length:

| Build | Runs (s of fighting, full-heavy windows) |
|---|---|
| main (5) | 95.0 (5 of 10), 106.8 (3 of 12), 90.9 (5 of 8), 105.7 (3 of 11), 96.2 (4 of 10) |
| branch (3) | 85.5 (7 of 9), 95.9 (3 of 10), 86.7 (6 of 9) |
| branch, light buffer off (3) | 95.6 (4 of 11), 95.9 (3 of 11), 87.6 (6 of 9) |
| branch, old hit-stop (4) | 115.6 (2 of 11), 103.3 (2 of 12), 86.6 (5 of 9), one hung (below) |

Both builds have both modes; main's lowest was 90.9 s, on the floor. No single change of this pass brings the branch back
(the buffer and the old hit-stop each still gave an 86 to 88 s run); the per-cycle timing shifts by a few hundredths of a
second decide which mode a run falls into, and the parries (2 to 3 in short runs, 4 to 8 in long ones) shift it most. It
is not a bug in the game; the lap's player plays better when it locks. Tomas's pacing is restored in his data: his
winded damage 1.5 to 1.25 (Tools/Editor/create_boss.py, DA_Boss_CH04_Tomas). Five runs after: 112.2, 104.6, 107.1,
104.6, 106.5 s. Santa's fight (no pacing check in the grotto laps): 57.5 s on main, 54.5 and 54.7 s on the branch.
One old-hit-stop run hung: the haymaker knocked Kate 200 cm over the roof's edge onto a ledge and the lap's player
shimmied there until the lap timed out. The player now climbs back (jump) and lets go (crouch) after a second.
## Gate (2026-11-12, second pass): green, merged
Both builds clean; 598 headless tests; 225 Python tests, ruff clean; `create-content.ps1` twice, nothing to commit after
either; city verify, `verify_content` PASS, traversal audit exit 0. Laps, each its own process, 13 of 13 first try, no
engine crash: Campaign, ChaseFallBehind, ReindeerGames, Grotto, SeenGrotto, SocialClub (114.8 s of fighting),
EastVillage, InteriorStealth, InteriorLoud, CrimeAmbush, CrimeMugging, CrimeRobbery, CrimeRooftop. Santa's fight in the
grotto laps: 58.2 and 50.6 s.
## Left for a human
- Input on release (20): the light could start on the press with the heavy as a hold that cancels its wind-up, as in
  most brawlers, but that changes the control scheme; Cameron's call.
- The warp's peak speed (22): the clip's root motion moves late, so the warp's speed peaks near 1,400 cm/s even under
  the average cap; a dash-in or re-authored root curves would smooth it. Does it look like sliding with a pad?
- Ragdoll mass (19) is spread by body part but compressed to about 4.5 to 1 so the joints hold; whether that reads as weight is a judgement with a pad.
