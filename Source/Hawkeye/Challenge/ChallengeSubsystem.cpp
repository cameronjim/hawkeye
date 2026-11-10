#include "Challenge/ChallengeSubsystem.h"

#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "Challenge/ChallengeCheckpoint.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeRules.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeTarget.h"
#include "Challenge/ChallengeTracker.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Mission/MissionSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

const FName UChallengeSubsystem::MarkerSource(TEXT("challenge"));
const FName UChallengeSubsystem::StartMarkerSource(TEXT("challenge_start"));

namespace HawkeyeChallenges
{
	/** A finished course's targets stay this long, so the last one is seen to fall, s. */
	static constexpr float TargetLingerSeconds = 1.5f;
}

UChallengeSubsystem* UChallengeSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UChallengeSubsystem>() : nullptr;
}

bool UChallengeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChallengeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Tracker = NewObject<UChallengeTracker>(this, TEXT("ChallengeTracker"));
	Tracker->OnRunEnded.AddUObject(this, &UChallengeSubsystem::HandleRunEnded);
}

void UChallengeSubsystem::Deinitialize()
{
	OnChallengeStarted.Clear();
	OnChallengeEnded.Clear();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UChallengeSubsystem::HandleCampaignLoaded);
	}
	// The world is going: no results card, no reward, just let go of everything.
	ReleaseThugs();
	Targets.Reset();
	Checkpoints.Reset();
	if (Tracker)
	{
		Tracker->Reset();
		Tracker = nullptr;
	}
	Super::Deinitialize();
}

void UChallengeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RestoreFromCampaign();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UChallengeSubsystem::HandleCampaignLoaded);
	}
}

TStatId UChallengeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UChallengeSubsystem, STATGROUP_Tickables);
}

bool UChallengeSubsystem::IsRunning() const
{
	return Tracker && Tracker->IsRunning();
}

UChallengeDefinition* UChallengeSubsystem::GetActiveDefinition() const
{
	return Tracker ? Tracker->GetActiveDefinition() : nullptr;
}

FChallengeRecord UChallengeSubsystem::GetRecord(FName ChallengeId) const
{
	return Tracker ? Tracker->GetRecord(ChallengeId) : FChallengeRecord();
}

bool UChallengeSubsystem::IsPlayerOut() const
{
	const APawn* Pawn = Challenger.Get();
	if (!IsValid(Pawn))
	{
		return true;
	}
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Pawn);
	const UHealthComponent* Health = Hawkeye ? Hawkeye->GetHealthComponent() : nullptr;
	return Hawkeye && ((Health && !Health->IsAlive()) || Hawkeye->IsDowned());
}

bool UChallengeSubsystem::StartChallenge(AChallengeStart* Start, APawn* Player)
{
	UChallengeDefinition* Definition = Start ? Start->Definition.Get() : nullptr;
	if (!Tracker || !Definition || !IsValid(Player) || IsRunning())
	{
		return false;
	}
	Challenger = Player;
	if (IsPlayerOut())
	{
		Challenger = nullptr;
		return false;
	}
	if (!Tracker->StartRun(Definition))
	{
		return false;
	}
	LastStart = Start;
	ClearChallengeMarker();
	SpawnCourse(Definition);
	CalmThugs(Definition);
	ShownProgress = INDEX_NONE;
	RefreshCourse();
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s started %s at %s: %d targets, %d checkpoints, %d thugs calmed."), *GetName(),
		*Player->GetName(), *Definition->Id.ToString(), *Start->GetName(), Targets.Num(), Checkpoints.Num(), CalmedThugs.Num());
	OnChallengeStarted.Broadcast(Definition);
	return true;
}

TArray<AChallengeStart*> UChallengeSubsystem::GetStarts() const
{
	TArray<AChallengeStart*> Out;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AChallengeStart> It(World); It; ++It)
		{
			if (IsValid(*It) && It->Definition)
			{
				Out.Add(*It);
			}
		}
	}
	return Out;
}

AChallengeStart* UChallengeSubsystem::FindNearestStart(const FVector& From) const
{
	AChallengeStart* Nearest = nullptr;
	float Best = TNumericLimits<float>::Max();
	for (AChallengeStart* Start : GetStarts())
	{
		const float Distance = FVector::DistSquared(Start->GetActorLocation(), From);
		if (Distance < Best)
		{
			Best = Distance;
			Nearest = Start;
		}
	}
	return Nearest;
}

AChallengeStart* UChallengeSubsystem::FindUnnoticedStart(const FVector& From, float Radius, const TArray<FName>& Noticed) const
{
	AChallengeStart* Nearest = nullptr;
	float Best = FMath::Square(Radius);
	for (AChallengeStart* Start : GetStarts())
	{
		const float Distance = FVector::DistSquared(Start->GetActorLocation(), From);
		if (Distance <= Best && !Noticed.Contains(Start->Definition->Id))
		{
			Best = Distance;
			Nearest = Start;
		}
	}
	return Nearest;
}

