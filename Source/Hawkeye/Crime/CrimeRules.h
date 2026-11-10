#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Crime/CrimeTypes.h"
#include "CrimeRules.generated.h"

/**
 * The street crimes' rules, pure and world-free: when the next one is due, where it may start, when the
 * victim is lost, when its thugs go. UCrimeSubsystem and UCrimeTracker apply them; tests call them
 * directly.
 *
 *   - Every 90 to 150 s of roaming (alive, not in a fight, not in a challenge or a chapter beat, no crime
 *     on) a crime is due. It starts at a City_CrimeSpot_ 25 to 40 m from the player, at least 60 m from
 *     the last crime's spot, preferring one out of her camera's sight; with none, it waits and tries again.
 *   - Never more than one crime at a time. A running crime pauses (its clocks stop) during a challenge
 *     or a chapter beat.
 *   - Mugging fails when the victim has taken VictimHitsToFail hits: one every TimeToFail / Hits seconds
 *     while a thug stands over him and is not fighting the player. Robbery fails when the runner reaches
 *     the escape point with the loot. Any crime is abandoned when the player goes AbandonDistance away or
 *     goes down. All thugs down (or, robbery, the loot picked up) stops it: RewardArrows standard arrows,
 *     a toast, and a completion counted per type in the save.
 *   - A stopped crime's thugs go 60 s after the end once the player is 40 m away; a failed one's after 20 s.
 */
UCLASS()
class HAWKEYE_API UCrimeRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Seconds of roaming to the next crime: Override when above 0 (the cvar), else uniform in [Min, Max]. */
	static float RollInterval(float MinSeconds, float MaxSeconds, float Override, FRandomStream& Stream);

	/** The schedule clock runs only while this holds: no crime on, no fight, no challenge, no chapter beat, player up. */
	UFUNCTION(BlueprintPure, Category = "Crime|Rules")
	static bool IsRoaming(bool bCrimeActive, bool bInCombat, bool bInChallenge, bool bInChapterBeat, bool bPlayerOut);

	/**
	 * Whether a crime may start at Spot: MinDistance to MaxDistance from Player on the ground plane (both
	 * ends count) and, when there was a last crime, at least LastApart from its spot.
	 */
	UFUNCTION(BlueprintPure, Category = "Crime|Rules")
	static bool IsSpotEligible(const FVector& Spot, const FVector& Player, bool bHasLast, const FVector& LastSpot,
		float MinDistance, float MaxDistance, float LastApart);

	/**
	 * The spot a due crime starts at: an eligible one out of sight if any, else any eligible one, chosen
	 * at random among those. INDEX_NONE when none is eligible.
	 */
	static int32 PickSpot(const TArray<FCrimeSpotCandidate>& Spots, const FVector& Player, bool bHasLast, const FVector& LastSpot,
		float MinDistance, float MaxDistance, float LastApart, FRandomStream& Stream);

	/**
	 * How many hits the victim has taken after SecondsOnVictim of thugs standing over him: one every
	 * TimeToFail / HitsToFail seconds, so the last lands at TimeToFail. Never more than HitsToFail.
	 */
	UFUNCTION(BlueprintPure, Category = "Crime|Rules")
	static int32 VictimHitsAfter(float SecondsOnVictim, float TimeToFailSeconds, int32 HitsToFail);

	/**
	 * Whether a crime's leftover (thug, victim, loot) should go now: a stopped crime's after
	 * CompleteDelay once the player is at least FarDistance away; a failed one's after FailDelay.
	 */
	UFUNCTION(BlueprintPure, Category = "Crime|Rules")
	static bool ShouldDespawn(float SecondsSinceEnd, bool bCompleted, float DistanceToPlayer, float CompleteDelay,
		float FailDelay, float FarDistance);

	/**
	 * Where roster thug Index of Count stands at an alley spot, in the spot's frame (X along the passage, Y across
	 * it), for a crime that spreads its roster (the ambush): pairs at either end, the first pair Radius out, each
	 * next pair 40% of Radius nearer, every other pair LateralCm to the other side. Never further across than
	 * LateralCm, so a 3 m passage holds them.
	 */
	static FVector2D AlleyRosterOffset(int32 Index, int32 Count, float Radius, float LateralCm = 60.f);

	/** "mugging", "robbery", "ambush", "rooftop". */
	UFUNCTION(BlueprintPure, Category = "Crime|Rules")
	static FText TypeText(ECrimeType Type);

	/** The HUD line: "[Crime: mugging] 38 m" (the name, then the distance rounded to whole metres). */
	UFUNCTION(BlueprintPure, Category = "Crime|Rules")
	static FText FormatHudLine(const FText& Name, float DistanceCm);

	/** "[Crime stopped]", "[Victim lost]", "[They got away]", "[Crime abandoned]". */
	UFUNCTION(BlueprintPure, Category = "Crime|Rules")
	static FText EndReasonText(ECrimeEndReason Reason);

	/** The save's key for a type ("Mugging"), and back. False for an unknown key. */
	static FName TypeKey(ECrimeType Type);
	static bool TypeFromKey(FName Key, ECrimeType& OutType);

	/** A crime type named on the console ("mugging", any case). False for anything else. */
	static bool ParseType(const FString& Text, ECrimeType& OutType);
};
