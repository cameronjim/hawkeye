#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Player/SecondaryMotion.h"
#include "RetargetGraphBuilder.generated.h"

class UAnimBlueprint;
class UIKRetargeter;
class USkeleton;

/**
 * Builds a body look's AnimBlueprint headless (Tools/Editor/create_kate_look.py calls it): one Retarget Pose
 * From Mesh node, source the parent skeletal mesh component (the hidden driver), with the given IK Retargeter,
 * into the output pose. Epic's Game Animation Sample shows its retargeted characters the same way. With dynamics
 * chains (FHawkeyeDynamicsChain, SecondaryMotion.h) the pose goes to component space through one Kawaii Physics node per
 * chain and back. Editor builds only; in a cooked game the function logs an error and returns null.
 */
UCLASS()
class HAWKEYE_API UHawkeyeRetargetGraphBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or rebuilds in place) the AnimBlueprint at PackageName for Skeleton (the look mesh's) and compiles
	 * it. Returns it dirty and unsaved, or null when it did not build or compile (the reasons are logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UAnimBlueprint* BuildRetargetFromParent(
		const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter);

	/**
	 * BuildRetargetFromParent with a Kawaii Physics node per chain in Chains after the retarget. A chain whose bones are
	 * not on Skeleton, or that HawkeyeSecondary::DescribeProblem rejects, is left out and logged (the look's skeleton may
	 * not have its simulation bones yet); with none left the graph is the plain one. Returns the Blueprint dirty and
	 * unsaved, or null (reasons logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UAnimBlueprint* BuildRetargetWithDynamics(const FString& PackageName, USkeleton* Skeleton,
		UIKRetargeter* Retargeter, const TArray<FHawkeyeDynamicsChain>& Chains);


	/**
	 * BuildRetargetFromParent with the Hawkeye Body Pose node (Player/BodyPoseNode.h) after the retarget, and
	 * UHawkeyeBodyAnimInstance as the AnimBP's parent class: the thugs' and the heavy's own hands and rest arms.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UAnimBlueprint* BuildRetargetWithBodyPose(
		const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter);

	/**
	 * BuildRetargetWithDynamics with the Hawkeye Ledge IK node (Player/LedgeIKNode.h) after the dynamics, and
	 * UHawkeyeKateBodyAnimInstance as the AnimBP's parent class: Kate's hands on the edge she hangs from, the body hung
	 * from them, the feet on the wall. With no usable chains it is the retarget, then the ledge IK.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UAnimBlueprint* BuildRetargetWithLedgeIK(const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter,
		const TArray<FHawkeyeDynamicsChain>& Chains);

	/** How many Hawkeye Ledge IK nodes Blueprint's anim graph holds, for verify scripts. -1 without a graph. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static int32 CountLedgeIKNodes(UAnimBlueprint* Blueprint);

	/** How many Hawkeye Body Pose nodes Blueprint's anim graph holds, for verify scripts. -1 without a graph. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static int32 CountBodyPoseNodes(UAnimBlueprint* Blueprint);

	/** How many Kawaii Physics nodes Blueprint's anim graph holds, for verify scripts. -1 without a graph. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static int32 CountDynamicsNodes(UAnimBlueprint* Blueprint);

private:
	static UAnimBlueprint* BuildGraph(const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter,
		const TArray<FHawkeyeDynamicsChain>& Chains, bool bBodyPose, bool bLedgeIK = false);
};
