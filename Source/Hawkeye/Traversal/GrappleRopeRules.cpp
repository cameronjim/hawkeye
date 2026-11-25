#include "Traversal/GrappleRopeRules.h"

void FGrappleRope::Begin()
{
	State = EGrappleRopeState::Flying;
	StateSeconds = 0.f;
	Slack = 1.f;
}

void FGrappleRope::Arrive()
{
	if (State != EGrappleRopeState::Flying)
	{
		return;
	}
	State = EGrappleRopeState::Taut;
	StateSeconds = 0.f;
}

void FGrappleRope::Release(const FVector& HandAtRelease)
{
	if (State == EGrappleRopeState::Hidden || State == EGrappleRopeState::Released)
	{
		return;
	}
	State = EGrappleRopeState::Released;
	StateSeconds = 0.f;
	FrozenHand = HandAtRelease;
}

void FGrappleRope::Reset()
{
	State = EGrappleRopeState::Hidden;
	StateSeconds = 0.f;
	Slack = 0.f;
}

void FGrappleRope::Step(float DeltaSeconds, const FHawkeyeGrappleRopeSettings& Settings)
{
	if (State == EGrappleRopeState::Hidden)
	{
		return;
	}
	StateSeconds += FMath::Max(DeltaSeconds, 0.f);
	switch (State)
	{
		case EGrappleRopeState::Flying:
			Slack = 1.f;
			break;
		case EGrappleRopeState::Taut:
		{
			// Ease-out cubic: most of the sag leaves in the first frames, the last of it settles.
			const float T = FMath::Clamp(StateSeconds / FMath::Max(Settings.SnapSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f);
			const float Inverse = 1.f - T;
			Slack = Inverse * Inverse * Inverse;
			break;
		}
		case EGrappleRopeState::Released:
		{
			// Goes slack in the first part of the release while it is drawn back.
			Slack = FMath::SmoothStep(0.f, FMath::Max(Settings.ReleaseSeconds * 0.4f, KINDA_SMALL_NUMBER), StateSeconds);
			if (StateSeconds >= Settings.ReleaseSeconds)
			{
				Reset();
			}
			break;
		}
		default:
			break;
	}
}

void FGrappleRope::BuildPoints(const FVector& Hand, const FVector& End, const FHawkeyeGrappleRopeSettings& Settings,
	TArray<FVector>& OutPoints) const
{
	const int32 Count = FMath::Max(Settings.SegmentCount, 2);
	OutPoints.Reset(Count + 1);

	FVector Start = Hand;
	if (State == EGrappleRopeState::Released)
	{
		const float Retract = FMath::SmoothStep(0.f, FMath::Max(Settings.ReleaseSeconds, KINDA_SMALL_NUMBER), StateSeconds);
		Start = FMath::Lerp(FrozenHand, End, Retract);
	}

	const FVector Axis = End - Start;
	const float Span = static_cast<float>(Axis.Size());
	if (Span < 1.f)
	{
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			OutPoints.Add(Start);
		}
		return;
	}
	const FVector Direction = Axis / Span;

	// Sags toward the ground; a near-vertical span sags to the side instead of along itself.
	FVector SagDirection = FVector(0.f, 0.f, -1.f) - Direction * static_cast<float>(FVector::DotProduct(FVector(0.f, 0.f, -1.f), Direction));
	if (!SagDirection.Normalize())
	{
		SagDirection = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
		if (SagDirection.IsNearlyZero())
		{
			SagDirection = FVector::CrossProduct(Direction, FVector::ForwardVector).GetSafeNormal();
		}
	}
	const FVector Side = FVector::CrossProduct(Direction, SagDirection).GetSafeNormal();
	const FVector ShiverDirection = (Side * 0.6f + SagDirection * 0.8f).GetSafeNormal();

	const float SagCm = FMath::Min(Span * Settings.SagFraction, Settings.MaxSagCm) * Slack;

	float Shiver = 0.f;
	float WaveAmplitude = 0.f;
	switch (State)
	{
		case EGrappleRopeState::Flying:
			WaveAmplitude = Settings.FlyWaveCm * static_cast<float>(FMath::Sin(UE_TWO_PI * Settings.FlyWaveHz * StateSeconds));
			break;
		case EGrappleRopeState::Taut:
			Shiver = Settings.VibrationCm * FMath::Exp(-StateSeconds / FMath::Max(Settings.VibrationDecaySeconds, KINDA_SMALL_NUMBER))
				* static_cast<float>(FMath::Cos(UE_TWO_PI * Settings.VibrationHz * StateSeconds));
			break;
		case EGrappleRopeState::Released:
		{
			const float Left = 1.f - FMath::Clamp(StateSeconds / FMath::Max(Settings.ReleaseSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f);
			Shiver = Settings.WhipCm * Left * static_cast<float>(FMath::Sin(UE_TWO_PI * Settings.WhipHz * StateSeconds));
			break;
		}
		default:
			break;
	}

	for (int32 Index = 0; Index <= Count; ++Index)
	{
		const float U = static_cast<float>(Index) / static_cast<float>(Count);
		// Both ends are pinned: the sag is a parabola, the shiver and the ripple the lowest mode of a string.
		const float Belly = 4.f * U * (1.f - U);
		const float Mode = static_cast<float>(FMath::Sin(UE_PI * U));
		FVector Point = FMath::Lerp(Start, End, U);
		Point += SagDirection * (SagCm * Belly);
		Point += ShiverDirection * (Shiver * Mode);
		Point += Side * (WaveAmplitude * Mode * static_cast<float>(FMath::Sin(UE_TWO_PI * U)));
		OutPoints.Add(Point);
	}
	// The ends are exactly where they were asked for.
	OutPoints[0] = Start;
	OutPoints[Count] = End;
}

float FGrappleRope::MaxDeviation(const TArray<FVector>& Points)
{
	if (Points.Num() < 3)
	{
		return 0.f;
	}
	const FVector& A = Points[0];
	const FVector& B = Points.Last();
	float Worst = 0.f;
	for (int32 Index = 1; Index < Points.Num() - 1; ++Index)
	{
		const FVector Closest = FMath::ClosestPointOnSegment(Points[Index], A, B);
		Worst = FMath::Max(Worst, static_cast<float>(FVector::Dist(Points[Index], Closest)));
	}
	return Worst;
}
