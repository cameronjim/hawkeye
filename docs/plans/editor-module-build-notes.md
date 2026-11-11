# HawkeyeEditor module: build notes

Written 2026-11-10 on `wip/editor` without compiling anything (the engine was busy on the other worktree). This is
the hand-off for the job that builds it: what was checked against the UE 5.8 headers, what was not, what each file
includes, and the order to build and verify in. The tool itself is described in
`claude-docs/tools-traversal-debugger.md`.

## What was added

| Where | Files |
|-------|-------|
| `Source/Hawkeye/Traversal/` (runtime) | `GrappleReachTypes.h`, `GrappleReachQuery.h/.cpp`, `GrappleReachWorld.h/.cpp`, `GrappleAudit.h/.cpp` |
| `Source/Hawkeye/Player/` (changed) | `GrappleComponent.h/.cpp`: defaults from `GrappleReachDefaults`, `MakeReachRules()`, `IsOnScreen` / `ChoosePick` / the angle / the release point call `FGrappleReachQuery` |
| `Source/Hawkeye/Tests/` | `GrappleReachQueryTest.cpp` (`Hawkeye.Grapple.Reach.*`, 11 tests) |
| `Source/HawkeyeEditor/` (new module) | `HawkeyeEditor.Build.cs`, `HawkeyeEditorModule.h/.cpp`, `TraversalDebuggerSubsystem.h/.cpp`, `STraversalDebuggerPanel.h/.cpp`, `TraversalAuditRunner.h/.cpp`, `HawkeyeTraversalAuditCommandlet.h/.cpp` |
| Project | `Hawkeye.uproject` (module entry), `Source/HawkeyeEditor.Target.cs` (`ExtraModuleNames`) |

## Decisions the brief did not foresee

- **LoadingPhase is `Default`, not `PostEngineInit`.** The engine finds a `-run=` commandlet's class at
  `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:3939`, after Default modules load (`:3896`,
  `LoadStartupModules`) and before PostEngineInit modules load (`:4068`). A PostEngineInit module's commandlet is never
  found ("looked like a commandlet, but we could not find the class"). The menu is registered through
  `UToolMenus::RegisterStartupCallback`, which waits for Slate and the menus, so nothing needed PostEngineInit.
- **A module named like the target is legal.** Target rules and module rules are different C# classes
  (`HawkeyeEditorTarget` vs `HawkeyeEditor`). Epic's own tree has the pattern at
  `Engine/Source/Programs/Shared/EpicGames.Perforce.Native/DotNetPerforceLib.Target.cs:7` and `DotNetPerforceLib.Build.cs:6`
  in the same folder; Lyra (`LyraEditor` target and module) is the well-known game example. The engine install has no
  game sample with an editor module to copy (the templates have no editor module).
- **`grapple_audit.json` is written by C++, not Python.** `Hawkeye.Grapple.Audit` (`Source/Hawkeye/Tests/GrappleAuditTest.cpp:1024`)
  writes it; `_reach.py` writes nothing. The new writer (`FGrappleAuditReport::ToJson`) keeps that file's keys and
  layout and adds one key, `street_facing_unreached`.
- **Traces stay in the component for now.** Only the pure math moved into `FGrappleReachQuery`. `FGrappleReachWorld::MakeOracle`
  mirrors `UGrappleComponent::HasLineOfSight`, `FindAnchorSupports`, `FindStartSupports` and `IsZipClear` for an anchor
  with no actor. Folding the component onto the oracle is step 8 below, after parity is shown.
- **The release point is now computed in double** (it was float). Same formula; differences are below a millimetre.

## APIs checked in the headers

Paths are under `C:\Program Files\Epic Games\UE_5.8\Engine\Source`. "Checked" means the signature was read at that
line; none of it has been compiled.

