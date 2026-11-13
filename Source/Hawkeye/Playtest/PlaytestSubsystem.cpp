#include "Playtest/PlaytestSubsystem.h"

#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Challenge/ChallengeTypes.h"
#include "Combat/HealthComponent.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeSubsystem.h"
#include "Crime/CrimeTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/PlatformTime.h"
#include "Hawkeye.h"
#include "HawkeyeGameMode.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettings.h"
#include "UObject/UObjectGlobals.h"
#include "World/HawkeyeCapture.h"
#include "World/SafehouseSubsystem.h"
#include "World/ThugCharacter.h"
#include "World/TimeOfDaySubsystem.h"

/** Keeps the last RecentLogLines LogHawkeye lines for the next note. Any thread. */
class FPlaytestLogDevice : public FOutputDevice
{
public:
	FPlaytestLogRing Ring{ HawkeyePlaytest::RecentLogLines };

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Category != LogHawkeye.GetCategoryName() || !V)
		{
			return;
		}
		const ELogVerbosity::Type Level = ELogVerbosity::Type(Verbosity & ELogVerbosity::VerbosityMask);
		const FString Time = FDateTime::Now().ToString(TEXT("%H:%M:%S.%s"));
		Ring.Add(Level == ELogVerbosity::Log || Level == ELogVerbosity::Display
			? FString::Printf(TEXT("[%s] %s"), *Time, V)
			: FString::Printf(TEXT("[%s] %s: %s"), *Time, ToString(Level), V));
	}

	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
};

UPlaytestSubsystem* UPlaytestSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UPlaytestSubsystem>() : nullptr;
}

bool UPlaytestSubsystem::IsAutomationRun()
{
	return HawkeyeAutomation::IsAutomationRunOrCommandlet();
}

void UPlaytestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	StartedAt = FDateTime::Now();
	StartSeconds = FPlatformTime::Seconds();
	LastTickSeconds = StartSeconds;
	FString Requested;
	if (FParse::Value(FCommandLine::Get(), TEXT("PlaytestSession="), Requested))
	{
		SessionName = HawkeyePlaytest::SanitizeSessionName(Requested);
	}
	if (SessionName.IsEmpty())
	{
		SessionName = HawkeyePlaytest::MakeSessionName(StartedAt);
	}
	NotesFile.Session = SessionName;
	Summary.Session = SessionName;
	Summary.StartedAt = StartedAt.ToString(TEXT("%Y-%m-%d %H:%M:%S"));

	LogDevice = MakeShared<FPlaytestLogDevice>();
	if (GLog)
	{
		GLog->AddOutputDevice(LogDevice.Get());
	}
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UPlaytestSubsystem::HandlePreLoadMap);
	bInitialized = true;
}

void UPlaytestSubsystem::Deinitialize()
{
	if (IsVerboseBumpActive())
	{
		EndVerboseBump();
	}
	if (!bSummaryWrittenAtExit && WriteSummary(TEXT("exit")))
	{
		CopyGameLog();
	}
	bSummaryWrittenAtExit = true;
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	if (GLog && LogDevice)
	{
		GLog->RemoveOutputDevice(LogDevice.Get());
	}
	LogDevice.Reset();
	bInitialized = false;
	Super::Deinitialize();
}

TStatId UPlaytestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPlaytestSubsystem, STATGROUP_Tickables);
}

ETickableTickType UPlaytestSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

UWorld* UPlaytestSubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

FString UPlaytestSubsystem::GetSessionFolder() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Playtest") / SessionName) + TEXT("/");
}

