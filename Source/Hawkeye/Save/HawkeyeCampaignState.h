#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ISpudObject.h"
#include "HawkeyeCampaignState.generated.h"

/** What Continue does with the save it finds. Anything but Load starts a new game instead. */
UENUM(BlueprintType)
enum class EHawkeyeLoadDecision : uint8
{
	/** The save is current and its chapter exists: load it. */
	Load,
	/** There is no save in the slot. */
	NoSave,
	/** Written by another version with no migration to this one. */
	VersionMismatch,
	/** Its chapter's mission asset is gone (renamed, deleted). */
	StaleMission
};

/**
 * The campaign-wide half of the save: which chapter, which objectives are done, which safehouses
 * are found, who the player was playing. Everything that belongs to an actor (health, quiver,
 * position, a dead thug) is saved by SPUD on the actor itself; this is the part with no actor.
 *
 * UHawkeyeSaveSubsystem owns one and registers it with SPUD as the global object
 * "HawkeyeCampaign", so SPUD writes its SaveGame properties into the slot and restores them
 * before the map reloads. Rules: claude-docs/gameplay-semantics.md, "Save data".
 */
UCLASS(BlueprintType)
class HAWKEYE_API UHawkeyeCampaignState : public UObject, public ISpudObject
{
	GENERATED_BODY()

public:
	/** The version this build writes. Bump it whenever a saved field changes meaning. */
	static constexpr int32 CurrentVersion = 1;

	/** Save format version, written with every save. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	int32 Version = CurrentVersion;

	/** The chapter's mission asset as a soft object path (/Game/Missions/DA_CH01_Rooftops.DA_CH01_Rooftops). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	FString MissionPath;

	/** Ids of every completed objective of that mission, optional ones included. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	TArray<FName> CompletedObjectives;

	/** Safehouses entered at least once; fast travel lists these. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	TArray<FName> DiscoveredSafehouses;

	/** The safehouse whose menu was last opened or that was last travelled to; None before any. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	FName LastSafehouse;

	/** Unfound safehouses and challenge pedestals the "nearby" toast has already announced, by id. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	TArray<FName> NoticedPlaces;

	/** Seconds of play in this campaign, across sessions. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	float PlayTimeSeconds = 0.f;

	/** CharacterName of the Hawkeye the player controlled when the save was written. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save")
	FString ControlledCharacter;

	/** Phone messages that have arrived, oldest first (DT_Messages row names). UPhoneSubsystem mirrors these. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Phone")
	TArray<FName> ReceivedMessages;

	/** The arrived messages that have been read. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Phone")
	TArray<FName> ReadMessages;

	/** Messages triggered but still on their delay when the save was written, soonest first. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Phone")
	TArray<FName> PendingMessages;

	/** Seconds each of PendingMessages had left, in the same order. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Phone")
	TArray<float> PendingMessageSeconds;

	/** Chapters (mission asset names) whose opening title card has been shown. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Narrative")
	TArray<FName> SeenChapterTitles;

	/** Dialogue sequences that have played through. UDialogueSubsystem mirrors these. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Narrative")
	TArray<FName> PlayedDialogueSequences;

	/** Side challenges completed at least once, in id order. UChallengeSubsystem mirrors these five arrays. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Challenges")
	TArray<FName> ChallengeIds;

	/** Best archery score per challenge, in ChallengeIds' order (0 for traversal). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Challenges")
	TArray<int32> ChallengeBestScores;

	/** Fastest traversal time per challenge, seconds (0 for archery). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Challenges")
	TArray<float> ChallengeBestSeconds;

	/** Best medal per challenge, as EChallengeMedal's value. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Challenges")
	TArray<int32> ChallengeBestMedals;

	/** Completed runs per challenge. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Challenges")
	TArray<int32> ChallengeCompletions;

	/** Street crime types stopped at least once ("Mugging"). UCrimeSubsystem mirrors these two arrays. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Crimes")
	TArray<FName> CrimeTypes;

	/** Crimes stopped per type, in CrimeTypes' order. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Crimes")
	TArray<int32> CrimeCompletions;

	/** First-time hints that have shown or been learned (UHawkeyeHintSubsystem), by rule id. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Hints")
	TArray<FName> HintIds;

	/** Times each of HintIds has shown, in the same order; a learned hint holds its rule's MaxShows. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Hints")
	TArray<int32> HintShowCounts;

	/** The running chase's route asset path; empty when none ran at the save. UChaseSubsystem mirrors these seven. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Chase")
	FString ChaseRoutePath;

	/** The thief's distance along the route, cm. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Chase")
	float ChaseThiefProgress = 0.f;

	/** Kate's distance along the route, cm. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Chase")
	float ChaseKateProgress = 0.f;

	/** Waypoints whose objective and line have been handled. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Chase")
	int32 ChaseReachedWaypoints = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Chase")
	bool bChaseBeatDone = false;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Chase")
	bool bChasePulledAhead = false;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Chase")
	bool bChaseVaulted = false;

	/** What the chapter's boss fight has spoken ("line:<row>", "bark:<phase>:<event>"), so a load repeats none of it. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Boss")
	TArray<FString> BossHeard;

	/**
	 * Story facts a chapter leaves for later ones ("saw_courier"), raised by UMissionObjective::CampaignFlag. Unlike the
	 * objectives they outlive the chapter; only a new game clears them.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Narrative")
	TArray<FName> CampaignFlags;

	/** The running chapter's events that have fired (UChapterEventSubsystem), so a load repeats no line or grant. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Narrative")
	TArray<FName> FiredChapterEvents;

	/** The running chapter's stealth objectives that were seen (UMissionObjective::bSpotted). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Narrative")
	TArray<FName> SpottedObjectives;

	/**
	 * A save made inside a chapter interior (gameplay-semantics.md, "Chapter interiors"): the district map its exits
	 * lead back to and the doorstep's return point. Empty for a save in the district.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Interior")
	FString InteriorReturnMap;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Save|Interior")
	FName InteriorReturnPoint;

	/** Back to a fresh campaign. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void ResetCampaign();

	/** Raises Flag. False when it is None or already up. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool RaiseFlag(FName Flag);

	UFUNCTION(BlueprintPure, Category = "Save")
	bool HasFlag(FName Flag) const { return !Flag.IsNone() && CampaignFlags.Contains(Flag); }

	/** A completed objective's CampaignFlag goes up (AHawkeyeGameMode calls this before the objective's autosave). */
	bool ApplyObjectiveFlag(const class UMissionObjective* Objective);

	/**
	 * Whether a save of SavedVersion can be brought up to CurrentVersion. There are no migrations
	 * yet, so only the current version passes; an older save starts a new game.
	 */
	static bool CanMigrateFrom(int32 SavedVersion);

	/** True when MissionPath names a mission asset that exists in this build. */
	static bool DoesMissionExist(const FString& InMissionPath);

	/**
	 * The rule Continue follows: no save, a version with no migration, or a chapter that no longer
	 * exists all start a new game; only a current save of an existing chapter loads.
	 */
	static EHawkeyeLoadDecision EvaluateLoad(bool bSaveExists, int32 SavedVersion, const FString& InMissionPath);

	//~ Begin ISpudObject interface
	virtual FString OverrideName_Implementation() const override;
	//~ End ISpudObject interface
};
