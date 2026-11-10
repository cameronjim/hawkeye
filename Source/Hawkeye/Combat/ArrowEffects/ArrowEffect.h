#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/ArrowDefinition.h"
#include "ArrowEffect.generated.h"

class UMaterialInterface;
class UNiagaraComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * What a trick arrow leaves behind where it lands (claude-docs/gameplay-semantics.md, "trick
 * arrows"). The arrow spawns one at the impact, hands it the hit, and the effect does the rest on
 * its own clock: AHeldEffect (putty), ABolaEffect, ASmokeCloud, AEmpPulse, AExplosiveBlast.
 *
 * Custom actors rather than the Gameplay Ability System: six small effects that each touch one or
 * two existing flags (a thug's knockdown, his controller's held / blinded / jammed) are simpler as
 * a timer on an actor than as abilities, attributes and GameplayEffects nobody else in the game
 * uses. Every effect runs off AdvanceEffect, which Tick calls and a test can call directly.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class HAWKEYE_API AArrowEffect : public AActor
{
	GENERATED_BODY()

public:
	AArrowEffect();

	/**
	 * Spawns Arrow's effect at Hit and activates it. The class is Arrow->EffectClass, or the stock
	 * class for its OnHitEffect; null for None and Grapple.
	 */
	static AArrowEffect* SpawnForHit(
		UWorld* World, const UArrowDefinition* Arrow, AActor* Shooter, const FHitResult& Hit);

	/** The stock effect class for Effect, or null for None and Grapple. */
	static TSubclassOf<AArrowEffect> GetDefaultClassFor(EArrowHitEffect Effect);

	/** Records what hit where and who shot it. Called before Activate. */
	void InitEffect(const UArrowDefinition* InArrow, AActor* InShooter, const FHitResult& InHit);

	/** Does the effect's work at the impact. SpawnForHit calls it; a test calls it after InitEffect. */
	UFUNCTION(BlueprintCallable, Category = "Arrow Effect")
	virtual void Activate();

	/** Moves the effect DeltaSeconds on. Tick calls it; a test with no ticking world calls it. */
	UFUNCTION(BlueprintCallable, Category = "Arrow Effect")
	virtual void AdvanceEffect(float DeltaSeconds);

	/** Seconds since Activate. */
	UFUNCTION(BlueprintPure, Category = "Arrow Effect")
	float GetElapsed() const { return Elapsed; }

	UFUNCTION(BlueprintPure, Category = "Arrow Effect")
	bool IsActivated() const { return bActivated; }

	/** Where the arrow landed. */
	UFUNCTION(BlueprintPure, Category = "Arrow Effect")
	FVector GetImpactPoint() const { return ImpactPoint; }

	/** The actor the arrow hit, or null for thin air. */
	UFUNCTION(BlueprintPure, Category = "Arrow Effect")
	AActor* GetHitActor() const { return HitActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Arrow Effect")
	AActor* GetShooter() const { return Shooter.Get(); }

	/** True once SpawnEffectVfx drew something: the placeholder shapes are hidden from then on. */
	UFUNCTION(BlueprintPure, Category = "Arrow Effect")
	bool HasEffectVfx() const { return bEffectVfxSpawned; }

	/** The definition's damage (0 when spawned without one). */
	UFUNCTION(BlueprintPure, Category = "Arrow Effect")
	float GetArrowDamage() const { return ArrowDamage; }

protected:
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Spawns the definition's EffectVfx at Location for Event, its Z along the impact normal. Returns
	 * the component, or null when unset or culled; either way the request is counted.
	 */
	UNiagaraComponent* SpawnEffectVfx(FName Event, const FVector& Location);

	/** Hides every mesh part of this effect (the placeholder look), leaving its lights alone. */
	void HidePlaceholders();

	/** A mesh part with no collision and a tint through BasicShapeMaterial's (or Material's) Color. */
	UStaticMeshComponent* MakeVisualPart(const TCHAR* Name, UStaticMesh* Mesh);

	/** Loads Path (a translucent or additive effect material) or falls back to BasicShapeMaterial. */
	static UMaterialInterface* LoadEffectMaterial(const TCHAR* Path);

	/** Puts Material on Part and sets its Color (and Opacity/Intensity when the material has them). */
	static void TintPart(
		UStaticMeshComponent* Part, UMaterialInterface* Material, const FLinearColor& Color, float Opacity = 1.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow Effect")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<const UArrowDefinition> Arrow = nullptr;

	TWeakObjectPtr<AActor> Shooter;
	TWeakObjectPtr<AActor> HitActor;

	FVector ImpactPoint = FVector::ZeroVector;
	FVector ImpactNormal = FVector::UpVector;
	FName HitBone = NAME_None;
	float ArrowDamage = 0.f;
	float Elapsed = 0.f;
	bool bActivated = false;
	bool bEffectVfxSpawned = false;

	/** The engine's basic shapes, found once in the constructor. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShapeMaterial = nullptr;

public:
	/** Translucent unlit Color/Opacity material (smoke), built by create_weapon_data.py. */
	static const TCHAR* const FxMaterialPath;

	/** Additive unlit Color/Intensity material (EMP ring, fireball), built by create_weapon_data.py. */
	static const TCHAR* const GlowMaterialPath;
};
