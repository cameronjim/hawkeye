# Architecture

One game module, `Hawkeye`, a handful of subsystems that talk through delegates. Nothing
holds a hard pointer across subsystem boundaries; everything that crosses one is either
a data asset reference or an event.

## Pivot status (2026-09-25)
The project pivoted from a first-person Punisher game to a third-person Hawkeye game.
This document describes the code as it is, plus what changes. Sections below are marked:
- **keep**: unchanged by the pivot.
- **rename**: same code, new names (guard becomes thug, weapon becomes arrow or bow).
- **remove**: deleted in the stage 2 cleanup step. Don't build on it.
- **planned**: not written yet; the shape it will take.

| Area | Status |
|------|--------|
| Mission, objectives, tracker, subsystem, trigger volume | keep |
| Flashback definition, sequencer, widget | keep, plus a "playable scene" hook (planned) |
| Health, boss phases, takedown | keep |
| Weapon component and definitions | rename to bow and arrow definitions; hitscan path replaced by projectiles (planned) |
| Inventory and hotbar | rename to quiver; radial UI (planned) |
| Interaction, pickups, doors | keep |
| Guard character and AI controller | rename to thug; add melee rush, gunner, archer, heavy variants (planned) |
| First-person arms component, viewmodel, body-mesh head hiding | remove |
| Third-person camera, spring arm, aim offset | planned |
| Locomotion via Game Animation Sample, parkour, grapple | planned |
| Partner AI and character switching | built: `AHawkeyePartnerController` on a headless-built StateTree, `SwitchCharacter()` on the controller, `UBanterComponent` |
| Save, checkpoints, safehouse, main menu | built on SPUD: `Plugins/SPUD`, campaign state object, `ASafehouse`, menu widgets |
| Trick arrows, quiver wheel | built: `Combat/ArrowEffects/*`, radial widget |
| Objective markers, compass, beacons | built: `UHawkeyeObjectiveWidget`, `UObjectiveMarkerMath` |
| City generator from OSM, props data, load-time spawner | built: `Tools/Editor/generate_city.py`, `UCityLedgeData`, `ACityLedgeSpawner`; World Partition still off |
| Settings, pause, end card, HUD, inventory widgets | keep, restyle later |
| Materials and room-art scripts | keep; extend to facades and streets |
| Pipeline, tests, verify, screenshots | keep |

## The systems

```
Player input
    │
    ▼
AHawkeyeCharacter ──► UWeaponComponent ──► ApplyPointDamage ──► UHealthComponent (on target)
    │                                                               │
    ├──► UTakedownComponent ──► ITakedownable::OnTakedown           ├──► OnHealthChanged ──► UBossPhaseComponent ──► OnPhaseChanged
    │                                                               │                                                  │
    │                                                               └──► OnDeath                                       └──► BP arena controller, behavior tree
    │
    ▼
World: AObjectiveTriggerVolume, doors, pickups
    │
    ▼
UMissionSubsystem (world subsystem)
    │  StartMission(UMissionDefinition*)
    │  CompleteObjective(FName)
    ├──► OnObjectiveUpdated ──► WBP_Hud
    ├──► OnMissionComplete ──► end card, save
    └──► OnFlashbackRequested(UFlashbackDefinition*)
              │
              ▼
       AHawkeyePlayerController ──► creates UFlashbackWidget ──► Play()
                                          │
                                          └──► OnFlashbackFinished ──► open NextLevel
```

### Mission (`Source/Hawkeye/Mission`)
- `UMissionDefinition` (primary data asset): the mission's identity, its ordered
  objectives (instanced `UMissionObjective` objects), the flashback to play after, the
  next level. One asset per mission in `Content/Missions`.
- `UMissionObjective` (instanced UObject): title, description, optional flag, completed
  flag, an `ObjectiveId` FName used by the world to complete it. Blueprintable so a mission
  can subclass it for special objectives, but most are the base class with data.
- `UMissionSubsystem` (world subsystem): the runtime. Duplicates the definition's
  objectives on `StartMission` so the asset never gets dirtied. Owns "which objective is
  current" and "is the mission done". Fires the delegates above. Lives per world, so a
  level transition resets it; the save system (stage 3) restores state on load.
