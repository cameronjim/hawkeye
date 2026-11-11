# December dressing: one hero block

The winter set dressing of one city block, from the district data, drawn as instanced meshes. Rules 2b and 2c hold:
nothing is hand-placed, every placement comes from the OpenStreetMap records and the planners that read them, and it
is one block, finished, before the next. Architecture summary: `architecture.md`, World.

| Layer | File | What it does |
|-------|------|--------------|
| plan | `Tools/Editor/_dressing.py` | pure; the district into placement records under the traversal constraints |
| committed plan | `Tools/Data/dressing/blk_R3363343.json` | written by `python -B Tools/Editor/write_dressing_plan.py` (about 3 min) |
| kit | `Tools/Editor/_dressing_kit.py` | pure; records into instance groups, the real lights, the shot cameras |
| engine | `Tools/Editor/_dressing_build.py` | editor; kit meshes and materials, the groups into `DA_EastVillage_CityProps`, lights, cameras |
| verify | `verify_city.py` `check_dressing`, `verify_content.py` `check_dressing` | counts against the committed plan |
| tests | `Tools/Editor/test_dressing.py` | constraints, a made-up street, the kit, the hero block, its golden hash and the committed file |
| shots | `Hawkeye.Screenshot.Dressing` | `Saved/Screenshots/Dressing/<name>.png` through the `City_DressShot_` cameras |

`generate_city.py` runs the engine layer: `ensure_city_props` appends the dressing's groups (kind `Dress_<group>`) to
the clutter groups, so the props hash covers them and `ACityLedgeSpawner` draws each group as one hierarchical
instanced mesh at load, transient, collision off the navmesh; `ensure_dressing_actors` places the lights and cameras.
A second run with the same plan saves nothing. Change the planner or the data, re-run the writer (the hero test fails
until you do), then `Tools\generate-city.ps1`.

## The hero block

`hero_block_id` picks the block holding the building the toy drive (chapter 2) has its back to, else the block under
chapter 1's chase start. On the committed data that is `blk_R3363343`: Avenue B to Avenue C, East 9th to East 10th,
30 buildings, 486 m of frontage, the Christodora House with the toy drive at its foot. Blocks come from
`partition_blocks` (footprints within 25 m with no street centre line between them); the id is `blk_` plus the
smallest OSM id.

## What is planned and drawn

| Kind | Records | Drawn as (group, mesh) |
|------|--------:|------------------------|
| `string_light` | 10 | `Dress_Wire` (a 4 cm bar per catenary segment, 80) and `Dress_Bulb_Red/Warm/Green/Blue` (`SM_Dress_Bulb`, 9 cm, every 30 cm, colours cycling from a hashed offset, 744) |
| `wreath` | 22 | `Dress_Wreath` (`SM_Dress_Wreath`, a ring 55 cm across and 10 deep), `Dress_WreathBow` (red box under it) and a ring of ten 5 cm warm bulbs on its face (a dark green ring on a dark wall does not read at night) |
| `window_light` | 369 | `Dress_Window_Dim/Warm/Bright` by `warmth`: a 2 cm emissive card over a window M_Facade paints |
| `window_candle` | 115 | `Dress_Candle` (cylinder) and `Dress_CandleFlame` (bulb mesh, emissive) |
| `roof_snow` | 29 | `Dress_RoofSnow`: one mesh for the block, `SM_Dress_RoofSnow_<block>`, every outline extruded 8 cm |
| `parapet_snow` | 418 | `Dress_ParapetSnow` (cube) |
| `snow_bank` | 25 | `Dress_SnowBank` / `Dress_SnowBankDirty` (sphere, half buried, collision) |
| `tree_stand` | 3 | `Dress_TreeStand` (cube, collision) and bulbs along the rail |
| `tree_for_sale` | 6 | `Dress_Tree` / `Dress_TreeNetted` (`SM_Dress_Tree`, stacked cones, collision) |
| `steam_vent` | 3 | `Dress_SteamStack` (cylinder, collision) and two `Dress_SteamStripe` bands each |
| `car_snow` | 8 | `Dress_CarSnow` (sphere) |
| `awning` | 2 | `Dress_Awning_<colour>` (cube, tilted 25 degrees) |
| `shop_sign` | 2 | `Dress_Sign` (cube, blank) |

1012 records, 2,161 instances in 25 groups: 25 instanced draws (plus shadow passes for the three that cast), not
1,012 actors. Materials: `M_DressGlow` (Color x Intensity, usable on instanced meshes) with
`MI_DressGlow_<key>` for the glows, `MI_Prop_Dress<key>` under `M_Prop` for the rest so snow settles on their tops.
Kit meshes live in `/Game/City/EastVillage/Dressing` with a `CityHash` recipe tag.

Light: the bulbs, windows and flames are emissive, not lights. Four movable point lights with no shadows
(`City_DressLight_String<k>` under the lowest point of three strings spread along the block, `City_DressLight_TreeStand`)
put colour on the street (`_dressing_kit.light_spots`, 450 / 350 lm, radius 9 / 6 m). The dressing is not part of
`UTimeOfDaySubsystem`'s night/day rescale: it glows by day.

## Record schema

Every record is a flat dict, JSON-ready:

