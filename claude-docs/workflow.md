# Workflow

How to work in this repo, whether you're Cameron, Claude, or a subagent.

## Before touching anything
1. Read `CLAUDE.md`. Then the `claude-docs` file for the area you're changing.
2. Read `docs/DESIGN.md` if the change affects what the game is. Read the stage plan in
   `docs/plans/` for what's in scope right now. Building stage 4 features during stage 2
   is the most common way this project dies.
3. `git status` and `git log -5`. Know what's uncommitted and what just landed.
4. If the editor is open, know it. Building C++ under an open editor without Live Coding
   wastes the build.

## Making a change
1. **Say what you're doing in one line before you do it.** In chat for Claude, in a
   commit message draft for a human.
2. **Data first.** Can this be a property, a data asset, a Blueprint default? Then do
   that. C++ is for rules and reusable machinery.
3. **Semantics doc before code**, for any new rule. Add the bullet to
   `gameplay-semantics.md`, write the test, then make it pass. This is not ceremony:
   the semantics doc is what keeps eight missions consistent.
4. **Build after every C++ change.** Fix warnings you introduced.
5. **Run the gate**: `Tools\gate.ps1` (the quick tier) after every commit or small batch. It picks the build, tests,
   content steps, audit and laps the change needs and runs only those; that is the whole per-change requirement
   (`testing.md`, 2k). `Tools\run-tests.ps1 -Filter <name>` is still there for one group while you work.
6. **Human checklist** (`testing.md`, section 3) after anything that affects feel.
7. **Commit and push.** Small commits, often: each class, each test file, each script,
   each doc is its own commit. Message: lowercase imperative summary under 70 chars,
   optional blank line and a short why. No co-author, no generated-by trailer, nothing
   after the body. `git push origin main` after every commit or two. Remote:
   https://github.com/cameronjim/hawkeye.git

## Agent work
- **Read the relevant `claude-docs` file first, every time.** Context from a previous
  session is not in your head.
- **Stay in your lane.** If you were asked to fix compile errors, don't refactor. If you
  were asked to add a test, don't change the semantics it tests. Report what else you
  noticed instead.
- **Never leave the tree broken.** If you can't get the build green, revert your changes
  and report the errors. A red build blocks everyone.
- **Headless over visual.** Use `UnrealEditor-Cmd.exe -nullrhi` for builds, tests, cooks,
  and Python asset scripts. Launch the full editor only when explicitly asked to check
  something visual, and close it when done.
- **Don't guess engine APIs.** UE 5.8 differs from what most training data shows. If a
  call doesn't compile, look in `C:\Program Files\Epic Games\UE_5.8\Engine\Source` for
  the header (grep is fine) rather than trying variations.
- **Report honestly.** "Built, tests pass" only if you ran them and they did. If a
  test is flaky, say so. If you skipped something, say what.
- **Python asset scripts are repeatable.** Every script in `Tools/Editor/` must be safe
  to run twice: check whether the asset exists before creating it, and save only what
  changed.
- **Don't touch:** the `ProjectID` in `DefaultGame.ini`, the project or module name,
  `.gitattributes` LFS rules (add, never remove), anything under `docs/` unless the task
  is docs.
- **Model routing** (Cameron's rule): thinking, design, and review in the main session.
  Delegated agents get `opus` for hard work (compile-fixing, systems code, AI) and
  `sonnet` for easy work (file moves, running scripts, checks).

## The gate, and not waiting on it
- Per change: `Tools\gate.ps1`. Not the old list of two builds, the whole suite, content twice, city, audit and 13 laps. `-Plan`
  shows what it would run; `-Status` shows when a full run is due.
- Full tier (`Tools\gate.ps1 -Tier full`, about 50 minutes) only before the owner records or a demo or milestone build is called
  ready, or when `-Status` shows risky changes since the last full. Never per change, and never in a brief for a single task.
- Do not sit waiting on the engine. Code-only work goes in worktrees, in parallel; one "engine lane" at a time runs the quick gates and
  the captures. A visual check captures several strips in one launch (`-HawkeyeTraversalOnly=a+b`).
- Machine rules: one heavy process at a time; `tasklist | findstr /i "Unreal Blender"` as its own command before each launch (the gate
  does it before every step and waits); an Unreal you did not start means the owner is playing, so wait, never kill it; `-ExecCmds`
  quoted and holding only `Automation RunTests <name>; Quit`; never touch `Saved\SaveGames\HawkeyeCampaign.sav` or `HawkeyeSettings.sav`.

## When something breaks
- Compile error: read the first error, not the last. UHT errors before compiler errors.
- Editor crash on startup: check `Saved\Logs\Hawkeye.log` for the last `LogHawkeye` line and
  `Saved\Crashes\` for the callstack. Usually a constructor touching something that isn't
  ready, or a bad config value.
- Asset won't load / "failed to load": a redirector or a renamed class. Fix Up
  Redirectors, or add a `[CoreRedirects]` entry in `DefaultEngine.ini` for the rename.
- Test fails only headless: it's touching rendering, input, or audio. Assert on calls,
  not effects.
- Out of memory: close the editor, close the browser, retry. If it recurs in the same
  map, that map needs streaming or fewer lights.

## Definition of done, for a task
- Builds clean.
- `Tools\gate.ps1` passes, and new rules have tests.
- Docs updated if semantics, structure, or conventions changed.
- Committed with a message that explains why.
- Anything left undone or noticed along the way is written in the report or as a
  `// TODO(stageN)` in code.

## Use what exists first
Before writing a system from scratch, check for an engine plugin, an Epic sample, or an
open-source library that does it well, and use it if it fits. Write from scratch when the
options are poor or don't do what the game needs. Always say which route you took and why
in the task report and in the relevant doc. Candidates to check first:

| Need | Look at |
|------|---------|
| Saves and checkpoints | SPUD (open source), else the engine's SaveGame |
| AI behaviour, partner logic | StateTree, Smart Objects, EQS, Behavior Trees |
| Menus and HUD | Common UI, UMG |
| Camera rigs | Gameplay Cameras plugin |
| Locomotion, traversal | Game Animation Sample (in use), Motion Warping, Pose Search |
| Placing props and clutter | Procedural Content Generation (PCG) |
| Effects | Niagara and the engine's starter content |
| Dialogue | DlgSystem or Mountea Dialogue (open source) |
| City data | OpenStreetMap via Overpass (in use), NYC Open Data |
| Real-world reference | Cesium for Unreal with Google 3D Tiles, reference only |

Free Fab packs are fine when they carry a permissive licence; paid packs only if Cameron
buys them. Epic's own samples are covered by the engine licence.
