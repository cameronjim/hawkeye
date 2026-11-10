#include "World/ThugStateTreeNodes.h"

#include "StateTreeExecutionContext.h"
#include "World/ThugAIController.h"

namespace HawkeyeThugNodes
{
	static AThugAIController* GetController(FStateTreeExecutionContext& Context, const FStateTreeNodeBase& Node)
	{
		const FHawkeyeThugNodeInstanceData& Data = Context.GetInstanceData<FHawkeyeThugNodeInstanceData>(Node);
		return Cast<AThugAIController>(Data.AIController);
	}
}

bool FHawkeyeThugNeedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AThugAIController* Controller = HawkeyeThugNodes::GetController(Context, *this);
	return Controller && Controller->HasNeed(Need);
}

EStateTreeRunStatus FHawkeyeThugModeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	AThugAIController* Controller = HawkeyeThugNodes::GetController(Context, *this);
	if (!Controller)
	{
		return EStateTreeRunStatus::Failed;
	}
	Controller->RunMode(Mode, 0.f);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FHawkeyeThugModeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AThugAIController* Controller = HawkeyeThugNodes::GetController(Context, *this);
	if (!Controller)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Controller->ChooseMode() != Mode)
	{
		return EStateTreeRunStatus::Succeeded;
	}
	Controller->RunMode(Mode, DeltaTime);
	return EStateTreeRunStatus::Running;
}
