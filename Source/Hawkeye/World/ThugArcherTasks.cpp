#include "World/ThugArcherTasks.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"
#include "World/ThugAIController.h"

using HawkeyeThugNodes::GetBrain;

// --- Draw ------------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeArcherDrawTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	return Step(Context);
}

EStateTreeRunStatus FHawkeyeArcherDrawTask::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
	return Step(Context);
}

void FHawkeyeArcherDrawTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/) const
{
	if (AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->CancelArcherDraw(TEXT("left the draw"));
	}
}

EStateTreeRunStatus FHawkeyeArcherDrawTask::Step(FStateTreeExecutionContext& Context) const
{
	AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this));
	if (!Brain || !IsValid(Brain->GetTarget()))
	{
		return EStateTreeRunStatus::Failed;
	}
	const bool bDrawing = Brain->IsDrawingBow();
	if (bDrawing)
	{
		Brain->RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
	}
	// Holding a draw on where she went: the hold clock watches for her, nothing else moves him.
	if (Brain->IsHoldingDraw())
	{
		return EStateTreeRunStatus::Running;
	}
	// Just loosed: he stands there, bow down, open to a shot back.
	if (Brain->IsInLooseWindow())
	{
		Brain->HoldStillFacingTarget();
		return EStateTreeRunStatus::Running;
	}
	if (Brain->StepBackFromTarget() || (Brain->GetMoveStatus() == EPathFollowingStatus::Moving && !bDrawing))
	{
		return EStateTreeRunStatus::Running;
	}
	Brain->HoldStillFacingTarget();
	const FVector Aim = Brain->AimBow();
	const bool bLine = Brain->HasLineToTarget();
	if (!bDrawing)
	{
		if (bLine && Brain->IsArcherGapOver())
		{
			Brain->StartArcherDraw();
		}
		return EStateTreeRunStatus::Running;
	}
	if (!bLine)
	{
		Brain->BeginArcherHold(Aim);
		return EStateTreeRunStatus::Running;
	}
	if (!Brain->IsDrawFull())
	{
		return EStateTreeRunStatus::Running;
	}
	Brain->LooseAtTarget();
	return EStateTreeRunStatus::Succeeded;
}

// --- Zip -------------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeArcherZipTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	const APawn* Thug = Brain ? Brain->GetPawn() : nullptr;
	if (!Thug || !Brain->FindArcherRoof(Data.To))
	{
		return EStateTreeRunStatus::Failed;
	}
	Brain->BeginZip();
	Data.From = Thug->GetActorLocation();
	Data.Elapsed = 0.f;
	Data.Seconds = FMath::Max(FVector::Dist(Data.From, Data.To) / Brain->ArcherZipSpeed, 0.1f);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FHawkeyeArcherZipTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	APawn* Thug = Brain ? Brain->GetPawn() : nullptr;
	if (!Thug)
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.Elapsed += DeltaTime;
	const float Alpha = FMath::Clamp(Data.Elapsed / Data.Seconds, 0.f, 1.f);
	Thug->SetActorLocation(FMath::Lerp(Data.From, Data.To, Alpha), false, nullptr, ETeleportType::TeleportPhysics);
	return Alpha < 1.f ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}

void FHawkeyeArcherZipTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/) const
{
	// Landed, or stunned in the air: either way he is back on his feet where he is.
	if (AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->EndZip();
	}
}