AChallengeStart* UChallengeSubsystem::MarkNearestChallenge(const FVector& From)
{
	AChallengeStart* Nearest = FindNearestStart(From);
	return MarkChallenge(Nearest) ? Nearest : nullptr;
}

bool UChallengeSubsystem::MarkChallenge(AChallengeStart* Start)
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!IsValid(Start) || !Start->Definition || !Missions)
	{
		return false;
	}
	MarkedStart = Start;
	Missions->SetSecondaryMarkers(StartMarkerSource, { Start->GetActorLocation() + FVector(0.f, 0.f, StartMarkerUp) });
	UE_LOG(LogHawkeye, Log, TEXT("%s: marked challenge %s at %s."), *GetName(), *Start->Definition->Id.ToString(), *Start->GetName());
	return true;
}

void UChallengeSubsystem::ClearChallengeMarker()
{
	if (!MarkedStart.IsValid())
	{
		return;
	}
	MarkedStart.Reset();
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->ClearSecondaryMarkers(StartMarkerSource);
	}
}

void UChallengeSubsystem::AbortChallenge()
{
	if (Tracker)
	{
		Tracker->AbortRun();
	}
}

bool UChallengeSubsystem::RetryLastChallenge()
{
	AChallengeStart* Start = LastStart.Get();
	APawn* Pawn = Challenger.Get();
	if (!IsValid(Pawn))
	{
		const UWorld* World = GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		Pawn = PC ? PC->GetPawn() : nullptr;
	}
	if (!Start || !Pawn || IsRunning())
	{
		return false;
	}
	// Back to the pedestal, facing out along the course, as if she had walked up and pressed E.
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FRotator Facing(0.f, Start->GetActorRotation().Yaw, 0.f);
	Pawn->TeleportTo(Start->GetStandLocation() + FVector(0.f, 0.f, HalfHeight + 2.f), Facing);
	if (AController* Controller = Pawn->GetController())
	{
		Controller->SetControlRotation(FRotator(-10.f, Facing.Yaw, 0.f));
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: retrying %s."), *GetName(), *GetNameSafe(Start->Definition));
	return StartChallenge(Start, Pawn);
}

void UChallengeSubsystem::SpawnCourse(const UChallengeDefinition* Definition)
{
	ClearCourse();
	UWorld* World = GetWorld();
	if (!World || !Definition)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (Definition->Type == EChallengeType::Archery)
	{
		for (int32 Index = 0; Index < Definition->Targets.Num(); ++Index)
		{
			const FChallengeTargetSpawn& Spawn = Definition->Targets[Index];
			AChallengeTarget* Target = World->SpawnActor<AChallengeTarget>(AChallengeTarget::StaticClass(), Spawn.Transform, Params);
			if (Target)
			{
				Target->InitTarget(Index, Spawn, Definition->OuterRadius, Definition->RingRadius, Definition->BullseyeRadius);
			}
			Targets.Add(Target);
		}
		return;
	}
	for (const FTransform& Ring : Definition->Checkpoints)
	{
		AChallengeCheckpoint* Checkpoint = World->SpawnActor<AChallengeCheckpoint>(AChallengeCheckpoint::StaticClass(),
			FTransform(Ring.Rotator(), Ring.GetLocation()), Params);
		Checkpoints.Add(Checkpoint);
	}
}

void UChallengeSubsystem::ClearCourse()
{
	for (const TWeakObjectPtr<AChallengeTarget>& Weak : Targets)
	{
		if (AChallengeTarget* Target = Weak.Get())
		{
			// Long enough to be seen falling; a retry spawns a fresh set beside it.
			Target->SetLifeSpan(HawkeyeChallenges::TargetLingerSeconds);
		}
	}
	for (const TWeakObjectPtr<AChallengeCheckpoint>& Weak : Checkpoints)
	{
		if (AChallengeCheckpoint* Checkpoint = Weak.Get())
		{
			Checkpoint->Destroy();
		}
	}
	Targets.Reset();
	Checkpoints.Reset();
}

void UChallengeSubsystem::CalmThugs(const UChallengeDefinition* Definition)
{
	ReleaseThugs();
	UWorld* World = GetWorld();
	if (!World || !Definition || Definition->CalmThugRadius <= 0.f)
	{
		return;
	}
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		AThugAIController* Brain = Cast<AThugAIController>(It->GetController());
		const float Distance = FVector::Dist(It->GetActorLocation(), Definition->StartLocation);
		if (!Brain || Distance > Definition->CalmThugRadius)
		{
			continue;
		}
		Brain->SetPacified(true);
		CalmedThugs.Add(Brain);
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s calm for %s (%.0f m from its start)."), *GetName(), *It->GetName(),
			*Definition->Id.ToString(), Distance / 100.f);
	}
}

void UChallengeSubsystem::ReleaseThugs()
{
	for (const TWeakObjectPtr<AThugAIController>& Weak : CalmedThugs)
	{
		if (AThugAIController* Brain = Weak.Get())
		{
			Brain->SetPacified(false);
		}
	}
	CalmedThugs.Reset();
}