| Field | Meaning |
|-------|---------|
| `id` | Stable id: `<block>/<kind>/<parts>`, built from OSM ids, edge indices, street faces and station numbers. |
| `kind`, `shape` | The kind above; `box`, `cylinder`, `sphere` or `spline`. |
| `mesh` | The engine basic shape for the first three, `null` for splines (the kit picks the drawn mesh). |
| `position` | `[x, y, z]` world cm, the shape's centre (basic shapes are 100 cm and centred); a closed spline's outline centroid at its base. |
| `yaw`, `pitch`, `roll` | `Rotator(pitch, yaw, roll)`. Facade items: yaw is the wall's outward normal. Wreaths pitch -90 (axis out of the wall), awnings -25. |
| `scale` | Size over 100 cm, straight onto the 100 cm unit meshes. `[1, 1, 1]` for splines. |
| `collision` | True for snow banks, the tree stand, the trees and the steam stacks. |
| `coating` | Roof and parapet snow: lies on its host and counts as that surface for the zip test. |
| `material`, `host` | A material key; the OSM id it is fixed to (or `null`). |
| `params` | Per kind. `string_light`: `points` (9, absolute), `radius`, `bulb_drop`, `bulb_size`, `bulb_spacing`, `sag`, `span`, `far_host`, `palette`. `roof_snow`: `closed`, `points`, `thickness`. `window_light`: `floor`, `warmth`. `window_candle`: `window`, `flame`. `wreath`: `door`, `street`, `bow`. `awning`: `colour`, `shop`, `name`. `shop_sign`: `text` (empty), `shop`, `name`. `snow_bank`: `dirty`. `tree_for_sale`: `netted`. `steam_vent`: `steam`, `stripes`, `road`. `car_snow`: `on_prop`. `tree_stand` rail: `lights`. |

The file: `{"version", "block", "buildings", "frontage_m", "counts", "records"}`, sorted keys, no spaces, floats to
0.01 cm, LF ending, records sorted by kind then id.

## Constraints

Each a pure predicate in `_dressing.py`; candidates are kept only when they pass all of them, then thinned to the
density caps. The engine does not re-check or move anything: a clearance that proves wrong in the game is fixed here.

| Constraint | Predicate | Rule |
|------------|-----------|------|
| Grapple lines | `blocks_anchor_line` | No record, grown 20 cm, crosses a sight line (lens to marker, blocked short of 60 cm allowed) or zip line `_reach.GrappleReach` counts from a pavement spot. Coatings on the anchor's own roofs are exempt from the zip. |
| Keep-outs | `in_keepout` | 1.5 m round every chase waypoint, the toy drive (its props and people plus 1.5 m, 7 m round the courier and the cruiser), the boss arena's roof and stand, and every safehouse and interior door (`door_keepout`: 2 m either side, 2.5 m out, up to 6 m for the sign; the second safehouse is on this block). |
| Structures | `overlaps_structure` | No overlap with a fire escape's box, a traversable ledge slab, or a building other than the host. |
| Street props | `hits_prop` | 30 cm from lamps and clutter props; parked cars by their box. |
| Street clearance | `clears_street` | A string's lowest point less its envelope (wire radius plus the bulbs' drop and size, `string_envelope`) is 5.5 m or more over the pavement. |
| Density caps | `apply_density_caps` | Per 100 m of frontage: strings 10, wreaths 14, windows 160, candles 30, roof snow 40, parapet snow 120, banks 30, stand pieces 8, trees 20, steam 3, car snow 12, awnings 14, signs 14. |
| Deterministic | golden test | Stable candidate order; every varied choice is `stable_hash` of the id. |

Changes made once the engine half existed: windows sit on M_Facade's painted grid (160 cm bays on the facade's own
horizontal coordinate, `world y * nx - x * ny`, half-bay centres; 330 cm storeys from world z = 0, sill 95, 110 x 150,
upper floors from 420 cm up to 185 cm under the wall top), so a lit card covers a painted window instead of straddling
brick; a string's volume and clearance include its bulbs (`string_envelope`), and strings hook 20 cm off each wall
(was 5) so that envelope does not reach into the far building; the doors' keep-out (one window card over the second
safehouse's sign went).

## What the data made impossible

- No kerb on the avenues: Avenue B and C carry OSM's 25 m default width, so the carriageway runs under the facades;
  all snow banks and the tree stand are on East 9th and East 10th.
- Nothing spans Avenue B (Tompkins Square Park across it) or Avenue C (the district's east edge): all strings are
  across the side streets.
- Few shops: six commercial POIs or retail buildings, two fit an awning clear of fire escapes and anchor lines.
- No doors in the data: one per `addr:housenumber`, on the wall fronting its street, placed by hash.

## Tunables

- Plan: the constants at the top of each pass in `_dressing.py` (string spacing, heights, sag, bulb drop and size;
  window lit share; bank step and gaps; tree stand corner; density caps). Each change moves the golden hash: re-run
  the writer and record the new hash in `test_dressing.GOLDEN`.
- Look: `_dressing_build.GLOWS` (emissive colour and strength per glow) and `COLOURS`; `_dressing_kit` (bulb size,
  bow, flame, window warmth buckets, stripe bands, the real lights' count, lumens and radius, the shot cameras).
  A look change needs `Tools\generate-city.ps1`, not the writer.
