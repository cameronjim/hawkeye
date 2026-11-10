#pragma once

#include "CoreMinimal.h"
#include "Audio/HawkeyeAudioTypes.h"

struct FHawkeyeSettings;

/**
 * The audio rules that are numbers, kept apart from anything that plays a sound so the tests can
 * check them with no audio device (claude-docs/gameplay-semantics.md, "Audio").
 */
namespace HawkeyeAudioMath
{
	/** A 0..1 slider to a linear gain: squared, so the middle of the slider sounds like half. */
	HAWKEYE_API float SliderToGain(float Slider);

	/** Per-class gains from the four sliders, each already multiplied by the master. */
	HAWKEYE_API FHawkeyeClassVolumes ComputeClassVolumes(const FHawkeyeSettings& Settings);

	/** Below this height above the street the rooftop wind is at its floor. */
	static constexpr float WindLowHeightCm = 300.f;

	/** From this height up the wind is at full and the street is at its floor. */
	static constexpr float WindHighHeightCm = 1000.f;

	/**
	 * The ambience crossfade for HeightCm above the street: wind 0.15 to 1, street 1 to 0.35,
	 * eased between WindLowHeightCm and WindHighHeightCm.
	 */
	HAWKEYE_API void ComputeAmbienceMix(float HeightCm, float& OutWind, float& OutStreet);

	/**
	 * Splits the street level between the night bed (MS_Amb_Street) and the day bed (MS_Amb_StreetDay) by the
	 * time of day's DayBedWeight (0 night, 1 day): night = street x (1 - w), day = street x w.
	 */
	HAWKEYE_API void ComputeStreetBeds(float StreetVolume, float DayBedWeight, float& OutNight, float& OutDay);

	/** A change of the day bed weight fades over this, s, instead of the ambience's usual FadeSeconds. */
	static constexpr float BedCrossfadeSeconds = 2.f;

	/** Drops shorter than this make no landing sound (a kerb, a step). */
	static constexpr float MinLandingSoundHeightCm = 40.f;

	/** 0 for no sound, else 0.1..1 rising with the fall to full at 600 cm. */
	HAWKEYE_API float ComputeLandingIntensity(float FallHeightCm);

	/** Flesh for anything with health, wood by the hit actor's name, stone for everything else. */
	HAWKEYE_API EHawkeyeArrowSurface ClassifyArrowSurface(bool bHasHealth, const FString& ActorName);

	/** The floor tag for a wooden floor (boards, stair treads, a stage). */
	HAWKEYE_API extern const FName WoodSurfaceTag;

	/** The floor tag for carpet. */
	HAWKEYE_API extern const FName CarpetSurfaceTag;

	/** Wood or carpet by the floor actor's tags (carpet wins if both), snow for anything else or no floor. */
	HAWKEYE_API EHawkeyeFootstepSurface ClassifyFootstepSurface(const TArray<FName>& FloorTags);
}
