# Code style

Epic's coding standard applies. This file is the parts we care about and the places we
choose differently. When in doubt, match the surrounding file.

## Naming
- Classes: `A` for actors, `U` for UObjects and components, `F` for structs, `E` for
  enums, `I` for interfaces, `T` for templates. No other prefixes.
- Booleans start with `b`: `bIsReloading`, `bOptional`. Never `isReloading` or `reloading`.
- Delegates are `On` + past-tense or noun: `OnHealthChanged`, `OnDeath`, `OnPhaseChanged`.
  Their types are `FOn...Signature`.
- Functions are verbs in PascalCase: `ApplyDamage`, `TryTakedown`, `CompleteObjective`.
  Query functions read as questions: `IsAlive()`, `CanFire()`, `HasObjective(Id)`.
- Enum values are unprefixed inside `enum class`: `EThugAlertState::Calm`, not `ETAS_Calm`.
- Member variables have no `m_` or trailing underscore.
- One class per header. File name matches the class without the prefix:
  `HealthComponent.h` holds `UHealthComponent`.

## Headers
- `#pragma once` first, then `CoreMinimal.h`, then the parent class header, then other
  engine headers, then project headers, then the `.generated.h` last. Nothing after
  `.generated.h`.
- Forward-declare everything you can. Include only what the header itself needs.
  Components used as members can be `TObjectPtr<UHealthComponent>` with a forward
  declaration; the .cpp includes the real header.
- Public API above private. `GENERATED_BODY()` on the first line inside the class.

## UObject specifics
- `TObjectPtr<T>` for every UObject member. Raw `T*` only for function parameters and
  locals.
- Every UObject member is a `UPROPERTY()` even if it isn't exposed, so the GC sees it.
- `UPROPERTY` categories are one or two words matching the component: `Health`,
  `Weapon|Ammo`, `Mission`, `Flashback|Timing`. Everything a designer tunes is
  `EditAnywhere` or `EditDefaultsOnly` plus `BlueprintReadOnly`. Runtime state is
  `VisibleAnywhere, BlueprintReadOnly` or `Transient`.
- Anything a designer might clamp gets `meta = (ClampMin = "0.0")` or `UIMin/UIMax`.
- Components: `UCLASS(ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))`.
- Data assets: `UCLASS(BlueprintType)` deriving `UPrimaryDataAsset`. Override
  `GetPrimaryAssetId` to return a stable type (`"Mission"`, `"Flashback"`).
- Interfaces: `UINTERFACE(MinimalAPI, Blueprintable)` with `BlueprintNativeEvent`
  functions. Callers use `ITakedownable::Execute_OnTakedown(Target, Attacker)` after
  checking `Target->Implements<UTakedownable>()`.
- Delegates: `DECLARE_DYNAMIC_MULTICAST_DELEGATE_*` so Blueprints can bind. Mark the
  property `UPROPERTY(BlueprintAssignable, Category = "...")`. Broadcast from C++ only;
  Blueprints listen.
- Subsystems: `UWorldSubsystem` for per-world runtime (missions). `UGameInstanceSubsystem`
  for anything that survives level loads (save, settings). Never a singleton.

## Functions
- `UFUNCTION(BlueprintCallable)` on anything a Blueprint needs to call.
  `BlueprintPure` only for side-effect-free getters that are cheap.
- Keep `BlueprintImplementableEvent` for "designer hooks" (`OnPhaseStarted`,
  `OnReloadStarted`) and provide a C++ default via `BlueprintNativeEvent` when a default
  makes sense.
- Aim for functions under 40 lines. It is a target, not something the code meets today:
  about 250 non-test functions are longer, most of them in `AHawkeyeCharacter`,
  `AHawkeyePlayerController` and `AThugAIController`, whose splits are listed in
  `architecture.md`, "Known next steps". New code meets it; when you change a long function,
  pull out the part you touch rather than growing it. Tick functions in particular should
  call two or three named helpers, not do the work inline.
- `const` on every method that doesn't mutate. `const&` for struct and container
  parameters.
- Early return over nesting. Validate at the top, then do the work.

## Memory and safety
- `IsValid(Ptr)` before dereferencing any UObject pointer that came from outside the
  function. `ensure(Ptr)` when it should never be null and you want a report; `check`
  only for programmer errors that make continuing meaningless.
- No `new`/`delete`. `NewObject`, `CreateDefaultSubobject`, `SpawnActor`, or stack
  structs.
- Timers: `FTimerHandle` as a member, cleared in `EndPlay`. Anything that could outlive
  the world uses a weak lambda capture or `TWeakObjectPtr`.
- Nothing runs on a thread other than the game thread unless it is pure math on
  copied data.

## Logging
- One category: `DECLARE_LOG_CATEGORY_EXTERN(LogHawkeye, Log, All)` in `Hawkeye.h`,
  defined in `Hawkeye.cpp`.
- `UE_LOG(LogHawkeye, Warning, TEXT("..."))` for designer mistakes (unknown objective id,
  missing data asset). `Error` for things that break the game. `Verbose` for per-frame
  or per-event traces that are off by default.
- Log format: `TEXT("%s: message with %s"), *GetNameSafe(this), *Id.ToString()`. Always
  say which object.

## Comments
- Comment the why, not the what. `// Duplicate so the asset never gets dirtied` is
  useful. `// Loop over objectives` is not.
- Doc comments (`/** */`) on every public UFUNCTION and UPROPERTY; the editor shows them
  as tooltips, so write them for a designer.
- No commented-out code in commits.
- `// TODO(stage3): ...` with the stage that will address it. Untagged TODOs get deleted.

## Blueprints (the parts that are still "code")
- Event graphs stay short. Anything over about 30 nodes becomes a function or moves to
  C++.
- Every Blueprint function has a description. Every variable that a level designer sets
  is "Instance Editable" with a tooltip and a category.
- Comment boxes around every logical group. Reroute nodes so wires don't cross.
- No `Delay` nodes for gameplay timing. Use timers or the C++ component.
- No `Get All Actors Of Class` in Tick. Cache at BeginPlay.
- Casts to specific mission or boss classes from shared Blueprints are banned; use
  interfaces or tags.

## Formatting
- Tabs for indentation (Epic standard). Allman braces (brace on its own line). No line
  over 120 characters.
- One blank line between functions. No double blank lines.
- `.clang-format` at the repo root encodes this; run it on files you touch, not on
  files you don't.
