#include "Partner/HawkeyePartnerController.h"

#include "Hawkeye.h"
#include "CollisionQueryParams.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionSubsystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "StateTree.h"
#include "TimerManager.h"
#include "World/GrappleAnchor.h"
#include "World/ThugCharacter.h"

namespace HawkeyePartner
{
	/** Thugs this close to either Hawkeye, alerted, keep the fight going. */
	static constexpr float ThugContactRadius = 3000.f;

	/** A goal this far from the last one is a new move. */
	static constexpr float RepathDistance = 150.f;

	/** Within this (2D) of the mark he has arrived. */
	static constexpr float MarkArriveDistance = 150.f;

	static bool IsAlive(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return false;
		}
		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		return !Health || Health->IsAlive();
	}
}

AHawkeyePartnerController::AHawkeyePartnerController()
{
	PrimaryActorTick.bCanEverTick = false;
	bSetControlRotationFromPawnOrientation = true;

	StateTreeComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeComponent"));
	// Started in OnPossess, once there is a pawn for the tree's Actor context and a tree to run.
	StateTreeComponent->SetStartLogicAutomatically(false);
}

// --- Rules -----------------------------------------------------------------------------------------

bool AHawkeyePartnerController::ComputeFollowGoal(const FVector& LeaderLocation, const FVector& PartnerLocation,
	float MinDistance, float MaxDistance, float SettleDistance, FVector& OutGoal)
{
	const FVector Offset = PartnerLocation - LeaderLocation;
	const float Distance = Offset.Size2D();
	if (Distance >= MinDistance && Distance <= MaxDistance)
	{
		OutGoal = PartnerLocation;
		return false;
	}
	// Along the line from her to him, so he stops on the side he is already on.
	FVector Direction = Offset.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector(-1.f, 0.f, 0.f);
	}
	OutGoal = FVector(LeaderLocation.X, LeaderLocation.Y, PartnerLocation.Z) + Direction * SettleDistance;
	return true;
}

AActor* AHawkeyePartnerController::SelectAttackTarget(const TArray<FHawkeyeLeaderHit>& Hits, double Now,
	float WindowSeconds, const FVector& From)
{
	AActor* Best = nullptr;
	double BestSeconds = -TNumericLimits<double>::Max();
	float BestDistance = TNumericLimits<float>::Max();
	for (const FHawkeyeLeaderHit& Hit : Hits)
	{
		AActor* Target = Hit.Target.Get();
		if (!HawkeyePartner::IsAlive(Target) || Now - Hit.Seconds > WindowSeconds || Hit.Seconds > Now)
		{
			continue;
		}
		const float Distance = FVector::Dist(Target->GetActorLocation(), From);
		const bool bLater = Hit.Seconds > BestSeconds + KINDA_SMALL_NUMBER;
		const bool bTieCloser = FMath::IsNearlyEqual(Hit.Seconds, BestSeconds) && Distance < BestDistance;
		if (bLater || bTieCloser)
		{
			Best = Target;
			BestSeconds = Hit.Seconds;
			BestDistance = Distance;
		}
	}
	return Best;
}

// --- Possession ------------------------------------------------------------------------------------

void AHawkeyePartnerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AHawkeyeCharacter* Partner = GetPartner();
	BindPartnerEvents(Partner, true);
	if (Partner && Partner->GetHealthComponent())
	{
		// Immortal for now; staggers still land.
		Partner->GetHealthComponent()->SetCannotDie(true);
	}
	if (Partner)
	{
		Partner->SetAIGait(EHawkeyeGait::Run);
	}

	EnsureLeader();

	if (PartnerStateTree && StateTreeComponent)
	{
		StateTreeComponent->SetStateTree(PartnerStateTree);
		StateTreeComponent->StartLogic();
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: partner for %s, driving %s (%s)."), *GetName(), *GetNameSafe(Leader.Get()),
		*GetNameSafe(InPawn), IsUsingStateTree() ? *FString::Printf(TEXT("StateTree %s"), *GetNameSafe(PartnerStateTree))
		: TEXT("C++ mode selection, no StateTree"));

	// A chapter without him: off stage from his first possession (the game mode benches any already possessed). A
	// later possession is a switch handing him the other Hawkeye, which a test or Act 2 asked for: left alone.
	if (const UMissionSubsystem* Missions = UMissionSubsystem::Get(this); !bEverPossessed
		&& Missions && Missions->GetCurrentMission() && !Missions->GetCurrentMission()->bPartnerPresent)
	{
		SetBenched(true);
	}
	bEverPossessed = true;

	if (UWorld* World = GetWorld(); World && bThinkingEnabled)
	{
		World->GetTimerManager().SetTimer(ThinkTimerHandle, this, &AHawkeyePartnerController::TickThink,
			ThinkIntervalSeconds, true);
	}
}

