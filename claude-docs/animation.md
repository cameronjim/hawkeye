# Combat animation

How strike, dodge, reaction, finisher and bow clips get from a download into the game, and what the
game does when a clip is missing. The rules themselves are in `gameplay-semantics.md`, "Combat
animation clips". Built 2026-09-27/28.

## The pipeline in one line

`Tools\import-anims.ps1` = `Tools/Editor/import_combat_anims.py` (import or copy, IK rigs, IK
retargeters, batch retarget, `AM_` montages) then `Tools/Editor/create_combat_anims.py` (fill the
`DA_AnimSet_` data assets and put them on the Blueprints). Everything reads
`Tools/Data/Anims/manifest.json`. `Tools\import-anims.ps1 -SelfTest` proves the whole path on clips
that are always on this machine.

## What was used, and why

| Need | Used | Why |
|------|------|-----|
| Retargeting | The engine's IK Rig and IK Retargeter, driven from Python: `IKRigController` for the rigs, `IKRetargeterController` (`add_default_ops`, `auto_map_chains`, a target retarget pose with `auto_align_all_bones`, the root motion op controller), `IKRetargetBatchOperation.run_batch_retarget` for the clips | It is Epic's supported path in 5.8 and fully scriptable headless. Nothing written from scratch. |
| Target rigs | Built by the script from the manifest's chain maps, on the project's own meshes (`IK_Kate_UEFN`, `IK_Thug_Mannequin`) | Epic's `IK_UEFN_Mannequin` from the Game Animation Sample was tried first. Its stored skeleton lacks this project's `attach` bone, and every retargeted track came out one bone down the hierarchy (pelvis at the origin, spine carrying the pelvis). The self-test's raw-bone checks now catch that. GASP's `RTG_UEFN_to_*` retargeters all go from UEFN outward, so none could be reused for clips coming in. |
| Montages | `UHawkeyeCombatMontageBuilder` (C++, editor-only): the engine's `UAnimMontageFactory` plus `UAnimationBlueprintLibrary`'s notify calls | Python cannot set a montage's slot name; C++ can, and the compiler checks it. |
| Aiming strikes at the target | The engine's Motion Warping plugin: the `MotionWarping` component the sample already puts on Kate (one is added only to a character without one), a skew-warp window in each strike montage, the warp target `CombatTarget` | GASP's traversal already uses it on Kate; a second component would warp every clip twice. |
| Upper body over locomotion | Slots in the bow-IK post-process graph (`ABP_BowIK_Post`, `_Thug`): `DefaultSlot`, cached, then `UpperBody` layered from `spine_01` up in mesh space, then the existing lean, spine turn and two-bone IK | The graph was already built headless by `UHawkeyeBowIKGraphBuilder`, runs on every character, and runs after the main AnimBP, so IK can correct a clip's hands. The sample's own AnimBP is left untouched. |
| FBX import | `AssetImportTask` with `FbxImportUI` options, which 5.8 converts for Interchange | The documented scripted import; the self-test runs it on the engine's own FBX automation file. |
| Bow aim up, down and round | An engine Aim Offset (`UAimOffsetBlendSpace`, played by the anim graph's Aim Offset node, `UAnimGraphNode_RotationOffsetBlendSpace`), `BS_BowAimOffset_Sparrow` per skeleton: Sparrow's own `AO_idle` layout and its nine `idle_AO_*` poses retargeted, made mesh-space additive on the centre pose. Built by `UHawkeyeCombatMontageBuilder::BuildAimOffset` (C++, editor-only) | Aim offsets are Epic's tool for exactly this. The asset route worked, so no spine twist was hand-rolled for pitch. C++ because a blend space's axes are protected and its samples are added through editor-only calls. |

## Where clips play

- A montage whose slot is `UpperBody` (the bow clips) plays on the post-process instance, over
  locomotion from `spine_01` up, with the bow's aim offset on top of it (see "The bow clips").
- Any other montage plays full body: on Kate and Clint in the sample AnimBP's `DefaultSlot` (the same
  slot the parkour clips use), on thugs and archers in the post-process graph's `DefaultSlot`
  (their main instance is a single looping sequence and has no slots).
- `HawkeyeCombatAnim::PickInstance` makes that choice; everything else calls
  `HawkeyeCombatAnim::Play` or `PlayRole` and treats a null return as "use the fallback".

## The fallback rule

Every role is optional. An empty slot in the set, a montage that does not load, or one that will not
play (no anim instance, another skeleton) sends the code down exactly the path it had before clips:
the strike pose and lunge, the procedural dash, the hit lean, the ragdoll knockdown, the IK-only bow.
The automation tests run with no content, so they exercise the fallback every time;
`Hawkeye.CombatAnim.NoClipKeepsProceduralBehaviour` fills every role with a montage that cannot play
and checks the old timings.

## Skeletons

| Who | Skeleton | Target rig | Clips land in |
|-----|----------|------------|---------------|
| Kate, Clint | `SK_UEFN_Mannequin` (the sample's) | `IK_Kate_UEFN` | `/Game/Characters/UEFN_Mannequin/Animations/Combat` |
| Thugs, the heavy, the gunner, archers | `SK_Mannequin_Skeleton` (the UE4 mannequin in `/Game/Mannequin`) | `IK_Thug_Mannequin` | `/Game/Mannequin/Animations/Combat` |

Note the thugs are not on the UEFN skeleton: they wear the UE4 mannequin, so every clip a thug uses is
retargeted twice, once per target.

Chain maps (in the manifest, `chain_maps`): all use the same chain names as Epic's UEFN rig (Root,
Spine, Neck, Head, Left/RightClavicle, Left/RightArm, fingers, Left/RightLeg, Left/RightToe), so the
retargeter maps them by exact name.

| Map | Source | Retarget root | Root bone | Notes |
|-----|--------|---------------|-----------|-------|
| `mixamo` | Mixamo's rig: Hips, Spine..Spine2, Neck, Head, LeftShoulder, LeftArm..LeftHand, LeftHandThumb1..3 (and Index, Middle, Ring, Pinky), LeftUpLeg..LeftFoot, LeftToeBase | Hips | none | Bone names match with or without a `mixamorig:` prefix. With no root bone the root motion op generates root motion from the target pelvis: root straight under the pelvis, height snapped to the ground, reference rotation (see "Root motion from a rig with no root"). |
| `ue4` | UE4 mannequin and Paragon heroes (Sparrow): root, pelvis, spine_01..03, neck_01, head, clavicle, upperarm..hand, three-joint fingers, thigh..foot, ball | pelvis | root | 22 chains on Sparrow. |
| `uefn` | UEFN and UE5 mannequins: spine_01..05, neck_01..02 | pelvis | root | Kate's target; the self-test's UE5 source. |

## Root motion from a rig with no root

The IK Retargeter's root motion op, set to Generate From Target Pelvis, still defaults its height to
Copy Height From Source, which reads the source's root bone; Mixamo has none, so that is its first
bone, the hips. Every Mixamo clip came out with the root track at 85 to 92 cm and the pelvis's own
track about 2 cm above it (A_Light1_Cross: root z 89, pelvis 91 in component space at frame 0). The
import's pose line read the pelvis through the root and looked right; the game extracts the root as
root motion (Kate's strikes) or locks it (everything else), and the pelvis dropped to the floor. The
script now sets the op to Snap To Ground with no offset from the pelvis and no pelvis rotation, so the
root carries only the hips' horizontal travel (A_Light1_Cross after: root z 0, pelvis 91 to 94).

Two more things made that fix look like it did nothing: `IKRetargetFactory` can create the retargeter
with the default op stack already in, and a retargeter the script could not delete (still loaded)
came back with every earlier run's stack, so `add_default_ops` stacked more copies whose root motion
op sat at its defaults and ran last. The script clears the stack and adds exactly one. The import log
prints the op count ("6 ops": the default five and the Pin Bones op below).

## The IK bones

The UEFN and UE4 mannequins carry `ik_foot_l`, `ik_foot_r`, `ik_hand_l`, `ik_hand_r` and `ik_hand_gun`,
and the Game Animation Sample's AnimBP runs foot placement and leg IK toward the foot ones. A retarget
leaves them at their reference pose, so until 2026-09-28 every Mixamo clip had Kate's feet pulled back
to the reference stance in game: the kick's foot stayed on the floor at its hit and the strikes lost
their split stance. The retargeter now ends with a Pin Bones op that copies each from the foot or hand
it follows (translation and rotation, no offset: they coincide in both reference poses; the op's
"maintain offset" measures in the retarget pose, where the aligned legs have moved, and was 3 to 8 cm
off). The self-test checks the foot ones on the clip's keys.

## Timing a clip for play

Mixamo strikes are demo-paced: about a second of wind-up before the punch. A clip can say, in its
source's seconds, which stretch to play and how fast (`start_s`, `end_s`, `rate`) and where its windows
are (`hit_s`, `combo_s`, optional `warp_end_s`; the warp defaults to the hit). The montage builder puts
the stretch and rate into the montage's own segment (the montage editor's Start Time, End Time and
Play Rate), so the montage is short and plays at rate 1, and maps the windows into it. A hit lands
`(hit_s[0] - start_s) / rate` after the input. The numbers for Kate's clips come from the fist or foot's
forward reach from the pelvis per frame (read with `GetRawBoneLocation`): the hit window opens when
the striking hand is about two thirds out, and the stretch starts at or just before the cocked pose.

