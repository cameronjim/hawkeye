# Testing

Three layers. Most effort goes into the first.

## 1. Automation tests (C++)

Where: `Source/Hawkeye/Tests/*Test.cpp`. Compiled only in editor and development builds
(`#if WITH_DEV_AUTOMATION_TESTS`). Named `Hawkeye.<System>.<Behaviour>`.

What gets tested: every rule in `gameplay-semantics.md`. Concretely:

| System | Tests |
|--------|-------|
| Health | clamp, invulnerable ignores damage, OnDeath fires once, heal after death is a no-op, Revive works, negative damage ignored |
| BossPhase | thresholds advance in order, overkill skips to correct phase with one broadcast, transition invulnerability set and restored, N phases produce N-1 transitions |
| Weapon | fire decrements magazine, fire at 0 ammo does nothing, fire rate blocks rapid fire, reload math with partial reserve, reload with full mag is no-op, OnAmmoChanged counts |
| Mission | completion when all non-optional done, optional never completes mission, duplicate CompleteObjective fires nothing, unknown id logs warning, OnMissionComplete once, OnFlashbackRequested fires only with a definition, current objective picks first incomplete non-optional |
| Flashback | null/empty definition finishes immediately, total duration equals sum of holds and fades, skip finishes whole thing, skip ignored in first 0.5 s, pause state restored |
| Takedown | angle check (behind passes, front fails, edge at MaxAngle), range check, alerted guard rejected, tag required |
| Save (stage 3) | round-trip every field, stale mission id starts new game, version mismatch handled |

Pattern for a component test (no world needed):

```cpp
#include "Misc/AutomationTest.h"
#include "Combat/HealthComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHealthDeathFiresOnce,
	"Hawkeye.Health.DeathFiresOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHealthDeathFiresOnce::RunTest(const FString& Parameters)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(100.f, /*bResetCurrent*/ true);

	int32 DeathCount = 0;
	Health->OnDeath.AddLambda([&DeathCount](AActor*) { ++DeathCount; });

	Health->ApplyDamage(150.f, nullptr);
	Health->ApplyDamage(10.f, nullptr);

	TestEqual(TEXT("Health clamped to zero"), Health->GetCurrentHealth(), 0.f);
	TestEqual(TEXT("OnDeath fired exactly once"), DeathCount, 1);
	return true;
}

#endif
```

Components that need an owner (Takedown, Weapon trace) get a test world:

```cpp
UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
Ctx.SetCurrentWorld(World);
// spawn actors, run the test
GEngine->DestroyWorldContext(World);
World->DestroyWorld(false);
```

Put that in a small RAII helper (`FHawkeyeTestWorld`) in `Tests/HawkeyeTestUtils.h` so
every test doesn't repeat it.

Dynamic multicast delegates can't take lambdas. Tests bind to a `UHawkeyeTestListener`
UObject with `UFUNCTION()` counters (`OnHealthChangedCount`, `LastNewHealth`, ...), also
in `HawkeyeTestUtils.h`.

Run all:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\camer\code\hawkeye\Hawkeye.uproject" -ExecCmds="Automation RunTests Hawkeye; Quit" -unattended -nullrhi -nosplash -nop4 -stdout -FullStdOutLogOutput -ReportExportPath="C:\Users\camer\code\hawkeye\Saved\Automation"
```

Run one group: replace `Hawkeye` with `Hawkeye.Health`. The report is JSON under
`Saved\Automation\index.json`; the console log shows `Test Completed. Result={Passed|Failed}`
per test. Exit code is 0 even on failure, so grep the log for `Result={Failed}` or read
the JSON. The helper `Tools\run-tests.ps1` does this and exits non-zero on failure.

Rules:
- A test per bullet in `gameplay-semantics.md`. When a rule changes, the test changes in
  the same commit.
- Tests never load Content assets. Build definitions in code with `NewObject`. Content
  can move or be renamed; tests shouldn't care.
- Tests must pass with `-nullrhi`. No rendering, no input, no audio playback assertions
  (assert that the call was made, not that sound came out).
- Under 100 ms each. The whole suite under 30 seconds including editor startup overhead.

## 2. Functional tests (in a map)

Where: `Content/Maps/Tests/L_Test_*.umap` with `AFunctionalTest` actors (Functional
Testing plugin). Run by the same automation command under the `Project.Functional Tests`
group.

Use for: things that need a real world tick and navigation. A guard patrols and reaches
its waypoints. A guard hears a sprinting player and goes Suspicious. A trigger volume
completes an objective when the pawn overlaps. A door opens with a keycard and not
without. A boss with a real behavior tree changes branches on phase change.

Keep to under 10 of these. They are slow (each loads a map) and brittle when levels
change. One per system that has world-dependent behaviour, not one per feature.

## 2b. Standalone game check (required for any visual change)

The screenshot tests run inside the editor process, where assets are already loaded and
material usage flags compile on demand. The standalone game (`-game`, what Play Hawkeye
launches) is not that process. On 2026-09-19 the editor renders looked right while the
real game showed default-material arms and guards, an invisible pistol, and unlit rooms.

So every pass that touches materials, meshes, lighting, or the viewmodel must also:
1. Run the screenshot tests from a `-game` process:
   `UnrealEditor-Cmd.exe <proj> -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot; Quit"`
