# Blender costume pipeline

Modelled clothes for Clint, the thug and the heavy, built by script in Blender 5.3 on the Quaternius body the game
already uses (`animation.md`, "Bodies"). Replaces the painted-on clothes (`M_BodyLook`) for whoever has a build on
disk. Built 2026-11-11. Kate's suit (on the pack's female body) followed on 2026-11-12 and is in the game the same day
(see "Kate" and "Unreal side").

## Running it

```powershell
$B = "C:\Program Files\Blender Foundation\Blender 5.3\blender.exe"
& $B --background --factory-startup --python Tools\Blender\build_clint.py -- [--no-render] [--no-export] [--views front,idle]
& $B --background --factory-startup --python Tools\Blender\build_thug.py  -- ...
& $B --background --factory-startup --python Tools\Blender\build_heavy.py -- ...
& $B --background --factory-startup --python Tools\Blender\build_kate.py  -- ...
```

About 15 to 30 s each on this machine, renders included. One Blender at a time; no Unreal needed. Flags:
`--assets-root` (else `HAWKEYE_ASSETS_ROOT`, `Tools\local.json` `assetsRoot`, `C:\Users\camer\code\hawkeye-assets`),
`--out` (else `<assets root>\built\<who>`), `--renders` (else `Saved\Screenshots\Blender`), `--views` (any of
`front, 34, side, back` at rest, `idle, arms_up, crouch, lunge, draw` posed, `head_front, head_back, feet, hand`
close-ups; default the first nine). `draw` is a full bow draw, side-on to a target on her left: bow arm out, draw hand
at the jaw, head turned. Kate's build also renders `kate_clint_scale` (1280x720): her beside Clint, both idle, she at
her driver's 0.93 scale and he at 1, Clint loaded from `built\clint\clint.blend`. Kate's run takes about a minute. Deterministic: same inputs, same meshes.

