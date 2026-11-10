#include "Crime/CrimeSubsystem.h"

#include "Hawkeye.h"
#include "HawkeyeGameMode.h"
#include "HawkeyePlayerController.h"
#include "AIController.h"
#include "Chase/ChaseSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Challenge/ChallengeSubsystem.h"
#include "CollisionQueryParams.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Crime/Civilian.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeLoot.h"
#include "Crime/CrimeRules.h"
#include "Crime/CrimeSpot.h"
#include "Crime/CrimeTracker.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Mission/MissionFlowController.h"
#include "Mission/MissionSubsystem.h"
#include "NavigationSystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

const FName UCrimeSubsystem::MarkerSource(TEXT("crime"));
const FName UCrimeSubsystem::CrimeThugTag(TEXT("CrimeThug"));

static TAutoConsoleVariable<float> CVarCrimeInterval(TEXT("hawkeye.CrimeInterval"), 0.f,
	TEXT("Seconds of roaming between street crimes. 0 uses the 90 to 150 s roll. Above 0 also lets crimes start in automation runs."),
	ECVF_Cheat);

static TAutoConsoleVariable<FString> CVarCrimeType(TEXT("hawkeye.CrimeType"), TEXT(""),
	TEXT("Forces the street crimes' type: mugging, robbery, ambush or rooftop. Empty picks any."), ECVF_Cheat);

namespace HawkeyeCrime
{
	/** How far to look for the navmesh under a spawn point, cm. */
	static const FVector NavExtent(200.f, 200.f, 300.f);
	/** A thug within this of the victim, not fighting the player, is on him, cm. */
	static constexpr float OnVictimRadius = 400.f;
	/** The marker floats this far over the crime point, cm. */
	static constexpr float MarkerUp = 150.f;
	/** Degrees either side of the camera's forward that count as in sight. */
	static constexpr float SightHalfAngle = 60.f;
	/** The runner is sent on again if he has stopped for this long, s. */
	static constexpr double RunnerRepathSeconds = 1.0;
	/** Mugging and robbery stand their thugs this close round the victim or the loot, cm. */
	static constexpr float CloseRing = 160.f;
}

UCrimeSubsystem* UCrimeSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UCrimeSubsystem>() : nullptr;
}

float UCrimeSubsystem::GetIntervalOverride()
{
	return FMath::Max(CVarCrimeInterval.GetValueOnGameThread(), 0.f);
}

bool UCrimeSubsystem::GetForcedType(ECrimeType& OutType)
{
	return UCrimeRules::ParseType(CVarCrimeType.GetValueOnGameThread(), OutType);
}

bool UCrimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCrimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Random.GenerateNewSeed();
	Tracker = NewObject<UCrimeTracker>(this, TEXT("CrimeTracker"));
	Tracker->OnCrimeEnded.AddUObject(this, &UCrimeSubsystem::HandleCrimeEnded);
	const float Override = GetIntervalOverride();
	bIntervalFromOverride = Override > 0.f;
	Schedule.Restart(UCrimeRules::RollInterval(MinIntervalSeconds, MaxIntervalSeconds, Override, Random));
}

void UCrimeSubsystem::Deinitialize()
{
	OnCrimeStarted.Clear();
	OnCrimeEnded.Clear();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UCrimeSubsystem::HandleCampaignLoaded);
	}
	// The world is going: no toast, no reward, just let go of everything.
	Thugs.Reset();
	Leftovers.Reset();
	if (Tracker)
	{
		Tracker->Reset();
		Tracker = nullptr;
	}
	Super::Deinitialize();
}

void UCrimeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RestoreFromCampaign();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UCrimeSubsystem::HandleCampaignLoaded);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %d crime spot(s); first crime after %.0f s of roaming."), *GetName(), GetSpots().Num(),
		Schedule.Interval);
}

TStatId UCrimeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCrimeSubsystem, STATGROUP_Tickables);
}

// --- State ---------------------------------------------------------------------------------------

bool UCrimeSubsystem::IsCrimeActive() const
{
	return Tracker && Tracker->IsActive();
}

