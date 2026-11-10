#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowProjectile.h"
#include "GrappleArrowProjectile.generated.h"

class AGrappleAnchor;
class UGrappleComponent;

/**
 * The grapple arrow in flight (BP_Arrow_Grapple's class): a straight line from the bow hand to
 * the anchor at Speed, no gravity and no collision, unlike a standard arrow, which arcs. On arrival
 * it sticks in the anchor and tells the grapple component, which starts the zip. It stays in the
 * anchor until the grapple component recovers it; the standard walk-over recovery is off.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AGrappleArrowProjectile : public AArrowProjectile
{
	GENERATED_BODY()

public:
	AGrappleArrowProjectile();

	/** cm/s. Fast enough that the zip starts a fraction of a second after the release. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "1.0"))
	float Speed = 6000.f;

	/** Sends the arrow at Anchor's marker. Grapple is told when it arrives; either may be null. */
	void Launch(AGrappleAnchor* Anchor, UGrappleComponent* Grapple);

	/**
	 * The same at InSpeed cm/s instead of Speed; 0 or less and it arrives (and tells Grapple) before
	 * this returns. A chain fired mid-zip uses this so the redirect happens in the air.
	 */
	void Launch(AGrappleAnchor* Anchor, UGrappleComponent* Grapple, float InSpeed);

	/** A grapple arrow only ever flies at an anchor; a plain launch does nothing. */
	virtual void LaunchWithVelocity(const FVector& Velocity) override;

	/** Moves the arrow DeltaSeconds along its line. Tick calls this; so does a test with no ticking. */
	void Advance(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Grapple")
	bool HasArrived() const { return bArrived; }

	UFUNCTION(BlueprintPure, Category = "Grapple")
	AGrappleAnchor* GetTargetAnchor() const { return TargetAnchor.Get(); }

protected:
	virtual void Tick(float DeltaSeconds) override;

	/** Sticks in the anchor and hands over to the grapple component. */
	void Arrive();

	TWeakObjectPtr<AGrappleAnchor> TargetAnchor;

	TWeakObjectPtr<UGrappleComponent> Grapple;

	bool bArrived = false;
};
