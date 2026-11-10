#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Challenge/ChallengeTypes.h"
#include "ChallengeTracker.generated.h"

class UChallengeDefinition;

/** Fired once when a run ends, however it ends, after the record is updated. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnChallengeRunEnded, const FChallengeResult&);

/**
 * All of the side challenges' rules, with no dependency on a world: one run at a time (its clock,
 * score, targets down, next checkpoint, failure) and the best record per challenge. UChallengeSubsystem
 * owns one and feeds it the player's position and the targets' hits; tests create one with NewObject.
 *
 * Rules are in claude-docs/gameplay-semantics.md, "Side challenges".
 */
UCLASS(BlueprintType)
class HAWKEYE_API UChallengeTracker : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Starts a run of Definition with the clock at 0. Refused (false, logged) while another run is on,
	 * or for a definition with no targets (archery) or no checkpoints (traversal).
	 */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	bool StartRun(UChallengeDefinition* Definition);

	/**
	 * One step of a running challenge: the clock moves DeltaSeconds, a traversal run takes the next
	 * checkpoint when PlayerLocation (the capsule centre) is within its radius (the last one completes
	 * the run), then time up and leaving the area are checked, in that order. True when the run ended
	 * during this step.
	 */
	bool Advance(float DeltaSeconds, const FVector& PlayerLocation);

	/**
	 * An arrow hit target TargetIndex RadialCm from its centre. Scores it once: a second hit on a target
	 * already down, a bad index, or no archery run scores nothing. Downing the last target completes the
	 * run. Returns the points scored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	int32 RegisterHit(int32 TargetIndex, float RadialCm);

	/** Ends the running challenge as Aborted: no medal, no record. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	void AbortRun();

	UFUNCTION(BlueprintPure, Category = "Challenge")
	bool IsRunning() const { return Active != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Challenge")
	UChallengeDefinition* GetActiveDefinition() const { return Active; }

	UFUNCTION(BlueprintPure, Category = "Challenge")
	float GetElapsedSeconds() const { return ElapsedSeconds; }

	/** Seconds left before time up, never below 0. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	float GetRemainingSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Challenge")
	int32 GetScore() const { return Score; }

	/** Targets down or checkpoints reached so far. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	int32 GetProgress() const { return Progress; }

	/** Targets or checkpoints in the running challenge. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	int32 GetTotal() const;

	/** The checkpoint a traversal run is heading for, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	int32 GetNextCheckpointIndex() const;

	UFUNCTION(BlueprintPure, Category = "Challenge")
	bool IsTargetDown(int32 TargetIndex) const { return TargetsDown.IsValidIndex(TargetIndex) && TargetsDown[TargetIndex]; }

	/** Points each target scored, 0 for those still up. */
	const TArray<int32>& GetTargetPoints() const { return TargetPoints; }

	/** How the last run that ended went. Valid once HasLastResult. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	const FChallengeResult& GetLastResult() const { return LastResult; }

	UFUNCTION(BlueprintPure, Category = "Challenge")
	bool HasLastResult() const { return bHasLastResult; }

	/** The record for ChallengeId: an empty one (no completions) when it has never been completed. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	FChallengeRecord GetRecord(FName ChallengeId) const;

	/** Every record with at least one completion, by id. */
	const TMap<FName, FChallengeRecord>& GetRecords() const { return Records; }

	/** The records as parallel arrays for the save (SPUD stores plain arrays), in id order. */
	void ExportRecords(TArray<FName>& OutIds, TArray<int32>& OutScores, TArray<float>& OutSeconds, TArray<int32>& OutMedals,
		TArray<int32>& OutCompletions) const;

	/** Replaces every record with the saved arrays. Rows past the shortest array are dropped with a warning. */
	void ImportRecords(const TArray<FName>& Ids, const TArray<int32>& Scores, const TArray<float>& Seconds,
		const TArray<int32>& Medals, const TArray<int32>& Completions);

	/** Drops the run (without ending it) and every record. */
	void Reset();

	FOnChallengeRunEnded OnRunEnded;

private:
	/** Medal, record, result and the broadcast; clears the run. */
	void EndRun(EChallengeEndReason Reason);

	UPROPERTY(Transient)
	TObjectPtr<UChallengeDefinition> Active = nullptr;

	float ElapsedSeconds = 0.f;
	int32 Score = 0;
	int32 Progress = 0;
	TArray<bool> TargetsDown;
	TArray<int32> TargetPoints;

	UPROPERTY(Transient)
	TMap<FName, FChallengeRecord> Records;

	UPROPERTY(Transient)
	FChallengeResult LastResult;

	bool bHasLastResult = false;
};