bool UCrimeSubsystem::IsCrimePaused() const
{
	return IsCrimeActive() && (IsInChallenge() || IsInChapterBeat());
}

UCrimeDefinition* UCrimeSubsystem::GetActiveDefinition() const
{
	return Tracker ? Tracker->GetActiveDefinition() : nullptr;
}

int32 UCrimeSubsystem::GetCompletions(ECrimeType Type) const
{
	return Tracker ? Tracker->GetCompletions(Type) : 0;
}

TArray<ACrimeSpot*> UCrimeSubsystem::GetSpots() const
{
	TArray<ACrimeSpot*> Spots;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ACrimeSpot> It(World); It; ++It)
		{
			Spots.Add(*It);
		}
	}
	return Spots;
}

APawn* UCrimeSubsystem::FindPlayer() const
{
	if (APawn* Override = PlayerOverride.Get())
	{
		return Override;
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

bool UCrimeSubsystem::IsPlayerOut(const APawn* Player, bool bDownCounts)
{
	if (!IsValid(Player))
	{
		return true;
	}
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Player);
	const UHealthComponent* Health = Hawkeye ? Hawkeye->GetHealthComponent() : nullptr;
	return Hawkeye && ((Health && !Health->IsAlive()) || (bDownCounts && Hawkeye->IsDowned()));
}

bool UCrimeSubsystem::IsSchedulingAllowed()
{
	if (GetIntervalOverride() > 0.f)
	{
		return true;
	}
	// Laps and screenshot passes drive the district themselves; four thugs turning up would spoil them.
	return AHawkeyePlayerController::bAutomationPlaysAsPlayer
		|| (!GIsAutomationTesting && FCString::Stristr(FCommandLine::Get(), TEXT("RunTests")) == nullptr);
}

bool UCrimeSubsystem::IsInChallenge() const
{
	const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	if (Challenges && Challenges->IsRunning())
	{
		return true;
	}
	const UWorld* World = GetWorld();
	const AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	return PC && PC->IsChallengeResultsOpen();
}

bool UCrimeSubsystem::IsInChapterBeat() const
{
	if (ChapterBeatOverride.IsSet())
	{
		return ChapterBeatOverride.GetValue();
	}
	const UWorld* World = GetWorld();
	if (const UChaseSubsystem* Chase = World ? World->GetSubsystem<UChaseSubsystem>() : nullptr; Chase && Chase->IsChaseRunning())
	{
		return true;
	}
	const AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC)
	{
		return false;
	}
	if (PC->IsCloseUpActive() || PC->IsChapterTitleShowing() || PC->IsFlashbackActive() || PC->IsMainMenuOpen()
		|| PC->IsDifficultyPromptOpen())
	{
		return true;
	}
	const UMissionFlowController* Flow = PC->GetMissionFlow();
	const EMissionFlowStep Step = Flow ? Flow->GetStep() : EMissionFlowStep::Idle;
	return Step != EMissionFlowStep::Idle && Step != EMissionFlowStep::Done;
}

bool UCrimeSubsystem::IsInPlayerSight(const FVector& Point, const APawn* Player) const
{
	const UWorld* World = GetWorld();
	if (!World || !Player)
	{
		return false;
	}
	const APlayerController* PC = Cast<APlayerController>(Player->GetController());
	FVector Eye = Player->GetPawnViewLocation();
	FVector Forward = Player->GetControlRotation().Vector();
	if (PC && PC->PlayerCameraManager)
	{
		Eye = PC->PlayerCameraManager->GetCameraLocation();
		Forward = PC->PlayerCameraManager->GetCameraRotation().Vector();
	}
	const FVector To = Point - Eye;
	if (FVector::DotProduct(To.GetSafeNormal(), Forward) < FMath::Cos(FMath::DegreesToRadians(HawkeyeCrime::SightHalfAngle)))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrimeSight), false, Player);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params);
}