| Clip | Variant | Stretch (s) | Rate | Hit after input | Combo window | Montage |
|------|---------|-------------|------|-----------------|--------------|---------|
| AM_Light1_Cross (right cross) | 1 of 2 | 0.60-1.70 | 1.7 | 0.25 s | 0.34-0.62 s | 0.65 s |
| AM_Light1_Punching (left jab from a dip) | 2 of 2 | 0.13-1.00 | 1.5 | 0.25 s | 0.33-0.56 s | 0.58 s |
| AM_Light2_Hook | 1 of 2 | 0.70-1.75 | 1.3 | 0.25 s | 0.35-0.77 s | 0.81 s |
| AM_Light2_Boxing (jab, then the right cross that hits) | 2 of 2 | 0.25-1.05 | 1.4 | 0.25 s | 0.33-0.55 s | 0.57 s |
| AM_Light3_UppercutJab (the left uppercut) | 1 of 2 | 0.45-1.25 | 1.4 | 0.24 s | 0.34-0.55 s | 0.57 s |
| AM_Light3_JabElbow (the left jab; the elbow after it is cut) | 2 of 2 | 0.48-1.40 | 1.7 | 0.25 s | 0.34-0.52 s | 0.54 s |
| AM_Kick_Kicking (the front kick) | 1 of 2 | 0.20-1.35 | 1.2 | 0.29 s | 0.46-0.92 s | 0.96 s |
| AM_Kick_SideKick | 2 of 2 | 0.20-1.45 | 1.4 | 0.29 s | 0.46-0.87 s | 0.89 s |
| AM_Heavy_Roundhouse (right roundhouse kick) | 1 of 1 | 0.40-1.65 | 1.2 | 0.39 s | 0.58-1.00 s | 1.04 s |
| AM_Heavy_SurpriseUppercut | out of the set | 0.65-1.80 | 1.1 | 0.41 s | 0.59-1.00 s | 1.05 s |

Variants (2026-09-28): each strike takes the next clip of its role in manifest order; the log line
"... variant 2 of 2 for ECombatAnimRole::Light1: AM_Light1_Punching" says which. Kate's heavy is the
Roundhouse only since 2026-09-29: side on, the Surprise Uppercut's hit window shows her back, hips 47
degrees turned and the fist still 46 cm out to her right at 101 cm, while the roundhouse's foot is 61 cm
out at 83 cm; a later trim would not help, the turn is the move. It stays imported with `"in_set": false`
until a better second heavy exists. The second variants were measured the same
way (`GetRawBoneLocation` per frame of the retargeted `A_` clip): the hit opens when the fist or foot is
80 to 90% of its way out. The Roundhouse's standing leg straightens from her bent-knee idle: her pelvis
is 16 cm above standing at the hit (95 against 79 cm), the standing foot flat on the ground.
The Kick role plays Kicking first (2026-09-28): at the side kick's hit Kate's torso is near flat over her
standing hip, head at 123 cm (its import `check` line says so: head +23 at mid-clip); the front kick
stays upright (head 142 cm) and lands at the same time. Side Kick is the second kick variant; only the
Light3 fallback and the kick shot play the Kick role, so whether it stays is Cameron's call. The thugs'
fists alternate Jab To Elbow and Standing Melee Punch (both fitted to the telegraph; the jab first since
2026-09-29, see below); the run-jump attack is `"in_set": false`.

