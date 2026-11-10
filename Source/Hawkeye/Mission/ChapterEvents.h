#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowDefinition.h"
#include "UObject/Object.h"
#include "ChapterEvents.generated.h"

/**
 * One thing a chapter does the first time something happens in it (claude-docs/gameplay-semantics.md, "Chapter
 * events"): speak lines, hand over arrows. Data on UMissionDefinition::Events; UChapterEventSubsystem fires it.
 *
 * Event names are what the game reports, lowercase: "parry" (the player's parry landed), "arrow_loosed:<arrow asset>"
 * ("arrow_loosed:DA_Arrow_Putty"), "objective_completed:<id>", "objective_started:<id>" (it became the current one).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeChapterEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chapter events")
	FName Event;

	/** DT_Dialogue rows spoken in order through the dialogue queue (played straight away, fight or not). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chapter events")
	TArray<FName> Lines;

	/** Arrows put in the player's quiver, up to each cap: an unlock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chapter events")
	TArray<FHawkeyeArrowGrant> Arrows;
};

/** The pure part of chapter events: which entries an event fires. */
UCLASS()
class HAWKEYE_API UHawkeyeChapterEventRules : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Indices into Events of every entry for Event, in order, or none once Event is in Fired: an event fires its
	 * entries the first time it happens in a run of the chapter and never again in that run.
	 */
	static TArray<int32> Match(const TArray<FHawkeyeChapterEvent>& Events, FName Event, const TArray<FName>& Fired);

	/** "objective_completed:<Id>". */
	static FName ObjectiveCompleted(FName ObjectiveId);

	/** "objective_started:<Id>". */
	static FName ObjectiveStarted(FName ObjectiveId);

	/** "arrow_loosed:<asset name>". */
	static FName ArrowLoosed(const UObject* Arrow);

	/** "parry". */
	static const FName Parry;
};
