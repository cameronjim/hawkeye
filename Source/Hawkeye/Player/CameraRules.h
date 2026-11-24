#pragma once

#include "CoreMinimal.h"
#include "Player/HawkeyeMovementTypes.h"

/**
 * The third-person camera's pure rules (claude-docs/gameplay-semantics.md, "Camera rules"): no actors, no world, so
 * every number here is unit-tested (Tests/CameraRulesTest.cpp). UHawkeyeSpringArmComponent and AHawkeyeCharacter
 * only feed these functions and apply what they return.
 *
 * Pitch is the control rotation's, up positive, degrees. Heights are above her feet on flat ground, cm.
 */
namespace HawkeyeCameraRules
{
	// --- Heights and pitch ---------------------------------------------------------------------

	/** a smooth maximum: exactly Max(A, B) when they differ by more than Width, and never below it. */
	HAWKEYE_API float SmoothMax(float A, float B, float Width);

	/**
	 * Where the lens sits above her feet. The pivot is her capsule centre (HalfHeight) plus PivotLift; the lens is the arm
	 * length back along the view and the socket offset out from there, so Z = HalfHeight + Lift - Arm * sin(Pitch) +
	 * SocketZ * cos(Pitch) (+ SocketX * sin(Pitch)). Linear in the arm, the socket and the lift, which is what lets a
	 * blend of two cameras that both clear a height clear it too.
	 */
	HAWKEYE_API float LensHeightAboveFeet(float Pitch, float ArmLength, const FVector& SocketOffset, float PivotLift,
		float HalfHeight);

	/** The pivot lift (cm, never below zero) that puts the lens exactly MinLensHeight above her feet at this pitch. */
	HAWKEYE_API float RequiredPivotLift(float Pitch, float ArmLength, const FVector& SocketOffset, float HalfHeight,
		float MinLensHeight);

	/**
	 * Raises the targets' PivotLift, never lowers it, until the lens at Pitch is at least MinLensHeight above her feet.
	 * Call it last, after every layer has changed the arm and the socket: whatever they did, the lens clears the height.
	 */
	HAWKEYE_API FHawkeyeCameraTargets EnsureLensHeight(const FHawkeyeCameraTargets& Targets, float Pitch, float HalfHeight,
		float MinLensHeight);

	/**
	 * The camera for one aim state at Pitch: Base's arm, socket and field of view moved along Curve (arm in, socket up and
	 * FOV out when looking up from Curve.UpStartPitch; the arm out when looking down), then the lift that keeps the lens
	 * Curve.MinLensHeight above her feet (EnsureLensHeight). Level (pitch 0) is Base itself.
	 */
	HAWKEYE_API FHawkeyeCameraTargets ComputePitchTargets(const FHawkeyeCameraTargets& Base, const FHawkeyeCameraPitchCurve& Curve,
		float Pitch, float HalfHeight);

	/** The widest field of view the camera ever asks for, degrees: ProbeRadiusClearsNearPlane holds up to here. */
	constexpr float MaxFieldOfView = 100.f;

	/** Distance from the lens to a corner of the near clip plane, cm (Fov is the horizontal field of view). */
	HAWKEYE_API float NearPlaneCornerDistance(float FovDegrees, float AspectRatio, float NearPlane);

	/**
	 * Scale for a look input of PitchDelta degrees (up positive) at CurrentPitch: 1 until within Band degrees of the
	 * limit it moves toward, easing down to MinScale at the limit, so the view settles onto its stop instead of hitting it.
	 * Moving away from a limit is never slowed.
	 */
	HAWKEYE_API float PitchLimitScale(float CurrentPitch, float PitchDelta, float PitchMin, float PitchMax, float Band,
		float MinScale);

	// --- Time, lag and layers --------------------------------------------------------------------

	/**
	 * A frame's time with the world's time dilation taken out, so the camera's lag and easing run on the clock the player
	 * watches: hit stop (0.1), the finisher (0.25) and the quiver wheel (0.2) stretch the world's frame, not the
	 * camera's. Dilation of 1 or more, or none, leaves it alone; the result is never more than MaxSeconds.
	 */
	HAWKEYE_API float UnscaledDelta(float DilatedDelta, float Dilation, float MaxSeconds = 0.05f);

	/** Clamps how far the lagged pivot trails her: horizontally to MaxHorizontal, vertically to MaxVertical (both 0 = none). */
	HAWKEYE_API FVector ClampLagOffset(const FVector& Offset, float MaxHorizontal, float MaxVertical);