- `AObjectiveTriggerVolume`: the common way levels complete objectives. Overlap by the
  player pawn, calls `CompleteObjective(ObjectiveId)`. Doors, pickups, and scripted beats
  do the same call from Blueprint.

### Flashback (`Source/Hawkeye/Flashback`)
- `UFlashbackDefinition` (primary data asset): ordered `FFlashbackSlide` array (texture,
  caption, hold seconds, crossfade seconds, voice line), ambient loop, skippable flag.
  Stage 3 adds `ReplacedByImage` and `GlitchSeconds` per slide for the lie-breaking effect.
- `UFlashbackWidget` (UUserWidget): two stacked `UImage`s crossfaded by opacity, a caption
  block, timing driven from `NativeTick` because the game is paused during playback and
  paused worlds don't run timers. Audio is created as UI sound so it survives the pause.
  Fires `OnFlashbackFinished`. Designers subclass it as `WBP_Flashback` to lay out the
  visuals; the logic stays in C++.

### Combat (`Source/Hawkeye/Combat`)
- `UHealthComponent`: `MaxHealth`, `CurrentHealth`, `bInvulnerable`. Hooks the owner's
  `OnTakeAnyDamage`. Fires `OnHealthChanged(Old, New, Instigator)` and `OnDeath`. Every
  damageable thing has one: player, guards, bosses, shootable lights.
- `UBossPhaseComponent`: requires a `UHealthComponent` on the same actor. An ordered
  `FBossPhase` array with health-percent thresholds. Listens to `OnHealthChanged`, advances
  the phase index when health drops below the next threshold, optionally sets
  `bInvulnerable` for `TransitionSeconds`, fires `OnPhaseChanged(Old, New, Phase)`. It does
  not know what a phase means; the boss Blueprint and behavior tree react.
- `UWeaponComponent`: hitscan weapon. Magazine, reserve, damage, range, fire rate, reload
  time. `Fire()` line-traces from the owning pawn's view and applies point damage.
  `Reload()` is a timer. Fires `OnAmmoChanged`. Stage 3 moves stats to a `UWeaponDefinition`
  data asset so a shotgun is data, not a subclass.
- `UTakedownComponent`: on the player. `TryTakedown()` sphere-traces for actors tagged
  `Guard`, checks the player is behind the target within `MaxAngleDegrees`, calls
  `ITakedownable::OnTakedown` on it. Fires `OnTakedownPerformed`.
- `ITakedownable`: one BlueprintNativeEvent, `OnTakedown(AActor* Attacker)`. Guards
  implement it in Blueprint (ragdoll, drop pickup, notify AI).
- Attack tokens (`UCombatTokenSubsystem`, a `UWorldSubsystem`; rules in `UHawkeyeCombatTokenRules`): per target,
  a melee and a ranged budget of who may be attacking her now. Attackers call `RequestToken` from their own
  think (asking and renewing are the same call, so nothing here ticks or sweeps actors) and `ReleaseToken` when
  the attack ends or anything stops it; budgets hold attackers by weak pointer and let a silent holder's token
  lapse. `GetBudgetView` is the read-only view (`hawkeye.DebugTokens`, the HUD). Thugs ask from the fight's tasks
  (`HoldRing`, `Swing`, `Burst`, `Draw` in ST_Thug, below) and give the token back when the swing, burst or draw
  ends or the fight is left. Written from scratch: the engine has no shared attacker budget, and
  Smart Object slots claim places, not turns. Budgets, gap and the off-screen telegraph
  are rows in the difficulty table.

### World (`Source/Hawkeye/World`)
- `IInteractable`: `Interact(Interactor)`, `GetInteractPrompt()`, `CanInteract(Interactor)`.
  Anything the player presses E on implements it.
- `UInteractionComponent`: on the player. Sphere-sweeps from the camera every 0.1 s out
  to `InteractRange` (250), tracks the focused interactable, pushes its prompt to the HUD,
  `TryInteract()` is bound to IA_Interact. Falls back to a takedown prompt when the
  takedown component has a valid target.
