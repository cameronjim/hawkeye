#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFocusedInteractableChangedSignature, AActor*, Focused);

/**
 * Player-side "look at a thing and press E". Traces from the owning pawn's view point every
 * RefreshSeconds, keeps the focused IInteractable, and pushes its prompt to the HUD.
 *
 * AHawkeyeCharacter owns one and binds TryInteract to IA_Interact.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

	/** How far ahead of the camera an interactable is picked up, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float InteractRange = 250.f;

	/** Seconds between focus traces. 0 traces every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float RefreshSeconds = 0.1f;

	/** Radius of the sweep, so the player does not have to be pixel-accurate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float TraceRadius = 12.f;

	/** When true the component writes the focused actor's prompt to the player's HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bDrivesHudPrompt = true;

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnFocusedInteractableChangedSignature OnFocusedInteractableChanged;

	/** Interacts with the focused actor. Returns true when something happened. */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool TryInteract();

	/** Re-runs the focus trace right now and updates the prompt. */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void RefreshFocus();

	UFUNCTION(BlueprintPure, Category = "Interaction")
	AActor* GetFocusedActor() const { return FocusedActor; }

	/** Prompt of the focused actor, or empty text when nothing is focused. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	FText GetFocusPrompt() const;

	/** The best interactable in front of the owner right now, ignoring the cached focus. */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	AActor* TraceForInteractable() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** View point the trace starts from: the player camera for a player-controlled pawn. */
	void GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	void SetFocusedActor(AActor* NewFocus);

	/** Pushes Prompt (or the takedown prompt, or nothing) to the HUD. */
	void UpdateHudPrompt();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<AActor> FocusedActor = nullptr;

private:
	float TimeSinceRefresh = 0.f;
};