void UChallengeSubsystem::RefreshCourse()
{
	const UChallengeDefinition* Definition = GetActiveDefinition();
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Definition)
	{
		return;
	}
	TArray<FVector> Marks;
	if (Definition->Type == EChallengeType::Archery)
	{
		for (const TWeakObjectPtr<AChallengeTarget>& Weak : Targets)
		{
			const AChallengeTarget* Target = Weak.Get();
			if (Target && !Target->IsDown())
			{
				Marks.Add(Target->GetFaceCentre());
			}
		}
	}
	else
	{
		const int32 Next = Tracker->GetNextCheckpointIndex();
		if (Next != ShownProgress)
		{
			ShownProgress = Next;
			for (int32 Index = 0; Index < Checkpoints.Num(); ++Index)
			{
				AChallengeCheckpoint* Ring = Checkpoints[Index].Get();
				if (!Ring)
				{
					continue;
				}
				Ring->SetState(Index < Next ? EChallengeCheckpointState::Passed
					: Index == Next ? EChallengeCheckpointState::Next
					: Index == Next + 1 ? EChallengeCheckpointState::Upcoming : EChallengeCheckpointState::Hidden);
			}
		}
		if (Definition->Checkpoints.IsValidIndex(Next))
		{
			Marks.Add(Definition->Checkpoints[Next].GetLocation());
		}
	}
	if (Missions)
	{
		Missions->SetSecondaryMarkers(MarkerSource, Marks);
	}
}

void UChallengeSubsystem::Tick(float DeltaTime)
{
	if (!IsRunning())
	{
		return;
	}
	if (IsPlayerOut())
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: the challenger is down or gone; abandoning %s."), *GetName(),
			*GetNameSafe(GetActiveDefinition()));
		AbortChallenge();
		return;
	}
	if (!Tracker->Advance(DeltaTime, Challenger->GetActorLocation()))
	{
		RefreshCourse();
	}
}

void UChallengeSubsystem::ReportTargetHit(AChallengeTarget* Target, float RadialCm, AActor* Shooter)
{
	const UChallengeDefinition* Definition = GetActiveDefinition();
	if (!Definition || Definition->Type != EChallengeType::Archery || !Target || !Targets.IsValidIndex(Target->GetTargetIndex())
		|| Targets[Target->GetTargetIndex()].Get() != Target || Tracker->IsTargetDown(Target->GetTargetIndex()))
	{
		return;
	}
	if (Shooter != Challenger.Get())
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: %s hit target %d, but it is not their challenge."), *GetName(), *GetNameSafe(Shooter),
			Target->GetTargetIndex());
		return;
	}
	// Down first: the last target's hit ends the run, and the ending lets the course go.
	Target->KnockDown(UChallengeRules::ScoreHit(Definition, RadialCm));
	Tracker->RegisterHit(Target->GetTargetIndex(), RadialCm);
	if (IsRunning())
	{
		RefreshCourse();
	}
}

void UChallengeSubsystem::HandleRunEnded(const FChallengeResult& Result)
{
	const UChallengeDefinition* Definition = LastStart.IsValid() ? LastStart->Definition.Get() : nullptr;
	ClearCourse();
	ReleaseThugs();
	ShownProgress = INDEX_NONE;
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->ClearSecondaryMarkers(MarkerSource);
	}

	APawn* Pawn = Challenger.Get();
	if (Result.IsCompleted() && Definition && Definition->Reward == EChallengeReward::ArrowRefill && Pawn)
	{
		UInventoryComponent* Inventory = Pawn->FindComponentByClass<UInventoryComponent>();
		const int32 Added = Inventory ? Inventory->RefillToCaps() : 0;
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s reward: arrows refilled (+%d)."), *GetName(), *Result.ChallengeId.ToString(), Added);
	}

	MirrorToCampaign();
	if (Result.IsCompleted())
	{
		if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
		{
			Save->SaveCampaign(TEXT("challenge"));
		}
	}
	OnChallengeEnded.Broadcast(Result);

	AHawkeyePlayerController* PC = Pawn ? Cast<AHawkeyePlayerController>(Pawn->GetController()) : nullptr;
	if (bShowResultsCard && PC)
	{
		PC->OpenChallengeResults(Result);
	}
}

void UChallengeSubsystem::MirrorToCampaign() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (Campaign && Tracker)
	{
		Tracker->ExportRecords(Campaign->ChallengeIds, Campaign->ChallengeBestScores, Campaign->ChallengeBestSeconds,
			Campaign->ChallengeBestMedals, Campaign->ChallengeCompletions);
	}
}

void UChallengeSubsystem::RestoreFromCampaign()
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (Campaign && Tracker)
	{
		Tracker->ImportRecords(Campaign->ChallengeIds, Campaign->ChallengeBestScores, Campaign->ChallengeBestSeconds,
			Campaign->ChallengeBestMedals, Campaign->ChallengeCompletions);
	}
}

void UChallengeSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		RestoreFromCampaign();
	}
}