Each writes to `<assets root>\built\<who>\`:

| File | What |
|------|------|
| `SK_<Who>.glb` | one skinned mesh on the body's own armature (65 bones, same names and rest pose), all its materials and the generated normal maps embedded |
| `<who>.slots.json` | per material slot: colour, roughness, metallic, normal strength, base/normal texture file (next to the glb, or the pack's `Textures` folder), masked, glow. `import_bodies.py` builds the engine's instances from it |
| `<who>.blend` | the built scene (armature, mesh, studio) for looking at by hand |
| `T_N_<weave>.png`, `T_Hair.png` | generated tiling normals (twill, grain, knit, rib, tricot) and the tinted hair; Kate's `T_Hair.png` is a generated strand texture (ragged alpha at both ends) with `T_N_strand.png`, and `T_Brows.png` her tinted brows |

and renders `Saved\Screenshots\Blender\<who>_<view>.png` (Eevee, 1000x1000, orthographic). The export step re-imports
the glb and fails the run unless it has one armature with the body's bone names, one mesh and no unweighted vertex.

**Why glTF (.glb), not FBX.** The body already reaches Unreal as glTF through Interchange (`import_bodies.py`), and
the Quaternius file was itself written by Blender's glTF exporter: same armature node, same root rotation, same bone
names and local axes, metres in, centimetres out, no extra root bone. Blender's FBX path adds the armature object as a
bone and needs scale and leaf-bone settings to land the same way. glTF also carries the PBR factors, normal maps and
the hair's alpha mask without per-exporter material quirks, and one .glb keeps the textures with the mesh.

## How a garment is made (`costume.py`, `pieces.py`)

1. **A volume around the body.** The body is copied and pushed out along its normals (per bone family: torso, arms,
   legs; `Body.offset_copy`), then merged with lofted solids where cloth should hang rather than cling: the coat's
   flared skirt, a jacket's torso falling straight from the chest (`torso_solid`), sleeves tapering along the arm
   (`arm_tubes`), trouser legs (`leg_tubes`). A voxel remesh makes one smooth closed surface: concavities fill, toes
   merge, muscle grooves vanish.
2. **Ironed.** Taubin smoothing (volume-keeping), hard over the torso, lighter on limbs (`iron`), so no pecs, abs or
   quads read through.
3. **Folds** are displacement fields along the normal: hanging folds in the coat skirt, bunching above cuffs and
   ankles, elbow and knee creases, quilting on the vest, ribs on knit bands.
4. **Openings on clean planes.** Hems, cuffs and the coat's front opening are `bisect` cuts, so edges are straight;
   the neckline is an ellipse cut whose ragged edge is snapped onto the ellipse (`snap_neckline`).
5. **Decimated** to a triangle budget, then **trim is cut on planes and raised**: panels, piping, stripes, zips are
   their own material faces with real height (`raise_faces`), never painted.
6. **Thickness** (Solidify inward, rims closed; the inner shell takes a lining material). A face attribute `shell`
   marks outer, inner and rim faces so later steps touch only the outside.
7. **UVs** are box projections in metres (`box_uv`), so the tiling cloth normals read at the same scale everywhere.
8. **Weights** come from the body: each garment vertex takes the bone weights of the nearest point on the body's
   surface (barycentric, `Body.transfer`), then hand fixes: the coat skirt hangs from the pelvis and follows each
   thigh only partly (more at the front and toward the hem); jacket and vest hems move leg weight onto the pelvis
   (`reassign`); armpits are smoothed harder; rigid props (quiver, arrows, forearm guard) are bound to one or two
   bones. Four influences, normalised.
9. **Skin under clothes is deleted**: only the head and neck (inside the collar), and the hands where there are no
   gloves, remain.

The body is `cs.Body(root, gltf=...)`: `BODY_GLTF` (Superhero_Male) by default, `BODY_FEMALE_GLTF` for Kate. A build on
another body sets `body.neck_z` (where `pieces.neck_shape` measures the neck: 1.545 on the male, 1.51 on the female)
and passes its own heights to the pieces that take them (`neck_hole`, `snap_neckline`, `stand_collar`,
`skin_above_collar`). Shared since Kate: `pieces.fitted_torso`, `pieces.forearm_guard` and `pieces.quiver` (moved out
of `build_clint.py`; Clint's numbers are their defaults, and his rebuild after the move was the same 76,967 triangles),
`pieces.gloves(keep=...)` for part of a hand, and `combat_boots(heel_drop=...)`.

Footwear (`footwear_upper`, `combat_boots`, `trainers`) is the same idea on the feet: the remesh merges the toes into
a toe box, the underside is flattened, and a separate sole is lofted from the outline of the upper with a heel step
and toe spring; laces are criss-cross tubes between eyelet pairs found by casting rays onto the upper. Trousers are
tucked into boots by clamping them inside the shaft and bagging them out over its rim (`tuck`).

## The costumes

**Clint** (`build_clint.py`). From a photo of the show's suit (cosplay; costume only, not the face). A tailored
long-sleeved coat to above the knee (hem at 0.60 m), navy black, fitted at the waist (`fitted_torso`: the torso's own
width at each height, smoothed along the height, so it comes in to the belt and bridges the pecs and the groove between
the shoulder blades; no hang from the chest), its skirt tapering a little to the hem and parting at the front below the
belt in an inverted V (`OPEN_GAP`, `OPEN_SPREAD`); a high stand
collar with a maroon lining; wide maroon panels down both front edges with a black placket between them on the closed
chest; a maroon yoke over the shoulders and bands at the elbows; thin silver piping along the panels, yoke, elbow
bands, cuffs, the side seams and a curved line from each shoulder to the panel. Leather belt with a buckle. Black
trousers, a little loose, tucked into tall lace-up combat boots (toe cap, heel counter, sole with a heel, strap and
buckle, pull tab). Leather gloves. A slim rigid guard (2.8 mm, open over the top third) with two thin straps on the bow
(left) forearm over close-fitting sleeves. A black
quiver across the back, mouth over the left shoulder, ten arrows with nocks and vanes, its strap across the chest.
Parted brown hair and the pack's beard (an original face comes later through MetaHuman). About 77k triangles, 19
materials.

**Thug** (`build_thug.py`). The Tracksuit Mafia in dark red: a zip jacket falling from the chest to a narrow ribbed
hem at the hips (no blouson), sleeves with a tube over the upper arm only that narrows into the forearm (a tube to
the wrist ended in a fat disc above the cuff, the balloon), ribbed cuffs, a short stand collar, a metal zip and pull, two white stripes from the collar down each
sleeve (on top of the T-posed arm, the outside when it hangs); trousers with two white stripes down the outside of each
leg and ribbed ankles that rest on the trainers' collars (`ANKLE_Z` 0.072; at 0.092 a gap showed); white trainers with a thick cupsole, a grey midsole line, black side panels
and collar, laces and a heel tab. Buzzed black hair, bare hands. About 47k triangles, 12 materials. Since
2026-11-12 the suit is matte (roughness 0.86; at 0.62 it read as vinyl in the engine's sun), the ribbed hem and cuffs
are the suit's own red (darker, they read as a seam band), the jacket hangs closer to the chest and draws in toward
the hem (pad 0.012, `hem_in` 0.08), and the trousers' crotch takes most of its weight from the pelvis, smoothed hard
(`crotch_weights`): each vertex there had followed the nearer thigh, so a wide stance tore it into a stepped crease.

**Heavy** (`build_heavy.py`). The thug's tracksuit with a glossy black quilted puffer vest over it (channels every
7 cm, a zip, a stand collar), a black knit balaclava with one eye slot, and the black combat boots with the trousers
tucked in. About 60k triangles, 14 materials.

**Kate** (`build_kate.py`). Kate Bishop's hero suit, costume only; the face is the pack's female face (an original
face, nobody's likeness). On `Superhero_Female` (176.7 cm in the pack, about 164 cm at BP_Kate's 0.93 driver scale,
against Clint's 181), made athletic under the clothes first (`athletic`: the bust in by half its projection, the hips
in, the seat back, the thighs 8 % slimmer toward the bone line; skeleton and weights untouched).

- Top: fitted, long-sleeved, deep purple (linear 0.050, 0.0055, 0.125; the first build's 0.090, 0.010, 0.228 read
  lavender in the studio and `#7539AF` to `#8649BE` on the street by day in the engine; times 0.55 the torso's median
  is `#4C357C` by day and `#632896` under a street lamp at night, around the `#5B2A86` aimed at), roughness 0.85, over `fitted_torso` (it bridges the bust and comes in to the
  waist), slim sleeves (`arm_tubes`, 3 to 4 mm pads). Black stretch side panels between two y planes up each side and
  along the underside of each upper sleeve to the elbow (on the forearm they hid the guard). A black yoke over the
  shoulders whose lower edge is a chevron pointing down the chest (1.235 m) and the back (1.285 m), light purple piping
  under it and at the shoulder seam, a metal zip down it, black cuffs, a high purple stand collar zipped to the top
  (0.6 degree gap, top edge at 1.534 m; 1.552 reached the jaw) and lined in dark purple. The hem is hidden under the belt.
