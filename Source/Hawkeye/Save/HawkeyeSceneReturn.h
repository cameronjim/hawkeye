#pragma once

#include "CoreMinimal.h"
#include "HawkeyeSceneReturn.generated.h"

class UWorld;

/** An actor in the district the player could be put at on coming back from a playable scene. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeReturnCandidate
{
	GENERATED_BODY()

	/** The actor's label (editor builds) or name. */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	FName Label;

	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	TArray<FName> Tags;

	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	FTransform Transform;
};

/**
 * A flashback's playable scene or an interior (L_Int_*) in progress: where to come back to
 * (claude-docs/gameplay-semantics.md, "Flashback"). An interior is left through an AInteriorExit door
 * rather than by completing a mission, and coming back does not move the chapter on.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeSceneReturn
{
	GENERATED_BODY()

	/** Between entering the scene and its mission completing. Saves are refused meanwhile. */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	bool bInScene = false;

	/** Between leaving the scene and the district being back with the player placed. */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	bool bReturnPending = false;

	/** The district was saved on the way in, so the way back is a load. */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	bool bSavedOnEntry = false;

	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	FName ReturnPointLabel;

	/** The district map's package, opened fresh if the save cannot be loaded. */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	FString ReturnMap;

	/** An interior entered through a door, not a flashback's scene: the way back leaves the mission flow alone. */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	bool bInterior = false;

	/** Set on entering an interior: its first playable frame fades in from black. */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	bool bFadeInOnArrival = false;

	/**
	 * A chapter interior: the running chapter carries on inside (one of its sites names this interior), so the
	 * interior starts it where the campaign has it, saves are allowed inside, and the way out carries what happened
	 * in there back to the district (gameplay-semantics.md, "Chapter interiors").
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Save|Scene")
	bool bChapterContinues = false;

	/**
	 * Whether the campaign may be saved now: never on the way back from a scene or interior, never in a flashback's
	 * scene or an interior the chapter does not carry on into (the slot holds the district they return to).
	 */
	bool AllowsSave() const { return !bReturnPending && (!bInScene || (bInterior && bChapterContinues)); }

	/** Starts an interior: in it, coming back to ReturnPoint in InReturnMap. bSaved: the district was saved on the way in. */
	void BeginInterior(const FString& InReturnMap, FName InReturnPoint, bool bSaved);

	/**
	 * Leaves the scene or interior: from here the district is on its way back. A ReturnPointOverride that
	 * is not None replaces the return point (an interior's roof door comes out on the roof, not the
	 * doorstep). False, and nothing changes, when no scene or interior is running.
	 */
	bool Leave(FName ReturnPointOverride = NAME_None);

	/** Whether coming back resumes the chapter's end sequence (a flashback's scene) or not (an interior). */
	bool ShouldResumeMissionFlow() const { return !bInterior; }

	/**
	 * Which candidate the player goes back to: the one labelled ReturnPointLabel, else the first
	 * tagged with it. INDEX_NONE when the label is None or nothing matches (the saved position stands).
	 */
	static int32 SelectReturnPoint(const TArray<FHawkeyeReturnCandidate>& Candidates, FName ReturnPointLabel);

	/** Every actor in World as a candidate. */
	static TArray<FHawkeyeReturnCandidate> GatherCandidates(UWorld* World);
};