void AHawkeyePartnerController::OnUnPossess()
{
	CancelBowShot();
	ClearFocus(EAIFocusPriority::Gameplay);
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(TEXT("Unpossessed"));
	}
	if (AHawkeyeCharacter* Partner = GetPartner())
	{
		BindPartnerEvents(Partner, false);
		if (UHealthComponent* Health = Partner->GetHealthComponent())
		{
			Health->SetCannotDie(false);
		}
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimerHandle);
	}
	bHasMoveGoal = false;
	Super::OnUnPossess();
}

void AHawkeyePartnerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimerHandle);
	}
	BindLeaderEvents(Leader.Get(), false);
	Super::EndPlay(EndPlayReason);
}

void AHawkeyePartnerController::EnsureLeader()
{
	if (Leader.IsValid())
	{
		return;
	}
	// A partner placed in the district follows whoever the player is playing; at level load the
	// player's pawn may not exist yet, so Think asks again until it does.
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Played = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (Played && Played != GetPawn())
	{
		SetLeader(Played);
		UE_LOG(LogHawkeye, Log, TEXT("%s: now backing up %s."), *GetName(), *GetNameSafe(Played));
	}
}

AHawkeyeCharacter* AHawkeyePartnerController::GetPartner() const
{
	return bBenched ? nullptr : Cast<AHawkeyeCharacter>(GetPawn());
}

