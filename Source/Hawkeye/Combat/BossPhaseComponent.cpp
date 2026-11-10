#include "Combat/BossPhaseComponent.h"

#include "Hawkeye.h"
#include "Boss/BossRules.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

UBossPhaseComponent::UBossPhaseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBossPhaseComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	UHealthComponent* OwnerHealth = Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;

	if (!OwnerHealth)
	{
		UE_LOG(LogHawkeye, Error, TEXT("%s has a UBossPhaseComponent but no UHealthComponent; phases are disabled."),
			Owner ? *Owner->GetName() : TEXT("<no owner>"));
		return;
	}

	Bind(OwnerHealth);
}

void UBossPhaseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HealthComponent)
	{
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &UBossPhaseComponent::HandleHealthChanged);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransitionTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UBossPhaseComponent::Bind(UHealthComponent* InHealthComponent)
{
	if (!InHealthComponent)
	{
		UE_LOG(LogHawkeye, Error, TEXT("%s: UBossPhaseComponent::Bind called with no health component."), *GetNameSafe(this));
		return;
	}

	// Bound again to the same health (BeginPlay, then the caller): the phase's own invulnerability is not scripted.
	const bool bScripted = InHealthComponent->IsInvulnerable()
		&& !(InHealthComponent == HealthComponent && (bPhaseHoldsInvulnerability || bTransitioning));
	if (HealthComponent)
	{
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &UBossPhaseComponent::HandleHealthChanged);
	}

	SortPhases();

	HealthComponent = InHealthComponent;
	HealthComponent->OnHealthChanged.AddDynamic(this, &UBossPhaseComponent::HandleHealthChanged);

	// Phase 0 is active from the start and is not a transition, so nothing is broadcast here.
	CurrentPhaseIndex = Phases.Num() > 0 ? 0 : INDEX_NONE;
	bCrewDown = false;
	bTransitioning = false;
	bPhaseHoldsInvulnerability = false;
	if (Phases.IsValidIndex(CurrentPhaseIndex))
	{
		ApplyPhaseInvulnerability(Phases[CurrentPhaseIndex], bScripted);
	}
}

void UBossPhaseComponent::ApplyPhaseInvulnerability(const FBossPhase& Phase, bool bScripted)
{
	if (!HealthComponent)
	{
		return;
	}
	HealthComponent->SetInvulnerable(bScripted || Phase.bBossInvulnerable);
	bPhaseHoldsInvulnerability = Phase.bBossInvulnerable && !bScripted;
}

void UBossPhaseComponent::NotifyCrewDown()
{
	if (!HealthComponent || HealthComponent->IsDead() || bCrewDown)
	{
		return;
	}
	bCrewDown = true;
	const int32 Target = UBossRules::ResolvePhase(Phases, CurrentPhaseIndex, HealthComponent->GetHealthPercent(), true, false);
	if (Target != CurrentPhaseIndex)
	{
		EnterPhase(Target);
	}
}

void UBossPhaseComponent::RestorePhase(int32 PhaseIndex)
{
	if (!Phases.IsValidIndex(PhaseIndex))
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransitionTimerHandle);
	}
	const bool bScripted = HealthComponent && HealthComponent->IsInvulnerable() && !bPhaseHoldsInvulnerability && !bTransitioning;
	bTransitioning = false;
	bCrewDown = false;
	CurrentPhaseIndex = PhaseIndex;
	ApplyPhaseInvulnerability(Phases[PhaseIndex], bScripted);
	UE_LOG(LogHawkeye, Log, TEXT("Boss %s restored to phase %d ('%s')."), *GetNameSafe(GetOwner()), PhaseIndex,
		*Phases[PhaseIndex].PhaseName.ToString());
}

void UBossPhaseComponent::SortPhases()
{
	Phases.StableSort([](const FBossPhase& A, const FBossPhase& B)
	{
		return A.HealthThresholdPercent > B.HealthThresholdPercent;
	});
}

