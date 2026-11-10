#pragma once

#include "CoreMinimal.h"
#include "World/ThugStateTreeNodes.h"
#include "ThugGunnerTasks.generated.h"

/**
 * The gunner's tasks in ST_Thug (claude-docs/gameplay-semantics.md, "gunner"): close to EngageRange, a burst after
 * a telegraph, into cover after a burst or when aimed at, peek out for the next, back away when she closes.
 */

/** Parent of every state in which he shoots: the pause between bursts runs, and a burst under way keeps its token. */
USTRUCT(meta = (DisplayName = "Gunner Stance", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerStanceTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	FHawkeyeGunnerStanceTask() { bShouldStateChangeOnReselect = false; }

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

/** Out of range: walks at her until she is inside EngageRange. Runs until the tree moves him on. */
USTRUCT(meta = (DisplayName = "Gunner Approach", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerApproachTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

/**
 * Picks a cover point out of her line and sets off for it: Succeeded when there is one, Failed when there is
 * none (a burst's worth of shots then counts as spent).
 */
USTRUCT(meta = (DisplayName = "Gunner Take Cover", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerTakeCoverTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	FHawkeyeGunnerTakeCoverTask() { bShouldCallTick = false; }

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

/**
 * One burst: stands, squares up, waits out the pause and a jam, takes a ranged token, raises the pistol for the
 * telegraph and fires ShotsPerBurst. Succeeded after the last shot; leaving it early breaks the burst.
 */
USTRUCT(meta = (DisplayName = "Gunner Burst", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerBurstTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

private:
	EStateTreeRunStatus Step(FStateTreeExecutionContext& Context, float DeltaTime) const;
};

USTRUCT()
struct HAWKEYE_API FHawkeyeGunnerClockInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Output")
	float Elapsed = 0.f;
};

/** She closed inside RetreatTriggerDistance: backs off to RetreatToDistance. Succeeded when far enough, there, or out of time. */
USTRUCT(meta = (DisplayName = "Gunner Back Away", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerBackAwayTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeGunnerClockInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

/**
 * Behind cover: to the cover point (giving up on the way after RelocateSeconds), then CoverHideSeconds hidden,
 * pistol down. Succeeded when the hide is over.
 */
USTRUCT(meta = (DisplayName = "Gunner Hide", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerHideTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeGunnerClockInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

/** Held this cover RelocateSeconds: to another. Succeeded with one; Failed (this one counts as fresh) without. */
USTRUCT(meta = (DisplayName = "Gunner Relocate", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerRelocateTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeThugNodeInstanceData;

	FHawkeyeGunnerRelocateTask() { bShouldCallTick = false; }

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct HAWKEYE_API FHawkeyeGunnerStepOutInstanceData : public FHawkeyeThugNodeInstanceData
{
	GENERATED_BODY()

	/** The spot beside the cover with a line on her. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	FVector PeekPoint = FVector::ZeroVector;
};

/** Steps out from cover to the nearest spot with a line on her. Succeeded on arrival. */
USTRUCT(meta = (DisplayName = "Gunner Step Out", Category = "Hawkeye|Gunner"))
struct HAWKEYE_API FHawkeyeGunnerStepOutTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FHawkeyeGunnerStepOutInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
