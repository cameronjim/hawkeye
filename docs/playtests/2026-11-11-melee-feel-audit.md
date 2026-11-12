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
| 9 | Kate after his punches | her look has no pelvis body, so its root body is a spine body inside the reaction's chain: the reaction cancelled on its first tick, left her chest simulating, and the look component followed that body away. Her look's head 108, 184, 261 cm off her after 1, 2, 3 punches; in `kate_after_his_punches.png` only her bow is left on the street | head 11.6 cm off at most (Sparrow against the mannequin, as standing); a physical flinch each punch (frame 8); she is in the picture | she flinches and stays herself | **Fixed** (likely the "ragdoll physics are off") |
| 10 | Hit-stop | light 35 to 46 ms, heavy 72 to 77, at frame 0; back-to-back stops unguarded | light 61 to 69 ms, heavy 90 to 98 (frame rounding at dilation 0.1), frame 0; a second blow within 0.15 s gets none | 50 to 90 ms by weight, never stacking | **Fixed** |
| 11 | Reaction clip, flash, sound | all on frame 0; his clip side Front for straight punches, Right for her hook; hers Front | same | within 1 to 2 frames, side matches | OK |
| 12 | Physical reaction timing | frame 4 (light), 9 (heavy): it waits out the stop (on thugs only, and with no kick) | frame 6 to 8 (light), 11 to 12 (heavy), on both | after the stop | OK |
| 13 | Knockout throw | light: bone 302 / body 127 cm/s; heavy 520 / 219; arrow 164 / 63; finisher 900 + 450 up on every body | the same, the finisher now 520 / 219 | capped, along the blow | **Fixed** (finisher) |
| 14 | Velocity from the animation | pelvis 1 to 2 cm/s standing, 79 in the finisher's clip; first sim frame 443 to 747 cm/s (1,271 finisher) | first sim frame 446 to 706 (534 to 621 finisher) | the body keeps its motion | OK |
| 15 | Finisher's travel | 790 and 829 cm | 127 to 161 cm | a man falls 1 to 2 m | **Fixed** |
| 16 | Travel (pelvis from his feet) | light 81 to 105 cm, heavy 87 to 269, arrow 44, ledge 146 to 237 | light 74 to 143, heavy 69 to 150, arrow 43 to 50, ledge 165 | 1 to 2 m | **Fixed** (heavy no longer 2.7 m) |
| 17 | Still moving late | 1.5 s in: 14 to 290 cm/s; slid 35 to 138 cm on the ground; settled 2.0 to 3.9 s | 1.5 s in: 28 to 276 cm/s; slid 6 to 64 cm; settled 1.96 to 3.0 s (median about 2.3) | still by ~2 s | **Better, not done**: most settle by 2.3 s, a heavy can roll to the 3 s cap |
| 18 | Stretch, tunnelling, pop | stretch 1 to 2 cm; 0.1 cm under the floor; seen body 1.7 to 2.0 cm off the driver's pelvis | stretch under 2 cm; 0 to 0.6 cm under the street, one ledge fall 13 cm under for a frame on landing; seen body 1.7 to 2.0 cm | none | OK; the ledge landing's one-frame dip is noted |
| 19 | Ragdoll setup | SK_Mannequin_PhysicsAsset, 19 bodies of 1 kg (19 kg), damping 0.01 (0.6 / 1.5 falling), no depenetration overrides; world: no substepping, 8 position / 2 velocity iterations; Kate walks through bodies | falling 0.6 / 1.5, then 4 / 8 once down; settle 0.3 s still, 3 s at most | | equal masses noted, not changed |
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
  above it (Kate's look).
- **C. Ragdoll.** The finisher throws through the knockout rule; bodies damp to 4 / 8 once down (never mid-air); settle
  after 0.3 s still, 3 s at most.
- **D. Smoothness.** A light pressed late in any recovery is kept.
- Also: `Hawkeye.Screenshot.Bodies` stands its subjects on level ground (it straddled the kerb).

## Seen in the frames (read by eye)
`kate_after_his_punches.png`: before the fix only her bow on the street; after, Kate standing, Lucky and the thug beside
her. `ko_heavy_*`: falls back with his arms up, lies on his back about 1.5 m off. `ko_finisher_02`: thrown over backward,
legs high (a flip; the throw's bone kick at the chest still turns him over). `ko_ledge_03`: on the street below the block.
`ko_light_04`, `ko_arrow_04`: lying where they fell. The frame labels say 0.6 s for the first two: the audit only starts
capturing once it has watched the blow 0.6 s; the measurements are per frame and unaffected.

## Left for a human
- Input on release (20): the light could start on the press with the heavy as a hold that cancels its wind-up, as in
  most brawlers, but that changes the control scheme; Cameron's call.
- The warp's peak speed (22): the clip's root motion moves late, so the warp's speed peaks near 1,400 cm/s even under
  the average cap; a dash-in or re-authored root curves would smooth it. Does it look like sliding with a pad?
- The finisher's flip (ko_finisher_02) and a heavy rolling up to the 3 s cap (17).
- Equal 1 kg masses (19): a heavier torso would make bodies drop with more weight; not changed.
