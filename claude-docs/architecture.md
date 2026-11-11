# Architecture

One game module, `Hawkeye`, a handful of subsystems that talk through delegates. Nothing
holds a hard pointer across subsystem boundaries; everything that crosses one is either
a data asset reference or an event.

This document says where things are and how they connect. What each system promises (the
numbers, the edge cases) is in `gameplay-semantics.md`; read the matching section there
before changing a rule. Each section below names it.

## Pivot status
The project pivoted from a first-person Punisher game to a third-person Hawkeye game on
2026-09-25. The pivot is done: the first-person arms, viewmodel and body-mesh head hiding are
gone, and the camera, locomotion, parkour, grapple, bow, quiver and trick arrows are built. What
is left of the old design is small and named here so nobody builds on it:

| Leftover | What it is now |
|----------|----------------|
| `UWeaponComponent`, `UWeaponDefinition` | The thugs' hitscan pistol, and Kate's `Hands` punch while she owns no bow. The player shoots with `UBowComponent`. |
| `AHawkeyeGameMode::RestartMission` | Reloads the level. Death and the pause menu's restart use `ReloadLastCheckpoint`, which loads the last save. |
| `gameplay-semantics.md` sections marked LEGACY or `Choices` | Punisher text, kept until the chapters that replace it are written. |

Still planned: World Partition (the district is one map with it off). Act 1's four chapters are built (chapter 3 on
2026-11-10); the campaign's order runs 1, 2, 3, 4.

## The systems

```
Player input
    │
    ▼
AHawkeyeCharacter ──► UBowComponent ──► AArrowProjectile ──► ApplyPointDamage ──► UHealthComponent (on target)
    │                      │                  │                                        │
    │                      │                  └─► AArrowEffect (trick arrows)          ├──► OnHealthChanged ──► UBossPhaseComponent ──► OnPhaseChanged
    │                      └─► UGrappleComponent ──► UHawkeyeMovementComponent (zip)    │
    ├──► UMeleeComponent / UFinisherComponent / UTakedownComponent                      └──► OnDeath
    ├──► UParkourComponent ──► UHawkeyeMovementComponent (traversal)
    └──► UInteractionComponent ──► IInteractable (doors, pickups, safehouses, ...)

Thugs: AThugAIController senses ──► ST_Thug (StateTree) ──► actuators ──► UCombatTokenSubsystem
Partner: AHawkeyePartnerController ──► ST_Partner (StateTree)

World: AObjectiveTriggerVolume, doors, pickups, chase, crimes, challenges
    │
    ▼
UMissionSubsystem (world subsystem)
    │  StartMission(UMissionDefinition*)
    │  CompleteObjective(FName)
    ├──► OnObjectiveUpdated ──► WBP_Hud, AHawkeyeGameMode (autosave), phone triggers
    ├──► OnMissionComplete ──► AHawkeyePlayerController ──► UMissionFlowController
    └──► OnFlashbackRequested(UFlashbackDefinition*)
              │
              ▼
       close-up ► end card ► UFlashbackWidget ► playable scene ► next level, roaming or final card
```

### Mission (`Source/Hawkeye/Mission`)
Rules: `gameplay-semantics.md`, "Mission", "Objective markers and compass".
- `UMissionDefinition` (primary data asset): the mission's identity, its ordered
  objectives (instanced `UMissionObjective` objects, below), the flashback to play after,
  the next level, and per-chapter switches: `StartingBow` and `StartingArrows` (what Kate is
  granted), `bPartnerPresent`, `bAllowSwitching`, `Chase`, `OpeningTitle`, `OpeningSubtitle`,
  `OpeningDialogueSequence`, `bReturnToRoamingAtEnd`, `bEnforceOrder`, `bShowObjectiveText`.
  One asset per mission in `Content/Missions`.
- `UMissionObjective` (instanced UObject): title, description, optional flag, completed
  flag, an `ObjectiveId` FName used by the world to complete it. Blueprintable, but most
  are the base class with data.
- `UMissionTracker`: every mission rule, with no world. Tests create one with `NewObject`.
- `UMissionSubsystem` (world subsystem): the Blueprint-facing wrapper that gives the tracker
  a lifetime. Duplicates the definition's objectives on `StartMission` so the asset never
  gets dirtied. Fires the delegates above. Also holds the marker points the HUD, compass and
  map read (`RegisterObjectiveLocation`, `SetSecondaryMarkers`). Lives per world, so a level
  transition resets it; the save system restores state on load.
- `AObjectiveTriggerVolume`: the common way levels complete objectives. Overlap by the
  player pawn, calls `CompleteObjective(ObjectiveId)`. `AThugGroupObjective` completes one
  when every thug with a tag is dead. `AChapterEndInteractable` (World) completes one when
  Kate examines it. Doors, pickups and scripted beats do the same call from Blueprint. An
  actor whose objective the running chapter does not have does nothing.
- Objectives may be optional and unmarked (`bUnmarked`: never on the HUD, no toast) and may raise a campaign flag
  (`CampaignFlag`, into `UHawkeyeCampaignState::CampaignFlags` before the objective's autosave, for later chapters).
- Chapter events (`ChapterEvents.h`, `UChapterEventSubsystem`, world): `UMissionDefinition::Events` speak lines and grant
  arrows the first time something happens in a run of the chapter (a parry, an arrow loosed, an objective completed or
  started). The character and the bow report through `UChapterEventSubsystem::NotifyEvent`; the objectives it hears
  itself. What fired is mirrored into the save. Rules: "Chapter events".
- Chapter sites (`UChapterSiteDefinition`, `UChapterSiteSubsystem`, world): a chapter's set piece on the district as data
  (props, lights, objective volumes, a crew and its group objective, keycard pickups, examines), spawned when the
  chapter starts, cleared when another does, matched to the save on a load, and the opening's start point. Built for
  chapter 2's toy drive (`DA_Site_CH02_ToyDrive`, `Tools/Editor/create_site.py`, planned by `_site.py`). Since chapter 3
  a site names its map and can carry interior doors, return points, triggers (a box that reports a chapter event) and
  switches (`AArrowSwitch`); its watch (`Watch`, a 0.2 s timer) fires the triggers and spots stealth objectives
  (`UMissionObjective::SpottedTitle`, `UMissionTracker::MarkObjectiveSpotted`). Rules: "Chapter sites", "Mission".
- Chapter interiors (`UHawkeyeChapterMaps`, `ChapterMaps.h`): which map a site or boss belongs in, and whether the running
  chapter carries on into an interior (`UMissionDefinition::InteriorSites`). `UHawkeyeSaveSubsystem` does the carrying:
  `EnterInterior` marks a continuation, the interior's game mode restores the chapter and calls `FinishContinuation`
  (which broadcasts `OnCampaignLoaded`), saves inside are allowed and keep the way out (`InteriorReturnMap`), and
  `ReturnFromInterior` carries the campaign out over the district's entry save or into a fresh district
  (`CarriedCampaign`, `AdoptCampaign`). Built for chapter 3's grotto (`DA_Site_CH03_Grotto`, `DA_Site_CH03_GrottoInside`,
  `Tools/Editor/create_grotto.py`, planned by `_grotto.py` and `Tools/Interiors/Grotto.json`). Rules: "Chapter
  interiors".
