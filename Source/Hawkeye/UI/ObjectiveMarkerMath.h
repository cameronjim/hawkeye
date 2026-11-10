#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ObjectiveMarkerMath.generated.h"

/** Where the objective marker goes on screen this frame. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FObjectiveMarkerPlacement
{
	GENERATED_BODY()

	/** Centre of the marker in viewport pixels, already clamped to the edge when off screen. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objective")
	FVector2D Position = FVector2D::ZeroVector;

	/** True when the point projects inside the screen, less the edge margin. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objective")
	bool bOnScreen = false;

	/** False when the point is behind the camera. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objective")
	bool bInFront = false;

	/** Screen-space direction from the centre toward the point, degrees (0 = right, 90 = down). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objective")
	float ArrowAngleDegrees = 0.f;
};

/**
 * The objective marker's and the compass's arithmetic, with no widget, player or world in it, so
 * every rule is unit-tested. Screen coordinates are pixels from the top-left, y down.
 * Bearings are compass degrees, clockwise from north, in [0, 360).
 */
UCLASS()
class HAWKEYE_API UObjectiveMarkerMath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The view-projection matrix UE builds for a camera at Location looking along Rotation with a
	 * horizontal FovDegrees on a ViewportSize screen (reversed-Z perspective, as the renderer uses).
	 */
	static FMatrix MakeViewProjection(const FVector& Location, const FRotator& Rotation, float FovDegrees,
		const FVector2D& ViewportSize);

	/**
	 * Projects World through ViewProjection. Returns true when it is in front of the camera; then
	 * OutScreen is its pixel position (which may lie outside the viewport). Behind the camera,
	 * OutScreen is centre plus the direction toward the point, not a position.
	 */
	static bool ProjectWorldToScreen(const FVector& World, const FMatrix& ViewProjection,
		const FVector2D& ViewportSize, FVector2D& OutScreen);

	/**
	 * The point on the rectangle inset EdgeMargin from the viewport's edges that lies from the
	 * centre along Direction. A zero direction means straight down (behind, dead centre).
	 */
	static FVector2D ClampToScreenEdge(const FVector2D& Direction, const FVector2D& ViewportSize, float EdgeMargin);

	/** Projects World and, when it is off screen or behind, clamps it to the edge with the arrow's angle. */
	static FObjectiveMarkerPlacement PlaceMarker(const FVector& World, const FMatrix& ViewProjection,
		const FVector2D& ViewportSize, float EdgeMargin);

	/** Compass bearing of a world yaw, given the world yaw that points north. */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	static float YawToBearing(float Yaw, float NorthYaw);

	/** Compass bearing from From to To on the ground plane. */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	static float BearingBetween(const FVector& From, const FVector& To, float NorthYaw);

	/** Signed turn from ViewBearing to TargetBearing, in (-180, 180]: positive is to the right. */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	static float BearingDelta(float TargetBearing, float ViewBearing);

	/**
	 * Horizontal offset from the strip's centre, in pixels, of something at TargetBearing on a
	 * compass strip StripWidth wide that shows SpanDegrees centred on ViewBearing. Clamped to the
	 * strip's ends; bOutClamped says so.
	 */
	UFUNCTION(BlueprintPure, Category = "HUD|Compass")
	static float CompassOffset(float TargetBearing, float ViewBearing, float StripWidth, float SpanDegrees,
		bool& bOutClamped);

	/** "42 m": whole metres, rounded. */
	UFUNCTION(BlueprintPure, Category = "HUD|Objective")
	static FText FormatDistance(float Centimetres);
};