FVector UCrimeSubsystem::GetCrimePoint() const
{
	const UCrimeDefinition* Definition = GetActiveDefinition();
	if (const ACrimeLoot* Bag = Loot.Get(); Bag && !Bag->IsRecovered())
	{
		return Bag->GetActorLocation();
	}
	if (const ACivilian* Civilian = Victim.Get(); Civilian && Definition && Definition->Type == ECrimeType::Mugging)
	{
		return Civilian->GetActorLocation();
	}
	FVector Sum = FVector::ZeroVector;
	int32 Standing = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
	{
		const AThugCharacter* Thug = Weak.Get();
		if (Thug && !Thug->IsLimp() && Thug->GetHealthComponent()->IsAlive())
		{
			Sum += Thug->GetActorLocation();
			++Standing;
		}
	}
	if (Standing > 0)
	{
		return Sum / Standing;
	}
	const ACrimeSpot* Spot = ActiveSpot.Get();
	return Spot ? Spot->GetActorLocation() : (Tracker ? Tracker->GetSpotLocation() : FVector::ZeroVector);
}

int32 UCrimeSubsystem::CountThugsDown() const
{
	int32 Down = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
	{
		const AThugCharacter* Thug = Weak.Get();
		Down += (!Thug || Thug->IsLimp() || !Thug->GetHealthComponent()->IsAlive()) ? 1 : 0;
	}
	return Down;
}

int32 UCrimeSubsystem::CountThugsOnVictim() const
{
	const ACivilian* Civilian = Victim.Get();
	if (!Civilian || Civilian->IsFreed())
	{
		return 0;
	}
	int32 On = 0;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
	{
		const AThugCharacter* Thug = Weak.Get();
		if (Thug && !Thug->IsLimp() && Thug->GetHealthComponent()->IsAlive() && !Thug->IsIncapacitated() && !Thug->IsAlerted()
			&& FVector::Dist2D(Thug->GetActorLocation(), Civilian->GetActorLocation()) <= HawkeyeCrime::OnVictimRadius)
		{
			++On;
		}
	}
	return On;
}

// --- Tick ----------------------------------------------------------------------------------------

void UCrimeSubsystem::Tick(float DeltaTime)
{
	NowSeconds += DeltaTime;
	UpdateLeftovers();
	if (IsCrimeActive())
	{
		if (!IsCrimePaused())
		{
			UpdateCrime(DeltaTime);
		}
		RefreshMarker();
		return;
	}
	UpdateSchedule(DeltaTime);
}

void UCrimeSubsystem::UpdateSchedule(float DeltaSeconds)
{
	if (!IsSchedulingAllowed())
	{
		return;
	}
	// The cvar takes effect at once, and letting it go rolls a normal interval again.
	const float Override = GetIntervalOverride();
	if (Override > 0.f && !FMath::IsNearlyEqual(Schedule.Interval, Override))
	{
		Schedule.Interval = Override;
		bIntervalFromOverride = true;
	}
	else if (Override <= 0.f && bIntervalFromOverride)
	{
		Schedule.Restart(UCrimeRules::RollInterval(MinIntervalSeconds, MaxIntervalSeconds, 0.f, Random));
		bIntervalFromOverride = false;
	}
	const APawn* Player = FindPlayer();
	const UWorld* World = GetWorld();
	const UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this);
	const bool bRoaming = UCrimeRules::IsRoaming(false, AHawkeyeGameMode::IsWorldInCombat(World), IsInChallenge(),
		IsInChapterBeat() || (Dialogue && Dialogue->IsBusy()), IsPlayerOut(Player, /*bDownCounts=*/true));
	if (Schedule.Advance(DeltaSeconds, bRoaming) && bRoaming && NowSeconds >= NextTryAt)
	{
		if (!TryStartDueCrime())
		{
			NextTryAt = NowSeconds + RetrySeconds;
		}
	}
}

