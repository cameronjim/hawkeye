#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ReturnFlight.generated.h"

/**
 * How a returning arrow flies (claude-docs/gameplay-semantics.md, "Trick arrows", returning arrows): straight out with
 * no drop, through up to MaxTargets bodies (each staggered and hurt), turning back at the first wall or switch, after
 * its last target or at MaxRange; then back along the path it flew to whoever loosed it, hitting bodies it had not hit
 * yet on the way (still up to MaxTargets in all). Data on UArrowDefinition::ReturnFlight; AReturningArrowProjectile
 * flies it.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeReturnFlight
{
	GENERATED_BODY()

	/** Bodies it hurts in one flight, out and back together. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "1"))
	int32 MaxTargets = 2;

	/** How far out it goes before turning back with nothing hit, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "100.0"))
	float MaxRange = 2200.f;

	/** Its speed out and back, cm/s (no gravity). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "100.0"))
	float Speed = 2600.f;

	/** She catches it when it comes at her from within this many degrees of where she faces, and she is not busy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float CatchConeDegrees = 70.f;

	/** Within this of her chest it has reached her (caught, or hit her), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "10.0"))
	float ArriveRadius = 70.f;

	/** What it does to her when she does not catch it: a token hurt that never takes her under 1 health. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "0.0"))
	float SelfHitDamage = 2.f;

	/** The path is remembered as a point every this many cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "5.0"))
	float PathStep = 50.f;

	/** A flight longer than this ends where it is: back in the quiver (a safety for a stuck flight), s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Return", meta = (ClampMin = "0.5"))
	float MaxFlightSeconds = 6.f;
};

/** Why an outbound returning arrow turns back. */
UENUM(BlueprintType)
enum class EHawkeyeReturnTurn : uint8
{
	/** It goes on. */
	None,
	/** It met a wall, a floor or anything that is not a body. */
	Wall,
	/** It tripped a switch (AArrowSwitch). */
	Switch,
	/** It has hurt as many as it can. */
	Targets,
	/** It went as far as it goes. */
	Range
};

/** The pure rules of a returning arrow; AReturningArrowProjectile applies them. */
UCLASS()
class HAWKEYE_API UHawkeyeReturnFlightRules : public UObject
{
	GENERATED_BODY()

public:
	/** Another body may be hurt: fewer than MaxTargets so far. */
	static bool CanHitMore(int32 HitsSoFar, int32 MaxTargets);

	/** Whether (and why) the outbound flight turns back now. A wall or a switch first, then the targets, then the range. */
	static EHawkeyeReturnTurn TurnBack(float DistanceOut, float MaxRange, int32 HitsSoFar, int32 MaxTargets, bool bHitWall, bool bHitSwitch);

	/**
	 * The way back: the outbound path's points from its last to its first, then Catcher (where she is now). Its length.
	 */
	static float ReturnLength(const TArray<FVector>& OutPath, const FVector& Catcher);

	/** The point Travelled cm along the way back (clamped to its ends). */
	static FVector PointAlongReturn(const TArray<FVector>& OutPath, const FVector& Catcher, float Travelled);

	/**
	 * She catches it: it is coming at her (along Velocity) from within ConeDegrees of where she faces (Facing, flat),
	 * and she is not busy (a takedown, a traversal, a zip, staggered, down).
	 */
	static bool IsCaught(const FVector& Velocity, const FVector& Facing, bool bBusy, float ConeDegrees);

	/** The self-hit's damage: Damage, never more than takes CurrentHealth to 1, never negative. */
	static float SelfHitDamage(float Damage, float CurrentHealth);
};