FPlaytestState UPlaytestSubsystem::CaptureState(const APlayerController* PC, bool bCountThugs)
{
	FPlaytestState S;
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return S;
	}
	S.bPaused = World->IsPaused();
	S.Map = UWorld::RemovePIEPrefix(World->GetMapName());
	S.bInterior = AHawkeyeGameMode::WantsInteriorCamera(World);
	if (const UTimeOfDaySubsystem* TimeOfDay = UTimeOfDaySubsystem::Get(World))
	{
		S.TimeOfDay = StaticEnum<EHawkeyeTimeOfDay>()->GetNameStringByValue(int64(TimeOfDay->GetApplied()));
	}
	if (const UDifficultySubsystem* Difficulty = UDifficultySubsystem::Get(World))
	{
		S.Difficulty = UDifficultySubsystem::GetDifficultyName(Difficulty->GetDifficulty()).ToString();
	}
	if (const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World))
	{
		if (const UCrimeDefinition* Crime = Crimes->GetActiveDefinition())
		{
			S.Crime = Crime->GetDisplayName().ToString();
		}
	}
	if (const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World))
	{
		const UChallengeDefinition* Challenge = Challenges->IsRunning() ? Challenges->GetActiveDefinition() : nullptr;
		if (Challenge)
		{
			S.Challenge = Challenge->GetDisplayName().ToString();
		}
	}
	if (bCountThugs)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const UHealthComponent* Health = It->GetHealthComponent();
			S.AlertedThugs += Health && Health->IsAlive() && It->GetAlertState() == EThugAlertState::Alerted ? 1 : 0;
		}
	}

	const APawn* Pawn = PC->GetPawn();
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			S.Movement = Movement->GetMovementName();
			S.Speed = float(Movement->Velocity.Size2D());
			S.bCrouching = Movement->IsCrouching();
		}
	}
	if (const UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr)
	{
		S.Health = Health->GetCurrentHealth();
	}
	if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Pawn))
	{
		S.bSprinting = Hawkeye->IsSprinting();
		S.bCrouching = S.bCrouching || Hawkeye->IsSliding();
		S.bAiming = Hawkeye->IsAiming();
		S.bDowned = Hawkeye->IsDowned();
		S.bZipping = Hawkeye->IsZipping();
		if (const UParkourComponent* Parkour = Hawkeye->GetParkourComponent())
		{
			if (Parkour->IsPerformingMove())
			{
				S.Parkour = StaticEnum<EHawkeyeParkourMove>()->GetNameStringByValue(int64(Parkour->GetActiveMove()));
			}
			else if (Parkour->IsHanging())
			{
				S.Parkour = TEXT("Hanging");
			}
		}
	}
	return S;
}

int32 UPlaytestSubsystem::TakeNote(const APlayerController* PC, FOnSaved OnSaved)
{
	FPlaytestNote Note;
	Note.Index = NotesFile.Notes.Num() + 1;
	Note.SessionSeconds = float(FPlatformTime::Seconds() - StartSeconds);
	Note.WallClock = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	Note.State = CaptureState(PC, true);
	Note.StateText = HawkeyePlaytest::DescribeState(Note.State);
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn)
	{
		Note.Position = Pawn->GetActorLocation();
	}
	else if (PC && PC->PlayerCameraManager)
	{
		Note.Position = PC->PlayerCameraManager->GetCameraLocation();
	}
	if (PC)
	{
		Note.Yaw = float(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw));
	}
	Note.Screenshot = FString::Printf(TEXT("note_%d.png"), Note.Index);
	Note.ScreenshotPath = GetSessionFolder() + Note.Screenshot;
	if (LogDevice)
	{
		Note.RecentLog = LogDevice->Ring.Snapshot();
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s"), *HawkeyePlaytest::FormatNoteLine(Note));
	NotesFile.Notes.Add(Note);
	Summary.Notes = NotesFile.Notes.Num();
	SaveNotes();

	const int32 Index = Note.Index;
	const FString Path = Note.ScreenshotPath;
	HawkeyeCapture::Request(Path, /*bShowUI=*/true, this, [Index, Path, OnSaved = MoveTemp(OnSaved)](bool bOk, const FString& /*Why*/)
	{
		if (bOk)
		{
			UE_LOG(LogHawkeye, Log, TEXT("Playtest: note %d saved to %s"), Index, *Path);
		}
		if (OnSaved)
		{
			OnSaved(Index, bOk);
		}
	});
	return Index;
}

int32 UPlaytestSubsystem::TakePhoto(FOnSaved OnSaved)
{
	const int32 Index = ++Summary.Photos;
	const FString Path = GetSessionFolder() + FString::Printf(TEXT("photo_%d.png"), Index);
	HawkeyeCapture::Request(Path, /*bShowUI=*/false, this, [Index, Path, OnSaved = MoveTemp(OnSaved)](bool bOk, const FString& /*Why*/)
	{
		if (bOk)
		{
			UE_LOG(LogHawkeye, Log, TEXT("Playtest: photo %d saved to %s"), Index, *Path);
		}
		if (OnSaved)
		{
			OnSaved(Index, bOk);
		}
	});
	return Index;
}