bool UCrimeSubsystem::TryStartDueCrime()
{
	APawn* Player = FindPlayer();
	if (!Player || IsCrimeActive())
	{
		return false;
	}
	ECrimeType Forced = ECrimeType::Mugging;
	const bool bForced = GetForcedType(Forced);
	TArray<ACrimeSpot*> Usable;
	TArray<FCrimeSpotCandidate> Candidates;
	for (ACrimeSpot* Spot : GetSpots())
	{
		const bool bHasCrime = bForced ? Spot->FindCrime(Forced) != nullptr : Spot->Crimes.ContainsByPredicate(
			[](const UCrimeDefinition* Crime) { return Crime != nullptr; });
		if (!bHasCrime)
		{
			continue;
		}
		FCrimeSpotCandidate Candidate;
		Candidate.Location = Spot->GetActorLocation();
		Candidate.bInSight = IsInPlayerSight(Candidate.Location + FVector(0.f, 0.f, 120.f), Player);
		Candidates.Add(Candidate);
		Usable.Add(Spot);
	}
	const int32 Index = UCrimeRules::PickSpot(Candidates, Player->GetActorLocation(), bHasLastSpot, LastSpotLocation, SpawnMinDistance,
		SpawnMaxDistance, LastCrimeApart, Random);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: a crime is due but none of %d spot(s) is 25 to 40 m out; waiting."), *GetName(), Usable.Num());
		return false;
	}
	ACrimeSpot* Spot = Usable[Index];
	TArray<UCrimeDefinition*> Choices;
	for (UCrimeDefinition* Crime : Spot->Crimes)
	{
		if (Crime && (!bForced || Crime->Type == Forced))
		{
			Choices.Add(Crime);
		}
	}
	UCrimeDefinition* Definition = Choices[Random.RandRange(0, Choices.Num() - 1)];
	UE_LOG(LogHawkeye, Log, TEXT("%s: a crime is due: %s at %s (%.0f m, %s)."), *GetName(), *Definition->Id.ToString(), *Spot->GetName(),
		FVector::Dist2D(Spot->GetActorLocation(), Player->GetActorLocation()) / 100.f,
		Candidates[Index].bInSight ? TEXT("in her sight: none was hidden") : TEXT("out of her sight"));
	return StartCrimeAt(Spot, Definition, Player);
}

// --- Starting --------------------------------------------------------------------------------------

bool UCrimeSubsystem::StartCrimeAt(ACrimeSpot* Spot, UCrimeDefinition* Definition, APawn* Player)
{
	if (!Tracker || !Spot || !Definition || !IsValid(Player) || IsCrimeActive() || !GetWorld())
	{
		return false;
	}
	Thugs.Reset();
	Victim = nullptr;
	Loot = nullptr;
	Runner = nullptr;
	bRunnerSetOff = false;
	ActiveSpot = Spot;
	CrimePlayer = Player;
	const FVector Centre = Spot->GetActorLocation();
	SpawnProps(Definition, Spot);
	SpawnRoster(Definition, Centre, Spot->GetActorRotation().Yaw, Player, Spot->bAlley);
	if (!Tracker->StartCrime(Definition, Centre, Thugs.Num()))
	{
		return false;
	}
	if (ACrimeLoot* Bag = Loot.Get(); Bag && Thugs.Num() > 0)
	{
		Runner = Thugs[0];
		Bag->SetCarrier(Runner.Get());
	}
	EscapeLocation = Spot->HasEscapeLocation() ? Spot->EscapeLocation
		: Centre + Spot->GetActorRightVector() * Definition->EscapeDistance;
	bMarkerShown = false;
	RefreshMarker();
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s started at %s (%s) for %s: %d thug(s)%s%s."), *GetName(), *Definition->Id.ToString(),
		*Spot->GetName(), *Centre.ToCompactString(), *Player->GetName(), Thugs.Num(), Victim.IsValid() ? TEXT(", a victim") : TEXT(""),
		Loot.IsValid() ? TEXT(", the loot") : TEXT(""));
	OnCrimeStarted.Broadcast(Definition);
	return true;
}

