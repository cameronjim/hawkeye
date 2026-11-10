#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MissionTracker.generated.h"

class UMissionDefinition;
class UMissionObjective;
class UFlashbackDefinition;

/** Fired once by StartMission, after the objectives are instanced and before anything else. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionStartedSignature, UMissionDefinition*, Mission);

/** Fired whenever an objective changes state. Index is the objective's slot in the active list. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnObjectiveUpdatedSignature, UMissionObjective*, Objective, int32, ObjectiveIndex);

/** Fired once every required objective of the active mission is complete. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionCompleteSignature, UMissionDefinition*, Mission);

/** Fired immediately after OnMissionComplete when the definition supplies a flashback to play. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlashbackRequestedSignature, UFlashbackDefinition*, Flashback);

/**
 * All of the mission rules, with no dependency on a world. UMissionSubsystem owns one of these
 * and forwards to it; automation tests create one with NewObject.
 *
 * Rules are in claude-docs/gameplay-semantics.md.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UMissionTracker : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionStartedSignature OnMissionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnObjectiveUpdatedSignature OnObjectiveUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionCompleteSignature OnMissionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnFlashbackRequestedSignature OnFlashbackRequested;

	/**
	 * Instances the definition's objectives and begins tracking them. An already-active mission is
	 * ended silently (no OnMissionComplete) with a warning. Refuses, with an error, a definition
	 * with no non-optional objectives. Returns true when the mission started.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	bool StartMission(UMissionDefinition* MissionDefinition);

	/**
	 * Completes the objective with this id. Unknown ids log a warning; an already-completed
	 * objective is a silent no-op. Returns true only when something changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	bool CompleteObjective(FName ObjectiveId);

	/** Clears the active mission without broadcasting completion. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	void AbortMission();

	/** First incomplete non-optional objective in array order, or nullptr when there is none. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	UMissionObjective* GetCurrentObjective() const;

	/** The active objective carrying ObjectiveId, completed or not. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	UMissionObjective* FindObjective(FName ObjectiveId) const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	int32 FindObjectiveIndex(FName ObjectiveId) const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	UMissionDefinition* GetCurrentMission() const { return CurrentMission; }

	UFUNCTION(BlueprintPure, Category = "Mission")
	TArray<UMissionObjective*> GetActiveObjectives() const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	UMissionObjective* GetObjectiveAt(int32 ObjectiveIndex) const;

	/** True once every non-optional objective is complete. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	bool AreRequiredObjectivesComplete() const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsMissionComplete() const { return bMissionComplete; }

	/**
	 * Records Location as the marker point for ObjectiveId. Kept across StartMission and
	 * AbortMission, because placed volumes register at BeginPlay, before or after the mission
	 * starts. A second call for the same id replaces the first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void RegisterObjectiveLocation(FName ObjectiveId, FVector Location);

	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void UnregisterObjectiveLocation(FName ObjectiveId);

	/** The point registered for ObjectiveId, if any. */
	UFUNCTION(BlueprintPure, Category = "Mission|Marker")
	bool GetRegisteredObjectiveLocation(FName ObjectiveId, FVector& OutLocation) const;

	/**
	 * The current objective's marker point: its own WorldLocation when bHasWorldLocation is set,
	 * otherwise the point registered for its id. False when there is no current objective or
	 * neither exists. (A MarkerActorLabel needs a world; UMissionSubsystem resolves that.)
	 */
	UFUNCTION(BlueprintPure, Category = "Mission|Marker")
	bool GetCurrentObjectiveLocation(FVector& OutLocation) const;

	/**
	 * Secondary markers: points the HUD marks alongside (in place of) the objective, grouped by who set
	 * them ("challenge"). Setting a source replaces its points; an empty list clears it. Kept across
	 * StartMission and AbortMission, like registered points. While any are set the HUD shows them instead
	 * of the objective's marker (claude-docs/gameplay-semantics.md, "Objective markers").
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void SetSecondaryMarkers(FName Source, const TArray<FVector>& Locations);

	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void ClearSecondaryMarkers(FName Source);

	/** Every source's points, in the order the sources were first set. */
	UFUNCTION(BlueprintPure, Category = "Mission|Marker")
	TArray<FVector> GetSecondaryMarkers() const;

	UFUNCTION(BlueprintPure, Category = "Mission|Marker")
	bool HasSecondaryMarkers() const { return SecondaryMarkers.Num() > 0; }

	/** Ids of every completed objective, optional ones included, in array order. The save writes these. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	TArray<FName> GetCompletedObjectiveIds() const;

	/**
	 * Loading a save: marks each listed objective complete without firing OnObjectiveUpdated or
	 * OnMissionComplete (the player already saw them happen). Ignores bEnforceOrder. Unknown ids
	 * log a warning and are skipped. Returns how many objectives changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	int32 RestoreCompletedObjectives(const TArray<FName>& ObjectiveIds);

	/** Drops every binding and all state; the subsystem calls this on Deinitialize. */
	void Reset();

private:
	/** Broadcasts mission completion and, when the mission has one, the flashback request. */
	void BroadcastCompletionIfFinished();

	UPROPERTY(Transient)
	TObjectPtr<UMissionDefinition> CurrentMission = nullptr;

	/** Runtime copies of the definition's objectives (the source asset is never mutated). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMissionObjective>> ActiveObjectives;

	bool bMissionComplete = false;

	/** Secondary marker points by source, in the order the sources were set. Plain vectors: nothing for the GC. */
	TArray<TPair<FName, TArray<FVector>>> SecondaryMarkers;

	/** Marker points by objective id, registered by placed volumes. */
	UPROPERTY(Transient)
	TMap<FName, FVector> RegisteredLocations;
};
