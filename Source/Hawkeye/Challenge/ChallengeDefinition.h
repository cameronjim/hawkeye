#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Challenge/ChallengeTypes.h"
#include "ChallengeDefinition.generated.h"

/**
 * One repeatable side challenge (claude-docs/gameplay-semantics.md, "Side challenges"): an archery
 * range of targets or a timed traversal route of checkpoints, started from an AChallengeStart
 * pedestal. Tools/Editor/create_challenges.py writes one per challenge under /Game/Challenges from the
 * district's geometry; nothing here is placed by hand.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UChallengeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable id the save records bests under. Lowercase snake_case ("archery_1"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	FName Id;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	EChallengeType Type = EChallengeType::Archery;

	/** What the pedestal, the panel and the results card call it. A bracketed placeholder until written. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	FText Name;

	/** Seconds from the start before the run fails as time up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge", meta = (ClampMin = "1.0"))
	float TimeLimitSeconds = 60.f;

	/** Where the pedestal stands (its feet). The area is measured from here. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	FVector StartLocation = FVector::ZeroVector;

	/** Which way the pedestal faces, degrees: toward the targets, or along the route's first leg. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	float StartYaw = 0.f;

	/** Going further than this from StartLocation (on the ground plane) fails the run, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge", meta = (ClampMin = "100.0"))
	float AreaRadius = 10000.f;

	/** Thugs within this of StartLocation go calm and ignore the player while the run lasts, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge", meta = (ClampMin = "0.0"))
	float CalmThugRadius = 6000.f;

	// --- Archery ------------------------------------------------------------------------------------

	/** Archery: the targets, each hit once. The run completes when all are down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Archery")
	TArray<FChallengeTargetSpawn> Targets;

	/** Points for an arrow inside BullseyeRadius of a target's centre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Archery", meta = (ClampMin = "0"))
	int32 CentrePoints = 10;

	/** Points inside RingRadius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Archery", meta = (ClampMin = "0"))
	int32 RingPoints = 5;

	/** Points anywhere else on the face, out to OuterRadius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Archery", meta = (ClampMin = "0"))
	int32 OuterPoints = 2;

	/** The bullseye's radius on a 60 cm target, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Archery", meta = (ClampMin = "0.0"))
	float BullseyeRadius = 8.f;

	/** The middle ring's outer radius, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Archery", meta = (ClampMin = "0.0"))
	float RingRadius = 18.f;

	/** The face's radius, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Archery", meta = (ClampMin = "0.0"))
	float OuterRadius = 30.f;

	// --- Traversal ----------------------------------------------------------------------------------

	/** Traversal: the checkpoint rings in order. Each transform is a ring's centre; its X is the way through. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Traversal")
	TArray<FTransform> Checkpoints;

	/** How the route expects each checkpoint to be reached, one per checkpoint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Traversal")
	TArray<EChallengeLeg> CheckpointLegs;

	/** The player's capsule centre within this of a ring's centre goes through it, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Traversal", meta = (ClampMin = "10.0"))
	float CheckpointRadius = 150.f;

	// --- Medals and reward ----------------------------------------------------------------------------

	/**
	 * The medal lines. Archery: the lowest score for each (higher is better). Traversal: the time to beat
	 * for each, seconds (a run faster than GoldValue is gold).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Medals")
	float BronzeValue = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Medals")
	float SilverValue = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Medals")
	float GoldValue = 0.f;

	/** What a completed run hands out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	EChallengeReward Reward = EChallengeReward::None;

	/** Targets for archery, checkpoints for traversal. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	int32 GetStepCount() const;

	/** Name, or the id in brackets when a designer left it empty. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	FText GetDisplayName() const;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
