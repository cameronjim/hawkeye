#include "UI/QuiverWheelMath.h"

int32 UQuiverWheelMath::ComputeWheelSegment(FVector2D Direction, int32 NumSegments, float DeadZone)
{
	if (NumSegments < 1 || Direction.Size() <= FMath::Max(DeadZone, 0.f) || Direction.IsNearlyZero())
	{
		return 0;
	}
	// Clockwise from up: atan2(x, y) is 0 up, 90 right, 180 down, -90 left.
	float Degrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.X, Direction.Y));
	const float Width = 360.f / NumSegments;
	// Segment 1 straddles up, so shift by half a segment before bucketing.
	Degrees = FMath::Fmod(Degrees + Width * 0.5f + 720.f, 360.f);
	return FMath::Clamp(FMath::FloorToInt(Degrees / Width), 0, NumSegments - 1) + 1;
}

float UQuiverWheelMath::GetSegmentCentreDegrees(int32 Segment, int32 NumSegments)
{
	if (NumSegments < 1)
	{
		return 0.f;
	}
	return FMath::Fmod(360.f / NumSegments * (Segment - 1) + 360.f, 360.f);
}

FVector2D UQuiverWheelMath::GetSegmentLabelOffset(int32 Segment, int32 NumSegments, float Radius)
{
	const float Radians = FMath::DegreesToRadians(GetSegmentCentreDegrees(Segment, NumSegments));
	// Screen space is y down, so up is -Y.
	return FVector2D(FMath::Sin(Radians) * Radius, -FMath::Cos(Radians) * Radius);
}
