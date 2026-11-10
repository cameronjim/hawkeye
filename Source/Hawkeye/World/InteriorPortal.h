#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Interactable.h"
#include "InteriorPortal.generated.h"

class UBoxComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * A door between the district and an interior map: a leaf in a dark surround with a sign over it and a
 * zone in front that counts as being at the door. Interact fades the screen to black over FadeSeconds and
 * then travels (AInteriorEntrance goes in, AInteriorExit comes out). Local frame: the origin is the
 * middle of the threshold at floor level, +X points at the side the player uses it from.
 * Rules: claude-docs/gameplay-semantics.md, "Interiors".
 */
UCLASS(Abstract, NotBlueprintable)
class HAWKEYE_API AInteriorPortal : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AInteriorPortal();

	/** Seconds of fade to black before the level changes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0.0"))
	float FadeSeconds = 0.5f;

	/** Starts the fade and the travel after it. False when Interactor is not the player or a travel is already under way. */
	UFUNCTION(BlueprintCallable, Category = "Interior")
	bool Use(AActor* Interactor);

	/** Between Use and the level changing. */
	UFUNCTION(BlueprintPure, Category = "Interior")
	bool IsTravelling() const { return bTravelling; }

	/** What the sign over the door says. */
	UFUNCTION(BlueprintCallable, Category = "Interior")
	void SetSignText(const FText& Text);

	UFUNCTION(BlueprintPure, Category = "Interior")
	UBoxComponent* GetEntryZone() const { return EntryZone; }

	//~ Begin IInteractable interface
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractable interface

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The screen is black: change level. Returns false if it could not (the fade is undone). */
	virtual bool Travel() PURE_VIRTUAL(AInteriorPortal::Travel, return false;);

	/** Puts the parts for a leaf whose face is LeafFrontX in front of the origin (0 in a wall's centre line). */
	void LayOutParts(float LeafFrontX);

	void TravelNow();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<USceneComponent> Root;

	/** Dark surround, one box behind the leaf and a little bigger. Scenery, no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<UStaticMeshComponent> DoorFrame;

	/** The leaf, 100 x 220; blocks the doorway (it never opens, the level changes instead). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<UStaticMeshComponent> Door;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<UStaticMeshComponent> SignBoard;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<UTextRenderComponent> SignText;

	/** A small wash over the door so it reads as the way through. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<UPointLightComponent> DoorLight;

	/** Standing in here counts as being at the door (the interaction component offers it). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior")
	TObjectPtr<UBoxComponent> EntryZone;

private:
	FTimerHandle TravelTimer;
	bool bTravelling = false;
};
