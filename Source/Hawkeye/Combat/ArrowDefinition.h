#pragma once

#include "CoreMinimal.h"
#include "Combat/ReturnFlight.h"
#include "Engine/DataAsset.h"
#include "ArrowDefinition.generated.h"

class AArrowEffect;
class AArrowProjectile;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class UTexture2D;

/**
 * What an arrow does when it lands, beyond its damage (claude-docs/gameplay-semantics.md, "trick
 * arrows"). Every value but None and Grapple spawns an AArrowEffect at the impact.
 */
UENUM(BlueprintType)
enum class EArrowHitEffect : uint8
{
	None,
	/** Replaced by the grapple flow: flies at the marked anchor and starts the zip. */
	Grapple,
	Putty,
	Bola,
	Smoke,
	EMP,
	Explosive
};

/**
 * Number of quiver slots, in the fixed order standard, grapple, putty, bola, smoke, EMP, explosive.
 * Number keys 1..6 select the first six; explosive (7) is reached by the wheel or the radial.
 */
static constexpr int32 HawkeyeQuiverSlotCount = 7;

/** Slots with a number key of their own. */
static constexpr int32 HawkeyeQuiverKeyedSlotCount = 6;

/**
 * One arrow type (claude-docs/gameplay-semantics.md, "bow and arrows"): the projectile it fires,
 * what it does on hit, its damage, how many the quiver holds and whether it can be pulled back out
 * of whatever it stuck in. A new trick arrow is a new asset (DA_Arrow_<Type>) plus, only when it
 * behaves differently in flight, a BP_Arrow_<Type> projectile.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UArrowDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Player-facing name ("Standard arrow"). Shown in the inventory screen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	FText DisplayName;

	/** Hotbar label, about eight characters. Falls back to DisplayName. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	FText ShortName;

	/** Quiver slot, 1..7; 1..6 are also the number keys. Standard arrows are always slot 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "1", ClampMax = "7"))
	int32 Slot = 1;

	/** What the bow spawns. BP_Arrow_Standard for most; BP_Arrow_Grapple flies straight at an anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	TSubclassOf<AArrowProjectile> ProjectileClass;

	/** Damage at full draw before the headshot multiplier and the perfect-release bonus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "0.0"))
	float Damage = 40.f;

	/** Most the quiver carries. Pickups and recovery never push the count past it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "1"))
	int32 Cap = 30;

	/** Stuck arrows of this type go back in the quiver when the player walks within reach. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	bool bRecoverable = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	EArrowHitEffect OnHitEffect = EArrowHitEffect::None;

	/**
	 * The actor spawned where the arrow lands. Empty uses the stock class for OnHitEffect
	 * (AHeldEffect, ABolaEffect, ASmokeCloud, AEmpPulse, AExplosiveBlast); a Blueprint child of
	 * one of those retunes it without code.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	TSubclassOf<AArrowEffect> EffectClass;

	/** What the effect sounds like as it goes off (MS_Trick_Putty, _Bola, _Smoke, _Emp, _Explosion). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Audio")
	TSoftObjectPtr<USoundBase> EffectSound;

	/**
	 * The sound rides on the effect and stops with it (the smoke's hiss loops for the cloud's life).
	 * Off, it plays once at the impact and outlives the effect actor.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Audio")
	bool bEffectSoundFollowsEffect = false;

	/**
	 * What the effect looks like as it goes off, spawned where it lands (NS_SmokeCloud, NS_EmpPulse,
	 * NS_Explosion, NS_PuttySplat). When it is set the effect's placeholder shapes stay hidden.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Effects")
	TSoftObjectPtr<UNiagaraSystem> EffectVfx;

	/** Rides on the arrow in flight (NS_BolaTrail). Empty: the bow's plain arrow trail. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Effects")
	TSoftObjectPtr<UNiagaraSystem> FlightVfx;

	/** A decal left on the ground under the effect (M_Decal_Scorch for the explosive). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Effects")
	TSoftObjectPtr<UMaterialInterface> GroundDecal;

	/** Radius of GroundDecal, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Effects", meta = (ClampMin = "1.0"))
	float GroundDecalRadius = 170.f;

	/** How long GroundDecal stays, fading over its last quarter, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Effects", meta = (ClampMin = "0.0"))
	float GroundDecalSeconds = 30.f;

	/**
	 * How it flies when its ProjectileClass is AReturningArrowProjectile (the boomerang): out, through up to MaxTargets,
	 * back along its path to her. Unused by any other projectile.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Return")
	FHawkeyeReturnFlight ReturnFlight;

	/** Hotbar icon. TODO(stage3): unused until icons are drawn; the hotbar shows ShortName. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	TSoftObjectPtr<UTexture2D> Icon;

	/**
	 * What a stuck arrow of this type goes back into the quiver as. Empty: this type. Trickshot's
	 * archers shoot DA_Arrow_Trickshot, which Kate picks up as DA_Arrow_Standard.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Recovery")
	TObjectPtr<UArrowDefinition> RecoverAs;

	/** HUD toast the first time the player picks one up ("Trickshot's arrow"). Empty: none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Recovery")
	FText PickupToast;

	/** Paints the projectile in the colours below instead of the projectile class's own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Look")
	bool bOverrideColors = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Look", meta = (EditCondition = "bOverrideColors"))
	FLinearColor ShaftColor = FLinearColor(0.6f, 0.55f, 0.45f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Look", meta = (EditCondition = "bOverrideColors"))
	FLinearColor FletchingColor = FLinearColor(0.45f, 0.1f, 0.75f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow|Look", meta = (EditCondition = "bOverrideColors"))
	FLinearColor NockColor = FLinearColor(0.45f, 0.1f, 0.75f);

	/** RecoverAs, or this type when it is empty. */
	UFUNCTION(BlueprintPure, Category = "Arrow|Recovery")
	UArrowDefinition* GetRecoveredType() { return RecoverAs ? RecoverAs.Get() : this; }

	/** DisplayName, falling back to the asset name. */
	UFUNCTION(BlueprintPure, Category = "Arrow")
	FText GetDisplayNameOrAssetName() const;

	/** ShortName, then DisplayName, then the asset name. */
	UFUNCTION(BlueprintPure, Category = "Arrow")
	FText GetShortNameOrDisplayName() const;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};

/** An arrow type and a count: what a mission grants at the start. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeArrowGrant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arrow")
	TSoftObjectPtr<UArrowDefinition> Arrow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "0"))
	int32 Count = 0;
};
