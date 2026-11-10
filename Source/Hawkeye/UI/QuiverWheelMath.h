#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "QuiverWheelMath.generated.h"

/**
 * The radial quiver's arithmetic, with no widget or player in it, so it is unit-tested.
 *
 * The wheel has NumSegments equal segments; segment 1 is centred straight up and the rest follow
 * clockwise, so with seven slots standard is at 12 o'clock and explosive just left of it.
 * Directions are "screen up" vectors: X right, Y up.
 */
UCLASS()
class HAWKEYE_API UQuiverWheelMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The segment (1..NumSegments) Direction points into, or 0 when it is no longer than DeadZone
	 * or NumSegments is under 1. A direction exactly on a boundary belongs to the segment clockwise
	 * of it.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	static int32 ComputeWheelSegment(FVector2D Direction, int32 NumSegments, float DeadZone = 0.f);

	/** Clockwise angle from straight up to the middle of Segment, degrees in [0, 360). */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	static float GetSegmentCentreDegrees(int32 Segment, int32 NumSegments);

	/**
	 * Where Segment's label goes, in widget pixels from the wheel's centre (X right, Y down),
	 * Radius out along the segment's middle.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD|Quiver Wheel")
	static FVector2D GetSegmentLabelOffset(int32 Segment, int32 NumSegments, float Radius);
};
