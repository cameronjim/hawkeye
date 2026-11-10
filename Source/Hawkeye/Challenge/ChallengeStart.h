#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Interactable.h"
#include "ChallengeStart.generated.h"

class UBoxComponent;
class UChallengeDefinition;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Where a side challenge starts: a dark pedestal with a glowing purple top, a floating icon turning
 * over it (a small target for archery, a small ring for traversal), the challenge's name above, and a
 * purple light so it reads from across the street. Interact (E) starts Definition's run through
 * UChallengeSubsystem. Placed by Tools/Editor/generate_city.py as City_Challenge_<id> at the
 * definition's StartLocation. Local frame: origin at the pedestal's foot, +X toward the targets or
 * along the route.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AChallengeStart : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AChallengeStart();

	/** The challenge this pedestal starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<UChallengeDefinition> Definition;

	/** The glow on the top and the icon: M_Emissive (Color, Intensity), an instance per part. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	/**
	 * The cap's emissive intensity at night. The top and the icon are what read from the next roof, but
	 * past about 0.5 the night's +2 EV washes the purple out toward white (2.5 read white, 0.8 lilac).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge", meta = (ClampMin = "0.0"))
	float CapGlow = 0.35f;

	/** The icon's emissive intensity at night (a ring icon twice it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge", meta = (ClampMin = "0.0"))
	float IconGlow = 0.6f;

	/** The purple point light's brightness, lm: 135, 30% of the 450 that washed the whole roof. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge", meta = (ClampMin = "0.0"))
	float GlowLumens = 135.f;

	/** How far the purple light reaches, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge", meta = (ClampMin = "0.0"))
	float GlowRadius = 400.f;

	/** The purple point light, for tests and the look. */
	UPointLightComponent* GetGlowLight() const { return Glow; }

	/** Degrees a second the icon turns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge")
	float IconSpinDegreesPerSecond = 60.f;

	/** Starts the challenge. Interactor is the player's pawn. */
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;

	/** Where the player's feet go to start, or to retry: 150 cm in front of the pedestal, facing along +X. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	FVector GetStandLocation() const;

	UFUNCTION(BlueprintPure, Category = "Challenge")
	UBoxComponent* GetEntryZone() const { return EntryZone; }

	/** Shows the icon and the name for Definition's type. The game calls it at BeginPlay. */
	void RefreshLook();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<UStaticMeshComponent> Pedestal;

	/** The glowing disc on top. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<UStaticMeshComponent> Cap;

	/** Turns about Z over the cap; carries both icons. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<USceneComponent> IconSpinner;

	/** Archery's icon: three stacked discs, a small target. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TArray<TObjectPtr<UStaticMeshComponent>> TargetIcon;

	/** Traversal's icon: a ring of short segments. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TArray<TObjectPtr<UStaticMeshComponent>> RingIcon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<UPointLightComponent> Glow;

	/** Standing in here counts as being at the pedestal, as with the safehouse door. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<UBoxComponent> EntryZone;
};
