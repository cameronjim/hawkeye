#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "EmpPulse.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * The EMP arrow's pulse. Everything electric within Radius of the impact dies: street lamps
 * (actors tagged CityLamp) go dark for LampOffSeconds through UArrowEffectsSubsystem, and every
 * gunner's pistol jams for JamSeconds (AThugAIController::Jam). TODO(stage3): security cameras,
 * once there are any, go off the same way.
 *
 * The look is the definition's EffectVfx (NS_EmpPulse: a ring of blue light racing out to 600 cm,
 * sparks and arcs) with the blue flash light at the centre, and a brief chromatic split on the
 * screen of a player within ScreenPulseRadius. Without the system it falls back to the old ring of
 * glowing segments (M_ArrowGlow) racing out over RingSeconds.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AEmpPulse : public AArrowEffect
{
	GENERATED_BODY()

public:
	AEmpPulse();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP", meta = (ClampMin = "0.0"))
	float Radius = 600.f;

	/** How long lamps in the radius stay dark, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP", meta = (ClampMin = "0.0"))
	float LampOffSeconds = 20.f;

	/** How long a gunner in the radius cannot fire, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP", meta = (ClampMin = "0.0"))
	float JamSeconds = 6.f;

	/** How long the ring takes to reach Radius and fade, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP", meta = (ClampMin = "0.05"))
	float RingSeconds = 0.9f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP")
	FLinearColor PulseColor = FLinearColor(0.25f, 0.6f, 1.f);

	/** A player this close sees the pulse on her own screen (a brief chromatic split), cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP", meta = (ClampMin = "0.0"))
	float ScreenPulseRadius = 2000.f;

	/** How long the screen pulse lasts, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "EMP", meta = (ClampMin = "0.0"))
	float ScreenPulseSeconds = 0.45f;

	/** Lamp actors put out by this pulse. */
	UFUNCTION(BlueprintPure, Category = "EMP")
	int32 GetLampsAffected() const { return LampsAffected; }

	/** Gunners jammed by this pulse. */
	UFUNCTION(BlueprintPure, Category = "EMP")
	int32 GetGunnersJammed() const { return GunnersJammed; }

	virtual void Activate() override;
	virtual void AdvanceEffect(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** Jams every living gunner within Radius. Returns how many. */
	int32 JamGunners();

	/** Sizes and fades the ring and the flash for the current moment. */
	void UpdateRing();

	/** The screen pulse for every player within ScreenPulseRadius. */
	void PulseScreens() const;

	static constexpr int32 SegmentCount = 32;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMP")
	TArray<TObjectPtr<UStaticMeshComponent>> Segments;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMP")
	TObjectPtr<UPointLightComponent> Flash;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SegmentMaterials;

	int32 LampsAffected = 0;
	int32 GunnersJammed = 0;
};
