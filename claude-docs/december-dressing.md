# December dressing: one hero block

The winter set dressing for one city block, planned from the district data. This is the plan half: a pure,
deterministic Python module that turns the block's buildings, streets, fire escapes, ledges, anchors and set pieces
into a list of placement records. The engine half (spawning greybox meshes from the records) is a separate job; this
doc is its contract.

- Module: `Tools/Editor/_dressing.py`
- Tests: `Tools/Editor/test_dressing.py` (unit cases, a made-up street, the hero block and its golden hash)
- Entry points: `plan_block_dressing(district, block_id) -> [record]`, `hero_block_id(gather_scene(district))`,
  `plan_scene_dressing(scene, block_id) -> plan`, `plan_json(plan)`, `write_plan(plan, path)`

Rules 2b and 2c hold: nothing is hand-placed, every placement comes from the OpenStreetMap records and the planners
that already read them, and it is one block, finished, before the next.

## The hero block

`hero_block_id` picks it by script: the block holding the building the toy drive's table has its back to
(`_site.plan_site`, chapter 2 "Reindeer Games"), falling back to the block under chapter 1's chase start when there is
no toy drive.

Why the toy drive and not the chase start: the toy drive is Act 1's one street-level set piece and the one the season
is about. Kate walks up to a Christmas toy drive and the fight happens on its pavement, so street-level kit (lights
over the street, wreaths, shop fronts, snow banks, a tree stand) is seen at the distance it was made for. The chase
starts on a roof at night and leaves its block within seconds; its roofs would only show the snow.

On the committed data that is `blk_R3363343`: the block between Avenue B and Avenue C, East 9th and East 10th
Streets. 30 buildings, 486 m of street frontage, including 143 Avenue B (the 65 m Christodora House) with the toy drive
at its foot.

Blocks come from `partition_blocks`: footprints within 25 m of each other, with no street centre line between their
closest points or their centroids, share a block. The id is `blk_` plus the block's smallest OSM id, so it survives a
re-download that keeps the buildings. The district has 14 blocks.

## What is planned

