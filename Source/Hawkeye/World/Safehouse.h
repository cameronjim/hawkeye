#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Interactable.h"
#include "Safehouse.generated.h"

class UBoxComponent;
class UHawkeyeSaveSubsystem;
class UInventoryComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * A safehouse doorway: a lit purple door in a storefront with a SAFEHOUSE sign over it.
 *
 * Walking up to it and pressing Interact (E) heals to full, marks the safehouse discovered, writes
 * an autosave, and opens the safehouse menu (refill arrows, save, fast travel, chapter select) on
 * AHawkeyePlayerController. Placed by Tools/Editor/generate_city.py as City_Safehouse. Local
 * frame: +X points out of the facade into the street, +Y along it, the origin at pavement level.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API ASafehouse : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ASafehouse();

	/** Stable id the save records when the safehouse is found. Lowercase snake_case. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safehouse")
	FName SafehouseId = TEXT("ch01_east_7th");

	/** What the menu and the fast-travel list call it (a placeholder, "[Safehouse 1]", until the story names it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safehouse")
	FText DisplayName;

	/** The street address, shown under the name in the menu. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safehouse")
	FText Address;

	/** How far out from the door fast travel puts the player, cm (inside the entry zone). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safehouse|Travel", meta = (ClampMin = "50.0"))
	float ArrivalOut = 180.f;

	/** Where the partner stands beside her: out from the door and along the facade, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safehouse|Travel")
	FVector2D PartnerArrivalOffset = FVector2D(200.f, 130.f);

	/** Heals, discovers, autosaves and opens the menu. Interactor is the player's pawn. */
	virtual void Interact_Implementation(AActor* Interactor) override;

	/**
	 * Arrival spot at pavement level, facing the street: slot 0 for the player, 1 for the partner.
	 */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	FTransform GetArrivalTransform(int32 Slot) const;

	/**
	 * Walking in: a player-controlled Hawkeye in the entry zone discovers the safehouse (a toast and an
	 * autosave the first time) and clears the safehouse marker. True when this found it for the first
	 * time. Save is the subsystem to record it in; null uses this world's.
	 */
	bool DiscoverBy(AActor* Visitor, UHawkeyeSaveSubsystem* Save = nullptr);
	virtual FText GetInteractPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;

	/** DisplayName, or the id when a designer left it empty. */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	FText GetDisplayName() const;

	/** The menu's Refill arrows: every filled slot to its cap. Returns arrows added. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	static int32 RefillArrows(UInventoryComponent* Inventory);

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	UBoxComponent* GetEntryZone() const { return EntryZone; }

protected:
	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	UFUNCTION()
	void HandleEntryOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<USceneComponent> Root;

	/** Dark surround the door is set into. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<UStaticMeshComponent> DoorFrame;

	/** The purple door itself; generate_city.py gives it the emissive beacon material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<UStaticMeshComponent> Door;

	/** The board the sign's letters sit on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<UStaticMeshComponent> SignBoard;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<UTextRenderComponent> SignText;

	/** Purple wash over the door so it reads from across the street at night. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<UPointLightComponent> DoorLight;

	/**
	 * Standing in here counts as being at the door: the interaction component offers the
	 * safehouse to a pawn overlapping it even when the camera trace misses the door.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<UBoxComponent> EntryZone;
};
