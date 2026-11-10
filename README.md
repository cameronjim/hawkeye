# Hawkeye

A third-person action game in Unreal Engine 5.8. Kate Bishop, a bow, and a dog in a
sweater, in a slice of the East Village built from real map data. Solo project, C++.

This is a fan project. It is NOT affiliated with Marvel, Disney, or Insomniac Games. It is
built in the style of Insomniac's Spider-Man games because those are the games I want to
learn to make. Nothing here is for sale.

## What is in it

**Traversal.** Kate has no web to swing on. She has parkour and a grapple arrow.
Both run on a custom `UCharacterMovementComponent` with their own movement modes, so the
zip and every vault are real movement with real velocity. You keep your speed going into
a zip. You land running. Let go mid-line and you are thrown along your path. Vaults and
mantles play animation warped to the ledge that was measured, so the body fits the wall.
Start in `Source/Hawkeye/Player/HawkeyeMovementComponent.h`.

**Combat.** Four thugs do NOT all swing at once. They take turns. An attack token decides
who attacks, the rest hold a ring around her, and a swing from behind always winds up
long enough to react to. The rules are pure functions with tests. Start in
`Source/Hawkeye/Combat/CombatTokenRules.h`.

**The city.** The district is NOT hand-built. It is generated. A Python pipeline reads
OpenStreetMap footprints and heights for the blocks around Tompkins Square Park and
writes the buildings, parapets, fire escapes, ledges and grapple anchors. Think of it as
a compiler: same input, same city, byte for byte, and it only rebuilds what changed. A
reach model places anchors so every street-facing roof can be grappled from the pavement,
and a second implementation in C++ checks the first using the game's own targeting.
Start in `Tools/Editor/generate_city.py` and `Tools/Editor/_reach.py`.

**Tests.** About 500 automation tests run headless: the rules for movement, damage,
tokens, missions and saves, plus scripted laps that play the chapter from the main menu.
The Python geometry has its own unit tests and golden hashes, and those run in CI.

## Status

A prototype. One district, one chapter being rebuilt around the story in
`docs/STORY.md`. Characters are stand-ins and most of the city is flat colour. The code
is ahead of the art on purpose: traversal had to feel right first.

## Setup

You need:

- Unreal Engine 5.8
- Visual Studio 2022 with the **Game development with C++** workload (`.vsconfig` lists the components)
- Git LFS (`git lfs install` before cloning)
- Epic's **Game Animation Sample** project, created from the Epic Launcher's Samples tab.
  Kate's locomotion comes from it and it is not redistributed here.
- Recommended: **Paragon: Sparrow** from Fab (free), added to the Game Animation Sample project. It gives
  Kate her intended look (Sparrow, in purple, drawn over the sample's mannequin) and the bow animations.
  Without it Kate is the grey mannequin in a purple suit and everything still plays. After adding it, run
  `.\Tools\import-anims.ps1` once: it copies Sparrow in and builds her look.

The scripts currently expect the engine at `C:\Program Files\Epic Games\UE_5.8` and the
sample at `C:\Users\camer\code\GASP`. `claude-docs/infrastructure.md` has the details.

```powershell
# Copy the sample's assets in and generate every game asset (safe to run twice)
.\Tools\create-content.ps1

# Build the editor target
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HawkeyeEditor Win64 Development -project="<repo>\Hawkeye.uproject" -waitmutex

# Run the tests
.\Tools\run-tests.ps1

# Play
.\Tools\play.ps1
```

## Layout

| Path | What |
|------|------|
| `Source/Hawkeye/` | C++. `Player` (movement, parkour, grapple), `Combat`, `World` (thugs, city), `Mission`, `UI`, `Tests` |
| `Tools/Editor/` | Python that generates the city and every asset |
| `Tools/` | PowerShell wrappers for build, test, content, packaging and measurement |
| `Content/` | Generated assets (LFS) |
| `docs/` | Design, story, chapter plans, playtest briefs |
| `claude-docs/` | Engineering reference: architecture, gameplay rules, code style, testing |

## How it is built

I work with Claude Code as a pair. I set the design and the story, play the build, and
decide what changes. The agent writes much of the code under the rules in `CLAUDE.md`,
and nothing lands without a clean build, the full test suite, and the scripted laps.
`docs/playtests/` has the notes from each round.