| Kind | Shape | From | Hero block |
|------|-------|------|-----------:|
| `string_light` | spline (catenary, 9 points) | a span every 10 m along each street face, straight across to the first facade outside the block, at 7.2 m (then 7.8, 8.4, 6.6 m) | 10 |
| `wreath` | cylinder, turned to face out | one per addressed building, over its door on the wall fronting its `addr:street` | 22 |
| `window_light` | box, a 2 cm card on the wall | a window grid (3.3 m storeys, 1.8 m bays) from the first floor up, 35% lit by hash | 335 |
| `window_candle` | cylinder | 30% of the lit windows, on the sill | 103 |
| `roof_snow` | closed spline, 8 cm thick | every roof, inset 40 cm (inside the parapet) | 29 |
| `parapet_snow` | box | every parapet top, in pieces of 3 m or less, bare round anchor fittings | 418 |
| `snow_bank` | sphere (ellipsoid, half buried) | every 5 m along the kerb, 30% gaps, clear of doors and crossings | 25 |
| `tree_stand` | box (two posts and a rail) | one NYC sidewalk tree stand, 5 m back from a corner on the widest pavement | 3 |
| `tree_for_sale` | sphere | six trees leaning on the stand's rail | 6 |
| `steam_vent` | cylinder | three Con Ed stacks in the near lane | 3 |
| `car_snow` | sphere | half of the parked cars (from `clutter_plan`) on the block's side, at most 8 | 8 |
| `awning` | box, tilted 25 degrees | the block's commercial POIs (shops, bars, restaurants...) and `building=retail` buildings | 2 |
| `shop_sign` | box | a blank sign over each awning (the POI's name is in `params`, the text is empty) | 2 |

966 records in all. JSON is about 320 KB.

## Record schema

Every record is a flat dict, JSON-ready:

| Field | Meaning |
|-------|---------|
| `id` | Stable id: `<block>/<kind>/<parts>`, e.g. `blk_R3363343/wreath/W250264728`. Built from OSM ids, edge indices, street faces and station numbers, never from list positions. |
| `kind` | One of the kinds above. |
| `shape` | `box`, `cylinder`, `sphere` or `spline`. |
| `mesh` | The engine basic shape for the first three (`/Engine/BasicShapes/Cube`, `Cylinder`, `Sphere`), `null` for splines. |
| `position` | `[x, y, z]` in Unreal cm, the shape's centre (the basic shapes are 100 cm and centred). For a closed spline, the outline's centroid at its base. |
| `yaw`, `pitch`, `roll` | Unreal `Rotator(pitch, yaw, roll)` in degrees. Facade items: yaw is the wall's outward normal (local X out of the wall, Y along it). Wreaths have pitch -90 so the cylinder's axis points out of the wall. Awnings have pitch -25 (front edge down). Roll is always 0. |
| `scale` | `[sx, sy, sz]`: the size in cm over 100, so `scale` goes straight onto the basic shape. `[1, 1, 1]` for splines. |
| `collision` | Whether the spawned actor should block (snow banks, tree stand, steam vents). Everything else is decor with no collision. |
| `coating` | True for roof and parapet snow: it lies on its host's surface and counts as that surface (see constraints). |
| `material` | A greybox material key: `snow`, `evergreen`, `warm_window`, `candle`, `bulb_string`, `lumber`, `steam_stack`, `awning`, `sign_blank`. |
| `host` | The OSM id of the building it is fixed to, or `null` (steam vents, car snow). |
| `params` | Per kind. `string_light`: `points` (9 catenary points, absolute), `radius`, `sag`, `span`, `far_host`, `bulb_spacing`, `palette`. `roof_snow`: `closed`, `points` (outline, absolute), `thickness`. `window_light`: `floor`, `warmth`. `window_candle`: `window` (the light's id), `flame`. `wreath`: `door`, `street`, `bow`. `awning`: `colour`, `shop`, `name`. `shop_sign`: `text` (empty), `shop`, `name`. `snow_bank`: `dirty`. `tree_for_sale`: `netted`. `steam_vent`: `steam`, `stripes`, `road`. `car_snow`: `on_prop` (the car's prop id). `tree_stand` rail: `lights`. |

The file `plan_json` writes: `{"version", "block", "buildings", "frontage_m", "counts", "records"}`, sorted keys, no
spaces, floats rounded to 0.01 cm, LF ending. Records are sorted by kind (the table order) then id.

## Constraints

Each is a pure predicate in `_dressing.py`; the planner generates candidates and keeps only those that pass all of
them (`violations(record, scene)` lists every one a record breaks), then thins each kind to its density cap. The
tests call each predicate directly on hand-made cases and re-check every planned record against the whole scene.

| Constraint | Predicate | Rule |
|------------|-----------|------|
| Grapple lines stay clear | `blocks_anchor_line` | `_reach.GrappleReach` is run for every anchor against every pavement spot in range; each pair it counts as targetable contributes its camera sight line (lens to marker) and its zip line (launch point to landing, the capsule's feet), exactly as `seen` and `zip_clear` test them. No record, grown by 20 cm, may cross either. As in `_reach`, a sight line may be blocked within 60 cm of the marker, and a zip ignores coatings on the anchor's supporting roofs (they are the roof it lands on). 11,582 lines in the district, 3,235 near the hero block. |
| Keep-outs | `in_keepout` | Nothing reaches into: a 1.5 m cylinder round every chase waypoint (30 cm under to 2.5 m over), the toy drive (its arrival box, the hull round its props, crew and sacks plus 1.5 m, 7 m round the courier and the cruiser), the boss arena's roof and a 3 m circle round its street stand. |
| Fire escapes and ledges | `overlaps_structure` | No overlap with a fire escape's box (`_reach`'s: landings, rails and ladders, grown 20 cm), a traversable ledge slab (`_ledges`), or a building other than the host. Touching faces do not count (parapet snow sits on the ledge slab's top). |
| Street props | `hits_prop` | 30 cm from lamps (pole and head) and `clutter_plan`'s props; parked cars use their 4.5 x 1.8 m box. Coatings and a car's own snow are exempt. |
| Lights clear the street | `clears_street` | The lowest point of every string (less its radius) is 5.5 m or more over the pavement. |
| Density caps | `within_density_caps`, `apply_density_caps` | Per 100 m of the block's frontage: string lights 10, wreaths 14, window lights 160, candles 30, roof snow 40, parapet snow 120, snow banks 30, tree stand pieces 8, trees 20, steam vents 3, car snow 12, awnings 14, signs 14. Over a cap, the records whose ids hash lowest stay. |
| Deterministic | golden test | Candidates come in a stable order and every varied choice is `stable_hash` of the record's id, so the same scene gives byte-identical JSON. |

Why candidates were turned away on the hero block (`plan["rejected"]`, counted per attempt, and a span or awning
tries several heights or shifts): string-light attempts failed 36 times on fire escapes and 40 on anchor lines;
windows lost 92 to fire escapes and 44 to the ledge slabs over the top 4 m of every wall; awning attempts failed 18
times on anchor lines.

## What the data made impossible

- **No kerb on the avenues.** Avenue B and Avenue C carry OSM's 25 m avenue default width, which runs the
  carriageway under the facades (the planning world treats the asphalt by the wall as the pavement). There is no kerb
  line on those faces, so no snow banks or tree stand there; all 25 banks are on East 9th and East 10th.
- **Nothing across Avenue B or Avenue C.** Across Avenue B is Tompkins Square Park, and Avenue C is the district's
  east edge (no buildings in the data beyond it), so no string lights span those faces; all 10 are across the
  side streets.
- **Few shops.** The block has only six commercial POIs or retail buildings near its walls; two fit an awning clear
  of the fire escapes and the anchor lines (The Cabinet, World of Wines).
- **No doors in the data.** OSM has no entrances here, so a door is one per `addr:housenumber`, on the wall fronting
  the tagged street, placed along it by hash. 25 buildings are addressed and 22 get a wreath: one door is inside the
  toy drive's keep-out on Avenue B, one is on a 4 m shop whose ledge slab covers its whole wall, one building has no
  street wall.

## How the engine half should consume it

1. In the editor, `import _dressing`, build the scene once (`gather_scene(district)`, about 2.5 minutes, cached on the
   district) and plan: `plan = _dressing.plan_scene_dressing(scene, _dressing.hero_block_id(scene))`. Or write the
   JSON once from plain Python with `write_plan` and read it back; both give the same records.
2. For each record with a `mesh`: spawn a `StaticMeshActor` with that mesh at `position`, `Rotator(pitch, yaw, roll)`,
   `scale`. Label it `actor_label(record)` (`City_Dress_<kind>_<crc32 of id>`), tag it `Dressing` and the block id, and
   put it in a `Dressing/<block>` folder. Collision profile `BlockAll` when `collision` is true, else `NoCollision`.
   Material from `material` (greybox `MI_` instances; emissive for `warm_window`, `candle`, `bulb_string`).
3. For `string_light`: a spline (or one thin cylinder per segment between consecutive `points`) of `radius`, with a
   bulb every `bulb_spacing` cm. For `roof_snow` (`closed`): extrude `points` by `thickness`, the same footprint
   extrusion generate_city uses for buildings.
4. Idempotent like the other scripts: find existing actors by the `Dressing` tag and label, update what changed,
   delete what is no longer in the plan.
5. Do not re-check constraints in the engine; they are the plan's job and tested here.

## Tests

`test_dressing.py`:

- `GeometryTest` (5): the catenary, segment against prism, touching prisms, the roof inset, a wreath's rotated volume.
- `ConstraintTest` (8): each predicate on hand-made cases, both sides of each rule.
- `StreetPlanTest` (11): the whole planner over a made-up street: blocks split by the street, the hero fallback,
  every kind planned, every record passes every constraint, the span in an anchor's lines is dropped (and comes back
  when the anchor moves), a keep-out clears what was there, lights reach the facing facade, the shop's awning, unique
  ids and labels, the same bytes twice.
- `HeroBlockTest` (7): the hero block holds the toy drive, the golden hash, every record re-checked against the whole
  district, every kind present, nothing in a keep-out.

`generate_city.py` and the existing golden hashes are unchanged.