- Trousers: black, fitted (the legs' own shape, ironed, with a fitted solid over the hips so the seat's cleft does not
  read; straight tubes from the thigh read as baggy pipes with a step at their top), a raised light purple stripe down
  the outside of each leg, the crotch on `crotch_weights`, tucked into black mid-calf combat boots (top at 0.30 m, a
  3 mm heel step).
- Gear: a belt with four leather pouches (rigid on the pelvis); a strap round the right thigh with a pouch; the archery
  guard on the left forearm (125 degrees each side, glossier than the cloth); a fingerless glove on the bow (left) hand
  (the hand and first finger joints) and a three-finger shooting glove on the draw hand (index, middle and ring whole;
  thumb and little finger bare), cut from the body's topology by each vertex's strongest bone, the skin keeping the
  glove's edge ring so no gap opens; Clint's quiver, smaller (radius 4 to 5.3 cm, eight arrows, purple vanes, light
  purple nocks) and mirrored, its mouth over the right shoulder (the draw hand's), the strap across the chest to the
  left hip.
- Hair: the pack has no ponytail, so it is built. The scalp is the head's own skin pushed out 6.5 mm (a voxel shell of
  the head grew surfaces inside the skull from the eyes and mouth and lost the crown; the glTF's verts are split at UV
  seams, so they are merged first), inside a hairline taken from the pack's `Hair_BuzzedFemale` (per direction round a
  crown axis, how far down it reaches, smoothed, brought lower at the front), thinning to 1.2 mm at the hairline, with
  grooves running to the tie. The ponytail is 26 locks (tapered six-sided tubes, each with its own wave and length) and
  a core, round a path from the tie at the back of the crown out and down to between the shoulder blades, kept 3.6 cm
  off the neck and collar, with a hair tie. UVs run root to tip, so the strand texture's alpha (clipped, the glTF MASK
  mode, as on the pack hair) breaks the tips and feathers the hairline. Skinned to `Head`, sharing up to 30 % with
  `neck_01` toward the tip.

