#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Combat/BowHandIKTypes.h"
#include "Combat/StrikePose.h"
#include "BowIKAnimInstance.generated.h"

class UBlendSpace;
class UBowComponent;

/**
 * Parent class of the bow hands post-process AnimBlueprints (ABP_BowIK_Post for the UEFN
 * mannequin, ABP_BowIK_Post_Thug for the old one), which UHawkeyeBowIKGraphBuilder builds headless.
 * UBowComponent sets the Blueprint as its mesh's post-process AnimBP override, so it runs after the
 * main AnimBP (the sample's motion matching, or a thug's single clip) and layers onto that pose:
 * the spine turns side-on toward the aim, the neck turns back, then two-bone IK puts hand_l on the
 * grip and hand_r on the string.
 *
 * This class only computes the graph's inputs, every animation update, from the owner's
 * UBowComponent: the targets in component space from the head bone and the aim, and the two alphas
 * blended over the settings' BlendSeconds. It has no graph of its own; used directly it would pass
 * no pose through, so it is never set as an override by itself.
 *
 * The same graph poses strikes and hit reactions, since there are no attack or flinch clips: the owner
 * hands it a strike pose (SetStrikePose), whose hand targets win over the bow's while their alphas are
 * above 0, and a hit lean (SetHitLean), a few degrees on spine_01 away from the hit. The graph reads
 * LeftArmAlpha and RightArmAlpha (the bow's or the strike's) and HitLean.
 *
 * With a bow clip playing (BowDraw, BowAimIdle, BowFire, BowNock on the graph's UpperBody slot) the
 * clip holds the arms, as much as the slot is blended in (ClipAlpha is UBowComponent::GetBowClipWeight):
 * the bow hand's IK is off by that much, the string hand stays on the clip with only a small correction
 * toward the arrow line (ComputeStringCorrectionAlpha), the spine drops the side-on turn the clip already
 * has, and the aim offset (AimOffset, when the graph has one) pitches and turns the upper body toward
 * the aim instead of the spine turn. See gameplay-semantics.md, "Combat animation clips".
 */
