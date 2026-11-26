#include "Player/PropGrip.h"

#include "Math/RotationMatrix.h"

namespace HawkeyePropGrip
{
	FVector ComputeHandleCenter(const FVector& Knuckles, const FVector& Along, const FVector& Across, const FVector& Palm,
		const FHawkeyePropGrip& Grip, float Scale)
	{
		return Knuckles + (Palm * Grip.PalmCm + Along * Grip.AlongCm + Across * Grip.AcrossCm) * Scale;
	}

	FVector ComputeMuzzleDirection(const FVector& Along, const FVector& Palm, const FHawkeyePropGrip& Grip)
	{
		const float Out = FMath::DegreesToRadians(Grip.MuzzleOutDegrees);
		return (Along * FMath::Cos(Out) - Palm * FMath::Sin(Out)).GetSafeNormal();
	}

	FQuat ComputePistolRotation(const FVector& Along, const FVector& Across, const FVector& Palm, const FHawkeyePropGrip& Grip)
	{
		return FRotationMatrix::MakeFromXZ(ComputeMuzzleDirection(Along, Palm, Grip), Across).ToQuat();
	}

	FVector ComputePistolLocation(const FVector& HandleCenter, const FQuat& Rotation, const FVector& GripCenterInPistol)
	{
		return HandleCenter - Rotation.RotateVector(GripCenterInPistol);
	}

	double DistanceToLine(const FVector& Point, const FVector& LinePoint, const FVector& LineDirection)
	{
		const FVector Direction = LineDirection.GetSafeNormal();
		const FVector To = Point - LinePoint;
		return (To - Direction * FVector::DotProduct(To, Direction)).Size();
	}

	double DistanceToSegment(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector Segment = B - A;
		const double LengthSquared = Segment.SizeSquared();
		if (LengthSquared < UE_KINDA_SMALL_NUMBER)
		{
			return FVector::Dist(Point, A);
		}
		const double T = FMath::Clamp(FVector::DotProduct(Point - A, Segment) / LengthSquared, 0.0, 1.0);
		return FVector::Dist(Point, A + Segment * T);
	}

	bool ParseOverride(const FString& Text, FHawkeyePropGrip& InOut)
	{
		FString Clean = Text;
		Clean.ReplaceInline(TEXT(","), TEXT(" "));
		TArray<FString> Parts;
		Clean.ParseIntoArrayWS(Parts);
		if (Parts.Num() == 0)
		{
			return false;
		}
		float* Fields[] = {&InOut.PalmCm, &InOut.AlongCm, &InOut.AcrossCm, &InOut.MuzzleOutDegrees, &InOut.WristTurnDegrees};
		for (int32 Index = 0; Index < Parts.Num() && Index < UE_ARRAY_COUNT(Fields); ++Index)
		{
			if (Parts[Index].IsNumeric())
			{
				*Fields[Index] = FCString::Atof(*Parts[Index]);
			}
		}
		return true;
	}

	bool FingersWrapHandle(const TArray<FVector>& Fingers, const FVector& LinePoint, const FVector& LineDirection, double MaxCm)
	{
		if (Fingers.Num() == 0)
		{
			return false;
		}
		for (const FVector& Finger : Fingers)
		{
			if (DistanceToLine(Finger, LinePoint, LineDirection) > MaxCm)
			{
				return false;
			}
		}
		return true;
	}
}
