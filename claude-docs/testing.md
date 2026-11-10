# Testing

Three layers. Most effort goes into the first.

## 1. Automation tests (C++)

Where: `Source/Hawkeye/Tests/*Test.cpp`. Compiled only in editor and development builds
(`#if WITH_DEV_AUTOMATION_TESTS`). Named `Hawkeye.<System>.<Behaviour>`.

What gets tested: every rule in `gameplay-semantics.md`. Concretely:

| System | Tests |
|--------|-------|
| Health | clamp, invulnerable ignores damage, OnDeath fires once, heal after death is a no-op, Revive works, negative damage ignored |
| BossPhase | thresholds advance in order, overkill skips to correct phase with one broadcast, transition invulnerability set and restored, N phases produce N-1 transitions, a crew opens its phase, restore to a checkpoint |
| Boss | phase walk and crew entry, winded window and its damage, finisher share, hazard cycle and hits, checkpoint from objectives, barks, a three-phase fight to the chapter's end in a test world, the checkpoint restart (`Hawkeye.Boss.*`); campaign order (`Hawkeye.Campaign.*`) |
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

`Hawkeye.Lap.Campaign` (standalone `-game`, about 2 minutes) plays Chapter 1, "Night Shift", as a new tester:
the main menu with no save (its own slot, `HawkeyeCampaignLap`, deleted first; the settings put back after), New
Game, Normal, and the chapter opens mid-chase under its title card. The scripted player runs the route the chase's
data asset lays out (`DA_Chase_CH01_NightShift`): a sprint over the vent boxes (vaults), the bulkhead (a mantle;
the jump key if the sprint stops short), the first grapple at its stand (aimed at the route's anchor, Q only on a
green marker), the grapple and its chain mid-zip, the grapple that goes wrong into the dumpster (its three moves
watched phase by phase), out of the alley along the navmesh, and a takedown from behind where the thief is
cornered. After the first grapple lands, Esc > Quit to menu > Continue is compared field by field (objective,
quiver, thugs, hints, crimes, challenges, phone, safehouses, position, health, and the chase: state, both
progresses, waypoints reached, the beat). Then the end card, the street with "[Chapter complete]", Clint's texts
read on the phone (no "Kate" contact: her replies sit in his thread), the save, map, music, a challenge, a fast
travel, an ambush lost on purpose and its reload, and Quit and Continue again. Every step checks input, HUD, pause,
dilation, fade, the grey post process and the widgets left in the viewport. It writes
`Saved/Automation/campaign_lap.json` (timeline, the chase's lead sampled each second, `chase_seconds`,
`lead_min_cm`/`lead_max_cm`, checks, the LogHawkeye warnings, and everything it had to force, `[lap]` for a gap in
its own driving and `[game]` for one in the game) and `Saved/Screenshots/Campaign/campaign_*.png` (open, grapple
prompt, thief on a roof, dumpster, cornered, end card, street, phone).