void UCrimeSubsystem::SpawnProps(const UCrimeDefinition* Definition, const ACrimeSpot* Spot)
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector Feet = Spot->GetActorLocation();
	const FRotator Facing(0.f, Spot->GetActorRotation().Yaw, 0.f);
	if (Definition->Type == ECrimeType::Mugging)
	{
		UClass* Class = Definition->VictimClass.IsNull() ? nullptr : Definition->VictimClass.LoadSynchronous();
		Class = Class ? Class : ACivilian::StaticClass();
		const float HalfHeight = Class->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		ACivilian* Civilian = World->SpawnActor<ACivilian>(Class, Feet + FVector(0.f, 0.f, HalfHeight + 2.f), Facing, Params);
		if (Civilian)
		{
			Civilian->SetCowering(true);
		}
		Victim = Civilian;
	}
	else if (Definition->Type == ECrimeType::Robbery)
	{
		UClass* Class = Definition->LootClass.IsNull() ? nullptr : Definition->LootClass.LoadSynchronous();
		Loot = World->SpawnActor<ACrimeLoot>(Class ? Class : ACrimeLoot::StaticClass(), Feet + FVector(0.f, 0.f, 20.f), Facing, Params);
	}
}

void UCrimeSubsystem::SpawnRoster(const UCrimeDefinition* Definition, const FVector& Centre, float Yaw, APawn* Player, bool bAlley)
{
	const int32 Count = Definition->GetThugCount();
	const bool bRound = Definition->Type == ECrimeType::Mugging || Definition->Type == ECrimeType::Robbery;
	const float Ring = bRound ? FMath::Min(Definition->Radius, HawkeyeCrime::CloseRing) : Definition->Radius;
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	int32 Index = 0;
	for (const FCrimeRosterEntry& Entry : Definition->Roster)
	{
		for (int32 N = 0; N < Entry.Count; ++N, ++Index)
		{
			// Round the victim or the loot, facing in; otherwise spread round the spot, facing the player. In an
			// alley the spread is a line along the passage, so nobody stands in a wall.
			const float Angle = Yaw + 180.f + 360.f * Index / FMath::Max(Count, 1) + (bRound ? 0.f : 30.f);
			const FVector Out = FRotator(0.f, Angle, 0.f).Vector();
			FVector Feet = Centre + Out * Ring;
			if (bAlley && !bRound)
			{
				const FVector2D Offset = UCrimeRules::AlleyRosterOffset(Index, Count, Ring);
				const FRotator Along(0.f, Yaw, 0.f);
				Feet = Centre + Along.Vector() * Offset.X + FRotationMatrix(Along).GetUnitAxis(EAxis::Y) * Offset.Y;
			}
			FNavLocation OnNav;
			if (Nav && Nav->ProjectPointToNavigation(Feet, OnNav, HawkeyeCrime::NavExtent))
			{
				Feet = OnNav.Location;
			}
			const FVector Look = bRound ? Centre - Feet : Player->GetActorLocation() - Feet;
			AThugCharacter* Thug = SpawnThug(Entry, Feet, Look.Rotation().Yaw);
			if (!Thug)
			{
				continue;
			}
			Thugs.Add(Thug);
			AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController());
			if (Brain && Definition->bAlertOnStart)
			{
				Brain->SetTarget(Player);
				Brain->ReportStimulus(EStimulusKind::Hearing, Player->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
			}
		}
	}
}

AThugCharacter* UCrimeSubsystem::SpawnThug(const FCrimeRosterEntry& Entry, const FVector& Feet, float Yaw)
{
	UWorld* World = GetWorld();
	UClass* Class = Entry.ThugClass.IsNull() ? nullptr : Entry.ThugClass.LoadSynchronous();
	Class = Class ? Class : AThugCharacter::StaticClass();
	const float HalfHeight = Class->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform At(FRotator(0.f, Yaw, 0.f), Feet + FVector(0.f, 0.f, HalfHeight + 2.f));
	AThugCharacter* Thug = World->SpawnActorDeferred<AThugCharacter>(Class, At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Thug)
	{
		return nullptr;
	}
	Thug->Weapon = Entry.Weapon;
	Thug->bSkipSave = true;
	Thug->Tags.AddUnique(CrimeThugTag);
	Thug->FinishSpawning(At);
	// A bare class (tests) comes with the engine's AAIController; every thug needs his own brain.
	if (!Cast<AThugAIController>(Thug->GetController()))
	{
		if (AController* Other = Thug->GetController())
		{
			Other->UnPossess();
			Other->Destroy();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AThugAIController* Brain = World->SpawnActor<AThugAIController>(AThugAIController::StaticClass(), At, Params))
		{
			Brain->Possess(Thug);
		}
	}
	return Thug;
}