| API | Where | Used in |
|-----|-------|---------|
| `UEditorSubsystem` (abstract, `UDynamicSubsystem`) | `Editor/EditorSubsystem/Public/EditorSubsystem.h:20` | subsystem |
| `USubsystem::Initialize(FSubsystemCollectionBase&)`, `Deinitialize()` | `Runtime/Engine/Public/Subsystems/Subsystem.h:59`, `:62` | subsystem |
| `UEditorEngine::GetEditorSubsystem<T>()` | `Editor/UnrealEd/Classes/Editor/EditorEngine.h:3383` | panel |
| `UToolMenus::RegisterStartupCallback`, `UnRegisterStartupCallback(FDelegateUserObjectConst)`, `UnregisterOwner`, `ExtendMenu` | `Developer/ToolMenus/Public/ToolMenus.h:145`, `:148`, `:123`, `:169` | module |
| `FToolMenuOwnerScoped`, `FToolMenuOwner(void*)` | `ToolMenus.h:531`, `Developer/ToolMenus/Public/ToolMenuOwner.h:36` | module |
| `UToolMenu::FindOrAddSection(FName, TAttribute<FText>, ...)` | `Developer/ToolMenus/Public/ToolMenu.h:114` | module |
| `FToolMenuSection::AddMenuEntry(FName, label, tooltip, FSlateIcon, FToolUIActionChoice)` | `Developer/ToolMenus/Public/ToolMenuSection.h:62` | module |
| `FToolUIActionChoice(const FUIAction&)` | `Developer/ToolMenus/Public/ToolMenuDelegates.h:105` | module |
| `"LevelEditor.MainMenu.Tools"` is the Tools menu's name | `Editor/LevelEditor/Private/LevelEditorMenu.cpp`; same call in `Engine/Plugins/Editor/DataValidation/.../DataValidationModule.cpp:141` | module |
| `FGlobalTabmanager::Get`, `RegisterNomadTabSpawner`, `UnregisterNomadTabSpawner`, `TryInvokeTab(const FTabId&)` | `Runtime/Slate/Public/Framework/Docking/TabManager.h:1530`, `:1561`, `:1563`, `:1087` | module |
| `FTabSpawnerEntry::SetMenuType`, `ETabSpawnerMenuType::Hidden`, `FTabId(FName)`, `class FSpawnTabArgs` | `TabManager.h:318`, `:221`, `:86`, `:149` | module |
| `SDockTab::TabRole`, `ETabRole::NomadTab` | `Runtime/Slate/Public/Widgets/Docking/SDockTab.h:120`, `:27` | module |
| `UWorld::GetLineBatcher(ELineBatcherType)`, `ELineBatcherType::WorldPersistent` | `Runtime/Engine/Classes/Engine/World.h:1023`, `:1011` | subsystem |
| `ULineBatchComponent::DrawLine`, `DrawBox` (both), `DrawCapsule`, `ClearBatch` | `Runtime/Engine/Classes/Components/LineBatchComponent.h:178`, `:152`, `:155`, `:176`, `:216` | subsystem |
| A line with lifetime 0 is never aged out | `Runtime/Engine/Private/Components/LineBatchComponent.cpp:620` | subsystem |
| `GCurrentLevelEditingViewportClient`, `FEditorDelegates::MapChange` | `Editor/UnrealEd/Public/Editor.h:948`, `:231` | subsystem |
| `FEditorViewportClient::GetViewLocation()` | `Editor/UnrealEd/Public/EditorViewportClient.h:562` | subsystem |
| `UEditorEngine::MoveViewportCamerasToBox`, `GetSelectedActors`, `GetEditorWorldContext` | `EditorEngine.h:1185`, `:1969`, `:2597` | subsystem |
| `USelection::Num`, `GetSelectedObject(int32)` | `Editor/UnrealEd/Public/Selection.h:86`, `:91` | subsystem |
| `UEditorLoadingAndSavingUtils::LoadMap(const FString&)` (takes a long package name: `TryConvertToMountedPath`) | `Editor/UnrealEd/Public/FileHelpers.h:64`; `Editor/UnrealEd/Private/FileHelpers.cpp:5829`, `:3254` | commandlet |
| `UCommandlet::Main`, `ParseCommandLine(..., TMap&)`, `UseCommandletResultAsExitCode`, `HelpUsage`, ctor with `FObjectInitializer` | `Runtime/Engine/Classes/Commandlets/Commandlet.h:103`, `:142`, `:96`, `:50`; `Runtime/Engine/Private/Commandlets/Commandlet.cpp:28` | commandlet |
| `AWorldSettings::DefaultGameMode` (public), `AGameModeBase::DefaultPawnClass` | `Runtime/Engine/Classes/GameFramework/WorldSettings.h:634`, `GameModeBase.h:108` | runtime world |
| `USpringArmComponent::ProbeChannel`, `ProbeSize`, `bDoCollisionTest` (public) | `Runtime/Engine/Classes/GameFramework/SpringArmComponent.h:41`, `:37`, `:45` | runtime world |
| `UWorld::LineTraceMultiByObjectType`, `OverlapMultiByObjectType`, `GetWorldSettings(bool, bool)` | `World.h:2224`, `:2430`, `:2938` | runtime world |
| `FCollisionResponseParams(const FCollisionResponseContainer&)` | `Runtime/Engine/Public/CollisionQueryParams.h:336` | runtime world |
| `TActorIterator` explicit bool | `Runtime/Engine/Public/EngineUtils.h:394` | runtime world |
| `AActor::GetActorBounds` | `Runtime/Engine/Classes/GameFramework/Actor.h:1611` | runtime world |
| `SListView::ListItemsSource(const TArray<T>*)`, `RequestListRefresh` | `Runtime/Slate/Public/Widgets/Views/SListView.h:357` (`IItemsSource.h:20`), `STableViewBase.h:160` | panel |
| `SWrapBox::UseAllottedSize`, `InnerSlotPadding`; `SCheckBox::OnCheckStateChanged`; `SEditableTextBox::OnTextCommitted` | `Runtime/Slate/Public/Widgets/Layout/SWrapBox.h:164`, `:161`; `Widgets/Input/SCheckBox.h:80`; `Widgets/Input/SEditableTextBox.h:136` | panel |
| `FSimpleMulticastDelegate::AddSP(UserClass*, ...)` | `Runtime/Core/Public/Delegates/DelegateSignatureImpl.inl` (around `:920`) | panel |
| `FMath::FloorToInt(double)` returns int64, so the new code uses `FloorToInt32` | `Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:278`, `:264` | query, audit |
| `TOptional::GetPtrOrNull() const` | `Runtime/Core/Public/Misc/Optional.h:417` | subsystem |
| `FFileHelper::LoadFileToStringArray` | `Runtime/Core/Public/Misc/FileHelper.h:163` | commandlet |

