#include "Chase/ChaseRules.h"

TArray<float> UChaseRules::Cumulative(const TArray<FVector>& Points)
{
	TArray<float> Out;
	Out.Reserve(Points.Num());
	float Sum = 0.f;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Index > 0)
		{
			Sum += FVector::Dist(Points[Index - 1], Points[Index]);
		}
		Out.Add(Sum);
	}
	return Out;
}

FVector UChaseRules::PointAt(const TArray<FVector>& Points, const TArray<float>& Distances, float Along, int32* OutSegment)
{
	if (Points.Num() == 0 || Distances.Num() != Points.Num())
	{
		return FVector::ZeroVector;
	}
	if (Points.Num() == 1 || Along <= 0.f)
	{
		if (OutSegment)
		{
			*OutSegment = 0;
		}
		return Points[0];
	}
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		if (Along <= Distances[Index])
		{
			const float Span = Distances[Index] - Distances[Index - 1];
			const float Alpha = Span > KINDA_SMALL_NUMBER ? (Along - Distances[Index - 1]) / Span : 1.f;
			if (OutSegment)
			{
				*OutSegment = Index - 1;
			}
			return FMath::Lerp(Points[Index - 1], Points[Index], Alpha);
		}
	}
	if (OutSegment)
	{
		*OutSegment = Points.Num() - 2;
	}
	return Points.Last();
}

bool UChaseRules::Project(const TArray<FVector>& Points, const TArray<float>& Distances, const FVector& Location,
	float PreviousAlong, const FChaseTuning& Tuning, float& OutAlong)
{
	OutAlong = PreviousAlong;
	if (Points.Num() < 2 || Distances.Num() != Points.Num())
	{
		return false;
	}
	const float From = PreviousAlong - Tuning.BackWindow;
	const float To = PreviousAlong + Tuning.AheadWindow;
	float Best = Tuning.OffRouteDistance;
	bool bFound = false;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const float A = Distances[Index - 1];
		const float B = Distances[Index];
		if (B < From || A > To)
		{
			continue;
		}
		const FVector Closest = FMath::ClosestPointOnSegment(Location, Points[Index - 1], Points[Index]);
		const float Along = FMath::Clamp(A + FVector::Dist(Points[Index - 1], Closest), From, To);
		const FVector OnWindow = PointAt(Points, Distances, Along);
		const float Distance = FVector::Dist(Location, OnWindow);
		if (Distance <= Best)
		{
			Best = Distance;
			OutAlong = Along;
			bFound = true;
		}
	}
	return bFound;
}

float UChaseRules::StepProgress(float Previous, float Projected, float MovedCm, float DeltaSeconds)
{
	return FMath::Clamp(Projected, Previous, Previous + FMath::Max(MovedCm, 0.f) * 1.5f + 300.f * FMath::Max(DeltaSeconds, 0.f));
}

bool UChaseRules::IsCatchable(bool bCornered)
{
	return bCornered;
}

float UChaseRules::Lead(float ThiefAlong, float KateAlong, bool bKateOnRoute, float StraightDistance)
{
	return bKateOnRoute ? FMath::Max(ThiefAlong - KateAlong, 0.f) : FMath::Max(StraightDistance, 0.f);
}

EChaseLeadBand UChaseRules::Band(float InLead, const FChaseTuning& Tuning)
{
	if (InLead >= Tuning.LoseLead)
	{
		return EChaseLeadBand::Losing;
	}
	if (InLead >= Tuning.FarLead)
	{
		return EChaseLeadBand::Far;
	}
	return InLead < Tuning.CloseLead ? EChaseLeadBand::Close : EChaseLeadBand::Steady;
}

float UChaseRules::TargetSpeed(float InLead, const FChaseTuning& Tuning)
{
	float Scale = 1.f;
	switch (Band(InLead, Tuning))
	{
	case EChaseLeadBand::Close:
		Scale = Tuning.CloseBoost;
		break;
	case EChaseLeadBand::Steady:
		Scale = 1.f;
		break;
	case EChaseLeadBand::Far:
	{
		const float Span = FMath::Max(Tuning.LoseLead - Tuning.FarLead, 1.f);
		Scale = FMath::Lerp(1.f, Tuning.MinSpeedScale, FMath::Clamp((InLead - Tuning.FarLead) / Span, 0.f, 1.f));
		break;
	}
	case EChaseLeadBand::Losing:
		Scale = Tuning.MinSpeedScale;
		break;
	}
	return FMath::Max(Tuning.BaseSpeed * Scale, 1.f);
}

float UChaseRules::StepSpeed(float Current, float Target, float Rate, float DeltaSeconds)
{
	const float MaxStep = FMath::Max(Rate, 0.f) * FMath::Max(DeltaSeconds, 0.f);
	return Current + FMath::Clamp(Target - Current, -MaxStep, MaxStep);
}