`Hawkeye.Lap.ChaseFallBehind` (about 30 s, its own process) starts the same way and then stands still: the chase is lost
("[He got away]"), the checkpoint (the new game's save) reloads, and the chase picks up from it with the thief no
further than `RestoreMaxLead` ahead and running. `campaign_fall_behind.json`, `campaign_lost*.png`.

`AHawkeyePlayerController::bAutomationPlaysAsPlayer` is the hook that gives both the boot menu, the prompt, the
opening and its chase, the notices and the crime schedule; `hawkeye.Hints 2` turns the hints on. Run each in a
process of its own: the campaign state lives in the game instance, so a second lap in one process starts with the
first one's (its title card already seen).

## 2d. The social club lap (chapter 4 from its start)

`Hawkeye.Lap.SocialClub` (standalone `-game`, `-Difficulty=Normal`, about 2 minutes, its own process): the district
loads on chapter 1, the lap starts `DA_CH04_SocialClub` the way chapter select does and runs its opening (Kate on the
street stand), grapples the roof's parapet anchor, waits out the pre-fight lines and fights (parry a seen wind-up,
finish anyone open, dodge a swing about to land, strike otherwise, never hit Tomas while he watches, leave the
Santa's lane when it lights, one confetti arrow at Tomas). In Tomas's phase, under 85% of his health, it saves and
loads (its own slot, `HawkeyeSocialClubLap`) and checks the fight came back at his phase's checkpoint: full health, no
crew, fighting, Kate where she was with her health, the same objectives. Then to the end card. Writes
`Saved/Automation/social_club_lap.json` (marks per phase, hits, the Santa's hits, finishers, winded windows, the round
trip, the timeline) and `Saved/Screenshots/SocialClub/social_*.png` (street, intro, crew, tomas or tomas_winded,
santa, down, endcard).

Since 2026-11-10 it also stands Kate 60 cm outside the Santa's lane across from Tomas while it telegraphs (the lure) and
asserts the sweep catches him at least once.

## 2e. The reindeer games lap (chapter 2 from its start)

`Hawkeye.Lap.ReindeerGames` (standalone `-game`, `-Difficulty=Normal`, about 100 s, its own process): the lap starts
`DA_CH02_ReindeerGames` the way chapter select does and its opening (Kate on the district's PlayerStart), runs the navmesh
to Avenue B, examines the courier on the way (the optional, unmarked objective: no toast, `saw_courier` raised), arrives
(the crew wakes, 3 foam arrive), fights (parry, finish, dodge, strike, the gunner shot, one foam arrow), and with one sack
carried saves and loads (its own slot, `HawkeyeReindeerGamesLap`) and checks the objectives, the flag, the fired events
(no line repeated), the foam, the carried sack, no crew and the sacks left. Then the other sacks, the table, the end card,
the "Visiting hours" slides, the texts on the phone, and chapter 4 started after it (no site actor left, Tomas waiting,
2 foam). Writes `Saved/Automation/reindeer_games_lap.json` and `Saved/Screenshots/ReindeerGames/reindeer_*.png` (site,
courier, crew, foam, sack, return, endcard, slides, phone).

## 2f. The grotto laps (chapter 3 from its start)

`Hawkeye.Lap.Grotto` (standalone `-game`, `-Difficulty=Normal`, its own process): the lap starts `DA_CH03_Grotto` the way
chapter select does and its opening, runs the navmesh to the grotto's frontage (find_sleigh), goes in by its door (the
chapter carries on in `L_Int_Grotto`), takes the hall's elf down from behind, creeps past the two elves talking in the
stockroom (overheard) to the boomerang on the shelf, looses the boomerang at the alarm box so the corridor's guard comes
to look (it trips the box, comes back and is caught), takes the guard down at the box, reaches the workshop door unseen
(enter_grotto), saves and loads inside the interior (its own slot, `HawkeyeGrottoLap`) and checks the objectives, the
events (no line again), the boomerangs, the elves down, the door, Santa and Kate's place, opens the door at the keypad
(open_vault), beats Santa, leaves by the roof stairs and walks onto the roof (leave_roof); then the end card,
the call from the farm (its subtitles), the ornament at her door (found_ornament) and chapter 4 started after it (2 foam,
2 boomerangs, Tomas waiting). Fails if any elf is ever Alerted. `Hawkeye.Lap.SeenGrotto` (its own process) creeps to the
shelf, looses the boomerang out of the stockroom and turns her back so it comes into her (the self-hit and its line, never
her last health), walks into the hall standing up so the hall's elf sees her (the spotted title and line), fights every
elf, then the keypad, Santa and the roof. Writes `Saved/Automation/grotto_lap.json` / `grotto_loud_lap.json` and `Saved/Screenshots/Grotto/grotto_*.png` (frontage, inside,
boomerang out and back, vault, santa, santa_down, roof, endcard, call, ornament).

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
via `Tools
un-tests.ps1`). Jobs: `python-tests` (unittest discovery over
`Tools/Editor/test_*.py`), `osm-determinism` (re-parse the committed Overpass data, fail if
the parsed JSON changes), `lint-python` (ruff, rules in `ruff.toml`), and `format-cpp`
(clang-format on C++ files changed under `Source/` only). Checkouts skip LFS.
