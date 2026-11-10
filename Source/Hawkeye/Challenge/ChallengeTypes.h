#pragma once

#include "CoreMinimal.h"
#include "ChallengeTypes.generated.h"

/** What a side challenge asks of the player. */
UENUM(BlueprintType)
enum class EChallengeType : uint8
{
	/** Shoot every target; scored by where each arrow lands. */
	Archery,
	/** Run the checkpoints in order; scored by time. */
	Traversal
};

/** The medal a finished run earned. Ordered, so a higher value is a better medal. */
UENUM(BlueprintType)
enum class EChallengeMedal : uint8
{
	None,
	Bronze,
	Silver,
	Gold
};

/** What a completed run hands out. */
UENUM(BlueprintType)
enum class EChallengeReward : uint8
{
	None,
	/** Every filled quiver slot back to its cap, as the safehouse does. */
	ArrowRefill
};

/** How a run ended. */
UENUM(BlueprintType)
enum class EChallengeEndReason : uint8
{
	/** Every target down, or the last checkpoint reached. */
	Completed,
	/** The time limit ran out first. */
	TimeUp,
	/** The player went further than the challenge's area radius from its start. */
	LeftArea,
	/** Given up, the player went down, or the world ended. */
	Aborted
};

/** How a traversal route expects a checkpoint to be reached. Verify and the scripted run read it. */
UENUM(BlueprintType)
enum class EChallengeLeg : uint8
{
	/** On foot from the previous checkpoint: a run, with any vault or mantle on the way. */
	Run,
	/** A grapple zip: the checkpoint sits at an anchor's landing point. */
	Grapple,
	/** A step up onto a neighbouring roof: a mantle over its parapet. */
	Mantle,
	/** Down a fire escape to the street. */
	Descent
};

/**
 * One archery target: where it stands and whether it moves. The transform's origin is the foot of the
 * target's post on the roof or landing, its yaw turns the face toward the shooting spot and its pitch
 * tilts the face (up toward a shooter above it, down toward one below).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChallengeTargetSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	FTransform Transform;

	/** Slides back and forth along its local Y (across the line of fire). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Target")
	bool bMoving = false;

	/** Length of the moving target's track, centred on Transform, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Target", meta = (ClampMin = "0.0"))
	float TrackLength = 600.f;

	/** How fast a moving target slides, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Target", meta = (ClampMin = "0.0"))
	float TrackSpeed = 150.f;
};

/** The best a player has done at one challenge. Only completed runs count. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChallengeRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Record")
	FName ChallengeId;

	/** Highest score of a completed archery run; 0 before one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Record")
	int32 BestScore = 0;

	/** Fastest completed traversal run, seconds; 0 before one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Record")
	float BestSeconds = 0.f;

	/** Best medal of any completed run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Record")
	EChallengeMedal BestMedal = EChallengeMedal::None;

	/** Completed runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge|Record")
	int32 Completions = 0;

	bool HasCompleted() const { return Completions > 0; }
};

/** What the results card shows about a run that has ended. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChallengeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	FName ChallengeId;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	EChallengeType Type = EChallengeType::Archery;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	FText Name;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	EChallengeEndReason Reason = EChallengeEndReason::Aborted;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	float Seconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	int32 Score = 0;

	/** Targets hit or checkpoints reached, of Total. */
	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	int32 Progress = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	int32 Total = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	EChallengeMedal Medal = EChallengeMedal::None;

	/** The record after this run was counted. */
	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	FChallengeRecord Record;

	/** This run beat (or set) the record. */
	UPROPERTY(BlueprintReadOnly, Category = "Challenge|Result")
	bool bNewBest = false;

	bool IsCompleted() const { return Reason == EChallengeEndReason::Completed; }
};
