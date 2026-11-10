#pragma once

#include "CoreMinimal.h"

struct FHitResult;
class AActor;
class UWorld;

/**
 * The effect rules that are numbers, kept apart from anything that spawns a particle so the tests
 * can check them with no renderer (claude-docs/gameplay-semantics.md, "Effects").
 */
namespace HawkeyeVfxMath
{
	/**
	 * Where the snowfall system sits for a camera at CameraLocation looking along CameraForward while
	 * the view moves at Velocity: AheadCm in front along the ground (pitch ignored, so looking at the
	 * sky does not throw the snow away), LeadSeconds of the motion ahead so running never outpaces
	 * it, and HeightCm up. A camera looking straight up or down keeps the motion lead only.
	 */
	HAWKEYE_API FVector ComputeSnowfallOrigin(const FVector& CameraLocation, const FVector& CameraForward,
		const FVector& Velocity, float AheadCm, float HeightCm, float LeadSeconds);

	/**
	 * How big the landing snow puff is for a fall of FallHeight cm: 0 (none) below MinHeight, then
	 * from 0.6 at MinHeight to 1.6 at FullHeight and above.
	 */
	HAWKEYE_API float ComputeLandingPuffScale(float FallHeight, float MinHeight = 150.f, float FullHeight = 800.f);

	/**
	 * Which way a footstep's snow kicks: back from the way she is moving and up. Standing still (or
	 * no Velocity) kicks behind Forward.
	 */
	HAWKEYE_API FVector ComputeFootstepKickDirection(const FVector& Forward, const FVector& Velocity);

	/**
	 * A scorch decal for GroundHit, the downward trace from a blast: at the hit point, projecting into
	 * the surface (the decal's X along -normal), with RollDegrees of spin. False when the trace hit
	 * nothing, or something steeper than MaxSlopeDegrees (a wall is not ground).
	 */
	HAWKEYE_API bool ComputeScorchPlacement(const FHitResult& GroundHit, float RollDegrees, FVector& OutLocation,
		FRotator& OutRotation, float MaxSlopeDegrees = 50.f);

	/**
	 * The ground under Point: a Visibility trace from 50 cm above it to MaxDropCm below, ignoring
	 * Ignore. Pawns do not block Visibility, so a blast on a thug finds the pavement under him.
	 */
	HAWKEYE_API bool TraceGround(const UWorld* World, const FVector& Point, float MaxDropCm, FHitResult& OutHit,
		const AActor* Ignore = nullptr);
}
