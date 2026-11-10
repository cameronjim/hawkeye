#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BowDefinition.generated.h"

class UStaticMesh;

/**
 * One bow (claude-docs/gameplay-semantics.md, "bow and arrows"). Draw is a hold from 0 to
 * FullDrawSeconds; the fraction drawn sets the arrow's speed, damage and spread, and a release
 * within PerfectWindowSeconds of reaching full draw adds PerfectBonus damage. Every rule is a pure
 * function here so the numbers are testable without a world.
 *
 * DA_Bow_Kate (0.8 s) and DA_Bow_Clint (1.0 s) are made by Tools/Editor/create_weapon_data.py.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UBowDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UBowDefinition();

	/** Player-facing name ("Kate's recurve"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow")
	FText DisplayName;

	/** Seconds of holding to reach full draw. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Draw", meta = (ClampMin = "0.05"))
	float FullDrawSeconds = 0.8f;

	/** A release below this fraction of full draw cancels: no arrow leaves and none is spent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Draw", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDrawFraction = 0.25f;

	/** Arrow speed at full draw, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Power", meta = (ClampMin = "1.0"))
	float MaxSpeed = 6000.f;

	/** Speed at zero draw as a fraction of MaxSpeed; it rises linearly to 1 at full draw. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinSpeedFraction = 0.4f;

	/** Damage at zero draw as a fraction of the arrow's; the semantics doc's "40% to 100%". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinDamageFraction = 0.4f;

	/** Cone half-angle at full draw, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Spread", meta = (ClampMin = "0.0"))
	float MinSpread = 0.5f;

	/** Cone half-angle at zero draw, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Spread", meta = (ClampMin = "0.0"))
	float MaxSpread = 4.f;

	/** A release this soon after reaching full draw is perfect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Draw", meta = (ClampMin = "0.0"))
	float PerfectWindowSeconds = 0.1f;

	/** Extra damage for a perfect release, as a fraction (0.25 is +25%). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Draw", meta = (ClampMin = "0.0"))
	float PerfectBonus = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Damage", meta = (ClampMin = "1.0"))
	float HeadshotMultiplier = 3.f;

	/** Bones that count as a head. Matches the UE5 mannequin skeleton. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Damage")
	TSet<FName> HeadBoneNames;

	/** The bow the character carries. Its +X is where the arrow goes, +Z up the limbs, grip at the origin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Visual")
	TSoftObjectPtr<UStaticMesh> BowMesh;

	/** Socket (or bone) on the character mesh the grip sits in while drawn. palm_l_Socket on the UEFN mannequin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow|Visual")
	FName HandSocket = FName(TEXT("hand_l"));

	/** 0..1 drawn after holding for ElapsedSeconds. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float ComputeDrawFraction(float ElapsedSeconds) const;

	/** True when a release at DrawFraction would cancel rather than fire. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsBelowMinDraw(float DrawFraction) const { return DrawFraction < MinDrawFraction; }

	/** MaxSpeed * lerp(MinSpeedFraction, 1, DrawFraction). */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float ComputeSpeed(float DrawFraction) const;

	/** lerp(MaxSpread, MinSpread, DrawFraction), degrees. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float ComputeSpread(float DrawFraction) const;

	/** True when ElapsedSeconds is from full draw to PerfectWindowSeconds after it. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsPerfectRelease(float ElapsedSeconds) const;

	/** ArrowDamage scaled by draw (MinDamageFraction to 1), plus PerfectBonus when perfect. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float ComputeReleaseDamage(float ArrowDamage, float DrawFraction, bool bPerfect) const;

	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsHeadBone(FName BoneName) const { return HeadBoneNames.Contains(BoneName); }

	/** Damage times HeadshotMultiplier when BoneName is a head bone, unchanged otherwise. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float ApplyHeadshot(float Damage, FName BoneName) const;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
