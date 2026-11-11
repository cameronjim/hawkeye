# Traversal debugger

An editor tool and a headless gate for one question: can Kate get onto every roof she should, and if not, why not.
It draws the district's grapple anchors, roof-edge ledges and fire-escape landings in the level viewport, checks
every anchor from a point you pick and colours each by why it can or cannot be grappled, and runs the grapple audit
(the same code `Hawkeye.Grapple.Audit` runs) without a character or a game world. The commandlet runs that audit in
the pipeline and fails when a street-facing roof is unreachable.

Status (2026-11-10): built, and the commandlet and the audit verified headless (numbers below). **The tab has never
been seen on screen.** What is verified about it: the module loads, the tab spawner and the Tools menu entry are
registered, the panel's widget tree builds under `-nullrhi`, its rows are the view model's, and on the district the
subsystem's Refresh and Reach from a point run end to end (`Hawkeye.Editor.TraversalDebugger.*`). Not verified: that
the lines draw in the viewport, the layer toggles, the buttons, Focus, and the layout. Known gaps are at the end.

## Opening it

Tools > Hawkeye > Traversal Debugger in the level editor (a nomad tab; dock it anywhere). Open the district first:
everything comes from the map's `ACityLedgeSpawner` data and its `CityBuilding` actors.

- **Layers**: grapple anchors (blue box on the marker, a line to the landing point), ledges (yellow slabs), fire-escape
  landings (orange boxes, the collision `AFireEscapeLanding` would spawn), unreached roofs (after an audit: red
  outline and pillar for a street-facing roof nothing reaches, purple for a roof no sidewalk is in range of), reach
  lines. Nothing has to be spawned to draw them; they are drawn from `DA_EastVillage_CityProps`.
- **Reach from camera / Reach from selection**: puts a capsule on the floor under the viewport camera or the selected
  actor and checks every anchor in range: green reachable, yellow too steep (outside the cone or the screen even with
  the camera turned to it, the pitch clamp), orange view blocked, red zip line blocked. The list below names each
  anchor, its roof, distance, angle, verdict and what blocked it; Focus moves the viewport to it.
- **Run audit / Run audit, write JSON**: the full audit on the editor world. The props are spawned for it (fire
  escapes and clutter collide) and removed after, unless they were already out. The second button writes
  `Saved/Automation/grapple_audit.json`. The roof list fills with every roof the street cannot reach, street-facing
  ones first, with the reasons (`view blocked by its own building x12, zip blocked by FireEscapeLanding x3`); Focus
  frames the roof.
