#include "World/ThugAim.h"

#include "World/ThugAIController.h"

void FHawkeyeTargetTrack::AddSample(double Now, const FVector& Location, const FVector& Velocity, bool bAirborne, bool bDodging)
{
	if (Samples.Num() > 0 && Now - Samples.Last().Time < HawkeyeThugAim::SampleSeconds - KINDA_SMALL_NUMBER)
	{
		// A dodge seen between samples still counts.
		Samples.Last().bDodging |= bDodging;
		return;
	}
	if (Samples.Num() > 0 && Now < Samples.Last().Time)
	{
		// The clock went back (a new world, a test): start again.
		Samples.Reset();
	}
	FHawkeyeTargetSample& Sample = Samples.AddDefaulted_GetRef();
	Sample.Time = Now;
	Sample.Location = Location;
	Sample.Velocity = Velocity;
	Sample.bAirborne = bAirborne;
	Sample.bDodging = bDodging;
	int32 Drop = 0;
	while (Drop < Samples.Num() - 1 && Now - Samples[Drop].Time > HawkeyeThugAim::HistorySeconds)
	{
		++Drop;
	}
	if (Drop > 0)
	{
		Samples.RemoveAt(0, Drop, EAllowShrinking::No);
	}
}

bool FHawkeyeTargetTrack::GetLagged(double Now, float LagSeconds, FVector& OutLocation, FVector& OutVelocity) const
{
	if (Samples.Num() == 0)
	{
		return false;
	}
	const double Then = Now - FMath::Max(LagSeconds, 0.f);
	// Where she was then: between the two samples round it, or the oldest if the history is shorter.
	OutLocation = Samples[0].Location;
	for (int32 Index = Samples.Num() - 1; Index >= 0; --Index)
	{
		if (Samples[Index].Time <= Then)
		{
			if (Index + 1 < Samples.Num())
			{
				const FHawkeyeTargetSample& A = Samples[Index];
				const FHawkeyeTargetSample& B = Samples[Index + 1];
				const double Span = FMath::Max(B.Time - A.Time, 1e-4);
				OutLocation = FMath::Lerp(A.Location, B.Location, static_cast<float>(FMath::Clamp((Then - A.Time) / Span, 0.0, 1.0)));
			}
			else
			{
				OutLocation = Samples[Index].Location;
			}
			break;
		}
	}
	// Her velocity then: the average of what was seen over the window before it.
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const FHawkeyeTargetSample& Sample : Samples)
	{
		if (Sample.Time <= Then + KINDA_SMALL_NUMBER && Sample.Time >= Then - HawkeyeThugAim::VelocityWindowSeconds - KINDA_SMALL_NUMBER)
		{
			Sum += Sample.Velocity;
			++Count;
		}
	}
	if (Count == 0)
	{
		// Nothing that old yet: the oldest look is all he has.
		Sum = Samples[0].Velocity;
		Count = 1;
	}
	OutVelocity = Sum / Count;
	return true;
}

bool FHawkeyeTargetTrack::IsErratic(double Now) const
{
	const double Since = Now - HawkeyeThugAim::ErraticWindowSeconds;
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(HawkeyeThugAim::ErraticTurnDegrees));
	TArray<FVector, TInlineAllocator<16>> Headings;
	for (const FHawkeyeTargetSample& Sample : Samples)
	{
		if (Sample.Time < Since - KINDA_SMALL_NUMBER)
		{
			continue;
		}
		if (Sample.bDodging)
		{
			return true;
		}
		if (Sample.Velocity.Size2D() > HawkeyeThugAim::ErraticMinSpeed)
		{
			Headings.Add(Sample.Velocity.GetSafeNormal2D());
		}
	}
	for (int32 A = 0; A < Headings.Num(); ++A)
	{
		for (int32 B = A + 1; B < Headings.Num(); ++B)
		{
			if (FVector::DotProduct(Headings[A], Headings[B]) < MinDot)
			{
				return true;
			}
		}
	}
	return false;
}

