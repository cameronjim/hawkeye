# Credits and third-party notices

What the game uses that it did not make from scratch, under which licence, and where it lives. Content from these
sources is git-ignored and rebuilt from files outside the repo (`claude-docs/infrastructure.md`) unless noted.

## Characters

| What | Source | Licence | Notes |
|------|--------|---------|-------|
| The base body of the thugs, the heavy and Clint (`SK_Body_Male`), and the hair meshes on the Blender-built costumes | Quaternius, Universal Base Characters (Standard) | CC0 | Clothes are modelled on it by `Tools/Blender/` (ours). |
| Quaternius's other packs on disk (LowPoly Animated Animals, Universal Animation Library 2) | Quaternius | CC0 | Not used by the game now (the animals pack's pug was Lucky's stand-in until 2026-11-11). |
| Lucky, the golden retriever (`SK_Lucky` and its clips) | Generated locally with TRELLIS.2 from a reference image made with Z-Image-Turbo, then rigged and animated in Blender | TRELLIS.2: MIT. Z-Image-Turbo: Apache-2.0 | TRELLIS.2 depends on Meta's DINOv3, which is under Meta's own DINOv3 licence, not an open-source one. Owner sign-off on that licence is **pending**; until it is given, treat Lucky's mesh as not cleared to ship. |
| Kate's look (Sparrow) | Epic Games, Paragon | Epic's Paragon asset terms (Unreal Engine projects only) | Never redistributed; tinted through our own material instances. |
| Locomotion, the mannequins and their clips | Epic Games, Game Animation Sample | Unreal Engine EULA | Copied in by `Tools\create-content.ps1`. |

## Code

| What | Source | Licence |
|------|--------|---------|
| Save system (`Plugins/SPUD`) | sinbad/SPUD | MIT |
