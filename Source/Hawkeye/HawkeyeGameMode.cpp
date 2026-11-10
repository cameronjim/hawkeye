#include "HawkeyeGameMode.h"

#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "GameFramework/Pawn.h"
#include "Mission/MissionDefinition.h"
#include "Player/InventoryComponent.h"
#include "NavigationSystem.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Misc/App.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/HawkeyeResidentAssets.h"
#include "Mission/MissionSubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Player/HawkeyeCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/HealthComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mission/MissionObjective.h"
#include "Partner/HawkeyePartnerController.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "TimerManager.h"
#include "World/ThugCharacter.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#endif

namespace HawkeyeGameModeClock
{
	/** When the last level change began (the outgoing game mode's EndPlay), wall seconds; 0 before any. */
	static double LevelChangeStartSeconds = 0.0;

	/** -HawkeyeReloadAfterPlayable has reopened the level once this process. */
	static bool bMeasuredReloadDone = false;
}

AHawkeyeGameMode::AHawkeyeGameMode()
{
	DefaultPawnClass = AHawkeyeCharacter::StaticClass();
	PlayerControllerClass = AHawkeyePlayerController::StaticClass();
	// Only the playable mark and the frame watch after it; the main menu pauses the world under it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

bool AHawkeyeGameMode::WantsOutdoorWeather(const UWorld* World)
{
	return WantsOutdoorWeather(World ? World->GetAuthGameMode() : nullptr);
}

bool AHawkeyeGameMode::WantsOutdoorWeather(const AGameModeBase* GameMode)
{
	const AHawkeyeGameMode* Hawkeye = Cast<AHawkeyeGameMode>(GameMode);
	return Hawkeye && Hawkeye->bOutdoorWeather;
}

bool AHawkeyeGameMode::WantsInteriorCamera(const UWorld* World)
{
	const AHawkeyeGameMode* Hawkeye = World ? Cast<AHawkeyeGameMode>(World->GetAuthGameMode()) : nullptr;
	return Hawkeye && Hawkeye->bInteriorCamera;
}

void AHawkeyeGameMode::Tick(float DeltaSeconds)

{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	if (PlayableSeconds < 0.f)
	{
		const UWorld* World = GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC && PC->GetPawn())
		{
			NotePlayable();
		}
		return;
	}
	WatchFrame(Now);
}

void AHawkeyeGameMode::NotePlayable()
{
	const double Now = FPlatformTime::Seconds();
	PlayableWallSeconds = Now;
	LastFrameWallSeconds = Now;
	FrameWatch = FHawkeyeFrameWatch();
	float HitchMs = HitchLogMs;
	FParse::Value(FCommandLine::Get(), TEXT("HawkeyeHitchMs="), HitchMs);
	FrameWatch.ThresholdSeconds = FMath::Max(HitchMs, 1.f) / 1000.0;
	PerfWindow = FHawkeyePerfWindow();
	PerfWindow.OverMs = 50.f;
	PerfWindowStartSeconds = Now;
	LastStatDumpSeconds = Now;
	const bool bFirstMap = HawkeyeGameModeClock::LevelChangeStartSeconds <= 0.0;
	const double From = bFirstMap ? GStartTime : HawkeyeGameModeClock::LevelChangeStartSeconds;
	PlayableSeconds = static_cast<float>(Now - From);
	UE_LOG(LogHawkeye, Log, TEXT("Playable after %.2f s (%s to the first tick with a possessed pawn; BeginPlay to it %.2f s, map %s)."),
		PlayableSeconds, bFirstMap ? TEXT("process start") : TEXT("level change"), Now - BeginPlayWallSeconds,
		*GetNameSafe(GetWorld()));

	ScreenshotProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(
		this, &AHawkeyeGameMode::HandleScreenshotProcessed);

	// Walked in through a door that faded to black: bring the picture back now the room is here.
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Save && PC && PC->PlayerCameraManager && Save->ConsumeInteriorArrivalFade())
	{
		PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, Save->FadeInSeconds, FLinearColor::Black, false, false);
	}
	// And she walked in with her quiver.
	if (Save && Save->IsInInterior())
	{
		Save->RestoreCarriedQuiver(GetWorld());
	}
}

void AHawkeyeGameMode::HandleScreenshotProcessed()
{
	bScreenshotThisFrame = true;
}

