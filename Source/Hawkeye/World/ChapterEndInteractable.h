#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Interactable.h"
#include "ChapterEndInteractable.generated.h"

class UBoxComponent;
class UCameraComponent;
class UStaticMeshComponent;

/**
 * Something the player examines to finish a chapter (claude-docs/gameplay-semantics.md, "Chapter
 * end"). Interact asks the controller for a close-up on CloseUpCamera (the view blends in, pushes
 * slowly toward the prop for CloseUpSeconds and blends back), then completes ObjectiveId. When that
 * completes the mission, the controller's end sequence starts with the close-up: close-up, end card,
 * the flashback if the mission has one, then roaming again or the next level.
 *
 * Generic: what it looks like is Prop and PropAccent's meshes and materials, set by whoever places
 * it (Tools/Editor/generate_city.py puts CH01's, City_ChapterEnd, in the find_arrow water tower).
 * Like an objective volume it registers its location as ObjectiveId's marker point.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AChapterEndInteractable : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AChapterEndInteractable();

	/** The mission objective this completes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end")
	FName ObjectiveId;

	/** Only offered while ObjectiveId is the current objective, so the chapter's order holds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end")
	bool bRequireCurrentObjective = true;

	/** The prompt the HUD shows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end")
	FText Prompt;

	/** How long the close-up lasts, blend in included, before the view blends back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end|Camera", meta = (ClampMin = "0.1"))
	float CloseUpSeconds = 2.f;

	/** Seconds each blend (in, and back) takes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end|Camera", meta = (ClampMin = "0.0"))
	float CloseUpBlendSeconds = 0.5f;

	/** How far the camera eases forward along its view during the close-up, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end|Camera", meta = (ClampMin = "0.0"))
	float PushDistance = 35.f;

	/** Where the close-up camera sits, in this actor's frame (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end|Camera")
	FVector CloseUpOffset = FVector(-120.f, 0.f, 0.f);

	/** The point it looks at, in this actor's frame (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end|Camera")
	FVector CloseUpLookAt = FVector::ZeroVector;

	/** The prop's turn relative to the actor (an arrow's pitch as it went in). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end")
	FRotator PropRotation = FRotator::ZeroRotator;

	/** Centre of the zone that offers the prompt, in this actor's frame (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end")
	FVector InteractZoneOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chapter end", meta = (ClampMin = "10.0"))
	FVector InteractZoneExtent = FVector(200.f, 200.f, 200.f);

	//~ Begin IInteractable interface
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	//~ End IInteractable interface

	/** Starts the push: the camera eases forward over CloseUpSeconds. The controller calls this. */
	void BeginPush();

	/** Puts the camera back where it started. */
	void EndPush();

	UFUNCTION(BlueprintPure, Category = "Chapter end")
	bool HasBeenUsed() const { return bUsed; }

	UFUNCTION(BlueprintPure, Category = "Chapter end|Camera")
	UCameraComponent* GetCloseUpCamera() const { return CloseUpCamera; }

	/** Blueprint hook: sound, a light, a line, when the player examines it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chapter end")
	void OnExamined(AActor* Interactor);

protected:
	/** Places the camera, the prop and the zone from the properties above. */
	virtual void OnConstruction(const FTransform& Transform) override;

	void ApplyLayout();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chapter end")
	TObjectPtr<USceneComponent> Root;

	/** The thing examined. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chapter end")
	TObjectPtr<UStaticMeshComponent> Prop;

	/** A second part in its own material (an arrow's fletching). Optional. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chapter end")
	TObjectPtr<UStaticMeshComponent> PropAccent;

	/** Where the close-up looks from. Place it; the push moves it along its own forward. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chapter end|Camera")
	TObjectPtr<UCameraComponent> CloseUpCamera;

	/** Standing in here offers the prompt even when the camera trace misses the prop. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chapter end")
	TObjectPtr<UBoxComponent> InteractZone;

private:
	bool bUsed = false;
	bool bPushing = false;
	float PushElapsed = 0.f;
	FVector CameraStart = FVector::ZeroVector;
};
