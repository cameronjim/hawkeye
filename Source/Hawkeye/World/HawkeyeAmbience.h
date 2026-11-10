#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HawkeyeAmbience.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * The district's sound bed (claude-docs/gameplay-semantics.md, "Audio"): rooftop wind and the street's
 * hum, non-spatial loops crossfaded by how high the listener's pawn stands above this actor (placed
 * at street level by generate_city.py as City_Ambience). Wind is at its floor under 3 m and full from
 * 10 m up; the street falls to a third up there. The fade is HawkeyeAudioMath::ComputeAmbienceMix. The street
 * level is split between a night bed and a day bed by the time of day's DayBedWeight
 * (HawkeyeAudioMath::ComputeStreetBeds), crossfading over BedCrossfadeSeconds when it changes.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AHawkeyeAmbience : public AActor
{
	GENERATED_BODY()

public:
	AHawkeyeAmbience();

	/** Gusting wind (MS_Amb_Wind). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	TSoftObjectPtr<USoundBase> WindSound;

	/** The night street: mains drone, traffic rumble and the odd far-off horn (MS_Amb_Street). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	TSoftObjectPtr<USoundBase> StreetSound;

	/**
	 * The day street: wind through the block, a brighter traffic hum, a far horn (MS_Amb_StreetDay). A C++
	 * default, so the generated map carries it without a rebuild.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	TSoftObjectPtr<USoundBase> DayStreetSound;

	/** How often the crossfade is recomputed, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience", meta = (ClampMin = "0.02"))
	float UpdateSeconds = 0.25f;

	/** Seconds each volume takes to reach a new level, so a zip up a building swells rather than steps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience", meta = (ClampMin = "0.0"))
	float FadeSeconds = 1.0f;

	/** The listener's height above this actor, cm, as last measured. */
	UFUNCTION(BlueprintPure, Category = "Ambience")
	float GetListenerHeight() const { return ListenerHeight; }

	/** The day bed weight the last update faded toward (0 night, 1 day). */
	UFUNCTION(BlueprintPure, Category = "Ambience")
	float GetDayBedWeight() const { return AppliedDayWeight; }

	/** The night and day street volumes the last update faded toward. */
	float GetNightStreetVolume() const { return NightStreetVolume; }
	float GetDayStreetVolume() const { return DayStreetVolume; }

	/** Measures the local pawn's height and fades every loop toward the mix for it. Tests call it directly. */
	void UpdateMix();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Wind = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Street = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> DayStreet = nullptr;

	/** Below zero until the first update: the first mix is set, not crossfaded. */
	float AppliedDayWeight = -1.f;
	float NightStreetVolume = 0.f;
	float DayStreetVolume = 0.f;

	float ListenerHeight = 0.f;
	float SinceUpdate = 0.f;

	/** The beds start on the first tick, after the first frame, not inside the map load. */
	bool bBedsStarted = false;
};
