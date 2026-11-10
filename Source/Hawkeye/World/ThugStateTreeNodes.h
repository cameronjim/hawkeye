#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "World/ThugTypes.h"
#include "ThugStateTreeNodes.generated.h"

class AAIController;
class AThugAIController;
struct FStateTreeExecutionContext;

/**
 * ST_Thug's nodes. The tree decides; these read the senses and drive the actuators of AThugAIController.
 * Fight tasks live in ThugGunnerTasks.h, ThugArcherTasks.h and ThugMeleeTasks.h.
 */

/** Instance data every thug node starts from: the AI controller, auto-bound from the schema's context. */
USTRUCT()
struct HAWKEYE_API FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;
};

namespace HawkeyeThugNodes
{
	/** The thug's brain behind a node's instance data, or nullptr. */
	HAWKEYE_API AThugAIController* GetBrain(const FHawkeyeThugNodeInstanceData& Data);
}

/**
 * True when the thug senses Sense (or does not, inverted). One condition type over the controller's sense table
 * rather than an evaluator's copy: transitions fire after the tasks tick, and a copy taken before them would miss
 * what a task just changed (a token granted, a swing finished), costing a frame and, in a 0.25 s test step, a step.
 */
USTRUCT(meta = (DisplayName = "Thug Senses", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugSenseCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	FHawkeyeThugSenseCondition() = default;
	FHawkeyeThugSenseCondition(EThugSense InSense, bool bInInvert) : Sense(InSense), bInvert(bInInvert) {}

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EThugSense Sense = EThugSense::Calm;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

/** Stunned: drops whatever was in his hands and does nothing until the tree moves him on. */
USTRUCT(meta = (DisplayName = "Thug Stunned", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugStunnedTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	FHawkeyeThugStunnedTask() { bShouldCallTick = false; }

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct HAWKEYE_API FHawkeyeThugPatrolInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	/** Standing on a patrol point, and for how long. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	bool bWaiting = false;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	float WaitElapsed = 0.f;
};

/** Calm: walks his PatrolPoints, waiting PatrolWaitSeconds on each. Runs until the tree moves him on. */
USTRUCT(meta = (DisplayName = "Thug Patrol", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugPatrolTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugPatrolInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

private:
	EStateTreeRunStatus Step(FStateTreeExecutionContext& Context, float DeltaTime) const;
};

/**
 * Suspicious: walks to the last stimulus (an archer only turns to it) and looks round; his senses drop him to
 * Calm after InvestigateSeconds of it.
 */
USTRUCT(meta = (DisplayName = "Thug Investigate", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugInvestigateTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

private:
	EStateTreeRunStatus Step(FStateTreeExecutionContext& Context, float DeltaTime) const;
};

/**
 * The whole of Attack: aims at her and sets the rush speed every tick, before the weapon's own state acts;
 * leaving the fight gives back his token and lets a draw down.
 */
USTRUCT(meta = (DisplayName = "Thug Engage", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugEngageTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	FHawkeyeThugEngageTask() { bShouldStateChangeOnReselect = false; }

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

/**
 * Alerted with nothing to shoot at: a noise he has not put a body to, or smoke between them. He stands and waits;
 * losing the target on the usual clock is what sends him looking.
 */
USTRUCT(meta = (DisplayName = "Thug Hold Fire", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugHoldFireTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

private:
	EStateTreeRunStatus Step(FStateTreeExecutionContext& Context) const;
};

USTRUCT()
struct HAWKEYE_API FHawkeyeThugStandOffInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	/** Where round her he is heading, degrees. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	float OrbitYaw = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	float RepathRemaining = 0.f;
};

/**
 * She is down (claude-docs/gameplay-semantics.md, "Health and damage"): nobody piles on. A swing under way plays
 * out (it can no longer hurt her); then he circles at StandOffDistance.
 */
USTRUCT(meta = (DisplayName = "Thug Stand Off", Category = "Hawkeye"))
struct HAWKEYE_API FHawkeyeThugStandOffTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugStandOffInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