2. Read `Saved/Logs/Hawkeye.log` from that run and require zero `LogMaterial: Warning`
   lines and zero `LogHawkeye: Warning` lines other than the ragdoll path notice.
3. Load soft references synchronously in game code paths that need them immediately
   (viewmodel meshes, weapon definitions); never rely on an asset already being resident.

## 2c. The campaign lap (Chapter 1 from a cold start)

`Hawkeye.Lap.Campaign` (standalone `-game`, about 4 minutes) plays CH01 as a new tester: the main menu
with no save (its own slot, `HawkeyeCampaignLap`, deleted first; the settings put back after), New Game,
Normal, the title card, the first text read on the phone, `reach_roof` and `cross_block` by grapple (a
planner that walks to a spot whose zip and camera line are clear, aims and presses Q; the camera line is the
hip camera turned to the anchor, `HawkeyeGrappleView::PredictLens`, as `Hawkeye.Grapple.Audit` has it; with
nothing in reach from a roof she goes down its fire escape and grapples up again from the street), the RoofPair with
a bola, the bow and melee, Esc > Quit to menu > Continue compared field by field (objective, quiver,
thugs, hints, crime and challenge counts, phone, safehouses, position, health), the ArcherPair from
parapet cover (`find_arrow`'s roof reached by grapple, never put on it: from cross_block down a fire escape and
up to the facade anchor on its street wall, an error if it cannot), the examine, the close-up, end card, slides
and the placeholder room walked to its
trigger, then on the street the save, map, music, a challenge, a fast travel, an ambush lost on purpose
and its reload, and Quit and Continue again. Every step checks input, HUD, pause, dilation, fade, the
grey post process and the widgets left in the viewport. It writes `Saved/Automation/campaign_lap.json`
(timeline, checks, the LogHawkeye warnings, and everything it had to force, marked `[lap]` for a gap in
its own driving and `[game]` for one in the game) and `Saved/Screenshots/Campaign/campaign_*.png`.
`AHawkeyePlayerController::bAutomationPlaysAsPlayer` is the hook that gives it the boot menu, the prompt,
the opening, the notices and the crime schedule; `hawkeye.Hints 2` turns the hints on.

## 3. Playtest checklist (human)

Before any commit that touches player feel, AI, or a level, play through this in
`L_District_EastVillage` or the affected chapter. Two minutes.

- [ ] Walk, sprint, crouch, jump. Nothing snaps or floats.
- [ ] Look sensitivity unchanged (or changed on purpose).
- [ ] Fire until empty, reload, fire again. Ammo HUD correct.
- [ ] Takedown a patrolling guard from behind. Prompt appears, guard drops, other guards
      don't react unless they should.
- [ ] Get spotted. Guard shoots, you take damage, screen effect shows.
- [ ] Die. Respawn at checkpoint (stage 3) or mission start.
- [ ] Complete the mission. End card, flashback, next level (or return to menu).
- [ ] Press Esc during gameplay and during a flashback. Correct menu, correct resume.
- [ ] Check `Saved/Logs/Hawkeye.log` for new Warnings or Errors from `LogHawkeye`.

Per-mission and per-boss playtests are in `docs/plans/04-content.md`. Formal playtesting
with other people is `docs/plans/06-polish-ship.md`.

