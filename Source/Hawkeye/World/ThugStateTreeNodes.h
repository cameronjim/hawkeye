#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "World/ThugTypes.h"
#include "ThugStateTreeNodes.generated.h"

class AAIController;

/** Instance data shared by the thug nodes: the AI controller, auto-bound from the schema's context. */
USTRUCT()
struct HAWKEYE_API FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;
};

/** ST_Thug's enter condition: true while AThugAIController has a reason to run Need. */
USTRUCT(meta = (DisplayName = "Hawkeye Thug Need", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugNeedCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EThugMode Need = EThugMode::Patrol;
};

/**
 * ST_Thug's task: runs one mode on AThugAIController every tick, and succeeds (so the state's
 * transition goes back to the root to choose again) once ChooseMode picks a different one.
 */
USTRUCT(meta = (DisplayName = "Hawkeye Thug Mode", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugModeTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EThugMode Mode = EThugMode::Patrol;
};
