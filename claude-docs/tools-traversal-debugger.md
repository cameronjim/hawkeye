# Traversal debugger

An editor tool and a headless gate for one question: can Kate get onto every roof she should, and if not, why not.
It draws the district's grapple anchors, roof-edge ledges and fire-escape landings in the level viewport, checks
every anchor from a point you pick and colours each by why it can or cannot be grappled, and runs the grapple audit
(the same spots and rules as `Hawkeye.Grapple.Audit`) without a character or a game world. The commandlet runs that
audit in the pipeline and fails when a street-facing roof is unreachable.

Status (2026-11-10): written against the UE 5.8 headers, not yet compiled. `docs/plans/editor-module-build-notes.md`
lists every API that was checked and the order to build and verify it in.

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
  `Saved/Automation/grapple_audit.json`. The roof list fills with every roof the street cannot reach, with the
  reasons (`view blocked by its own building x12, zip blocked by FireEscapeLanding x3`); Focus frames the roof.
- **Objective chain**: objective ids (comma-separated) whose roofs must chain from the street, e.g.
  `reach_roof, cross_block, find_arrow` for chapter 1. Their roofs are found from the `objective:<id>` and `osm:<id>`
  tags the generator puts on the objective volumes.

The lines go into the editor world's persistent line batcher under the tool's own batch id, so Clear drawing removes
only the tool's lines. A map change drops the scene; press Refresh.

## The commandlet

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" -run=HawkeyeTraversalAudit -map=/Game/Maps/L_District_EastVillage `
    -chain=reach_roof,cross_block,find_arrow -allowfile="$ProjDir\Tools\traversal_audit_allow.txt" -unattended -nullrhi -nosplash -nop4 -stdout
```

| Switch | Meaning |
|--------|---------|
| `-map=` | Required. The map to load in the editor world. |
| `-out=` | Where to write the JSON. Default `Saved/Automation/grapple_audit.json`. |
| `-chain=` | Objective ids whose roofs must chain, in order. Not checked when absent. |
| `-allowfile=` | Known exceptions: one osm id per line, first word only, `#` comments. Listed roofs do not fail the gate. |
| `-pawn=` | A pawn class path to read the rules from (default: the map's game mode's default pawn, else `AHawkeyeCharacter`). |
| `-nospawn` | Do not spawn the city props for the audit (faster; fire escapes and clutter then never block). |

Exit codes: 0 passed, 1 a street-facing roof is unreachable (and not allowed) or the chain is broken, 2 it could not
run (no map, no spawner, no city data, cannot write the JSON). `UseCommandletResultAsExitCode` is on, so warnings in
the map load do not change it. The allow file does not exist yet: the first run's `street_facing_unreached` list is
the baseline to review with Cameron (the 2026-09-29 audit had 34 roofs no 2.5 m spot reaches, most with no sidewalk in
range, which this gate does not count).

"Street-facing" means: anchored (over 8 m), has anchors, and some sidewalk spot (10 m or 2.5 m sampling) is within the
grapple's range of one of them. A roof too tall for 25 m from the pavement, or inside its block, is `NoStreetInRange`:
listed, never failed. That is the same line `verify_city.py` draws for the reach model.

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
  GrappleReachWorld      FGrappleReachWorld: the world side. A profile (rules, lens, capsule collision) from a pawn's
                         class default; a scene (roofs, anchors, ledges, fire escapes, streets, objective roofs) from
                         the city data; an oracle whose traces are UGrappleComponent's (same channels, supports and
                         allowances) with no character.
  GrappleAudit           FGrappleAudit::Run: Hawkeye.Grapple.Audit's passes over a scene and an oracle; the report,
                         its JSON, and the gate.