## Unsure, look here first if it does not compile or behave

1. **Include path into the game module.** With `BuildSettingsVersion.V7` a dependent module only gets the other
   module's `PublicIncludePaths` (`Programs/UnrealBuildTool/Configuration/UEBuildModule.cs:721`; the legacy paths at
   `:731` are off). `Hawkeye.Build.cs` adds the relative `"Hawkeye"`, and it is not certain what that resolves to for a
   dependent, so `HawkeyeEditor.Build.cs` adds `Source/Hawkeye` to its own `PrivateIncludePaths`. If UBT warns about a
   duplicate path, drop it.
2. **The game module's private dependencies leaking through headers.** The editor module includes
   `Traversal/GrappleAudit.h` (pulls `World/CityLedgeData.h`), `World/CityLedgeSpawner.h`, `World/CityMapData.h`,
   `World/GrappleAnchor.h` and `World/FireEscapeLanding.h`, none of which include a privately-linked module's header.
   `Player/HawkeyeCharacter.h` was kept out of the editor module on purpose (it pulls SPUD and the combat headers).
3. **Reading a Blueprint class default's components.** `FGrappleReachWorld::MakeProfile` reads the grapple component,
   capsule and camera boom off `BP_Kate`'s class default object (`GetDefaultObject<APawn>()`). Native default
   subobjects on a Blueprint CDO carry the Blueprint's overrides; check in the log line the tab prints after Refresh
   (`rules from BP_Kate_C (range 2500, cone 30)`) that the source is BP_Kate and not `HawkeyeCharacter`.
4. **`UGrappleComponent::MakeReachRules` on a CDO.** `GetCharacter()` is `Cast<ACharacter>(GetOwner())`; on a default
   subobject the owner is the class default actor, on `GetDefault<UGrappleComponent>()` (the test) it is null and the
   capsule falls back to `GrappleReachDefaults`.
5. **Lines in the editor viewport.** The persistent line batcher is updated "at the end of UWorld::Tick"
   (`LineBatchComponent.cpp:162`); the editor world ticks while a viewport is realtime. If the layers only appear after
   moving the camera, turn the viewport's Realtime on or call `GEditor->RedrawLevelEditingViewports()` after `Redraw`.
