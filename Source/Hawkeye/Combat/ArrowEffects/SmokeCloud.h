#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "SmokeCloud.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * The smoke arrow's cloud: a Radius sphere that stands for LifeSeconds. It registers with
 * UArrowEffectsSubsystem, and a thug whose line of sight to the player passes through it (or who
 * stands in it) is blinded; Kate crouched inside it cannot be detected at all.
 *
 * The look is the definition's EffectVfx (NS_SmokeCloud: soft lit sprites billowing out at once,
 * then a stream for six seconds, drifting). Without it the old placeholder stands in: a cluster of
 * grey translucent puffs (M_ArrowFx) that swell over GrowSeconds and thin out over FadeSeconds.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API ASmokeCloud : public AArrowEffect
{
	GENERATED_BODY()

public:
	ASmokeCloud();

	/** Full radius of the cloud, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Smoke", meta = (ClampMin = "0.0"))
	float Radius = 500.f;

	/** How long the cloud stands, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Smoke", meta = (ClampMin = "0.0"))
	float LifeSeconds = 8.f;

	/** How long it takes to billow out to Radius, s. It blinds from the first frame at its current size. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Smoke", meta = (ClampMin = "0.01"))
	float GrowSeconds = 0.6f;

	/** The last part of LifeSeconds over which the puffs thin out; it stops blinding at the end. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Smoke", meta = (ClampMin = "0.0"))
	float FadeSeconds = 1.5f;

	/** How much the cloud's centre sits above the impact, so it fills a person's height, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Smoke")
	float CentreLift = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Smoke")
	FLinearColor SmokeColor = FLinearColor(0.3f, 0.31f, 0.34f);

	/** Opacity of each puff at full strength. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Smoke", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PuffOpacity = 0.7f;

	/** Where the cloud is centred. */
	UFUNCTION(BlueprintPure, Category = "Smoke")
	FVector GetCloudCentre() const;

	/** The radius right now: growing to Radius over GrowSeconds, 0 once LifeSeconds are up. */
	UFUNCTION(BlueprintPure, Category = "Smoke")
	float GetCurrentRadius() const;

	/** True from Activate until LifeSeconds have passed. */
	UFUNCTION(BlueprintPure, Category = "Smoke")
	bool IsBlocking() const;

	virtual void Activate() override;
	virtual void AdvanceEffect(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Scales and fades the puffs for the current moment. */
	void UpdatePuffs();

	static constexpr int32 PuffCount = 9;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Smoke")
	TArray<TObjectPtr<UStaticMeshComponent>> Puffs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PuffMaterials;

	/** Each puff's offset from the centre as a fraction of Radius, and its size fraction. */
	TArray<FVector4f> PuffLayout;

	bool bRegistered = false;
};