UCLASS(Transient, Blueprintable)
class HAWKEYE_API UHawkeyeBowIKAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/**
	 * Hand and elbow targets in AimFrame's space for DrawFraction (clamped to 0..1): the bow hand on
	 * the grip, the string hand moving linearly from the rest offset to the full-draw offset, the
	 * draw elbow's hint moving with it. AimFrame's X is the aim; its scale is ignored.
	 */
	static FBowHandIKTargets ComputeHandTargets(const FTransform& AimFrame, float DrawFraction,
		const FBowHandIKSettings& Settings);

	/**
	 * ComputeHandTargets with the aim frame placed so the bow hand lands exactly on Grip (world): the
	 * clip put the bow there, and the string hand is placed relative to it along Aim.
	 */
	static FBowHandIKTargets ComputeHandTargetsOnGrip(const FVector& Grip, const FRotator& Aim, float DrawFraction,
		const FBowHandIKSettings& Settings);

	/**
	 * How much the IK corrects a clip's string hand that is OffLineCm off the arrow line: 0 within the
	 * dead zone, rising smoothly to ClipCorrectionMaxAlpha at ClipCorrectionFullDistance.
	 */
	static float ComputeStringCorrectionAlpha(float OffLineCm, const FBowHandIKSettings& Settings);

	/**
	 * The point on the arrow line nearest Point: the line runs back from Rest (the bow) against
	 * LaunchDirection (where the arrow will fly), and the point is at least MinDraw behind Rest.
	 */
	static FVector ComputeArrowLinePoint(const FVector& Rest, const FVector& LaunchDirection, const FVector& Point, float MinDraw);

	/**
	 * The aim offset's inputs for Aim with the body at BodyYaw: X the yaw off the body, Y the pitch times
	 * AimOffsetPitchScale (AimOffsetPitchScaleDown below level) plus AimOffsetPitchBias, each clamped to MaxAimOffsetYaw/Pitch and, with an AimOffsetAsset,
	 * to its first two axes' ranges.
	 */
	static FVector2D ComputeAimOffsetInput(const FRotator& Aim, float BodyYaw, const FBowHandIKSettings& Settings,
		const UBlendSpace* AimOffsetAsset);

	/** Current moved toward Target at a rate that covers 0 to 1 in BlendSeconds. Snaps when BlendSeconds <= 0. */
	static float StepAlpha(float Current, float Target, float DeltaSeconds, float BlendSeconds);

	/**
	 * Spine yaw while the bow is up: the aim's yaw off the body's, clamped to MaxAimTwistDegrees,
	 * plus SideOnDegrees. Degrees; the caller scales it by the bow alpha.
	 */
	static float ComputeSpineTwistDegrees(float AimYaw, float BodyYaw, const FBowHandIKSettings& Settings);

	/**
	 * One update from Bow (null is fine: the hands let go). Alphas step toward 1 while the bow is up
	 * (bow hand) or drawing (string hand); the targets are recomputed while either is above 0.
	 * NativeUpdateAnimation calls this with the owner's bow; tests call it directly.
	 */
	void UpdateFromBow(const UBowComponent* Bow, float DeltaSeconds);

	/** UpdateFromBow, told whether a bow clip holds the bow: the clip alpha blends toward it over BlendSeconds (tests). */
	void UpdateFromBowWithClip(const UBowComponent* Bow, float DeltaSeconds, bool bClipHoldsBow);

	/** UpdateFromBow with the clip alpha set to Weight, the clip's slot weight (UpdateFromBow asks the bow for it). */
	void UpdateFromBowWithClipWeight(const UBowComponent* Bow, float DeltaSeconds, float Weight);

	float GetBowAlpha() const { return BowAlpha; }
	float GetDrawAlpha() const { return DrawAlpha; }

	/** 0..1, how far a bow clip has taken over the bow arm. */
	float GetClipAlpha() const { return ClipAlpha; }

	/** How far the clip's string hand was off the arrow line at the last update, cm (0 without a clip). */
	float GetStringOffLine() const { return StringOffLine; }

	/** 0..1, the correction the IK put on the clip's string hand at the last update. */
	float GetStringCorrectionAlpha() const { return StringCorrectionAlpha; }

	/**
	 * This frame's strike override: Sample's hands (in ActorFrame, the striker's actor transform) as
	 * component-space targets. A hand whose alpha is 0 leaves the bow's target alone.
	 */
	void SetStrikePose(const FHawkeyeStrikePoseSample& Sample, const FTransform& ActorFrame);

	/** This frame's hit lean: WorldDirection (flat, the way to lean) at Alpha (0..1). */
	void SetHitLean(const FVector& WorldDirection, float Alpha);

	/** Writes the graph's inputs from the bow targets, the strike override and the hit lean. */
	void ComposeOutputs();

	/** hand_l's target, component space. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	FVector LeftHandTarget = FVector::ZeroVector;

	/** Where the left elbow points, component space. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	FVector LeftElbowTarget = FVector::ZeroVector;

	/** hand_r's target (the string), component space. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	FVector RightHandTarget = FVector::ZeroVector;

	/** Where the right elbow points, component space. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	FVector RightElbowTarget = FVector::ZeroVector;

	/** 0..1, the bow hand on the grip (and the spine turn). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	float BowAlpha = 0.f;

	/** 0..1, the string hand on the string. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	float DrawAlpha = 0.f;

	/** 0..1, a bow clip holding the bow arm: the bow hand's IK is off by this much. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	float ClipAlpha = 0.f;

	/** Added to the spine bone, component space. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	FRotator SpineTwist = FRotator::ZeroRotator;

	/** Added to the neck bone, component space: the side-on turn taken back so the head stays on the aim. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	FRotator NeckTwist = FRotator::ZeroRotator;

	/** hand_l's IK alpha as the graph reads it: the bow hand's, or a strike's while one is on. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	float LeftArmAlpha = 0.f;

	/** hand_r's IK alpha as the graph reads it: the string hand's, or a strike's while one is on. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	float RightArmAlpha = 0.f;

	/**
	 * The upper body's aim offset (BS_BowAimOffset_Sparrow for this skeleton), set on the Blueprint's
	 * defaults by UHawkeyeBowIKGraphBuilder when its graph has the aim offset node. Null: no aim offset,
	 * and the spine turns toward the aim as it did before.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bow IK|Aim Offset")
	TObjectPtr<UBlendSpace> AimOffset = nullptr;

	/** The aim offset's yaw input, degrees off the body. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK|Aim Offset")
	float AimOffsetYaw = 0.f;

	/** The aim offset's pitch input, degrees. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK|Aim Offset")
	float AimOffsetPitch = 0.f;

	/** 0..1, the aim offset's weight: the clip alpha times the bow alpha, 0 without an AimOffset. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK|Aim Offset")
	float AimOffsetAlpha = 0.f;

	/** Added to spine_01, component space: the lean away from a hit. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Bow IK")
	FRotator HitLean = FRotator::ZeroRotator;

	/** The way the upper body leans, component space, flat. Zero for none. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Hit Lean")
	FVector HitLeanDirection = FVector::ZeroVector;

	/** 0..1, how far into the lean. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Hit Lean")
	float HitLeanAlpha = 0.f;

	/** The lean at full alpha, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow IK|Hit Lean")
	float HitLeanDegrees = 5.f;

	/** A strike's right hand target, component space; wins over the string hand while its alpha is above 0. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Strike")
	FVector StrikeRightHandTarget = FVector::ZeroVector;

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Strike")
	FVector StrikeRightElbowTarget = FVector::ZeroVector;

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Strike")
	float StrikeRightHandAlpha = 0.f;

	/** A strike's left hand target, component space; wins over the bow hand while its alpha is above 0. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Strike")
	FVector StrikeLeftHandTarget = FVector::ZeroVector;

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Strike")
	FVector StrikeLeftElbowTarget = FVector::ZeroVector;

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Bow IK|Strike")
	float StrikeLeftHandAlpha = 0.f;

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** Recomputes the component-space targets and the twists from Bow and the last pose's head bone. */
	void UpdateTargets(const UBowComponent& Bow);

	/** The alphas, targets and outputs for this update, once ClipAlpha is set. */
	void UpdateAfterClipAlpha(const UBowComponent* Bow, float DeltaSeconds);

	/**
	 * Under a clip: the string hand's target, component space, where the clip put the hand moved toward
	 * the arrow line; its correction alpha into StringCorrectionAlpha. Reads the last pose.
	 */
	FVector ComputeClipStringTarget(const UBowComponent& Bow, const FVector& Grip, FVector& OutElbow);

	/** 0..1, the correction the IK puts on the clip's string hand (times the draw alpha, under a clip). */
	float StringCorrectionAlpha = 0.f;

	/** How far off the arrow line the clip's string hand was, cm. */
	float StringOffLine = 0.f;

	/** Last update's string hand target and alpha, component space: the IK's share of the last pose's hand. */
	FVector LastRightTarget = FVector::ZeroVector;
	float LastRightAlpha = 0.f;
};
