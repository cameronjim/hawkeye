#include "World/ThugStateTreeNodes.h"

#include "Hawkeye.h"
#include "Combat/MeleeComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

AThugAIController* HawkeyeThugNodes::GetBrain(const FHawkeyeThugNodeInstanceData& Data)
{
	return Cast<AThugAIController>(Data.AIController);
}

// --- Senses ----------------------------------------------------------------------------------------

bool FHawkeyeThugSenseCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Context.GetInstanceData(*this));
	return Brain && Brain->Senses(Sense) != bInvert;
}

#if WITH_EDITOR
FText FHawkeyeThugSenseCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FText SenseName = StaticEnum<EThugSense>()->GetDisplayNameTextByValue(static_cast<int64>(Sense));
	return bInvert ? FText::Format(NSLOCTEXT("HawkeyeThug", "NotSense", "not {0}"), SenseName) : SenseName;
}
#endif

// --- Stunned ---------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeThugStunnedTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	if (AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->StandDown(TEXT("stunned"));
	}
	return EStateTreeRunStatus::Running;
}

// --- Patrol ----------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeThugPatrolTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	return Step(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeThugPatrolTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Step(Context, DeltaTime);
}

void FHawkeyeThugPatrolTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/) const
{
	if (AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->SetLookPitch(0.f);
	}
}

EStateTreeRunStatus FHawkeyeThugPatrolTask::Step(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Data);
	if (!Brain || !Brain->HasPatrolRoute())
	{
		return EStateTreeRunStatus::Running;
	}
	if (Data.bWaiting)
	{
		Brain->LookFromPatrolPoint();
		Data.WaitElapsed += DeltaTime;
		if (Data.WaitElapsed < Brain->GetThug()->PatrolWaitSeconds)
		{
			return EStateTreeRunStatus::Running;
		}
		Data.bWaiting = false;
		Data.WaitElapsed = 0.f;
		Brain->NextPatrolPoint();
	}
	Data.bWaiting = Brain->WalkToPatrolPoint();
	return EStateTreeRunStatus::Running;
}

// --- Investigate -----------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeThugInvestigateTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	return Step(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeThugInvestigateTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Step(Context, DeltaTime);
}

EStateTreeRunStatus FHawkeyeThugInvestigateTask::Step(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Context.GetInstanceData(*this));
	AThugCharacter* Thug = Brain ? Brain->GetThug() : nullptr;
	if (!Thug)
	{
		return EStateTreeRunStatus::Running;
	}
	const FVector Noise = Brain->GetLastStimulusLocation();
	// An archer holds his roof: he turns to the noise instead of walking off to it.
	if (Thug->IsArcher())
	{
		AThugAIController::FaceTarget(Thug, Noise - Thug->GetActorLocation());
	}
	else if (Brain->GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		return EStateTreeRunStatus::Running;
	}
	else if (FVector::DistSquared2D(Thug->GetActorLocation(), Noise) > FMath::Square(120.f))
	{
		// A noise he cannot walk to (on the roof over him, in the air where she zipped) he watches from here.
		if (Brain->RequestMoveToLocation(Noise, /*AcceptanceRadius=*/60.f, AThugAIController::WideNavSnap, TEXT("the noise")))
		{
			return EStateTreeRunStatus::Running;
		}
		AThugAIController::FaceTarget(Thug, Noise - Thug->GetActorLocation());
	}
	Brain->AdvanceInvestigation(DeltaTime);
	return EStateTreeRunStatus::Running;
}

// --- Engage ----------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeThugEngageTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	return Tick(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeThugEngageTask::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
	if (AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->ApplyMoveSpeed();
		Brain->AimAtTarget();
	}
	return EStateTreeRunStatus::Running;
}

void FHawkeyeThugEngageTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/) const
{
	// Staggered, knocked down, held, dead, or off to cover or a zip: his turn is over.
	if (AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->CancelArcherDraw(TEXT("left the attack"));
		Brain->ReleaseAttackToken();
	}
}

// --- Hold fire -------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeThugHoldFireTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	return Step(Context);
}

EStateTreeRunStatus FHawkeyeThugHoldFireTask::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
	return Step(Context);
}

EStateTreeRunStatus FHawkeyeThugHoldFireTask::Step(FStateTreeExecutionContext& Context) const
{
	AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Context.GetInstanceData(*this));
	if (!Brain)
	{
		return EStateTreeRunStatus::Running;
	}
	if (Brain->IsBlinded() && IsValid(Brain->GetTarget()))
	{
		Brain->StopMovement();
		Brain->StandDown(TEXT("blinded"));
		return EStateTreeRunStatus::Running;
	}
	Brain->ReleaseAttackToken();
	return EStateTreeRunStatus::Running;
}

// --- Stand off -------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeThugStandOffTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Data);
	if (!Brain || !IsValid(Brain->GetTarget()))
	{
		return EStateTreeRunStatus::Running;
	}
	// Round her from the side he is already on, so he steps back rather than across her.
	Data.OrbitYaw = (-Brain->GetToTarget()).Rotation().Yaw;
	Data.RepathRemaining = 0.f;
	Brain->StandDown(TEXT("target down"));
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s is down; backing off to circle at %.0f cm."), *GetNameSafe(Brain->GetPawn()),
		*GetNameSafe(Brain->GetTarget()), Brain->StandOffDistance);
	return Tick(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeThugStandOffTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = HawkeyeThugNodes::GetBrain(Data);
	const AThugCharacter* Thug = Brain ? Brain->GetThug() : nullptr;
	if (!Thug)
	{
		return EStateTreeRunStatus::Running;
	}
	const UMeleeComponent* Melee = Thug->GetMeleeComponent();
	if (Melee && Melee->IsAttacking())
	{
		return EStateTreeRunStatus::Running;
	}
	Data.OrbitYaw = FMath::UnwindDegrees(Data.OrbitYaw + Brain->StandOffOrbitDegreesPerSecond * DeltaTime);
	Brain->CircleTarget(DeltaTime, Brain->StandOffDistance, Data.OrbitYaw, Data.RepathRemaining, TEXT("the circle round her"));
	return EStateTreeRunStatus::Running;
}
