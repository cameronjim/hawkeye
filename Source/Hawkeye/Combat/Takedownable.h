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
 * Implemented by anything the player can silently take down (the thugs, elves included).
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

	/**
	 * Asked just before OnTakedown when the attacker can play the paired takedown: play the victim's half (his AI and
	 * movement stopped, held by the clip, handed to the ragdoll from its last frame) and return its length in seconds,
	 * or 0 when this target has no such clip (the attacker then does the takedown as before).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Takedown")
	float BeginPairedTakedown(AActor* Attacker);
	virtual float BeginPairedTakedown_Implementation(AActor* Attacker) { return 0.f; }
};
