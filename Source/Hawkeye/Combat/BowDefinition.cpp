#include "Combat/BowDefinition.h"

UBowDefinition::UBowDefinition()
{
	HeadBoneNames.Add(FName(TEXT("head")));
	HeadBoneNames.Add(FName(TEXT("neck_01")));
}

float UBowDefinition::ComputeDrawFraction(float ElapsedSeconds) const
{
	return FullDrawSeconds > 0.f ? FMath::Clamp(ElapsedSeconds / FullDrawSeconds, 0.f, 1.f) : 1.f;
}

float UBowDefinition::ComputeSpeed(float DrawFraction) const
{
	return MaxSpeed * FMath::Lerp(MinSpeedFraction, 1.f, FMath::Clamp(DrawFraction, 0.f, 1.f));
}

float UBowDefinition::ComputeSpread(float DrawFraction) const
{
	return FMath::Lerp(MaxSpread, MinSpread, FMath::Clamp(DrawFraction, 0.f, 1.f));
}

bool UBowDefinition::IsPerfectRelease(float ElapsedSeconds) const
{
	// Small tolerance so a release on exactly the last frame of the window still counts.
	return ElapsedSeconds >= FullDrawSeconds && ElapsedSeconds <= FullDrawSeconds + PerfectWindowSeconds + KINDA_SMALL_NUMBER;
}

float UBowDefinition::ComputeReleaseDamage(float ArrowDamage, float DrawFraction, bool bPerfect) const
{
	const float Scaled = ArrowDamage * FMath::Lerp(MinDamageFraction, 1.f, FMath::Clamp(DrawFraction, 0.f, 1.f));
	return bPerfect ? Scaled * (1.f + PerfectBonus) : Scaled;
}

float UBowDefinition::ApplyHeadshot(float Damage, FName BoneName) const
{
	return IsHeadBone(BoneName) ? Damage * HeadshotMultiplier : Damage;
}

FPrimaryAssetId UBowDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Bow"), GetFName());
}
