#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponDefinition.generated.h"

/**
 * Everything that makes one weapon different from another: damage, ammo, fire rate, spread
 * and melee reach. A new weapon is a new data asset, never a new class. The player's bow and
 * arrows are UBowDefinition and UArrowDefinition; this is left for Hands (the melee fallback)
 * and the thugs' hitscan.
 *
 * Create via Content Browser > Miscellaneous > Data Asset > WeaponDefinition, or let
 * Tools/Editor/create_weapon_data.py make DA_Weapon_Hands.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Player-facing name ("Fists"). Shown in the inventory screen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FText DisplayName;

	/** Short label for the hotbar box, where there is room for about eight characters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FText ShortName;

	/** Damage per hit before the headshot multiplier. For a melee weapon this is the punch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0"))
	float Damage = 34.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	int32 MagazineSize = 12;

	/** Spare rounds carried when this weapon is first picked up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	int32 DefaultReserve = 24;

	/** Rounds per minute. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1.0"))
	float FireRate = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.0"))
	float ReloadSeconds = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Spread", meta = (ClampMin = "0.0"))
	float HipSpreadDegrees = 2.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Spread", meta = (ClampMin = "0.0"))
	float AimSpreadDegrees = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1.0"))
	float HeadshotMultiplier = 3.f;

	/** Bones that count as a head. Matches the default UE5 skeleton naming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSet<FName> HeadBoneNames;

	// --- Melee ----------------------------------------------------------------------------------

	/** True for Hands: Fire() punches instead of tracing a bullet, and there is no ammo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	bool bIsMelee = false;

	/** Reach of the punch sweep, in centimetres. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee", meta = (ClampMin = "0.0"))
	float MeleeRange = 120.f;

	/** Seconds between punches. Slower than the fire rate on purpose: fists are not a gun. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee", meta = (ClampMin = "0.0"))
	float MeleeCooldown = 0.6f;

	/** Melee hits above UHealthComponent's stagger threshold fire OnStaggered on the victim. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Melee")
	bool bStaggerOnHit = true;

	UWeaponDefinition();

	/** DisplayName, falling back to the asset name so an unnamed definition still reads. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FText GetDisplayNameOrAssetName() const;

	/** ShortName, falling back to DisplayName and then to the asset name. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FText GetShortNameOrDisplayName() const;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
