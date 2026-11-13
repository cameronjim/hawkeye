# City look: facades, windows, ground, light

How the district's surfaces and light are made, by script, with nothing imported or downloaded. Rules 2b and 2c
hold: every material is built by `Tools/Editor/_materials.py` from `Tools/Editor/_look.py`, every actor setting by
`generate_city.py`, and progress is judged on the hero block round the PlayerStart (East 7th Street across from
Tompkins Square Park) through `Hawkeye.Screenshot.Block`.

## What is built

| Asset | Built by | What it draws |
|-------|----------|---------------|
| `M_Facade` + `MI_Facade_<style>` | `_look.build_facade`, `FACADE_HLSL` | every building wall and roof |
| `M_WindowLit` | `_look.build_facade(card=True)` | the dressing's lit window cards (`MI_DressGlow_Window*`): the facade's window, always lit |
| `M_StreetAsphalt` | `_look.build_ground("road")`, `ROAD_HLSL` | carriageways |
| `M_SidewalkSnow` | `_look.build_ground("sidewalk")`, `SIDEWALK_HLSL` | sidewalks, kerbs, and `City_Ground` as a plaza |
| `M_ParkSnow` | `_look.build_ground("park")`, `PARK_HLSL` | the park |

Each is one HLSL Custom expression fed by world position, the vertex normal, the camera vector and a few
parameters; normals come out in world space (`tangent_space_normal` off), so the generated meshes need no UVs or
tangents. The graphs carry the `HawkeyeBuild` tag `block-1/<hash of the HLSL>`: editing `_look.py` rebuilds them on
the next `generate-city.ps1` or `create-content.ps1`, and a run with nothing edited saves nothing.

### Facade (`FACADE_HLSL`)

- **Coordinates.** Along the face: world XY on the face's own tangent (up x normal), so each face of any footprint
  gets a straight grid that restarts at its corners. Up: world Z. The mesh top (object bounds) places the cornice.
- **Masonry at real size**, per style (`_look.MASONRY`): brick 20 x 6.5 cm in 1 cm mortar (21 x 7.5 cm pitch),
  running bond, a header course every sixth (NYC common bond); brownstone ashlar 91.5 x 33 cm in 0.7 cm joints;
  board-marked concrete panels 160 x 110 cm for the towers; painted brick (paint from a small palette, peeling to
  the brick). The joints are box-filtered against the pixel footprint (`FLook.Joint`, after Inigo Quilez), and the
  per-brick colour, mottling and bevel normals fade out as a brick gets under two pixels, so a wall far away is its
  average colour instead of shimmering.
- **Colour per building**: the style's colour times a hash of the building (0.78 to 1.2, warmer or cooler), per brick
  0.74 to 1.2 with a few dark clinkers. Styles stay in `_materials.FACADE_STYLES` (BrickPurple is now a dark
  ironspot brick, Stone a grey concrete).
- **Weather**: large blotches, a dark base over the first 2.8 m with a white salt line at 20 to 75 cm, soot under the
  cornice, streaks below each sill.
- **Windows** on the grid the dressing's cards already use (160 cm bays, 330 cm storeys from z 0, sill 95, 110 x 150,
  upper floors from 420 cm to 165 cm under the top): a stone lintel and sill (trim colour) with snow on top and a
  shadow under the sill; the opening recessed 16 cm (brownstone 22, concrete 10). The reveal, the double-hung sash
  (frame, meeting rail; white, black or green per building), a window air conditioner in 16 %, the glass, blinds and
  curtains, and the room behind are all ray-cast in the shader from the camera vector: no geometry, collision
  untouched.
