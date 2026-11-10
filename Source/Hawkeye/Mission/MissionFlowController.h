#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MissionFlowController.generated.h"

/** One beat of the end-of-mission sequence. AHawkeyePlayerController performs each in turn. */
UENUM(BlueprintType)
enum class EMissionFlowStep : uint8
{
	/** Nothing is running; the mission is still being played. */
	Idle,
	/** Black card with the mission name and its EndCardLine, held for EndCardSeconds. */
	EndCard,
	/** The mission's flashback slideshow. */
	Flashback,
	/** End card again, held until a key is pressed. Only when there is no next level. */
	FinalCard,
	/** Travel to the completed mission's NextLevel. */
	OpenNextLevel,
	/** Travel to the main menu (or the sandbox), because the campaign is over. */
	OpenMenuLevel,
	/** The sequence has handed off; nothing left to do. */
	Done,
	/** The camera pushed in on the thing that ended the chapter (AChapterEndInteractable), then back. */
	CloseUp,
	/** The flashback's playable scene: the district is saved and the scene map opened. */
	PlayableScene,
	/** No next level and the chapter says so: back to the district with the chapter-complete toast. */
	ReturnToRoaming
};

/** Which beats an end-of-mission sequence has, decided once when it begins. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FMissionFlowRoute
{
	GENERATED_BODY()

	/** A close-up was running when the mission completed: it plays out before the end card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission|Flow")
	bool bHasCloseUp = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission|Flow")
	bool bHasFlashback = false;

	/** The flashback ends in a playable scene. Ignored without bHasFlashback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission|Flow")
	bool bHasPlayableScene = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission|Flow")
	bool bHasNextLevel = false;

	/** UMissionDefinition::bReturnToRoamingAtEnd. Ignored when there is a next level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission|Flow")
	bool bReturnToRoaming = false;
};

/**
 * The order of the end-of-mission beats, with no UMG, no world and no travel in it.
 *
 * AHawkeyePlayerController owns one, calls Begin when the mission completes, performs whatever
 * GetStep returns, and calls Advance when that beat reports it has finished. Keeping the
 * ordering here is what makes it testable: a test drives Begin/Advance and asserts the
 * sequence, without a widget or a level to load.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UMissionFlowController : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Starts the sequence at the end card. bHasFlashback and bHasNextLevel are read once, here,
	 * so the route is decided before anything on screen can change it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission|Flow")
	EMissionFlowStep Begin(bool bInHasFlashback, bool bInHasNextLevel);

	/**
	 * Starts the sequence with every beat the route has: the close-up when there is one, else
	 * the end card. The route is fixed from here on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission|Flow")
	EMissionFlowStep BeginRoute(const FMissionFlowRoute& InRoute);

	/**
	 * Picks the sequence up at Step on a route begun in another world: back from a playable scene
	 * the district's controller resumes at PlayableScene and advances to what follows it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission|Flow")
	void ResumeAt(const FMissionFlowRoute& InRoute, EMissionFlowStep InStep);

	/** Moves to the beat after the current one and returns it. Idle and Done never move. */
	UFUNCTION(BlueprintCallable, Category = "Mission|Flow")
	EMissionFlowStep Advance();

	/** Puts the sequence back to Idle, for a fresh mission in the same world. */
	UFUNCTION(BlueprintCallable, Category = "Mission|Flow")
	void Reset();

	UFUNCTION(BlueprintPure, Category = "Mission|Flow")
	EMissionFlowStep GetStep() const { return Step; }

	/** True between Begin and the sequence reaching Done. */
	UFUNCTION(BlueprintPure, Category = "Mission|Flow")
	bool IsRunning() const { return Step != EMissionFlowStep::Idle && Step != EMissionFlowStep::Done; }

	UFUNCTION(BlueprintPure, Category = "Mission|Flow")
	bool HasFlashback() const { return Route.bHasFlashback; }

	UFUNCTION(BlueprintPure, Category = "Mission|Flow")
	bool HasNextLevel() const { return Route.bHasNextLevel; }

	UFUNCTION(BlueprintPure, Category = "Mission|Flow")
	FMissionFlowRoute GetRoute() const { return Route; }

private:
	/** The beat that follows Current, given the route. */
	EMissionFlowStep NextAfter(EMissionFlowStep Current) const;

	/** What comes once the story beats (card, flashback, scene) are over: travel, roaming or the final card. */
	EMissionFlowStep AfterStory() const;

	UPROPERTY(Transient)
	EMissionFlowStep Step = EMissionFlowStep::Idle;

	UPROPERTY(Transient)
	FMissionFlowRoute Route;
};
