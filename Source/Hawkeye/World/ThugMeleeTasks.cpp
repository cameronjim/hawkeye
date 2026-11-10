#include "World/ThugMeleeTasks.h"

#include "Hawkeye.h"
#include "Combat/MeleeComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"
#include "World/ThugAIController.h"

using HawkeyeThugNodes::GetBrain;

namespace HawkeyeMeleeTasks
{
	static bool IsSwinging(const AThugAIController& Brain)
	{
		const AThugCharacter* Thug = Brain.GetThug();
		const UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
		return Melee && Melee->IsAttacking();
	}
}

// --- Hold the ring ---------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeMeleeHoldRingTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	return Step(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeMeleeHoldRingTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Step(Context, DeltaTime);
}

EStateTreeRunStatus FHawkeyeMeleeHoldRingTask::Step(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	if (!Brain || !IsValid(Brain->GetTarget()))
	{
		return EStateTreeRunStatus::Failed;
	}
	Brain->TickMeleeCooldown(DeltaTime);
	const bool bReady = Brain->IsReadyToSwing(Brain->GetToTarget().Size2D());
	if (Brain->RequestAttackToken(EHawkeyeAttackKind::Melee, bReady).bGranted)
	{
		return EStateTreeRunStatus::Succeeded;
	}
	Brain->WaitOnRing(DeltaTime, Data.RepathRemaining);
	return EStateTreeRunStatus::Running;
}

// --- Swing -----------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeMeleeSwingTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const AThugAIController* Brain = GetBrain(Data);
	Data.bWasSwinging = Brain && HawkeyeMeleeTasks::IsSwinging(*Brain);
	Data.StepInElapsed = 0.f;
	return Step(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeMeleeSwingTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Step(Context, DeltaTime);
}

EStateTreeRunStatus FHawkeyeMeleeSwingTask::Step(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	if (!Brain || !IsValid(Brain->GetTarget()))
	{
		return EStateTreeRunStatus::Failed;
	}
	const bool bSwinging = HawkeyeMeleeTasks::IsSwinging(*Brain);
	if (Data.bWasSwinging && !bSwinging)
	{
		Brain->FinishSwing();
		return EStateTreeRunStatus::Succeeded;
	}
	Data.bWasSwinging = bSwinging;
	Brain->TickMeleeCooldown(DeltaTime);

	// The lease on the token is renewed every step; the swing itself is under way whatever the lease says.
	const float Distance = Brain->GetToTarget().Size2D();
	const FHawkeyeTokenGrant Token = Brain->RequestAttackToken(EHawkeyeAttackKind::Melee, Brain->IsReadyToSwing(Distance));
	if (!bSwinging && !Token.bGranted)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (!bSwinging && Distance <= Brain->MeleeEngageRange)
	{
		Data.bWasSwinging = Brain->StartSwing(Token, Distance);
		Data.StepInElapsed = 0.f;
	}
	else if (!bSwinging && (Data.StepInElapsed += DeltaTime) >= Brain->MeleeStepInMaxSeconds)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s stayed out of reach %.1f s after his turn came; he gives it back."),
			*GetNameSafe(Brain->GetPawn()), *GetNameSafe(Brain->GetTarget()), Brain->MeleeStepInMaxSeconds);
		Brain->ReleaseAttackToken();
		return EStateTreeRunStatus::Failed;
	}

	// Keep closing through the wind-up; stop and square up once he is in reach.
	if (Distance > Brain->MeleeCloseDistance)
	{
		Brain->RequestMoveToActor(Brain->GetTarget(), /*AcceptanceRadius=*/Brain->MeleeCloseDistance * 0.6f);
	}
	else
	{
		Brain->HoldStillFacingTarget();
	}
	return EStateTreeRunStatus::Running;
}

// --- Back off --------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeMeleeBackOffTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	Data.Elapsed = 0.f;
	return Brain && Brain->StartBackOff() ? Tick(Context, 0.f) : EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FHawkeyeMeleeBackOffTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	if (!Brain)
	{
		return EStateTreeRunStatus::Failed;
	}
	Brain->TickMeleeCooldown(DeltaTime);
	Data.Elapsed += DeltaTime;
	if (Brain->GetMoveStatus() == EPathFollowingStatus::Moving && Data.Elapsed < Brain->MeleeBackOffMaxSeconds)
	{
		return EStateTreeRunStatus::Running;
	}
	Brain->EndBackOff();
	return EStateTreeRunStatus::Succeeded;
}