- **Objective chain**: objective ids (comma-separated) whose roofs must chain from the street, e.g.
  `reach_roof, cross_block, find_arrow` for chapter 1. Their roofs are found from the `objective:<id>` and `osm:<id>`
  tags the generator puts on the objective volumes. The tab has no street-link box, so from the tab a link that only
  works down a fire escape (find_arrow's) shows as broken; the commandlet takes `-streetlinks=`.

The lines go into the editor world's persistent line batcher under the tool's own batch id, so Clear drawing removes
only the tool's lines. A map change drops the scene; press Refresh.

## The commandlet

```powershell
.\Tools\traversal-audit.ps1            # the district, chapter 1's chain, find_arrow as a street link
.\Tools\traversal-audit.ps1 -AllowFile Tools\traversal_audit_allow.txt -Out Saved\Automation\audit.json
```

The wrapper finds the engine through `Tools\hawkeye.env.ps1`, runs the commandlet headless, prints the summary, the
chain and every failing roof, and exits with the commandlet's code. Raw form:

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" -run=HawkeyeTraversalAudit -map=/Game/Maps/L_District_EastVillage `
    -chain=reach_roof,cross_block,find_arrow -streetlinks=find_arrow -unattended -nullrhi -nosplash -nop4 -stdout
```

| Switch | Meaning |
|--------|---------|
| `-map=` | Required. The map to load in the editor world. |
| `-out=` | Where to write the JSON. Default `Saved/Automation/grapple_audit.json` (the wrapper: `traversal_audit.json`). |
| `-chain=` | Objective ids whose roofs must chain, in order. Not checked when absent. |
| `-streetlinks=` | Chain objectives that may be reached down a fire escape and up from the street instead (find_arrow). |
| `-allowfile=` | Known exceptions: one osm id per line, first word only, `#` comments. Listed roofs do not fail the gate. |
| `-pawn=` | A pawn class path to read the rules from (default: the map's game mode's default pawn, else `AHawkeyeCharacter`). |
| `-nospawn` | Do not spawn the city props for the audit (faster; fire escapes and clutter then never block). |

Exit codes: 0 passed, 1 a street-facing roof is unreachable (and not allowed) or the chain is broken, 2 it could not
run (no map, no spawner, no city data, cannot write the JSON, or the audit found no sidewalk or roof spots, which means
its traces hit nothing). `UseCommandletResultAsExitCode` is on, so warnings in the map load do not change it. About
35 to 50 s on this machine, 3 s of it the audit.

There is no allow file yet, so on the district the gate fails: 14 street-facing roofs have no anchor any sidewalk spot
reaches (listed with their reasons in the JSON's `street_facing_unreached`). That list is the baseline to review with
Cameron before `Tools/traversal_audit_allow.txt` is written; the wrapper picks that file up when it exists.

"Street-facing" means: anchored (over 8 m), has anchors, and some sidewalk spot (10 m or 2.5 m sampling) is within the
grapple's range of one of them. A roof too tall for 25 m from the pavement, or inside its block, is `NoStreetInRange`:
listed, never failed. That is the same line `verify_city.py` draws for the reach model. The 34 roofs no 2.5 m spot
reaches are these 14 plus 20 with no sidewalk in range.

### Parity with `Hawkeye.Grapple.Audit` (2026-11-10)

Same district, same build. The test runs in the standalone game with Kate's own traces and picker; the commandlet in
the editor world with `FGrappleReachWorld`'s oracle.

| | Test (`-game`) | Commandlet |
|---|---|---|
| Anchors, roofs over 8 m, spots (street, roof) | 2457, 507, 4438 (1111, 3327) | the same |
| Spot-anchor pairs in range / seen / zip-clear / reachable | 76035 / 35574 / 38955 / 24420 | the same |
| Roofs with no anchor from a 10 m spot / with a spot in range / none at 2.5 m / none from another roof | 92 / 70 / 34 / 7 | the same |
| Neighbour pairs, not reachable, out of range | 3536, 519, 690 | the same |
| Anchors reachable from nowhere | 54 | the same |
| Chapter 1 chain | reach_roof 1 from the street; cross_block 7 hops; find_arrow 37 hops | the same |
| Picked dead on / picker disagreements | 24371 of 24420 (49 by the tie rule) / 0 of 24420 dead on, 0 of 97680 aimed off | not checked |

Every roof row and every anchor row (`in_range`, `seen`, `clear`, `reach`, roof) is identical; the only differences
in the JSON are the anchor names (`BP_GrappleAnchor_C_266` in the game, `City_Anchor_266` from the data, same
number) and the picker fields. Two things had to be right for that:

- **Collision in an unticked world.** A commandlet's world never ticks, so in 5.8 nothing finishes the editor's async
  static mesh compiles (a compiling mesh gets no physics state), the bodies deferred while the level loaded, or the
  async physics-state jobs, and every trace misses. `FGrappleReachWorld::FlushPendingPhysicsState` finishes all three
  (on the district: 1993 compilations); the scene builders call it.
- **Chapter actors.** Anything tagged `objective:<id>` (the find_arrow arrow and its prop, beacons) collides in the
  game only while its chapter runs (`AHawkeyeGameMode::ApplyChapterCast`). In the editor world they always collide,
  which cost 3 seen and 6 clear pairs on five roofs round find_arrow and one hop of the chain. The data scene lists
  them in `FGrappleReachScene::SeeThrough` and every trace ignores them.

## Architecture

Three layers, each only knowing the one below it.

```
Source/Hawkeye/Traversal (runtime, used by the game, the tests and the editor)
  GrappleReachTypes.h    GrappleReachDefaults (the grapple's numbers, one place), FGrappleReachRules, FGrappleLensModel,
                         anchors, roofs, spots, streets, EGrappleReachVerdict, EGrappleRoofReach, FGrappleReachOracle
  GrappleReachQuery      FGrappleReachQuery: pure functions. Range, cone, IsOnScreen, ChoosePick, the predicted lens,
                         the zip's launch and release point, Evaluate (one anchor, one spot, a verdict), EvaluateAll,
                         roof spots, neighbours across a gap, chains of roofs, sidewalk sampling, roofs the street
                         cannot reach. The world comes in through FGrappleReachOracle (sight, zip sweep, lens probe)
                         and callbacks (the floor under a point): no UObject.
  GrappleReachWorld      FGrappleReachWorld: the world side. A profile (rules, lens, capsule collision) from a pawn; a
                         scene from the city data (BuildScene) or from the actors a game world spawned
                         (BuildSceneFromActors); an oracle whose traces are UGrappleComponent's (same channels, supports
                         and allowances) with no character; FlushPendingPhysicsState.
  GrappleAudit           FGrappleAudit::Run: the audit's passes over a scene and an oracle, with an optional picker hook;
                         the report, its JSON, and the gate.

Source/HawkeyeEditor (editor only)
  HawkeyeEditorModule    the tab spawner and the Tools menu entry
  TraversalDebuggerSubsystem   UEditorSubsystem: the scene, the reach check, the audit, the drawing
  TraversalDebuggerViewModel   what the tab lists, in what order and colour, its status and summary: no Slate
  STraversalDebuggerPanel      the tab's Slate layout of the view model's rows; all state in the subsystem
  TraversalAuditRunner   editor-world glue shared by the tab and the commandlet: find the data, spawn the props for
                         the audit, run it, refuse an audit that found no spots, tidy up
  HawkeyeTraversalAuditCommandlet   -run=HawkeyeTraversalAudit
  Tests/                 Hawkeye.Editor.TraversalDebugger.*
```

`Hawkeye.Grapple.Audit` (`Source/Hawkeye/Tests/GrappleAuditTest.cpp`) is `FGrappleAudit` with the game world plugged
in: the scene from `BuildSceneFromActors`, the oracle Kate's `UGrappleComponent::HasLineOfSight` and `IsZipClear` on the
spawned anchors, the lens `FGrappleReachWorld::MakeProfile` of the live Kate with her spring arm's probe (her capsule
left out), and the picker her `SelectBestAnchorFrom`, checked against the full check dead on and 6.3 degrees off.
Its assertions are unchanged (a chapter 1 roof the street or the previous objective cannot reach; find_arrow may be
reached down a fire escape) and so are its numbers.

`UGrappleComponent` takes its UPROPERTY defaults from `GrappleReachDefaults` and calls `FGrappleReachQuery` for the angle
off the view, `IsOnScreen`, `ChoosePick` and the zip's release point; that math is the component's old code moved
verbatim, float arithmetic included, so the game's picks did not change. `MakeReachRules()` hands its live values
(and the capsule) to the tools; they read them off the pawn's class default, so BP_Kate's overrides are what is
checked (the tab's Refresh reports `rules from BP_Kate_C`). The component's own traces (`HasLineOfSight`,
`IsZipClear`, the supports) are still its own; `FGrappleReachWorld::MakeOracle` mirrors them for an anchor with no
actor, and the parity above is the check that the two agree.

The module loads in the Default phase, not PostEngineInit: the engine looks a `-run=` commandlet's class up in
`FEngineLoop::PreInitPostStartupScreen` before it loads PostEngineInit modules, so a PostEngineInit module's commandlet
is never found. The menu waits for `UToolMenus::RegisterStartupCallback`. `HawkeyeEditor` is typed `Editor` in
`Hawkeye.uproject` and listed only in `HawkeyeEditor.Target.cs`; the game target (`Hawkeye Win64 Development`) builds
without it, and nothing under `Source/Hawkeye` includes its headers.

## How it relates to `_reach.py`

| | `Tools/Editor/_reach.py` (`GrappleReach`) | `Hawkeye.Grapple.Audit` | This tool and commandlet |
|---|---|---|---|
| Runs in | plain Python, the generator | the standalone game (`-game`) | the editor world, or the commandlet |
| Audit code | its own | `FGrappleAudit` | `FGrappleAudit` |
| Geometry | buildings as prisms, lamps as columns, fire escapes as boxes | the real collision | the real collision (props spawned for it, chapter actors seen through) |
| Camera | the hip camera's sums, a probe against the prisms | `FGrappleReachQuery::PredictLens` on live Kate | the same on BP_Kate's class default |
| Zip | feet over the prisms | `UGrappleComponent::IsZipClear` | the same sweeps through `FGrappleReachWorld::MakeOracle` |
| Picker | no | yes, against a full check | no (needs the component in a game world) |
| Output | anchor placement, verify_city's guarantees | `grapple_audit.json`, fails on chapter 1 | the same keys, fails on street-facing roofs |

The JSON (both audits) has the keys the test always wrote plus `street_facing_unreached` and `anchors` (one row per
anchor: `in_range`, `seen`, `clear`, `reach`, `picked`), which is what to diff when two audits disagree.

## Tests

- `Source/Hawkeye/Tests/GrappleReachQueryTest.cpp`, `Hawkeye.Grapple.Reach.*`, no map needed:
  `SharesTheComponentsRules`, `PickNearestMiddleThenNearest`, `OnScreenInsideTheMargin`, `StartReleasePoint`,
  `VerdictOrder`, `PredictLens`, `RoofSpots`, `NeighboursAndChains`, `SidewalkSpots`, `RoofsTheStreetCannotReach`,
  `AuditReportAndGate`, `AuditPickerAndStreetLinks`.
- `Source/HawkeyeEditor/Tests/TraversalDebuggerTest.cpp`, `Hawkeye.Editor.TraversalDebugger.*`, headless editor:
  `RegistersTabAndMenu` (module loaded, subsystem up, `FGlobalTabmanager` has the spawner, the Tools menu has the
  Hawkeye section and entry), `ViewModelRows`, `PanelBuildsHeadless` (the Slate panel built with no window, its rows the
  view model's), `ReachOnTheDistrict` (Refresh and Reach from a street point on the editor world; skipped when the
  editor world is not the district).
- `Hawkeye.Grapple.Audit` (standalone game) and the existing `Hawkeye.Grapple.*` tests cover the component's side.

## Known gaps

- The tab has not been opened by anyone. Check on first use: the lines appear without moving the camera (if not, the
  viewport needs Realtime, or `Redraw` should call `GEditor->RedrawLevelEditingViewports()`), each toggle and button,
  Focus, and that the lists are readable docked narrow.
- The allow-file baseline (`Tools/traversal_audit_allow.txt`) is not written: the 14 roofs are a design call.
- The tab cannot set street links, so chapter 1's chain reads as broken there at find_arrow.
- `UGrappleComponent::HasLineOfSight` and `IsZipClear` are not yet routed through the shared trace code; the parity
  check is what keeps the oracle honest until they are.

## Extending it

- A new layer: a value in `ETraversalDebugLayer`, a `Draw*` in the subsystem, a toggle in the panel.
- A new verdict: add it to `EGrappleReachVerdict` in check order, `Evaluate`, `LexToString`, the view model's colours,
  a test.
- Another district: nothing to change. The scene is whatever the map's spawner and the map data hold.
