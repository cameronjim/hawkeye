#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Takedownable.generated.h"

UINTERFACE(MinimalAPI, Blueprintable, BlueprintType)
class UTakedownable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by anything the player can silently take down (thugs, wardens).
 * Implement OnTakedown in the actor's Blueprint to play the animation, ragdoll, drop a weapon, etc.
 */
class HAWKEYE_API ITakedownable
{
	GENERATED_BODY()

public:
	/** Called on the target when a takedown succeeds. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Takedown")
	void OnTakedown(AActor* Attacker);
	virtual void OnTakedown_Implementation(AActor* Attacker) {}

	/**
	 * Lets a target veto a takedown (already alerted, already dead, scripted, ...).
	 * Thugs return false while Alerted.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Takedown")
	bool CanBeTakenDown(AActor* Attacker);
	virtual bool CanBeTakenDown_Implementation(AActor* Attacker) { return true; }
};
