#include "Partner/PartnerStateTreeNodes.h"

#include "Partner/HawkeyePartnerController.h"
#include "StateTreeExecutionContext.h"

namespace HawkeyePartnerNodes
{
	static AHawkeyePartnerController* GetController(FStateTreeExecutionContext& Context, const FStateTreeNodeBase& Node)
	{
		const FHawkeyePartnerNodeInstanceData& Data = Context.GetInstanceData<FHawkeyePartnerNodeInstanceData>(Node);
		return Cast<AHawkeyePartnerController>(Data.AIController);
	}
}

bool FHawkeyePartnerNeedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AHawkeyePartnerController* Controller = HawkeyePartnerNodes::GetController(Context, *this);
	return Controller && Controller->HasNeed(Need);
}

EStateTreeRunStatus FHawkeyePartnerModeTask::EnterState(FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/) const
{
	AHawkeyePartnerController* Controller = HawkeyePartnerNodes::GetController(Context, *this);
	if (!Controller)
	{
		return EStateTreeRunStatus::Failed;
	}
	Controller->RunMode(Mode, 0.f);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FHawkeyePartnerModeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AHawkeyePartnerController* Controller = HawkeyePartnerNodes::GetController(Context, *this);
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