- `UMissionFlowController`: the order of the end-of-mission beats (close-up, end card,
  flashback, playable scene, then next level, return to roaming, or the final card), with
  no UMG, world or travel in it, so tests drive `Begin` and `Advance`. The player
  controller owns one and performs each step.

### Flashback (`Source/Hawkeye/Flashback`)
Rules: `gameplay-semantics.md`, "Flashback".
- `UFlashbackDefinition` (primary data asset): ordered `FFlashbackSlide` array (image or a
  tint, caption, hold seconds, crossfade seconds, voice line), ambient loop, skippable flag,
  and an optional `PlayableScene` map with a `ReturnPointLabel` for the district.
- `UFlashbackSequencer`: the clock, with no UMG or world.
- `UFlashbackWidget` (UUserWidget): two stacked `UImage`s crossfaded by opacity, a caption
  block, timing driven from `NativeTick` because the game is paused during playback and
  paused worlds don't run timers. Audio is created as UI sound so it survives the pause.
  Fires `OnFlashbackFinished`. Designers subclass it as `WBP_Flashback` to lay out the
  visuals; the logic stays in C++.
- `UFlashbackReplayWidget` (pause menu): replays the flashbacks the settings save says were seen.
- The playable scene and the interiors share one mechanism: `UHawkeyeSaveSubsystem` saves
  the district, opens the other map, and `FHawkeyeSceneReturn` puts the player back.

### Combat (`Source/Hawkeye/Combat`)
- `UHealthComponent`: `MaxHealth`, `CurrentHealth`, `bInvulnerable`, `bCannotDie`, optional
  regeneration. Hooks the owner's `OnTakeAnyDamage`. Fires
  `OnHealthChanged(Component, NewHealth, Delta, Instigator)`, `OnStaggered` and `OnDeath`.
  `ApplyMeleeDamage` can stagger. Every damageable thing has one: Kate, Clint, thugs, shootable lights.
  Rules: "Health and damage".
- `UBossPhaseComponent`: requires a `UHealthComponent` on the same actor. An ordered
  `FBossPhase` array: health thresholds (a fraction), an entry (health below, or the crew of the phase before
  down: `NotifyCrewDown`), a whole-phase invulnerability, a protected transition. Listens to `OnHealthChanged`,
  walks phases with `UBossRules::ResolvePhase`, fires `OnPhaseChanged(Old, New, Phase)`; `RestorePhase` is a
  checkpoint. It does not know what a phase means; `UBossFightSubsystem` (Boss, below) reacts. The music reads it
  to tell a boss from a thug. Rules: "Boss phases".
- `UBowComponent`, `UBowDefinition`: the player's bow. `StartDraw` on the fire press,
  `ReleaseDraw` on release; the draw fraction sets speed, damage and spread, with a bonus
  for a release in the perfect window. Spends one arrow of the active quiver slot and spawns
  its projectile. Also owns the bow's look (back while holstered, left hand while drawing)
  and, with `UHawkeyeBowIKAnimInstance` as its post-process AnimBP (built by
  `UHawkeyeBowIKGraphBuilder`), the two-hand IK. Thugs with a bow have their own. One bow per
  character: `DA_Bow_Kate`, `DA_Bow_Clint`. Rules: "Bow and arrows".
- `UArrowDefinition` (primary data asset): one arrow type, its projectile, on-hit effect,
  damage, cap and whether it can be pulled back out. `AArrowProjectile` flies, hits, embeds
  and is recoverable. A new arrow is a new asset. The quiver has seven slots
  (`HawkeyeQuiverSlotCount`), six with number keys. A returning arrow (the boomerang) is an arrow asset whose projectile
  is `AReturningArrowProjectile`, flown by its `ReturnFlight` (`Combat/ReturnFlight.h`: the struct and
  `UHawkeyeReturnFlightRules`, pure); it trips `AArrowSwitch`es (World). Rules: "Trick arrows", returning arrows.
- Trick arrows (`Combat/ArrowEffects`): the projectile spawns an `AArrowEffect` at the
  impact and the effect runs on its own clock (`AHeldEffect` putty, `ABolaEffect`,
  `ASmokeCloud`, `AEmpPulse`, `AExplosiveBlast`). Custom actors, not the Gameplay Ability
  System. `UArrowEffectsSubsystem` knows which smoke clouds are up (the thugs' sight check
  asks) and which street lamps an EMP has put out. Rules: "Trick arrows".
- The grapple arrow is not a bow projectile: release with it nocked calls
  `UGrappleComponent::TryFire` (Player, below). `AGrappleArrowProjectile` (World) is the
  arrow in flight.
- `UMeleeComponent`: a swing as wind-up, one sweep and recovery, shared by Kate (light,
  heavy) and the thugs (fists, bat). The attack is data, an `FHawkeyeMeleeAttack`. With a
  clip, the montage's `ANS_HitWindow` and `ANS_ComboWindow` time the swing and motion warping
  closes on the target. `MeleeCombo.h`, `StrikePose.h`, `UHawkeyeMeleeRules` (parry rules)
  and the soft-lock assist hold the pure parts. Rules: "Melee depth", "Melee assist".
- `UFinisherComponent`: the loud kill on a staggered or downed thug. `UTakedownComponent`:
  the stealth kill, tried first on the same key. `TryTakedown` sphere-traces for actors
  tagged `Thug`, checks the player is behind the target within `MaxAngleDegrees`, asks
  `ITakedownable::CanBeTakenDown` (thugs refuse while alerted) and calls
  `ITakedownable::OnTakedown`. Fires `OnTakedownPerformed`.