void AHawkeyePartnerController::SetBenched(bool bInBenched)
{
	APawn* Benched = GetPawn();
	if (bBenched == bInBenched || !Benched)
	{
		return;
	}
	if (bInBenched)
	{
		SetThinkingEnabled(false);
	}
	bBenched = bInBenched;
	Benched->SetActorHiddenInGame(bInBenched);
	Benched->SetActorEnableCollision(!bInBenched);
	if (ACharacter* Body = Cast<ACharacter>(Benched))
	{
		if (bInBenched)
		{
			Body->GetCharacterMovement()->DisableMovement();
		}
		else
		{
			Body->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
	if (!bInBenched)
	{
		SetThinkingEnabled(true);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s %s."), *GetName(), *GetNameSafe(Benched),
		bInBenched ? TEXT("benched: this chapter has no partner") : TEXT("back from the bench"));
}

void AHawkeyePartnerController::SetLeader(AHawkeyeCharacter* NewLeader)
{
	if (Leader.Get() == NewLeader)
	{
		return;
	}
	BindLeaderEvents(Leader.Get(), false);
	Leader = NewLeader;
	BindLeaderEvents(NewLeader, true);
	LeaderHits.Reset();
	ReviveTarget.Reset();
	CancelCommand();
}

void AHawkeyePartnerController::BindLeaderEvents(AHawkeyeCharacter* InLeader, bool bBind)
{
	if (!InLeader)
	{
		return;
	}
	UBowComponent* Bow = InLeader->GetBowComponent();
	UMeleeComponent* Melee = InLeader->GetMeleeComponent();
	UHealthComponent* Health = InLeader->GetHealthComponent();
	if (bBind)
	{
		if (Bow) { Bow->OnHit.AddUniqueDynamic(this, &AHawkeyePartnerController::HandleLeaderBowHit); }
		if (Melee) { Melee->OnAttackLanded.AddUniqueDynamic(this, &AHawkeyePartnerController::HandleLeaderMeleeLanded); }
		if (Health) { Health->OnHealthChanged.AddUniqueDynamic(this, &AHawkeyePartnerController::HandleLeaderHealthChanged); }
		return;
	}
	if (Bow) { Bow->OnHit.RemoveDynamic(this, &AHawkeyePartnerController::HandleLeaderBowHit); }
	if (Melee) { Melee->OnAttackLanded.RemoveDynamic(this, &AHawkeyePartnerController::HandleLeaderMeleeLanded); }
	if (Health) { Health->OnHealthChanged.RemoveDynamic(this, &AHawkeyePartnerController::HandleLeaderHealthChanged); }
}

void AHawkeyePartnerController::BindPartnerEvents(AHawkeyeCharacter* Partner, bool bBind)
{
	if (!Partner)
	{
		return;
	}
	UBowComponent* Bow = Partner->GetBowComponent();
	UMeleeComponent* Melee = Partner->GetMeleeComponent();
	UHealthComponent* Health = Partner->GetHealthComponent();
	if (bBind)
	{
		if (Bow) { Bow->OnHit.AddUniqueDynamic(this, &AHawkeyePartnerController::HandlePartnerBowHit); }
		if (Melee) { Melee->OnAttackLanded.AddUniqueDynamic(this, &AHawkeyePartnerController::HandlePartnerMeleeLanded); }
		if (Health) { Health->OnHealthChanged.AddUniqueDynamic(this, &AHawkeyePartnerController::HandlePartnerHealthChanged); }
		return;
	}
	if (Bow) { Bow->OnHit.RemoveDynamic(this, &AHawkeyePartnerController::HandlePartnerBowHit); }
	if (Melee) { Melee->OnAttackLanded.RemoveDynamic(this, &AHawkeyePartnerController::HandlePartnerMeleeLanded); }
	if (Health) { Health->OnHealthChanged.RemoveDynamic(this, &AHawkeyePartnerController::HandlePartnerHealthChanged); }
}

// --- Events ----------------------------------------------------------------------------------------

void AHawkeyePartnerController::HandleLeaderBowHit(AActor* HitActor, float /*Damage*/, bool /*bHeadshot*/)
{
	NotifyLeaderHit(HitActor);
}

void AHawkeyePartnerController::HandleLeaderMeleeLanded(AActor* HitActor, float /*DamageDealt*/, FName /*AttackName*/)
{
	NotifyLeaderHit(HitActor);
}

void AHawkeyePartnerController::HandleLeaderHealthChanged(UHealthComponent* /*Health*/, float /*NewHealth*/, float Delta,
	AActor* DamageInstigator)
{
	// Hurt by someone (a fall has no instigator): that is contact.
	if (Delta < 0.f && DamageInstigator && DamageInstigator != Leader.Get())
	{
		FightClock.NotifyContact(GetNowSeconds());
	}
}

void AHawkeyePartnerController::HandlePartnerHealthChanged(UHealthComponent* /*Health*/, float /*NewHealth*/, float Delta,
	AActor* DamageInstigator)
{
	if (Delta < 0.f && DamageInstigator && DamageInstigator != GetPawn() && DamageInstigator != Leader.Get())
	{
		NotifyShotAt(DamageInstigator);
	}
}

void AHawkeyePartnerController::HandlePartnerBowHit(AActor* /*HitActor*/, float /*Damage*/, bool /*bHeadshot*/)
{
	FightClock.NotifyContact(GetNowSeconds());
}

void AHawkeyePartnerController::HandlePartnerMeleeLanded(AActor* /*HitActor*/, float /*DamageDealt*/, FName /*AttackName*/)
{
	FightClock.NotifyContact(GetNowSeconds());
}

void AHawkeyePartnerController::NotifyLeaderHit(AActor* Target)
{
	if (!IsValid(Target) || Target == GetPawn() || Target == Leader.Get())
	{
		return;
	}
	const double Now = GetNowSeconds();
	FightClock.NotifyContact(Now);
	for (FHawkeyeLeaderHit& Hit : LeaderHits)
	{
		if (Hit.Target.Get() == Target)
		{
			Hit.Seconds = Now;
			return;
		}
	}
	FHawkeyeLeaderHit& Hit = LeaderHits.AddDefaulted_GetRef();
	Hit.Target = Target;
	Hit.Seconds = Now;
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: the lead hit %s; a target for %.0f s."), *GetName(), *GetNameSafe(Target),
		AttackWindowSeconds);
}

void AHawkeyePartnerController::NotifyShotAt(AActor* Shooter)
{
	const double Now = GetNowSeconds();
	FightClock.NotifyContact(Now);
	const bool bWasUnderFire = Now < UnderFireUntil;
	UnderFireUntil = Now + CoverSeconds;
	if (IsValid(Shooter))
	{
		ThreatLocation = Shooter->GetActorLocation();
	}
	if (!bWasUnderFire)
	{
		bHasCoverPoint = false;
		UE_LOG(LogHawkeye, Log, TEXT("%s: shot at by %s; taking cover."), *GetName(), *GetNameSafe(Shooter));
	}
}

bool AHawkeyePartnerController::RequestRevive(AHawkeyeCharacter* Downed)
{
	if (!Downed || Downed != Leader.Get() || !GetPawn() || bBenched)
	{
		return false;
	}
	const double Now = GetNowSeconds();
	if (!FightClock.ConsumeRevive(Now))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: the revive is already spent this fight."), *GetName());
		return false;
	}
	ReviveTarget = Downed;
	ReviveElapsed = 0.f;
	ReviveWaitElapsed = 0.f;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s is down; reviving (once this fight)."), *GetName(), *GetNameSafe(Downed));
	return true;
}