void AHawkeyeGameMode::WatchFrame(double Now)
{
	const double FrameSeconds = Now - LastFrameWallSeconds;
	LastFrameWallSeconds = Now;
	const double Since = Now - PlayableWallSeconds;
	// The capture ran in the previous frame's draw, so it lengthens the frame that ends now.
	const bool bScreenshot = bScreenshotThisFrame;
	bScreenshotThisFrame = false;
	if (FrameWatch.AddFrame(FrameSeconds, Since, bScreenshot))
	{
		// The stat unit split of the last completed frame: which of the game thread, the render thread, the RHI
		// thread and the GPU the time went to (a frame long on none of them was waiting on something else).
		UE_LOG(LogHawkeye, Log, TEXT("Hitch: a %.0f ms frame %.2f s after the playable mark (frame %d)%s; game %.0f, draw %.0f, rhi %.0f, gpu %.0f ms."),
			FrameSeconds * 1000.0, Since, FrameWatch.Frames - 1, bScreenshot ? TEXT(", a screenshot capture") : TEXT(""),
			FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
			FPlatformTime::ToMilliseconds(GRHIThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
	}
	if (!bFrameSummaryLogged && Since >= FrameSummarySeconds)
	{
		bFrameSummaryLogged = true;
		UE_LOG(LogHawkeye, Log, TEXT("First %.0f s after the playable mark: %d frames, worst %.0f ms at %.2f s%s; worst without captures %.0f ms at %.2f s; %d frame(s) of %.0f ms or more (%d of them screenshot captures)."),
			FrameSummarySeconds, FrameWatch.Frames, FrameWatch.WorstSeconds * 1000.0, FrameWatch.WorstAtSeconds,
			FrameWatch.bWorstWasScreenshot ? TEXT(" (a capture)") : TEXT(""), FrameWatch.WorstGameSeconds * 1000.0,
			FrameWatch.WorstGameAtSeconds, FrameWatch.HitchFrames, FrameWatch.ThresholdSeconds * 1000.0, FrameWatch.ScreenshotHitchFrames);
	}
	WatchPerf(Now, FrameSeconds, bScreenshot);
	// The reload measurement: one OpenLevel of the same map, as a death with no save does.
	if (ReloadAfterPlayableSeconds >= 0.f && !HawkeyeGameModeClock::bMeasuredReloadDone && Since >= ReloadAfterPlayableSeconds)
	{
		HawkeyeGameModeClock::bMeasuredReloadDone = true;
		UE_LOG(LogHawkeye, Log, TEXT("%s: -HawkeyeReloadAfterPlayable=%.1f: reopening the level."), *GetName(), ReloadAfterPlayableSeconds);
		ReopenCurrentLevel();
		return;
	}
	// Wall time, not a timer: the main menu pauses the world on a fresh boot.
	if (QuitAfterPlayableSeconds >= 0.f && !bQuitRequested && Since >= QuitAfterPlayableSeconds
		&& (ReloadAfterPlayableSeconds < 0.f || HawkeyeGameModeClock::bMeasuredReloadDone))
	{
		bQuitRequested = true;
		UE_LOG(LogHawkeye, Log, TEXT("%s: -HawkeyeQuitAfterPlayable=%.1f: quitting."), *GetName(), QuitAfterPlayableSeconds);
		FPlatformMisc::RequestExit(/*bForce=*/false, TEXT("HawkeyeQuitAfterPlayable"));
	}
}

namespace HawkeyeGameModePerf
{
	static FString Phase;

	static FString CurrentLabel()
	{
		FString Label = TEXT("play");
#if WITH_AUTOMATION_TESTS
		if (const FAutomationTestBase* Test = FAutomationTestFramework::Get().GetCurrentTest())
		{
			Label = Test->GetTestFullName();
		}
#endif
		return Phase.IsEmpty() ? Label : Label + TEXT("/") + Phase;
	}
}

void AHawkeyeGameMode::SetPerfPhase(const FString& Phase)
{
	HawkeyeGameModePerf::Phase = Phase;
}

void AHawkeyeGameMode::WatchPerf(double Now, double FrameSeconds, bool bScreenshot)
{
	if (PerfLogSeconds <= 0.f && StatDumpSeconds <= 0.f)
	{
		return;
	}
	const FString Label = HawkeyeGameModePerf::CurrentLabel();
	if (Label != PerfLabel)
	{
		FlushPerfWindow(Now);
		PerfLabel = Label;
	}
	// What stat unit shows: the last completed frame's game, render thread and GPU times.
	PerfWindow.AddFrame(float(FrameSeconds * 1000.0), FPlatformTime::ToMilliseconds(GGameThreadTime),
		FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)), bScreenshot);
	if (PerfLogSeconds > 0.f && Now - PerfWindowStartSeconds >= PerfLogSeconds)
	{
		FlushPerfWindow(Now);
	}
	// ExecCmds only starts the automation run when it leads the line, so the hitch dump is switched on here.
	static bool bHitchDumpOn = false;
	if (StatDumpSeconds > 0.f && !bHitchDumpOn && GEngine)
	{
		bHitchDumpOn = true;
		GEngine->Exec(GetWorld(), TEXT("stat dumphitches"));
	}
	if (StatDumpSeconds > 0.f && Now - LastStatDumpSeconds >= StatDumpSeconds && GEngine)
	{
		LastStatDumpSeconds = Now;
		UE_LOG(LogHawkeye, Log, TEXT("Perf stat dump [%s] %.1f s after the playable mark."), *PerfLabel, Now - PlayableWallSeconds);
		GEngine->Exec(GetWorld(), TEXT("stat dumpave -num=120 -ms=0.1"));
	}
}