- Combat animation clips: `UCombatAnimSet` (one data asset per character, a montage per
  `ECombatAnimRole`), `UHawkeyeCombatMontageBuilder` and `CombatAnimPlayback` (the headless
  import pipeline and the playback helpers). An empty slot means the procedural fallback.
  Pipeline: `animation.md`.
- Combat readability and aim assist: `CombatReadability.h` (target marker, fight camera,
  telegraph look), `UHawkeyeAimAssist` (the bow's cone, magnetism, snap and bend). Rules:
  "Combat readability", "Bow aim assist".
- Attack tokens (`UCombatTokenSubsystem`, a `UWorldSubsystem`; rules in `UHawkeyeCombatTokenRules`): per target,
  a melee and a ranged budget of who may be attacking her now. Attackers call `RequestToken` from their own
  think (asking and renewing are the same call, so nothing here ticks or sweeps actors) and `ReleaseToken` when
  the attack ends or anything stops it; budgets hold attackers by weak pointer and let a silent holder's token
  lapse. A holder is dropped after `LeaseSeconds` of silence and never keeps a token past `MaxHoldSeconds`. The
  rules rank the waiters (the overdue first, then not the last to attack, on screen before off, nearest, longest
  wait) and place the ones without a token on a ring round her (`GetRingSlotYaw`). `GetBudgetView` is the
  read-only view `hawkeye.DebugTokens` and the tests use. Thugs ask from the fight's tasks
  (`HoldRing`, `Swing`, `Burst`, `Draw` in ST_Thug, below) and give the token back when the swing, burst or draw
  ends or the fight is left. Written from scratch: the engine has no shared attacker budget, and
  Smart Object slots claim places, not turns. Budgets, gap and the off-screen telegraph
  are rows in the difficulty table. Rules: "Attack tokens".

### World (`Source/Hawkeye/World`)
- `IInteractable`: `Interact(Interactor)`, `GetInteractPrompt()`, `CanInteract(Interactor)`.
  Anything the player presses E on implements it: `ADoorActor`, `APickupActor`, `ASafehouse`,
  `AChallengeStart`, `AChapterEndInteractable`, `AInteriorPortal`. `AChapterEndInteractable::RequiredObjective` holds
  an examine back until an objective is done.
- `UInteractionComponent`: on the player. Sphere-sweeps from the camera every 0.1 s out
  to `InteractRange` (250), tracks the focused interactable, pushes its prompt to the HUD,
  `TryInteract()` is bound to IA_Interact. Falls back to a takedown prompt when the
  takedown component has a valid target.
- `APickupActor`: `EPickupType {Bow, Keycard, Arrows}`. Bow gives the player a bow; Keycard
  adds `KeycardId` to the keycard ring; Arrows adds `ArrowCount` of an arrow type to the
  quiver up to its cap. Optional `CompletesObjectiveId`. Thugs drop them on death.
- `ADoorActor`: frame and door meshes, swing or slide, `bLocked`, `RequiredKeycardId`, lerps
  open over `OpenSeconds` in Tick, completes `CompletesObjectiveId` the first time it opens; `OpensOnObjective` opens it
  by itself when that objective completes (the grotto's workshop door).
- The district (data, not actors): `Tools/Editor/generate_city.py` writes the buildings, roads
  and props into the map and the rest into data assets. `UCityLedgeData`
  (`DA_EastVillage_CityProps`) holds every roof-edge ledge, grapple anchor, fire-escape landing and
  clutter group. `ACityLedgeSpawner` (one in the map) spawns them at BeginPlay, anchors first and
  ledges nearest first within a frame budget, so the map does not carry thousands of saved actors.
  `AFireEscapeLanding` is one landing's collision; `AGrappleAnchor` is what an arrow bites.
  `UCityMapData` (`DA_EastVillage_Map`) is the flat map the world map draws.
- December dressing (one hero block, `blk_R3363343`: Avenue B to C, E 9th to 10th), no C++ of its own. Three layers:
  `Tools/Editor/_dressing.py` plans records from the district data under the traversal constraints (anchor lines,
  keep-outs, fire escapes, ledges, props, street clearance; pure, tested in `test_dressing.py`);
  `write_dressing_plan.py` commits the plan as `Tools/Data/dressing/<block>.json` (the test fails when it is stale);
  `_dressing_kit.py` (pure) turns records into instance groups, one per mesh and material (wires, four bulb colours,
  wreaths and bows, three window warmths, candles and flames, roof/parapet/kerb/car snow, tree stand, trees, steam
  stacks, awnings, signs); `_dressing_build.py` (editor) builds the kit meshes and materials under
  `/Game/City/EastVillage/Dressing` and `M_DressGlow`, and generate_city appends the groups to
  `DA_EastVillage_CityProps` as `Dress_*` clutter groups, so `ACityLedgeSpawner` draws the block as one hierarchical
  instanced mesh per group (no collision on the navmesh; collision only on banks, the stand, trees and stacks). The
  bulbs and windows are emissive, not lights; a handful of `City_DressLight_` point lights (no shadows) give colour
  on the street. `City_DressShot_` cameras are what `Hawkeye.Screenshot.Dressing` looks through. The dressing is
  not touched by `UTimeOfDaySubsystem`: it glows by day too. Details, schema and tunables: `december-dressing.md`.
- Interiors: `AInteriorEntrance` and `AInteriorExit` (both `AInteriorPortal`) travel between the
  district and an `L_Int_*` map through `UHawkeyeSaveSubsystem`. `Tools/Editor/generate_interior.py`
  builds the maps from layouts in `Tools/Interiors/`. Rules: "Interiors".
- `ASafehouse` and `USafehouseSubsystem`: heal, discover, autosave, open the safehouse menu;
  fast travel between discovered ones. Rules: "Safehouses and fast travel".
- `UTimeOfDaySubsystem`: lights the district for night or day from the setting, without a reload.
  `AHawkeyeAmbience` is the wind and street bed. `UHawkeyeResidentAssets` (game instance) keeps the
  heavy shared content loaded across level travel. `FHawkeyeFrameWatch` and `HawkeyeCapture`
  measure frames and write screenshots for the test passes.
- `UHawkeyeNavigationLibrary`: navmesh helpers for the headless scripts.

### Thugs (`Source/Hawkeye/World/Thug*`)
The controller senses and acts; the StateTree `ST_Thug` decides. Nothing in C++ picks what a thug does next.
Rules: "Melee and thugs", "Thug variety and squad alert", "Guard AI states", "Interiors: enemies and the AI indoors".
- `AThugCharacter`: the body (a crew's look: the tracksuit's colours by material, and `Headwear`, a static mesh on the
  head bone fitted upright in his own frame: chapter 2's antlers). `EThugWeapon` (Fists, Bat, Shield, Pistol, Bow), `EThugAlertState` with
  `OnAlertStateChanged`, health, stagger and knockdown, the telegraph glint, `ITakedownable`,
  `bRefusesTakedown` (a chase's thief before the corner).
- `AThugAIController`, the senses: perception (sight 1500 / lose 1800 / half angle 35, hearing 1200 through
  `HawkeyeThugHearing`, so walls and closed doors stop it), the archer's own sight, smoke, being aimed at, the
  squad alert, and the clocks that move Calm, Suspicious and Alerted, on a 0.25 s think (`Think`; tests call it).
  `ReportStimulus` is the one entry point for perception and tests. `Senses(EThugSense)` answers the tree's
  questions, read when asked so a transition sees what a task did earlier in the same tick.
- `AThugAIController`, the hands: public actuators the tasks call (`RequestMoveToActor`, `StartBurst`,
  `AdvanceBurst`, `StartSwing`, `BeginCover`, `StartArcherDraw`, `FindArcherRoof`, token request and release).
  Clocks that belong to a weapon stay with it: the burst, the archer's hold and loose window, cooldowns.
- `ST_Thug` (`/Game/Blueprints/AI/Thug`, written by `UHawkeyeThugTreeBuilder` from `Tools/Editor/create_enemies.py`,
  run every frame by `UHawkeyeThugTreeComponent`): one condition type, `FHawkeyeThugSenseCondition`, and tasks
  that each do one thing and report Running, Succeeded or Failed (`ThugStateTreeNodes`, `ThugGunnerTasks`,
  `ThugArcherTasks`, `ThugMeleeTasks`). A top-level state leaves for the root when a higher one is wanted or
  its own reason is gone; the fight's steps chain on completion. Where the thug is in a fight (swinging,
  backing off, peeking, standing off) is the active state and its task's instance data, not a flag on the
  controller. A pawn that names no tree (spawned from C++, every test) runs the controller's
  `DefaultStateTree`, the same asset. Cover and roof points come from EQS (`UHawkeyeEnvQueryContext_ThugTarget`).

```
Thug                    children tried in order: [condition]  -> transition
├─ Stunned              [Stunned]
├─ Reposition           [Engaged]
│  ├─ BackAway          [Gunner, Crowded]             done -> Thug
│  └─ ArcherZip         [RoofWanted]                  done -> Thug
├─ Cover                [Engaged, Gunner, HasCover]
│  ├─ Hide                                            done -> Relocate if CoverStale, else Peek
│  ├─ Relocate                                        found -> Hide, none -> Peek
│  └─ Peek
│     ├─ StepOut                                      -> PeekBurst
│     └─ PeekBurst                                    -> Hide if CoverOwed, else again
├─ Attack               [Alerted]   aims, rush speed; leaving gives the token back
│  ├─ HoldFire          [not Engaged]
│  ├─ StandOff          [TargetDown]
│  ├─ Gunner            [Gunner]
│  │  ├─ Approach       [OutOfRange]
│  │  ├─ TakeCover      [AimedAt or CoverOwed]        found -> Cover, none -> Burst
│  │  └─ Burst                                        aimed at -> TakeCover, done -> Gunner
│  ├─ Archer            [Archer]
│  │  └─ Draw                                         loosed -> Draw
│  └─ Melee
│     ├─ BackOff        [BackOffOwed]                 done -> Melee
│     ├─ Swing          [HoldsToken]                  done -> Melee
│     └─ HoldRing                                     granted -> Melee
├─ Investigate          [Suspicious]
└─ Patrol
```
- Debugging: `hawkeye.DebugThugs 1` draws each thug's active path over his head (`Thug > Attack > Melee >
  HoldRing`); `hawkeye.DebugTokens 1` adds the token ring. For why a transition fired, open ST_Thug in the
  editor and use the StateTree debugger (in 5.8 it records through the Rewind Debugger; Ctrl+Shift+R starts a
  recording before PIE): pick the thug by his controller's name, step state changes with
  Ctrl+arrows, and read each condition's result and each task's status per frame. Breakpoints go on a state's
  enter or exit. A standalone run records with `-trace=default,StateTreeDebug` for Unreal Insights.

### Partner (`Source/Hawkeye/Partner`, `Dialogue/BanterComponent`)
Rules: "Partner and switching".
- `AHawkeyePartnerController` possesses whichever of Kate and Clint the player is not playing and
  backs up the lead: follow, cover, attack what the lead hit, go to a mark, revive. Which mode runs is
  the StateTree `ST_Partner`, built headless by `UHawkeyePartnerTreeBuilder` (`create_partner.py`):
  a state per `EHawkeyePartnerMode` in priority order, each entered on `FHawkeyePartnerNeedCondition`
  and running `FHawkeyePartnerModeTask`. The controller keeps the senses on a think timer. With no tree
  (tests) the same timer picks the mode itself with `ChooseMode`.
- `AHawkeyePlayerController::SwitchCharacter()` swaps the pawns. `UMissionDefinition::bPartnerPresent`
  and `bAllowSwitching` gate the partner and the switch per chapter; `AHawkeyeGameMode::ApplyChapterCast`
  applies them.
- `UBanterComponent` (on the player controller): picks a line from `DT_Dialogue` for the situation and
  the speaker's turn and speaks it through `UDialogueSubsystem`.

### Dialogue and phone (`Source/Hawkeye/Dialogue`, `Source/Hawkeye/Phone`)
Rules: "Narrative plumbing". Placeholder text only; the story is written with Cameron.
- `UDialogueSubsystem` (world): every spoken line, as a subtitle and its audio, in a queue so two
  never overlap, plus walk-and-talk sequences from `DT_DialogueSequences` that wait out fights. The
  rules are in `UDialogueQueue`, reactive lines (ahead of story lines, dropped when 3 s late) among them. Played
  sequences are mirrored into the campaign state.
- `UPhoneSubsystem` (world) and `UPhoneInbox` (rules): `DT_Messages` rows triggered by mission
  events arrive after a delay, show a toast, and are mirrored into the campaign state. A row with a `CallSequence` is
  a call: its sequence plays as it arrives.
  `UPhoneWidget` is the panel.
- `UHawkeyeNarrativeSettings` (developer settings) holds the table paths, because world
  subsystems have no Blueprint defaults.

### Chase (`Source/Hawkeye/Chase`)
Rules: "Chases". Built for Act 1's "Night Shift".
- `UChaseRouteDefinition` (primary data asset, `DA_Chase_CH01_NightShift`): the thief's waypoints,
  each with a leg (`EChaseLeg`: Run, Vault, Climb, JumpGap, Drop), the objectives and dialogue
  sequences placed along it, the dumpster beat, the props, and `FChaseTuning`. Planned from the
  district's OSM records by `Tools/Editor/create_chase.py` (`_chase.py`). A chapter names it on
  `UMissionDefinition::Chase`; another chapter's chase is another asset.
- `UChaseRules`: the pure rules (progress along the route, the lead and its band, the thief's speed,
  the lose clock, the catch, the beat trigger, the restore). Tests: `Hawkeye.Chase.*`.
- `UChaseSubsystem` (world, ticking): spawns the thief (an `AThugCharacter` with its thinking off),
  moves him along the route with the rubber band, follows Kate's progress, completes the route's
  objectives, plays the dumpster beat, notices the catch and the loss (the loss reloads the last
  checkpoint), and mirrors its state into `UHawkeyeCampaignState`. `AHawkeyePlayerController`
  starts it as the chapter's opening begins.

### Boss (`Source/Hawkeye/Boss`)
Rules: "Bosses". Built for Act 1's "The Social Club".
- `UBossDefinition` (primary data asset, `DA_Boss_CH04_Tomas`): the boss's body class (a thug Blueprint,
  `BP_Boss_Tomas`), health, scale, move set (`FBossMoveSet`: the combo, each swing parryable or unblockable, the
  winded window, finisher share and whether only the window opens it, his parry stagger, stagger threshold), his
  phases (`FBossPhaseDefinition`: the `FBossPhase` rule plus objective, fights or watches, crew, hazard, start and end
  lines, barks, arrow grants, and the solve: his guard's fraction, the arrow effects that break it, whether the hazard
  catches him, the transition beat `FBossBeat`), the defeat beat, the decisive finisher's camera
  (`FBossFinisherCamera`), the light that rides on him, the arena (`FBossArena`: the roof's outline, the fight's
  centre, the street stand and its anchor, his spot, the hazard's lane and mesh, props and lights) and the hazard's
  tuning. Written by `Tools/Editor/create_boss.py`, the arena planned from the district by `_arena.py`. A chapter
  names it on `UMissionDefinition::Boss`; a second boss is another asset: chapter 3's Santa (`DA_Boss_CH03_Santa`,
  `create_grotto.py`) fights in an interior, his definition's `Map`, his arena a room.
- `UBossRules` and `FBossComboClock`: the pure rules (phase walk, damageable, winded bonus, finisher share,
  checkpoint from objectives and its health, the hazard's clock and its hits, barks, intro, on the roof; since the
  second pass the finisher's opening, the guard and what breaks it, the decisive finisher, the beat's camera and
  clock, lines not yet heard, the putty pin, the props the hazard pushes, a forced winded). Tests: `Hawkeye.Boss.*`
  (`BossTest.cpp`, `BossSolveTest.cpp`), `Hawkeye.BossPhase.*`, `Hawkeye.Campaign.*`.
- `UBossFightSubsystem` (world, ticking): on the chapter's start dresses the roof and stands the boss and the first
  crew there (thugs spawned from data with their thinking off, `bSkipSave`; the boss gets his combo, finisher share,
  knockdown resistance and a `UBossPhaseComponent` added at spawn, so he needs no class of his own), completes the
  meet objective when Kate's feet are on the roof, plays the pre-fight sequence, runs the phases (crews alerted, the
  boss fighting through `ST_Thug` with his combo or watching, the winded window from his swing count, barks from
  what it sees, the hazard loose, arrows granted), completes each phase's objective as the next opens and the last
  after the after sequence, and restarts at the save's checkpoint on a load (`OnCampaignLoaded`). Every tick of a
  phase it sets his guard (`UHealthComponent::IncomingDamageScale`), looks at new trick-arrow effects (a guard break,
  a hazard pin) and asks the hazard whether it reaches him. Beats spawn an `ACameraActor` and blend the player's view
  to it (`SetViewTargetWithBlend`), hold Kate (`AHawkeyeCharacter::SetScriptedLock`) and everyone else, and wait for
  her finisher to end first; a decisive finisher on him swaps her `UFinisherComponent`'s slow motion and push-in for
  the definition's until it ends. What the fight has said is mirrored into the campaign (`BossHeard`) by
  `UHawkeyeSaveSubsystem::CaptureCampaign` and kept in the save subsystem's memory across a reload.
  `AHawkeyePlayerController` calls `BeginChapter` with the chapter's opening (Kate to the street stand).
- `ABossArenaHazard`: the arena's moving hazard (the inflatable Santa): tethered, then telegraph, sweep and rest on
  the rules' clock, a lit strip and a red light for the telegraph, hits Kate only in its lane while it sweeps; held by
  a beat, pinned by putty, pushing the props it was given along its lane, and answering `Reaches` for the boss.
- Campaign order (`Mission/CampaignOrder`, `UHawkeyeCampaignOrder`): the chain of `UMissionDefinition::NextChapter`
  from the district's starting chapter; the safehouse's chapter select lists it and starts an open chapter with
  `AHawkeyeGameMode::StartChapter`, which `-HawkeyeChapter=` and `hawkeye.StartChapter` use too.

### Crime and challenges (`Source/Hawkeye/Crime`, `Source/Hawkeye/Challenge`)
Rules: "Street crimes", "Side challenges". Both follow one shape: pure rules, a tracker with no world,
a world subsystem that applies them, data assets written by a `create_*.py` script, and counts mirrored
into the campaign state.
- Crimes: `UCrimeRules`, `UCrimeTracker`, `UCrimeSubsystem`; `UCrimeDefinition` per type
  (mugging, robbery, ambush, rooftop); `ACrimeSpot` actors placed by `generate_city.py`; `ACivilian`
  and `ACrimeLoot`. One crime at a time while she roams.
- Challenges: `UChallengeRules`, `UChallengeTracker`, `UChallengeSubsystem`; `UChallengeDefinition`
  per challenge (archery ranges, timed traversal routes); `AChallengeStart` (the pedestal, an
  `IInteractable`), `AChallengeTarget`, `AChallengeCheckpoint`.

### Save (`Source/Hawkeye/Save`, `Plugins/SPUD`)
Rules: "Save data".
- SPUD (MIT, vendored in `Plugins/SPUD`) persists the world. Kate, Clint and thugs implement
  `ISpudObject`; the part with no actor is `UHawkeyeCampaignState`, registered as SPUD's global
  object (chapter, objectives, safehouses, seen flashbacks, hints, phone, dialogue, chase, bests).
- `UHawkeyeSaveSubsystem` (game instance): one campaign slot. Decides when to save (objective
  complete, safehouse, new game, quit to menu, 60 s of calm roaming through `FHawkeyeAutosaveClock`),
  whether a save may be loaded (version, stale chapter), and carries the player between the district
  and a playable scene or interior (`FHawkeyeSceneReturn`). Every travel that is not a SPUD load starts
  SPUD afresh, so only explicit saves persist.
- `AHawkeyeGameMode` runs the autosave timer and the death flow; its objective save runs before
  `OnMissionComplete`, which clears the quiver for the end card.

### Settings and difficulty (`Source/Hawkeye/Settings`)
- `UHawkeyeSettingsSubsystem` (game instance): owns `FHawkeyeSettings` (sensitivity, audio, difficulty,
  accessibility, aim assist, sprint mode, time of day, seen flashbacks), persists it to the
  `HawkeyeSettings` save slot on every change, broadcasts `OnSettingsChanged`. The character
  subscribes so changes apply live.
- `UDifficultySubsystem`: one setting and one table of what it changes (`EDifficultyStat`), so tuning
  lives in one place. Code asks `GetScalarFor(this, Stat)`. Story, Normal, Hard.
- `UHawkeyeAccessibility`: palettes, subtitle sizes, shake and flash scales, hold versus toggle. Pure.

### Audio, effects and playtest (`Source/Hawkeye/Audio`, `Vfx`, `Playtest`)
- `UHawkeyeAudioSubsystem` and `UHawkeyeVfxSubsystem` are where every sound and every particle effect
  is played from. Components hold soft pointers; the subsystems load on first use and log each request
  so tests assert that the call was made. Sounds are MetaSounds built by `create_audio.py`; systems are
  Niagara built by `create_vfx.py` through `UHawkeyeVfxBuilder`. `USnowfallComponent` (on the player
  controller) keeps the snow round the camera.
- `UHawkeyeMusicSubsystem` plays one layered score and fades its layers from `FHawkeyeMusicDirector`,
  which reads the thugs, the pawn, the crime and the challenge subsystems. `UHawkeyeVolumeSubsystem`
  keeps the settings mix loaded.
- `UPlaytestSubsystem` and `UPlaytestPhotoMode`: F12 notes with a screenshot and log tail, photo mode,
  and a per-session summary in `Saved/Playtest/`. Nothing else knows they exist. Rules: `testing.md`.

### Inventory (`Source/Hawkeye/Player`)
- `UInventoryComponent` on the player: the bow, a seven-slot quiver (standard arrows always in slot 1,
  present even at zero; the grapple arrow has no count), the keycard ring, `ActiveArrowSlot`; select by
  number, next and previous (empty slots skipped), `AddArrows`, `ConsumeArrow`, `GiveBow`. Fires
  `OnInventoryChanged` and `OnActiveArrowSlotChanged`. A character with a quiver of its own (Clint)
  sets `bUseOwnStartingQuiver`.
- Mission lifecycle hooks: the game mode applies the chapter's `StartingBow` and `StartingArrows` on
  `OnMissionStarted`, and the inventory goes back to that grant on `OnMissionComplete` (before the end
  card) and on restart.
- With no bow, the fire press is `Hands`, the melee fallback on the owner's `UWeaponComponent`.

### UI (`Source/Hawkeye/UI`)
Widgets build their own layout in `RebuildWidget` when the Blueprint has none, and dim the game with an
`FSlateColorBrush` (a `UBorder` default brush has no resource, so tinting it draws nothing). Layout lives
in `WBP_*`; behaviour in the C++ parent.
- `UHawkeyeHudWidget`: objective title, interaction prompt, reticle, and the HUD's overlay children:
  `UHawkeyeHotbarWidget` (seven quiver boxes), `UHawkeyeQuiverWheelWidget` (radial, opened by
  `AHawkeyeCharacter`), `UHawkeyeObjectiveWidget` (marker, compass, toasts), the grapple marker, the
  thug overhead glyphs (`UHawkeyeThugOverheadWidget`), the challenge and crime panels, and the first-time
  hint line (`UHawkeyeHintSubsystem`, `UHawkeyeHintWidget`). Subscribes to the mission subsystem, the
  pawn's bow, weapon and takedown components. Hidden during flashbacks by the controller. Its ammo line
  still binds `UWeaponComponent` and stays blank for Kate, who has no ammo count.
- Menus and cards, all driven by `AHawkeyePlayerController`: `UHawkeyeMainMenuWidget`,
  `UHawkeyeDifficultyPromptWidget` (once at New Game), `UHawkeyePauseWidget`, `UHawkeyeSettingsWidget`,
  `UHawkeyeInventoryWidget` (Tab, pauses), `UHawkeyeSafehouseWidget`, `UHawkeyeMapWidget` (the world
  map, drawn from `UCityMapData`), `UPhoneWidget`, `UChapterTitleWidget`, `UMissionEndCardWidget`,
  `UChallengeResultsWidget`, `UFlashbackReplayWidget`.
- The arithmetic is in pure classes with tests: `UObjectiveMarkerMath`,
  `UQuiverWheelMath`, the map's math (`HawkeyeMapMath.h`), `FHawkeyeTitleCardClock`, `UHawkeyeHintRules`.

### Player and framework
- `AHawkeyeCharacter`: third person. A spring arm (`CameraBoom`) behind and above the body, probing with a
  sphere and pulled in against walls, with `FollowCamera` on its end; aim blends the arm to the right
  shoulder, a fight lengthens it, an interior map caps it (`IndoorArmLength`). The body turns to face the
  way it moves, or the camera while aiming. Each frame it picks a gait (`EHawkeyeGait`: walk, run, sprint,
  crouch, slide) from the input and speeds are properties. Landings are measured from the top of the arc
  (`EHawkeyeLanding`: dip, roll, stumble; a very long fall costs health, never all of it). Dodge, parry and
  stagger live here; downing and the partner's revive too (`EnterDowned`, `ReviveFromDown`).
  It owns: `UHealthComponent`, `UTakedownComponent`, `UInteractionComponent`, `UWeaponComponent` (Hands),
  `UBowComponent`, `UMeleeComponent`, `UFinisherComponent`, `UInventoryComponent`, `UGrappleComponent`,
  `UParkourComponent`, and a `UPawnNoiseEmitterComponent` (0.4 loudness walking, 1.0 sprinting, 0
  crouched or still, every 0.5 s). It implements `ISpudObject` for the save. Enhanced Input actions are
  assigned in `BP_HawkeyeCharacter`; tuning lives on `BP_Kate`, the playable child.
- Body animation: `BP_Kate` derives from the Game Animation Sample's `SandboxCharacter_CMC`, whose
  motion-matching AnimBP owns the body. The character writes its gait, crouch and aim into the sample's
  `CharacterInputState` every update (`SyncGaspInputState`). `HawkeyeLocomotion` (`LocomotionAnim`) is the
  idle and walk clip switch for a mesh with no AnimBP (thugs). `FHawkeyeBodyLook` (`BodyLook`) draws a
  different skeletal mesh over the driver mesh (Kate: Paragon's Sparrow over the UEFN mannequin) through a
  Retarget Pose From Mesh AnimBP. Setup: `animation.md`.
- `UHawkeyeMovementComponent` (`Player/HawkeyeMovementComponent`): the character's
  CharacterMovementComponent (`AHawkeyeCharacter` swaps it in with `SetDefaultSubobjectClass`, so
  BP_Kate and BP_Clint keep their tuned values). It adds custom movement modes
  (`EHawkeyeCustomMovement`: `Zip` and `Traversal`); `PhysCustom` runs the zip (`PhysZip`): hop, line, speed and
  acceleration, the chain's turn, the swept move, a truthful `Velocity`, and the velocity she leaves
  with (arrival carry, let-go, blocked). `UGrappleComponent` keeps the rules: targeting, arrows, which
  actors the capsule ignores as start and anchor supports, audio and effects, the delegates. It
  starts a zip with an `FHawkeyeZipLine`, lets go with `ReleaseZip`, reads progress, direction and
  speed back, and answers the movement component's three callbacks: a hit (drop, retry, or
  straighten a chain's swing), each sub-step (release the start supports), and the end (arrived,
  blocked, released, interrupted). Anchors are bucketed into a grid so the pick only looks at nearby cells.
  The second mode is the traversal (`PhysTraversal`, since 2026-11-10; moves and hangs were
  `MOVE_Flying` with the capsule placed by hand). `UParkourComponent` keeps the rules: the probe, which
  move (`EHawkeyeParkourMove`), the roof-edge guard, the hang, shimmy and corners, the log, the buffered press, which clip and
  the warp targets (`FrontLedge`, `BackLedge`, `BackFloor` on the `MotionWarping` component). It hands
  the movement one `FHawkeyeTraversalMove` with `StartTraversal`: path keys (capsule centre and yaw at
  even steps over the move's time) or none, when the montage's root motion moves her; the actors the move
  passes through (ignored only until it ends); and the exit (hold, Walk or Fall, and the exit velocity
  worked out from her entry speed). The movement sub-steps it, sweeps, slides along anything else, sets a
  truthful `Velocity`, and at the end holds still (a hang) or leaves. `MoveTraversalHold` moves the held
  point (a shimmy), `ReleaseTraversal` lets go (a drop, the hop off), and `OnTraversalEnded` reports
  held, exited, released or interrupted, which is how the parkour component enters the hang, finishes a
  move, or drops its state when something else changed the mode. The parkour component ticks first
  (a tick prerequisite), so a frame's rules are in before the movement runs them. The sample's own
  traversal (`AC_TraversalLogic`, called by reflection through `HawkeyeGaspTraversal`) still runs in
  Flying; `EHawkeyeParkourRoute` records which route a move took. Rules: "Movement", "Traversal".
- `AHawkeyePlayerController`: the glue for UI and flow. Creates `HudWidgetClass` on BeginPlay and the
  other widgets on demand, subscribes to the mission subsystem, owns the `UMissionFlowController` and
  performs its steps (close-up camera, end card, flashback, playable scene, travel), shows the chapter
  title and plays the opening dialogue as a chapter starts (and starts its chase), opens the phone
  (P), map (M), pause and inventory, handles the safehouse menu and fast travel, switches characters, and
  owns the banter, snowfall and photo-mode components.
- `AHawkeyeGameMode`: default pawn and controller classes, `StartingMission` which it starts at BeginPlay,
  the death flow (`ReloadLastCheckpoint` fades and loads the last save; `RestartMission` is the fallback),
  the autosave timer and its gate (`CanAutosaveNow`, `IsWorldInCombat`), `ApplyChapterCast`, and per-map
  switches (`bOutdoorWeather`, `bInteriorCamera`, `bBuildNavigationAtStart`). It also builds the navmesh
  at start, because the generated maps ship with none.

### Traversal debugger (`Source/Hawkeye/Traversal`, `Source/HawkeyeEditor`)
The second module, `HawkeyeEditor` (Editor, Default loading phase), and the runtime code it stands on. Full write-up:
`tools-traversal-debugger.md`.
- `FGrappleReachQuery` (runtime, pure): the grapple's reach rules with no world: range, cone, screen, the predicted
  lens, the zip's launch and release point, a verdict per anchor from a standing point (`EGrappleReachVerdict`), roof
  spots, neighbours, chains, sidewalk sampling, street walls (the generator's street-facing rule), roofs the street
  cannot reach. The world comes in through
  `FGrappleReachOracle`. `GrappleReachDefaults` holds the grapple's numbers; `UGrappleComponent`'s defaults come from
  it and the component calls the query for the shared math (`ChoosePick`, `IsOnScreen`, the release point).
- `FGrappleReachWorld` (runtime): a profile from a pawn, a scene either from `DA_EastVillage_CityProps`, the map data and
  the `CityBuilding` actors (editor world) or from the actors a game world spawned, and an oracle with the component's
  traces. `FlushPendingPhysicsState` gives an unticked world (a commandlet's) its collision before any trace.
- `FGrappleAudit` (runtime): the grapple audit over a scene and an oracle, with an optional picker hook; the report,
  `grapple_audit.json` and the gate. `Hawkeye.Grapple.Audit` is this with Kate's own traces and picker plugged in; the
  commandlet's run of it on the district matches the test's to the anchor.
- `UTraversalDebuggerSubsystem`, `FTraversalDebuggerViewModel` and `STraversalDebuggerPanel` (editor): the Tools > Hawkeye >
  Traversal Debugger tab. The subsystem holds the state and draws into the editor world's persistent line batcher;
  the view model turns it into rows and text with no Slate (tested headless); the panel lays them out.
- `UHawkeyeTraversalAuditCommandlet` (editor): `-run=HawkeyeTraversalAudit -map=...` (`Tools\traversal-audit.ps1`), the
  audit headless; exits 1 when a street-facing roof is unreachable or the chain breaks, 2 when it cannot run. Both
  editor surfaces go through `FTraversalAuditRunner`.
- The boundary: `HawkeyeEditor` depends on `Hawkeye`, never the other way. It is typed `Editor` in the .uproject and
  named only in `HawkeyeEditor.Target.cs`, so the game target never builds it. Anything with rules goes in
  `Source/Hawkeye/Traversal`, where the game and the tests can use it; the editor module is UI, editor-world plumbing
  and the command line.

### Tools (`Tools/`)
Everything under `Content/` comes from scripts. Commands: `infrastructure.md`.
- `Tools/Editor/create_all.py` runs the `create_*.py` and `import_*.py` scripts in dependency order
  (input, audio, vfx, GASP import, blueprints, world blueprints, weapon data, chase, boss, site, missions, partner,
  enemies, narrative, crimes, challenges, combat clips, bow IK, Kate's look). Each is idempotent: it
  checks before creating and prints one line per asset. `verify_*.py` are read-only checks.
- `generate_city.py`: the district from OSM records (`parse_osm.py` makes them; `Tools/fetch-osm.ps1`
  downloads). It is the one script that touches `unreal`; the placement rules are plain Python in
  modules it re-exports so they run and test under CPython: `_geo` and `_geomutil` (projection and 2D
  geometry), `_district` (the records, shared dimensions, street layout, facade style, lamps),
  `_reach` (the grapple's reach model and anchor placement, matched to the traversal audit's rules and sidewalk
  spots; it also drops a fire escape that would leave the street no anchor), `_ledges` (one slab per roof edge),
  `_fire_escapes`. `_chase.py` plans the chase from the same records with the same reach model; `_arena.py` picks
  and lays out the boss's roof the same way (`create_boss.py`); `_site.py` picks chapter 2's spot on Avenue B and lays out
  its toy drive and the courier (`create_site.py`); `_grotto.py` picks chapter 3's grotto (a street door on a
  building nobody else uses, its roof point) and lays out its frontage and, from `Tools/Interiors/Grotto.json`, its
  inside (`create_grotto.py`).
  Python tests: `test_geo.py`, `test_city_geometry.py`, `test_city_golden.py`, `test_interior.py`,
  `test_parse_osm.py`, `test_arena.py`, `test_site.py`, `test_grotto.py`. `verify_city.py` checks the generated map
  against the records.
- Interiors: `generate_interior.py` and `_interior.py` from `Tools/Interiors/*.json`.

## Boundaries
- C++ never references a specific mission, boss, or level by name. If you find
  `"CH01"` in a .cpp file, that's a bug.
- Blueprints never implement rules that a test should cover. Health math, objective
  ordering, phase thresholds, arrow counts: C++.
- Widgets: layout in `WBP_*`, behaviour in the C++ parent. A widget Blueprint with more
  than a screenful of graph nodes is a sign the parent needs a function.
- AI: decisions are StateTrees (`Content/Blueprints/AI`), written by C++ builders so the script is the one
  description of each tree. C++ provides senses, actuators, conditions and tasks; a choice of what to do next
  in a controller is a bug. The partner follows the same rule.
- Rules are pure where they can be: a `*Rules` class or namespace with no world, a tracker or clock that
  holds state, a subsystem that applies them. Tests call the first two directly.

## Lifecycle of a mission, end to end
1. The map's game mode (`BP_GameMode_EastVillage`) names a `StartingMission`; `AHawkeyeGameMode::BeginPlay`
   calls `MissionSubsystem->StartMission(DA_CH01_NightShift)`. A save load starts it the same way, then
   `ApplyPendingRestore` puts the objectives and actors back.
2. Subsystem duplicates objectives, marks the first as current, fires `OnMissionStarted`. The game mode
   applies the chapter's cast and grants its bow and arrows; a boss chapter's roof is set up. The player
   controller shows the chapter title (once per campaign), plays the opening dialogue, starts the chase if the
   mission names one, and puts Kate on the boss arena's street stand if it names a boss. Another chapter starts
   the same way from chapter select (`AHawkeyeGameMode::StartChapter`).
3. Player plays. World actors call `CompleteObjective` by id. Order is not enforced unless
   `UMissionDefinition::bEnforceOrder` is on. Each completed objective autosaves.
4. When every non-optional objective is complete: `OnMissionComplete` (the autosave has already run). If
   `FlashbackToPlay` is set: `OnFlashbackRequested`.
5. The controller starts `UMissionFlowController`: a close-up if one is running, the end card, the flashback
   (paused world, `WBP_Flashback`), the flashback's playable scene if it has one (the district is saved first
   and the player comes back to `ReturnPointLabel`), then the next level, or back to roaming with a toast
   (`bReturnToRoamingAtEnd`), or the final card and the menu.

## Things deliberately not built
No branching chapters and no dialogue choices (lines are queued from data tables). No multiplayer,
replication, or networking of any kind; don't add `Replicated` specifiers. No Gameplay Ability System:
trick arrows are small actors. No World Partition yet; the district is one map.