void AHawkeyePartnerController::CommandMoveTo(FVector Point)
{
	bHasMark = true;
	MarkPoint = Point;
	bAtMark = false;
	bHasMoveGoal = false;
	UE_LOG(LogHawkeye, Log, TEXT("%s: going to the mark at %s."), *GetName(), *Point.ToCompactString());
}

void AHawkeyePartnerController::CancelCommand()
{
	bHasMark = false;
	bAtMark = false;
}

// --- Modes -----------------------------------------------------------------------------------------

bool AHawkeyePartnerController::HasNeed(EHawkeyePartnerMode Mode) const
{
	switch (Mode)
	{
	case EHawkeyePartnerMode::Revive:
		return ReviveTarget.IsValid() && ReviveTarget->IsReviveExpected();
	case EHawkeyePartnerMode::GoToMark:
		return bHasMark;
	case EHawkeyePartnerMode::Cover:
		return GetNowSeconds() < UnderFireUntil;
	case EHawkeyePartnerMode::Attack:
		return GetAttackTarget() != nullptr;
	case EHawkeyePartnerMode::Follow:
	default:
		return true;
	}
}

EHawkeyePartnerMode AHawkeyePartnerController::ChooseMode() const
{
	// The same order as ST_Partner's states; see UHawkeyePartnerTreeBuilder.
	static const EHawkeyePartnerMode Priority[] = {
		EHawkeyePartnerMode::Revive, EHawkeyePartnerMode::GoToMark, EHawkeyePartnerMode::Cover,
		EHawkeyePartnerMode::Attack, EHawkeyePartnerMode::Follow };
	for (const EHawkeyePartnerMode Mode : Priority)
	{
		if (HasNeed(Mode))
		{
			return Mode;
		}
	}
	return EHawkeyePartnerMode::Follow;
}

AActor* AHawkeyePartnerController::GetAttackTarget() const
{
	const APawn* Me = GetPawn();
	return SelectAttackTarget(LeaderHits, GetNowSeconds(), AttackWindowSeconds, Me ? Me->GetActorLocation() : FVector::ZeroVector);
}

void AHawkeyePartnerController::EnterMode(EHawkeyePartnerMode NewMode)
{
	if (NewMode == CurrentMode)
	{
		return;
	}
	const EHawkeyePartnerMode OldMode = CurrentMode;
	CurrentMode = NewMode;
	CancelBowShot();
	ClearFocus(EAIFocusPriority::Gameplay);
	StopMovement();
	bHasMoveGoal = false;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s -> %s."), *GetName(), *UEnum::GetValueAsString(OldMode),
		*UEnum::GetValueAsString(NewMode));
	OnModeChanged.Broadcast(OldMode, NewMode);
}

void AHawkeyePartnerController::RunMode(EHawkeyePartnerMode Mode, float DeltaSeconds)
{
	AHawkeyeCharacter* Partner = GetPartner();
	if (!Partner || !bThinkingEnabled)
	{
		return;
	}
	EnterMode(Mode);
	// A stagger, a swing or a traversal move owns the body for its length.
	if (Partner->IsStaggered() || Partner->IsZipping() || Partner->IsTraversing())
	{
		return;
	}
	switch (Mode)
	{
	case EHawkeyePartnerMode::Revive: TickRevive(DeltaSeconds); break;
	case EHawkeyePartnerMode::GoToMark: TickGoToMark(DeltaSeconds); break;
	case EHawkeyePartnerMode::Cover: TickCover(DeltaSeconds); break;
	case EHawkeyePartnerMode::Attack: TickAttack(DeltaSeconds); break;
	case EHawkeyePartnerMode::Follow:
	default: TickFollow(DeltaSeconds); break;
	}
}

void AHawkeyePartnerController::TickFollow(float /*DeltaSeconds*/)
{
	AHawkeyeCharacter* Partner = GetPartner();
	const AHawkeyeCharacter* Lead = Leader.Get();
	if (!Partner || !Lead)
	{
		return;
	}
	FVector Goal;
	if (!ComputeFollowGoal(Lead->GetActorLocation(), Partner->GetActorLocation(), FollowMinDistance, FollowMaxDistance,
			FollowSettleDistance, Goal))
	{
		// Once he has set off he carries on to the settle distance rather than stopping at the band's edge.
		const float Distance = FVector::Dist2D(Lead->GetActorLocation(), Partner->GetActorLocation());
		if (bHasMoveGoal && GetMoveStatus() == EPathFollowingStatus::Moving && Distance > FollowSettleDistance + 50.f)
		{
			return;
		}
		if (bHasMoveGoal)
		{
			StopMovement();
			bHasMoveGoal = false;
		}
		return;
	}
	const float Distance = FVector::Dist2D(Lead->GetActorLocation(), Partner->GetActorLocation());
	Partner->SetAIGait(Distance > SprintDistance ? EHawkeyeGait::Sprint : EHawkeyeGait::Run);
	MoveTowards(Goal, 80.f);
}