- **Rooms (interior mapping)**, `FLook.Room`: a box 2.8 to 4.2 m wide and 3.5 to 6 m deep per window, floor and
  ceiling at the storey's, painted walls from a palette, a sofa or dresser and a picture on the back wall, a board
  floor and a rug, a ceiling lamp whose falloff lights it. The glass is dark and glossy (each sash a little out of
  plane so reflections break up); the room is emissive. Lit at night: 35 % of windows (15 % painted), a hash of bay,
  storey and facade plane; 70 % warm (`LitColor`), 20 % cool white, 10 % TV blue, brightness 0.6 to 1 times 2.5.
  Unlit rooms glow at 3 % by night and up to 63 % by day, read from the sky atmosphere light's illuminance (moon
  0.6 lux, sun 12), so day and night need no parameter of their own.
- **Ground floors** of buildings over 7 m: storefronts 256 cm on a 320 cm pitch with an interior-mapped shop
  (shelves of goods, a chequered floor, uniform light), roll-down gates (ribbed, tagged) on the closed ones at night,
  and a sign band with a coloured panel and block lettering, faintly lit at night.
- **Cornice** with dentils under the parapet and its black shadow line; **roofs** tar under `MF_Snow`, with drift
  bumps in the normal.
- `WindowGlow` is still the parameter `UTimeOfDaySubsystem` scales (x0.75 by day).

### Ground

- **Street frame.** `_look.street_frame` gives each road its direction (longest segment), centre-line offset and half
  width; `generate_city` writes them as custom primitive data 0 to 3 on the road's and its sidewalks' actors
  (`custom_primitive_data`, the saved property). `City_Ground` gets the grid direction with a half width of 1e6
  (plaza: flags, no kerb, no path).
- **Road**: asphalt 0.045 with aggregate, patches and cracks; two wheel tracks per 3.3 m lane worn through compacted
  snow to wet asphalt (roughness 0.1); slush between; a plowed, dirty ridge in the last metre at the kerb; the
  parking lanes (2.5 m) snowed in; a double yellow line on roads over 18 m wide.
- **Sidewalk**: 5 ft (152.4 cm) concrete flags with joints, grain and cracks on a few; a granite kerb 18 cm wide with
  a steel edge on its face; a trodden path down the middle, banks by the building line and the kerb, salt stains and
  wet dark patches where it is clear.
- **Park**: drifted snow (three scales of height in the normal, wind ripples), patches of grass, glints within 14 m.

### Light (`generate_city.py` constants for the night, `TimeOfDaySubsystem::MakeDay` for the day)

- Night: moon 0.6 lux (was 0.3), source angle 4 degrees, sky light 5 (was 3), fog 0.01 with a warm city-glow
  inscattering (0.07, 0.05, 0.055). The sky atmosphere is set as winter haze for both: Rayleigh 0.022 (engine 0.0331),
  Mie 0.012 (0.004), anisotropy 0.6 (0.8); moon colour (0.72, 0.8, 1) (was a saturated (0.55, 0.65, 1)).
- Day, winter haze: sun 12 lux (was 16) 22 degrees up, nearly white; sky light 11 cool (was 8 warm); fog 0.008 grey from
  15 m (was 0.0015 warm from 30 m), exposure bias 0 (was -0.3). Exposure stays fixed (EV100 1, bias +2 night, 0 day).
