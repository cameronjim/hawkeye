#include "Player/CameraRules.h"

namespace HawkeyeCameraRules
{
	float SmoothMax(float A, float B, float Width)
	{
		const float Larger = FMath::Max(A, B);
		const float Difference = FMath::Abs(A - B);
		if (Width <= 0.f || Difference >= Width)
		{
			return Larger;
		}
		const float H = (Width - Difference) / Width;
		return Larger + H * H * Width * 0.25f;
	}

	float LensHeightAboveFeet(float Pitch, float ArmLength, const FVector& SocketOffset, float PivotLift, float HalfHeight)
	{
		const float Radians = FMath::DegreesToRadians(Pitch);
		const float Sine = FMath::Sin(Radians);
		const float Cosine = FMath::Cos(Radians);
		return HalfHeight + PivotLift - ArmLength * Sine + static_cast<float>(SocketOffset.Z) * Cosine
			+ static_cast<float>(SocketOffset.X) * Sine;
	}

	float RequiredPivotLift(float Pitch, float ArmLength, const FVector& SocketOffset, float HalfHeight, float MinLensHeight)
	{
		return FMath::Max(0.f, MinLensHeight - LensHeightAboveFeet(Pitch, ArmLength, SocketOffset, 0.f, HalfHeight));
	}

	FHawkeyeCameraTargets EnsureLensHeight(const FHawkeyeCameraTargets& Targets, float Pitch, float HalfHeight, float MinLensHeight)
	{
		// The signed shortfall, not RequiredPivotLift's floor at zero, so the maximum below can be smooth: no kink in the
		// lift where the pitch crosses from "clear anyway" to "needs a lift".
		const float Shortfall = MinLensHeight - LensHeightAboveFeet(Pitch, Targets.ArmLength, Targets.SocketOffset, 0.f, HalfHeight);
		FHawkeyeCameraTargets Out = Targets;
		Out.PivotLift = SmoothMax(Targets.PivotLift, Shortfall, 20.f);
		return Out;
	}

	FHawkeyeCameraTargets ComputePitchTargets(const FHawkeyeCameraTargets& Base, const FHawkeyeCameraPitchCurve& Curve, float Pitch,
		float HalfHeight)
	{
		const float Up = FMath::SmoothStep(Curve.UpStartPitch, FMath::Max(Curve.UpFullPitch, Curve.UpStartPitch + 1.f), Pitch);
		// SmoothStep wants its edges in increasing order: work in "degrees down".
		const float Down = FMath::SmoothStep(0.f, FMath::Max(-Curve.DownFullPitch, 1.f), -Pitch);

		FHawkeyeCameraTargets Out = Base;
		Out.ArmLength = FMath::Lerp(Base.ArmLength, Curve.ArmUp, Up) + Curve.ArmDown * Down;
		Out.SocketOffset.Z = FMath::Lerp(static_cast<float>(Base.SocketOffset.Z), Curve.SocketZUp, Up);
		Out.FieldOfView = FMath::Min(Base.FieldOfView + Curve.FovUp * Up, MaxFieldOfView);
		return EnsureLensHeight(Out, Pitch, HalfHeight, Curve.MinLensHeight);
	}

	float NearPlaneCornerDistance(float FovDegrees, float AspectRatio, float NearPlane)
	{
		const float HalfWidth = NearPlane * FMath::Tan(FMath::DegreesToRadians(FovDegrees * 0.5f));
		const float HalfHeight = HalfWidth / FMath::Max(AspectRatio, KINDA_SMALL_NUMBER);
		return FMath::Sqrt(NearPlane * NearPlane + HalfWidth * HalfWidth + HalfHeight * HalfHeight);
	}

