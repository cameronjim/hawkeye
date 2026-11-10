#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "HeldEffect.generated.h"

class AThugCharacter;
class UStaticMeshComponent;

/** Where a putty arrow's hold is in its life. */
UENUM(BlueprintType)
enum class EHeldPhase : uint8
{
	/** Not activated yet. */
	None,
	/** The thug reels from the hit before the putty sets. */
	Stagger,
	/** Stuck fast: no movement, AI paused, the blob on him. */
	Held,
	/** A blob on a wall with nobody near it; it sits there, then goes. */
	SurfaceBlob,
	/** Over; the actor is on its way out. */
	Done
};

/**
 * The putty arrow's glue. A thug hit directly, or standing within HoldRadius of where the putty
 * landed, reels for his own StaggerSeconds and is then held for HoldSeconds: his movement is off,
 * his AI does nothing (AThugAIController::SetHeld), and a putty blob sits on his chest. A putty
 * arrow that lands with nobody near leaves a blob on the surface for SurfaceBlobSeconds.
 * A second putty on a thug already held restarts his hold instead of stacking a second one.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AHeldEffect : public AArrowEffect
{
	GENERATED_BODY()

public:
	AHeldEffect();

	/** How long the thug is stuck once the putty sets, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Putty", meta = (ClampMin = "0.0"))
	float HoldSeconds = 4.f;

	/** A thug this close to a surface hit is held too, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Putty", meta = (ClampMin = "0.0"))
	float HoldRadius = 200.f;

	/** How long a blob on a wall with nobody to hold stays, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Putty", meta = (ClampMin = "0.0"))
	float SurfaceBlobSeconds = 10.f;

	/** Blob diameter, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Putty", meta = (ClampMin = "1.0"))
	float BlobSize = 55.f;

	/**
	 * Where the blob sits on a held thug, from his capsule centre in his own frame (X forward): on
	 * the front of his chest. The capsule, not a bone, so it never depends on how a skeleton's axes
	 * are authored.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Putty")
	FVector BlobOffset = FVector(22.f, 0.f, 36.f);

	/** Putty purple, glowing faintly (M_ArrowNock), so it reads as Kate's on a red tracksuit at night. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Putty")
	FLinearColor PuttyColor = FLinearColor(0.08f, 0.035f, 0.13f);

	UFUNCTION(BlueprintPure, Category = "Putty")
	EHeldPhase GetPhase() const { return Phase; }

	/** The thug being held (or about to be), or null for a surface blob. */
	UFUNCTION(BlueprintPure, Category = "Putty")
	AThugCharacter* GetTarget() const { return Target.Get(); }

	/** Seconds the current phase has left. */
	UFUNCTION(BlueprintPure, Category = "Putty")
	float GetPhaseRemaining() const { return PhaseRemaining; }

	/** Starts the hold on Thug over again: another putty arrow on someone already stuck. */
	void RestartHold();

	/** The living thug HoldRadius or less from Point, nearest first, or null. */
	static AThugCharacter* FindThugNear(UWorld* World, const FVector& Point, float Radius);

	virtual void Activate() override;
	virtual void AdvanceEffect(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The putty sets: movement off, AI paused, the blob onto his chest. */
	void BeginHold();

	/** The putty lets go. Safe to call twice. */
	void Release();

	/** Puts the blob on the wall at the impact. */
	void BeginSurfaceBlob();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Putty")
	TObjectPtr<UStaticMeshComponent> Blob;

	/** Two smaller gobs thrown off the main one, so it reads as goo splashed over him. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Putty")
	TObjectPtr<UStaticMeshComponent> Splat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Putty")
	TObjectPtr<UStaticMeshComponent> Splat2;

	/** Shows or hides the blob and, on a thug, the gobs. */
	void ShowBlob(bool bWithSplats);

	TWeakObjectPtr<AThugCharacter> Target;
	EHeldPhase Phase = EHeldPhase::None;
	float PhaseRemaining = 0.f;
	bool bHolding = false;
};