Source/HawkeyeEditor (editor only)
  HawkeyeEditorModule    the tab spawner and the Tools menu entry
  TraversalDebuggerSubsystem   UEditorSubsystem: the scene, the reach check, the audit, the drawing
  STraversalDebuggerPanel      the tab's Slate UI; all state in the subsystem
  TraversalAuditRunner   editor-world glue shared by the tab and the commandlet: find the data, spawn the props for
                         the audit, run it, tidy up
  HawkeyeTraversalAuditCommandlet   -run=HawkeyeTraversalAudit
```

`UGrappleComponent` takes its UPROPERTY defaults from `GrappleReachDefaults` and calls `FGrappleReachQuery` for the angle
off the view, `IsOnScreen`, `ChoosePick` and the zip's release point, so the picker in the game and the tools share
that math. `MakeReachRules()` hands its live values (and the capsule) to the tools; they read them off the pawn's class
default, so BP_Kate's overrides are what is checked. The component's own traces (`HasLineOfSight`, `IsZipClear`,
the supports) are still its own; `FGrappleReachWorld::MakeOracle` mirrors them for an anchor with no actor. Folding the
component onto the oracle is the next step once the commandlet's JSON matches the test's (build notes, step 6).

The module loads in the Default phase, not PostEngineInit: the engine looks a `-run=` commandlet's class up in
`FEngineLoop::PreInitPostStartupScreen` before it loads PostEngineInit modules, so a PostEngineInit module's commandlet
is never found. The menu waits for `UToolMenus::RegisterStartupCallback`, which is what PostEngineInit was for.

## How it relates to `_reach.py` and `Hawkeye.Grapple.Audit`

| | `Tools/Editor/_reach.py` (`GrappleReach`) | `Hawkeye.Grapple.Audit` | This tool and commandlet |
|---|---|---|---|
| Runs in | plain Python, the generator | the standalone game (`-game`) | the editor world, or the commandlet |
| Geometry | buildings as prisms, lamps as columns, fire escapes as boxes | the real collision | the real collision (props spawned for it) |
| Camera | the hip camera's sums, a probe against the prisms | `HawkeyeGrappleView::PredictLens` on live Kate | `FGrappleReachQuery::PredictLens` on BP_Kate's class default, the probe on the boom's channel |
| Zip | feet over the prisms | `UGrappleComponent::IsZipClear` | the same sweeps through `FGrappleReachWorld::MakeOracle` |
| Picker | no | yes, against a full check | no (needs the component in a game world) |
| Output | anchor placement, verify_city's guarantees | `grapple_audit.json`, fails on chapter 1 | `grapple_audit.json` (same keys plus `street_facing_unreached`), fails on street-facing roofs |

The Python model places the anchors and is kinder about views past fire escapes; the audits use the game's traces.
The commandlet differs from the test in three known ways: anchors come from the data records rather than spawned
actors (each is put on the roof under its landing point, as the test does, falling back to the record's osm id);
`picker_misses` and `anchors_never_picked` are always empty; and the chain's "down a fire escape and up from the
street" fallback applies to any link, not only find_arrow. Its summary line says "picker not checked (offline audit)".

## Tests

`Source/Hawkeye/Tests/GrappleReachQueryTest.cpp`, `Hawkeye.Grapple.Reach.*`, no map needed:
`SharesTheComponentsRules`, `PickNearestMiddleThenNearest`, `OnScreenInsideTheMargin`, `StartReleasePoint`,
`VerdictOrder`, `PredictLens`, `RoofSpots`, `NeighboursAndChains`, `SidewalkSpots`, `RoofsTheStreetCannotReach`,
`AuditReportAndGate`. The existing `Hawkeye.Grapple.PickRule.*` and `Hawkeye.Grapple.Audit` cover the component's
side of the shared math.

## Extending it

- A new layer: a value in `ETraversalDebugLayer`, a `Draw*` in the subsystem, a toggle in the panel.
- A new verdict: add it to `EGrappleReachVerdict` in check order, `Evaluate`, `LexToString`, the colours, a test.
- Another district: nothing to change. The scene is whatever the map's spawner and the map data hold.