- `APickupActor`: `EPickupType {Weapon, Keycard, Ammo}`. Weapon calls `GiveWeapon()` on
  the player's weapon component and sets ammo; Keycard adds `KeycardId` to the player's
  keycard set; Ammo adds reserve. Optional `CompletesObjectiveId`. Destroys itself.
- `ADoorActor`: frame and door meshes, `bLocked`, `RequiredKeycardId`, lerps open over
  `OpenSeconds` in Tick, completes `CompletesObjectiveId` the first time it opens.

### Thugs (`Source/Hawkeye/World/Thug*`)
The controller senses and acts; the StateTree `ST_Thug` decides. Nothing in C++ picks what a thug does next.
- `AThugCharacter`: the body. `EThugWeapon` (Fists, Bat, Shield, Pistol, Bow), `EThugAlertState` with
  `OnAlertStateChanged`, health, stagger and knockdown, the telegraph glint, `ITakedownable`.
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
  `DefaultStateTree`, the same asset.

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

### Inventory and weapons (`Source/Hawkeye/Player`, `Source/Hawkeye/Combat`)
- `UWeaponDefinition` (primary data asset, type `Weapon`): every stat for one weapon, its
  hotbar slot, melee flags, viewmodel mesh, arms pose name, and hand offset. Assets:
  `DA_Weapon_Hands`, `DA_Weapon_Pistol`, `DA_Weapon_Rifle` in `Content/Blueprints/Weapons`.
- `UInventoryComponent` on the player: three slots keyed by `EHotbarSlot`, each holding a
  definition plus magazine and reserve; the keycard set; `ActiveSlot`; select/next/previous
  with a swap lockout; `AddWeapon`, `AddAmmo`, `Clear`, `ApplyStartingWeapons`. Fires
  `OnInventoryChanged` and `OnActiveSlotChanged`. Hands are always present.
- `UWeaponComponent` reads its stats from the active definition and writes ammo through to
  the inventory slot on every change. Melee definitions do a short sphere sweep and call
  `UHealthComponent::ApplyMeleeDamage`, which can stagger.
- Mission lifecycle hooks: starting weapons applied on `OnMissionStarted`, inventory cleared
  on `OnMissionComplete` (before the end card) and on restart.

### Settings (`Source/Hawkeye/Settings`)
- `UHawkeyeSettingsSubsystem` (game instance): owns `FHawkeyeSettings`, persists it to the
  `HawkeyeSettings` save slot on every change, broadcasts `OnSettingsChanged`. The character
  subscribes so sensitivity changes apply live.

### UI (`Source/Hawkeye/UI`)
- `UHawkeyeHotbarWidget`: three slot boxes at bottom centre, inside the HUD overlay.
- `UHawkeyeInventoryWidget`: Tab. Read-only list of weapons, keycards, spare ammo. Pauses.
- `UHawkeyePauseWidget`, `UHawkeyeSettingsWidget`, `UMissionEndCardWidget`: pause menu with
  Resume, Settings, Restart, Quit; settings with the sensitivity slider; mission end card.
  All build their own layout when the Blueprint has none, and dim the game with an
  `FSlateColorBrush` (a `UBorder` default brush has no resource, so tinting it draws nothing).
- `UHawkeyeHudWidget`: objective title, ammo (`12 / 24`, blank when unarmed), interaction
  prompt, crosshair. Builds its own layout in `RebuildWidget` when the Blueprint has none.
  Subscribes to the mission subsystem, the pawn's weapon, and the takedown component.
  Hidden during flashbacks by the controller. Honours `UMissionDefinition::bShowObjectiveText`.

### Player and framework
- `AHawkeyeCharacter`: first-person camera on the capsule, health, takedown, interaction,
  weapon (starts with `bHasWeapon` false), noise emitter (1.0 sprinting, 0.4 walking, 0
  crouched, every 0.5 s; 3.0 on fire), keycard set, `LookSensitivity`. Viewmodel:
  `ArmsMesh` (the mannequin with legs and head hidden, attached to the camera, owner-only)
  and `WeaponMesh` (template pistol) with procedural recoil, reload dip, aim lerp, sway,
  and a muzzle light. Enhanced Input action properties and all viewmodel assets are
  assigned in `BP_HawkeyeCharacter`. Movement tuning lives on the Blueprint's
  CharacterMovement component, not in C++.