void AHawkeyePartnerController::TickAttack(float DeltaSeconds)
{
	EngageTarget(GetAttackTarget(), DeltaSeconds, /*bAllowMove=*/true);
}

void AHawkeyePartnerController::EngageTarget(AActor* Target, float DeltaSeconds, bool bAllowMove)
{
	AHawkeyeCharacter* Partner = GetPartner();
	if (!Partner || !IsValid(Target))
	{
		CancelBowShot();
		return;
	}
	SetFocus(Target, EAIFocusPriority::Gameplay);
	const float Distance = FVector::Dist2D(Target->GetActorLocation(), Partner->GetActorLocation());
	if (Distance <= MeleeRange)
	{
		CancelBowShot();
		StopMovement();
		bHasMoveGoal = false;
		if (!Partner->IsMeleeAttacking())
		{
			Partner->StartLightAttack();
		}
		return;
	}
	const UInventoryComponent* Inventory = Partner->GetInventoryComponent();
	const bool bHasArrows = Inventory && Inventory->HasBow() && Inventory->GetArrowCount(1) > 0;
	if (bHasArrows && Distance <= BowRange && HasLineOfSight(Target))
	{
		StopMovement();
		bHasMoveGoal = false;
		UpdateBowShot(Target, DeltaSeconds);
		return;
	}
	CancelBowShot();
	if (bAllowMove)
	{
		Partner->SetAIGait(EHawkeyeGait::Run);
		// Out of arrows he goes in close; otherwise to where he can draw.
		MoveTowards(Target->GetActorLocation(), bHasArrows ? FMath::Min(BowRange * 0.5f, 1200.f) : MeleeRange * 0.6f);
	}
}

void AHawkeyePartnerController::UpdateBowShot(AActor* Target, float /*DeltaSeconds*/)
{
	AHawkeyeCharacter* Partner = GetPartner();
	UBowComponent* Bow = Partner ? Partner->GetBowComponent() : nullptr;
	UInventoryComponent* Inventory = Partner ? Partner->GetInventoryComponent() : nullptr;
	if (!Bow || !Inventory)
	{
		return;
	}
	const FVector Chest = Target->GetActorLocation() + FVector(0.f, 0.f, ChestHeight);
	Bow->SetAimOverride(Chest);
	// Face the target outright: the draw strafes on the control rotation, which the focus turns.
	const FVector Flat = (Target->GetActorLocation() - Partner->GetActorLocation()).GetSafeNormal2D();
	if (!Flat.IsNearlyZero())
	{
		Partner->SetActorRotation(FRotator(0.f, Flat.Rotation().Yaw, 0.f));
	}

	const double Now = GetNowSeconds();
	if (!Bow->IsDrawing())
	{
		if (Now - LastShotSeconds < ShotCooldownSeconds || Partner->IsMeleeAttacking())
		{
			return;
		}
		Inventory->SelectArrowSlot(1);
		if (Bow->StartDraw())
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: drawing on %s at %.0f cm."), *GetName(), *GetNameSafe(Target),
				FVector::Dist(Target->GetActorLocation(), Partner->GetActorLocation()));
		}
		return;
	}
	const UBowDefinition* BowDef = Bow->GetBow();
	const float FullDraw = BowDef ? BowDef->FullDrawSeconds : 1.f;
	if (Bow->GetDrawElapsed() >= FullDraw + ReleaseAfterFullDrawSeconds)
	{
		Bow->ReleaseDraw();
		LastShotSeconds = Now;
	}
}

void AHawkeyePartnerController::CancelBowShot()
{
	const AHawkeyeCharacter* Partner = GetPartner();
	UBowComponent* Bow = Partner ? Partner->GetBowComponent() : nullptr;
	if (!Bow)
	{
		return;
	}
	if (Bow->IsDrawing())
	{
		Bow->CancelDraw();
	}
	Bow->ClearAimOverride();
}

