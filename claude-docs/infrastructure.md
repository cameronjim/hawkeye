# Infrastructure

Paths, commands, and the machine. Everything here is Windows.

Renamed from Castle to Hawkeye on 2026-09-26; CoreRedirects in DefaultEngine.ini map the old script package and classes.

Prison content removed 2026-09-26; the district is the only map.

## Paths

| Thing | Path |
|-------|------|
| Project | `C:\Users\camer\code\hawkeye` |
| Engine | `C:\Program Files\Epic Games\UE_5.8` |
| Editor | `...\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe` |
| Headless editor | `...\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe` |
| Build script | `...\UE_5.8\Engine\Build\BatchFiles\Build.bat` |
| UAT (packaging) | `...\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat` |
| Visual Studio | `C:\Program Files\Microsoft Visual Studio\2022\Community` |
| Logs | `C:\Users\camer\code\hawkeye\Saved\Logs\Hawkeye.log` |
| Crash dumps | `C:\Users\camer\code\hawkeye\Saved\Crashes\` |

Set these as variables at the top of any script:

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
$Proj = "C:\Users\camer\code\hawkeye\Hawkeye.uproject"
```

## Build

Generate project files (after adding or removing source files, or changing Build.cs):

```powershell
& "$UE\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="$Proj" -game -rocket -progress
```

Build the editor target (what you run 95% of the time):

```powershell
& "$UE\Engine\Build\BatchFiles\Build.bat" HawkeyeEditor Win64 Development -project="$Proj" -waitmutex
```

Build the standalone game target (for packaging tests):

```powershell
& "$UE\Engine\Build\BatchFiles\Build.bat" Hawkeye Win64 Development -project="$Proj" -waitmutex
```

Exit code 0 is success. Errors look like `error C2065` (compiler) or `Error: ... UnrealHeaderTool`
(reflection). UHT errors come first and block compilation; fix them before reading further.

`Source/Hawkeye.Target.cs` and `HawkeyeEditor.Target.cs` must stay on
`BuildSettingsVersion.V7` and `EngineIncludeOrderVersion.Unreal5_8`. Older values make UBT
refuse to build against the launcher engine (it reports `OtherCompilationError` in under a
second with no compiler output). 5.8 also writes `Hawkeye.slnx` next to `Hawkeye.sln`; both are
ignored by git.

Build time: about 80 seconds for a full rebuild of the Hawkeye module, 20-40 seconds incremental.
If the editor is open, close it or use Live Coding (Ctrl+Alt+F11 in the editor) instead.
Building with the editor open and Live Coding off produces a DLL the editor won't reload.

## Config facts worth knowing
- `DefaultEngine.ini` declares the `Weapon` trace channel (`ECC_GameTraceChannel1`).
  Don't reorder or renumber custom channels; assets store the enum value.
- `RuntimeGeneration=Dynamic` on `RecastNavMesh` is what lets headlessly generated maps
  have a navmesh at all. Removing it silently freezes every guard.
- The Python plugin is enabled for headless asset scripts. Its stub, when present, is at
  `Intermediate/PythonStub/unreal.py` and is the fastest way to check property names.

## Running the editor

Full editor (visual work only):

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor.exe" "$Proj"
```

Play a specific map in a standalone window without the editor UI (fast iteration on feel):

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor.exe" "$Proj" /Game/Maps/L_District_EastVillage -game -windowed -ResX=1600 -ResY=900 -log
```

