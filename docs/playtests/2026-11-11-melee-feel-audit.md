# Melee feel audit, 2026-11-11

Cameron, after playing: "fighting isn't smooth - punches don't land properly, ragdoll physics are off". No other
specifics, and no playtest capture from the session. The logic tests were green; this turns the complaint into
measured defects before anything is changed.

## How it was measured
`Hawkeye.Audit.MeleeFeel` (`Source/Hawkeye/Tests/MeleeFeelAuditTest.cpp`), a standalone `-game` run on the district
street by the PlayerStart (a flat stretch, no kerb), AI off, a fresh fists thug per trial:

- Kate's light and heavy from 60, 100, 150, 200 and 250 cm, at 0, 30 and 60 degrees off her facing (30 trials), her
  three-light chain at 100 cm, the thug's fists on her from 80 to 200 cm and at 45 degrees.
- At the frame the damage lands (sampled after every actor ticked): the nearest of the striker's fists and feet to the
  struck body's surface. "Driver" is the animated mannequins against the physics asset (what the game's sweep uses);
  "seen" is the bodies people look at (Sparrow on Kate, the built thug on the thugs: their bone segments less a flesh
  radius, so about +-5 cm). Then, frame by frame after it: the hit-stop, the reaction clip and its side, the physical
  reaction, the hit flash and the sound.
- Five knockouts (a light, the heavy, the finisher, an arrow, the heavy off a 150 cm ledge): the throw, the pelvis's
  speed before and after, how far and how fast the body went, joint stretch, depth below the floor, the seen body
  against the driver, motion after 1.5 s, and the settle. Frames in `Saved/Screenshots/MeleeAudit/`.
- A wall between Kate and a thug 220 cm off; a thug on the street 240 cm out with Kate on a 150 cm block; her path
  frame by frame through a light from 200 cm and through a buffered chain; her clips' blend times.
- Numbers JSON: `Saved/Automation/melee_feel_audit.json`. Input latency and the camera kick were read from the code.

## Findings (before)