void AHawkeyePartnerController::TickCover(float /*DeltaSeconds*/)
{
	AHawkeyeCharacter* Partner = GetPartner();
	if (!Partner)
	{
		return;
	}
	if (!bHasCoverPoint)
	{
		bHasCoverPoint = FindCoverPoint(ThreatLocation, CoverPoint);
		if (!bHasCoverPoint)
		{
			// Nothing solid nearby: open the distance instead.
			const FVector Away = (Partner->GetActorLocation() - ThreatLocation).GetSafeNormal2D();
			CoverPoint = Partner->GetActorLocation() + Away * (CoverSearchRadius * 0.6f);
			bHasCoverPoint = true;
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: cover at %s, %.0f cm away."), *GetName(), *CoverPoint.ToCompactString(),
			FVector::Dist2D(CoverPoint, Partner->GetActorLocation()));
	}
	Partner->SetAIGait(EHawkeyeGait::Sprint);
	MoveTowards(CoverPoint, 50.f);
}

void AHawkeyePartnerController::TickGoToMark(float DeltaSeconds)
{
	AHawkeyeCharacter* Partner = GetPartner();
	const AHawkeyeCharacter* Lead = Leader.Get();
	if (!Partner)
	{
		return;
	}
	if (Lead && FVector::Dist2D(Lead->GetActorLocation(), MarkPoint) > MarkReleaseDistance)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: the lead has moved on; leaving the mark."), *GetName());
		CancelCommand();
		return;
	}
	if (!bAtMark)
	{
		bAtMark = FVector::Dist2D(Partner->GetActorLocation(), MarkPoint) <= HawkeyePartner::MarkArriveDistance;
		if (!bAtMark)
		{
			Partner->SetAIGait(EHawkeyeGait::Run);
			MoveTowards(MarkPoint, 60.f);
			return;
		}
		StopMovement();
		bHasMoveGoal = false;
		UE_LOG(LogHawkeye, Log, TEXT("%s: holding the mark."), *GetName());
	}
	// Holding: he still shoots what the lead hits, from where he stands.
	if (AActor* Target = GetAttackTarget())
	{
		EngageTarget(Target, DeltaSeconds, /*bAllowMove=*/false);
	}
	else
	{
		CancelBowShot();
		ClearFocus(EAIFocusPriority::Gameplay);
	}
}

void AHawkeyePartnerController::TickRevive(float DeltaSeconds)
{
	AHawkeyeCharacter* Partner = GetPartner();
	AHawkeyeCharacter* Downed = ReviveTarget.Get();
	if (!Partner || !Downed || !Downed->IsReviveExpected())
	{
		ReviveTarget.Reset();
		return;
	}
	const float Distance = FVector::Dist(Downed->GetActorLocation(), Partner->GetActorLocation());
	if (Distance > ReviveReach)
	{
		ReviveWaitElapsed += DeltaSeconds;
		if (ReviveWaitElapsed < ReviveTimeoutSeconds)
		{
			Partner->SetAIGait(EHawkeyeGait::Sprint);
			MoveTowards(Downed->GetActorLocation(), ReviveReach * 0.5f);
			return;
		}
		// Could not get there (a roof he cannot path to): put him beside her.
		const FVector Beside = Downed->GetActorLocation() - Downed->GetActorForwardVector() * (ReviveReach * 0.5f);
		Partner->TeleportTo(Beside, Partner->GetActorRotation(), false, true);
		UE_LOG(LogHawkeye, Log, TEXT("%s: could not reach %s in %.0f s; moved beside her."), *GetName(),
			*GetNameSafe(Downed), ReviveTimeoutSeconds);
	}
	StopMovement();
	bHasMoveGoal = false;
	SetFocus(Downed, EAIFocusPriority::Gameplay);
	ReviveElapsed += DeltaSeconds;
	if (ReviveElapsed >= ReviveSeconds)
	{
		Downed->ReviveFromDown(ReviveHealthFraction);
		ReviveTarget.Reset();
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s is back up at %.0f%%."), *GetName(), *GetNameSafe(Downed),
			ReviveHealthFraction * 100.f);
	}
}

// --- Movement and queries ----------------------------------------------------------------------------

void AHawkeyePartnerController::MoveTowards(const FVector& Goal, float AcceptanceRadius)
{
	const bool bMoving = GetMoveStatus() == EPathFollowingStatus::Moving;
	if (bMoving && bHasMoveGoal && FVector::Dist(Goal, LastMoveGoal) < HawkeyePartner::RepathDistance)
	{
		return;
	}
	LastMoveGoal = Goal;
	bHasMoveGoal = true;
	const EPathFollowingRequestResult::Type Result = MoveToLocation(Goal, AcceptanceRadius, /*bStopOnOverlap=*/true,
		/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true);
	if (Result == EPathFollowingRequestResult::Failed && !bLoggedMoveFailure)
	{
		bLoggedMoveFailure = true;
		UE_LOG(LogHawkeye, Log, TEXT("%s: no path to %s (off the navmesh, or no navmesh here)."), *GetName(),
			*Goal.ToCompactString());
	}
}

