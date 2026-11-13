#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/ArrowDefinition.h"
#include "Mission/ChapterEvents.h"
#include "MissionDefinition.generated.h"

class UBossDefinition;
class UChapterSiteDefinition;
class UBowDefinition;
class UChaseRouteDefinition;
class UMissionObjective;
class UFlashbackDefinition;
class UWorld;

/**
 * Designer-authored description of one mission of the campaign.
 * Create via Content Browser > Miscellaneous > Data Asset > MissionDefinition.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UMissionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Player-facing mission name ("Night Shift"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FText MissionName;

	/** Ordering within the campaign; also used for save slots / debug menus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	int32 MissionNumber = 0;

	/** Ordered objectives. Instanced so each mission owns its own objective objects. */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Mission")
	TArray<TObjectPtr<UMissionObjective>> Objectives;

	/**
	 * When true, non-optional objectives must be completed in array order: completing one out of
	 * turn logs a warning and does nothing. Optional objectives are never ordered.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	bool bEnforceOrder = false;

	/** When false the HUD hides the objective line for this mission (a silent opening, a boss room). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	bool bShowObjectiveText = true;

	/**
	 * One line of Kate's, shown under the mission name on the end card. Optional: an empty
	 * line leaves the card as just the mission name.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission", meta = (MultiLine = "true"))
	FText EndCardLine;

	/**
	 * The bow this chapter starts with. Null means none until one is picked up: left click is
	 * Hands. Nothing carries between chapters.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSoftObjectPtr<UBowDefinition> StartingBow;

	/** Arrows in the quiver at the start, one entry per type. Standard arrows are slot 1 even at zero. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TArray<FHawkeyeArrowGrant> StartingArrows;

	/**
	 * Whether the player may swap between Kate and Clint during this chapter
	 * (AHawkeyePlayerController::SwitchCharacter). Off by default: most chapters fix who you play.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	bool bAllowSwitching = false;

	/**
	 * Whether the partner (Clint) is in this chapter. Off, the game mode benches him when it starts: no AI
	 * partner on screen, no HUD partner line, nothing to switch to (gameplay-semantics.md, "Chases", Partner).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	bool bPartnerPresent = true;

	/** The chase this chapter runs, from its opening. None for a chapter without one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSoftObjectPtr<UChaseRouteDefinition> Chase;

	/**
	 * The boss fight this chapter runs (gameplay-semantics.md, "Bosses"): set up on the arena's roof when the chapter
	 * starts, its objectives completed by the fight. None for a chapter without one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSoftObjectPtr<UBossDefinition> Boss;

	/**
	 * The chapter's set piece on the district (gameplay-semantics.md, "Chapter sites"): props, lights, objective volumes,
	 * a crew, pickups and examines, spawned by UChapterSiteSubsystem while the chapter is the running one. None for a
	 * chapter without one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSoftObjectPtr<UChapterSiteDefinition> Site;

	/**
	 * The chapter's sites inside interiors, each naming its map (UChapterSiteDefinition::Map): dressed when the chapter
	 * carries on into that interior (gameplay-semantics.md, "Chapter interiors"). Empty for most chapters.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TArray<TSoftObjectPtr<UChapterSiteDefinition>> InteriorSites;

	/**
	 * What the chapter does the first time something happens in it (gameplay-semantics.md, "Chapter events"): lines
	 * spoken and arrows granted on a parry, an arrow loosed, an objective completed or started.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TArray<FHawkeyeChapterEvent> Events;

	/**
	 * The chapter after this one in the campaign's order (UHawkeyeCampaignOrder): it opens in chapter select once this
	 * one is complete. None for the last.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSoftObjectPtr<UMissionDefinition> NextChapter;

	/**
	 * The title, subtitle, end card line and toast are a draft (docs/chapters/ACT1-script.md) waiting for the
	 * owner's edit, so they can be found and replaced.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	bool bDraftText = false;

	/** Flashback slideshow played when this mission completes. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSoftObjectPtr<UFlashbackDefinition> FlashbackToPlay;

	/** Level opened once the mission (and its flashback) is done. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TSoftObjectPtr<UWorld> NextLevel;

	// --- Chapter opening and ending (claude-docs/gameplay-semantics.md, "Chapter title") ----------

	/** The title card's big line, shown once per campaign when the chapter starts. Empty skips the card. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission|Opening")
	FText OpeningTitle;

	/** The smaller line under the title. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission|Opening", meta = (MultiLine = "true"))
	FText OpeningSubtitle;

	/**
	 * A row of DT_DialogueSequences' Sequence column played once when the chapter starts (its first
	 * gap lets the title card go first). None plays nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission|Opening")
	FName OpeningDialogueSequence;

	/**
	 * With no NextLevel: after the end card (and the flashback) the player is handed back to the
	 * district with ChapterCompleteToast instead of the final card and the menu. The quiver is kept.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission|Ending")
	bool bReturnToRoamingAtEnd = false;

	/** The toast shown on returning to roaming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission|Ending")
	FText ChapterCompleteToast;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
