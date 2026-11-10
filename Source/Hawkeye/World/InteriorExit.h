#pragma once

#include "CoreMinimal.h"
#include "World/InteriorPortal.h"
#include "InteriorExit.generated.h"

class UWorld;

/**
 * A way out of an interior map back to the district, placed by Tools/Editor/generate_interior.py as
 * Int_<room>_Exit_<n>: Interact fades to black and leaves through UHawkeyeSaveSubsystem::ReturnFromInterior
 * (the district's entry save loads and the player is put at the return point). The front door leaves
 * ReturnPointOverride empty and comes out where the player went in; a roof door names the point on the
 * roof. A map opened directly (a test, -game straight into it) has no entry to go back to: then
 * FallbackDistrict opens fresh and the player is put at FallbackReturnPoint.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AInteriorExit : public AInteriorPortal
{
	GENERATED_BODY()

public:
	AInteriorExit();

	/** Where in the district this door comes out. None: where the player came in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	FName ReturnPointOverride;

	/** The district to open when the interior was not entered through a door. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	TSoftObjectPtr<UWorld> FallbackDistrict;

	/** The return point used with FallbackDistrict when ReturnPointOverride is None (the entrance's doorstep). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	FName FallbackReturnPoint;

	/** The prompt's verb ("[Leave]", "[Roof]"); a placeholder until the story names the doors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	FText ExitName;

	virtual FText GetInteractPrompt_Implementation() const override;

protected:
	virtual bool Travel() override;
};
