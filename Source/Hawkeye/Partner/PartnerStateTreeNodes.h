#pragma once

#include "CoreMinimal.h"
#include "Partner/PartnerTypes.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "PartnerStateTreeNodes.generated.h"

class AAIController;

/** Instance data shared by the partner nodes: the AI controller, auto-bound from the schema's context. */
USTRUCT()
struct HAWKEYE_API FHawkeyePartnerNodeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;
};

/**
 * ST_Partner's enter condition: true while AHawkeyePartnerController has a reason to run Need
 * (the lead is down, a mark is set, he was shot at, the lead hit someone).
 */
USTRUCT(meta = (DisplayName = "Hawkeye Partner Need", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyePartnerNeedCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyePartnerNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EHawkeyePartnerMode Need = EHawkeyePartnerMode::Follow;
};

/**
 * ST_Partner's task: runs one mode on AHawkeyePartnerController every tick, and succeeds (so the
 * state's transition goes back to the root to choose again) once a higher-priority need appears or
 * its own need is gone.
 */
USTRUCT(meta = (DisplayName = "Hawkeye Partner Mode", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyePartnerModeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyePartnerNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EHawkeyePartnerMode Mode = EHawkeyePartnerMode::Follow;
};