float UChaseRules::StepLoseClock(float Clock, float InLead, float DeltaSeconds, bool bHeld, const FChaseTuning& Tuning)
{
	if (bHeld)
	{
		return Clock;
	}
	return InLead >= Tuning.LoseLead ? Clock + FMath::Max(DeltaSeconds, 0.f) : 0.f;
}

bool UChaseRules::IsLost(float Clock, const FChaseTuning& Tuning)
{
	return Clock >= Tuning.LoseSeconds;
}

float UChaseRules::LegSeconds(EChaseLeg Leg, float HorizontalLength, float Drop)
{
	switch (Leg)
	{
	case EChaseLeg::Vault:
		return 0.5f;
	case EChaseLeg::Climb:
		return 0.8f;
	case EChaseLeg::JumpGap:
		return FMath::Min(0.45f + 0.06f * FMath::Max(HorizontalLength, 0.f) / 100.f, 1.4f);
	case EChaseLeg::Drop:
		return FMath::Max(Drop, 0.f) / 900.f + 0.3f;
	case EChaseLeg::Run:
	default:
		return 0.f;
	}
}

FVector UChaseRules::LegPosition(EChaseLeg Leg, const FVector& From, const FVector& To, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	switch (Leg)
	{
	case EChaseLeg::Vault:
	{
		// Over and down: a hop 110 cm over the higher end clears a 90 cm parapet or vent box.
		const FVector Flat = FMath::Lerp(From, To, T);
		return Flat + FVector(0.f, 0.f, 110.f * 4.f * T * (1.f - T) + (FMath::Max(From.Z, To.Z) - Flat.Z) * FMath::Sin(PI * T));
	}
	case EChaseLeg::Climb:
	{
		// Up first (the first 60% of the time), then forward onto the top.
		const float Up = FMath::Clamp(T / 0.6f, 0.f, 1.f);
		const float Forward = FMath::Clamp((T - 0.35f) / 0.65f, 0.f, 1.f);
		return FVector(FMath::Lerp(From.X, To.X, Forward), FMath::Lerp(From.Y, To.Y, Forward),
			FMath::Lerp(From.Z, To.Z, Up) + 40.f * FMath::Sin(PI * Up) * (1.f - Forward));
	}
	case EChaseLeg::JumpGap:
	{
		// A parabola: the straight line plus a rise of a quarter of the length (at least 60, at most 180 cm).
		const float Length = FVector::Dist2D(From, To);
		const float Rise = FMath::Clamp(Length * 0.25f, 60.f, 180.f);
		return FMath::Lerp(From, To, T) + FVector(0.f, 0.f, Rise * 4.f * T * (1.f - T));
	}
	case EChaseLeg::Drop:
	{
		// Out over the edge in the first fifth, then straight down.
		const float Out = FMath::Clamp(T / 0.2f, 0.f, 1.f);
		const float Down = FMath::Clamp((T - 0.2f) / 0.8f, 0.f, 1.f);
		return FVector(FMath::Lerp(From.X, To.X, Out), FMath::Lerp(From.Y, To.Y, Out), FMath::Lerp(From.Z, To.Z, Down));
	}
	case EChaseLeg::Run:
	default:
		return FMath::Lerp(From, To, T);
	}
}

bool UChaseRules::IsCaught(bool bTakenDown, bool bLimp, bool bIncapacitated, bool bDead)
{
	return bTakenDown || bLimp || bIncapacitated || bDead;
}

float UChaseRules::CorneredYaw(const FVector& Thief, const FVector& Kate)
{
	const FVector Away = (Thief - Kate) * FVector(1.f, 1.f, 0.f);
	return Away.IsNearlyZero() ? 0.f : Away.Rotation().Yaw;
}

float UChaseRules::RestoredThiefProgress(float SavedThief, float KateProgress, const FChaseTuning& Tuning)
{
	return FMath::Min(SavedThief, KateProgress + Tuning.RestoreMaxLead);
}

bool UChaseRules::HasReached(float KateProgress, float WaypointAlong, const FChaseTuning& Tuning)
{
	return KateProgress >= WaypointAlong - Tuning.ReachTolerance;
}

bool UChaseRules::ShouldStartBeat(bool bBeatDone, bool bZipStarted, bool bStartInZone, float AnchorDistance)
{
	return !bBeatDone && bZipStarted && bStartInZone && AnchorDistance <= AnchorTolerance;
}

bool UChaseRules::CanSkipBeat(bool bReplay, float BeatSeconds)
{
	return bReplay && BeatSeconds >= SkipAfterSeconds;
}

bool UChaseRules::IsBeatStuck(float BeatSeconds)
{
	return BeatSeconds >= BeatWatchdogSeconds;
}
