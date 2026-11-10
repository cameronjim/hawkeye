#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChapterSiteSubsystem.generated.h"

class AActor;
class AChapterEndInteractable;
class AObjectiveTriggerVolume;
class APawn;
class APickupActor;
class AThugCharacter;
class AThugGroupObjective;
class UChapterSiteDefinition;
class UMissionDefinition;
class UMissionObjective;
enum class EThugWeapon : uint8;

/**
 * Spawns the running chapter's UChapterSiteDefinition (claude-docs/gameplay-semantics.md, "Chapter sites") and keeps it
 * in step with the chapter: dressed when the chapter starts, cleared when another starts, the crew woken by its alert
 * objective, and after a load the crew, the pickups and the examines made to match what the save says is done. Knows no
 * chapter by name; everything comes from the data asset.
 */
UCLASS()
class HAWKEYE_API UChapterSiteSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The tag every crew thug of a site carries (the crew objective counts it). */
	static const FName CrewTag;
	/** The tag on everything a site spawns. */
	static const FName SiteTag;

	UFUNCTION(BlueprintPure, Category = "Chapter site", meta = (WorldContext = "WorldContextObject"))
	static UChapterSiteSubsystem* Get(const UObject* WorldContextObject);

	/** Clears the old site and spawns Definition's (the mission start does this). False with no definition. */
	bool SpawnSite(UChapterSiteDefinition* Definition);

	/** Destroys everything the site spawned. */
	void ClearSite();

	/** The chapter's opening (never a load): Kate to the site's PlayerStart, if it has one. True when moved. */
	bool BeginChapter(const UMissionDefinition* Mission);

	/** Wakes the crew: thinking on, alerted on the player. */
	void AlertCrew();

	/** Makes the site match the running chapter's completed objectives and the player's keycards (a load). */
	void Reconcile();

	UChapterSiteDefinition* GetDefinition() const { return Definition; }
	TArray<AThugCharacter*> GetCrew() const;
	TArray<APickupActor*> GetPickups() const;
	TArray<AChapterEndInteractable*> GetExamines() const;
	TArray<AObjectiveTriggerVolume*> GetVolumes() const;
	AThugGroupObjective* GetCrewObjective() const { return CrewGroup.Get(); }
	int32 GetPropCount() const { return Props.Num(); }
	bool IsCrewAlerted() const { return bCrewAlerted; }

	/** When set (tests), stands in for the world's player. */
	TWeakObjectPtr<APawn> PlayerOverride;

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem interface

	/** Binds to the mission subsystem (OnWorldBeginPlay does it; a test world calls it). */
	void BindMissions();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	APawn* FindPlayer() const;
	bool IsObjectiveDone(FName ObjectiveId) const;
	AThugCharacter* SpawnThug(UClass* Class, const FVector& Feet, float Yaw, EThugWeapon Weapon);
	void ClearCrew();

	UPROPERTY(Transient)
	TObjectPtr<UChapterSiteDefinition> Definition = nullptr;

	TArray<TWeakObjectPtr<AActor>> Props;
	TArray<TWeakObjectPtr<AThugCharacter>> Crew;
	TArray<TWeakObjectPtr<APickupActor>> Pickups;
	TArray<TWeakObjectPtr<AChapterEndInteractable>> Examines;
	TArray<TWeakObjectPtr<AObjectiveTriggerVolume>> Volumes;
	TWeakObjectPtr<AThugGroupObjective> CrewGroup;
	bool bCrewAlerted = false;
};
