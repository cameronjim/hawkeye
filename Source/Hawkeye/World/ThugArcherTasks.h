#pragma once

#include "CoreMinimal.h"
#include "World/ThugStateTreeNodes.h"
#include "ThugArcherTasks.generated.h"

/**
 * The archer's tasks in ST_Thug (claude-docs/gameplay-semantics.md, "archer"): keep the band, draw and loose
 * with lead; zip to another roof when she closes in or is out of reach.
 */

/**
 * One arrow: keeps ArcherMinRange with a step back, waits out the gap after the last arrow, takes a ranged token,
 * draws (the creak and the glint), holds the draw if she breaks his line (AThugAIController's hold clock) and
 * looses at full draw. Succeeded once the arrow is away; leaving it early lets the string down.
 */
USTRUCT(meta = (DisplayName = "Archer Draw", Category = "Hawkeye|Archer"))
struct HAWKEYE_API FHawkeyeArcherDrawTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

private:
	EStateTreeRunStatus Step(FStateTreeExecutionContext& Context) const;
};

USTRUCT()
struct HAWKEYE_API FHawkeyeArcherZipInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Output")
	FVector From = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	FVector To = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	float Elapsed = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	float Seconds = 0.f;
};

/**
 * Zips to the grapple anchor whose landing is best for the band, a straight kinematic line at ArcherZipSpeed.
 * Failed when no anchor lands in the band with a line on her; Succeeded on landing.
 */
USTRUCT(meta = (DisplayName = "Archer Zip", Category = "Hawkeye|Archer"))
struct HAWKEYE_API FHawkeyeArcherZipTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeArcherZipInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