	float PitchLimitScale(float CurrentPitch, float PitchDelta, float PitchMin, float PitchMax, float Band, float MinScale)
	{
		if (Band <= 0.f || FMath::IsNearlyZero(PitchDelta))
		{
			return 1.f;
		}
		const float Distance = PitchDelta > 0.f ? PitchMax - CurrentPitch : CurrentPitch - PitchMin;
		if (Distance >= Band)
		{
			return 1.f;
		}
		const float Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Max(Distance, 0.f) / Band);
		return FMath::Lerp(FMath::Clamp(MinScale, 0.f, 1.f), 1.f, Alpha);
	}

	float UnscaledDelta(float DilatedDelta, float Dilation, float MaxSeconds)
	{
		if (Dilation <= 0.f || Dilation >= 1.f)
		{
			return DilatedDelta;
		}
		return FMath::Min(DilatedDelta / FMath::Max(Dilation, 0.001f), MaxSeconds);
	}

	FVector ClampLagOffset(const FVector& Offset, float MaxHorizontal, float MaxVertical)
	{
		FVector Out = Offset;
		if (MaxHorizontal > 0.f)
		{
			const double Planar = FMath::Sqrt(Out.X * Out.X + Out.Y * Out.Y);
			if (Planar > MaxHorizontal)
			{
				const double Scale = MaxHorizontal / Planar;
				Out.X *= Scale;
				Out.Y *= Scale;
			}
		}
		if (MaxVertical > 0.f)
		{
			Out.Z = FMath::Clamp(Out.Z, -static_cast<double>(MaxVertical), static_cast<double>(MaxVertical));
		}
		return Out;
	}

	float DeadZoneOffset(float Offset, float DeadZone)
	{
		return FMath::Sign(Offset) * FMath::Max(FMath::Abs(Offset) - FMath::Max(DeadZone, 0.f), 0.f);
	}

	float StepToward(float Current, float Target, float Delta, float MaxUp, float MaxDown)
	{
		const float Step = FMath::Max(Delta, 0.f);
		if (Target > Current)
		{
			return Current + FMath::Min(Target - Current, MaxUp * Step);
		}
		return Current - FMath::Min(Current - Target, MaxDown * Step);
	}

	FHawkeyeCameraTargets LimitTargetStep(const FHawkeyeCameraTargets& Previous, const FHawkeyeCameraTargets& Wanted, float Delta,
		const FRates& Rates)
	{
		if (Delta <= 0.f)
		{
			return Wanted;
		}
		FHawkeyeCameraTargets Out;
		Out.ArmLength = StepToward(Previous.ArmLength, Wanted.ArmLength, Delta, Rates.ArmPerSecond, Rates.ArmPerSecond);
		Out.SocketOffset.X = StepToward(static_cast<float>(Previous.SocketOffset.X), static_cast<float>(Wanted.SocketOffset.X), Delta,
			Rates.SocketPlanarPerSecond, Rates.SocketPlanarPerSecond);
		Out.SocketOffset.Y = StepToward(static_cast<float>(Previous.SocketOffset.Y), static_cast<float>(Wanted.SocketOffset.Y), Delta,
			Rates.SocketPlanarPerSecond, Rates.SocketPlanarPerSecond);
		Out.SocketOffset.Z = StepToward(static_cast<float>(Previous.SocketOffset.Z), static_cast<float>(Wanted.SocketOffset.Z), Delta,
			Rates.SocketVerticalPerSecond, Rates.SocketVerticalPerSecond);
		Out.FieldOfView = StepToward(Previous.FieldOfView, Wanted.FieldOfView, Delta, Rates.FovPerSecond, Rates.FovPerSecond);
		Out.PivotLift = StepToward(Previous.PivotLift, Wanted.PivotLift, Delta, TNumericLimits<float>::Max(), Rates.LiftFallPerSecond);
		return Out;
	}

	float EasedHalfHeight(float Held, float Actual, float Delta, float DropPerSecond)
	{
		const float Eased = StepToward(Held, Actual, Delta, TNumericLimits<float>::Max(), DropPerSecond);
		return FMath::Max(Eased, Actual);
	}

	float EaseFraction(float Current, float Wanted, float HardLimit, float Delta, float OutSpeed)
	{
		const float Next = Wanted < Current ? Wanted : FMath::FInterpTo(Current, Wanted, FMath::Max(Delta, 0.f), OutSpeed);
		return FMath::Clamp(FMath::Min(Next, HardLimit), 0.f, 1.f);
	}

	float EaseFeelerFraction(float Current, float FeelerFraction, float Delta, float MinFraction)
	{
		const float Target = FMath::Clamp(FeelerFraction, MinFraction, 1.f);
		// Eases in quickly enough to be ahead of the arm's own hit, and back out slowly so a feeler brushing a doorframe
		// does not make the arm breathe.
		const float Speed = Target < Current ? 8.f : 2.5f;
		return FMath::Clamp(FMath::FInterpTo(Current, Target, FMath::Max(Delta, 0.f), Speed), MinFraction, 1.f);
	}

	float RiseNeededForFloor(float LensZ, float FloorUnderLens, float FloorUnderCharacter, float Clearance)
	{
		const float Floor = FMath::Max(FloorUnderLens, FloorUnderCharacter);
		if (Floor <= NoFloor * 0.5f)
		{
			return 0.f;
		}
		return FMath::Max(Floor + Clearance - LensZ, 0.f);
	}

	float EaseFloorCorrection(float Current, float Need, float Delta, float Slack, float RiseSpeed, float FallPerSecond)
	{
		const float Next = Need > Current
			? FMath::FInterpTo(Current, Need, FMath::Max(Delta, 0.f), RiseSpeed)
			: StepToward(Current, Need, Delta, 0.f, FallPerSecond);
		// However it eases, the lens is never more than Slack short of the clearance.
		return FMath::Max(Next, Need - Slack);
	}

	float BackOffFraction(float Fraction, float Length, float Margin)
	{
		if (Fraction >= 1.f)
		{
			return 1.f;
		}
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		return FMath::Clamp((Fraction * Length - Margin) / Length, 0.f, 1.f);
	}

	bool ShouldHideBody(bool bHiddenNow, float LensDistance, float HideBelow, float ShowAbove)
	{
		return bHiddenNow ? LensDistance <= FMath::Max(ShowAbove, HideBelow) : LensDistance < HideBelow;
	}
}
