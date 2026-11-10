#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BowIKGraphBuilder.generated.h"

class UAnimBlueprint;
class UAnimInstance;
class UBlendSpace;
class USkeleton;

/**
 * Builds the bow hands post-process AnimBlueprints headless, so they come from a script like every
 * other asset here (Tools/Editor/create_bow_ik.py calls it). Parent class UHawkeyeBowIKAnimInstance,
 * whose variables the graph reads through variable-get nodes. The anim graph:
 *
 *   Input Pose -> [the mesh's own post-process AnimBP, as a linked anim graph]
 *   -> Slot DefaultSlot (full-body clips) -> cached -> Layered blend per bone: UpperBody slot, then the
 *      aim offset (when one is given; X AimOffsetYaw, Y AimOffsetPitch, alpha AimOffsetAlpha), from spine_01 up
 *   -> to component space
 *   -> ModifyBone spine_01 (add HitLean) -> ModifyBone SpineBone (add SpineTwist)
 *   -> ModifyBone NeckBone (add NeckTwist)
 *   -> TwoBoneIK hand_l to LeftHandTarget, elbow to LeftElbowTarget, alpha LeftArmAlpha
 *   -> TwoBoneIK hand_r to RightHandTarget, elbow to RightElbowTarget, alpha RightArmAlpha
 *   -> to local space -> Output Pose
 *
 * The spine turns before the IK so the shoulders are where they end up when the arms reach. Both
 * IKs keep the hand's animated rotation relative to the forearm, so wrists stay as the animation
 * had them. Editor builds only; in a cooked game the function logs an error and returns null.
 */
UCLASS()
class HAWKEYE_API UHawkeyeBowIKGraphBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or rebuilds in place) the AnimBlueprint at PackageName (e.g.
	 * "/Game/Blueprints/Animation/ABP_BowIK_Post") for Skeleton and compiles it. ChainedPostProcess
	 * (may be None) runs first inside it: the post-process AnimBP the mesh asset already has, which
	 * the override would otherwise replace. AimOffset (may be None) is the upper body's aim offset blend
	 * space for Skeleton; it is also set on the Blueprint's defaults (UHawkeyeBowIKAnimInstance::AimOffset)
	 * so the instance clamps its inputs to its range. Returns the Blueprint, dirty and unsaved, or null
	 * when it did not build or compile (the reasons are logged). The caller saves it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UAnimBlueprint* BuildBowIKPostProcess(const FString& PackageName, USkeleton* Skeleton,
		TSubclassOf<UAnimInstance> ChainedPostProcess, FName SpineBone, FName NeckBone, UBlendSpace* AimOffset = nullptr);

	/** Nodes in Blueprint's anim graph, for verify scripts. -1 without one. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static int32 CountAnimGraphNodes(UAnimBlueprint* Blueprint);
};
