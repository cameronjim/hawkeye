#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Settings/DifficultySubsystem.h"
#include "CombatTokenRules.generated.h"

/** Which of a target's two token budgets an attack draws on. */
UENUM(BlueprintType)
enum class EHawkeyeAttackKind : uint8
{
	/** Fists, bat, the heavy's bash and slow swing. */
	Melee,
	/** A gunner's burst, an archer's draw. */
	Ranged
};

/** One attacker waiting on a budget, as the ranking sees him. */
struct FHawkeyeTokenRequest
{
	/** Flat distance to the target, cm. */
	float Distance = 0.f;

	/** Degrees between where the target looks and the line from her view to him. */
	float ViewAngleDegrees = 0.f;

	/** Seconds he has been ready and asking. */
	float WaitSeconds = 0.f;

	/** World seconds he was last granted this budget's token; below 0 for never. */
	double LastGrantSeconds = -1.0;
};

/** What a request for a token got. */
struct FHawkeyeTokenGrant
{
	bool bGranted = false;

	/** The shortest his telegraph may be: 0 when granted on her screen, the difficulty's minimum off it. */
	float MinTelegraphSeconds = 0.f;
};

/**
 * The attack token rules (gameplay-semantics.md, "Attack tokens"): who of the attackers waiting on one of a
 * target's budgets is granted next, when the budget may grant, how long an off-screen telegraph lasts, and
 * where the waiting melee thugs stand on the ring. Pure, so the tests check them without a fight;
 * UCombatTokenSubsystem keeps the state and the clock.
 */
UCLASS()
class HAWKEYE_API UHawkeyeCombatTokenRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The difficulty stat holding Kind's budget: MeleeTokens or RangedTokens. */
	static EDifficultyStat GetBudgetStat(EHawkeyeAttackKind Kind);

	/**
	 * Whether a budget of Budget tokens, Held of them out, may grant at NowSeconds: one is free and GapSeconds
	 * have passed since one was last given back (LastReleaseSeconds).
	 */
	static bool CanGrant(int32 Budget, int32 Held, double NowSeconds, double LastReleaseSeconds, float GapSeconds);

	/** Degrees between ViewForward from ViewLocation and the line to Point. 0 when either is degenerate. */
	static float ComputeViewAngle(const FVector& ViewLocation, const FVector& ViewForward, const FVector& Point);

	/** On her screen: within OnScreenHalfAngleDegrees of where she looks. */
	static bool IsOnScreen(float ViewAngleDegrees)
	{
		return ViewAngleDegrees <= OnScreenHalfAngleDegrees;
	}

	/** The shortest telegraph for an attack granted at ViewAngleDegrees: 0 on screen, OffScreenSeconds off it. */
	static float GetMinTelegraphSeconds(float ViewAngleDegrees, float OffScreenSeconds);

	/**
	 * Requests' indices, first in line first: the overdue (waited OverdueSeconds, longest first), then all but
	 * the one granted most recently, then on screen before off, then nearest in DistanceStep steps, then the
	 * longest wait, then the order given. Deterministic.
	 */
	static void RankRequests(const TArray<FHawkeyeTokenRequest>& Requests, TArray<int32>& OutOrder);

	/**
	 * Places on a ring for attackers standing at BearingsDegrees round a target (yaw from her to each): at least
	 * MinSeparationDegrees apart (or 360 / count when that cannot fit), in the order they stand, each as close to
	 * his own bearing as the spacing allows (least squares). OutSlotsDegrees is in the order of the input, in
	 * (-180, 180]. Deterministic; no random slots.
	 */
	static void SpreadRingSlots(
		const TArray<float>& BearingsDegrees, float MinSeparationDegrees, TArray<float>& OutSlotsDegrees);

	/** Within this of where she looks an attacker is on her screen, degrees. */
	static constexpr float OnScreenHalfAngleDegrees = 50.f;

	/** Waiting this long puts an attacker at the front, whatever else: nobody starves, seconds. */
	static constexpr float OverdueSeconds = 4.f;

	/** Distances within the same step of this rank as equal, so the longer wait decides, cm. */
	static constexpr float DistanceStep = 50.f;

	/** A holder or waiter who has not asked for this long is dropped (he stopped thinking), seconds. */
	static constexpr float LeaseSeconds = 0.75f;

	/** Nobody holds a token longer than this, asking or not: the longest attack (a held draw) is under 5 s. */
	static constexpr float MaxHoldSeconds = 6.f;
};
