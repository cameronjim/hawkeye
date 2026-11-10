#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(MinimalAPI, Blueprintable, BlueprintType)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything the player can look at and press Interact on: doors, pickups, levers.
 *
 * UInteractionComponent line-traces from the camera, asks the focused actor for its prompt and
 * calls Interact when the action fires. Implement the three events in C++ or in the Blueprint.
 */
class HAWKEYE_API IInteractable
{
	GENERATED_BODY()

public:
	/** Do the thing. Only called when CanInteract returned true. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void Interact(AActor* Interactor);
	virtual void Interact_Implementation(AActor* Interactor) {}

	/** Player-facing line for the HUD prompt ("Open", "Locked: keycard required"). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FText GetInteractPrompt() const;
	virtual FText GetInteractPrompt_Implementation() const { return FText::GetEmpty(); }

	/**
	 * False hides the actor from the interaction component entirely: no prompt, no Interact.
	 * A locked door still returns true so the player is told why it will not open.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	bool CanInteract(AActor* Interactor) const;
	virtual bool CanInteract_Implementation(AActor* Interactor) const { return true; }
};