bool UPlaytestSubsystem::SaveNotes() const
{
	const FString Path = GetSessionFolder() + TEXT("notes.json");
	const bool bSaved = FFileHelper::SaveStringToFile(HawkeyePlaytest::NotesToJson(NotesFile), *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bSaved)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("Playtest: could not write %s"), *Path);
	}
	return bSaved;
}

void UPlaytestSubsystem::StartVerboseBump(float Seconds)
{
	const double Now = FPlatformTime::Seconds();
	if (!IsVerboseBumpActive())
	{
		VerbosityBeforeBump = LogHawkeye.GetVerbosity();
		LogHawkeye.SetVerbosity(ELogVerbosity::Verbose);
	}
	VerboseBumpEndSeconds = Now + FMath::Max(0.1f, Seconds);
	UE_LOG(LogHawkeye, Log, TEXT("Playtest: LogHawkeye at Verbose for %.0f s (after note %d)."), Seconds, NotesFile.Notes.Num());
}

void UPlaytestSubsystem::EndVerboseBump()
{
	VerboseBumpEndSeconds = 0.0;
	UE_LOG(LogHawkeye, Log, TEXT("Playtest: LogHawkeye back to %s."), ToString(VerbosityBeforeBump));
	LogHawkeye.SetVerbosity(VerbosityBeforeBump);
}

bool UPlaytestSubsystem::WriteSummary(const FString& Reason, bool bForce)
{
	const bool bWanted = bForce || Summary.Notes > 0 || Summary.Photos > 0 || (IsRunningGame() && !IsAutomationRun());
	if (!bWanted)
	{
		return false;
	}
	Summary.Reason = Reason;
	Summary.WrittenAt = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	Summary.SessionSeconds = float(FPlatformTime::Seconds() - StartSeconds);
	const FString Path = GetSessionFolder() + TEXT("summary.json");
	const bool bSaved = FFileHelper::SaveStringToFile(HawkeyePlaytest::SummaryToJson(Summary), *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogHawkeye, Log, TEXT("Playtest: summary (%s) %s %s: %d notes, %d photos, %.0f s played, %d deaths, %d of %d fights won."),
		*Reason, bSaved ? TEXT("written to") : TEXT("could not be written to"), *Path, Summary.Notes, Summary.Photos,
		Summary.PlaySeconds, Summary.Deaths, Summary.FightsWon, Summary.Fights);
	return bSaved;
}

void UPlaytestSubsystem::CopyGameLog() const
{
	// Only the game's own log: a test run's -abslog or another process's file is not this session's.
	const FString GameLogPath = FPaths::ConvertRelativePathToFull(FGenericPlatformOutputDevices::GetAbsoluteLogFilename());
	const FString Expected = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir() / TEXT("Hawkeye.log"));
	if (!FPaths::IsSamePath(GameLogPath, Expected))
	{
		return;
	}
	if (GLog)
	{
		GLog->Flush();
	}
	const FString Target = GetSessionFolder() + TEXT("Hawkeye.log");
	// The log is still open for writing, so a plain copy (which asks for exclusive read) is refused: read it
	// with write sharing instead.
	TArray<uint8> Bytes;
	if (const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*GameLogPath, FILEREAD_AllowWrite)); Reader)
	{
		Bytes.SetNumUninitialized(Reader->TotalSize());
		Reader->Serialize(Bytes.GetData(), Bytes.Num());
	}
	if (Bytes.IsEmpty() || !FFileHelper::SaveArrayToFile(Bytes, *Target))
	{
		UE_LOG(LogHawkeye, Warning, TEXT("Playtest: could not copy %s to %s"), *GameLogPath, *Target);
	}
}

void UPlaytestSubsystem::HandlePreLoadMap(const FString& /*MapName*/)
{
	WriteSummary(TEXT("map change"));
	// The fight (if any) belongs to the world that is going.
	FightWatch.bInFight = false;
	FightThugs.Reset();
	CachedAlerted = 0;
}

void UPlaytestSubsystem::BindWorld(UWorld* World)
{
	BoundWorld = World;
	Summary.AddMap(UWorld::RemovePIEPrefix(World->GetMapName()));
	if (UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(World))
	{
		Crimes->OnCrimeStarted.AddUniqueDynamic(this, &UPlaytestSubsystem::HandleCrimeStarted);
		Crimes->OnCrimeEnded.AddUniqueDynamic(this, &UPlaytestSubsystem::HandleCrimeEnded);
	}
	if (UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(World))
	{
		Challenges->OnChallengeEnded.AddUniqueDynamic(this, &UPlaytestSubsystem::HandleChallengeEnded);
	}
	if (USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World))
	{
		Safehouses->OnFastTravelFinished.AddUniqueDynamic(this, &UPlaytestSubsystem::HandleFastTravelFinished);
	}
}