- `UHawkeyeMovementComponent` (`Player/HawkeyeMovementComponent`): the character's
  CharacterMovementComponent (`AHawkeyeCharacter` swaps it in with `SetDefaultSubobjectClass`, so
  BP_Kate and BP_Clint keep their tuned values). It adds custom movement modes
  (`EHawkeyeCustomMovement`); `PhysCustom` runs the zip (`PhysZip`): hop, line, speed and
  acceleration, the chain's turn, the swept move, a truthful `Velocity`, and the velocity she leaves
  with (arrival carry, let-go, blocked). `UGrappleComponent` keeps the rules: targeting, arrows, which
  actors the capsule ignores as start and anchor supports, audio and effects, the delegates. It
  starts a zip with an `FHawkeyeZipLine`, lets go with `ReleaseZip`, reads progress, direction and
  speed back, and answers the movement component's three callbacks: a hit (drop, retry, or
  straighten a chain's swing), each sub-step (release the start supports), and the end (arrived,
  blocked, released, interrupted).
  The second mode is the traversal (`PhysTraversal`, since 2026-11-10; moves and hangs were
  `MOVE_Flying` with the capsule placed by hand). `UParkourComponent` keeps the rules: the probe, which
  move, the roof-edge guard, the hang, shimmy and corners, the log, the buffered press, which clip and
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
  traversal (`AC_TraversalLogic`) still runs in Flying.
- `AHawkeyePlayerController`: the glue for UI. Creates `HudWidgetClass` on BeginPlay,
  subscribes to the mission subsystem, owns `FlashbackWidgetClass`, hides the HUD during a
  flashback, opens the next level when it finishes. Stage 3 adds end cards, subtitles,
  and the pause menu here.
- `AHawkeyeGameMode`: default pawn and controller classes, `StartingMission` which it
  starts at BeginPlay, `RestartMission()` which reopens the level 2 s after player death.
  Stage 3 replaces the restart with checkpoints.

## Boundaries
- C++ never references a specific mission, boss, or level by name. If you find
  `"CH01"` in a .cpp file, that's a bug.
- Blueprints never implement rules that a test should cover. Health math, objective
  ordering, phase thresholds, ammo counts: C++.
- Widgets: layout in `WBP_*`, behaviour in the C++ parent. A widget Blueprint with more
  than a screenful of graph nodes is a sign the parent needs a function.
- AI: decisions are StateTrees (`Content/Blueprints/AI`), written by C++ builders so the script is the one
  description of each tree. C++ provides senses, actuators, conditions and tasks; a choice of what to do next
  in a controller is a bug.

## Lifecycle of a mission, end to end
1. Level `L_M0X` loads. Its Level Blueprint (or a `AMissionStarter` actor, stage 2) calls
   `MissionSubsystem->StartMission(DA_M0X)`.
2. Subsystem duplicates objectives, marks the first as current, fires `OnObjectiveUpdated`.
3. Player plays. World actors call `CompleteObjective` by id. Order is not enforced by
   default; `UMissionDefinition::bEnforceOrder` (stage 2) can require it.
4. When every non-optional objective is complete: `OnMissionComplete`. Controller shows
   the end card. If `FlashbackToPlay` is set: `OnFlashbackRequested`.
5. Controller creates `WBP_Flashback`, pauses, plays. `OnFlashbackFinished` unpauses
   and, if `NextLevel` is set, calls `OpenLevel`.
6. Save (stage 3) writes progress at step 4 before the flashback so a quit during the
   slideshow doesn't lose the mission.

## Things deliberately not built
No inventory system (one weapon slot, ammo counters). No dialogue tree (linear lines from
a data table). No branching levels (two boolean choices that change a line and a decal).
No multiplayer, replication, or networking of any kind; don't add `Replicated` specifiers.
