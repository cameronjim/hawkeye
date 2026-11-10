#pragma once

#include "CoreMinimal.h"
#include "Mission/ChapterEvents.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChapterEventSubsystem.generated.h"

class UInventoryComponent;
class UMissionDefinition;
class UMissionObjective;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChapterEventFiredSignature, FName, Event);

/**
 * Fires the running chapter's UMissionDefinition::Events (claude-docs/gameplay-semantics.md, "Chapter events"): the
 * game reports what happens (Notify: a parry, an arrow loosed, objectives completed and started, which this hears from
 * the mission subsystem itself), and the first time an event the chapter lists happens in a run of it, its lines are
 * spoken and its arrows granted. Knows no chapter by name. What has fired is mirrored into the campaign, so a load
 * mid-chapter repeats nothing; a chapter started afresh (chapter select, a new game) starts with nothing fired.
 */
UCLASS()
class HAWKEYE_API UChapterEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Chapter events", meta = (WorldContext = "WorldContextObject"))
	static UChapterEventSubsystem* Get(const UObject* WorldContextObject);

	/** Reports Event in WorldContextObject's world (nothing happens without a subsystem). */
	static void NotifyEvent(const UObject* WorldContextObject, FName Event);

	/** Reports Event for the running chapter. Returns how many entries it fired. */
	UFUNCTION(BlueprintCallable, Category = "Chapter events")
	int32 Notify(FName Event);

	/** Events fired in this run of the chapter. */
	const TArray<FName>& GetFired() const { return Fired; }

	/** When set (tests), lines and arrows go here instead of the world's player and dialogue. */
	TWeakObjectPtr<UInventoryComponent> InventoryOverride;
	bool bSkipLines = false;

	/** The lines spoken so far (row names), for tests and the lap. */
	const TArray<FName>& GetSpokenLines() const { return SpokenLines; }

	UPROPERTY(BlueprintAssignable, Category = "Chapter events")
	FOnChapterEventFiredSignature OnEventFired;

	void MirrorToCampaign() const;
	void RestoreFromCampaign();

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem interface

	/** Binds to a mission subsystem's delegates (OnWorldBeginPlay does it; a test world calls it). */
	void BindMissions();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	UInventoryComponent* FindInventory() const;

	UPROPERTY(Transient)
	TArray<FName> Fired;

	UPROPERTY(Transient)
	TArray<FName> SpokenLines;

	/** The current objective last seen, for objective_started. */
	FName LastCurrentObjective;
};