| # | What | Measured | Good looks like | Verdict |
|---|------|----------|-----------------|---------|
| 1 | Kate's light: fist to his body at the damage frame, 60 to 250 cm, 0 to 60 deg | driver 0 to 16 cm, seen 1 to 13 cm; lands at 0.25 s at every distance and angle; facing 1 to 2 deg | under ~20 cm, facing him | OK: the contact warp works |
| 2 | Kate's heavy (roundhouse), same grid | driver 0 to 13 cm, seen 0 to 13 cm; 0.39 to 0.40 s; facing 0 deg | as above | OK |
| 3 | Her chain at 100 cm | light2 8 cm (facing 7 deg), light3 15 cm | as above | OK |
| 4 | Thug's fists on Kate, 80 to 160 cm and 45 deg | driver 14 to 16 cm, 0.64 to 0.66 s (his 0.6 s telegraph); 200 cm whiffs (past his 110 cm slide cap) | lands unless she moves | OK |
| 5 | Magnetism through a wall | picked the thug behind a 30 cm wall, warped 65 cm into it, and the assist's forgiveness landed the punch with her fist 87 cm off him | no pick, no pull, no forgiveness through walls | **Defect** |
| 6 | Magnetism over a ledge | from a 150 cm block she lunged off the edge to a thug on the street and hit him (fist 37 cm off) | no pull over a drop | **Defect** |
| 7 | The thug's physical reaction to a blow | every one of 37 blows logged "SK_Mannequin ... has to have 'CollisionEnabled' set to ... if you'd like to AddImpulse": the kick was dropped, his bodies only blended against the clip | the struck part snaps with the blow | **Defect**: punches visibly do not land on him |
| 8 | Which of his bodies a blow moves | the body nearest her fist: hand_l (15), spine_02 (17), lowerarm_l (4), thigh_l (1); a jab at his face moved his guarding hand | the region aimed at (head for a jab, ribs for the roundhouse) | **Defect** |
| 9 | Kate's physical reaction to his punch | her look (Sparrow) has no pelvis body, so "the root body simulating" (spine_01, the reaction's own chain) read as a ragdoll: the reaction cancelled on its first tick and left 5 chest bodies simulating for good, falling away (her look's bodies 2.8 m, then 11 m, then 580 m off her); every later blow skipped her | she flinches each time | **Defect** |
| 10 | Hit-stop | light 35 to 46 ms, heavy 72 to 77 ms, at the damage frame (frame 0), the world at 0.1; a new stop restarts the timer (no cap on back-to-back stops) | 50 to 90 ms by weight, both parties, never stacking | **Defect** (light too short; stacking unguarded) |
| 11 | Reaction clip, flash, sound | clip, flash and sound all on frame 0; his clip side Front for her straight punches, Right for her hook (light2: correct, the hook comes from his right); her clip Front | within 1 to 2 frames, side matches the blow | OK |
| 12 | Physical reaction timing | frame 4 (light) and 9 (heavy) after the damage: it waits out the hit-stop | after the stop | OK |
| 13 | Knockout throw | light: bone 302 / body 127 cm/s; heavy: 520 / 219; arrow 164 / 63 | capped, along the blow | OK |
| 14 | Velocity inherited from the animation | pelvis 1 to 2 cm/s standing, 79 cm/s in the finisher's clip; the first sim frame carries the throw (443 to 747 cm/s) | the body keeps its motion | OK |
| 15 | Finisher's throw | every body set to 900 cm/s on and 450 up: pelvis 1,271 to 1,287 cm/s, the body went 790 and 829 cm | a man falls 1 to 2 m | **Defect** |
| 16 | Distance travelled (pelvis from his feet) | light 81 to 105 cm, heavy 87 to 269 cm, arrow 44 cm, ledge 146 to 237 cm | 1 to 2 m | heavy borderline, finisher bad (15) |
| 17 | Still moving late | 1.5 s after the knockout: 14 to 290 cm/s; slid 35 to 138 cm along the ground; settled at 2.0 to 3.9 s | still by ~2 s, no long slide | **Defect** |
| 18 | Stretch, tunnelling, pop | joint stretch 1 to 2 cm; nothing under the floor (0.1 cm); the seen body 1.7 to 2.0 cm off the driver's pelvis the whole fall | none | OK |
| 19 | Ragdoll setup | SK_Mannequin_PhysicsAsset, 19 bodies, 19 kg in all (1 kg each), damping 0.01 (0.6 / 1.5 set while falling), no depenetration overrides, world: no substepping, 8 position / 2 velocity iterations, Kate walks through bodies (Ragdoll ignores Pawn) | | equal masses: noted, not changed (see below) |
| 20 | Input to swing | the light starts on the button's release (a hold of 0.4 s is the heavy), so a tap adds its own length (typically 70 to 120 ms) before the swing; then 0.25 s to the hit | under ~50 ms to the first frame | **Defect, design**: left for Cameron (below) |
| 21 | Buffering | a light pressed in the recovery before the combo window is accepted and goes 0.08 s later, when the window opens | buffered | OK |
| 22 | Slide and snap in a swing | a light from 200 cm: top speed 1,389 cm/s, the biggest speed change in one frame 404 to 463 cm/s, 0 cm/s the frame the swing ends | the gap closed without a glide | **Defect** (minor): no closing speed cap |
| 23 | Clip blends | Light1-3 and Heavy: in 0.10 s, out 0.20 s | 0.05 to 0.15 in | OK |

The short version: punches do reach the body, and on time. What makes them read as not landing is that nothing
happens to the body when they do: the thug's physical kick was thrown away on every blow (7), aimed at his hand when it
did exist (8), Kate never flinched after the first punch she took (9), and her light's hit-stop was two frames (10).
The ragdolls are not exploding or tunnelling; they are too fast off the finisher (15) and keep rolling and sliding for
two to four seconds (17). Magnetism goes through walls and off ledges (5, 6).

## Fixes (priority order from the brief)
Filled in with the after numbers once the gate is run.

## Left for a human
- Input on release (20): the light could start on the press with the heavy as a hold that cancels its wind-up, as in
  most brawlers, but that changes the control scheme; Cameron's call.
- Equal 1 kg masses (19): a heavier torso would make bodies drop with more weight; not changed, the damping change
  addresses the visible symptom first.
