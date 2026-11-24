#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;

/**
 * Our own hands and rest arms for the visible thug and heavy bodies (claude-docs/animation.md, "Hands, third pass").
 *
 * The bodies are posed from a hidden driver mannequin by Retarget Pose From Mesh, and a retarget cannot make a
 * hand look right on a body with other proportions: two passes of retarget offsets failed. So after the retarget
 * the visible body's fingers are written outright from a table of authored hand poses (Relaxed, Fist, Grip), blended
 * by state, and at rest the arms are brought down to hang beside the thighs. This file is the plain rules (the table,
 * the blends, the arm's rest directions, the hand's frame); FAnimNode_HawkeyeBodyPose (BodyPoseNode.h) applies them.
 * Angles are degrees; a finger's curl is positive toward the palm.
 */
namespace HawkeyeBodyPose
{
	/** Side indices: 0 the left hand, 1 the right (the `_l` and `_r` bones). */
	constexpr int32 HandLeft = 0;
	constexpr int32 HandRight = 1;

	enum class EFinger : uint8
	{
		Thumb,
		Index,
		Middle,
		Ring,
		Pinky,
		Count
	};

	enum class EHandPose : uint8
	{
		/** A hand hanging at rest: fingers gently curled and staggered, the thumb alongside and clear of the index. */
		Relaxed,
		/** A closed fist, thumb across: the hand of a punch. */
		Fist,
		/** Closed round a handle (a pistol grip, a bat), looser than the fist. */
		Grip
	};

	/** One finger: the curl at its three joints (the thumb's is out of the palm's plane), and its swing at the first joint
	 * (the thumb: in the palm's plane, toward the fingers). */
	struct FFingerAngles
	{
		float Curl[3] = {0.f, 0.f, 0.f};
		float Spread = 0.f;
	};

	/** The authored angles of one pose for one finger. */
	HAWKEYE_API FFingerAngles Angles(EHandPose Pose, EFinger Finger);

	/** Relaxed, blended to the fist by FistAlpha, then to the grip by GripAlpha (the grip wins where both are on). */
	HAWKEYE_API FFingerAngles Blend(EFinger Finger, float FistAlpha, float GripAlpha);

	/** Current moved toward Target so a full swing takes BlendSeconds (0.12 s: a hand closing, not snapping). */
	HAWKEYE_API float StepAlpha(float Current, float Target, float DeltaSeconds, float BlendSeconds = 0.12f);

	/** The rest arm: directions (unit, in whichever space Up/Outward/Forward are given) of the upper arm and forearm. */
	struct FArmRest
	{
		FVector Upper = FVector::DownVector;
		FVector Lower = FVector::DownVector;
	};

	/**
	 * A hanging arm: the upper arm straight down, tilted OutwardDegrees away from the body (shoulders much wider than the
	 * hips: the hand hangs beside the thigh), the forearm carrying on bent ElbowDegrees forward. Up, Outward (away from the
	 * midline, on this arm's side) and Forward must be unit and mutually perpendicular.
	 */
	HAWKEYE_API FArmRest ArmRestDirections(const FVector& Up, const FVector& Outward, const FVector& Forward,
		float OutwardDegrees = 20.f, float ElbowDegrees = 6.f);

	/** The hand's axes in its own bone's frame, from the reference pose. */
	struct FHandFrame
	{
		/** Along the fingers. */
		FVector Along = FVector::ZeroVector;
		/** Across the knuckles, from the little finger to the index. */
		FVector Across = FVector::ZeroVector;
		/** Into the palm: the way the fingers curl. */
		FVector Palm = FVector::ZeroVector;
		/** The knuckle line's middle, as an offset from the hand bone in the hand bone's frame. */
		FVector Knuckles = FVector::ZeroVector;
		bool bValid = false;
	};

	/** The frame of the hand (side Left or Right) on a skeleton with the UE mannequin's hand and finger bone names. */
	HAWKEYE_API FHandFrame ComputeHandFrame(const FReferenceSkeleton& Reference, int32 Side);

	/** The angle between the thumb's first bone and the fingers' line at the reference pose, and out of the palm's plane (degrees). */
	HAWKEYE_API FVector2D ThumbRestAngles(const FReferenceSkeleton& Reference, int32 Side);

	/** The suffix of a side's bones, `l` or `r`. */
	HAWKEYE_API const TCHAR* SideSuffix(int32 Side);

	/** Where the thug or heavy wants his hands this frame (his state; the anim instance smooths it into alphas). */
	struct FWish
	{
		bool bFist[2] = {false, false};
		bool bGrip[2] = {false, false};
		/** 0..1: how much of the rest arm (hanging by the thigh) to apply. */
		float ArmRest = 0.f;
		/** The held prop's axis in the world per hand (its handle line, zero for none): the wrist turns toward it. */
		FVector PropAxis[2] = {FVector::ZeroVector, FVector::ZeroVector};
	};

	/**
	 * Where a closed hand's thumb lies, per joint, as weights on the hand's axes (Along the fingers, Across the knuckles toward
	 * the index finger, Palm into the palm). A thumb is not a finger: its base is out of the palm's plane, so a closed
	 * thumb is aimed at a direction rather than curled. Pose is Fist or Grip.
	 */
	HAWKEYE_API FVector ClosedThumbDirection(EHandPose Pose, int32 Joint);

	/** How far the wrist may turn toward a prop's axis, degrees (a bat is held across the fingers on a diagonal). */
	constexpr float MaxWristTurnDegrees = 50.f;

	/** Rest arm weight from the thug's state: 1 standing at ease, 0 while he moves faster than ~1.5 m/s or acts. */
	HAWKEYE_API float RestArmWeight(bool bActing, float SpeedCmPerSecond);
}