void AHawkeyeGameMode::FlushPerfWindow(double Now)
{
	if (PerfWindow.Num() > 0 || PerfWindow.CaptureFrames > 0)
	{
		const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
		UE_LOG(LogHawkeye, Log, TEXT("Perf [%s] %s %.1f-%.1f s: %s; working set %.2f GB, peak %.2f GB."),
			*PerfLabel, *GetNameSafe(GetWorld()), PerfWindowStartSeconds - PlayableWallSeconds, Now - PlayableWallSeconds,
			*PerfWindow.Describe(), Memory.UsedPhysical / double(1 << 30), Memory.PeakUsedPhysical / double(1 << 30));
	}
	const float OverMs = PerfWindow.OverMs;
	PerfWindow = FHawkeyePerfWindow();
	PerfWindow.OverMs = OverMs;
	PerfWindowStartSeconds = Now;
}

void AHawkeyeGameMode::BeginPlay()
{
	Super::BeginPlay();
	BeginPlayWallSeconds = FPlatformTime::Seconds();
	FParse::Value(FCommandLine::Get(), TEXT("HawkeyeQuitAfterPlayable="), QuitAfterPlayableSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("HawkeyeReloadAfterPlayable="), ReloadAfterPlayableSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("HawkeyePerfLog="), PerfLogSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("HawkeyeStatDump="), StatDumpSeconds);

	IndexMotionMatchingDatabases();
	// What this map loaded that the next one will want too stays loaded through the level change.
	if (UHawkeyeResidentAssets* Resident = UHawkeyeResidentAssets::Get(this))
	{
		Resident->KeepWorldContent(GetWorld());
	}
	BuildNavigationIfEmpty();
	// After the indexing, which needs nothing streaming: these land over the first seconds.
	PreloadEffectsAndSounds();

	UMissionSubsystem* MissionSubsystem = UMissionSubsystem::Get(this);
	if (!MissionSubsystem)
	{
		return;
	}

	MissionSubsystem->OnMissionComplete.AddDynamic(this, &AHawkeyeGameMode::HandleMissionComplete);
	MissionSubsystem->OnMissionStarted.AddDynamic(this, &AHawkeyeGameMode::HandleMissionStarted);
	MissionSubsystem->OnObjectiveUpdated.AddDynamic(this, &AHawkeyeGameMode::HandleObjectiveUpdated);

	// -HawkeyeChapter=<asset name>: a lap or a test starts the district on another chapter (a load still wins below).
	UMissionDefinition* Chapter = StartingMission;
	FString ChapterName;
	if (FParse::Value(FCommandLine::Get(), TEXT("HawkeyeChapter="), ChapterName))
	{
		if (UMissionDefinition* Asked = FindChapter(ChapterName))
		{
			Chapter = Asked;
			UE_LOG(LogHawkeye, Log, TEXT("%s: -HawkeyeChapter starts %s."), *GetName(), *Asked->GetName());
		}
		else
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: -HawkeyeChapter=%s names no chapter under /Game/Missions."), *GetName(), *ChapterName);
		}
	}
	if (Chapter)
	{
		MissionSubsystem->StartMission(Chapter);
	}
	else
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s has no StartingMission set."), *GetName());
	}

	// A load in progress: the mission has just started from the top; the save says how far it got.
	// Actors (Kate, Clint, thugs) are restored by SPUD right after BeginPlay.
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (!Save)
	{
		return;
	}
	Save->ApplyPendingRestore(GetWorld());
	// Into a chapter interior, or out of one into a district opened fresh: the chapter carried on with no load.
	Save->FinishContinuation(GetWorld());
	// Back from a playable scene without a save to load: put the player at the return point now.
	Save->ApplyPendingSceneReturn(GetWorld());
	GetWorldTimerManager().SetTimer(AutosaveTimerHandle, this, &AHawkeyeGameMode::TickAutosave, AutosaveTickSeconds, true);
	if (Save->ConsumeNewGameAutosave())
	{
		GetWorldTimerManager().SetTimer(NewGameSaveTimerHandle, this, &AHawkeyeGameMode::SaveNewGame, 1.5f, false);
	}
}