	/**
	 * The vertical dead zone for a jump's arc: how far the pivot's target is from its reference height when she is
	 * Offset above (positive) or below it. Inside DeadZone the pivot does not follow at all; beyond it, only the part past it.
	 */
	HAWKEYE_API float DeadZoneOffset(float Offset, float DeadZone);

	/** Moves Current toward Target by at most MaxUp (rising) or MaxDown (falling) per second over Delta seconds. */
	HAWKEYE_API float StepToward(float Current, float Target, float Delta, float MaxUp, float MaxDown);

	/** Rates the blended camera may move at: no layer, however it is stacked, moves the lens faster than these. */
	struct FRates
	{
		/** Arm length, cm/s. The aim blend (350 to 180 in 0.15 s) is 1130, so the cap sits above it. */
		float ArmPerSecond = 1400.f;
		/** Socket offset X and Y (the shoulder), cm/s. */
		float SocketPlanarPerSecond = 600.f;
		/** Socket offset Z, cm/s. */
		float SocketVerticalPerSecond = 500.f;
		/** Field of view, degrees/s. The aim blend is 133. */
		float FovPerSecond = 200.f;
		/** The pivot lift falling, cm/s; rising is never limited (the lift is what keeps the lens off the ground). */
		float LiftFallPerSecond = 500.f;
	};

	/**
	 * One frame of the blended camera: Previous moved toward Wanted at the Rates. The pivot lift rises at once and falls
	 * at LiftFallPerSecond. The hanging side swap and the shoulder are already eased by their own timers; this is the
	 * backstop that no layer can pop.
	 */
	HAWKEYE_API FHawkeyeCameraTargets LimitTargetStep(const FHawkeyeCameraTargets& Previous, const FHawkeyeCameraTargets& Wanted,
		float Delta, const FRates& Rates);

	/**
	 * A capsule that shrinks (a roll, a slide, a crouch) drops the pivot with it in one step. This holds the pivot's height:
	 * the returned half height falls toward Actual at DropPerSecond and rises with it at once. The difference
	 * (result - Actual) is added to the pivot lift.
	 */
	HAWKEYE_API float EasedHalfHeight(float Held, float Actual, float Delta, float DropPerSecond);

	// --- Collision -------------------------------------------------------------------------------

	/**
	 * A fraction of a sweep (the arm's length, the shoulder's reach) that snaps in at once when something is in the way
	 * and eases back out at OutSpeed once it is not. Never more than HardLimit (the sweep's own result: never inside a wall).
	 */
	HAWKEYE_API float EaseFraction(float Current, float Wanted, float HardLimit, float Delta, float OutSpeed);

	/** The side feelers' soft pull on the arm: eases down toward the nearer feeler's hit (never below MinFraction), slowly back up. */
	HAWKEYE_API float EaseFeelerFraction(float Current, float FeelerFraction, float Delta, float MinFraction = 0.55f);

	/** How far the lens must rise to be ClearanceAbove the highest floor found (a floor with none is Lowest). */
	HAWKEYE_API float RiseNeededForFloor(float LensZ, float FloorUnderLens, float FloorUnderCharacter, float Clearance);

	/** The floor correction that rides on the lens: rises quickly (never more than Slack short of Need), falls slowly. */
	HAWKEYE_API float EaseFloorCorrection(float Current, float Need, float Delta, float Slack = 12.f, float RiseSpeed = 25.f,
		float FallPerSecond = 120.f);

	/** No floor found below. */
	constexpr float NoFloor = -3.0e38f;

	/**
	 * A sweep's hit fraction backed off by Margin cm of its Length, never below zero. A resolved point (the lifted pivot,
	 * the shoulder) is where the next sweep starts; one left exactly touching a wall can read as "starts inside it", so
	 * it is left a hair clear.
	 */
	HAWKEYE_API float BackOffFraction(float Fraction, float Length, float Margin = 1.5f);

	/**
	 * The body is hidden when the lens comes inside HideBelow and shown again only when it is beyond ShowAbove, and either
	 * change waits until the last one is MinDwell seconds old, so a lens working along a wall cannot flicker her.
	 */
	HAWKEYE_API bool ShouldHideBody(bool bHiddenNow, float LensDistance, float HideBelow, float ShowAbove, float SecondsSinceChange = 1.0e6f,
		float MinDwell = 0.f);
}