FBossPhase UBossPhaseComponent::GetCurrentPhase() const
{
	return Phases.IsValidIndex(CurrentPhaseIndex) ? Phases[CurrentPhaseIndex] : FBossPhase();
}

FName UBossPhaseComponent::GetCurrentBehaviorTag() const
{
	return Phases.IsValidIndex(CurrentPhaseIndex) ? Phases[CurrentPhaseIndex].BehaviorTag : NAME_None;
}

void UBossPhaseComponent::HandleHealthChanged(UHealthComponent* /*InHealthComponent*/, float /*NewHealth*/, float Delta, AActor* /*DamageInstigator*/)
{
	// Heals never move the fight backwards, and death belongs to the boss Blueprint, not to phases.
	if (Delta >= 0.f || !HealthComponent || HealthComponent->IsDead())
	{
		return;
	}

	// Skip ahead through every threshold a single big hit crossed (never past a phase its crew opens), then
	// broadcast once.
	const int32 TargetPhase = UBossRules::ResolvePhase(Phases, CurrentPhaseIndex, HealthComponent->GetHealthPercent(), bCrewDown,
		HealthComponent->IsDead());

	if (TargetPhase != CurrentPhaseIndex)
	{
		EnterPhase(TargetPhase);
	}
}

void UBossPhaseComponent::EnterPhase(int32 PhaseIndex)
{
	if (!Phases.IsValidIndex(PhaseIndex) || PhaseIndex == CurrentPhaseIndex)
	{
		return;
	}

	const int32 OldPhaseIndex = CurrentPhaseIndex;
	CurrentPhaseIndex = PhaseIndex;
	bCrewDown = false;

	const FBossPhase& Phase = Phases[PhaseIndex];

	UE_LOG(LogHawkeye, Log, TEXT("Boss %s entering phase %d ('%s', behaviour '%s')."),
		*GetNameSafe(GetOwner()), PhaseIndex, *Phase.PhaseName.ToString(), *Phase.BehaviorTag.ToString());

	// Invulnerability goes up before the broadcast so listeners see the transition state, and the
	// broadcast still lands before the window elapses so the behaviour tree can swap in time.
	BeginTransition(Phase);

	OnPhaseChanged.Broadcast(OldPhaseIndex, PhaseIndex, Phase);

	ScheduleTransitionEnd(Phase);
}

void UBossPhaseComponent::BeginTransition(const FBossPhase& Phase)
{
	// Capture the pre-transition invulnerability only once: a boss already invulnerable for
	// scripted reasons must stay that way when the transition ends.
	if (!bTransitioning)
	{
		bInvulnerableBeforeTransition = HealthComponent && HealthComponent->IsInvulnerable() && !bPhaseHoldsInvulnerability;
	}
	bPhaseHoldsInvulnerability = false;

	bTransitioning = true;

	if (Phase.bInvulnerableDuringTransition && HealthComponent)
	{
		HealthComponent->SetInvulnerable(true);
	}
}

void UBossPhaseComponent::ScheduleTransitionEnd(const FBossPhase& Phase)
{
	UWorld* World = GetWorld();
	if (World && Phase.TransitionSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(
			TransitionTimerHandle, this, &UBossPhaseComponent::FinishTransition, Phase.TransitionSeconds, false);
		return;
	}

	// Timers need a world. Without one (automation tests, or a zero-length transition) the
	// invulnerability is set and restored in the same call so the contract still holds.
	FinishTransition();
}

void UBossPhaseComponent::FinishTransition()
{
	bTransitioning = false;

	// Back to whatever scripted state it had, plus the new phase's own (a crew phase's boss stays out of it).
	if (Phases.IsValidIndex(CurrentPhaseIndex))
	{
		ApplyPhaseInvulnerability(Phases[CurrentPhaseIndex], bInvulnerableBeforeTransition);
	}
	else if (HealthComponent)
	{
		HealthComponent->SetInvulnerable(bInvulnerableBeforeTransition);
	}

	OnPhaseTransitionFinished.Broadcast(CurrentPhaseIndex);
}
