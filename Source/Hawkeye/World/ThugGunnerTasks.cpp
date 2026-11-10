#include "World/ThugGunnerTasks.h"

#include "Hawkeye.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"
#include "World/ThugAIController.h"

using HawkeyeThugNodes::GetBrain;

// --- Stance ----------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerStanceTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	if (AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->TickGunnerClocks(DeltaTime);
	}
	return EStateTreeRunStatus::Running;
}

// --- Approach --------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerApproachTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	if (AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->CancelBurst(TEXT("out of range"));
	}
	return Tick(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeGunnerApproachTask::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
	if (AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->RequestMoveToActor(Brain->GetTarget(), /*AcceptanceRadius=*/Brain->EngageRange * 0.8f);
	}
	return EStateTreeRunStatus::Running;
}

// --- Take cover ------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerTakeCoverTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this));
	if (!Brain)
	{
		return EStateTreeRunStatus::Failed;
	}
	const bool bOwed = Brain->Senses(EThugSense::CoverOwed);
	if (Brain->BeginCover(bOwed ? TEXT("fired a burst") : TEXT("aimed at")))
	{
		return EStateTreeRunStatus::Succeeded;
	}
	// Nowhere to go: he stays out, and the shots that earned the cover are spent.
	if (bOwed)
	{
		Brain->ResetShotsSinceCover();
	}
	return EStateTreeRunStatus::Failed;
}

// --- Burst -----------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerBurstTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	return Step(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeGunnerBurstTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Step(Context, DeltaTime);
}

void FHawkeyeGunnerBurstTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/) const
{
	if (AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this)))
	{
		Brain->CancelBurst(TEXT("interrupted"));
	}
}

EStateTreeRunStatus FHawkeyeGunnerBurstTask::Step(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this));
	if (!Brain || !IsValid(Brain->GetTarget()))
	{
		return EStateTreeRunStatus::Failed;
	}
	Brain->HoldStillFacingTarget();
	if (Brain->IsBursting())
	{
		return Brain->AdvanceBurst(DeltaTime) ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
	}
	if (Brain->IsJammed())
	{
		Brain->HoldFireJammed();
	}
	else if (!Brain->IsBurstPaused())
	{
		Brain->StartBurst();
	}
	return EStateTreeRunStatus::Running;
}

// --- Back away -------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerBackAwayTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	Data.Elapsed = 0.f;
	return Brain && Brain->BeginBackAway() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FHawkeyeGunnerBackAwayTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	Data.Elapsed += DeltaTime;
	return !Brain || Brain->FinishBackAway(Data.Elapsed) ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

// --- Hide ------------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerHideTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Elapsed = 0.f;
	if (AThugAIController* Brain = GetBrain(Data); Brain && Brain->Senses(EThugSense::CoverOwed))
	{
		// Back from a peek: behind the same cover.
		UE_LOG(LogHawkeye, Log, TEXT("%s: back into cover after a burst."), *GetNameSafe(Brain->GetPawn()));
		Brain->ResetShotsSinceCover();
		Brain->RequestMoveToLocation(Brain->GetCoverPoint(), 40.f, AThugAIController::WideNavSnap, TEXT("cover"));
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FHawkeyeGunnerHideTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	APawn* Thug = Brain ? Brain->GetPawn() : nullptr;
	if (!Thug || !IsValid(Brain->GetTarget()))
	{
		return EStateTreeRunStatus::Running;
	}
	Brain->HoldCover(DeltaTime);
	if (FVector::Dist2D(Thug->GetActorLocation(), Brain->GetCoverPoint()) > AThugAIController::ArriveDistance)
	{
		if (Brain->GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			Brain->RequestMoveToLocation(Brain->GetCoverPoint(), 40.f, AThugAIController::WideNavSnap, TEXT("cover"));
		}
		// Never stuck on the way: after the relocation time he gives up on this one.
		if (!Brain->Senses(EThugSense::CoverStale))
		{
			return EStateTreeRunStatus::Running;
		}
	}
	AThugAIController::FaceTarget(Thug, Brain->GetToTarget());
	Data.Elapsed += DeltaTime;
	return Data.Elapsed < Brain->CoverHideSeconds ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}

// --- Relocate --------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerRelocateTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	AThugAIController* Brain = GetBrain(Context.GetInstanceData(*this));
	if (!Brain)
	{
		return EStateTreeRunStatus::Failed;
	}
	const FVector Old = Brain->GetCoverPoint();
	if (Brain->BeginCover(TEXT("relocating"), &Old))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: relocates after %.1f s at %s."), *GetNameSafe(Brain->GetPawn()), Brain->RelocateSeconds,
			*Old.ToCompactString());
		return EStateTreeRunStatus::Succeeded;
	}
	Brain->RestartCoverClock();
	return EStateTreeRunStatus::Failed;
}

// --- Step out --------------------------------------------------------------------------------------

EStateTreeRunStatus FHawkeyeGunnerStepOutTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	if (!Brain)
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.PeekPoint = Brain->FindPeekPoint();
	Brain->RequestMoveToLocation(Data.PeekPoint, 40.f);
	UE_LOG(LogHawkeye, Log, TEXT("%s: peeks from cover at %s."), *GetNameSafe(Brain->GetPawn()), *Data.PeekPoint.ToCompactString());
	return Tick(Context, 0.f);
}

EStateTreeRunStatus FHawkeyeGunnerStepOutTask::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	AThugAIController* Brain = GetBrain(Data);
	const APawn* Thug = Brain ? Brain->GetPawn() : nullptr;
	if (!Thug)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (FVector::Dist2D(Thug->GetActorLocation(), Data.PeekPoint) <= AThugAIController::ArriveDistance)
	{
		return EStateTreeRunStatus::Succeeded;
	}
	if (Brain->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		Brain->RequestMoveToLocation(Data.PeekPoint, 40.f);
	}
	return EStateTreeRunStatus::Running;
}