Thug strikes are trimmed the same way but kept at rate 1 in the manifest; the melee component fits
them to the attack's telegraph when they play (`HawkeyeCombatAnim::FitHitToWindup`): the rate that
puts the hit window on the telegraph's end, held between 0.8x and 1.3x; past 1.3x the clip starts
later in its wind-up, below 0.8x it holds its first frame first. The log line says which ("plays
AM_Light1_MeleePunch at 0.80x from 0.00 s, held 0.29 s") and "hit window opens 0.61 s into the swing".
The numbers come from the striking hand's forward reach per frame, as for Kate's.

| Clip | Stretch (s) | Hit (s) | Swings | Fit, hit lands at | Hit to end |
|------|-------------|---------|--------|-------------------|------------|
| AM_Light1_JabElbow (fists, first variant) | 0.30-1.40 | 0.85-0.95 | fists 0.6 s | 0.92x, 0.60 s | 0.60 s |
| AM_Light1_MeleePunch (fists, second variant) | 0.00-0.75 | 0.25-0.35 | fists 0.6 s | held 0.29 s, 0.8x, 0.60 s | 0.63 s |
| AM_Heavy_MeleeHorizontal | 0.00-1.50 | 0.87-0.97 | bat 0.6 s, bash 0.8 s, slow swing 1.0 s | 1.3x from 0.09 s, 0.60 s; 1.09x, 0.80 s; 0.87x, 1.00 s | 0.48, 0.58, 0.72 s |
| AM_Heavy_RunJumpAttack (`in_set: false`) | 0.55-2.40 | 1.64-1.74 | | a jump attack; its pelvis leaves the ground | |

Standing Melee Punch has only 0.25 s of wind-up, hence the hold; the Jab To Elbow jab fits 0.6 s
with none. Since 2026-09-29 the jab is the first fists variant: `thug_punch.png` and `thug_punch_2.png`
(0.3 s into the telegraph, side on) show the jab with his guard up (right hand at 160 cm) and the punch
still in its held first frame, hands low (107 to 112 cm), which reads as standing about, not winding up. The thug's reactions: Knocked
Out plays 0.80-2.60 s (0.9 s of standing still cut; on the floor by 1.4 s, holding its last frame
there), Sweep Fall 0.60-2.00 s (starts as his feet go). Receive Punch To The Face starts reacting on
its second frame and is untrimmed.

## The bow clips

Rebuilt 2026-09-28 after Cameron's playtest ("the bow animation is not natural"). The still
`bow_aim_full.png` looked right; the draw in motion did not. `Hawkeye.Screenshot.BowDraw` (below)
stopped time at each moment of a real draw and showed why, with numbers in its log:

1. **The draw clip was stretched to the draw time.** `RMB_Drawback` is Sparrow's 2.47 s charged shot
   (the string hand creeps back from 0.2 s to 2.2 s), and the code fitted it to Kate's 0.8 s draw at
   3.1x. Its auto blend-out then started before its end: 0.6 s into the draw its montage read position
   0 and the clip alpha was falling, the bow hand's IK coming back on, before `BowAimIdle` blended in at
   full draw. A dip in the middle of every draw.
2. **No aim offset.** At 30 degrees up or down the bow arm did not move (hand_l height the same to a
   centimetre); the bow turned in the hand and the string hand IK, keeping its offset from the grip
   along the aim, dragged the draw hand down to her stomach (aiming up) or over her head (aiming down).
3. **The IK overrode the clip's string hand.** Its target under a clip was the grip plus the procedural
   offsets, which put the string hand 20 cm to the right of the bow: the nocked arrow pointed 20 to 25
   degrees off where the arrow would fly (logged as "arrow ... deg off the launch").
4. **The bow went on her back with the arms still up.** The follow-through ended at 0.6 s while the
   fire clip still held the arms at full weight, so her bow hand held nothing. (With the shorter fire
   clip the opposite showed next: the clip was gone by 0.43 s and the follow-through's IK lifted the
   empty-handed arm back up. A shot on a clip now follows through only until the clip blends out.)

After (same log): the nocked arrow is 2 to 6 degrees off the launch direction when held (was 20 to
31), the string hand 3 to 7 cm off the arrow line at level aim with a correction of 0 to 0.13, and the
bow arm pitches with the aim: the bow hand is 39 cm above the actor at level aim, 60 at 30 degrees up
and 17 at 30 down (it was 50 at all three).

What plays now (numbers in `gameplay-semantics.md`, "Combat animation clips"):

| Role | Sparrow clip | Stretch | Montage | Notes |
|------|--------------|---------|---------|-------|
| BowDraw | Primary_Fire_Med | 0.43-0.93 s, rate 1 | 0.50 s, holds its last frame | The hand from the arrow rest to the anchor; the pull itself is 0.33 s. Ends on the idle's first pose. |
| BowAimIdle | idle | all 10 s, loops | 10 s | Sparrow's idle is her drawn stance, breathing. |
| BowFire | Primary_Fire_Med | 0.00-0.43 s | 0.43 s | The snap, then the hand back to the string: where BowDraw starts, so a re-draw runs on. |

Sparrow's `Primary_Fire_*` clips are whole shots (release, nock, draw); the stretches above were found
by reading the hands per frame (`GetRawBoneLocation`: the string hand's distance from the bow hand goes
74 cm at the anchor, 101 at the release's follow-through, 19 at the nock, back to 74). The bow roles blend
in over 0.15 s and out over 0.25 s. The draw's pace is the clip's; the draw fraction (damage, the
perfect window) keeps its own clock, so Kate is at the anchor at 0.5 s and at full strength at 0.8 s.

Under a clip the IK follows it: the clip alpha is the UpperBody slot's weight, so the bow hand's IK is
exactly as far off as the clip is in; the bow rides in the clip's hand; the string hand stays on the
clip with a correction toward the arrow line of at most 0.5 (none within 3 cm), its pole the clip's
own elbow. The correction needs the clip's hand before the IK moved it: the post-process instance
recovers it from the last pose, since the two-bone IK blends the hand's component-space position
linearly by its alpha (`clip = (final - alpha * target) / (1 - alpha)`).

The aim offset is built by `import_combat_anims.py` from the manifest's `aim_offsets` block: the nine
`BowAimOffset_<pose>` clips (CC, CU, CD, LC, LU, LD, RC, RU, RD) are retargeted like any clip (full
poses, additive off), then made mesh-space additive on the retargeted CC pose, and
`BS_BowAimOffset_Sparrow` is laid out as Sparrow's `AO_idle` (its axes, ranges and each pose's place);
the back poses (LB*, RB*) are left out since she always turns to face the aim. `create_bow_ik.py` puts an
Aim Offset node after the UpperBody slot in both bow graphs, fed `AimOffsetYaw`, `AimOffsetPitch` and
`AimOffsetAlpha` (clip alpha times bow alpha) from `UHawkeyeBowIKAnimInstance`, and sets the asset on
the Blueprint's defaults so the instance clamps its inputs to its range. Without the asset the graph
has no node and everything is as before.

## Sources

- `mixamo_fbx`: a folder of FBX. The source's `mesh_file` (one clip downloaded With Skin) imports as
  a skeletal mesh and defines the skeleton; then every clip, that one included, imports as an
  animation onto it (`/Game/AnimSources/<Source>/A_Src_<File>`). Frames snap to 30 fps.
- `uasset_project`: clips in another project's `Content` folder. The clips, the source mesh and their
  hard-reference closure are copied to the same `/Game/` paths, never overwriting.
  `Tools/Editor/_packages.py` reads the import tables of both UE 5.8 and UE4 packages (the Paragon
  packs were saved by 4.19), so a copy takes what a package loads and nothing else: Sparrow's six
  clips are 3 MB, its mesh with materials 730 MB.

Paragon's hit reactions are additive over its idle. They are retargeted with additive off
(`retain_additive_flags=False`), which bakes idle plus hit into a full pose; kept additive they played
as garbage in a full-body slot and pointed at Sparrow's idle on Sparrow's skeleton.

## How to add a clip

1. Put the file in `Tools/Data/Anims/<Source>/` (or the pack in its project) and add a line to
   `manifest.json` `clips`: `source`, `file` or `asset`, `role`, `variant`, `characters`
   (`kate`, `clint`, `thug`, `archer`, `all`, or a list).
2. Optional per clip: `hit`, `combo` (fractions of the clip), `warp_end`, `loop`, `hold`,
   `root_motion`, `blend_in`, `blend_out`, `slot`; or the timing keys in seconds of the source clip,
   `start_s`, `end_s`, `rate`, `hit_s`, `combo_s`, `warp_end_s` (see "Timing a clip for play"). `"in_set": false` imports a clip but keeps it out of the anim set. Leave the timing keys out to take `ROLE_DEFAULTS` in
   `import_combat_anims.py` (strikes: hit 25-45%, combo 45-80%, warp to the hit; heavy 40-55%, 60-85%;
   Knockdown holds its last frame; BowAimIdle loops; bow roles play `UpperBody`).
3. `.\Tools\import-anims.ps1` (add `-Force` to redo existing assets). Read the log: one `created`
   line per montage with its windows, one `pose` (or `check`) line per retargeted clip with its
   pelvis height at start, middle and end against the reference, and the head and hands.
4. When a role has several clips, all of them go in the set in manifest order: the first in the role's
   slot, the rest in `MoreVariants`. Strikes cycle through them; every other role plays the first.
   Reorder the manifest to change the order, `"in_set": false` to leave one out. Hand-made
   `AM_<Role>_*` montages in the folder come after, by name.
5. Commit the manifest and the `DA_AnimSet_` assets. The clips, the copies and the retargeted
   results are git-ignored (Mixamo's and Epic's content is not republished); a fresh clone rebuilds
   them with the script.

## Checking without looking

Nobody can watch a headless run, so the scripts read bones:

- `--selftest` retargets two of GASP's UE5 mannequin clips (another skeleton, a `uasset_project`
  source), the engine's `FbxEditorAutomation/AnimatedCharacter.fbx` (an FBX source with no root bone),
  and Sparrow's `RMB_Drawback` and `HitReact_Fwd` to both targets, then asserts: on the target
  skeleton, the source's length, only target bones in the tracks, full-pose not additive, the right
  slot and windows, standing clips standing (pelvis within 35 cm of the reference, hips and head
  where they belong, and the pelvis within 15 cm of the reference with the root locked: the engine's
  FBX character has no root bone, so this is the check that fails if the root takes the hips' height), and for the bow draw: the bow arm as extended as the source's, the bow hand
  30+ cm in front, the string hand drawn back 40+ cm behind it at full draw; and `ik_foot_l` and
  `ik_foot_r` on the feet on every key. 134 checks.
- Poses are read from the raw bone tracks with `UHawkeyeCombatMontageBuilder::GetRawBoneLocation`
  (`bRootLocked` holds the root at its reference pose, as the game does; the `pose` line prints both).
  `AnimPoseExtensions` evaluation gave the pelvis as the origin on some UEFN clips while its children
  were right, so it is not trusted for this.
- `Hawkeye.Smoke.ThugClipsStrikeOnTheTelegraph` swings the district's street thug (fists, bat) and the
  heavy (bash, slow swing) with their clips and checks each hit window opens on the telegraph's end.
- `Hawkeye.Screenshot.Melee` shoots two heavies and the kick side on at their hit windows
  (heavy_strike.png, heavy_strike_2.png, kick.png) and fails on the wrong montage (the second heavy
  repeating the first's clip is wrong), or a pelvis more than 15 cm below standing or 20 cm above it.
- `Hawkeye.Smoke.CombatClipsPlay` loads the district and plays each character's clips: the slot's
  global weight must be above 0 a quarter second later, and Kate's Light1 must still be winding up
  before its `ANS_HitWindow` and recovering after it (the notify firing from a real montage).
- `Hawkeye.Screenshot.BowDraw` (standalone game) draws on the world clock at 0.25x and stops time
  (global dilation 0.0001) at 0.1, 0.3, 0.6 and 1.0 s into a draw, held 30 degrees up and down, 0.1 s
  after the release, 0.3 s into a re-draw, twice while strafing, and 0.6 s after a second release;
  each moment is a side-front shot and one through her own camera (`Saved/Screenshots/BowDraw/`). Its
  log gives the clip, the slot weight, every IK alpha, the string hand's distance off the arrow line
  and the nocked arrow's angle to the launch direction for each.

## Kate's visible mesh (built 2026-11-10)

**Since 2026-11-12 Kate wears her built suit** (`SK_Built_Kate` on the Quaternius female body, `blender-pipeline.md`,
"Kate"), set by `import_bodies.py` exactly as Clint's is: `IK_Body_Female`, `RTG_Kate_To_Body` from `IK_Kate_UEFN`,
`ABP_Kate_Body` (see "Bodies" below). Sparrow, described in the rest of this section, is the fallback when the built
Kate is not on disk or does not import: `create_kate_look.py` still builds her assets and puts her on `BP_Kate` then.
The built look has no physics asset, so, like Clint's, it ticks after physics and her hit reactions run on the driver;
arrows stuck in her sit on the driver's surface. Nothing in C++ changed for the swap.

Kate is drawn as Paragon's Sparrow, in purple, over the Game Animation Sample's UEFN mannequin. The mannequin
(`GetMesh()`, the driver) keeps doing everything it did: the sample's motion-matched AnimBP, every montage, the
bow-IK post-process graph (hands, string, hit lean, hang hands), root motion, notifies, collision and the sockets
gameplay reads; the bow is attached to it. It is hidden (`SetVisibility(false)`, Visibility Based Anim Tick Option
`AlwaysTickPoseAndRefreshBones`, so it still poses unseen, and a hidden component casts no shadow). A child
skeletal mesh, `BodyLook`, shows Sparrow at the driver's own transform (so it takes the 0.93 scale and the roll's
tumble), ticks after it, has no collision, and is posed every frame by one Retarget Pose From Mesh node whose source
is the parent component. That is how the sample shows its retargeted characters (its `ABP_GenericRetarget`); the
sample's AnimBP is not copied because it hard-references every retargeter in the sample.

| Asset | Built by | What |
|-------|----------|------|
| `/Game/Characters/Kate/Look/IK_Kate_Sparrow` | `create_kate_look.py` | IK Rig on Sparrow: the manifest's `ue4` chains (the names `IK_Kate_UEFN` uses), a goal on each hand and foot, one limb solver per limb |
| `.../RTG_Kate_To_Sparrow` | same | `IK_Kate_UEFN` -> `IK_Kate_Sparrow`: the default op stack plus an IK Chains op under Run IK Rig (5.8's defaults have none), chains by name, Sparrow's retarget pose auto-aligned to the mannequin's; the arm chains blend fully to the source (place and turn) |
| `.../ABP_Kate_Sparrow` | `UHawkeyeRetargetGraphBuilder` (C++, editor-only), called by the script | Retarget Pose From Mesh, parent component, that retargeter |
| `.../MI_Kate_Sparrow_TorsoArms`, `_ChestBag`, `_LegsHands` | same script, `_materials.ensure_material_instance` | Sparrow's cloth colour parameters set to purples; Paragon's materials are untouched |
| `BP_Kate.BodyLook` (`FHawkeyeBodyLook`) | same script | soft references to the mesh, the AnimBP class and the instances per slot, and the bones to hide |

The `Look` folder is git-ignored like the other Paragon-derived assets; BP_Kate (committed) only holds soft
references. Without Sparrow on disk (a fresh clone) the script logs one `skipped` line and changes nothing, and at
runtime `HawkeyeBodyLook::Apply` finds the packages missing, logs one Display line and leaves the mannequin drawn,
exactly as before. The script runs in `create_all.py` after the bow IK and at the end of `Tools\import-anims.ps1`
(which is what copies Sparrow in).

**Which mesh is read.** `AHawkeyeCharacter::GetBodyMesh()` is the mesh people see (the look, else the driver);
`GetMesh()` stays the driver. Read the body mesh for how she looks: the camera's close-up hide
(`UpdateBodyVisibilityForCamera`) and arrows stuck in her (`AArrowProjectile::FindBodyMesh`: Sparrow's physics
asset, so an arrow sits in Sparrow's surface and rides her bone). Everything else reads the driver: the bow
(placement, string, grip socket, holster bone), montages and notifies (melee hit windows), the hang hands, the zip
line's hand, the strike-contact height, `IsBodyDrivenByAnimBlueprint`. Footstep and landing effects spawn at the
capsule's bottom and the camera's arm is on the capsule, so neither reads a mesh.
No material on Kate is driven at runtime (low health and colour-blind palettes are post-process and UI; hit flash is
the thugs'), so nothing else needed moving.

**The bow.** Sparrow's mesh carries her own bow (`bow_base` and its shaft and string bones under `hand_l`) and a
nocked arrow (`arrow_nock` under `hand_r`). Both are hidden (`HideBoneByName`), and Kate keeps the game's bow: it
already has the string IK, the nocked arrow, the holster and the draw timing, and keeping it means no bow code
changes. Sparrow's bow bones are not driven by the retarget (they are not in a chain), so hers could not draw anyway.
Her back quiver stays: it is the only quiver she has.

**Proportions.** Sparrow is taller and longer-legged than the mannequin: pelvis 110 cm against 93, head bone 165
against 152, arm (shoulder to wrist) 53 cm against 48, in the reference poses. At the driver's 0.93 scale she
stands about 170 cm, which is Kate's height, so no extra scale. The retarget scales the pelvis's height by her
pelvis ratio (1.18), so standing and running her feet reach the floor on her own leg length (the legs' IK goals
keep the retarget's placement). Her hands are blended fully onto the driver's hands, so the bow grip, the
string hand at the anchor and a hang's hands on the ledge are exactly where the driver's IK put them; her longer
arm reaches them from her higher shoulders. Where the driver's arm is straight down by its side she cannot quite
reach (her shoulder is about 12 cm higher) and her arm stays straight, a few cm short.

**Hair and cloth.** Sparrow's mesh has no clothing assets, and her hair, hood, skirt and feather bones are not
simulated here (her Paragon AnimBP did that with AnimDynamics, which this AnimBP does not run): they follow their
parents rigidly. Nothing simulates, so a zip, a vault or a fast-travel teleport cannot throw them about. (The built Kate's
ponytail and quiver do simulate, see "Secondary motion".)

**Swapping the look** (a licensed Kate later): put the new skeletal mesh in, point `create_kate_look.py`'s
`SPARROW_MESH`, chain map, material slots and `HIDDEN_BONES` at it (or set `BP_Kate.BodyLook` by hand for a test),
and run the script. Nothing in C++ names Sparrow.

## Bodies: the thugs, the heavy, Clint and Lucky (built 2026-11-11)

The same look mechanism, on Quaternius's CC0 packs instead of Paragon. `Tools/Editor/import_bodies.py` (a
`create_all.py` step, after `create_kate_look`) imports from the packs outside the repo
(`HAWKEYE_ASSETS_ROOT`, `Tools\local.json` `assetsRoot`, else `C:\Users\camer\code\hawkeye-assets`; read as data, only
handed to the engine's importer) and builds everything under git-ignored folders:

| Asset | What |
|-------|------|
| `/Game/Characters/Bodies/SK_Body_Male`, `SKEL_Body_Male` | Universal Base Characters' `Superhero_Male_FullBody.gltf` through Interchange (no materials, no textures, no physics asset). 181 cm, cm scale, faces +Y with arms along X like the UE mannequins, so no import rotation or scale was needed. Its 65 bones carry the UE mannequin's names (`root`, `pelvis`, `spine_01..03`, `neck_01`, `Head`, `clavicle`..`hand`, three-joint fingers plus `_04_leaf`, `thigh`..`ball`); no `ik_` or twist bones. The Standard pack has no regular-proportion male, only this Superhero one. |
| `T_Body_Male_D/_N/_R`, `T_Body_Eye_D`, `T_Body_Hair_D` | from the pack's `Textures` folder (the glTF names `_png.png` files the pack does not ship) |
| `M_BodyLook`, `MI_Body_Thug/_Heavy/_Clint/_Eyes/_Brows` | skin from the texture; clothes painted from the bind pose (`PreSkinnedPosition`/`Normal`, like the thugs' tracksuit): top, legs, belt, stripes, a vest, a forearm guard, a chest chevron, boots, painted short hair and stubble, an optional ski mask, each a parameter. Cloth takes no normal map, little specular and high roughness, so the sculpted muscles read as clothing. `HitFlash` and `Telegraph` are the thug's. |
| `IK_Body_Male` | `create_kate_look.ensure_rig` on the body: the manifest's `ue4` chains (`head` matches `Head`; FNames ignore case), a goal and limb solver per limb |
| `/Game/Characters/Thug/Look/RTG_Thug_To_Body`, `ABP_Thug_Body` | `IK_Thug_Mannequin` (UE4 mannequin) to the body; hands blended fully onto the driver's |
| `/Game/Characters/Clint/Look/RTG_Clint_To_Body`, `ABP_Clint_Body` | `IK_Kate_UEFN` (UEFN mannequin) to the body; the same |
| `SK_Body_Female`, `SKEL_Body_Female`, `IK_Body_Female` | the pack's `Superhero_Female_FullBody.gltf` (177 cm, the same 65 bone names, its own rest pose), imported only when the built Kate is on disk, and its IK rig |
| `/Game/Characters/Kate/Look/RTG_Kate_To_Body`, `ABP_Kate_Body` | `IK_Kate_UEFN` to the female body for `SK_Built_Kate`; hands blended fully onto the driver's. `BP_Kate.BodyLook` points at it (`ensure_kate`) |
| `/Game/Characters/Lucky/SK_Lucky`, `SKEL_Lucky`, `A_Lucky_Idle/_Walk/_Trot/_Sit`, `T_Lucky_D/_N`, `MI_Lucky` | Lucky, a golden retriever generated locally (`docs/CREDITS.md`): `<assets root>\built\lucky\SK_Lucky.glb` through Interchange (no materials; metres in, 74 cm tall and 115 cm long in, faces +X, so no rotation or scale), its own 30-bone skeleton and four in-place clips at 30 fps (Walk's feet cover about 30 cm/s, Trot's about 92; Sit is a seated idle). `MI_Lucky` is an instance of `M_BuiltLook` with his base colour and his Blender normal map (green flipped). `ALuckyCharacter` (C++, `City_Lucky` in the district) plays them as single-node animations, the moving clips at his ground speed over the clip's (gameplay-semantics.md, "Lucky"). |

**The skeleton question.** No mesh is put on another skeleton. The thugs keep the UE4 mannequin and Clint the UEFN
mannequin as their drivers (`GetMesh()`): every clip, montage, notify, socket, the bow, the bat and the shield, hit
contact's sweeps against the physics bodies by bone name, the hit reaction's motors and the knockout ragdoll stay on
the driver exactly as before. The body is a `BodyLook` (`AThugCharacter::BodyLook`, `AHawkeyeCharacter::BodyLook`)
posed from the driver by Retarget Pose From Mesh. The body mesh has no physics asset, and that is the switch:
`HawkeyeBodyLook::Apply` ticks a look without bodies in `TG_PostPhysics`, after the driver's physics is blended into
its pose, so the body follows a hit reaction or a ragdoll (measured: the body's pelvis 2 cm from the ragdoll's); and
`UHitReactionComponent::GetBodyMesh` reacts on the driver when the look has no physics asset (Kate's Sparrow has one
and still reacts herself). `AThugCharacter::GetBodyMesh()` is what is drawn; his hit-flash and telegraph material
instances are made on it.

Who wears what: `BP_Thug` (fists, bat, gunner) the dark red tracksuit; `BP_Thug_Heavy` the tracksuit, vest, ski mask and
boots; `BP_Clint` black; `BP_Kate` her built suit on the female body (no painted version: without the build she is
Sparrow or the mannequin).

**Built costumes.** Modelled clothes from Blender replace the painted ones per wearer when they are on disk
(`blender-pipeline.md`): `Tools/Blender/build_<who>.py` writes `<assets root>\built\<who>\SK_<Who>.glb` on the body's
own armature, and `import_bodies.py` (`ensure_built`) imports it onto `SKEL_Body_Male` as
`/Game/Characters/Bodies/Built/SK_Built_<Who>` with `MI_<slot>` instances of `M_BuiltLook`, then sets that wearer's
`BodyLook` to it with the same `ABP_<Driver>_Body`, with four engine-reduced LODs. No build on disk, or any step
failing: the painted body as before. First run in the engine 2026-11-11: clean, see that doc's "Unreal side". Every other child of `BP_Thug` (the archer, elves, reindeer, candy cane, Santa, Tomas) is
written an empty look by the script, so it keeps its own mannequin colours rather than inheriting BP_Thug's.

**Fallback.** Without the packs the script logs one `skipped` line. The Blueprints keep their soft references;
`HawkeyeBodyLook::Apply` finds the packages missing, logs one Display line and the mannequin is drawn as before.
`verify_content.py` (`check_bodies`) says which each wearer shows and fails only when a body that is on disk is
incomplete (or SK_Lucky is there without its four clips). `Hawkeye.Screenshot.Bodies` writes
`Saved/Screenshots/Bodies/body_thug, body_heavy, body_clint, body_lucky, body_thug_ragdoll, body_thug_punch,
body_thug_flash, body_clint_draw, body_lucky_kate.png`, then Kate's `body_kate_<shot>_<day, night>.png` (blender-pipeline.md,
"Unreal side") and checks the body is not exploded, T-posed, turned or off the
driver's head, follows the ragdoll, the punch is caught in its wind-up (the telegraph glow), the flash over half, and
Lucky 40 to 95 cm tall, facing his way and on the ground.

**Not done.** (The get-up is our own since 2026-11-13, "Authored clips"; UAL2 is not used.) The painted body does not use the pack's hair meshes
(hair is painted; the built costumes do);
arrows stuck in a thug sit on the hidden driver's surface, a few cm off the body's.

## Authored clips (built 2026-11-13)

Our own get-ups, hit reactions and a bow set, keyed by script in Blender on the drivers' own skeletons, not downloaded.

**Pipeline.** `Tools/Editor/export_clip_rigs.py` (headless editor, once) writes the drivers' idles and the pose references
to `<assets root>/built/clips/rigs/` (FBX of animations: they carry the skeleton; the skeletal mesh exporter asserts in a
headless editor). `Tools/Blender/build_clips.py` (with `clipkit.py`) keys each clip and writes
`<assets root>/built/clips/<ue4 | uefn>/<Clip>.fbx` plus `Skeleton.fbx` (the rig at rest with a capsule stand-in body:
the import binds the source skeleton from it; a clip's own file bound it in its first frame's pose and the retarget came
out 8 m tall), renders a side-on strip per clip to `Saved/Screenshots/Blender/clips/` and fails on a check. The manifest's
`AuthoredThug` (ue4 chains) and `AuthoredKate` (uefn chains) sources are `mixamo_fbx` with `"rebuilt": true`: a file
whose size or time changed is imported again, and its retargets and montages follow (`HawkeyeFile` tag); a changed
`Skeleton.fbx` clears the source's folder. `create-content.ps1` runs them (`import_authored_clips.py`, before
`create_combat_anims`). Not built: one "missing" line per source and the game plays what it did before.

```powershell
& "C:\Program Files\Blender Foundation\Blender 5.3\blender.exe" --background --factory-startup --python-exit-code 1 --python Tools\Blender\build_clips.py -- [--only getup_back,hit_heavy]
```

**How a clip is keyed.** Tracks per parameter (pelvis place and turn; chest on the pelvis and neck on the chest, spread
down the spine and neck bones; clavicles; hand and foot targets with elbow and knee poles, solved as two-bone IK; hand
and foot turns; a foot placed by its ball; toe bend; finger curl), monotone cubic between keys so nothing overshoots by
accident: overshoot and settle are keys, arcs are via keys. The idle (frame 0 of the game's idle) is the base, so twist
bones and fingers look like the game's; the last frames cross-fade into it exactly. Checks per frame: a planted ankle or
ball moves under 0.6 cm, no bone turns over 24 degrees a frame (the bow's string arm is allowed 45: a real draw swings
the elbow 50 cm in 0.2 s), no wrist inside the torso, no joint under the floor.

| Clip | Rig | Role | Length | What |
|------|-----|------|--------|------|
| GetUp_Back | ue4 (thugs, archers) | GetUp | 1.6 s | chin tucks, sits up with the right hand pushing behind, heels drawn in to the stance, hips rocked over the feet into a crouch, up a touch tall, settle |
| GetUp_Front | ue4 | GetUpFront | 1.8 s | hands under the shoulders, chest up, onto hands and knees, left foot steps through, rises off it with the right toes tucked, right foot steps up |
| HitFront/Left/Right_Light | both | HitFront/Left/Right | 0.53 s | head and chest snap away from the blow, the far foot steps out, the near follows, back past upright, settle |
| HitHeavy_Stagger | both | HitHeavy | 0.73 s | thrown back, shoulders up, knees buckle, two steps back, forward past upright, settle |
| BowDraw, BowAimIdle, BowFire | uefn | (out of the set) | 0.36, 2.0, 0.5 s | nock to anchor, a breathing hold, release and follow-through back to the nock |
| Takedown_Victim | ue4 | TakedownVictim | 1.73 s | a jolt, hands up to her arm, head back; heels go, dragged down onto his back, limp; holds its last frame |
| Takedown_Attacker | uefn | TakedownAttacker | 1.73 s | steps in, right arm across his chest, left hand on his side; sits back, drags him down in two steps each way (root motion, about 95 cm), crouches over him, lets go, stands |

The takedown pair is authored in one frame: her capsule at the origin, his 55 cm ahead, both facing the same way. His clip
is built first (the ue4 rig comes first) and her hands are keyed on his solved collarbone and ribs frame by frame,
divided by her driver's 0.93 scale. Her root carries the drag (`root_pos`: `clipkit` solves her other places relative to
it and keys the armature object, which is the root bone in the FBX); her montage takes root motion. Strips:
`takedown_paired.png` and, for comparison, `takedown_before.png` (she stands still while he is thrown away).

The hit clips start with the whole body moved toward the blow by the shove the game gives (light 10 cm, heavy 28 cm at
500 cm/s) and step back onto the capsule, so the feet stay put in the world while the shove slides the capsule.

**What ships and what does not** (strips from `Hawkeye.Screenshot.Clips`, `Saved/Screenshots/Clips/`): the get-ups (there
were none), the heavy stagger (new), Kate's and Clint's light hits (Sparrow's baked additive reactions put her in Sparrow's
own bow stance, arms out, for half a second: `kate_hit_front_before.png`), the thugs' side hits (same fault,
`thug_hit_left_before.png`). The thugs' front hit stays Mixamo's Receive Punch To The Face (mocap with the guard up reads
better than ours, `thug_hit_front_before.png`; ours is imported with `"in_set": false`). The bow set stays Sparrow's: ours
takes its anchor and nock from Sparrow's poses (where the game's bow and aim offset expect the hands) and in the strips
is the same motion faster (`kate_bow_draw.png` against `kate_bow_draw_before.png`), not better; it is imported with
`"in_set": false`.

**Get-up in the game.** The capsule is moved and turned so the clip's first frame lies on the body (the pelvis's turn
picks back or front, `gameplay-semantics.md`, "Authored clips"); out of the knockdown clip the mesh eases from where it
lay onto the capsule over 0.3 s, out of the ragdoll the physics weight falls 1 to 0 over 0.3 s. Strips:
`getup_from_clip.png`, `getup_from_ragdoll.png`.

**Ledge clips (2026-11-25; phase 2 built them, imported them and read the strips).** Phase 2 found that `create_combat_anims.py`'s `ROLES`
list did not name the ledge roles, so `DA_AnimSet_Kate` never held them and none of the authored clips played in phase 1's design:
the dismount and the catch were the sample's clips under the IK. Now the five roles are in `ROLES` (the vault's plant stays out by
`in_set: false`); `import-anims.ps1` fills them (41 roles) and `LogHawkeye: ... ledge clip <role> (<montage>) at rate` names each play.
What phase 2 changed in the clips: the climb up's pelvis now goes to the hips-under-the-top position with the chest on the edge and then
stands (it dived 117 cm below the floor); the dismount's hands rest in front of the chest while the lip is out of reach and go to it as
it comes within reach (they were an arm flung out at the lip); the hang drop is 1.6 s, the push-off then her falling feet first with
the arms down and a little out and the knees soft, so the fall after a let-go no longer goes into the sample's arms-wide fall pose
(a landing's own montage ends it). `clipkit.py` takes a per-clip `pelvis_cm` limit. Six clips on Kate's rig
(`uefn`) in `build_clips.py` ("the ledge clips"), for the ledge body (`gameplay-semantics.md`, "Ledge body"). All in place (no
root motion): the capsule's path is the game's and the rig's origin is the capsule's bottom, so the ledge is keyed where the game
puts it relative to her (hang: edge top 145 cm over the origin and 36 cm ahead, wrists a shoulder width apart; the numbers are
game cm over the driver's 0.93). The visible body's IK finishes the contact, so a hand here only has to be close; hand turns are
the idle's (the IK turns the hand), the elbows are poles out and back.

| Clip | Length | Role | What |
|------|--------|------|------|
| Ledge_DropToHang | 0.5 s | LedgeDropToHang | crouch at the parapet, hips over it as she turns, lowered to the hang; the hands follow the parapet's top as the game's path and turn move it past her (`_dismount_edge_local`), pointing at what they cannot reach yet; ends on the hang's first frame exactly |
| Ledge_Catch | 0.45 s | LedgeCatch | arms up for the edge, the hands close, the body's weight comes on (arms out straight, feet out behind), swings in, knees bend, toes meet the wall; ends on the hang |
| Ledge_HangIdle | 1.6 s | LedgeHangIdle (loops) | the hang: near-straight arms, toes on the brick, a slow sway |
| Ledge_HangDrop | 0.5 s | LedgeHangDrop | a small pull, the hands come off the edge, a push off the wall, arms swung forward and the legs straightened under her; ends exactly on the fall loop's first frame |
| Ledge_FallLoop | 1.0 s | LedgeFallLoop (loops) | the fall after a let-go (and an instant drop): standing height with soft knees, torso 8 degrees forward, head down at the landing, the arms forward and up (hands in front of the shoulders, elbows down, not out), the hands circling 6 to 8 cm against each other, the legs pedalling, the hips and chest swaying a few degrees |
| Ledge_FallBrace | 0.5 s | LedgeFallBrace (holds) | from the fall to the knees up and forward, the hips dropped, the arms down and a little out; plays when a trace down finds the ground within 0.22 s of the fall speed |
| Ledge_ClimbUp | 1.0 s | LedgeClimbUp | pull, chest over with the palms pressed back onto the top, a knee up, stand (the last 0.25 s cross-fade into the idle); the ledge sinks past her and comes back behind her as the climb's path does (`_climb_edge_local`) |
| Vault_Plant | 0.6 s | LedgeVaultPlant | a speed vault over a 90 cm top at a run: the torso pitches to 42 degrees, the right hand plants on the top ahead (reach limited to the arm), the legs swing together to the left beside the hips (feet at hip height), the left arm tucked in front of her (out to the side read as flung), then she lands running (the lead foot first) |
| Vault_Crouch | 0.6 s | LedgeVaultCrouch | the same plant and swing, landing in a crouch with both hands ready (a slow entry: the vault takes 0.45 s or more) |

The clips are made with `Tools/Blender/build_clips.py` as the others are (`--only ledge_hang_idle,ledge_catch,...`; the function names),
the pose checks run with one change: a clip may say how low it goes (`extra["floor_cm"]`, -45 cm for these: the arms are straight
in a hang 145 cm under the ledge, so the body is under the capsule's bottom). Checked in Python only so far, with Blender and the
rig stubbed (the clips' ends meet the hang pose exactly; the worst per-frame hand jump is 26 cm on the dismount, 37 on the climb
and the vault's 16): the bone-turn and torso checks have not run, and will say which of the `pop_degrees` need raising or which
hand keys need softening. The first Blender run also settles the hand and foot turn signs, which were worked out on paper.

`import_combat_anims.ROLE_DEFAULTS` has the six roles (DefaultSlot, in place; the hang idle loops) and `manifest.json` six lines
for `AuthoredKate` (Kate only). `UParkourComponent` plays them through the owner's combat anim set (`PlayLedgeRole`); a set without them
leaves the sample's clips. `ABP_Kate_Body` has the Hawkeye Ledge IK node after the Kawaii nodes (`create_kate_look.ensure_anim_bp(...,
ledge_ik=True)`, from `import_bodies.ensure_kate`).

**Phase 3 (2026-11-25, from the player-camera night footage).** (1) The fall after a let-go was the hang drop's last pose held for a second
(elbows wide, hands at the hips, knees in a half squat: the pelvis was keyed 60 cm under standing height, the hang's). The drop is now
the 0.5 s push-off that ends on `Ledge_FallLoop`'s first frame; `UParkourComponent::BeginLedgeFall` (both let-goes) starts the loop as the
push-off's last 0.15 s begin and `TickLedgeFall` plays `Ledge_FallBrace` when a trace down finds ground within 0.22 s of the fall's speed
(over 250 cm/s); it ends when she grabs, hangs, starts a move or lands (the landing's own montage takes over). (2) Vault: both vault clips are
in the set and the vault routes to them (`PlayAuthoredMoveClip`; the guard's leap keeps the sample's pose). `Vault_Crouch` plays at 0.45 s or more.
The capsule's path is unchanged (Rise = Height + 10: its bottom just clears the parapet; a lower arc, tried first, put the capsule into the parapet and
the vault ended on top of it). So the clip is keyed with the body far under the capsule over the top: in world heights (hips 27 cm over the top, the hand on it,
the feet at hip height beside her) and taken into the capsule's frame by the capsule's height (`zb`), floor_cm -110. It is keyed against the rig solved without
its arms (the shoulders' real place), the elbows poled back so a hanging arm never flips.

**The ledge strips** (`Hawkeye.Screenshot.Traversal`, written, not run): `roof_dismount`, `ledge_catch`, `hang_drop`, `climb_up`, `vault`,
`hang_hands` and `<name>_wide` of each, in `Saved/Screenshots/Traversal/<name>_<side | three_quarter>_<off | on>.png`, on a stage built
200 m over the district at a fixed 1/60 s a frame, with the ledge body off (`hawkeye.LedgePose 0`) and on; eight frames of 1/30 s
around where the hands reach the edge (`hang_hands`: eight of 1/10 s once it has settled, the camera 110 cm from her hands).

## Hands on the bodies (fixed 2026-11-24)

Symptoms: fingers splayed straight like claws, wrists cocked, palms the wrong way, arms held out from the body.
Causes, all in the retarget (`create_kate_look.ensure_retargeter`, which builds every body's `RTG_<Who>_To_Body`):

1. **Retarget pose.** The bodies are built in a T-pose (palms down, straight fingers); the drivers' reference is an
   A-pose with relaxed fingers. The engine's `auto_align_all_bones(CHAIN_TO_CHAIN)` (`IKRetargeterPoseGenerator.cpp`)
   turns each bone by the shortest rotation onto the source chain's direction: it never matches roll, skips the last
   bone of every chain (the hand, each finger's third joint) and single-bone chains (clavicle). So the body kept its
   T-pose wrist roll and straight fingers.
2. **Hand goals copied the source bone's absolute rotation** (`blend_to_source_rotation` 1.0, `IKChainsOp.cpp`
   `CalculateGoalRotation`): a different roll on a skeleton with other bone axes. Now 0: the hand goes to the driver's
   hand but turns by its change only (the bow grip and a hang's hands still sit on the driver's).
3. The finger chains were mapped all along (`ue4` / `uefn` chain maps, `auto_map_chains`); that was not the problem.

Fix: `Tools/Editor/_handpose.py` (plain maths, `Tools/test-handpose.ps1`) builds a frame per arm bone and finger joint
from the joint positions on both skeletons (direction along the bone, roll from the palm normal, which both give by the
same formula; a finger's last joint carries on the bend before it), turns each target bone onto the source's frame and
writes the local offsets into the `HawkeyeAligned` retarget pose (global = parent global * local ref * offset). The
clavicles are left alone. `HAND_CURL_DEGREES` (0) adds bend to fingers a driver holds straighter. `Tools/Editor/probe_handpose.py`
prints the frames each skeleton gives. `verify_content` (`check_hand_chains`) fails if a body retargeter has an unmapped
finger chain or no offsets on the hands. `Hawkeye.Screenshot.Bodies` ends with `hands_<who>_<rest, fist, bow>.png`
close-ups (thug, heavy, Clint, Kate).

**Thugs and the heavy, second pass (2026-11-24).** The thug's and heavy's hands still read badly after the first fix: the
UE4 mannequin that drives them rests its hand with the index and thumb hooked, and its hands hang beside a narrower body's
thighs, so on the superhero body (shoulders far wider than the hips) they hovered in front of the groin. Three changes, all in
`RTG_Thug_To_Body` (the heavy shares it), none touching Kate's or Clint's retargeters (their stamps are unchanged):
1. `_handpose.RELAXED_HAND` replaces the fingers' bend (and a little thumb) with a table: the four fingers curl together, a little
   more toward the little finger (about 28 / 42 / 26 degrees at the index's three joints), each finger keeping its own spread. The
   rest hand is a loose fist rather than a claw; the index and thumb still nearly meet on the right hand.
2. `_handpose.adduct` swings the arm chain 14 degrees toward the midline about the shoulder (`THUG_ARM_ADDUCT_DEGREES`).
3. The hand goals' translation blend is 0 (`THUG_HAND_TRANSLATION`; `create_kate_look._hands_to_driver(translation)`): the arms'
   own forward kinematics place the hands, not the driver's hand positions. Strike hands are the bow-IK graph's and the bat is
   placed by `AThugCharacter`, so nothing needed the driver's exact hand place on a thug. Kate and Clint keep 1.
Still off: the rest pose is the template's bent-elbow guard, so the hands sit at the belt in front, not on the thighs; the
heavy's bat is drawn in front of the hand, not through the fist (its grip is `BatGripFromEnd`, not tuned here).
**The grey checkered block on the thug's right wrist** was the gunner's pistol (`PistolComponent` and `PistolGrip`, engine cubes
at his side): the cube's own material is the editor grid. `AThugCharacter::TintProps` gives the pistol, its grip and the held bat
the engine's shape material in a colour (`PistolColor` gunmetal, `BatColor` dark wood). The riot shield already did this.

**Thugs and the heavy, third pass: our own hands (2026-11-24).** Two passes of retarget offsets did not fix the hands, so the
visible thug and heavy no longer take their fingers (or their rest arms) from the retarget at all. `ABP_Thug_Body` is now
Retarget Pose From Mesh, to component space, one **Hawkeye Body Pose** node (`FAnimNode_HawkeyeBodyPose`,
`Player/BodyPoseNode.cpp`; editor graph node `UAnimGraphNode_HawkeyeBodyPose` in `Source/HawkeyeAnimEditor`, an
`UncookedOnly` module (an `Editor` one is not loaded in the standalone `-game` runs and left the AnimBP's graph broken there; the
game module finds the class by path); built by
`UHawkeyeRetargetGraphBuilder::BuildRetargetWithBodyPose`, stamp `body-pose-1`), back to local. Its parent class is
`UHawkeyeBodyAnimInstance`, which each frame asks the owner (`AThugCharacter::GetBodyPoseWish`) what the hands are doing and
eases it into alphas over 0.12 s. Kate and Clint have no such node. Since 2026-11-25 the thugs' faces and builds vary (`BodyLookVariants`, `blender-pipeline.md`, "Enemy heads and bodies"): the same skeleton, retargeter and AnimBP for every variant.
- **Fingers.** Every finger joint is written outright: local rotation = swing about the palm normal (thumb only) * curl about the
  finger's own flexion axis * the joint's reference rotation, all axes taken from the body's bind pose (the palm frame is the
  same formula as `_handpose.palm_frame`, `HawkeyeBodyPose::ComputeHandFrame`). The table is `HawkeyeBodyPose::Angles`:
  Relaxed (staggered 14-32 degrees at the knuckle), Fist (78-88) and Grip (58-70, looser), blended Relaxed to Fist to Grip. The
  body's thumb already points 32 degrees off the fingers and 27 out of the palm plane at its reference pose, so the relaxed thumb
  is that (curled a few degrees: alongside, clear of the index). A closed thumb is not curled (rotating it about the palm normal
  stuck it out whichever way it turned, two attempts): each of its joints is aimed at a direction in the hand's axes
  (`ClosedThumbDirection`), lying across the curled fingers.
- **State.** Fist: both hands during a swing, except one holding a prop. Grip: the right hand while the pistol or the bat is
  held. Nothing held and not swinging: Relaxed. An unarmed thug (`Weapon` Fists) holds nothing.
- **Rest arms.** `ArmRest` weight (1 standing at ease, falling to 0 by 150 cm/s, 0 while swinging or the pistol is raised, in a
  knockdown or collapse) turns the upper arm to hang down 20 degrees out from the body (10 until 2026-11-24, when the fingertips sank into the
  tracksuit trousers; `hands_thug_empty.png` against `hands_thug_empty_before_arm.png`: the hand now hangs clear of the hip by a
  hand's width), and the forearm on it bent 6 degrees forward (`ArmRestDirections`), about the shoulder and elbow, in component space.
- **Rest legs (2026-11-25).** By the same `ArmRest` weight each leg is turned in about the hip, in the frontal plane, until the foot
  stands at 0.92 of the hip's distance out from the midline (`LegAdductionTurn`, never more than 24 degrees, never outward), the foot
  and toes turned back so they stay flat (`EvaluateLeg`). The retargeter's IK puts the feet where the driver's combat idle has them,
  35 cm apart under hips 22.8 cm apart, and neither the bind pose nor a retarget-pose offset on the thigh changes where IK puts a
  foot (a 3 degree thigh offset in the retarget pose, tried first, moved nothing). The Bodies shot measures it:
  `feet 19 cm apart (hips 21)` for a thug at rest, and fails past 1.35 times the hips; Clint, whose retarget is the plain one, stands
  43 cm apart (hips 22) and is not checked. Walking fades it out with the arms (full at rest, nothing by 150 cm/s).
- **Props.** The pistol and the bat follow the visible hand (`GetBodyHandGrip`), placed again from
  `OnBoneTransformsFinalized` so they do not lag a frame. The pistol's grip block centre sits in the fist (handle across the
  knuckles, muzzle along the fingers 18 degrees away from the thigh); a gripping wrist turns toward the bat's axis, up to 50
  degrees (the bat is held on a diagonal), and the bat passes through the fist centre. The pistol is now 18 x 3 x 3.2 cm with a
  3.2 x 3 x 10 cm grip (the old block's child offset was written in the wrong units and left a 10 cm slab on the wrist).
- **The grey block of the second pass** was the gunner's pistol grip; the tint code was in, but `hands_thug_rest.png` of that pass was
  shot before the build that had it (the image is older than the commit).
- **Tools:** `Tools\shoot-bodies.ps1` builds and takes the `Hawkeye.Screenshot.Bodies` shots (the hand close-ups are re-aimed every frame
  after the pose settles: they used to be aimed at the spawn frame's hands, a raised arm). `hands_thug_empty.png` is an unarmed thug;
  `hands_thug_rest.png` the gunner with the pistol, `hands_heavy_rest.png` the heavy's bat hand (seen from his right, the shield hides
  the front); `*_before2.png` are the second pass's shots. Still off: the pistol shows only its grip block and a stub of slide beside
  the fist; the bat is held in front of the fingers, not woven through them; the thumbs and fingers have no finger-to-finger contact.

**Gut reactions, 2026-11-24.** `HitGut_Light` folded the chest 32 degrees, so on a light punch the head went 34 cm toward Kate (head
overlapping hers, `contact_light_after2.png`) with the capsule only 1.9 cm back. It is now a flinch (`build_clips.hit_zone`, Gut: fold
factor 0.4, about 10 degrees, the hips 8 cm back): in `contact_light_after3.png` his head is within 5 cm of where it began, his capsule
ends 6.2 cm back, no overlap. The distance between the two roots still reads 72 to 66 cm because Kate steps in for the blow (she was
already at 68 before it landed). The heavy fold is 1.3 (was 1.8): head up to 54 cm off over her shin at the deepest frame
(`contact_heavy_after3.png`), no overlap with her torso; his capsule ends 3.2 cm back (the game's heavy knock-back of 28 cm is not
what this strip measures).

## The weld clips (written 2026-11-23; not yet built or seen)

`build_clips.py` also makes, per rig, six hit reactions by where the blow landed and how hard (`HitHead_`, `HitGut_`, `HitLegs_` x
`Light`, `Heavy`: roles `HitHeadLight` ... `HitLegsHeavy`), and on Kate's rig a jab, cross, hook and front kick (`Strike_Jab`,
`Strike_Cross`, `Strike_Hook`, `Strike_FrontKick`), all at rate 1.0.
- A reaction's first frame IS the damaged pose (the game starts it at `hit_peak_s` 0 with no blend after the freeze begins), then it
  settles slowly. The capsule slides back by the eased knock-back (10 / 28 cm over 0.3 s, `knock_share`), so a planted foot holds
  `PLANT_HOLD` (55%) of the slide in the world and a stepping foot steps back with it.
- A strike has 4 to 6 frames of anticipation, 4 of swing, a 3-frame hold at full extension from the frame its hit window opens
  (`hit_s`), a follow-through and a recovery, the weight through the hips, and an early step-in on `root_pos` (the motion warp scales
  it: the peak speed stays under the 1,100 cm/s cap because the step is done by 0.2 s). Hands are keyed as offsets from the solved
  shoulder (`shoulder_keys`).
- Each clip's measures go into `clips.json` "extra" (`hit_peak_s`, `knockback_cm`, `contact_cm`, `contact_height_cm`, `wide`);
  `create_combat_anims.py` bakes them into the anim set's `ClipData` (a manifest line's own key of the same name wins). `contact_cm`
  is the striking bone's flat reach from the capsule at the hit frame plus the knuckles (9 cm) or toes (14) and the body's depth to the
  bone the warp aims at (16), times the rig's scale (Kate's 0.93).
- All sixteen manifest lines are `in_set: false`: imported, not worn. A clip goes in only when its strip beats the one it replaces
  (`Hawkeye.Screenshot.Clips`: `<clip>.png` against `<clip>_before.png`; `contact_light_<tag>.png` and `contact_heavy_<tag>.png`, eight
  frames round a live blow, `-ContactStripTag=before` on the old build). Say which did not beat theirs.

## Traversal body pose (written 2026-11-23, seen and tuned 2026-11-24 from the strips; the rules are `gameplay-semantics.md`, "Traversal body pose")

Why: the sample's motion matching knows nothing about a zip. The movement component carried the capsule along a line, the
only rotation was the yaw to the landing, the sample showed a generic pose, and a roll was a scripted spin. The pose below
answers it per phase (Insomniac's way: authored or chosen per traversal phase, physics only has to look plausible) and the
engine is not asked to simulate her.

**Route.** The bow IK post-process graph (`ABP_BowIK_Post`, `UHawkeyeBowIKGraphBuilder`, `UHawkeyeBowIKAnimInstance`): it
already runs on Kate's and Clint's driver after the sample's graph, its hand IK already takes strike and hang hands, and the
visible body inherits it through Retarget Pose From Mesh exactly as it inherits the roll. A new AnimInstance base or a
Control Rig would have meant a second post-process on a mesh that takes one. The graph is now (build tag `bow-ik-5`):

    ... -> to component space
    -> ModifyBone pelvis (rotation, offset), spine_01, spine_02, spine_03     (TraversalAlpha: skipped off a trip)
    -> hit lean, spine twist, neck twist                                       (the bow's and the melee's, as before)
    -> ModifyBone neck, thigh_l, thigh_r, calf_l, calf_r                       (TraversalAlpha)
    -> TwoBoneIK hand_l, hand_r                                                (the same IKs; the strike, hang and traversal take them in turn)
    -> to local space

All of them additive in component space, `Rotation`/`Translation` pins from `UPROPERTY`s on the instance
(`TraversalPelvisRotation`, `TraversalSpine01Rotation`, ... `TraversalCalfRRotation`, `TraversalAlpha`). Each update the
instance steps the owner's `FTraversalPoseState` (`AHawkeyeCharacter::GetTraversalPoseState`, input from
`BuildTraversalPoseInput`: the movement's zip flags and hop fraction, `Velocity`, the rope from the anchor's marker, nothing
written) and `ApplyTraversalPose` turns its actor-frame result into component space (`FTraversalPoseRules::ActorToComponent`:
the same world turn, written in the mesh's axes; offsets divided by the mesh's 0.93 scale). The hands go through the strike
channel's slots in `ComposeOutputs`, only while no strike has the arm and the bow is under half up. Everyone else on the
graph (thugs, the archer, Clint standing) has no `AHawkeyeCharacter` pose state and `TraversalAlpha` 0: nothing is
evaluated. Teleports (a jump past what she can run in a frame) reset the state; the engine resets the dynamics nodes itself.
`hawkeye.ZipPose 0|1` is a console variable read by the instance.

**Heavy landings play at 1.6** (`HeavyLandingRate`, `FTraversalPoseRules::LandingClipRate`; 2026-11-24): the sample's crouch at the bottom held about 0.6 s. In the `land_heavy` strip (frames at 0, 0.1, 0.2, 0.3, 0.4, 0.55, 0.75, 1.0 s from touchdown, `_on`) the deepest crouch is at 0.4 to 0.55 s and she is rising by 0.75, a hold of about 0.3 s. Not fixed: the clip's first 0.3 s after the capsule is down still shows the legs tucked with the feet 10 to 15 cm up (its approach pose), which reads as floating. `land_heavy_side_on_before.png` is the old strip. `BeginLandingPose` now logs the landing's clip and rate at Log level.

Landing and fall clips are montages in the combat set (`ECombatAnimRole::LandStandLight .. LandSprintHeavy, LandRoll,
LandStumble, FallLoop`; `UCombatAnimSet::LandingRoleFor`). `Tools/Editor/create_traversal_anims.py` (a `create_all` step before
`create_combat_anims`) copies the sample's clips from `/Game/Characters/UEFN_Mannequin/Animations/Jump` (already on the
mannequin's skeleton: no retarget) to `.../Animations/Traversal` with root motion off and the root locked (the setting the dodge
and hit clips use, `apply_root_motion` in the import script), makes an `AM_<Role>_<Lfoot|Rfoot>` montage of each in the folder
`create_combat_anims` reads, and logs the pelvis at the start, middle and end with the root locked (a warning when it is
over 40 cm off the capsule or under 40 cm high: the lock did not take). The anim set cycles the two feet. Without the sample
in Content the script logs one line and the legs' absorb draws every landing.

Checked on 2026-11-24 with the strips: the hand reaches the rope (the yellow line starts on the drawn hand, `Hawkeye.Screenshot.Traversal`
tests it under 45 cm), the root lock keeps the landing clips on the capsule (the pelvis lines in the content log stay under 30 cm off
and over 70 cm high), and the pelvis and spine turns read on the built Kate. Things found by running it: the sample's landing clips
run 2 to 4 s (they carry on into the stand or the run), so `create_traversal_anims.py` keeps the first 0.55 s of a light one and
0.8 s of a heavy one (`end_s`) and blends out into the locomotion underneath; **the sample has no roll and no stumble clip**
(`M_Neutral_Jump_F_Land_Roll*` and `_Stumble*` do not exist), so the roll stays the scripted spin and the stumble the legs' absorb
(`LandRoll` and `LandStumble` stay empty roles); and a skeleton the costume adds bones to must be saved (`import_bodies.import_built`
does, and imports again when the skeleton lacks a bone the .glb has), or the AnimBPs compile without them.

Design 2026-11-24: Load (the crouch before the hop, while the arrow flies), Brace (the last 0.3 s of a pull) and the arrival
classes by zip length, slope and the stick, not line speed: `gameplay-semantics.md`, "Traversal body pose". A pull-up pose for a
climb to a parapet was not made: the two-step reads as catching her weight there. No authored clip was made either (a launch, a
release tuck, a parapet pull-up): the procedural pose does the launch, the brace and the arrival well enough in the strips, and an
authored clip would have had to beat that.

`Hawkeye.Screenshot.Traversal` (standalone `-game`): per scenario and mode, `Saved/Screenshots/Traversal/<scenario>_<side |
three_quarter>_<off|on>.png`, the frames in `Frames/`. A yellow line is drawn from the drawn hand to the rope's end.

## Secondary motion (Kawaii Physics; built, run and looked at 2026-11-24)

Kate's ponytail and quiver swing, simulated by the Kawaii Physics plugin (pafuhana1213/KawaiiPhysics, MIT, vendored at
`Plugins/KawaiiPhysics`, pinned to v1.21.0, commit 0035f52; `docs/CREDITS.md`). It replaced the engine's AnimDynamics, which
put NaNs in `ponytail_01` on the first frame (a hard assert in `BonePose.h`) as a chain, as single bodies and as one body, with
and without every constraint, while the same graph with no node was clean; the cause was never found. **Status: on.** The plugin
built on 5.8 unchanged (editor target, 70 s, including its own Private/Tests), `Hawkeye.SecondaryMotion.*` (4) pass, the AnimBP
rebuilds with 2 Kawaii Physics nodes and no rejected chain, and standalone `-game` runs (the traversal strips, `t.MaxFPS` 10 and
144, a teleport mid-run, time held at 0.0001) never hit the `BonePose.h` assert. The module-type trap that broke the body pose
node does not apply: the plugin's `KawaiiPhysics` module is Runtime (the node struct) and `KawaiiPhysicsEd` is `UncookedOnly`
(`PreDefault`), which loads in the standalone editor-binary runs; `Hawkeye.Build.cs` links `KawaiiPhysics` only in editor
targets because only the builder (WITH_EDITOR) names its struct, and the game target builds the plugin by being enabled in the
`.uproject`. Strips: `tail_run` (sprint 1.6 s, stick released, stopped dead), `cut` (teleport 12 m back mid-run) and the
existing `pull` and `land_heavy`; `-HawkeyeTraversalOnly=a+b` and `-HawkeyeTraversalPoseOnly` limit a run.

What the strips show (the starting numbers were kept; nothing needed tuning): running, the ponytail streams back and up,
a few degrees off the pose's line; on a dead stop it swings forward over the shoulder and up and then hangs, settling inside
the 0.3 s between strip frames (the stop is instant, the harshest case); along a zip pull it trails back along the body; on a
heavy landing it is flung up past the head at the touchdown and drops by 0.55 s. A teleport shows no whip in the frames after
(the engine's reset). It never passes through the head, neck or back in any frame; the rest ponytail hangs straight down the
back, not displaced. The quiver stays within a few degrees. Not seen: a slow oscillation count (the strip frames are 0.1 s
apart at best) and the ponytail's behaviour in a long turn; the hem and the bow on her back are still rigid.

Where it lives:

- `Source/Hawkeye/Player/SecondaryMotion.h/.cpp`: `FHawkeyeDynamicsChain` (one chain: root bone, optional last bone,
  damping, stiffness, world damping, radius, limit angle, gravity scale, teleport thresholds, warm-up steps, step rate,
  colliders), `FHawkeyeDynamicsCollider` and `HawkeyeSecondary::` (which bones a chain simulates, `DescribeProblem`, where a
  collider sits in a bone's axes). No editor, no plugin: the game target links it and `Hawkeye.SecondaryMotion.*` tests it.
  The structs kept their AnimDynamics-era names so the scripts did not change.
- `UHawkeyeRetargetGraphBuilder::BuildRetargetWithDynamics` (`RetargetGraphBuilder.cpp`): the look's pose goes to component
  space, through one Kawaii Physics node per usable chain, and back to local space. Editor builds only. The plugin's anim graph
  node class (`UAnimGraphNode_KawaiiPhysics`, in its `KawaiiPhysicsEd` module) is **not exported**, so the builder cannot link
  it: it loads the module, finds the class by name (`/Script/KawaiiPhysicsEd.AnimGraphNode_KawaiiPhysics`), creates the node
  through `FGraphNodeCreator<UAnimGraphNode_Base>::CreateNode(false, Class)` and writes the plugin's runtime struct
  `FAnimNode_KawaiiPhysics` (exported, `Hawkeye.Build.cs` depends on `KawaiiPhysics`, editor targets only) through
  reflection, as the Retarget Pose node already is. A chain `DescribeProblem` rejects (a missing bone, fewer than two bones,
  a zero-length, non-finite or unscaled bone, numbers out of range, a collider on a missing bone) is left out with a line in
  `LogHawkeye` (Log for a missing bone, Warning for anything else), so a Kate built before the bones existed is the plain graph.
- `import_bodies.KATE_DYNAMICS` is the list (Python names: `bound_bone`, `chain_end`, `damping`, ..., `colliders` with
  `bone`, `radius`, `length`, `offset`, `axis`); `create_kate_look.ensure_anim_bp(dynamics=...)` turns it into structs and
  stamps the AnimBP with it, so a changed value rebuilds the graph and an unchanged one does not (idempotent).

What each chain is (starting values, to be tuned by eye):

| | ponytail `ponytail_01..05` | quiver `quiver_01..02` |
|---|---|---|
| damping / stiffness | 0.15 / 0.08 | 0.25 / 0.6 |
| limit angle | 60 degrees | 12 degrees |
| world damping (location / rotation) | 0.88 / 0.85 | 0.9 / 0.9 |
| gravity | 1.0 (980 cm/s^2, world down) | 0.5 |
| bone radius | 1.2 cm | 1.0 cm |
| collision | Head sphere (r 6.5, 8 cm up), neck_01 capsule (r 5, 6 long), spine_03 capsule (r 11, 12 long) | none |

Component-space simulation with the plugin's own reading of her motion: each frame the bones are moved back by
`(1 - WorldDamping)` of the mesh's travel and pulled toward the pose by `Stiffness` per step, so an 18 m/s sprint or zip (30 cm a
frame at 60 fps) saturates at the limit angle and streams the ponytail back, a walk gives a few cm of trail, standing gives
gravity's sag (a few cm for the ponytail; these are worked out from the plugin's code, not seen). The plugin sub-steps at a fixed 1/60 s (`UKawaiiPhysicsDeveloperSettings`,
`bUseFixedSubstepping` true, 4 steps at most, its defaults, nothing in `Config/`), so the look does not depend on the frame rate.

Guards against the first-frame NaN, all in the plugin's own fields plus the builder's checks:

- **Reference-pose checks first.** `DescribeProblem` refuses a bone on its parent (a zero-length direction), non-finite or zero
  scale poses, and any number out of range, so the plugin is never handed a pose it normalises to NaN.
- **Zero delta time.** The plugin returns from a step with `DeltaTime <= 0` (the pose passes through untouched) and divides
  velocity by `max(DeltaTimeOld, KINDA_SMALL_NUMBER)`, `DeltaTimeOld` starting at `1 / TargetFramerate`; the builder clamps
  `TargetFramerate` to at least 1.
- **Warm-up and reset.** `bNeedWarmUp` with `WarmUpFrames` 6 settles the chain under gravity before it is seen, and
  `bUseWarmUpWhenResetDynamics` repeats it after every `ResetDynamics` (the engine calls it on a teleport, so
  `NotifyTeleported()` and a fast-travel cut restart the chain from the pose, not from a whip).
- **Teleport thresholds.** A mesh move over 150 cm in one frame, or a turn over 45 degrees in one frame, is a teleport to the
  plugin: that frame the bones do not follow it. A zip's 18 m/s is 30 cm a frame at 60 fps and a snap turn is
  well over 45 degrees, so zips and runs trail and cuts do not.

Collision shapes are written once, in the skeleton's reference-pose axes with Z up ("8 cm up the head", capsule along Z), and
`HawkeyeSecondary::ColliderInBoneSpace` turns them into the plugin's bone-space offset and rotation from the bone's reference
pose, so the way the Blender export pointed a bone's axes does not matter. A shape that overlaps the ponytail's own rest pose
pushes it off its animation every frame: if the rest ponytail is displaced, shrink or move the Head sphere first.

Not done / not known (read before changing): the plugin's v1.21.0 README states 5.3 to 5.7; it compiled and ran on 5.8
without a patch; the jacket's hem, the
bow on her back (a static mesh on the driver's holster socket: it rides the driver rigidly, simulating it means a spring on
the holster socket, not a node here) and the plugin's `BoneForwardAxis` (only the dummy-bone tip reads it; `DummyBoneLength` is
0 because both chains end in real leaf bones) are left alone.

Bones: the built Kate had none worth simulating (her ponytail's 26 locks were skinned to Head and neck_01, the quiver to
spine_02 and 03). `Tools/Blender/build_kate.py` adds, with `costume.Body.add_bone`: `ponytail_01..04` under Head down the
ponytail's axis (joints at 0.12, 0.35, 0.60 and 0.85 of its length, the head holding the tie) and a leaf `ponytail_05` so the
last real bone has a direction; `quiver_01` under spine_03 from the quiver's mouth to its bottom and its leaf
`quiver_02`, the quiver's own pieces (not the strap) handing weight from the spine to it down their length (85% at the bottom).
`--no-dynamics-bones` builds the old skeleton and weights. **It must be built into a staging folder**: `--out
<staging>\built\kate` (the engine import reads `<assets root>\built\kate`; point `HAWKEYE_ASSETS_ROOT` at a staging copy for
the import until it has been looked at).

Cost: the same `Hawkeye.Lap.EastVillage` lap through `measure-perf.ps1`, one binary, Kawaii on then switched off with
`-dpcvars=a.AnimNode.KawaiiPhysics.Enable=0`: 6.5 ms average both ways (p95 8.6 / 8.5), game thread 5.0 / 4.9 ms, so the two
nodes cost about 0.1 ms on the game thread, inside the noise.

Checking it again: `Hawkeye.SecondaryMotion`, `create-content.ps1` (LogHawkeye: "2 Kawaii Physics node(s)"), then the strips with
`a.AnimNode.KawaiiPhysics.Debug 1` to draw the shapes.

## Known limits

- Kate's clip-timed lights land at 0.25 s, not the procedural 0.1 s (a clip needs a visible swing).
- Every second fists swing (Standing Melee Punch) still holds its first frame for 0.29 s of the 0.6 s
  telegraph (its wind-up is short); the first (Jab To Elbow) needs no hold. Leaving the punch out of the
  set (`"in_set": false`) would make every swing the jab, at the cost of variety.
- (2026-11-13: the thugs get up through `GetUp_Back` / `GetUp_Front`, "Authored clips".) Kate has no knockdown, so no
  get-up of hers.
- The aim offset under-pitched (Sparrow's CU and CD poses sit at +-90 on the axis but aim less far): at
  30 degrees up the nocked arrow pointed 24 up. Fixed 2026-09-29 by scaling the pitch input (1.57 up,
  1.85 down, +5; gameplay-semantics.md, "Aim offset pitch"): 30.6 up, -30.2 down, -1.5 level. Looking
  down the arrow now follows the view while it flies along a shallower line from the bow hand (the
  string hand sits 34 cm off that line with the correction at its 0.5 cap); it still flies to the
  reticle.
- The first 0.1 s of a draw is still a blend from locomotion: the bow turns up in the hand while the
  arms rise, and the nocked arrow points down across the body (28 degrees off) for a few frames.
- `bow_aim_half.png` (the BowIK still at half draw) shows the anchor: the draw clip is at its end
  after 0.5 s whatever the draw fraction, which is the new rule, not a fault.
- The clip set has no BowNock: after the fire clip the arms blend to locomotion and the bow goes on the
  back once the clip weight is under 0.05. There is no holster clip, so that last step is still a cut.
- Sparrow's `Knock_Bwd` is a standing knock-back (pelvis never drops), not a knockdown; it is not
  mapped.
