#pragma once

#include "CoreMinimal.h"
#include "Mission/MissionTracker.h"
#include "Subsystems/WorldSubsystem.h"
#include "MissionSubsystem.generated.h"

class UMissionDefinition;
class UMissionObjective;
class UFlashbackDefinition;

/**
 * Per-world owner of the active mission. All of the rules live in UMissionTracker; this is the
 * Blueprint-facing wrapper that gives it a lifetime tied to the world.
 * Get it from Blueprints with "Get World Subsystem" -> MissionSubsystem.
 */
UCLASS()
class HAWKEYE_API UMissionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Convenience accessor; returns nullptr outside of a valid game world. */
	UFUNCTION(BlueprintPure, Category = "Mission", meta = (WorldContext = "WorldContextObject"))
	static UMissionSubsystem* Get(const UObject* WorldContextObject);

	/** The object that holds the mission rules. Tests drive this directly. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	UMissionTracker* GetTracker() const { return Tracker; }

	/** Instances the definition's objectives and begins tracking them. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	bool StartMission(UMissionDefinition* MissionDefinition);

	/** Completes the objective carrying ObjectiveId. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	bool CompleteObjective(FName ObjectiveId);

	/** Clears the active mission without broadcasting completion. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	void AbortMission();

	UFUNCTION(BlueprintPure, Category = "Mission")
	UMissionDefinition* GetCurrentMission() const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	TArray<UMissionObjective*> GetActiveObjectives() const;

	/** First incomplete non-optional objective; this is the one the HUD shows. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	UMissionObjective* GetCurrentObjective() const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsMissionComplete() const;

	/** Records Location as ObjectiveId's marker point. AObjectiveTriggerVolume calls this at BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void RegisterObjectiveLocation(FName ObjectiveId, FVector Location);

	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void UnregisterObjectiveLocation(FName ObjectiveId);

	/**
	 * Where the HUD marker points for the current objective: its WorldLocation when set, else the
	 * actor named by its MarkerActorLabel, else the point a volume registered for its id.
	 */
	UFUNCTION(BlueprintPure, Category = "Mission|Marker")
	bool GetCurrentObjectiveLocation(FVector& OutLocation) const;

	/** Replaces Source's secondary markers (UMissionTracker::SetSecondaryMarkers). */
	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void SetSecondaryMarkers(FName Source, const TArray<FVector>& Locations);

	UFUNCTION(BlueprintCallable, Category = "Mission|Marker")
	void ClearSecondaryMarkers(FName Source);

	/** Every secondary marker point; the HUD marks these instead of the objective while there are any. */
	UFUNCTION(BlueprintPure, Category = "Mission|Marker")
	TArray<FVector> GetSecondaryMarkers() const;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionStartedSignature OnMissionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnObjectiveUpdatedSignature OnObjectiveUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionCompleteSignature OnMissionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnFlashbackRequestedSignature OnFlashbackRequested;

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

protected:
	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	UFUNCTION()
	void HandleMissionComplete(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleFlashbackRequested(UFlashbackDefinition* Flashback);

	/** The placed actor whose label, name or tag is Label; cached, since the district has thousands. */
	AActor* FindMarkerActor(FName Label) const;

	UPROPERTY(Transient)
	TObjectPtr<UMissionTracker> Tracker = nullptr;

	/** Label -> actor, filled on first lookup. A stale entry is looked up again. */
	mutable TMap<FName, TWeakObjectPtr<AActor>> MarkerActorCache;

	/** Labels already searched for and not found, so a typo warns once instead of every frame. */
	mutable TSet<FName> MissingMarkerLabels;
};