void UPlaytestSubsystem::HandleCrimeStarted(UCrimeDefinition* /*Crime*/)
{
	++Summary.CrimesStarted;
}

void UPlaytestSubsystem::HandleCrimeEnded(const FCrimeResult& Result)
{
	Summary.AddCrimeEnd(Result.Reason == ECrimeEndReason::Completed);
}

void UPlaytestSubsystem::HandleChallengeEnded(const FChallengeResult& Result)
{
	Summary.AddChallengeEnd(uint8(Result.Medal));
}

void UPlaytestSubsystem::HandleFastTravelFinished(FName /*SafehouseId*/, float /*Seconds*/)
{
	++Summary.FastTravels;
}

void UPlaytestSubsystem::WatchFight(UWorld* World, const APawn* Pawn, double Now)
{
	CachedAlerted = 0;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (Health && Health->IsAlive() && It->GetAlertState() == EThugAlertState::Alerted)
		{
			++CachedAlerted;
			FightThugs.Add(*It);
		}
	}
	int32 Standing = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Thug : FightThugs)
	{
		const UHealthComponent* Health = Thug.IsValid() ? Thug->GetHealthComponent() : nullptr;
		Standing += Health && Health->IsAlive() ? 1 : 0;
	}
	const UHealthComponent* PlayerHealth = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Pawn);
	const bool bStanding = PlayerHealth && PlayerHealth->IsAlive() && !(Hawkeye && Hawkeye->IsDowned());
	const FPlaytestFightWatch::EEnd End = FightWatch.Update(CachedAlerted, FightThugs.Num(), Standing, bStanding, Now);
	if (End != FPlaytestFightWatch::EEnd::None)
	{
		Summary.AddFightEnd(End == FPlaytestFightWatch::EEnd::Won);
		UE_LOG(LogHawkeye, Log, TEXT("Playtest: fight over, %s (%d thugs, %d still standing)."),
			End == FPlaytestFightWatch::EEnd::Won ? TEXT("won") : TEXT("not won"), FightThugs.Num(), Standing);
		FightThugs.Reset();
	}
}

void UPlaytestSubsystem::Tick(float /*DeltaTime*/)
{
	const double Now = FPlatformTime::Seconds();
	// Real time, clamped so a hitch or a breakpoint does not count as a minute of sprinting.
	const float Dt = float(FMath::Clamp(Now - LastTickSeconds, 0.0, 0.5));
	LastTickSeconds = Now;

	if (IsVerboseBumpActive() && Now >= VerboseBumpEndSeconds)
	{
		EndVerboseBump();
	}

	UWorld* World = GetTickableGameObjectWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	if (BoundWorld.Get() != World)
	{
		BindWorld(World);
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	FPlaytestState State = CaptureState(PC, /*bCountThugs=*/false);
	State.AlertedThugs = CachedAlerted;
	const bool bPaused = World->IsPaused();
	if (bPaused)
	{
		Summary.PausedSeconds += Dt;
	}
	else
	{
		Summary.PlaySeconds += Dt;
		Summary.AimSeconds += State.bAiming ? Dt : 0.f;
		Summary.FightSeconds += CachedAlerted > 0 ? Dt : 0.f;
	}
	Summary.AddMovement(HawkeyePlaytest::MovementKey(State), Dt);

	if (Pawn)
	{
		const FVector Location = Pawn->GetActorLocation();
		if (Pawn == LastPawn.Get() && !bPaused)
		{
			Summary.AddStep(LastPawnLocation, Location);
		}
		LastPawnLocation = Location;
	}
	const UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	const bool bDead = Health && !Health->IsAlive();
	if (Pawn != LastPawn.Get())
	{
		// A new pawn (a reload, a switch to Clint) starts its edges where it is.
		LastPawn = Pawn;
		DeadEdge.bLast = bDead;
		ZipEdge.bLast = State.bZipping;
	}
	Summary.Deaths += DeadEdge.Rise(bDead) ? 1 : 0;
	Summary.Grapples += ZipEdge.Rise(State.bZipping) ? 1 : 0;

	if (!bPaused && Now >= NextFightPollSeconds)
	{
		NextFightPollSeconds = Now + 0.25;
		WatchFight(World, Pawn, Now);
	}
}
