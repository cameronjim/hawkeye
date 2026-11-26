#pragma once

#include "CoreMinimal.h"
#include "PropGrip.generated.h"

/**
 * Where a held prop's handle lies in a closed hand, per prop (claude-docs/animation.md, "Props in the hand"). The hand's
 * frame is HawkeyeBodyPose::FHandFrame: Along the fingers, Across the knuckles toward the index finger, Palm into the palm
 * (the way the fingers curl). The point is measured from the knuckle line's middle (the middle of the index and little
 * fingers' first joints), in centimetres at the body's own scale.
 *
 * A closed fist is a loop: the fingers leave the knuckle line, curl palm-ward and come back toward the wrist to meet the
 * palm. Its hollow, where a handle passes, is therefore palm-ward of the knuckle line by about the finger's thickness and
 * back toward the wrist, not out along the fingers. A handle placed forward of the knuckles (the old +0.8 cm along, 2.4
 * palm) sits in front of the fingers, which is what the heavy's bat did.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyePropGrip
{
	GENERATED_BODY()

	/** Into the palm from the knuckle line, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip")
	float PalmCm = 3.2f;

	/** Along the fingers from the knuckle line, cm: negative is back toward the wrist (where the curled fingers meet the palm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip")
	float AlongCm = -1.3f;

	/** Across the knuckles toward the index finger, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip")
	float AcrossCm = 0.f;

	/** A pistol: how far the muzzle turns from the fingers' line away from the thigh, degrees. Unused by a bat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip")
	float MuzzleOutDegrees = 18.f;

	/** How far the wrist may turn the hand toward the prop's axis, degrees (a bat is held on a diagonal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float WristTurnDegrees = 50.f;
};

/** The plain rules of a prop in a hand. */
namespace HawkeyePropGrip
{
	/**
	 * The handle's centre in the world: the knuckle line's middle (world) moved Grip.PalmCm into the palm, Grip.AlongCm along the
	 * fingers and Grip.AcrossCm across the knuckles, all times Scale (the body's scale). Along, Across and Palm are unit.
	 */
	HAWKEYE_API FVector ComputeHandleCenter(const FVector& Knuckles, const FVector& Along, const FVector& Across, const FVector& Palm,
		const FHawkeyePropGrip& Grip, float Scale);

	/** The pistol's muzzle: the fingers' line turned Grip.MuzzleOutDegrees away from the palm (the thigh side). Unit. */
	HAWKEYE_API FVector ComputeMuzzleDirection(const FVector& Along, const FVector& Palm, const FHawkeyePropGrip& Grip);

	/** The pistol's rotation: X the muzzle, Z across the knuckles (the handle runs across the fist). */
	HAWKEYE_API FQuat ComputePistolRotation(const FVector& Along, const FVector& Across, const FVector& Palm, const FHawkeyePropGrip& Grip);

	/** The pistol's origin so that its grip block's centre (GripCenterInPistol, the pistol's frame, cm) sits at HandleCenter. */
	HAWKEYE_API FVector ComputePistolLocation(const FVector& HandleCenter, const FQuat& Rotation, const FVector& GripCenterInPistol);

	/** The distance from Point to the infinite line through LinePoint along LineDirection (unit). */
	HAWKEYE_API double DistanceToLine(const FVector& Point, const FVector& LinePoint, const FVector& LineDirection);

	/** The distance from Point to the segment A to B. */
	HAWKEYE_API double DistanceToSegment(const FVector& Point, const FVector& A, const FVector& B);

	/**
	 * Applies a console override ("palm along across muzzleOut wristTurn": as many numbers as given, in that order, blanks or
	 * commas between) to InOut: the live-tuning route (hawkeye.Thug.BatGrip, hawkeye.Thug.PistolGrip). False for empty text.
	 */
	HAWKEYE_API bool ParseOverride(const FString& Text, FHawkeyePropGrip& InOut);

	/**
	 * Whether a closed hand's fingers wrap the handle: each of Fingers (the middle joint positions of the four fingers, world)
	 * lies within MaxCm of the handle's line. A bat passed through the palm has them all near its line; a bat held out in front of
	 * the fingers has them far from it.
	 */
	HAWKEYE_API bool FingersWrapHandle(const TArray<FVector>& Fingers, const FVector& LinePoint, const FVector& LineDirection, double MaxCm);
}