UMissionDefinition* AHawkeyeGameMode::FindChapter(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return nullptr;
	}
	const FString Path = Name.StartsWith(TEXT("/")) ? Name : FString::Printf(TEXT("/Game/Missions/%s.%s"), *Name, *Name);
	return LoadObject<UMissionDefinition>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

bool AHawkeyeGameMode::StartChapter(UMissionDefinition* Chapter)
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Chapter || !Missions)
	{
		return false;
	}
	// Chosen, not cut short: the chapter before is put away without the warning StartMission gives a replaced one.
	Missions->AbortMission();
	if (!Missions->StartMission(Chapter))
	{
		return false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: chapter %s started by hand."), *GetName(), *Chapter->GetName());
	GetWorldTimerManager().SetTimer(ChapterSaveTimerHandle, this, &AHawkeyeGameMode::SaveChapterStart, 1.5f, false);
	return true;
}

void AHawkeyeGameMode::SaveChapterStart()
{
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->SaveCampaign(TEXT("chapter start"));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GHawkeyeStartChapterCommand(
	TEXT("hawkeye.StartChapter"),
	TEXT("hawkeye.StartChapter <mission asset name>: starts that chapter now (DA_CH04_SocialClub)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AHawkeyeGameMode* Mode = World ? World->GetAuthGameMode<AHawkeyeGameMode>() : nullptr;
		UMissionDefinition* Chapter = Args.Num() > 0 ? AHawkeyeGameMode::FindChapter(Args[0]) : nullptr;
		if (!Mode || !Chapter || !Mode->StartChapter(Chapter))
		{
			UE_LOG(LogHawkeye, Warning, TEXT("hawkeye.StartChapter: no such chapter, or no Hawkeye game mode."));
		}
	}));

void AHawkeyeGameMode::SaveNewGame()
{
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->SaveCampaign(TEXT("new game"));
	}
}

void AHawkeyeGameMode::HandleObjectiveUpdated(UMissionObjective* Objective, int32 /*ObjectiveIndex*/)
{
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (Save && Objective && Objective->IsCompleted())
	{
		// The objective's story flag, if it has one, goes into this very save.
		if (UHawkeyeCampaignState* Campaign = Save->GetCampaignState(); Campaign && Campaign->ApplyObjectiveFlag(Objective))
		{
			UE_LOG(LogHawkeye, Log, TEXT("Campaign flag %s raised by %s."), *Objective->CampaignFlag.ToString(), *Objective->ObjectiveId.ToString());
		}
		// Before OnMissionComplete, which clears the quiver for the end card.
		Save->SaveCampaign(FString::Printf(TEXT("objective %s"), *Objective->ObjectiveId.ToString()));
	}
}

void AHawkeyeGameMode::ApplyChapterCast(UWorld* World, const UMissionDefinition* Mission)
{
	if (!World || !Mission)
	{
		return;
	}
	for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
	{
		It->SetBenched(!Mission->bPartnerPresent);
	}
	// Another chapter's beacons (tagged objective:<id>) stay dark while this one runs.
	TSet<FName> Ids;
	for (const UMissionObjective* Objective : Mission->Objectives)
	{
		if (Objective)
		{
			Ids.Add(Objective->ObjectiveId);
		}
	}
	static const FString Prefix(TEXT("objective:"));
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		for (const FName Tag : It->Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(Prefix))
			{
				const bool bOurs = Ids.Contains(FName(*Text.RightChop(Prefix.Len())));
				It->SetActorHiddenInGame(!bOurs);
				It->SetActorEnableCollision(bOurs);
				break;
			}
		}
	}
}

bool AHawkeyeGameMode::IsWorldInCombat(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (Health && Health->IsAlive() && It->GetAlertState() == EThugAlertState::Alerted)
		{
			return true;
		}
	}
	for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
	{
		if (It->IsInFight())
		{
			return true;
		}
	}
	return false;
}

