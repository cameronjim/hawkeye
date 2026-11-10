#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrappleAnchor.generated.h"

class AGrappleArrowProjectile;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Something a grapple arrow can bite into: a rooftop corner, a vent, a pipe. The actor's origin
 * is the base of the fitting; +X points inboard, towards LandingPoint, which is where the
 * character's feet end up after the zip (on the roof surface, clear of the parapet).
 *
 * Arrows that land here stay stuck in it until the player comes within the grapple component's
 * RecoverRadius and pulls them back into the quiver (claude-docs/gameplay-semantics.md,
 * traversal: "the arrow is recoverable at the anchor").
 *
 * BP_GrappleAnchor gives it its dark steel look; the generated district places City_Anchor_<n>.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AGrappleAnchor : public AActor
{
	GENERATED_BODY()

public:
	AGrappleAnchor();

	/** Off: never targeted, never shows a marker. A designer switch for a broken or scripted anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grapple")
	bool bEnabled = true;

	/** Where the character's feet land. Moved per instance by generate_city.py. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	FVector GetLandingLocation() const;

	/** The point the arrow flies at and the HUD marker sits on: the middle of the fitting. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	FVector GetMarkerLocation() const;

	UFUNCTION(BlueprintPure, Category = "Grapple")
	USceneComponent* GetLandingPoint() const { return LandingPoint; }

	/** Grapple arrows stuck in this anchor, waiting to be recovered, whoever shot them. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	int32 GetStuckArrowCount() const { return StuckArrowCount; }

	/** The stuck arrows Shooter can take back: the ones Shooter shot, and any nobody owns. */
	int32 GetStuckArrowCountFor(const AActor* Shooter) const;

	/** An arrow has arrived, shot by Shooter. The projectile, if any, stays here as the visible arrow until recovered. */
	void AddStuckArrow(AGrappleArrowProjectile* Arrow, AActor* Shooter = nullptr);

	/**
	 * Hands back the stuck arrows Shooter can take (every one when Shooter is null) and removes their
	 * projectiles. Returns how many. Clint walking past Kate's anchor does not pocket her arrow.
	 */
	int32 RecoverStuckArrows(AActor* Shooter = nullptr);

	/** Height of the marker above the actor origin: the centre of the 40 cm fitting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float MarkerHeight = 20.f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grapple")
	TObjectPtr<USceneComponent> Root;

	/** The fitting. A 40 cm cube for now; a vent or pipe later. Never collides. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grapple")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Where the character's feet end up: on the roof, 60 cm inboard of the anchor by default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grapple")
	TObjectPtr<USceneComponent> LandingPoint;

	UPROPERTY(Transient)
	int32 StuckArrowCount = 0;

	/** One entry per stuck arrow: its projectile (null when it had none) and who shot it (null: anyone's). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AGrappleArrowProjectile>> StuckArrows;

	TArray<TWeakObjectPtr<AActor>> StuckArrowShooters;
};
