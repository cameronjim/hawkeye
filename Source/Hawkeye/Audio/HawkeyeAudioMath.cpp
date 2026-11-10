#include "Audio/HawkeyeAudioMath.h"

#include "Settings/HawkeyeSettings.h"

bool FHawkeyeFootstepTracker::Advance(float DistanceCm, bool bSprinting, bool bGrounded)
{
	if (!bGrounded || DistanceCm > MaxStepPerFrameCm)
	{
		Accumulated = 0.f;
		return false;
	}
	Accumulated += FMath::Max(DistanceCm, 0.f);
	const float Stride = FMath::Max(bSprinting ? SprintStrideCm : WalkStrideCm, 1.f);
	if (Accumulated + KINDA_SMALL_NUMBER < Stride)
	{
		return false;
	}
	// Carry what is left over, but never more than a stride: a hitch should not queue a run of steps.
	Accumulated = FMath::Min(Accumulated - Stride, Stride);
	return true;
}

float HawkeyeAudioMath::SliderToGain(float Slider)
{
	const float Clamped = FMath::Clamp(Slider, 0.f, 1.f);
	return Clamped * Clamped;
}

FHawkeyeClassVolumes HawkeyeAudioMath::ComputeClassVolumes(const FHawkeyeSettings& Settings)
{
	FHawkeyeClassVolumes Volumes;
	Volumes.Master = SliderToGain(Settings.MasterVolume);
	Volumes.Sfx = Volumes.Master * SliderToGain(Settings.SfxVolume);
	Volumes.Ambient = Volumes.Master * SliderToGain(Settings.AmbientVolume);
	Volumes.UI = Volumes.Sfx;
	Volumes.Music = Volumes.Master * SliderToGain(Settings.MusicVolume);
	return Volumes;
}

void HawkeyeAudioMath::ComputeAmbienceMix(float HeightCm, float& OutWind, float& OutStreet)
{
	const float Alpha = FMath::SmoothStep(WindLowHeightCm, WindHighHeightCm, HeightCm);
	OutWind = FMath::Lerp(0.15f, 1.f, Alpha);
	OutStreet = FMath::Lerp(1.f, 0.35f, Alpha);
}

void HawkeyeAudioMath::ComputeStreetBeds(float StreetVolume, float DayBedWeight, float& OutNight, float& OutDay)
{
	const float Weight = FMath::Clamp(DayBedWeight, 0.f, 1.f);
	OutNight = StreetVolume * (1.f - Weight);
	OutDay = StreetVolume * Weight;
}

float HawkeyeAudioMath::ComputeLandingIntensity(float FallHeightCm)
{
	if (FallHeightCm < MinLandingSoundHeightCm)
	{
		return 0.f;
	}
	return FMath::Clamp((FallHeightCm - MinLandingSoundHeightCm) / (600.f - MinLandingSoundHeightCm), 0.1f, 1.f);
}

EHawkeyeArrowSurface HawkeyeAudioMath::ClassifyArrowSurface(bool bHasHealth, const FString& ActorName)
{
	if (bHasHealth)
	{
		return EHawkeyeArrowSurface::Flesh;
	}
	// The generated city has no physical materials; the few wooden things are named for what they are.
	static const TCHAR* const WoodWords[] = {
		TEXT("Wood"), TEXT("WaterTower"), TEXT("Door"), TEXT("Pallet"), TEXT("Crate"), TEXT("Bench"), TEXT("Tree"),
		TEXT("Scaffold"),
	};
	for (const TCHAR* Word : WoodWords)
	{
		if (ActorName.Contains(Word, ESearchCase::IgnoreCase))
		{
			return EHawkeyeArrowSurface::Wood;
		}
	}
	return EHawkeyeArrowSurface::Stone;
}

const FName HawkeyeAudioMath::WoodSurfaceTag(TEXT("SurfaceWood"));
const FName HawkeyeAudioMath::CarpetSurfaceTag(TEXT("SurfaceCarpet"));

EHawkeyeFootstepSurface HawkeyeAudioMath::ClassifyFootstepSurface(const TArray<FName>& FloorTags)
{
	if (FloorTags.Contains(CarpetSurfaceTag))
	{
		return EHawkeyeFootstepSurface::Carpet;
	}
	return FloorTags.Contains(WoodSurfaceTag) ? EHawkeyeFootstepSurface::Wood : EHawkeyeFootstepSurface::Snow;
}
