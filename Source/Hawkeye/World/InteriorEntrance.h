#pragma once

#include "CoreMinimal.h"
#include "World/InteriorPortal.h"
#include "InteriorEntrance.generated.h"

class UWorld;

/**
 * A district doorway into an interior map (L_Int_*): Interact fades to black, saves the district, and
 * opens Interior through UHawkeyeSaveSubsystem::EnterInterior. Leaving the interior by its front door
 * puts the player back at ReturnPointLabel, the labelled point on the pavement outside this door.
 * generate_city.py places one as City_InteriorEntrance_<name> on the building it picks.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AInteriorEntrance : public AInteriorPortal
{
	GENERATED_BODY()

public:
	AInteriorEntrance();

	/** The interior this door opens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	TSoftObjectPtr<UWorld> Interior;

	/** Label (or tag) of the actor on the doorstep the player comes back out at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	FName ReturnPointLabel;

	/** What the building is called on the sign and the prompt ("[Auction house]" until the story names it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	FText DisplayName;

	virtual FText GetInteractPrompt_Implementation() const override;

protected:
	virtual void BeginPlay() override;
	virtual bool Travel() override;
};
