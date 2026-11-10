#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Challenge/ChallengeTypes.h"
#include "ChallengeRules.generated.h"

class UChallengeDefinition;

/**
 * The side challenges' arithmetic, pure and world-free: what an arrow scores, what a run's score or
 * time earns, when a run fails. UChallengeTracker applies it; tests call it directly.
 * Rules: claude-docs/gameplay-semantics.md, "Side challenges".
 */
UCLASS()
class HAWKEYE_API UChallengeRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Points for an arrow RadialCm from a target's centre, in the plane of its face: CentrePoints inside
	 * BullseyeRadius, RingPoints inside RingRadius, OuterPoints inside OuterRadius, nothing beyond. The
	 * edges count inward (exactly on the bullseye's edge is a bullseye).
	 */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static int32 ScoreForRadius(float RadialCm, float BullseyeRadius, float RingRadius, float OuterRadius,
		int32 CentrePoints, int32 RingPoints, int32 OuterPoints);

	/** ScoreForRadius with the definition's rings and points. 0 with no definition. */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static int32 ScoreHit(const UChallengeDefinition* Definition, float RadialCm);

	/**
	 * The medal a completed run earns. Archery: Value is the score; at least Gold's line is gold, and so
	 * on down. Traversal: Value is seconds; under Gold's line is gold, under Silver's silver, under
	 * Bronze's bronze. A line of 0 or less is never earned.
	 */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static EChallengeMedal ComputeMedal(EChallengeType Type, float Value, float BronzeLine, float SilverLine, float GoldLine);

	/** ComputeMedal with the definition's lines: the score for archery, Seconds for traversal. */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static EChallengeMedal MedalForRun(const UChallengeDefinition* Definition, int32 Score, float Seconds);

	/**
	 * Whether a running challenge has failed: TimeUp once ElapsedSeconds reaches TimeLimitSeconds,
	 * LeftArea once the player is further than AreaRadius from the start on the ground plane. Time is
	 * checked first. False (and OutReason untouched) while it is still on.
	 */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static bool CheckFailure(float ElapsedSeconds, float TimeLimitSeconds, const FVector& PlayerLocation,
		const FVector& StartLocation, float AreaRadius, EChallengeEndReason& OutReason);

	/** Whether a completed run of Score or Seconds beats Old: a higher score for archery, a faster time for traversal. Any completion beats none. */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static bool IsNewBest(EChallengeType Type, int32 Score, float Seconds, const FChallengeRecord& Old);

	/** "1:05.3". */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static FText FormatSeconds(float Seconds);

	/** "Gold", "Silver", "Bronze", or "No medal". */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static FText MedalText(EChallengeMedal Medal);

	/** "Complete", "Time up", "Left the area", "Abandoned". */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static FText EndReasonText(EChallengeEndReason Reason);

	/** A point's distance from a target's centre in the plane of its face (FaceNormal is the way it faces). */
	UFUNCTION(BlueprintPure, Category = "Challenge|Rules")
	static float RadialDistanceOnFace(const FVector& Point, const FVector& FaceCentre, const FVector& FaceNormal);
};
