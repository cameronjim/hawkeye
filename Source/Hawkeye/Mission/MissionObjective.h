#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MissionObjective.generated.h"

class UMissionObjective;

/** Fired when this objective is completed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObjectiveCompletedSignature, UMissionObjective*, Objective);

/**
 * A single step of a mission ("Reach the laundry", "Take down the guard").
 * Authored inline on a UMissionDefinition and duplicated per-playthrough by the UMissionSubsystem,
 * so runtime state never dirties the source data asset.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class HAWKEYE_API UMissionObjective : public UObject
{
	GENERATED_BODY()

public:
	/** Stable id used to complete this objective by name. Lowercase snake_case: find_weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName ObjectiveId;

	/** Short player-facing line shown in the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FText Title;

	/** Longer description shown in the mission log. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (MultiLine = "true"))
	FText Description;

	/** A second, smaller line under the title in the HUD while this is current (where to look). Empty for none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FText Hint;

	/** The title, description and hint are a draft (docs/chapters/ACT1-script.md) waiting for the owner's edit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bDraft = false;

	/** Optional objectives do not block mission completion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bOptional = false;

	/**
	 * Never on the HUD: no "Objective complete" toast when it completes, no marker and no compass bearing (it is never
	 * the current objective, which only a required one can be). For an optional thing the player finds or not, like an
	 * examine off the path. Ignored on a required objective, which the HUD has to show.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	bool bUnmarked = false;

	/**
	 * Raised in the campaign (UHawkeyeCampaignState::CampaignFlags) when this completes, for later chapters to read;
	 * kept across chapters until a new game. None raises nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName CampaignFlag;

	/** bUnmarked on an optional objective: the HUD leaves it out entirely. */
	bool IsHiddenFromHud() const { return bUnmarked && bOptional; }

	/**
	 * Use WorldLocation as the HUD marker's point. Leave off for objectives completed by an
	 * AObjectiveTriggerVolume: the volume registers its own centre at BeginPlay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective|Marker")
	bool bHasWorldLocation = false;

	/** Where the HUD marker and compass point for this objective, in world space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective|Marker", meta = (EditCondition = "bHasWorldLocation"))
	FVector WorldLocation = FVector::ZeroVector;

	/**
	 * A placed actor whose location the marker follows, found by its label (editor builds), its
	 * name or one of its tags. Used when bHasWorldLocation is off; wins over a registered volume.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective|Marker")
	FName MarkerActorLabel;

	/** Runtime completion state. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	bool bCompleted = false;

	/** Broadcast once, the first time this objective completes. */
	UPROPERTY(BlueprintAssignable, Category = "Objective")
	FOnObjectiveCompletedSignature OnObjectiveCompleted;

	/** Marks the objective complete and broadcasts. Returns false if it was already complete. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	bool Complete();

	/** Clears completion state (used when a mission is restarted). */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void ResetObjective();

	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsCompleted() const { return bCompleted; }

	//~ Begin UObject interface
	virtual UWorld* GetWorld() const override;
	//~ End UObject interface
};
