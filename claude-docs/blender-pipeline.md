# Blender costume pipeline

Modelled clothes for Clint, the thug and the heavy, built by script in Blender 5.3 on the Quaternius body the game
already uses (`animation.md`, "Bodies"). Replaces the painted-on clothes (`M_BodyLook`) for whoever has a build on
disk. Built 2026-11-11. Kate's suit (on the pack's female body) followed on 2026-11-12; it is built and rendered but
not yet imported into the engine (see "Kate" and "Unreal side").

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

- Top: fitted, long-sleeved, deep purple (linear 0.090, 0.010, 0.228: `#5B2A86` with less green, which read lavender
  under the studio's lights and AgX), roughness 0.85, over `fitted_torso` (it bridges the bust and comes in to the
  waist), slim sleeves (`arm_tubes`, 3 to 4 mm pads). Black stretch side panels between two y planes up each side and
  along the underside of each upper sleeve to the elbow (on the forearm they hid the guard). A black yoke over the
  shoulders whose lower edge is a chevron pointing down the chest (1.235 m) and the back (1.285 m), light purple piping
  under it and at the shoulder seam, a metal zip down it, black cuffs, a high purple stand collar zipped to the top
  (0.6 degree gap) and lined in dark purple. The hem is hidden under the belt.
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

`Tools/Editor/import_bodies.py` (`ensure_built`): for each of `thug`, `heavy`, `clint` whose
`<assets root>\built\<who>\SK_<Who>.glb` exists, imports it through Interchange onto `SKEL_Body_Male`
(`/Game/Characters/Bodies/Built/SK_Built_<Who>`; the generic pipeline's common skeletal settings `skeleton` property is
honoured for glTF), gives it four LODs by the engine's skeletal mesh reduction (`BUILT_LODS`; each half the triangles of
the last), builds `M_BuiltLook` / `M_BuiltLookMasked` (colour times texture, roughness, metallic, normal, the thug's
HitFlash everywhere and Telegraph on the `glow` slots, like `M_BodyLook`) and an `MI_<slot>` per slot from
`<who>.slots.json`, and points that wearer's `BodyLook` at the built mesh with the same `ABP_<Driver>_Body` (same
skeleton, so the same IK rig and retargeter). Generated normal maps are imported with the green channel flipped
(Blender writes OpenGL normals). Anything missing or failing (no file, the import making its own skeleton, no slot
table) logs a line and leaves the painted body.

First run 2026-11-11: all three imported clean onto the skeleton, second run idempotent. LOD vertex counts at import:
thug 38.7k / 12.7k / 7.4k / 4.2k, heavy 52.6k / 17.7k / 10.0k / 5.6k, Clint 66.3k / 21.2k / 11.6k / 6.2k.
`Hawkeye.Screenshot.Bodies` (`-game`) shows each one posed by its driver, materials right, the hair cards masked, the
hit flash (whole body white) and the telegraph (head and hands orange) on the thug, and the look on the ragdoll (pelvis
2 cm off).

**Kate is not imported yet.** What it needs (engine side, not written): `import_bodies.py` must import
`Superhero_Female_FullBody.gltf` as `SK_Body_Female` / `SKEL_Body_Female` (same 65 bone names, but the female's rest
pose and proportions differ, so the built Kate cannot go on `SKEL_Body_Male`); `ensure_built` needs a skeleton per
wearer (`kate` on `SKEL_Body_Female`) and `BUILT_LOOKS` an entry for her; an IK rig on the female body
(`create_kate_look.ensure_rig` with the manifest's `ue4` chains, as `IK_Body_Male`), a retargeter from `IK_Kate_UEFN`
to it and an `ABP_Kate_Body` like Clint's; and `BP_Kate.BodyLook` pointed at `SK_Built_Kate` instead of Sparrow
(`create_kate_look.py` writes Sparrow's look today; Sparrow's `HIDDEN_BONES` do not apply). Kate keeps the game's bow.
Her slot table has no `glow` slots; the hair, ponytail and brows are masked (`M_BuiltLookMasked`).

## Known limits

- No cloth simulation: Clint's skirt is skinned. It swings in a stride, but in a deep crouch the front panels fold
  into the boots. A cloth asset (Chaos Cloth) on the skirt would fix it in the engine.
- LOD0 is still 47k (thug), 60k (heavy) and 77k (Clint) triangles; the engine LODs carry the distance. The LOD screen
  sizes are the engine's defaults, not tuned.
- The thug's upper sleeves are still full: the Superhero body's deltoids and biceps are under them.
- The face is the pack's single male face; hair, beard and the balaclava are what tell them apart. Kate has the
  pack's female face, with its painted eyeliner and heavy brows.
- Kate's ponytail is skinned to the head, not simulated: it turns with the head and does not swing. Physics bones or
  AnimDynamics in the engine would let it move.
- Kate's glove edges follow the body's own edges, so the bow glove's finger ends are a little jagged close up; the
  black under-sleeve panel meets the side panel with a small purple notch under each arm; the arrow vanes are flat
  boxes.
- The pack's hands are large and its eyebrow and beard cards are coarse.
