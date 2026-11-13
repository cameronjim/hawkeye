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

## Shots and numbers

`Hawkeye.Screenshot.Block` (`Source/Hawkeye/Tests/BlockScreenshotTest.cpp`) writes
`Saved/Screenshots/Block/<shot>_<day|night>.png` for street, kate_street, kate_roof, facade, window and ground, and
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

1. **Clouds.** The day is hazy, not overcast: the sky atmosphere cannot grey the zenith. A low volumetric cloud layer
   (the engine's own cloud material, scripted) would give the flat white January sky and the orange-lit underside at
   night, and let the stars go. Measure its cost first (budget 1 ms).
2. **Depth on the facade.** Fire escapes and parapets are the only relief. Cornices, sills and lintels as real
   geometry on the hero block's street faces (a render-only mesh with no collision, so the audit and golden hashes
   stay), and a stoop at each door, would carry the silhouette the shader cannot.
3. **Street furniture and trees.** Tompkins Square's elms, newspaper boxes, bus shelters, parked cars with snow, a
   crosswalk at each corner: the ground reads real up close, but the street has too little in it.