bool AHawkeyePartnerController::HasLineOfSight(const AActor* Target) const
{
	const APawn* Me = GetPawn();
	const UWorld* World = GetWorld();
	if (!Me || !World || !Target)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PartnerSight), false, Me);
	Params.AddIgnoredActor(Target);
	const FVector From = Me->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	const FVector To = Target->GetActorLocation() + FVector(0.f, 0.f, ChestHeight);
	return !World->LineTraceTestByChannel(From, To, ECC_Visibility, Params);
}

bool AHawkeyePartnerController::FindCoverPoint(const FVector& Threat, FVector& OutPoint) const
{
	const APawn* Me = GetPawn();
	UWorld* World = GetWorld();
	if (!Me || !World)
	{
		return false;
	}
	// EQS-shaped, without an asset: rings of candidates, kept where the shooter's line is blocked.
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PartnerCover), false, Me);
	const FVector Eye = Threat + FVector(0.f, 0.f, 60.f);
	float BestDistance = TNumericLimits<float>::Max();
	bool bFound = false;
	for (const float Radius : { CoverSearchRadius * 0.4f, CoverSearchRadius * 0.7f, CoverSearchRadius })
	{
		for (int32 Step = 0; Step < 12; ++Step)
		{
			const float Angle = Step * 30.f;
			FVector Candidate = Me->GetActorLocation() + FRotator(0.f, Angle, 0.f).Vector() * Radius;
			FNavLocation OnNav;
			if (Nav && Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(100.f, 100.f, 250.f)))
			{
				Candidate = OnNav.Location + FVector(0.f, 0.f, 90.f);
			}
			if (!World->LineTraceTestByChannel(Eye, Candidate, ECC_Visibility, Params))
			{
				continue;
			}
			const float Distance = FVector::Dist2D(Candidate, Me->GetActorLocation());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				OutPoint = Candidate;
				bFound = true;
			}
		}
		if (bFound)
		{
			return true;
		}
	}
	return false;
}

void AHawkeyePartnerController::GrappleCatchUp()
{
	AHawkeyeCharacter* Partner = GetPartner();
	const AHawkeyeCharacter* Lead = Leader.Get();
	UWorld* World = GetWorld();
	if (!Partner || !Lead || !World)
	{
		return;
	}
	AGrappleAnchor* Best = nullptr;
	float BestDistance = CatchUpAnchorRadius;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetLandingLocation(), Lead->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	UGrappleComponent* Grapple = Partner->GetGrappleComponent();
	const bool bCanZip = Best && Grapple && FVector::Dist(Best->GetLandingLocation(), Partner->GetActorLocation()) <= 2500.f
		&& Grapple->IsZipClear(Partner->GetActorLocation(), Best, /*bFromGround=*/true);
	StopMovement();
	bHasMoveGoal = false;
	if (bCanZip && Grapple->StartZip(Best))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: catch-up, grapple zip to %s (%.0f cm from the lead)."), *GetName(),
			*GetNameSafe(Best), BestDistance);
		return;
	}
	// No clear line: the zip is taken as read and he lands where it would have put him.
	const FVector Landing = (Best && Grapple) ? Grapple->ComputeZipEnd(Best)
		: Lead->GetActorLocation() - Lead->GetActorForwardVector() * FollowSettleDistance;
	Partner->TeleportTo(Landing, Partner->GetActorRotation(), false, true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: catch-up, teleported to %s (%s)."), *GetName(), *Landing.ToCompactString(),
		Best ? *FString::Printf(TEXT("anchor %s, zip blocked"), *Best->GetName()) : TEXT("no anchor near the lead"));
}

// --- Think -----------------------------------------------------------------------------------------

void AHawkeyePartnerController::TickThink()
{
	Think(ThinkIntervalSeconds);
}

void AHawkeyePartnerController::Think(float DeltaSeconds)
{
	if (!bThinkingEnabled || !GetPawn())
	{
		return;
	}
	EnsureLeader();
	UpdateSenses(DeltaSeconds);
	if (!IsUsingStateTree())
	{
		RunMode(ChooseMode(), DeltaSeconds);
	}
}

