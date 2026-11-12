# Blender costume pipeline

Modelled clothes for Clint, the thug and the heavy, built by script in Blender 5.3 on the Quaternius body the game
already uses (`animation.md`, "Bodies"). Replaces the painted-on clothes (`M_BodyLook`) for whoever has a build on
disk. Built 2026-11-11.

## Running it

```powershell
$B = "C:\Program Files\Blender Foundation\Blender 5.3\blender.exe"
& $B --background --factory-startup --python Tools\Blender\build_clint.py -- [--no-render] [--no-export] [--views front,idle]
& $B --background --factory-startup --python Tools\Blender\build_thug.py  -- ...
& $B --background --factory-startup --python Tools\Blender\build_heavy.py -- ...
```

About 15 to 30 s each on this machine, renders included. One Blender at a time; no Unreal needed. Flags:
`--assets-root` (else `HAWKEYE_ASSETS_ROOT`, `Tools\local.json` `assetsRoot`, `C:\Users\camer\code\hawkeye-assets`),
`--out` (else `<assets root>\built\<who>`), `--renders` (else `Saved\Screenshots\Blender`), `--views` (any of
`front, 34, side, back` at rest, `idle, arms_up, crouch, lunge` posed, `head_front, head_back, feet, hand`
close-ups; default the first eight). Deterministic: same inputs, same meshes.

Each writes to `<assets root>\built\<who>\`:

| File | What |
|------|------|
| `SK_<Who>.glb` | one skinned mesh on the body's own armature (65 bones, same names and rest pose), all its materials and the generated normal maps embedded |
| `<who>.slots.json` | per material slot: colour, roughness, metallic, normal strength, base/normal texture file (next to the glb, or the pack's `Textures` folder), masked, glow. `import_bodies.py` builds the engine's instances from it |
| `<who>.blend` | the built scene (armature, mesh, studio) for looking at by hand |
| `T_N_<weave>.png`, `T_Hair.png` | generated tiling normals (twill, grain, knit, rib, tricot) and the tinted hair |

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

Footwear (`footwear_upper`, `combat_boots`, `trainers`) is the same idea on the feet: the remesh merges the toes into
a toe box, the underside is flattened, and a separate sole is lofted from the outline of the upper with a heel step
and toe spring; laces are criss-cross tubes between eyelet pairs found by casting rays onto the upper. Trousers are
tucked into boots by clamping them inside the shaft and bagging them out over its rim (`tuck`).

## The costumes

**Clint** (`build_clint.py`). From a photo of the show's suit (cosplay; costume only, not the face). Knee-length
long-sleeved coat, navy black, its skirt open at the front below the belt, hanging clear of the legs; a high stand
collar with a maroon lining; wide maroon panels down both front edges with a black placket between them on the closed
chest; a maroon yoke over the shoulders and bands at the elbows; thin silver piping along the panels, yoke, elbow
bands, cuffs, the side seams and a curved line from each shoulder to the panel. Leather belt with a buckle. Black
trousers, a little loose, tucked into tall lace-up combat boots (toe cap, heel counter, sole with a heel, strap and
buckle, pull tab). Leather gloves. A rigid guard with two straps on the bow (left) forearm over the sleeve. A black
quiver across the back, mouth over the left shoulder, ten arrows with nocks and vanes, its strap across the chest.
Parted brown hair and the pack's beard (an original face comes later through MetaHuman). About 77k triangles, 19
materials.

**Thug** (`build_thug.py`). The Tracksuit Mafia in dark red: a loose zip jacket hanging straight from the chest to a
ribbed hem, ribbed cuffs, a short stand collar, a metal zip and pull, two white stripes from the collar down each
sleeve (on top of the T-posed arm, the outside when it hangs); baggy trousers with two white stripes down the outside
of each leg and gathered ribbed ankles; white trainers with a thick cupsole, a grey midsole line, black side panels
and collar, laces and a heel tab. Buzzed black hair, bare hands. About 47k triangles, 12 materials.

**Heavy** (`build_heavy.py`). The thug's tracksuit with a glossy black quilted puffer vest over it (channels every
7 cm, a zip, a stand collar), a black knit balaclava with one eye slot, and the black combat boots with the trousers
tucked in. About 60k triangles, 14 materials.

## Unreal side (written, not yet run)

`Tools/Editor/import_bodies.py` (`ensure_built`): for each of `thug`, `heavy`, `clint` whose
`<assets root>\built\<who>\SK_<Who>.glb` exists, imports it through Interchange onto `SKEL_Body_Male`
(`/Game/Characters/Bodies/Built/SK_Built_<Who>`), builds `M_BuiltLook` / `M_BuiltLookMasked` (colour times texture,
roughness, metallic, normal, the thug's HitFlash everywhere and Telegraph on the `glow` slots, like `M_BodyLook`) and
an `MI_<slot>` per slot from `<who>.slots.json`, and points that wearer's `BodyLook` at the built mesh with the same
`ABP_<Driver>_Body` (same skeleton, so the same IK rig and retargeter). Anything missing or failing (no file, the
import making its own skeleton, no slot table) logs a line and leaves the painted body. **This path was written
without an engine run** (the editor could not be launched): the first `create-content.ps1` run after this should be
watched for `FAILED` lines under `/Game/Characters/Bodies/Built`, and `Hawkeye.Screenshot.Bodies` rerun.

## Known limits

- No cloth simulation: Clint's skirt is skinned. It swings in a stride, but in a deep crouch the front panels fold
  into the boots. A cloth asset (Chaos Cloth) on the skirt would fix it in the engine.
- Triangle counts are still high for enemies (47k and 60k; most of it is the doubled shell from Solidify and the
  stripe cuts); engine LODs (or lower `JACKET_TRIS` / `TROUSER_TRIS`) are the next step.
- Faint upper-back muscle relief still shows through Clint's coat between the shoulder blades in flat light.
- The face is the pack's single male face; hair, beard and the balaclava are what tell them apart.
- The pack's hands are large and its eyebrow and beard cards are coarse.