Day, to see the block: add `-TimeOfDay=Day` to that line or to any automation run (not saved), or
type `hawkeye.TimeOfDay Day` in the console (`Night` to go back, `Saved` for the player's own
setting; also unsaved). `Hawkeye.Screenshot.TimeOfDay` writes `night_street.png`,
`day_street.png`, `day_park.png` (from the park at the tenement row) and `day_pedestal.png`
(challenge_pedestal's framing) to `Saved/Screenshots/Kate/` without the rest of the Kate pass,
in about two minutes; tune the day against those, at the same cameras. Two cautions reading
them: the pedestal's floating target icon spins, so it can show its plain cream back, and a
flake near the lens is a big soft white disc.

## Unreal MCP (live editor control, set up 2026-10-26)

UE 5.8 ships Epic's experimental `ModelContextProtocol` plugin: an MCP server inside the editor
process, loopback only (127.0.0.1), no auth, streamable HTTP at `http://127.0.0.1:8000/mcp`. The
project enables it plus the Toolset Registry and these toolsets in `Hawkeye.uproject`: Editor
(viewport and asset captures, actor and asset selection, camera, content browser, console
variables, PIE start and stop, output log), Automation Test (discover, list, run, results),
Slate Inspector (screenshot, click, type, inspect widgets), Live Coding (compile), Niagara
(system, emitter, module editing), Config Settings. The registry also loads Python toolsets for
actors, assets, blueprints, data assets, data tables, materials, meshes, textures, string tables.
`Config/DefaultEditorPerProjectUserSettings.ini` sets the server to auto-start with the editor.

- Client config: `.mcp.json` in the project root, `{"mcpServers":{"unreal-mcp":{"type":"http",
  "url":"http://127.0.0.1:8000/mcp"}}}`. Generate it from the editor console with
  `ModelContextProtocol.GenerateClientConfig ClaudeCode` (also Cursor, VSCode, Gemini, Codex,
  All). It is git-ignored; each clone makes its own. Start Claude Code from the project root
  after the editor is open; `/mcp` should show `unreal-mcp` connected.
- The server exposes three meta tools (`list_toolsets`, `describe_toolset`, `call_tool`) with
  `bEnableToolSearch` on. `call_tool` takes `toolset_name` (e.g. `EditorToolset.EditorAppToolset`)
  and the SHORT `tool_name` (`CaptureViewport`, not the dotted path). Every input the schema lists
  must be present, including optional ones (`pattern: ""`, a full `captureTransform` with
  `annotations`); omitted fields are an error, not a default.
- Console: `ModelContextProtocol.StartServer`, `.StopServer`, `.RefreshTools`. Settings under
  Edit, Editor Preferences, General, Model Context Protocol.
- It only runs while the full editor is open, which is the heavy process on this machine. Agents
  keep using the headless commands below for builds, tests and content; the MCP is for a human
  session with Claude looking at and poking a live editor. Never run it alongside a headless
  engine process. Smoke-tested 2026-10-26 with raw JSON-RPC: initialize, tools/list, 27 toolsets
  listed, `GetLogEntries` read the plugin's own log.

## Headless editor

Use `UnrealEditor-Cmd.exe` with `-nullrhi` for anything that doesn't need a screen.
Common flags: `-unattended` (no dialogs), `-nosplash`, `-nop4`, `-stdout`,
`-FullStdOutLogOutput` (log to console), `-NoLogTimes` (cleaner diff-able logs).

Run automation tests:

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" -ExecCmds="Automation RunTests Hawkeye; Quit" -unattended -nullrhi -nosplash -nop4 -stdout -FullStdOutLogOutput -ReportExportPath="C:\Users\camer\code\hawkeye\Saved\Automation"
```

Run a Python script in the editor (asset creation, batch edits):

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" -run=pythonscript -script="C:\Users\camer\code\hawkeye\Tools\Editor\create_input_assets.py" -unattended -nullrhi -nosplash -nop4 -stdout -FullStdOutLogOutput
```

Requires `PythonScriptPlugin` enabled in the .uproject. Scripts live in `Tools/Editor/`.
Python has `import unreal`. Useful entry points: `unreal.AssetToolsHelpers.get_asset_tools()`
for creating assets, `unreal.EditorAssetLibrary` for save/load/rename,
`unreal.EditorLevelLibrary` / `unreal.LevelEditorSubsystem` for maps,
`unreal.BlueprintEditorLibrary` for Blueprint parents and reparenting. Some things
(Blueprint graph nodes, widget trees) are not scriptable; do those by hand in the editor
or in C++ instead.

Cook content (checks for broken references and packaging problems without a full package):

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" -run=Cook -TargetPlatform=Windows -unattended -nullrhi -stdout
```

## Game Animation Sample (not in git)

Kate's locomotion comes from Epic's Game Animation Sample, installed at
`C:\Users\camer\code\GASP`. `Tools/Editor/import_gasp.py` copies about 2,030 packages
(2 GB, mostly `Content/Characters/UEFN_Mannequin`) into `Content/` at the same paths.
Those paths are in `.gitignore`: the repo is public and Epic's sample content shouldn't be
republished, and 2 GB is past the free LFS quota. Consequences:

- `BP_Kate` (committed) derives from `SandboxCharacter_CMC` (ignored). **A fresh clone must
  have the sample installed at that path and run `Tools\create-content.ps1` once before
  `BP_Kate` will load.** `verify_gasp.py` checks the copy.
- Plugins enabled for it: PoseSearch, Chooser, AnimationWarping, MotionWarping,
  CurveExpression, DrawDebugLibrary, Mover (the AnimBP uses it), MovieSceneAnimMixer,
  GameplayCameras, SmartObjects, StateTree, GameplayStateTree, GameplayInteractions,
  FullBodyIK. The sample's console variables and gameplay tags are copied into `Config/`.
- Parent chain: `BP_Kate` → `SandboxCharacter_CMC` → `BP_HawkeyeCharacter` →
  `AHawkeyeCharacter`. The sample's own input context is emptied and its graph bindings are
  dropped on possession; C++ bridges `WantsToSprint`, `WantsToWalk`, `WantsToStrafe`,
  `WantsToAim`, `WantsToCrouch`, and `FullMovementInput` into it each tick. Its camera is
  switched off at runtime. Its graph sets movement speeds (run 500, stop 200, aimed strafe
  180), overriding ours on Kate only; thugs keep the native speeds.
- The full `Hawkeye.Screenshot` group has hung once mid-run; run `Hawkeye.Screenshot.Kate`
  and the others separately.

## Combat clips (not in git)

Mixamo FBX downloads go in `Tools/Data/Anims/Mixamo/`; Paragon Sparrow is read from the Game
Animation Sample's `Content/ParagonSparrow/` (Fab added it there). `.\Tools\import-anims.ps1`
imports or copies them, retargets them to Kate's and the thugs' skeletons and builds the `AM_`
montages (44 s for 29 Mixamo and 7 Sparrow clips), then fills the `DA_AnimSet_` assets.
`-SelfTest` runs the pipeline on clips that are always on this machine and asserts on the bones.
The downloads, the copies (`Content/AnimSources/`, `Content/ParagonSparrow/`,
`Content/Characters/UE5_Mannequins/`) and the results (`.../Animations/Combat/`) are git-ignored; a
fresh clone runs the script again. Details and the fallback rule: `claude-docs/animation.md`.

## Vendored plugins
- `Plugins/SPUD` (Steve's Persistent Unreal Data, MIT, commit 12a30da, source checked in,
  not a submodule). Saves the world state; see the Save section of gameplay-semantics.md.
  Builds on 5.8 unchanged. Update by copying a newer checkout over it and re-running the
  save round-trip test.

## City generation from OpenStreetMap

The district is generated, never hand-placed. Scripts under `Tools/`:

```powershell
.\Tools\fetch-osm.ps1          # downloads Overpass JSON into Tools\Data\osm\ (committed; re-run only to refresh)
.\Tools\test-geo.ps1           # 23 pure-python tests for projection and handedness, no engine needed
.\Tools\generate-city.ps1      # runs Tools\Editor\generate_city.py headless; -Verify also runs verify_city.py
```

Facts that matter:
- Bounding box for the East Village district: the grid-aligned rectangle between the
  centre lines of 1st Ave, Ave C, E 6th St and E 11th St (667 x 376 m plus 22 m margin).
  Origin 40.7264398, -73.9816394. Manhattan's grid is about 29 degrees off true north,
  so streets run diagonally in Unreal; that's correct, not a bug.
- Projection: local tangent plane in `Tools/Editor/_geo.py`. Unreal X = east, Y = south,
  Z up. Verified by tests that the avenues order 1st < A < B < C in X and E 6th is south
  of E 11th.
- Heights: from the OSM `height` tag (524 of 526 buildings have it), else levels x 3.2 +
  1.5, else 15 m. Parapets 90 cm high, 30 cm thick, skipped under 6 m: one mitred ring whose outer
  faces are the facades (flush at every corner; `parapet_ring`).
- Meshes: one static mesh per building under `Content/City/EastVillage/Meshes/`, built
  with Geometry Script (`append_simple_extrude_polygon`, normals flipped if the signed
  volume is negative), complex-as-simple collision. Each actor carries a `CityHash`
  metadata tag; a rerun rebuilds only meshes whose footprint or height changed, and saves
  nothing when nothing changed.
- Streets and sidewalks from highway ways: default widths 25 m avenues, 12 m streets, 4 m
  sidewalks; OSM rarely tags widths here. Courtyard holes and park footpaths are not built.
- Verify: `verify_city.py` checks record count vs actors, collision, heights within 1 cm,
  handedness, lights, PlayerStart, nav volume, game mode. Screenshots via
  `Hawkeye.Screenshot.EastVillage` into `Saved/Screenshots/City/`.
- `overpass-api.de` rejects PowerShell's default user agent; the fetch script sends its
  own and falls back to `overpass.kumi.systems`.
- Grapple anchors: `generate_city.anchor_spots` places the parapet anchors and then the facade anchors
  (gameplay-semantics.md, "Anchor placement guarantees") by a reach model, `GrappleReach` in the same
  script; `anchor_reach_report` is what verify_city checks. The script runs in plain Python too (a stub
  `unreal` module is enough), which is how the model was tuned: about 20 s for the district.
- The anchor audit, `Hawkeye.Grapple.Audit` (`Source/Hawkeye/Tests/GrappleAuditTest.cpp`), checks the
  placement with the game's own traces. It needs the district as a game world but no rendering:

  ```powershell
  & "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" /Game/Maps/L_District_EastVillage -game -nullrhi -unattended -nosplash -nop4 -log -abslog="$Proj\..\Saved\Logs\GrappleAudit.log" -ExecCmds="Automation RunTests Hawkeye.Grapple.Audit; Quit"
  ```

  About a minute with the load, 10 s of audit. From sidewalk spots every 10 m (and 2.5 m for a roof the
  10 m ones miss) and every roof's centre and parapet middles it counts, per roof over 8 m, the anchors in
  range, seen by the hip camera turned to them (`HawkeyeGrappleView::PredictLens`: the spring arm's sums,
  the pitch clamp, the probe; then the cone, `IsOnScreen` and `HasLineOfSight`) and zip-clear, and asks
  the picker from each view (dead on and 6.3 degrees off) against a full check of every anchor. It writes
  `Saved/Automation/grapple_audit.json` (the summary, the chapter 1 chain, roofs with none from the street
  and why, neighbour pairs missing, anchors reachable from nowhere) and fails only on the chapter 1 roofs.
  2026-09-29, before the facade anchors and after: 1691 / 2457 anchors; roofs with no anchor reachable
  from a 10 m sidewalk spot 265 / 92, from any 2.5 m spot 236 / 34 (of the 34, 20 have no sidewalk in
  range and 5 are inside their blocks); roof pairs across a gap not reaching each other 991 / 519 of about
  3,500 in range; anchors no spot reaches 31 / 54 (corners on back yards and shared walls; the spots are a sample); the
  reach_roof roof reachable from the street by 0 / 1 anchors, find_arrow 1 / 2.
- World Partition is off for the single block. Stage 3 turns it on.
- The world map's data, `/Game/City/EastVillage/DA_EastVillage_Map` (`UCityMapData`), is written by
  the same run (`ensure_city_map`): every footprint and the park cleaned to a few points (edges
  under 1 m merged, vertices within 50 cm of their neighbours' line dropped), every street piece
  Douglas-Peucker'd to 50 cm with its width, and the ground rectangle's bounding box, all world cm
  in whole centimetres. 526 footprints at 6.8 points each, 1 park, 46 street pieces, 826 x 711 m;
  the asset is 142 KB. It carries a `SourceHash` like the props asset, so a rerun on the same
  records logs "hash unchanged" and saves nothing. `generate-city.ps1` hides `/Game/City/` lines
  from its console summary; the line is in `Saved\Logs\HawkeyeCity.log`. The widget loads it by
  path, so `/Game/City/EastVillage` is in `DirectoriesToAlwaysCook`.

## Packaging

```powershell
& "$UE\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="$Proj" -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -archive -archivedirectory="C:\Users\camer\code\hawkeye\Saved\Packaged" -nop4 -utf8output
```

Takes 15-40 minutes. Output under `Saved\Packaged\Windows\`. Stage 6 only.

## Git

- `git lfs install` has been run. `.gitattributes` tracks `*.uasset *.umap *.png *.jpg
  *.wav *.fbx *.mp3 *.tga *.psd *.exr`. Check a new binary type is listed before adding it.
- `.gitignore` excludes `Binaries/ Intermediate/ Saved/ DerivedDataCache/ .vs/ *.sln
  *.VC.db *.opensdf *.sdf`.
- Commit at the end of every working session. Push to a remote (stage 1 done-when
  includes creating one). A repo that only exists on this SSD is one drive failure from
  gone.
- Line endings: the repo will warn LF vs CRLF on text files. Set `git config core.autocrlf
  true` once and stop worrying.

## Machine constraints

- 16 GB RAM. The editor idles at 4-6 GB, climbs to 10+ during shader compiles and
  lighting builds. Visual Studio takes 1-2 GB. Do not run the editor and a full VS build
  at once. Close browsers before builds.
- Only the RTX 4070 (12 GB VRAM) should render. If the editor is slow, check it isn't on
  the Intel iGPU: Windows Settings, Display, Graphics, add UnrealEditor.exe, set High
  performance.
- Derived data cache lives at `%LOCALAPPDATA%\UnrealEngine\Common\DerivedDataCache`. It
  grows to tens of GB. Fine on this disk. Don't delete it casually; it's the shader cache.
- Reboot pending from the Visual Studio install as of 2026-09-17. Until then `dotnet`
  is not on PATH in new shells. The engine's own .NET handles builds, so this only
  affects running `dotnet` directly.

## Environment checks (run when something is weird)

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Workload.NativeGame -property installationPath
Get-ChildItem "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
git lfs ls-files | Measure-Object -Line
Get-Content "C:\Users\camer\code\hawkeye\Saved\Logs\Hawkeye.log" -Tail 50 | Select-String -Pattern "Error|Fatal|Warning: .*Hawkeye"
```

## Load time (measured 2026-09-27)
- `Tools\measure-load.ps1` reads the game mode's "Playable after X s" line from a `-game`
  run. Warm load about 21 to 23 s, cold about 25 s. Roughly 12 s of that is uncooked
  editor data behind the animation sample's 1,300 animations; only a cooked build removes
  it. Mid-session reloads (death, playable scene and back) are 1 to 2 s because shared
  characters and databases stay loaded across level changes.
- Keep the Zen cache server running between launches (saves about 3 s). Python is off in
  `-game`. Motion-matching database indices build during load rather than on the first
  tick, so the first playable frame is under 80 ms.
- Ledges within 40 m spawn at load; the rest stream in nearest-first. Effects and sounds
  stream in after load. An async navmesh build was tried and reverted (3.4 s and thug
  warnings); the synchronous build is 0.5 s.
- Screenshot captures in tests run one at a time, write off the game thread, and fail the
  test if the file is missing, empty, or older than the request.
  The queue is `World/HawkeyeCapture` (2026-09-29); `Tests/HawkeyeShots` wraps it for tests and the playtest
  kit's notes and photos (`Saved/Playtest/<session>/`, see testing.md) go through the same one.

## Packaged builds (working since 2026-09-27)
- `Tools\package.ps1`: Development by default, `-Shipping`, `-NoZip`, `-NoIoStore`. Cooks
  both maps plus the folders the code loads by path (listed in `DefaultGame.ini`), pak and
  IoStore on, archives to `Saved\Packaged\Windows`, zips to
  `Saved\Packaged\Hawkeye-win64-<date>.zip`. A warm run takes 1 to 3 minutes. Log at
  `Saved\Logs\Package.log`. Nothing under `Saved/` is committed.
- Load time in the package: about 5.5 s on a fresh exe, 2.8 to 3.0 s after, versus 21 s
  in the editor's `-game` mode (uncooked animation data). Sizes: Development 1.21 GB
  (zip 0.64 GB; the exe and symbols are most of it), Shipping 0.87 GB (zip 0.54 GB).
- Automation runs in the packaged Development build (`-ExecCmds="Automation RunTests
  Hawkeye.Smoke.LoadEastVillage; Quit"`); smoke tests carry `ClientContext`.
- Runtime code must not include editor-only headers inside `#if WITH_EDITOR` blocks that
  are needed outside them; the VFX builder and a screenshot test broke the standalone
  target once. `GetActorLabel` is editor-only; use `GetActorNameOrLabel`.
- Open: the packaged log shows about 140 load errors from `CR_UEFN_Mannequin_FullBodyIK`
  (editor-only Control Rig classes referenced by the sample's Mover animation Blueprint,
  which Kate doesn't use). Harmless so far. The NNEDenoiser plugin adds 98 MB and could be
  disabled.
- Desktop shortcut: "Play Hawkeye (Packaged)".

## Performance, 2026-09-29
- `Tools\measure-perf.ps1` runs scripted tests in the standalone game (`-game`, editor binaries, uncooked)
  with `-HawkeyePerfLog=2` (the game mode logs a `Perf [<test>/<phase>]` line every 2 s: frames, average,
  p95 and worst frame, the stat unit split of game thread, render thread ("draw") and GPU, frames over
  50 ms, working set and peak) and `-HawkeyeHitchMs=50` (every frame of 50 ms or more after a playable mark
  is logged with its thread split). It samples its own process's working set and private bytes once a
  second (`Saved\Perf\<tag>.memory.csv`), never another editor's, and prints one row per phase.
  `-StatDump 12` adds `stat dumpave` every 12 s and `stat dumphitches` (it has to be switched on by the game
  mode: `-ExecCmds` only starts an automation run when `Automation` leads the line). `-Extra` passes
  switches such as `-dpcvars=` for a diagnostic run. Screenshot capture frames are counted apart.
- `Hawkeye.Perf.Tour` covers what the laps don't: 8 s standing on the block, 8 s with the map open, 8 s
  with the inventory open, and a death reload with no save (`RestartMission(0)`, the level reopened).
  Fast travel is `Hawkeye.Lap.FastTravel`, the interior enter and exit `Hawkeye.Lap.InteriorWalk`.
- `Tools\run-tests.ps1 -LogFile <path> -Report <dir>` keeps a run's log and report apart from another
  agent's.

Measured on this machine (RTX 4070, i7-13700K, 16 GB), before and after the fixes below. Frame numbers
are ms, average / p95 (game, draw, GPU averages for the fight). "Hitches" counts frames of 50 ms or more
after a playable mark, captures left out. Laps, fight and duel came out the same both times: lap 36.6 s
completed; street fight (bat, gunner, heavy; music on; the fight camera did not engage, no two alerted
thugs came within its 6 m) won in 15.6 s, 1 hit taken, 6 arrows; archer duel won in 8.9 s, 0 hits, 4 arrows.

| Run | Lap | Street fight (game/draw/GPU) | Duel | Idle | Map open | Inventory open | Worst after a level change | Peak working set | Hitches |
|-----|-----|------------------------------|------|------|----------|----------------|----------------------------|------------------|---------|
| Night 1280x720, before | 7.0 / 8.7 | 6.7 / 11.3 (6.1, 5.4, 4.3) | 6.0 / 9.3 | 6.0 / 6.7 | 5.6 / 6.4 | 5.5 / 6.4 | 662 (reload), 628 (interior) | 8.54 GB (private 10.0) | 12 |
| Night 1280x720, after | 5.9 / 7.0 | 6.3 / 11.5 (5.7, 4.9, 4.0) | 5.3 / 7.3 | 5.4 / 6.1 | 5.0 / 5.8 | 4.9 / 5.7 | 71 (reload), 55 (interior) | 8.13 GB (private 9.3) | 6 |
| Day 1280x720, before | 7.0 / 8.7 | 6.7 / 10.8 (6.0, 5.4, 4.2) | 6.0 / 8.5 | 6.0 / 6.7 | 5.6 / 6.5 | 5.5 / 6.3 | 653, 622 | 8.47 GB | 7 |
| Day 1280x720, after | 5.9 / 7.8 | 6.4 / 10.8 (5.8, 4.9, 4.0) | 5.4 / 7.8 | 5.4 / 6.1 | 5.0 / 5.7 | 4.9 / 5.7 | 67, 60 | 8.19 GB | 7 |
| Night 1920x1080, before | 7.3 / 8.5 | 7.0 / 10.7 (5.8, 5.7, 4.8) | 6.4 / 9.4 | 6.4 / 6.8 | 6.0 / 6.6 | 5.9 / 6.5 | 642, 621 | 8.63 GB | 8 |
| Night 1920x1080, after | 6.3 / 7.1 | 6.6 / 10.9 (5.5, 5.2, 4.5) | 5.8 / 7.2 | 5.8 / 6.2 | 5.4 / 5.7 | 5.3 / 5.6 | 74, 62 | 8.25 GB | 4 |

- Load (`measure-load.ps1`, 3 runs, reload at 10 s): playable after 22.5 to 23.0 s warm before, 21.9 s
  after, so the 21 s editor-build figure above still holds (the packaged 5 s figure was not re-measured); LoadMap 13.4
  to 14.4 s either way; the scripted death reload 1.6 s either way. Level changes late in a session got
  quicker once the block has streamed in: interior in 1.02 to 0.79 s, out 1.17 to 1.10 s, reload 2.13 to
  1.77 s. The first run after another process has used the machine is slower (27 to 47 s); don't read it.
- Hitches left: the first one or two frames after every playable mark (55 to 110 ms; the frame the load
  finished in, then streaming completions), one frame at 10.2 s that is the automation run starting (game,
  draw and GPU all small), one at 65 s in the lap that is the lap test's own 34,000-ray roof survey, and
  0.5 s screenshot captures. None is the game's steady state.
- Where the time goes (`stat dumpave`, street fight): game thread about 4 ms of world tick, spread thin
  (tick groups 0.6 to 1.2 ms each, animation, movement); every Hawkeye tickable together (music, crime,
  challenges, time of day) is 0.25 ms. The overhead widget, grapple targeting (grid, 0.1 s), fight camera
  (0.25 s recount), and music (pushes a gain only when it changes) are below the dump's 0.1 ms cut. Render
  thread about 5 ms (UpdatePrimitive 1.9 incl. 1.45 waiting on the occlusion fence, InitViews 1.5). GPU 4
  to 5 ms at both resolutions, night and day alike, so the 285 lamp lights are not worth culling.
- The one real cost was the ledges: each streamed-in `BP_TraversableBlock` (Game Animation Sample) carries
  visible spline components, and a spline in a game world is still a scene primitive. With the block
  streamed in that was thousands of primitives the visibility pass walked every frame, 13 ms of AddPrimitive
  in some streaming frames, and 0.6 s of render thread removing them on every level change (the old world's
  purge, three frames after the playable mark). `ACityLedgeSpawner::MakeTraceOnly` now hides them in game
  worlds (the editor still draws them); the traversal's queries read the geometry, not the render state.
- Memory: the peak working set never passed 9 GB (8.5 before, 8.1 to 8.3 after); private bytes peaked at
  10.0 GB before and 9.3 after. Most of it is uncooked editor data behind the animation sample; a cooked
  build is the way to cut it further. A run where the working set drops to 3 to 4 GB with private bytes
  still at 9 GB was trimmed by Windows under another process's memory pressure, and its frame times (draw
  50 ms) are not the game's.
- `UTimeOfDaySubsystem::SetGlow` drops entries whose material has gone when a new one registers, not
  only at the next Apply.
