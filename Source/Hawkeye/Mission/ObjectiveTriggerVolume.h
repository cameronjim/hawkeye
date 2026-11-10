#pragma once

#include "CoreMinimal.h"
#include "Engine/TriggerBox.h"
#include "ObjectiveTriggerVolume.generated.h"

/**
 * Drop in a level, set ObjectiveId, and the matching mission objective completes
 * when the player pawn walks in. Its centre is where the HUD's objective marker points.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AObjectiveTriggerVolume : public ATriggerBox
{
	GENERATED_BODY()

public:
	AObjectiveTriggerVolume();

	/** Tag of the UMissionObjective to complete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	FName ObjectiveId;

	/** When true the volume disables itself after the first successful trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	bool bOnlyOnce = true;

	/** Only the locally controlled player pawn triggers the volume when true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	bool bPlayerOnly = true;

	/** Blueprint hook for VFX/SFX/dialogue when the volume fires. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Objective")
	void OnObjectiveTriggered(AActor* TriggeringActor);

protected:
	/** Also registers the volume's centre as ObjectiveId's marker point, so data assets need no coordinates. */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleActorBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

	bool bHasTriggered = false;
};
