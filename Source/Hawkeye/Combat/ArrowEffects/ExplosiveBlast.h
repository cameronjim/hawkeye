#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "ExplosiveBlast.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * The explosive arrow's blast. Everything with health within Radius of the impact takes the
 * arrow's damage (80) at the centre falling linearly to 0 at the edge, Kate included if she is in
 * it. Surviving thugs are knocked down for KnockdownSeconds and thrown; the player's camera
 * shakes within ShakeRadius. No cover check: the blast goes round corners. TODO(stage3): occlusion.
 *
 * The look is the orange flash light plus the definition's EffectVfx (NS_Explosion: fireball, core
 * flash, sparks, a smoke puff) and its GroundDecal (M_Decal_Scorch) on the ground found by a trace
 * down from the impact. Without the system the old additive fireball spheres (M_ArrowGlow) stand in.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AExplosiveBlast : public AArrowEffect
{
	GENERATED_BODY()

public:
	AExplosiveBlast();

	/** Where the damage reaches 0, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "1.0"))
	float Radius = 400.f;

	/** The shooter's partner (Kate in Clint's blast, or Clint in hers) takes this share of the damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PartnerDamageScale = 0.5f;

	/** How long a surviving thug is down, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float KnockdownSeconds = 2.f;

	/** How hard a thug at the centre is thrown, cm/s; less further out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float ThrowSpeed = 900.f;

	/** The player's camera shakes when she is this close, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float ShakeRadius = 2500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float ShakeSeconds = 0.5f;

	/** Lens throw at the start of the shake for a blast at her feet, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float ShakeAmplitude = 18.f;

	/** How long the fireball and the flash last, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.05"))
	float FlashSeconds = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive")
	FLinearColor FireColor = FLinearColor(1.f, 0.45f, 0.08f);

	/**
	 * The flash light's peak with the Niagara fireball up, cd. It only throws the colour; at 700 it
	 * lit the smoke and the street as one flat orange sheet.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float BlastLightCandelas = 400.f;

	/** The flash light's peak over the placeholder spheres, when NS_Explosion is missing, cd. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float PlaceholderLightCandelas = 2500.f;

	/**
	 * How long a shaken player's camera draws without motion blur, s: the shake's first jolts
	 * otherwise smear the fireball into a grey wash.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float MotionBlurOffSeconds = 0.3f;

	/**
	 * Damage at Distance from a blast of MaxDamage that reaches 0 at InRadius: linear, never
	 * negative, MaxDamage at the centre. Pure.
	 */
	UFUNCTION(BlueprintPure, Category = "Explosive")
	static float ComputeFalloffDamage(float MaxDamage, float Distance, float InRadius);

	/** What the flash light is multiplied by: 1, or 0.3 with "reduce flashing". Read when it goes off. */
	UFUNCTION(BlueprintPure, Category = "Explosive")
	float GetFlashScale() const { return FlashScale; }

	/** The flash light's intensity now, cd. */
	UFUNCTION(BlueprintPure, Category = "Explosive")
	float GetFlashIntensity() const;

	/** Actors this blast damaged. */
	UFUNCTION(BlueprintPure, Category = "Explosive")
	int32 GetVictimCount() const { return VictimCount; }

	virtual void Activate() override;
	virtual void AdvanceEffect(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** Damages, knocks down and shakes. */
	void ApplyBlast();

	/** The scorch decal on the ground under the blast, if there is ground within ScorchDropCm. */
	void LeaveScorch();

	/** How far below the impact the scorch looks for ground, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Explosive", meta = (ClampMin = "0.0"))
	float ScorchDropCm = 250.f;

	void UpdateFireball();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive")
	TObjectPtr<UStaticMeshComponent> Fireball;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive")
	TObjectPtr<UStaticMeshComponent> FireballCore;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Explosive")
	TObjectPtr<UPointLightComponent> Flash;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FireballMaterial;

	float FlashScale = 1.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CoreMaterial;

	int32 VictimCount = 0;
};
