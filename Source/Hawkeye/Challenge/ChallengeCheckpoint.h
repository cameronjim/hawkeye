#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChallengeCheckpoint.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UStaticMeshComponent;
class USceneComponent;

/** How a traversal checkpoint ring shows. */
UENUM(BlueprintType)
enum class EChallengeCheckpointState : uint8
{
	/** Not reached yet and not next: hidden. */
	Hidden,
	/** The one after the next: a dim ring, so the route reads a step ahead. */
	Upcoming,
	/** The one to go through now: glowing purple, lit, turning. */
	Next,
	/** Gone through. */
	Passed
};

/**
 * One traversal challenge checkpoint: a floating ring 200 cm across, spawned by UChallengeSubsystem for
 * a run and removed after. Built from short segments of the engine cube round a circle in the actor's
 * YZ plane, so the ring faces along +X (the way through). Purely visual; the subsystem's tracker decides
 * when the player has gone through it.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AChallengeCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	AChallengeCheckpoint();

	UFUNCTION(BlueprintCallable, Category = "Challenge|Checkpoint")
	void SetState(EChallengeCheckpointState NewState);

	UFUNCTION(BlueprintPure, Category = "Challenge|Checkpoint")
	EChallengeCheckpointState GetState() const { return State; }

	/** The ring's radius to the middle of its tube, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Checkpoint", meta = (ClampMin = "10.0"))
	float RingRadius = 100.f;

	/** Thickness of the tube, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Checkpoint", meta = (ClampMin = "1.0"))
	float TubeThickness = 9.f;

	/** The rings' glow: M_Emissive (Color, Intensity), purple on the next ring, dim on the one after. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Checkpoint")
	TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	/** The next ring's emissive intensity; the one after glows a quarter of it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Checkpoint", meta = (ClampMin = "0.0"))
	float NextGlow = 6.f;

	/** Degrees a second the next ring turns about its axis. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Challenge|Checkpoint")
	float SpinDegreesPerSecond = 40.f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** Places the segments round the circle. */
	void LayoutSegments();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Checkpoint")
	TObjectPtr<USceneComponent> Root;

	/** Turns about X; carries the segments. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Checkpoint")
	TObjectPtr<USceneComponent> Spinner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Checkpoint")
	TArray<TObjectPtr<UStaticMeshComponent>> Segments;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Challenge|Checkpoint")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedGlow = nullptr;

	EChallengeCheckpointState State = EChallengeCheckpointState::Hidden;

	/** BeginPlay has run: the glow is loaded and the segments may take their instances. */
	bool bLookReady = false;
};