bool AHawkeyeGameMode::CanAutosaveNow() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AHawkeyeCharacter* Player = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	const UHealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
	const UCharacterMovementComponent* Movement = Player ? Player->GetCharacterMovement() : nullptr;
	if (!Health || !Health->IsAlive() || Player->IsDowned() || !Movement || !Movement->IsMovingOnGround())
	{
		return false;
	}
	return !bRestartPending && !IsInCombat();
}

void AHawkeyeGameMode::TickAutosave()
{
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->AdvanceAutosave(AutosaveTickSeconds, CanAutosaveNow());
	}
}

void AHawkeyeGameMode::ReloadLastCheckpoint(float FadeSeconds)
{
	UWorld* World = GetWorld();
	if (bRestartPending || !World)
	{
		return;
	}
	bRestartPending = true;
	OnMissionRestarting();

	const float Seconds = FadeSeconds < 0.f ? DeathFadeSeconds : FadeSeconds;
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager)
	{
		// Held at black: the load that follows swaps the world out underneath it.
		PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, Seconds, FLinearColor::Black, false, true);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: fading out for %.1f s, then loading the last save."), *GetName(), Seconds);

	if (Seconds <= 0.f)
	{
		LoadCheckpointNow();
		return;
	}
	World->GetTimerManager().SetTimer(RestartTimerHandle, this, &AHawkeyeGameMode::LoadCheckpointNow, Seconds, false);
}

void AHawkeyeGameMode::LoadCheckpointNow()
{
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (Save && Save->HasSave())
	{
		// Either the save loads, or LoadCampaign decides it is stale and starts a new game; both
		// travel. Only a refusal (SPUD busy) leaves nothing pending, and then the level reload runs.
		Save->LoadCampaign();
		if (Save->IsTravelPending())
		{
			return;
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: no save to go back to; reloading the level."), *GetName());
	ReopenCurrentLevel();
}

void AHawkeyeGameMode::IndexMotionMatchingDatabases()
{
#if WITH_EDITOR
	if (!bIndexMotionMatchingAtLoad)
	{
		return;
	}
	using namespace UE::PoseSearch;
	const double Start = FPlatformTime::Seconds();
	// A database waits for its animations to be loaded and compiled before it can compose its key.
	// Whatever BeginPlay has asked to stream in so far would land in the first frames anyway.
	FlushAsyncLoading();
	const int32 Compiling = FAssetCompilingManager::Get().GetNumRemainingAssets();
	FAssetCompilingManager::Get().FinishAllCompilation();
	const double Compiled = FPlatformTime::Seconds();
	int32 AlreadyIndexed = 0;
	TArray<const UPoseSearchDatabase*> Requested;
	for (TObjectIterator<UPoseSearchDatabase> It; It; ++It)
	{
		const UPoseSearchDatabase* Database = *It;
		if (!IsValid(Database) || Database->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
			|| Database->GetPackage()->HasAnyPackageFlags(PKG_Cooked))
		{
			continue;
		}
		if (FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest)
			== EAsyncBuildIndexResult::Success)
		{
			++AlreadyIndexed;
			continue;
		}
		// Composes the key now and starts the DDC fetch on a worker thread.
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::NewRequest);
		Requested.Add(Database);
	}
	const double Keyed = FPlatformTime::Seconds();

	// Then take the fetched indices in here too: handing 160 of them over is 60 ms of the first tick
	// otherwise. A wait is only safe for a request that has started, and every one has once nothing
	// is loading or compiling; if something still is, the manager's tick finishes the job instead.
	int32 Indexed = 0;
	const bool bCanWait = !IsAsyncLoading() && FAssetCompilingManager::Get().GetNumRemainingAssets() == 0;
	if (bCanWait)
	{
		for (const UPoseSearchDatabase* Database : Requested)
		{
			if (IsValid(Database) && FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
					ERequestAsyncBuildFlag::ContinueRequest | ERequestAsyncBuildFlag::WaitForCompletion) == EAsyncBuildIndexResult::Success)
			{
				++Indexed;
			}
		}
	}
	const double End = FPlatformTime::Seconds();
	UE_LOG(LogHawkeye, Log, TEXT("%s: motion matching: %d asset(s) finished compiling in %.0f ms, %d database(s) keyed in %.0f ms, %d indexed from the cache in %.0f ms%s, %d already indexed."),
		*GetName(), Compiling, (Compiled - Start) * 1000.0, Requested.Num(), (Keyed - Compiled) * 1000.0, Indexed,
		(End - Keyed) * 1000.0, bCanWait ? TEXT("") : TEXT(" (still loading: left to the first frames)"), AlreadyIndexed);