About 70k triangles (top 22k with its trim and thickness, trousers 10.5k, boots 11k, ponytail 5.4k, scalp 3.5k), 23
materials.

## Unreal side

`Tools/Editor/import_bodies.py` (`ensure_built`): for each build in `BUILT_LOOKS` (`thug`, `heavy`, `clint` on the male
body, `kate` on the female) whose `<assets root>\built\<who>\SK_<Who>.glb` exists, imports it through Interchange onto
its base body's skeleton (`SKEL_Body_Male` or `SKEL_Body_Female`;
`/Game/Characters/Bodies/Built/SK_Built_<Who>`; the generic pipeline's common skeletal settings `skeleton` property is
honoured for glTF), gives it four LODs by the engine's skeletal mesh reduction (`BUILT_LODS`; each half the triangles of
the last), builds `M_BuiltLook` / `M_BuiltLookMasked` (colour times texture, roughness, metallic, normal, the thug's
HitFlash everywhere and Telegraph on the `glow` slots, like `M_BodyLook`) and an `MI_<slot>` per slot from
`<who>.slots.json`, and points that wearer's `BodyLook` at the built mesh with the same `ABP_<Driver>_Body` (same
skeleton, so the same IK rig and retargeter). Generated normal maps are imported with the green channel flipped
(Blender writes OpenGL normals); the pack's own textures a slot names (Kate's skin and normal) are imported once as
`Built/T_Built_<file>`. Anything missing or failing (no file, the import making its own skeleton, no slot table) logs a
line and leaves the painted body.

First run 2026-11-11: all three imported clean onto the skeleton, second run idempotent. LOD vertex counts at import:
thug 38.7k / 12.7k / 7.4k / 4.2k, heavy 52.6k / 17.7k / 10.0k / 5.6k, Clint 66.3k / 21.2k / 11.6k / 6.2k.
`Hawkeye.Screenshot.Bodies` (`-game`) shows each one posed by its driver, materials right, the hair cards masked, the
hit flash (whole body white) and the telegraph (head and hands orange) on the thug, and the look on the ragdoll (pelvis
2 cm off).