6. **The commandlet's world.** `LoadMap` gives an editor world with collision (editor worlds have a physics scene); the
   audit then spawns the props with `ACityLedgeSpawner::SpawnAll`, which `verify_city.py` already does in an editor
   world. If traces hit nothing, check `World->GetPhysicsScene()` and that `-nullrhi` did not skip component
   registration.
7. **Slate details.** `SNew(STextBlock).Text(this, &STraversalDebuggerPanel::GetStatusText)` (attribute bound to a const
   member), the `_Lambda` forms of `IsChecked`, `OnCheckStateChanged`, `OnClicked`, `Text`, `OnTextCommitted`, and
   `ToolTipText` on `SButton` are the standard macros but were not traced to their expansions.
8. **`LexToString` overloads** for the two new enums sit in the global namespace next to the engine's; if a call is
   ambiguous, rename them `ToString`.

## Includes per file

| File | Includes |
|------|----------|
| `Traversal/GrappleReachTypes.h` | `CoreMinimal.h` |
| `Traversal/GrappleReachQuery.h/.cpp` | `CoreMinimal.h`, `GrappleReachTypes.h` |
| `Traversal/GrappleReachWorld.h` | `CoreMinimal.h`, `Engine/EngineTypes.h`, `GrappleReachQuery.h`, `World/CityLedgeData.h` |
| `Traversal/GrappleReachWorld.cpp` | `CollisionQueryParams.h`, `Components/CapsuleComponent.h`, `Engine/OverlapResult.h`, `Engine/World.h`, `EngineUtils.h`, `GameFramework/GameModeBase.h`, `GameFramework/SpringArmComponent.h`, `GameFramework/WorldSettings.h`, `Player/GrappleComponent.h`, `Player/HawkeyeCharacter.h`, `World/CityLedgeSpawner.h`, `World/CityMapData.h`, `World/GrappleAnchor.h` |
| `Traversal/GrappleAudit.h` | `CoreMinimal.h`, `GrappleReachWorld.h` |
| `Traversal/GrappleAudit.cpp` | `Engine/World.h`, `HAL/FileManager.h`, `HAL/PlatformTime.h`, `Misc/FileHelper.h`, `Misc/Paths.h` |
| `Player/GrappleComponent.h` | adds `Traversal/GrappleReachTypes.h` |
| `Player/GrappleComponent.cpp` | adds `Traversal/GrappleReachQuery.h` |
| `Tests/GrappleReachQueryTest.cpp` | `Algo/Reverse.h`, `Misc/AutomationTest.h`, `Player/GrappleComponent.h`, `Traversal/GrappleAudit.h`, `Traversal/GrappleReachQuery.h` |
| `HawkeyeEditorModule.h` | `CoreMinimal.h`, `Modules/ModuleInterface.h` |
| `HawkeyeEditorModule.cpp` | `Framework/Application/SlateApplication.h`, `Framework/Commands/UIAction.h`, `Framework/Docking/TabManager.h`, `Modules/ModuleManager.h`, `STraversalDebuggerPanel.h`, `Styling/AppStyle.h`, `Textures/SlateIcon.h`, `ToolMenus.h`, `Widgets/Docking/SDockTab.h` |
| `TraversalDebuggerSubsystem.h` | `CoreMinimal.h`, `EditorSubsystem.h`, `Traversal/GrappleAudit.h` |
| `TraversalDebuggerSubsystem.cpp` | `CollisionQueryParams.h`, `Components/LineBatchComponent.h`, `Editor.h`, `Engine/World.h`, `HawkeyeEditorModule.h`, `LevelEditorViewport.h`, `Selection.h`, `TraversalAuditRunner.h`, `World/FireEscapeLanding.h` |
| `STraversalDebuggerPanel.h` | `CoreMinimal.h`, `Widgets/DeclarativeSyntaxSupport.h`, `Widgets/SCompoundWidget.h`, `Widgets/Views/SListView.h` |
| `STraversalDebuggerPanel.cpp` | `Editor.h`, `TraversalDebuggerSubsystem.h`, `Widgets/Input/SButton.h`, `Widgets/Input/SCheckBox.h`, `Widgets/Input/SEditableTextBox.h`, `Widgets/Layout/SSeparator.h`, `Widgets/Layout/SWrapBox.h`, `Widgets/SBoxPanel.h`, `Widgets/Text/STextBlock.h`, `Widgets/Views/STableRow.h` |
| `TraversalAuditRunner.h` | `CoreMinimal.h`, `Traversal/GrappleAudit.h` |
| `TraversalAuditRunner.cpp` | `Engine/World.h`, `GameFramework/Pawn.h`, `HawkeyeEditorModule.h`, `Traversal/GrappleReachWorld.h`, `World/CityLedgeSpawner.h`, `World/CityMapData.h`, `World/GrappleAnchor.h` |
| `HawkeyeTraversalAuditCommandlet.h` | `CoreMinimal.h`, `Commandlets/Commandlet.h` |
| `HawkeyeTraversalAuditCommandlet.cpp` | `FileHelpers.h`, `HawkeyeEditorModule.h`, `Misc/FileHelper.h`, `Misc/Paths.h`, `TraversalAuditRunner.h` |

