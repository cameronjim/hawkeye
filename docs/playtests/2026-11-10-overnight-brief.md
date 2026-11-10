# Overnight brief, 2026-11-10

For Cameron. Written as the night went; newest state is at the top of each section. Every
item marked "on main" passed its gate: clean build, the full headless suite, the
standalone laps, and screenshots read by eye.

## Play this first
1. **The grapple.** Zip to a roof with the stick held: she should land running. Zip again
   and let go mid-line with jump: she should be thrown along her path, not dropped.
2. **A sprint vault.** Sprint at a low wall. She should come out the far side at speed.
3. **A mantle at two heights.** Waist-high and head-high. The body should fit the wall,
   not stretch.

Open the editor before we talk so the Unreal MCP tools can connect.

## On main
- **Movement component.** `UHawkeyeMovementComponent` runs the grapple zip and every
  parkour move as custom movement modes with real velocity. Zip entry keeps her speed and
  accelerates; arrival carries 600 cm/s; a jump release keeps the zip's velocity plus a
  boost. Vaults leave at entry speed. Vault and mantle play the sample's clips with motion
  warping to the measured ledge, replacing the stretched 1 m clip.
- **Generator split.** `generate_city.py` went from 5,356 to 4,143 lines; the reach model,
  fire escapes, ledges, district data and geometry helpers are plain-Python modules with
  unit tests and golden hashes. Regenerating the city in the editor rebuilt nothing.
- **Hosted CI.** `.github/workflows/ci.yml`: Python tests, map-data determinism, lint.
- **Story.** `docs/STORY.md` (yours, verbatim), `docs/chapters/ACT1.md` (chapter split),
  `docs/chapters/ACT1-script.md` (draft lines for you to edit).
- **Small.** The false Epic copyright line is gone from all source; project descriptions
  no longer say first-person shooter.

## Needs you
- Feel checks above.
- Edit or replace the draft lines in `ACT1-script.md`.
- The download list below. One "yes" covers what you want; logins and licence clicks are yours.
- Confirm the internship's enrolment rule and deadline.

## Download list
Nothing was downloaded. Already on disk and usable with no download: the Paragon Sparrow
mesh and skins (`Content/ParagonSparrow`), and the sample's mannequins.

| Need | Candidate | Source | Licence | Size | Login |
|------|-----------|--------|---------|------|-------|
| Lucky | LowPoly Animated Animals (the dog; no sit clip) | quaternius.itch.io/lowpoly-animated-animals | CC0 | 6.6 MB | no |
| Thug and boss bodies | Universal Base Characters (Standard) | quaternius.itch.io/universal-base-characters | CC0 | 122 MB | no |
| Extra animations | Universal Animation Library 2 (Standard) | quaternius.itch.io/universal-animation-library-2 | CC0 | 17 MB | no |
| Hang, shimmy, roll, get-up clips | Mixamo (names to check by eye) | mixamo.com | Mixamo terms | small | Adobe |
| Brick | `red_bricks_02`, `painted_brick` | polyhaven.com | CC0 | ~100 MB | no |
| Pavement, road | `concrete_pavement_03`, `asphalt_snow` | polyhaven.com | CC0 | ~100 MB | no |
| Snow | Snow010A | ambientcg.com | CC0 | ~24 MB | no |
| Christmas props | Holiday Kit | kenney.nl/assets/holiday-kit | CC0 | small | no |
| Hits and foley | Impact Sounds, RPG Audio | kenney.nl | CC0 | small | no |
| Bow shot stand-in | Crossbow firing and hitting target (384919) | freesound.org | CC0 | small | likely |
| Carols | "Hark! The Herald Angels Sing" (U.S. Army Band), "Deck the Halls" (USAF Band) | commons.wikimedia.org | Public domain | ~4 MB | no |

Not found free: an inflatable Santa, wreaths, string lights, a bow mesh, a tracksuit
outfit, dog bark, winter street ambience. Those get built by script or stay synthesized.
Sizes marked ~ are estimates. Fab showed no licence text on the listings checked.