**Kate (in the game since 2026-11-12).** The female body is its own import: `Superhero_Female_FullBody.gltf` as
`SK_Body_Female` / `SKEL_Body_Female` (`import_base("female")`, only when the built Kate is on disk; 177 cm, pelvis 93,
head bone 155, the same 65 bone names as the male but its own rest pose, so a mesh built on it cannot go on
`SKEL_Body_Male`). `SK_Built_Kate` lands on it (LODs 59.8k / 20.2k / 11.7k / 6.6k vertices, 23 slots). `ensure_kate`
then builds `IK_Body_Female` (`create_kate_look.ensure_rig`, the `ue4` chains: 22 chains, 4 goals, 4 limb solvers),
`/Game/Characters/Kate/Look/RTG_Kate_To_Body` from `IK_Kate_UEFN` (her driver, the same source as Clint's; hands
blended fully onto the driver's) and `ABP_Kate_Body`, and sets `BP_Kate.BodyLook` to `SK_Built_Kate` with its 23
instances and no hidden bones (Sparrow's `HIDDEN_BONES` were her own bow and arrow; the built Kate has none, and keeps
the game's bow). `create_kate_look.py`, the step before, still builds Sparrow's assets but leaves `BP_Kate` alone
while the built Kate is on disk; if her import or retarget fails, `import_bodies` puts Sparrow back on her
(`create_kate_look.run(apply=True)`), and without either the mannequin is drawn. She takes the driver's 0.93 scale
(about 164 cm; Clint 181). Her slot table has no `glow` slots: nothing on Kate is driven by material (the hit flash
and telegraph are the thugs'). Her only quiver is the built one (right shoulder); the game's bow sits across her back
when holstered, its string over the quiver.

`Hawkeye.Screenshot.Bodies` shoots her in two rounds, day and night (`body_kate_<front, back, run, crouch, punch,
draw, trio>_<day, night>.png`) and checks her standing pose, feet (0 to 5.5 cm above the capsule's bottom), the crouch,
a run (500 cm/s), and that the look's hands stay within 5 cm of the driver's in the punch (0.0 to 2.8) and the draw
(2.8 to 2.9: at full draw her arm is a little short of the driver's hands); the look's pelvis is 0.0 to 0.1 cm from the
driver's in every shot.

## Enemy heads and bodies (rebuilt 2026-11-24; built, read and shipped 2026-11-25)

The owner's verdict on the thugs and the heavy was "bad": a mask-like face with painted features, a bulging chest under a
tracksuit, a wide straddle, balloon trousers. What the first builds were, and why they read that way:

| Part | How it was built | What looked wrong |
|------|------------------|-------------------|
| Head and neck | The pack's `Superhero_Male` skin above the collar: about 1,440 of the body's 7,281 vertices, 18 cm wide, 23 cm tall. The face is **painted into the skin texture** (`T_Superhero_Male_Dark.png`); the geometry has a smooth egg with a nose bump. Eyes are two 3 cm spheres (`Face`, 646 verts) textured from `T_Eye_Brown.png`, brows are alpha cards from `T_Hair_2`. One face for every man. | Features that do not catch the light (a mask), eyes too big and flat, no lips or sockets in the mesh, no way to tell two thugs apart but hair and a balaclava. |
| Body | The pack's bodybuilder (shoulder joints 42 cm apart, deltoids to 54 cm, chest 27 cm deep, thighs 13 cm radius), the cloth built round it by `pieces.shell` (offset 1.8 cm, voxel remesh, Taubin "ironing"). | A body that wide under 1.2 cm of cloth reads as an inflated torso, whatever the smoothing. |
| Trousers | `pieces.leg_tubes` tapered from the thigh to **the knee's radius times 0.9 plus 3 cm at the ankle** (about 8.9 cm, 17.8 cm across), unioned with the body's offset shell; the ribbed cuff only gathered the last 4.5 cm. | The balloon: the ankle was never narrow; the cuff pinched a tube that was already wide. |
| Stance | The skeleton is the pack's: hip joints 22.8 cm apart (a real man's are about 17), feet 22.8 cm apart in the rest pose, legs vertical, arms in a T. The engine retargets the mannequin driver's idle onto it. | The straddle is the skeleton's hip width plus the driver's combat idle; no mesh change fixes it (see "Not changed" below). |
| Textures | Skin and normal are the pack's; the cloth has only tiling normals; the hair is the pack's card texture tinted. | One skin, no stubble, lips, veins, pores of its own. |

### What replaces it

* **`looks.py`** (pure Python, tested by `test_looks.py`): the data. `FaceSpec` (about 25 numbers: jaw width and squareness,
  chin, brow, eye spacing, size and depth, cheekbone and hollow, nose projection, width and bridge, lips, mouth width, ears,
  age, neck, skin tone, flush, hairline and fade, stubble, plus choices: skin and eye colour, hair style
  `bald/buzz/crop/short`, headwear `none/beanie/cap`, facial hair `none/stubble/goatee/beard`) and `BodyBuild` (shoulder, chest,
  waist, belly, arm, thigh, hip, cloth ease, ankle radius, trouser taper), each with a range checked by `problems()`. `LOOKS` is the
  table of rows: `thug` (the default, what `built/thug` always was; a heavy-browed, square-jawed man in his thirties), `thug_b`
  (long, narrow, hooked nose, beanie), `thug_c` (round, full lips, cap, beard), `thug_d` (shaved head, long square jaw, goatee going
  grey), `heavy` (balaclava, so brow and eyes). **A new thug is a new row**; `BLUEPRINT_LOOKS` says which a Blueprint draws from.
  Every face is an original: parameters of a base head, tuned apart by eye, none modelled on a person, actor or photograph.
* **`headshape.py`** (numpy, tested by `test_headshape.py`): the head as a structured grid, 90 rings (denser over the face: 2.5 mm
  apart to the brow) of 96 points (denser at the front), a crown apex and a neck cap, 8.6k vertices and 17k triangles. Each ring is a
  superellipse (half width, front and back depth, centre, squareness from control points through a monotone cubic), so the skull,
  the jaw, the chin, the under-jaw and the neck are one surface; the face is heights added along the ring's normal: brow ridge and
  glabella, sockets, the lid openings the eyeballs show through, the lids' rims and creases, tear troughs and bags, cheekbones and
  hollows, nose (dorsum, tip, wings, nostrils, hump or dip), nasolabial folds, philtrum, lips, mouth line and corners, chin, jaw
  angle, masseter. Landmarks are ordinary adult proportions (eyes at half the head's height, nose base a third up, mouth a quarter,
  22.5 cm chin to crown). Also: ears (a lens-shaped solid with a concha and a helix, standing off the head by `ear_flare`),
  eyeballs (a pole at the front, a 1.1 mm cornea dome inside 33.75 degrees, planar UVs), brow cards sampled off the skin, a hair cap
  from a hairline curve (forehead, temples, sideburn, nape, skin fade) up to the crown, a beanie (rolled cuff), a cap (crown and a curved brim). UVs
  are the grid: u round the head (seam at the back), v up it.
* **`headtex.py`** (numpy): procedural bakes, no network, no model. Skin albedo and normal at 1024 square from the same numbers that
  shaped the mesh (tone drift, flush, eye shadow, lips, the line between them, roots under the hair, stubble/goatee/beard dots with grey
  flecks by age; pores, forehead lines, crow's feet, neck creases, lip lines), eye (sclera, veins, iris fibres, limbal ring, pupil),
  brow (many strokes, alpha-clipped), hair (strand stripes and a groove normal). `<look id>_skin.png` / `_skin_n.png` in `--override-dir` replace the skin
  bake: the hook for anything made outside the repo. **No AI-generation path is wired**: the only local generation the project has
  (TRELLIS.2 via Z-Image-Turbo for Lucky, `docs/CREDITS.md`) depends on Meta's DINOv3 licence, whose owner sign-off is still pending, and it makes
  whole meshes, not faces to a spec.
* **`headbuild.py`** (bpy): the objects, skinned rigid to `Head` (the head and neck blend into the body's own weights below the
  chin), the materials (skin, sclera and a wet iris at roughness 0.05 so the dome catches the engine's light, brows, hair, headwear)
  and the hands' skin (the pack's texture recoloured to the face's tone). The pack's eyes and brows are dropped.
* **`bodyshape.py`**: `reshape_body` narrows and softens `body.reference` by the look's `BodyBuild` before any garment measures
  it (torso width and depth by height, belly, arm and thigh girth about the bone lines), then a Taubin pass. Skeleton, rest pose and
  weights untouched. Clothes hang with `build.ease` of air (the old fixed 1.2 cm), with long drape folds down the jacket and trouser
  legs, and the trousers taper to `build.ankle` (`pieces.leg_tubes(r_bottom=, taper=)`).
* **Variants at runtime.** `import_bodies.BUILT_LOOKS` has a row per thug look; `LOOK_VARIANTS` writes them to
  `BP_Thug.BodyLookVariants` (C++, `AThugCharacter`): each thug takes one of {`BodyLook`, the variants} when he is made, by hash of his actor's
  name (`HawkeyeBodyLook::PickVariantIndex`), so a placed thug keeps his face. A variant not on disk falls back to `BodyLook`. The
  heavy has one look. **The C++ for this was written without a compiler; build `HawkeyeEditor` before trusting it.**

```powershell
$B = "C:\Program Files\Blender Foundation\Blender 5.3\blender.exe"
foreach ($l in "thug","thug_b","thug_c","thug_d") { & $B --background --factory-startup --python Tools\Blender\build_thug.py -- --look $l }
& $B --background --factory-startup --python Tools\Blender\build_heavy.py -- --look heavy
```

Extra flags: `--legacy-head` (the pack's head), `--legacy-body` (no reshape), `--override-dir`, views `face_front, face_34, face_side,
face_back, eyes` (face close-ups, 0.34 m across), and the usual `--views`. Tests, no Blender: `python Tools/Blender/test_looks.py`,
`python Tools/Blender/test_headshape.py` (numpy; skipped without it).

**Not changed, on purpose.** The skeleton, its bone names, its rest pose (T-pose arms, vertical legs at 22.8 cm) and the weights
transfer are the pack's, so `SKEL_Body_Male`, `IK_Body_Male`, the retargeters, the montages and `_handpose.py` see no difference.

**What the first real runs found (2026-11-25).**

* The reshape (`bodyshape.reshape_body`) smoothed the body's reference mesh vertex by vertex. The glTF body's vertices are split at the UV
  seams, so each copy of a seam vertex moved on its own, the seams tore, and the voxel remesh behind every garment filled only a crust
  (5.6k vertices instead of 90k) or none, which crashed the trainers. Smoothing and the offset normals now treat coincident vertices as one
  (`welded_taubin`, `weld_normals`); all five looks build every time.
* The face is a grid of rings, so a feature is a bump on the ring's normal. Wide Gaussians at one height read as a shelf across the
  cheek (the cheekbone, the muzzle, a mouth groove in the face-plane profile); each was narrowed, tilted or removed, the face's plane
  made monotone and the grid smoothed twice. Judge the renders at full size: a 500 px montage of the faces shows moire from the pore
  normal map that is not in the 1000 px render or the engine.
* The head sits 2.2 cm lower (`CHIN_Z` 1.563) and 4.5 % wider than first written: the first seat left a 5 cm stalk of neck over the stand
  collar and a head that looked small on the shoulders.
* The skin textures are quieter (cheek flush and blotches cut by half or more), the brows thinner and softer, the buzz cut is a stippled
  alpha (dissolving toward the hairline) instead of a hard-edged shell, and the lids, nostrils, lash lines and the under-chin shadow are
  painted in.
* `BP_Thug_Heavy` is a child of `BP_Thug`, so it inherited the thugs' `BodyLookVariants` and wore a beanie thug's face over the vest and
  balaclava; each wearer is now written its variants (the heavy's none).

**Stance.** The straddle is not in the mesh and the retarget pose cannot fix it: the retargeter's IK puts the feet where the driver's
combat idle has them, 35 cm apart under hips 22.8 cm apart. It is fixed after the retarget, in `FAnimNode_HawkeyeBodyPose`
(`HawkeyeBodyPose::LegAdductionTurn`, `Hawkeye.BodyPose.LegAdduction`): by the same at-ease weight as the rest arms, each leg is turned in
at the hip, in the frontal plane, until the foot is `FeetUnderHipsRatio` (0.92) of the hip's distance out, never more than 24 degrees; the
foot is turned back so it stays flat. The Bodies shot checks it (`feet N cm apart (hips M)`, a failure past 1.35 times).

## Known limits

- No cloth simulation: Clint's skirt is skinned. It swings in a stride, but in a deep crouch the front panels fold
  into the boots. A cloth asset (Chaos Cloth) on the skirt would fix it in the engine.
- LOD0 is still 47k (thug), 60k (heavy) and 77k (Clint) triangles; the engine LODs carry the distance. The LOD screen
  sizes are the engine's defaults, not tuned.
- The thug's upper sleeves were full of the Superhero body's deltoids and biceps; the reshape (`bodyshape.py`) is meant to fix it.
- The thugs' and heavy's faces are ours since the rebuild above (once phase 2 has built them); Clint still has the pack's male
  face, and Kate the pack's female face with its painted eyeliner and heavy brows.
- A teleport turns Kate's capsule but not her body: the Game Animation Sample's AnimBP keeps the mesh's old facing until
  she moves (a 180 degree teleport in the bodies shot left her facing the old way, the holstered bow on her chest). The
  shot walks her into each facing. Not fixed in the game: fast travel and checkpoint loads may show it for a moment.
- Kate's ponytail and quiver are bones simulated in the engine by the Kawaii Physics plugin (animation.md, "Secondary
  motion"); in this build's meshes they only turn with the head and spine until that is on.
- Kate's glove edges follow the body's own edges, so the bow glove's finger ends are a little jagged close up; the
  black under-sleeve panel meets the side panel with a small purple notch under each arm; the arrow vanes are flat
  boxes.
- The pack's hands are large and its eyebrow and beard cards are coarse.