## Build and verify, in this order

1. **Build** the editor target (closes nothing; the editor must be closed): the usual
   `Build.bat HawkeyeEditor Win64 Development -project=...\Hawkeye.uproject -waitmutex`. The target and the new module
   share the name; the module builds into `Binaries/Win64/UnrealEditor-HawkeyeEditor.dll`. Fix errors in the runtime
   files first (the editor module depends on them), using the "Unsure" list.
2. **Tests, headless**: `Automation RunTests Hawkeye.Grapple` (the new `Hawkeye.Grapple.Reach.*` and the existing
   `PickRule`, `Target`, `Component`, `Shot` tests, which guard the component refactor). Then the full `Hawkeye` run as
   usual. Nothing else changed behaviour, so any other failure was already there.
3. **Commandlet, no spawn** (fast smoke): `-run=HawkeyeTraversalAudit -map=/Game/Maps/L_District_EastVillage -nospawn
   -out=<scratch>\audit_nospawn.json`. Expect exit 0 or 1 (not 2), the summary line in the log, and the JSON.
4. **Commandlet, full**: the same without `-nospawn`, with `-chain=reach_roof,cross_block,find_arrow`
   `-out=<scratch>\audit_commandlet.json`. Expect the "spawned N city props" line, then the summary.
5. **Test audit for comparison**: run `Hawkeye.Grapple.Audit` from the standalone game (command in
   `claude-docs/infrastructure.md`); it writes `Saved/Automation/grapple_audit.json`. Copy it aside.
6. **Compare** the two JSONs: `roofs` row by row (`from_street`, `from_roofs`, `street_spots_in_range`,
   `neighbours_reaching`), the lengths of `roofs_no_street`, `roofs_no_street_at_2_5_m`, `anchors_never_reachable`, and
   `objectives`. Expected differences: `picker_misses` and `anchors_never_picked` empty in the commandlet's; a roof
   reached from a spot the test's live capsule probe would have treated differently (the predicted lens ignores no
   actor, the test's ignores Kate). Counts within a handful are parity; a large gap means the oracle's traces differ
   from the component's: diff `FGrappleReachWorld::MakeOracle` against `UGrappleComponent::IsZipClear` first.
7. **The tab**, in the full editor (only after 1 to 6; close it after): open the district, Tools > Hawkeye >
   Traversal Debugger, Refresh (check the rules' source is BP_Kate), toggle each layer, Reach from camera on a
   sidewalk and on a roof, Run audit, Focus a red roof. Then write the allow-file baseline
   (`Tools/traversal_audit_allow.txt`) from the commandlet's `street_facing_unreached` with Cameron.
8. **Follow-ups once parity holds**: route `UGrappleComponent::HasLineOfSight` / `IsZipClear` through the shared trace
   code so the oracle and the component cannot drift; have `GrappleAuditTest.cpp` use `FGrappleReachQuery`'s footprint
   helpers and `FGrappleAuditReport::ToJson` instead of its own copies; add the commandlet to `Tools/` scripts and
   `claude-docs/infrastructure.md`; mention the second module in `CLAUDE.md`'s repo map.
