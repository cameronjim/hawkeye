#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Interactable.h"
#include "PickupActor.generated.h"

class UArrowDefinition;
class USphereComponent;
class UBowDefinition;
class UStaticMeshComponent;

/** What picking this up actually does. */
UENUM(BlueprintType)
enum class EPickupType : uint8
{
	/** Gives the player Bow. */
	Bow,
	/** Adds KeycardId to the player's keycard ring. */
	Keycard,
	/** Adds ArrowCount of Arrow to the quiver, up to its cap. */
	Arrows
};

/**
 * A thing on the floor the player walks up to and presses Interact on. One class covers a bow,
 * a keycard and a bundle of arrows; which one it is, is data.
 *
 * Thugs spawn these from AThugCharacter::DropOnDeath.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API APickupActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APickupActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	EPickupType PickupType = EPickupType::Keycard;

	/** The bow a Bow pickup gives. Soft so a level of pickups does not load every definition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	TSoftObjectPtr<UBowDefinition> Bow;

	/** The arrow type an Arrows pickup adds. None set means the arrow nocked in the active slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	TSoftObjectPtr<UArrowDefinition> Arrow;

	/** Keycard id granted by a Keycard pickup. Doors match on this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	FName KeycardId = FName(TEXT("red"));

	/** Arrows an Arrows pickup adds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "0"))
	int32 ArrowCount = 6;

	/** Optional mission objective completed the moment this is picked up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	FName CompletesObjectiveId;

	/** Prompt override; when empty a sensible default per PickupType is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	FText PromptOverride;

	/** Root and collision proxy. The silhouette is built from Part1..Part4 instead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * Four boxes the pickup's shape is assembled from: body and stripe for the keycard. Fixed components rather than script-created
	 * subobjects because a Blueprint CDO's component templates have to exist in C++ for the
	 * content script to be able to write to them reliably.
	 *
	 * Mesh, scale, offset and material are all set in Tools/Editor/create_world_blueprints.py.
	 * A part with no static mesh simply does not render, so a two-part pickup leaves two empty.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup|Parts")
	TObjectPtr<UStaticMeshComponent> Part1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup|Parts")
	TObjectPtr<UStaticMeshComponent> Part2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup|Parts")
	TObjectPtr<UStaticMeshComponent> Part3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup|Parts")
	TObjectPtr<UStaticMeshComponent> Part4;

	/**
	 * Standing within this of the pickup counts as looking at it (the third-person camera trace starts
	 * metres behind her and misses a keycard on a desk). Overlaps pawns only.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<USphereComponent> PickupZone;

	/** All four parts in order, for a script or a test that wants to walk them. */
	UFUNCTION(BlueprintPure, Category = "Pickup|Parts")
	TArray<UStaticMeshComponent*> GetParts() const;

	// --- Idle motion ----------------------------------------------------------------------------

	/** Slow yaw spin so a pickup on a grey floor reads as a pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Motion")
	float SpinDegreesPerSecond = 45.f;

	/** Centimetres the pickup rises and falls around its hover height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Motion", meta = (ClampMin = "0.0"))
	float BobAmplitude = 3.f;

	/** Bobs per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Motion", meta = (ClampMin = "0.0"))
	float BobHz = 0.7f;

	/** Height the pickup floats at above wherever it was placed or dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Motion", meta = (ClampMin = "0.0"))
	float HoverHeight = 40.f;

	/** Applies the pickup to Interactor and destroys this actor. Returns true when it applied. */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	bool ApplyTo(AActor* Interactor);

	/** VFX/SFX hook. Fired just before the actor is destroyed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Pickup")
	void OnPickedUp(AActor* Interactor);

	/**
	 * In a thug's pocket (AThugCharacter::CarriedPickups): hidden, no collision, no motion, and nothing
	 * to interact with, until DropAt. The thug sets it at BeginPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void SetCarried(bool bInCarried);

	UFUNCTION(BlueprintPure, Category = "Pickup")
	bool IsCarried() const { return bCarried; }

	/** Out of the carrier's pocket onto the floor at Where: shown, reachable, hovering up from there. */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void DropAt(FVector Where);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Where the pickup was placed or dropped; the hover and bob are measured from here. */
	UPROPERTY(Transient)
	FVector RestLocation = FVector::ZeroVector;

	/** Seeded per actor so two pickups dropped together do not bob in lockstep. */
	UPROPERTY(Transient)
	float BobPhase = 0.f;

	float BobElapsed = 0.f;

	bool bCarried = false;

public:
	//~ Begin IInteractable interface
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractable interface
};