#endif
}

void AHawkeyeGameMode::BuildNavigationIfEmpty()
{
	if (!bBuildNavigationAtStart)
	{
		return;
	}

	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem)
	{
		UE_LOG(LogHawkeye, Warning,
			TEXT("%s: no navigation system; nothing will patrol in this level."), *GetName());
		return;
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s: building navigation."), *GetName());
	const double Start = FPlatformTime::Seconds();
	NavSystem->Build();
	UE_LOG(LogHawkeye, Log, TEXT("%s: navigation built in %.0f ms."), *GetName(), (FPlatformTime::Seconds() - Start) * 1000.0);
}

void AHawkeyeGameMode::PreloadEffectsAndSounds()
{
	// A test world with no renderer draws and plays nothing; nothing to load for it.
	if (!FApp::CanEverRender())
	{
		return;
	}
	if (UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(this))
	{
		Vfx->PreloadFolderAsync(TEXT("/Game/VFX"));
	}
	if (UHawkeyeAudioSubsystem* Audio = GetWorld() ? GetWorld()->GetSubsystem<UHawkeyeAudioSubsystem>() : nullptr)
	{
		Audio->PreloadFolderAsync(TEXT("/Game/Audio"));
	}
}

void AHawkeyeGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EndPlayReason == EEndPlayReason::LevelTransition)
	{
		HawkeyeGameModeClock::LevelChangeStartSeconds = FPlatformTime::Seconds();
	}
	if (PlayableSeconds >= 0.f && (PerfLogSeconds > 0.f || StatDumpSeconds > 0.f))
	{
		FlushPerfWindow(FPlatformTime::Seconds());
	}
	if (ScreenshotProcessedHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
		ScreenshotProcessedHandle.Reset();
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RestartTimerHandle);
		World->GetTimerManager().ClearTimer(AutosaveTimerHandle);
		World->GetTimerManager().ClearTimer(NewGameSaveTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AHawkeyeGameMode::RestartMission(float Delay)
{
	if (bRestartPending)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float WaitSeconds = Delay < 0.f ? RestartDelaySeconds : Delay;

	bRestartPending = true;

	// Starting over means starting over: the quiver goes back to what the mission grants.
	if (UInventoryComponent* Inventory = FindPlayerInventory())
	{
		Inventory->Clear();
	}

	OnMissionRestarting();

	UE_LOG(LogHawkeye, Log, TEXT("%s: restarting the mission in %.1f s."), *GetName(), WaitSeconds);

	if (WaitSeconds <= 0.f)
	{
		ReopenCurrentLevel();
		return;
	}

	World->GetTimerManager().SetTimer(
		RestartTimerHandle, this, &AHawkeyeGameMode::ReopenCurrentLevel, WaitSeconds, false);
}

void AHawkeyeGameMode::ReopenCurrentLevel()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FName CurrentLevel(*UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString=*/true));
	UGameplayStatics::OpenLevel(World, CurrentLevel);
}

UInventoryComponent* AHawkeyeGameMode::FindPlayerInventory() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	return Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr;
}

void AHawkeyeGameMode::HandleMissionStarted(UMissionDefinition* Mission)
{
	ApplyChapterCast(GetWorld(), Mission);
	UInventoryComponent* Inventory = FindPlayerInventory();
	if (!Inventory || !Mission)
	{
		return;
	}

	// Nothing carries between chapters: the chapter's own data asset says which bow and arrows
	// she starts with, and everything else she has to find again.
	Inventory->ApplyMissionStart(Mission);
}

void AHawkeyeGameMode::HandleMissionComplete(UMissionDefinition* Mission)
{
	UE_LOG(LogHawkeye, Log, TEXT("Mission complete: %s"),
		Mission ? *Mission->MissionName.ToString() : TEXT("<none>"));

	// Before the end card, so the last frame of gameplay is not a quiver the player keeps. A chapter
	// that hands the player back to the district keeps it: she goes on roaming with it.
	UInventoryComponent* Inventory = FindPlayerInventory();
	if (Inventory && !(Mission && Mission->bReturnToRoamingAtEnd))
	{
		Inventory->Clear();
	}

	OnMissionCompleted(Mission);
}
