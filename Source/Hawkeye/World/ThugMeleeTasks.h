#pragma once

#include "CoreMinimal.h"
#include "World/ThugStateTreeNodes.h"
#include "ThugMeleeTasks.generated.h"

/**
 * The melee thug's tasks in ST_Thug (claude-docs/gameplay-semantics.md, "Attack tokens" and "melee"): wait on the
 * ring for a token, close and swing with it, step back after a pair. Each counts the cooldown after a swing down
 * as it runs.
 */

USTRUCT()
struct HAWKEYE_API FHawkeyeMeleeRingInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Output")
	float RepathRemaining = 0.f;
};

/** Holds a place on the ring round her, spread from the others, asking for a melee token. Succeeded when granted one. */
USTRUCT(meta = (DisplayName = "Melee Hold Ring", Category = "Hawkeye|Melee"))
struct HAWKEYE_API FHawkeyeMeleeHoldRingTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeMeleeRingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

private:
	EStateTreeRunStatus Step(FStateTreeExecutionContext& Context, float DeltaTime) const;
};

USTRUCT()
struct HAWKEYE_API FHawkeyeMeleeSwingInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	/** The swing was under way at the last step: its end is what finishes the task. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	bool bWasSwinging = false;

	/** Holding the token without the swing started: she ran. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	float StepInElapsed = 0.f;
};

/**
 * With her melee token: closes at RushSpeed, winds up inside MeleeEngageRange (no shorter than the token's
 * minimum) while still closing to MeleeCloseDistance, and lets the swing land. Succeeded when the swing is done
 * (token back, cooldown on); Failed when the token is lost, or not used within MeleeStepInMaxSeconds.
 */
USTRUCT(meta = (DisplayName = "Melee Swing", Category = "Hawkeye|Melee"))
struct HAWKEYE_API FHawkeyeMeleeSwingTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeMeleeSwingInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

private:
	EStateTreeRunStatus Step(FStateTreeExecutionContext& Context, float DeltaTime) const;
};

USTRUCT()
struct HAWKEYE_API FHawkeyeMeleeBackOffInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Output")
	float Elapsed = 0.f;
};

/**
 * MeleeSwingsBeforeBackOff swings done: a step of MeleeBackOffDistance back from her. Succeeded when he stops or
 * after MeleeBackOffMaxSeconds, or at once with nowhere to stand.
 */
USTRUCT(meta = (DisplayName = "Melee Back Off", Category = "Hawkeye|Melee"))
struct HAWKEYE_API FHawkeyeMeleeBackOffTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeMeleeBackOffInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
