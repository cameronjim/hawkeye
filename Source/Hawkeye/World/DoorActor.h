#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Interactable.h"
#include "DoorActor.generated.h"

class ANavLinkProxy;
class UBoxComponent;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;

/** How the door leaf gets out of the way. */
UENUM(BlueprintType)
enum class EDoorMotion : uint8
{
	/** Swings around the frame's vertical axis by OpenYawDegrees. */
	Swing,
	/** Slides along its own Y axis by SlideDistance. */
	Slide
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDoorOpenedSignature, AActor*, Opener);

/**
 * A door the player opens with Interact, optionally locked behind a keycard id.
 *
 * Animation is a lerp in Tick rather than a timeline so the whole thing stays in C++ and a
 * test can drive it by calling OpenNow(). Blocks the player while closed.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API ADoorActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ADoorActor();

	/** When true the door refuses to open until the player carries RequiredKeycardId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bLocked = false;

	/** Keycard id that unlocks this door. Ignored when bLocked is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FName RequiredKeycardId = FName(TEXT("red"));

	/** Mission objective completed the first time this door opens. Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	FName CompletesObjectiveId;

	/**
	 * The door unlocks and opens by itself when this objective completes (or is already complete when it starts, or a
	 * load says so): the grotto's workshop door, opened by the keypad's examine. It answers no interaction while it
	 * waits. None: an ordinary door.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	FName OpensOnObjective;

	/** Opens it if OpensOnObjective is complete. True when that opened it. */
	bool CheckOpensOnObjective();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	EDoorMotion Motion = EDoorMotion::Slide;

	/** Seconds the door takes to travel from closed to open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door", meta = (ClampMin = "0.01"))
	float OpenSeconds = 1.f;

	/** Swing amount, in degrees, for EDoorMotion::Swing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	float OpenYawDegrees = -95.f;

	/** Slide amount along the leaf's local Y, in centimetres, for EDoorMotion::Slide. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	float SlideDistance = 110.f;

	/**
	 * Height of the leaf in centimetres. Only used to sit it on the floor: the mesh itself is
	 * scaled in the Blueprint, and this is what says where its centre goes.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Door", meta = (ClampMin = "1.0"))
	float LeafHeight = 220.f;

	/** Height of the frame surround, same deal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Door", meta = (ClampMin = "1.0"))
	float FrameHeight = 260.f;

	/**
	 * An unlocked door opens for a thug who walks into InteractZone (patrols go through doors). A locked
	 * one never does: only her keycard opens it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bOpensForThugs = true;

	/**
	 * The nav link that carries paths through this doorway (generate_interior.py places it: the navmesh
	 * leaves nothing of a 100 cm doorway). While the door is locked and shut its area is Null, so nobody
	 * plans a walk through it; from the moment it opens it is Default again and its tiles are rebuilt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	TObjectPtr<ANavLinkProxy> NavLink;

	/** True when NavLink lets paths through: the door is unlocked or open. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsPassable() const { return !bLocked || bOpen; }

	UPROPERTY(BlueprintAssignable, Category = "Door")
	FOnDoorOpenedSignature OnDoorOpened;

	/**
	 * The actor's root, at the foot of the doorway. Everything else hangs off it.
	 *
	 * The frame used to be the root, and a root component's relative location is overwritten by
	 * the spawn transform - so the 130 cm the content script wrote on it was thrown away and the
	 * leaf, whose offset is measured from the frame, ended up floating near the ceiling with a
	 * hole at eye height. A root that is never moved keeps both offsets honest.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> DoorRoot;

	/**
	 * Static surround; never moves and never collides. It is scenery: the leaf is what stops the
	 * player and what the interaction sweep has to find, and a frame that blocked would seal the
	 * opening the door just cleared.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> FrameMesh;

	/** The leaf that animates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/**
	 * Standing in here, on either side of the doorway, counts as being at the door: in third person the
	 * interaction trace starts at the camera, metres behind the character, and rarely reaches a leaf.
	 * Overlaps pawns only; never blocks anything.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> InteractZone;

	/**
	 * Opens the door if it can be. Returns false when it is locked and Interactor has no keycard,
	 * or when it is already open / opening.
	 */
	UFUNCTION(BlueprintCallable, Category = "Door")
	bool TryOpen(AActor* Interactor);

	/** Opens with no keycard check (level scripting, tests). Returns false if already opening. */
	UFUNCTION(BlueprintCallable, Category = "Door")
	bool OpenNow(AActor* Interactor);

	/** True when Interactor carries RequiredKeycardId, or the door is not locked at all. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsUnlockedFor(const AActor* Interactor) const;

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsOpen() const { return bOpen; }

	/** World centre of the leaf. Exposed so a test can trace at the heights it really covers. */
	UFUNCTION(BlueprintPure, Category = "Door")
	FVector GetLeafWorldCentre() const;

	/** 0 closed, 1 fully open. */
	UFUNCTION(BlueprintPure, Category = "Door")
	float GetOpenAlpha() const { return OpenAlpha; }

	/** Blueprint hook for the buzzer / denied beep. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Door")
	void OnOpenRefused(AActor* Interactor);

	//~ Begin IInteractable interface
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractable interface

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void HandleObjectiveUpdated(class UMissionObjective* Objective, int32 ObjectiveIndex);

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	/** Puts the leaf at OpenAlpha along its closed -> open path. */
	void ApplyOpenAlpha();

	/** NavLink's area Default when IsPassable, Null otherwise. */
	void UpdateNavLink();

	UFUNCTION()
	void HandleZoneBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	bool bOpen = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	float OpenAlpha = 0.f;

private:
	/** Leaf transform at BeginPlay; the open pose is derived from it. */
	FVector ClosedRelativeLocation = FVector::ZeroVector;
	FRotator ClosedRelativeRotation = FRotator::ZeroRotator;

	bool bAnimating = false;

};
