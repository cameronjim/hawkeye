#pragma once

#include "Animation/AnimInstance.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "CoreMinimal.h"
#include "Player/BodyPose.h"

#include "BodyPoseNode.generated.h"

/** The smoothed hand and arm state the node reads: alphas per hand (index 0 left, 1 right), the rest arm's weight, the props' axes. */
struct FHawkeyeBodyPoseState
{
	float Fist[2] = {0.f, 0.f};
	float Grip[2] = {0.f, 0.f};
	float ArmRest = 0.f;
	FVector PropAxis[2] = {FVector::ZeroVector, FVector::ZeroVector};
};

/**
 * The AnimInstance parent of a thug or heavy body look's AnimBP (ABP_Thug_Body): each frame it asks the owner what
 * his hands are doing (AThugCharacter::GetBodyPoseWish) and eases the answer into the alphas the Hawkeye Body Pose
 * node uses, over ~0.12 s so a hand closes rather than snaps. An owner that is not a thug leaves them at zero: relaxed
 * hands and no arm correction.
 */
UCLASS(Transient)
class HAWKEYE_API UHawkeyeBodyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** Advances State toward a wish by DeltaSeconds (public for tests). */
	void StepToward(const HawkeyeBodyPose::FWish& Wish, float DeltaSeconds);

	const FHawkeyeBodyPoseState& GetState() const { return State; }

private:
	FHawkeyeBodyPoseState State;
};

/**
 * Hawkeye Body Pose: after the retarget, in component space, writes every finger joint of both hands from the authored
 * table (HawkeyeBodyPose::Blend: Relaxed, Fist, Grip), brings the arms down to hang beside the thighs by the instance's
 * ArmRest weight, and turns a gripping wrist toward the prop's axis. Nothing of the retargeted fingers is kept. By the same
 * weight the legs are turned in at the hip so the feet stand under the hips (HawkeyeBodyPose::LegAdductionTurn): the skeleton's
 * hips are 22.8 cm apart and the driver's idle plants its feet far wider, a straddle no retarget offset fixes.
 */
USTRUCT(BlueprintInternalUseOnly)
struct HAWKEYE_API FAnimNode_HawkeyeBodyPose : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(
		FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	/** One bone under an upper arm: the arm, the hand, a finger joint, or the tip bone after it. */
	struct FJoint
	{
		FCompactPoseBoneIndex Bone = FCompactPoseBoneIndex(INDEX_NONE);
		int32 ParentSlot = INDEX_NONE;
		int32 Finger = INDEX_NONE;
		int32 JointIndex = INDEX_NONE;
		FQuat RefLocalRotation = FQuat::Identity;
		/** In the parent's frame at the reference pose: the way a joint curls, and the first joint's swing. */
		FVector FlexAxis = FVector::ZeroVector;
		FVector SpreadAxis = FVector::ZeroVector;
		/** Thumb joints: the bone's direction and rotation at the reference pose (mesh space), and its parent's rotation. */
		FVector BindDir = FVector::ZeroVector;
		FQuat BindRot = FQuat::Identity;
		FQuat ParentBindRot = FQuat::Identity;
	};

	struct FSide
	{
		TArray<FJoint> Joints;
		int32 LowerSlot = INDEX_NONE;
		int32 HandSlot = INDEX_NONE;
		FVector Outward = FVector::RightVector;
		FVector Forward = FVector::ForwardVector;
		HawkeyeBodyPose::FHandFrame Frame;
		bool bValid = false;
	};

	/** One leg: the thigh, the foot, and everything under the thigh in compact (parent first) order. */
	struct FLeg
	{
		TArray<FCompactPoseBoneIndex> Bones;
		int32 FootSlot = INDEX_NONE;
		FVector Outward = FVector::RightVector;
		float HipOffset = 0.f;
		bool bValid = false;
	};

	FSide Sides[2];
	FLeg Legs[2];
	FHawkeyeBodyPoseState State;
	bool bReady = false;

	void EvaluateSide(const FSide& Side, int32 SideIndex, FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms) const;
	void EvaluateLeg(const FLeg& Leg, FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) const;
};
