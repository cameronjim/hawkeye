#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArrowEffectsSubsystem.generated.h"

class ASmokeCloud;
class UAudioComponent;
class ULightComponent;
class UMaterialInstanceDynamic;

/**
 * The world-wide side of the trick arrows: which smoke clouds are up (the thugs' sight check asks
 * here) and which street lamps an EMP has put out and until when. A subsystem rather than a
 * lookup from every thug every think, and it owns the lamp clock so two overlapping pulses extend
 * an outage instead of the first one switching the lamp back on under the second.
 */
UCLASS()
class HAWKEYE_API UArrowEffectsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem for WorldContext's world, or null. */
	static UArrowEffectsSubsystem* Get(const UObject* WorldContext);

	/** Tag the generator puts on every street lamp's light, pole and head. */
	static const FName LampTag;

	// --- Smoke ------------------------------------------------------------------------------------

	void RegisterSmoke(ASmokeCloud* Cloud);
	void UnregisterSmoke(ASmokeCloud* Cloud);

	/** True when the segment From..To passes through (or starts or ends inside) any live cloud. */
	UFUNCTION(BlueprintPure, Category = "Arrow Effects")
	bool IsSightBlocked(const FVector& From, const FVector& To) const;

	/** True when Point is inside a live cloud. */
	UFUNCTION(BlueprintPure, Category = "Arrow Effects")
	bool IsInsideSmoke(const FVector& Point) const;

	UFUNCTION(BlueprintPure, Category = "Arrow Effects")
	int32 GetSmokeCount() const { return Clouds.Num(); }

	// --- Lamps ------------------------------------------------------------------------------------

	/** How far above or below the pulse a lamp can be and still be reached, cm. */
	static constexpr float LampReachHeight = 1200.f;

	/**
	 * Puts out every actor tagged LampTag within Radius of Centre across the ground (and within
	 * LampReachHeight of it vertically, since a lamp head is 7 m up) for Seconds: its lights are
	 * hidden and any Intensity parameter on its meshes (the lamp head's glow) goes to 0. A lamp
	 * already out has its outage extended. Returns how many lamp actors were affected.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arrow Effects")
	int32 DisableLampsInRadius(const FVector& Centre, float Radius, float Seconds);

	/** True while Lamp is out. */
	UFUNCTION(BlueprintPure, Category = "Arrow Effects")
	bool IsLampDisabled(const AActor* Lamp) const;

	UFUNCTION(BlueprintPure, Category = "Arrow Effects")
	int32 GetDisabledLampCount() const { return Lamps.Num(); }

	/** Seconds on this subsystem's own clock. Advanced by Tick. */
	double GetClockSeconds() const { return Clock; }

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

	/** Pure: does the segment A..B come within Radius of Centre? */
	static bool SegmentIntersectsSphere(const FVector& A, const FVector& B, const FVector& Centre, float Radius);

protected:
	struct FLampOutage
	{
		TWeakObjectPtr<AActor> Lamp;
		double OnAgainAt = 0.0;
		TArray<TWeakObjectPtr<ULightComponent>> Lights;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Glows;
		TArray<float> GlowIntensities;
		/** The lamp's buzz (the City_LampBuzz_ sounds carry the lamp tag), silenced with the light. */
		TArray<TWeakObjectPtr<UAudioComponent>> Sounds;
	};

	/** Hides the lamp's lights and dims its glow, remembering what to put back. */
	void SwitchOff(AActor* Lamp, FLampOutage& Outage) const;

	/** Puts back what SwitchOff took. */
	void SwitchOn(FLampOutage& Outage) const;

	TArray<TWeakObjectPtr<ASmokeCloud>> Clouds;
	TArray<FLampOutage> Lamps;
	double Clock = 0.0;
};