## 3b. Playtest capture (the note key, photo mode, the session report)

Cameron reports feel problems from memory, and every fix so far came from a log line matched to what he
described. The capture kit (`Source/Hawkeye/Playtest/`, bindings in gameplay-semantics.md, Controller) turns
"it felt wrong when I zipped off the roof near the park" into a timestamped record with the state, a picture
and the log around it.

How to use it:
- Start with `.\Tools\play.ps1 -Notes`. It names the session after the launch time, waits for the game to
  close, prints the session folder and writes its `report.md`. Without `-Notes` the kit still records; the
  folder is the newest one under `Saved\Playtest\`.
- **F12** (or **hold Menu 0.6 s** on a pad) the moment something feels off: a note. Keep holding 2 s and the
  next 10 s log LogHawkeye at Verbose, so the thing you do next is captured in detail. A "Note 3 saved" toast
  confirms it.
- **F11** (or **Pause > Photo mode**) for a clean shot: WASD / left stick, mouse / right stick, E and Q (RT and
  LT) up and down, Shift (L3) fast, wheel (LB / RB) field of view, R (Y) back to Kate's view, F12 (A) takes
  the photo, Esc (B) returns to the game exactly where it was.

Where files land, `Saved/Playtest/<session>/` (`<session>` is the launch time, `2026-09-29_14-03-11`, or
`-PlaytestSession=<name>`):

| File | What |
|------|------|
| `note_<n>.png` | the frame at the note, HUD on |
| `notes.json` | every note: index, seconds into the session, wall clock, position, yaw, the state (movement mode, speed, sprint, crouch, parkour move or hang, zip, aim, downed, health, alerted thugs, crime, challenge, interior, map, time of day, difficulty, paused), the state line, the screenshot's name and path, and the last 20 LogHawkeye lines |
| `photo_<n>.png` | photo mode's shots, no UI |
| `summary.json` | notes, photos, session and play time, paused time, deaths, fights and fights won, crimes started and stopped, challenges run and medals, fast travels, distance, grapples, time aiming and in fights, time in each movement state, the maps played. Written on every map change and at exit |
| `Hawkeye.log` | the game's own log, copied at exit (only when the running log is `Saved/Logs/Hawkeye.log`) |
| `report.md` | `python Tools\playtest-report.py <folder>` (or `--latest`, `--stdout`): one markdown file with the summary, each note's state and screenshot link and log lines, the photos, and the log's LogHawkeye warnings |

The log line to grep is `LogHawkeye: NOTE #<n> at <s>s: pos=(x,y,z) yaw=<deg> state=move=Walking speed=512
... fight=2 ...`; the kit's own lines start `Playtest:` and `Photo mode:`. Automation runs (a `-game` run with
`RunTests`, the headless suite) write a session folder only once a note or photo is taken, so test runs do not
fill `Saved/Playtest`. Screenshots go through the same one-at-a-time, off-thread queue as the screenshot tests
(`World/HawkeyeCapture`), so a note can never take a test's capture.

Tests: `Hawkeye.Playtest.*` (headless: the state line, the NOTE line, the note JSON round trip, the summary
counters and the fight rule, the log ring and session name, the photo camera's steps). `Hawkeye.Screenshot.Playtest`
(standalone, `-game`): presses F12, opens photo mode with F11, flies the camera and narrows the view, takes a
photo, leaves with Esc, and checks `note_<n>.png`, `photo_<n>.png`, `notes.json` and `summary.json`; read the two
images after (the photo must have no HUD and no corner card).

## What's not tested with code
Level layout, materials, lighting, animation feel, sound mix, slideshow imagery, story
pacing. These get the human checklist and the playtest rounds. Don't write brittle
screenshot tests.

## CI
`.github/workflows/ci.yml` runs on push and pull request to main, with no Unreal install
(the engine is too big for a hosted runner, so the C++ build and automation tests stay local
via `Toolsun-tests.ps1`). Jobs: `python-tests` (unittest discovery over
`Tools/Editor/test_*.py`), `osm-determinism` (re-parse the committed Overpass data, fail if
the parsed JSON changes), `lint-python` (ruff, rules in `ruff.toml`), and `format-cpp`
(clang-format on C++ files changed under `Source/` only). Checkouts skip LFS.
