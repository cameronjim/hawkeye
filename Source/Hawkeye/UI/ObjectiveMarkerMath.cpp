#include "UI/ObjectiveMarkerMath.h"

#include "Math/InverseRotationMatrix.h"
#include "Math/PerspectiveMatrix.h"
#include "Math/RotationMatrix.h"
#include "Math/TranslationMatrix.h"

FMatrix UObjectiveMarkerMath::MakeViewProjection(const FVector& Location, const FRotator& Rotation, float FovDegrees,
	const FVector2D& ViewportSize)
{
	// Same construction as FMinimalViewInfo: world to camera-local, then UE's axis swap so x is
	// right, y is up and z is forward, then the reversed-Z projection.
	const FMatrix ViewRotation = FInverseRotationMatrix(Rotation) * FMatrix(
		FPlane(0.f, 0.f, 1.f, 0.f),
		FPlane(1.f, 0.f, 0.f, 0.f),
		FPlane(0.f, 1.f, 0.f, 0.f),
		FPlane(0.f, 0.f, 0.f, 1.f));
	const FMatrix View = FTranslationMatrix(-Location) * ViewRotation;
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(FovDegrees, 1.f, 170.f) * 0.5f);
	const float Aspect = ViewportSize.Y > 0.f ? ViewportSize.X / ViewportSize.Y : 1.f;
	const FMatrix Projection = FReversedZPerspectiveMatrix(HalfFov, HalfFov, 1.f, Aspect, 10.f, 10.f);
	return View * Projection;
}

bool UObjectiveMarkerMath::ProjectWorldToScreen(const FVector& World, const FMatrix& ViewProjection,
	const FVector2D& ViewportSize, FVector2D& OutScreen)
{
	const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(World, 1.f));
	const bool bInFront = Clip.W > UE_KINDA_SMALL_NUMBER;
	// In front, divide by depth as usual. Behind, the divide would flip the direction, so the
	// undivided x and y (the camera-space right and up, scaled) give the way to turn instead.
	const FVector2D Ndc = bInFront ? FVector2D(Clip.X / Clip.W, Clip.Y / Clip.W) : FVector2D(Clip.X, Clip.Y);
	OutScreen = FVector2D((Ndc.X * 0.5f + 0.5f) * ViewportSize.X, (0.5f - Ndc.Y * 0.5f) * ViewportSize.Y);
	return bInFront;
}

FVector2D UObjectiveMarkerMath::ClampToScreenEdge(const FVector2D& Direction, const FVector2D& ViewportSize,
	float EdgeMargin)
{
	const FVector2D Centre = ViewportSize * 0.5f;
	const FVector2D Half(FMath::Max(Centre.X - EdgeMargin, 0.f), FMath::Max(Centre.Y - EdgeMargin, 0.f));
	const FVector2D Dir = Direction.IsNearlyZero() ? FVector2D(0.f, 1.f) : Direction;

	// Scale the direction until it meets the nearer of the vertical and horizontal edges.
	const float ScaleX = FMath::Abs(Dir.X) > UE_KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Dir.X) : BIG_NUMBER;
	const float ScaleY = FMath::Abs(Dir.Y) > UE_KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Dir.Y) : BIG_NUMBER;
	return Centre + Dir * FMath::Min(ScaleX, ScaleY);
}

FObjectiveMarkerPlacement UObjectiveMarkerMath::PlaceMarker(const FVector& World, const FMatrix& ViewProjection,
	const FVector2D& ViewportSize, float EdgeMargin)
{
	FObjectiveMarkerPlacement Placement;
	FVector2D Screen;
	Placement.bInFront = ProjectWorldToScreen(World, ViewProjection, ViewportSize, Screen);
	Placement.bOnScreen = Placement.bInFront
		&& Screen.X >= EdgeMargin && Screen.X <= ViewportSize.X - EdgeMargin
		&& Screen.Y >= EdgeMargin && Screen.Y <= ViewportSize.Y - EdgeMargin;

	FVector2D Direction = Screen - ViewportSize * 0.5f;
	if (Direction.IsNearlyZero())
	{
		Direction = FVector2D(0.f, 1.f);
	}
	Placement.ArrowAngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	Placement.Position = Placement.bOnScreen ? Screen : ClampToScreenEdge(Direction, ViewportSize, EdgeMargin);
	return Placement;
}

float UObjectiveMarkerMath::YawToBearing(float Yaw, float NorthYaw)
{
	// UE yaw grows from +X toward +Y, which seen from above is clockwise, like a compass.
	const float Bearing = FMath::Fmod(Yaw - NorthYaw, 360.f);
	return Bearing < 0.f ? Bearing + 360.f : Bearing;
}

float UObjectiveMarkerMath::BearingBetween(const FVector& From, const FVector& To, float NorthYaw)
{
	const FVector Delta = To - From;
	return YawToBearing(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), NorthYaw);
}

float UObjectiveMarkerMath::BearingDelta(float TargetBearing, float ViewBearing)
{
	float Delta = FMath::Fmod(TargetBearing - ViewBearing, 360.f);
	if (Delta <= -180.f)
	{
		Delta += 360.f;
	}
	else if (Delta > 180.f)
	{
		Delta -= 360.f;
	}
	return Delta;
}

float UObjectiveMarkerMath::CompassOffset(float TargetBearing, float ViewBearing, float StripWidth, float SpanDegrees,
	bool& bOutClamped)
{
	const float HalfWidth = StripWidth * 0.5f;
	const float Offset = BearingDelta(TargetBearing, ViewBearing) / FMath::Max(SpanDegrees, 1.f) * StripWidth;
	bOutClamped = FMath::Abs(Offset) > HalfWidth;
	return FMath::Clamp(Offset, -HalfWidth, HalfWidth);
}

FText UObjectiveMarkerMath::FormatDistance(float Centimetres)
{
	const int32 Metres = FMath::RoundToInt(FMath::Max(Centimetres, 0.f) / 100.f);
	return FText::FromString(FString::Printf(TEXT("%d m"), Metres));
}