void AHawkeyePartnerController::UpdateSenses(float DeltaSeconds)
{
	AHawkeyeCharacter* Partner = GetPartner();
	const AHawkeyeCharacter* Lead = Leader.Get();
	const double Now = GetNowSeconds();
	UWorld* World = GetWorld();
	if (World && Partner)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			const bool bNear = FVector::Dist(It->GetActorLocation(), Partner->GetActorLocation()) < HawkeyePartner::ThugContactRadius
				|| (Lead && FVector::Dist(It->GetActorLocation(), Lead->GetActorLocation()) < HawkeyePartner::ThugContactRadius);
			if (bNear && It->IsAlerted() && HawkeyePartner::IsAlive(*It))
			{
				FightClock.NotifyContact(Now);
				break;
			}
		}
	}

	const bool bInFight = FightClock.IsInFight(Now);
	if (bWasInFight && !bInFight)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: the fight is over (%.0f s since the last contact)."), *GetName(),
			FightClock.FightEndSeconds);
		OnFightEnded.Broadcast(this);
	}
	bWasInFight = bInFight;

	UHealthComponent* Health = Partner ? Partner->GetHealthComponent() : nullptr;
	if (Health && Health->GetHealthPercent() < SelfHealBelowFraction)
	{
		Health->Heal(Health->GetMaxHealth(), Partner);
	}

	// Left behind on the street with her on a roof: after a while he takes the grapple up.
	if (Partner && Lead && CurrentMode == EHawkeyePartnerMode::Follow && !Partner->IsZipping())
	{
		const float VerticalGap = FMath::Abs(Lead->GetActorLocation().Z - Partner->GetActorLocation().Z);
		const bool bOutOfRange = FVector::Dist(Lead->GetActorLocation(), Partner->GetActorLocation()) > FollowMaxDistance;
		OutOfRangeSeconds = bOutOfRange && VerticalGap > CatchUpVerticalGap ? OutOfRangeSeconds + DeltaSeconds : 0.f;
		if (ShouldGrappleCatchUp(VerticalGap, OutOfRangeSeconds, CatchUpVerticalGap, CatchUpDelaySeconds))
		{
			OutOfRangeSeconds = 0.f;
			GrappleCatchUp();
		}
	}
}

void AHawkeyePartnerController::SetThinkingEnabled(bool bEnabled)
{
	if (bThinkingEnabled == bEnabled)
	{
		return;
	}
	bThinkingEnabled = bEnabled;
	UWorld* World = GetWorld();
	if (!bEnabled)
	{
		CancelBowShot();
		StopMovement();
		bHasMoveGoal = false;
		if (World)
		{
			World->GetTimerManager().ClearTimer(ThinkTimerHandle);
		}
		return;
	}
	if (World && GetPawn())
	{
		World->GetTimerManager().SetTimer(ThinkTimerHandle, this, &AHawkeyePartnerController::TickThink,
			ThinkIntervalSeconds, true);
	}
}

bool AHawkeyePartnerController::IsUsingStateTree() const
{
	return StateTreeComponent && StateTreeComponent->IsRunning();
}

double AHawkeyePartnerController::GetNowSeconds() const
{
	if (bUseTestTime)
	{
		return TestTimeSeconds;
	}
	const UWorld* World = GetWorld();
	return World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
}

void AHawkeyePartnerController::SetTestTimeSeconds(double Seconds)
{
	bUseTestTime = true;
	TestTimeSeconds = Seconds;
}

FText AHawkeyePartnerController::GetStatusText() const
{
	const AHawkeyeCharacter* Partner = GetPartner();
	if (Partner && Partner->IsStaggered())
	{
		return NSLOCTEXT("Hawkeye", "PartnerDown", "down");
	}
	switch (CurrentMode)
	{
	case EHawkeyePartnerMode::Revive: return NSLOCTEXT("Hawkeye", "PartnerReviving", "reviving you");
	case EHawkeyePartnerMode::GoToMark:
		return bAtMark ? NSLOCTEXT("Hawkeye", "PartnerHolding", "holding the mark")
			: NSLOCTEXT("Hawkeye", "PartnerMoving", "moving to the mark");
	case EHawkeyePartnerMode::Cover: return NSLOCTEXT("Hawkeye", "PartnerCovering", "covering");
	case EHawkeyePartnerMode::Attack: return NSLOCTEXT("Hawkeye", "PartnerFighting", "fighting");
	case EHawkeyePartnerMode::Follow:
	default: return NSLOCTEXT("Hawkeye", "PartnerFollowing", "following");
	}
}
