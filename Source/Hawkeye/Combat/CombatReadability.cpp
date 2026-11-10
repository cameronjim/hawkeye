#include "Combat/CombatReadability.h"

#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

void FHawkeyeTargetMarker::Mark(AActor* InTarget)
{
	Target = InTarget;
	FadeElapsed = 0.f;
	bFading = false;
}

void FHawkeyeTargetMarker::Advance(float DeltaSeconds, bool bSwingActive)
{
	if (!Target.IsValid())
	{
		Clear();
		return;
	}
	if (const AThugCharacter* Thug = Cast<AThugCharacter>(Target.Get()); Thug && Thug->IsLimp())
	{
		Clear();
		return;
	}
	if (bSwingActive && !bFading)
	{
		return;
	}
	// Once the swing is over the fade runs to the end: a later swing at him marks him afresh.
	bFading = true;
	FadeElapsed += FMath::Max(DeltaSeconds, 0.f);
	if (FadeElapsed >= FadeSeconds)
	{
		Clear();
	}
}

void FHawkeyeTargetMarker::Clear()
{
	Target = nullptr;
	FadeElapsed = 0.f;
	bFading = false;
}

float FHawkeyeTargetMarker::GetAlpha() const
{
	if (!Target.IsValid())
	{
		return 0.f;
	}
	if (!bFading)
	{
		return 1.f;
	}
	return FadeSeconds > 0.f ? FMath::Clamp(1.f - FadeElapsed / FadeSeconds, 0.f, 1.f) : 0.f;
}

namespace HawkeyeCombatReadability
{
	int32 CountEngaged(const FVector& Origin, const TArray<FVector>& Positions, float Radius, float MaxHeight, float FarRadius)
	{
		int32 Near = 0;
		int32 Far = 0;
		for (const FVector& At : Positions)
		{
			if (FMath::Abs(At.Z - Origin.Z) > MaxHeight)
			{
				continue;
			}
			const float Flat = FVector::Dist2D(At, Origin);
			Near += Flat <= Radius ? 1 : 0;
			Far += Flat <= FMath::Max(Radius, FarRadius) ? 1 : 0;
		}
		// Someone has to be close for the far ones to count.
		return Near > 0 ? Far : 0;
	}

	bool IsFight(int32 Engaged, const FHawkeyeFightCameraSettings& Settings)
	{
		return Engaged >= FMath::Max(Settings.MinThugs, 1);
	}

	float AdvanceFightAlpha(float Alpha, bool bFight, float DeltaSeconds, const FHawkeyeFightCameraSettings& Settings)
	{
		const float Seconds = bFight ? Settings.BlendInSeconds : Settings.BlendOutSeconds;
		const float Target = bFight ? 1.f : 0.f;
		if (Seconds <= 0.f)
		{
			return Target;
		}
		return FMath::FInterpConstantTo(FMath::Clamp(Alpha, 0.f, 1.f), Target, FMath::Max(DeltaSeconds, 0.f), 1.f / Seconds);
	}

	void ComputeFightOffsets(float Alpha, float AimAlpha, float PushAlpha, const FHawkeyeFightCameraSettings& Settings,
		float& OutExtraArm, float& OutPitchDrop)
	{
		const float Weight = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Alpha, 0.f, 1.f))
			* (1.f - FMath::Clamp(AimAlpha, 0.f, 1.f)) * (1.f - FMath::Clamp(PushAlpha, 0.f, 1.f));
		OutExtraArm = Settings.ExtraArmLength * Weight;
		OutPitchDrop = Settings.PitchDrop * Weight;
	}

	float ComputeCameraPunch(float Elapsed, float Seconds, float Distance, float ShakeScale)
	{
		if (Seconds <= 0.f || Elapsed < 0.f || Elapsed > Seconds)
		{
			return 0.f;
		}
		return Distance * FMath::Max(ShakeScale, 0.f) * FMath::Sin(PI * Elapsed / Seconds);
	}

	FHawkeyeTelegraphLook ComputeTelegraphLook(float Elapsed, float WindupSeconds, float FlashScale, float PulseHz, float PulseAmount)
	{
		FHawkeyeTelegraphLook Look;
		const float Progress = WindupSeconds > 0.f ? FMath::Clamp(Elapsed / WindupSeconds, 0.f, 1.f) : 1.f;
		// A beat that starts at its rest (sin from 0), so the glyph pops as it appears and on every beat after.
		const float Beat = 0.5f + 0.5f * FMath::Sin(2.f * PI * PulseHz * FMath::Max(Elapsed, 0.f) - 0.5f * PI);
		const float Pulse = PulseAmount * FMath::Clamp(FlashScale, 0.f, 1.f) * Beat;
		Look.Scale = 1.f + 0.3f * Progress + Pulse;
		Look.Alpha = 1.f - 0.5f * Pulse;
		return Look;
	}

	float ComputeGlyphBottom(float AnchorY, bool bBarShown, float BarHeight, float Gap)
	{
		return bBarShown ? AnchorY - FMath::Max(BarHeight, 0.f) - FMath::Max(Gap, 0.f) : AnchorY;
	}

	bool GetStrikeTelegraph(const AThugCharacter* Thug, const FVector& Viewer, float GunnerRange, float& OutElapsed,
		float& OutWindupSeconds)
	{
		OutElapsed = -1.f;
		OutWindupSeconds = 0.f;
		const EHawkeyeParryKind Kind = UHawkeyeMeleeRules::ClassifyParry(Thug);
		if (Kind == EHawkeyeParryKind::None)
		{
			return false;
		}
		if (Kind == EHawkeyeParryKind::Burst)
		{
			const AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController());
			if (!Brain || FVector::Dist2D(Thug->GetActorLocation(), Viewer) > GunnerRange)
			{
				return false;
			}
			OutWindupSeconds = Brain->Burst.GetTelegraphSeconds();
		}
		else
		{
			OutWindupSeconds = Thug->GetMeleeComponent()->GetCurrentAttack().WindupSeconds;
		}
		OutElapsed = UHawkeyeMeleeRules::GetTelegraphElapsed(Thug);
		return OutElapsed >= 0.f;
	}

	bool GetUnblockableTelegraph(const AThugCharacter* Thug, float& OutElapsed, float& OutWindupSeconds)
	{
		OutElapsed = -1.f;
		OutWindupSeconds = 0.f;
		if (!UHawkeyeMeleeRules::IsUnblockableWindup(Thug))
		{
			return false;
		}
		OutWindupSeconds = Thug->GetMeleeComponent()->GetCurrentAttack().WindupSeconds;
		OutElapsed = UHawkeyeMeleeRules::GetTelegraphElapsed(Thug);
		return OutElapsed >= 0.f;
	}

	FLinearColor ComputeUnblockableColor(const FLinearColor& Base, float Elapsed, float FlashScale, float PulseHz)
	{
		// A square flash, not a pulse: on the beat it is white, between beats its colour.
		const bool bFlash = FMath::Frac(FMath::Max(Elapsed, 0.f) * FMath::Max(PulseHz, 0.f)) < 0.5f;
		const float Amount = bFlash ? FMath::Clamp(FlashScale, 0.f, 1.f) : 0.f;
		FLinearColor Out = FMath::Lerp(Base, FLinearColor::White, Amount);
		Out.A = Base.A;
		return Out;
	}
}
