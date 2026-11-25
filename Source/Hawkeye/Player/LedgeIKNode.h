#pragma once

#include "Animation/AnimInstance.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "CoreMinimal.h"
#include "Player/BodyPose.h"
#include "Traversal/LedgeBodyRules.h"

#include "LedgeIKNode.generated.h"

/**
 * The AnimInstance parent of Kate's visible body's AnimBP (ABP_Kate_Body): each frame it asks the owner's parkour component
 * what her body is doing on a ledge (UParkourComponent::GetLedgeBodyPose: world space targets from the ledge's own geometry)
 * and keeps the answer for the Hawkeye Ledge IK node. An owner that is not climbing leaves it inactive: the node does nothing
 * and the pose is the retarget's, exactly as before.
 */
UCLASS(Transient)
class HAWKEYE_API UHawkeyeKateBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	const FLedgeBodyPose& GetLedgePose() const { return LedgePose; }

	/** Sets the pose by hand (tests, the strips' debug draw). NativeUpdateAnimation overwrites it. */
	void SetLedgePose(const FLedgeBodyPose& InPose) { LedgePose = InPose; }

private:
	FLedgeBodyPose LedgePose;
};

/**
 * Hawkeye Ledge IK: after the retarget (and the secondary motion), in component space, locks Kate's visible body to the
 * ledge she is on. Her hands are two-bone IK'd onto the edge (the wrists on the ledge line at shoulder width, turned so the
 * fingers point into the wall tipped down and the palm is on the top, the elbows out and down, the fingers written from the
 * authored Hang table); the body is moved rigidly so her shoulders hang under the hands the arms' length away, the swing
 * (a damped pendulum from the parkour component) turns it about the hands' line, and the feet are put on the wall's plane
 * with the ball of the foot a few cm off it. Every weight is the pose's; the reach weight lets go of a hand whose target
 * is further than the arm can go, so a clip that has not got her there yet is not stretched. The driver's own hands, which the
 * game reads for the bow and the rope, are not touched: this only changes what is drawn.
 *
 * The maths is HawkeyeLedge (Traversal/LedgeBodyRules.h); this is the bones.
 */
USTRUCT(BlueprintInternalUseOnly)
struct HAWKEYE_API FAnimNode_HawkeyeLedgeIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(
		FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	/** A bone under a hand: a finger joint (Finger, Joint set) or anything else (the tip bones). */
	struct FHandJoint
	{
		int32 Slot = INDEX_NONE;
		int32 ParentSlot = INDEX_NONE;
		int32 Finger = INDEX_NONE;
		int32 Joint = INDEX_NONE;
		FQuat RefLocalRotation = FQuat::Identity;
		/** In the parent's frame at the reference pose: the way the joint curls toward the palm. */
		FVector FlexAxis = FVector::ZeroVector;
		/** In the parent's frame: the palm's normal, and the turn about it (radians) that brings this joint parallel to the middle finger's. */
		FVector AdductAxis = FVector::ZeroVector;
		float AdductRadians = 0.f;
	};

	struct FArm
	{
		int32 Upper = INDEX_NONE;
		int32 Lower = INDEX_NONE;
		int32 Hand = INDEX_NONE;
		/** Slots (parent first) of everything from the upper arm down, from the forearm down, from the hand down. */
		TArray<int32> FromUpper;
		TArray<int32> FromLower;
		TArray<int32> FromHand;
		/** Everything under the hand but the hand, parent first. */
		TArray<FHandJoint> Joints;
		HawkeyeBodyPose::FHandFrame Frame;
		bool bValid = false;
	};

	struct FLeg
	{
		int32 Thigh = INDEX_NONE;
		int32 Calf = INDEX_NONE;
		int32 Foot = INDEX_NONE;
		int32 Ball = INDEX_NONE;
		TArray<int32> FromThigh;
		TArray<int32> FromCalf;
		TArray<int32> FromFoot;
		bool bValid = false;
	};

	/** The pelvis and everything under it, parent first, and each one's parent in the same list. */
	TArray<FCompactPoseBoneIndex> Bones;
	TArray<int32> ParentSlot;
	FArm Arms[2];
	FLeg Legs[2];
	FLedgeBodyPose Pose;
	bool bReady = false;

	void SolveArm(int32 Side, TArray<FTransform>& Comp, const TArray<FTransform>& Local, const FTransform& ToWorld, float Scale,
		const FVector& NormalC, const FVector& UpC) const;
	void SolveLeg(int32 Side, TArray<FTransform>& Comp, const FTransform& ToWorld, float Scale, const FVector& NormalC,
		const FVector& UpC, float FootWeight) const;
};