// --- Running ---------------------------------------------------------------------------------------

void UCrimeSubsystem::UpdateCrime(float DeltaSeconds)
{
	const UCrimeDefinition* Definition = GetActiveDefinition();
	APawn* Player = CrimePlayer.IsValid() ? CrimePlayer.Get() : FindPlayer();
	FCrimeTickState State;
	State.ThugsDown = CountThugsDown();
	State.ThugsOnVictim = CountThugsOnVictim();
	State.bPlayerOut = IsPlayerOut(Player, /*bDownCounts=*/false);
	State.PlayerDistance = Player ? FVector::Dist2D(Player->GetActorLocation(), Tracker->GetSpotLocation()) : 0.f;
	if (Definition->Type == ECrimeType::Robbery)
	{
		UpdateRobbery(State);
	}
	const int32 HitsBefore = Tracker->GetVictimHits();
	if (Tracker->Advance(DeltaSeconds, State))
	{
		return;
	}
	DealVictimHits(HitsBefore, Tracker->GetVictimHits());
}

void UCrimeSubsystem::DealVictimHits(int32 Before, int32 After)
{
	ACivilian* Civilian = Victim.Get();
	if (!Civilian || After <= Before)
	{
		return;
	}
	AThugCharacter* Nearest = nullptr;
	float Best = BIG_NUMBER;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
	{
		AThugCharacter* Thug = Weak.Get();
		const float Distance = Thug ? FVector::Dist2D(Thug->GetActorLocation(), Civilian->GetActorLocation()) : BIG_NUMBER;
		if (Thug && !Thug->IsLimp() && Distance < Best)
		{
			Best = Distance;
			Nearest = Thug;
		}
	}
	for (int32 Hit = Before; Hit < After; ++Hit)
	{
		Civilian->TakeHit(Nearest);
	}
	if (Nearest)
	{
		// Turn to him for the blow; there is no swing animation, so the victim's flinch carries it.
		const FVector To = Civilian->GetActorLocation() - Nearest->GetActorLocation();
		Nearest->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}
}

void UCrimeSubsystem::UpdateRobbery(FCrimeTickState& State)
{
	const UCrimeDefinition* Definition = GetActiveDefinition();
	ACrimeLoot* Bag = Loot.Get();
	AThugCharacter* Carrier = Runner.Get();
	const APawn* Player = CrimePlayer.Get();
	if (!Definition || !Bag)
	{
		return;
	}
	AThugAIController* Brain = Carrier ? Cast<AThugAIController>(Carrier->GetController()) : nullptr;
	if (Bag->IsCarried())
	{
		const bool bRunnerDown = !Carrier || Carrier->IsLimp() || !Carrier->GetHealthComponent()->IsAlive() || Carrier->IsIncapacitated()
			|| (Brain && Brain->IsHeld());
		if (bRunnerDown)
		{
			Bag->SetCarrier(nullptr);
			if (Brain)
			{
				Brain->SetThinkingEnabled(true);
			}
			UE_LOG(LogHawkeye, Log, TEXT("%s: %s dropped the loot at %s."), *GetName(), *GetNameSafe(Carrier), *Bag->GetActorLocation().ToCompactString());
		}
		else
		{
			bool bAnyAlerted = false;
			for (const TWeakObjectPtr<AThugCharacter>& Weak : Thugs)
			{
				bAnyAlerted |= Weak.IsValid() && Weak->IsAlerted();
			}
			const bool bPlayerClose = Player && FVector::Dist2D(Player->GetActorLocation(), Carrier->GetActorLocation()) <= Definition->RunnerStartDistance;
			if (!bRunnerSetOff && (bAnyAlerted || bPlayerClose || Tracker->GetElapsedSeconds() >= Definition->TimeToFailSeconds))
			{
				bRunnerSetOff = true;
				RunnerMoveAt = 0.0;
				if (Brain)
				{
					Brain->SetThinkingEnabled(false);
				}
				Carrier->GetCharacterMovement()->MaxWalkSpeed = Definition->RunnerSpeed;
				UE_LOG(LogHawkeye, Log, TEXT("%s: %s runs for %s with the loot."), *GetName(), *Carrier->GetName(), *EscapeLocation.ToCompactString());
			}
			if (bRunnerSetOff && Brain && NowSeconds >= RunnerMoveAt && Carrier->GetVelocity().Size2D() < 50.f)
			{
				Brain->MoveToLocation(EscapeLocation, Definition->EscapeRadius * 0.4f, false, true, true, false);
				RunnerMoveAt = NowSeconds + HawkeyeCrime::RunnerRepathSeconds;
			}
			State.bLootEscaped = bRunnerSetOff && FVector::Dist2D(Carrier->GetActorLocation(), EscapeLocation) <= Definition->EscapeRadius;
		}
	}
	if (!Bag->IsCarried() && !Bag->IsRecovered() && Player
		&& FVector::Dist2D(Player->GetActorLocation(), Bag->GetActorLocation()) <= Definition->LootPickupRadius
		&& FMath::Abs(Player->GetActorLocation().Z - Bag->GetActorLocation().Z) <= 200.f)
	{
		Bag->Recover();
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s picked up the loot."), *GetName(), *Player->GetName());
	}
	State.bLootRecovered = Bag->IsRecovered();
}