float HawkeyeThugAim::ComputeConeDegrees(const FVector& From, const FVector& ChestSeen, const FVector& VelocitySeen,
	float BaseConeDegrees, float ConeScale, bool bAirborne)
{
	const FVector Line = ChestSeen - From;
	const float Distance = FMath::Max(Line.Size(), 100.f);
	const FVector Dir = Line.GetSafeNormal();
	// Her velocity across his line of sight.
	const FVector Across = VelocitySeen - Dir * FVector::DotProduct(VelocitySeen, Dir);
	const float Sweep = FMath::RadiansToDegrees(FMath::Atan(Across.Size() * LateralSweepSeconds / Distance));
	const float Cone = BaseConeDegrees + Sweep + (bAirborne ? AirborneConeDegrees : 0.f);
	return FMath::Max(Cone * FMath::Max(ConeScale, 0.f), 0.f);
}

FHawkeyeRangedShot HawkeyeThugAim::BuildShot(const FHawkeyeTargetTrack& Track, double Now, const FVector& From,
	const FVector& ChestNow, const FVector& VelocityNow, float TrackingLagSeconds, float ProjectileSpeed, float GravityZ,
	float BaseConeDegrees, float ConeScale, float TargetRadius)
{
	FHawkeyeRangedShot Shot;
	Shot.From = From;
	Shot.ChestNow = ChestNow;
	Shot.VelocityNow = VelocityNow;
	Shot.TrackingLagSeconds = TrackingLagSeconds;
	if (!Track.GetLagged(Now, TrackingLagSeconds, Shot.ChestSeen, Shot.VelocitySeen))
	{
		Shot.ChestSeen = ChestNow;
		Shot.VelocitySeen = VelocityNow;
	}
	Shot.ProjectileSpeed = ProjectileSpeed;
	Shot.GravityZ = GravityZ;
	Shot.BaseConeDegrees = BaseConeDegrees;
	Shot.ConeScale = ConeScale;
	const FHawkeyeTargetSample* Latest = Track.GetLatest();
	Shot.bAirborne = Latest && Latest->bAirborne;
	Shot.bErratic = Track.IsErratic(Now);
	Shot.TargetRadius = TargetRadius;
	return Shot;
}

FHawkeyeRangedAim HawkeyeThugAim::ComputeShot(const FHawkeyeRangedShot& Shot, FRandomStream& Stream)
{
	FHawkeyeRangedAim Out;
	const float DistanceNow = FVector::Dist(Shot.From, Shot.ChestNow);

	if (Shot.bErratic && DistanceNow > ErraticSafeDistance)
	{
		// She jinked: this one goes wide, on the side she came from (behind her across his line).
		const FVector Dir = (Shot.ChestNow - Shot.From).GetSafeNormal();
		FVector Across = Shot.VelocityNow - Dir * FVector::DotProduct(Shot.VelocityNow, Dir);
		Across.Z = 0.f;
		FVector Side = -Across.GetSafeNormal();
		if (Side.IsNearlyZero())
		{
			FVector Seen = Shot.ChestSeen - Shot.ChestNow;
			Seen.Z = 0.f;
			Side = Seen.GetSafeNormal();
		}
		if (Side.IsNearlyZero())
		{
			Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
		}
		const FVector Wide = Shot.ChestNow + Side * (Shot.TargetRadius + MissMargin);
		Out.AimPoint = Shot.ProjectileSpeed > 0.f
			? AThugAIController::ComputeLeadAimPoint(Shot.From, Wide, FVector::ZeroVector, Shot.ProjectileSpeed, Shot.GravityZ)
			: Wide;
		Out.bForcedMiss = true;
		return Out;
	}

	// What he believes: where she was, carried on at the speed she was going, over the lag.
	const FVector Believed = Shot.ChestSeen + Shot.VelocitySeen * Shot.TrackingLagSeconds;
	const FVector Aim = Shot.ProjectileSpeed > 0.f
		? AThugAIController::ComputeLeadAimPoint(Shot.From, Believed, Shot.VelocitySeen, Shot.ProjectileSpeed, Shot.GravityZ)
		: Believed;
	Out.ConeDegrees = ComputeConeDegrees(Shot.From, Shot.ChestSeen, Shot.VelocitySeen, Shot.BaseConeDegrees, Shot.ConeScale,
		Shot.bAirborne);
	const FVector Line = Aim - Shot.From;
	const FVector Scattered = Stream.VRandCone(Line.GetSafeNormal(), FMath::DegreesToRadians(Out.ConeDegrees));
	Out.AimPoint = Shot.From + Scattered * Line.Size();
	return Out;
}