- Lumen GI and reflections as before; no MegaLights (285 spot lights cost GPU 4 to 5 ms already measured flat
  between day and night, `infrastructure.md`), no volumetric clouds or fog (the haze is the atmosphere's Mie).

## Look pass 2 (2026-11-13): shops, sky, relief, street

- **Shops** (`SHOP_HLSL`, in `FACADE_HLSL`). Bays of 280, 330 or 400 cm per building (a hash of the building) with
  piers 36 to 70 cm, a trade per bay from `_look.SHOPS` (generic names only: LAUNDROMAT, DELI & GROCERY, PIZZA,
  HARDWARE, PHARMACY, BARBER, BAKERY, CLEANERS, LIQUORS, NAIL SALON, CAFE, TAILOR, FLOWERS, DINER, WINE & LIQUOR,
  GROCERY). The sign spells the name in a 5 x 7 pixel font held as bit rows in the shader (`_look.FONT`), in one of
  three faces (regular, bold, condensed), centred and fitted to the panel, in five colour pairs, read left to right
  as the viewer sees the face (the face's coordinate runs either way), and fading to an average ink when a letter
  cell is under a couple of pixels. Eight interior kinds (`FShop.Room`): washers and dryers, shelves of groceries
  with glowing fridges, a counter under a menu board, a dark dense hardware wall, a pale pharmacy, mirrors and
  chairs, racks of clothes, buckets of flowers; floors and light colour by kind. By day an open shop is darker than
  the street and 40 % desaturated; 15 % of shops are vacant and gated day and night, and half the rest gate at
  night. Mullions and transoms vary per shop.
- **Sky**: `City_CloudDeck`, the engine sphere (2.5 km radius) seen from inside in `M_CloudDeck` (unlit,
  `CLOUD_HLSL`): drifting cloud bellies on a plane overhead, a flat white-grey overcast by day, brighter at the zenith,
  and a dull orange-brown city-lit underside by night, brightest at the horizon. Day or night from the atmosphere
  light's illuminance like the rooms. It is inside the fog's cutoff so the horizon melts into the haze, outside
  nothing but the star sphere (which it now hides), and left out of the sky light's capture, no shadow, no
  collision, no distance field. The day's sun drops to 7 lux and the sky light rises to 13 under it. Cost: within
  measurement noise (below).
- **Relief** (`_street_look.relief`, render only, NoCollision, so the traversal audit and golden hashes do not
  move): on every street face (a wall whose normal meets a road centre line within 26 m) of the buildings within
  120 m of the PlayerStart, boxes in the building's trim (cream for brick, brown for brownstone, grey for concrete
  and painted): a three-step cornice under the parapet and a shop cornice over the sign band on buildings over 7 m,
  a sill (12 cm proud) and lintel on every window M_Facade draws (its grid on the face's own coordinate), one belt
  course over the shops on fronts of 20 m or more, a door with pilasters, a pediment and a two-step stoop per
  building, and canvas awnings (green, red, navy, cream; 50 degrees so snow slides off) on 35 % of the shop bays,
  placed on the same bays as the shader by evaluating its hash in float32. One mesh per material,
  `SM_Street_<key>`, in world cm, under `/Game/City/EastVillage/Street`, tagged with a hash of its boxes.
- **Street dressing** (render only): bare street trees (`SM_Street_Tree`: a trunk, four limbs, three branches each,
  two twigs each; scale 0.85 to 1.15) every 9 m along the sidewalks within 120 m, 1.1 m in from the kerb and 2.5 m
  clear of lamps, hydrants, bins, bags, doors, test blocks and the start, in tree pits with a steel edge; the park's
  elms on a jittered 17 m grid within 160 m of the start (scale 1.25 to 1.65); ladder crosswalks (`M_RoadPaint`,
  worn white) across each road at the corners; two newspaper boxes. Parked cars keep their box but the cabin
  narrows to the roof and `M_Car` paints tyres, bumpers, a trim line, glass round the cabin and lamps (lit at
  night), snow on top.
- **Street blocks**: the parkour test blocks keep their Game Animation Sample actor (and collision) but are hidden;
  a render-only cube of the same size wears `M_StreetBlock`: the vault a concrete jersey barrier with an orange and
  white band, the mantle a ribbed green dumpster with rust, the park walls granite.
- **Ground and snow**: wheel tracks wider and darker (roughness 0.22, compacted snow 0.72 cover between them), a
  dirtier kerb ridge; cracks only in patches (they had been long winding contour lines of the noise, the "dark seam"
  down the road). Snowflakes: no nearer than 3.5 m (`NS_Snowfall`'s recycler, was 1.2 m) and 3.5 to 7 cm (was 5 to 9).
- Not done (in pass 2): signs cut where a building ended, manholes and steam, the long fronts' sameness. Done in pass 3.

## Look pass 3 (2026-11-13): lettering, facades, parked cars, trees, night

- **Sign lettering** (`bake_sign_atlas.py`, `_sign_font.py`, `T_SignAtlas`). The 5 x 7 pixel font is gone. A plain
  CPython script (Pillow and numpy, not the editor) rasterises capitals, digits and `& ' - . $ /` from four faces of
  the engine's own Roboto (`Engine/Content/Slate/Fonts`: Bold, Bold Condensed, Regular, Bold Italic) at 4x and writes
  a signed-distance atlas, 8 x 8 cells of 128 px, one face per channel (`Tools/Data/look/sign_atlas.png`, committed,
  LFS) and the glyph advances (`_sign_font.py`, generated). `--check` rebakes and compares. `_look.ensure_sign_atlas`
  imports it as `/Game/Materials/T_SignAtlas` (RGBA8 uncompressed, linear), again only when the file's hash changes.
  M_Facade lays each trade's name out on the advances, centred and fitted to its panel, samples the atlas with
  `SampleLevel` (the LOD from the pixel footprint) and antialiases the 0.5 edge by the footprint; three neighbouring
  glyphs are sampled so italics and wide letters may overhang their advance. Four treatments: painted fascia (five
  colour pairs, 55 % faintly lit at night), raised letters (gold or white on black, bronze or brick, with a drop
  shadow, half of them halo-lit at night), a lit box sign (white, yellow or blue face in a dark frame, glowing at
  night) and neon (Regular or Italic tubes, pink, blue, orange or green, a halo on a black board; pale glass by day).
  26 generic trades.
- **Facades and bays** (`_facade_plan.py`, shared by M_Facade and `_street_look`). Each building actor carries its
  frame as custom primitive data 0 to 6: its long axis A (an edge of its smallest bounding rectangle), the ring's
  extents along A and B, and an integer seed. A wall whose tangent is within 14 degrees of A or B spans the whole
  extent on that axis. Spans over 13 m are cut into facades about 13 m wide (cuts jittered 30 % and snapped to the
  160 cm window grid, none under 6.4 m), each its own building: one of seven palettes (red, brown, dark ironspot and
  buff brick, brownstone, limestone ashlar, painted), its own colour hash, window heads (a flat stone lintel, a
  segmental soldier-brick arch, a full stone surround, a projecting hood), pilasters grouping the windows in twos or
  threes on some, its cornice (stepped stone with dentils, brackets under a slab, a plain coping, or a deep
  pressed-metal cornice in green, black or cream), a dark seam where two meet. The window grid itself (160 cm bays,
  330 cm storeys) is unchanged: the fire escapes, the December dressing's 367 lit window cards and the window shot
  sit on it, so storey height does not vary per facade. Every choice hashes integers with lowbias32 (`FLook.IH` in
  the shader, `ih` in Python, pinned by `test_facade_plan.py`), so the relief and the shader agree exactly.
- **Shopfronts**: each facade's ground floor is a row of bays from its own corner to its own corner (45 cm end piers,
  pitch 300, 360 or 420 cm per facade, fitted to a whole number), one sign per bay centred in it with 14 cm clear
  either side, so no sign crosses a building edge or another sign; with three bays or more the middle one is the
  tenement's entrance (a door 60 cm in with a lit transom, a relief surround, pediment and stoop). Each shop has a
  door recessed 85 cm (tiled floor, jambs, a glass door in an aluminium frame with a push bar, the shop seen through
  it), a bulkhead (tile, steel or painted wood), mullions and transoms; closed shops are gated over glass and door.
  The interiors gained depth: counters, shelf gondolas running back, a bank of machines in laundromats, pendant
  lamps in the warm-lit shops, ray-cast as boxes in front of the room's walls.
- **Night**: the street lamps are 1200 lm (was 1100), 24 m reach (18), outer cone 80 degrees (70), inner 45 (35), so
  the pool reaches the ground and first floors of their own side. 48 open shops within 90 m spill light: a spot just
  outside the glass, 240 lm, 8 m, no shadows, warm or fluorescent by trade, tagged `CityLamp` and `CityShopLight`
  (`City_ShopLight_<n>`) so the time of day and the EMP switch them with the lamps. M_Facade adds the city-lit
  overcast on every wall at night (`NightFill` 0.06 x its colour, warm), so upper floors read as brick against the
  orange sky instead of a black mass.
- **Street** (render only, NoCollision): 33 parked cars on both kerbs of the roads within 90 m of the start
  (`City_Car_<n>`, `SM_Street_Car_Sedan|SUV|Van`: side profiles extruded, a narrower glasshouse, four wheels;
  `MI_Car_<shape>_<colour>` under M_Car, whose `Shape1..3` place the glass, pillars, wheel wells, rims and lamps per
  body), nose to tail with 0.7 to 2.4 m gaps, 12 % of spaces empty, parked with the traffic, clear of crossings by 7 m
  and of every clutter keepout, patrol line, hydrant zone and solid clutter car; the 17 solid clutter cars near the
  start are unchanged. 21 manholes (`City_Manhole_<n>`, `M_Manhole`, masked: an iron cover in its frame and a ragged
  ring of wet black asphalt the steam keeps clear), the three nearest the start steaming (`City_Steam_<n>`, `M_Steam`:
  two open cones, unlit translucent, a rising noise, soft at the silhouette). 554 lumps of snow
  (`SM_Street_SnowPiles`, Perlin-displaced spheres merged into one mesh, `M_SnowPile`: clean on top, grey-brown low
  and on the sides): windrows of three to five lumps along the kerbs in the gaps between cars, one in each tree pit and
  behind each hydrant. Eleven sign poles (`City_Sign_<n>`, `M_StreetSign`: NO PARKING with its small print, a BUS STOP,
  a ONE WAY arrow, lettered from the atlas) and two mailboxes.
- **Trees**: three street-tree and two park-elm meshes (`SM_Street_Tree_0..4`), grown in five orders (trunk, limbs,
  branches, shoots, twigs) from tapered round segments that bend at every joint and overlap so a bend leaves no gap,
  children spread round the parent's outer half, the outer orders arching out; a leader on the street trees, a vase
  on the elms. About 3,000 triangles each, smooth normals; `M_TreeBark` is fissured bark with snow lying along the
  upper side of every branch and twig. 178 trees as before; pass 2's stick mesh is deleted.
- **Ground**: the road is wet black wheel tracks (roughness 0.42) with grey-brown slush between them and at the lane
  lines, the parking lanes packed and dirty, the plowed ridge at the kerb; the sidewalk has a path shovelled down the
  middle (75 to 255 cm from the kerb, a ragged edge) with banks either side that fall away into it in the normal.

## Shots and numbers

`Hawkeye.Screenshot.Block` (`Source/Hawkeye/Tests/BlockScreenshotTest.cpp`) writes
`Saved/Screenshots/Block/<shot>_<day|night>.png` for street, kate_street, kate_roof, facade, window, ground,
shopfront, wide, kerb (along the shop-side kerb: parked cars, windrows, signs) and tree (up into the nearest street
tree), and
logs `BlockPerf` for the two Kate shots (3 s of frames: average, p95, game, draw, GPU). The window shot finds a lit
window with the shader's own hash. Run it from the standalone game:

    UnrealEditor-Cmd.exe Hawkeye.uproject /Game/Maps/L_District_EastVillage -game -windowed -ResX=1280 -ResY=720
        -unattended -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.Block; Quit"

Measured 2026-11-13 on this machine (RTX 4070, i7-13700K), standalone `-game`, editor binaries, uncooked, 3 s per
shot; ms, frame average / p95 (stat unit's GPU average). Before is main at 839ee45 (the painted facades), after is
this look. The frame is bound by the render thread's draw, not the GPU; the materials add about 0.1 ms of GPU.

| Shot | Before 1280x720 | After 1280x720 | After 1920x1080 |
|------|-----------------|----------------|-----------------|
| kate_street, day | 4.80 / 5.31 (GPU 3.48) | 4.94 / 5.45 (GPU 3.59) | 5.48 / 5.76 (GPU 4.17) |
| kate_roof, day | 5.93 / 6.25 (GPU 4.50) | 6.04 / 6.37 (GPU 4.61) | 6.60 / 6.92 (GPU 5.20) |
| kate_street, night | 5.50 / 5.81 (GPU 4.12) | 5.59 / 5.91 (GPU 4.21) | 6.16 / 6.43 (GPU 4.82) |
| kate_roof, night | 6.45 / 6.84 (GPU 4.93) | 6.56 / 6.88 (GPU 5.02) | 7.10 / 7.37 (GPU 5.63) |

Every shot is under 7.4 ms at p95 at 1080p (135 fps and up), far inside 60 fps (16.7 ms).

After look pass 2 (shops, cloud deck, relief, 178 trees, street dressing), 1920x1080: kate_street day 5.58 / 5.92
(GPU 4.26), kate_roof day 6.74 / 7.01 (5.34), kate_street night 6.27 / 6.55 (4.90), kate_roof night 7.17 / 7.44 (5.72): about
0.1 ms more than pass 1, slowest p95 7.44 ms.

After look pass 3 (atlas signs, facades, 33 cars, 554 snow lumps, new trees, 48 shop lights, wider lamps),
1920x1080: kate_street day 5.76 / 6.01 (GPU 4.41), kate_roof day 7.04 / 7.32 (5.66), kate_street night
6.66 / 6.98 (5.26), kate_roof night 7.56 / 7.85 (6.10): about 0.4 ms more than pass 2, slowest p95 7.85 ms.

## Research and sources

Done briefly before building; what fits a 16 GB / RTX 4070 machine and a no-download rule.

- Interior mapping: Joost van Dongen, "Interior Mapping: A new technique for rendering realistic buildings" (CGI 2008)
  and his write-up prompted by Marvel's Spider-Man, https://joostdevblog.blogspot.com/2018/09/interior-mapping-real-rooms-without.html;
  Epic's UDK gem https://docs.unrealengine.com/udk/Three/DevelopmentKitGemsInteriorMapping.html; Insomniac's use in
  Spider-Man, https://automaton-media.com/en/news/20231201-23558/ and the GDC 2019 talks
  https://gdcvault.com/play/1026495/-Marvel-s-Spider-Man. Chosen: an analytic box room per window in the shader
  (no cubemaps to make), with the reveal and sash ray-cast the same way.
- Filtering procedural patterns: Inigo Quilez, https://iquilezles.org/articles/filtering, and
  https://www.gamedeveloper.com/programming/antialiasing-procedural-shaders-in-the-unreal-engine. Chosen: box-filtered
  joints and footprint fades instead of baked textures, so nothing is imported.
- Fog and atmosphere: https://dev.epicgames.com/documentation/unreal-engine/exponential-height-fog-in-unreal-engine
  (inscattering is additive to the sky atmosphere; the atmosphere light drives both). Chosen: the atmosphere's
  Rayleigh/Mie for the haze, the height fog for distance, no volumetric fog.
- Not used: MegaLights (the 285 street lamps are already cheap, `infrastructure.md` Performance), volumetric clouds
  (a real overcast needs them; next), Megascans/Fab (needs the owner's account; the brief says build our own).

## Next

1. **Car bodies from Blender.** The extruded profiles read as cars at a distance but are flat-shaded slabs close up:
   rounded bodies with real wheel arches, bumpers, mirrors and a windscreen with depth, three or four built headless
   like the costumes; then make the hero street's cars solid once the laps tolerate them.
2. **Storey height and depth per facade.** The window grid is shared with the fire escapes and the dressing's cards,
   so facades differ in everything but storey height and wall plane. Give each facade its own storey height and set
   some back or forward a few centimetres (render-only skins), with the fire escapes and cards following the plan.
3. **Snow and wet on everything.** The windrows are smooth lumps: give them a plowed cross-section and chunks, put
   snow on the cars' bonnets as geometry, wet reflections under the lamps at night, and footprints and tyre ruts in
   the parking-lane snow.
