#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArrowSwitch.generated.h"

class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArrowSwitchTrippedSignature, AArrowSwitch*, Switch);

/**
 * A box on a wall an arrow trips (claude-docs/gameplay-semantics.md, "Chapter sites", switches): an alarm buzzer that
 * pulls a guard over to look (a noise at it, heard as any noise is), and the chapter event "switch:<SwitchId>". Most
 * want a returning arrow's blunt head (bReturningArrowsOnly): a plain arrow sticks in the box and does nothing. Trips
 * once; its lamp goes from red to off. A chapter site spawns it (FChapterSiteSwitch).
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AArrowSwitch : public AActor
{
	GENERATED_BODY()

public:
	AArrowSwitch();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	FName SwitchId;

	/** The noise it makes tripping: a thug hears it within HearingRange times this (walls and doors count). 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch", meta = (ClampMin = "0.0"))
	float NoiseLoudness = 1.f;

	/** Only a returning arrow (AReturningArrowProjectile) trips it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Switch")
	bool bReturningArrowsOnly = true;

	/** "switch:<Id>". */
	static FName EventFor(FName Id);

	/**
	 * An arrow (Projectile) loosed by Shooter hit it. True when that tripped it: not tripped before, and the projectile
	 * is a returning arrow unless any arrow will do.
	 */
	bool HandleArrowHit(AActor* Projectile, AActor* Shooter);

	/** The rule: whether a projectile of this kind trips a switch that wants (or does not want) a returning arrow. */
	static bool Trips(bool bTrippedAlready, bool bReturningOnly, bool bProjectileReturns);

	UFUNCTION(BlueprintPure, Category = "Switch")
	bool IsTripped() const { return bTripped; }

	void SetMesh(UStaticMesh* Mesh);

	UPROPERTY(BlueprintAssignable, Category = "Switch")
	FOnArrowSwitchTrippedSignature OnTripped;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<UStaticMeshComponent> Box;

	/** A small red lamp on its face while it is armed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Switch")
	TObjectPtr<UPointLightComponent> Lamp;

	bool bTripped = false;
};
