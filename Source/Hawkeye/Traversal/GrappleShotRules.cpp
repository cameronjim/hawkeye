#include "Traversal/GrappleShotRules.h"

FGrappleShotSchedule FGrappleShotRules::MakeSchedule(
	float Distance, float ArrowSpeed, bool bChain, const FHawkeyeGrappleShotSettings& Settings)
{
	FGrappleShotSchedule Out;
	const float Length = FMath::Max(Distance, 0.f);
	const float Extra = FMath::Max(Settings.ExtraLatencySeconds, 0.f);

	// The press-to-pull time the traversal already has: the arrow's flight, or a chain's instant arrival.
	float Total = 0.f;
	if (ArrowSpeed > KINDA_SMALL_NUMBER)
	{
		Total = Length / ArrowSpeed;
	}
	else if (bChain)
	{
		Total = FMath::Max(Settings.ChainTotalSeconds, 0.f);
	}
	Total += Extra;

	if (!Settings.bEnabled || Total <= KINDA_SMALL_NUMBER)
	{
		// The old shot: the arrow leaves with the press and flies the line.
		Out.TotalSeconds = Total;
		Out.FlightSeconds = Total;
		Out.ArrowSpeed = ArrowSpeed > KINDA_SMALL_NUMBER ? ArrowSpeed : (Total > KINDA_SMALL_NUMBER ? Length / Total : 0.f);
		return Out;
	}

	const float Wanted = FMath::Max(Settings.RaiseSeconds, 0.f) + FMath::Max(Settings.DrawSeconds, 0.f);
	const float Latest = FMath::Max(Total * Settings.ReleaseMaxShare, KINDA_SMALL_NUMBER);
	const float Earliest = FMath::Min(Settings.MinReleaseSeconds, Latest);
	Out.ReleaseSeconds = FMath::Clamp(Wanted, Earliest, Latest);
	Out.Compression = Wanted > KINDA_SMALL_NUMBER ? Out.ReleaseSeconds / Wanted : 1.f;
	const float RaiseShare = Wanted > KINDA_SMALL_NUMBER ? FMath::Max(Settings.RaiseSeconds, 0.f) / Wanted : 0.5f;
	Out.RaiseSeconds = Out.ReleaseSeconds * RaiseShare;
	Out.DrawSeconds = Out.ReleaseSeconds - Out.RaiseSeconds;
	Out.TotalSeconds = Total;
	Out.FlightSeconds = FMath::Max(Total - Out.ReleaseSeconds, KINDA_SMALL_NUMBER);
	Out.ArrowSpeed = Length / Out.FlightSeconds;
	return Out;
}

float FGrappleShotRules::DrawFractionAt(float Elapsed, const FGrappleShotSchedule& Schedule)
{
	if (Schedule.ReleaseSeconds <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	// The string comes back over the draw part of the clock; the raise is the arm, not the string.
	const float Start = Schedule.RaiseSeconds * 0.5f;
	return FMath::SmoothStep(Start, Schedule.ReleaseSeconds, Elapsed);
}

float FGrappleShotRules::RaiseFractionAt(float Elapsed, const FGrappleShotSchedule& Schedule)
{
	if (Schedule.RaiseSeconds <= KINDA_SMALL_NUMBER)
	{
		return Schedule.ReleaseSeconds > 0.f ? 1.f : 0.f;
	}
	return FMath::SmoothStep(0.f, Schedule.RaiseSeconds, Elapsed);
}

EGrappleShotPhase FGrappleShotRules::PhaseAt(float Elapsed, const FGrappleShotSchedule& Schedule, bool bArrived)
{
	if (bArrived)
	{
		return EGrappleShotPhase::Stuck;
	}
	if (Elapsed >= Schedule.ReleaseSeconds)
	{
		return EGrappleShotPhase::Flight;
	}
	return Elapsed < Schedule.RaiseSeconds ? EGrappleShotPhase::Raise : EGrappleShotPhase::Draw;
}

float FGrappleShotRules::BlurAmount(float Base, float Traversal, float Weight)
{
	return FMath::Lerp(Base, Traversal, FMath::Clamp(Weight, 0.f, 1.f));
}

float FGrappleShotRules::StepBlurWeight(float Weight, bool bTraversing, float DeltaSeconds, float InSeconds, float OutSeconds)
{
	const float Seconds = bTraversing ? InSeconds : OutSeconds;
	if (Seconds <= KINDA_SMALL_NUMBER)
	{
		return bTraversing ? 1.f : 0.f;
	}
	const float Step = FMath::Max(DeltaSeconds, 0.f) / Seconds;
	return FMath::Clamp(Weight + (bTraversing ? Step : -Step), 0.f, 1.f);
}
