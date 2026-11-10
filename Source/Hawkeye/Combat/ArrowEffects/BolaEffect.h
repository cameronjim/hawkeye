#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "BolaEffect.generated.h"

/**
 * The bola arrow: a thug it hits is tripped, down for TripSeconds through the same ragdoll and
 * get-up path as Kate's heavy (AThugCharacter::KnockdownFor). A miss does nothing more; the arrow
 * itself is recoverable like a standard one.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API ABolaEffect : public AArrowEffect
{
	GENERATED_BODY()

public:
	/** How long a tripped thug stays down, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bola", meta = (ClampMin = "0.0"))
	float TripSeconds = 2.5f;

	/** How hard his legs go out from under him, cm/s: less of a throw than the heavy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bola", meta = (ClampMin = "0.0"))
	float TripLaunchSpeed = 150.f;

	/** True once a thug was tripped. */
	UFUNCTION(BlueprintPure, Category = "Bola")
	bool DidTrip() const { return bTripped; }

	virtual void Activate() override;

protected:
	bool bTripped = false;
};