void UCrimeSubsystem::RefreshMarker()
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Missions)
	{
		return;
	}
	// A challenge marks its own targets; the crime's marker waits with the crime.
	if (!IsCrimeActive() || IsCrimePaused())
	{
		if (bMarkerShown)
		{
			Missions->ClearSecondaryMarkers(MarkerSource);
			bMarkerShown = false;
		}
		return;
	}
	Missions->SetSecondaryMarkers(MarkerSource, { GetCrimePoint() + FVector(0.f, 0.f, HawkeyeCrime::MarkerUp) });
	bMarkerShown = true;
}

// --- Ending ----------------------------------------------------------------------------------------

void UCrimeSubsystem::AbortCrime()
{
	if (Tracker)
	{
		Tracker->AbortCrime();
	}
}

void UCrimeSubsystem::HandleCrimeEnded(const FCrimeResult& Result)
{
	const UCrimeDefinition* Definition = ActiveSpot.IsValid() ? ActiveSpot->FindCrime(Result.Type) : nullptr;
	APawn* Player = CrimePlayer.Get();
	RefreshMarker();
	if (AThugCharacter* Carrier = Runner.Get())
	{
		if (AThugAIController* Brain = Cast<AThugAIController>(Carrier->GetController()))
		{
			Brain->SetThinkingEnabled(true);
		}
	}
	int32 Arrows = 0;
	if (Result.IsCompleted())
	{
		UInventoryComponent* Inventory = Player ? Player->FindComponentByClass<UInventoryComponent>() : nullptr;
		const int32 Want = Definition && Definition->Reward == ECrimeReward::ArrowRefill ? Definition->RewardArrows : 0;
		// Slot 1's own arrow when she has one, so the reward lands on the stack the hotbar shows.
		UArrowDefinition* Standard = Inventory ? Inventory->GetArrowSlot(1).Arrow.Get() : nullptr;
		Standard = Standard ? Standard : (Inventory ? Inventory->GetStandardArrowDefinition() : nullptr);
		Arrows = Inventory && Standard && Want > 0 ? Inventory->AddArrows(Standard, Want) : 0;
		Tracker->NoteArrowsGiven(Arrows);
		if (ACivilian* Civilian = Victim.Get())
		{
			Civilian->Free(Player);
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s %s; +%d arrow(s)."), *GetName(), *Result.CrimeId.ToString(),
		*UCrimeRules::EndReasonText(Result.Reason).ToString(), Arrows);

	MirrorToCampaign();
	if (Result.IsCompleted())
	{
		if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
		{
			Save->SaveCampaign(TEXT("crime"));
		}
	}
	UHawkeyeHudWidget* Hud = bShowToast && Player ? AHawkeyePlayerController::GetHawkeyeHudFor(Player) : nullptr;
	if (UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr)
	{
		const FText Title = Arrows > 0 ? FText::Format(NSLOCTEXT("Hawkeye", "CrimeToastArrows", "{0}   +{1} arrows"), Result.Name, Arrows)
			: Result.Name;
		Toasts->PushToast(UCrimeRules::EndReasonText(Result.Reason), Title);
	}

	// Everything the crime put in the world is cleaned up later, by the rules.
	TArray<TWeakObjectPtr<AActor>> Left;
	for (const TWeakObjectPtr<AThugCharacter>& Thug : Thugs)
	{
		Left.Add(TWeakObjectPtr<AActor>(Thug.Get()));
	}
	Left.Add(TWeakObjectPtr<AActor>(Victim.Get()));
	Left.Add(TWeakObjectPtr<AActor>(Loot.Get()));
	for (const TWeakObjectPtr<AActor>& Actor : Left)
	{
		if (Actor.IsValid())
		{
			FLeftover Entry;
			Entry.Actor = Actor;
			Entry.EndedAt = NowSeconds;
			Entry.bCompleted = Result.IsCompleted();
			if (Definition)
			{
				Entry.CompleteDelay = Definition->DespawnAfterCompleteSeconds;
				Entry.FailDelay = Definition->DespawnAfterFailSeconds;
				Entry.FarDistance = Definition->DespawnFarDistance;
			}
			Leftovers.Add(Entry);
		}
	}
	bHasLastSpot = true;
	LastSpotLocation = Tracker->GetSpotLocation();
	const float Override = GetIntervalOverride();
	bIntervalFromOverride = Override > 0.f;
	Schedule.Restart(UCrimeRules::RollInterval(MinIntervalSeconds, MaxIntervalSeconds, Override, Random));
	Thugs.Reset();
	Victim = nullptr;
	Loot = nullptr;
	Runner = nullptr;
	ActiveSpot = nullptr;
	bRunnerSetOff = false;
	OnCrimeEnded.Broadcast(Tracker->GetLastResult());
}

void UCrimeSubsystem::UpdateLeftovers()
{
	const APawn* Player = FindPlayer();
	for (int32 Index = Leftovers.Num() - 1; Index >= 0; --Index)
	{
		const FLeftover& Entry = Leftovers[Index];
		AActor* Actor = Entry.Actor.Get();
		if (!Actor)
		{
			Leftovers.RemoveAtSwap(Index);
			continue;
		}
		const float Distance = Player ? FVector::Dist(Player->GetActorLocation(), Actor->GetActorLocation()) : BIG_NUMBER;
		if (UCrimeRules::ShouldDespawn(NowSeconds - Entry.EndedAt, Entry.bCompleted, Distance, Entry.CompleteDelay, Entry.FailDelay,
			Entry.FarDistance))
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: %s cleaned up %.0f s after its crime ended (%.0f m away)."), *GetName(), *Actor->GetName(),
				NowSeconds - Entry.EndedAt, Distance / 100.f);
			Actor->Destroy();
			Leftovers.RemoveAtSwap(Index);
		}
	}
}

void UCrimeSubsystem::DespawnLeftoversNow()
{
	for (const FLeftover& Entry : Leftovers)
	{
		if (AActor* Actor = Entry.Actor.Get())
		{
			Actor->Destroy();
		}
	}
	Leftovers.Reset();
}

// --- Save ------------------------------------------------------------------------------------------

void UCrimeSubsystem::MirrorToCampaign() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (Campaign && Tracker)
	{
		Tracker->ExportCompletions(Campaign->CrimeTypes, Campaign->CrimeCompletions);
	}
}

void UCrimeSubsystem::RestoreFromCampaign()
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (Campaign && Tracker)
	{
		Tracker->ImportCompletions(Campaign->CrimeTypes, Campaign->CrimeCompletions);
	}
}

void UCrimeSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		RestoreFromCampaign();
	}
}
