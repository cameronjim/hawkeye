#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RetargetGraphBuilder.generated.h"

class UAnimBlueprint;
class UIKRetargeter;
class USkeleton;

/**
 * One chain (or single bone) the body look simulates with the engine's AnimDynamics (no physics asset, no plugin): a
 * ponytail, a quiver. Everything is a data field so a costume's chains are a list in a script, not code. The simulation
 * runs in component space with only part of the actor's own motion passed in, clamped, so a character at 18 m/s does
 * not throw them about; a teleport resets them (the engine does that for every AnimDynamics node).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeDynamicsChain
{
	GENERATED_BODY()

	/** The top of the chain (its root stays on the animation); a single body when ChainEnd is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FName BoundBone;

	/** The last bone of the chain; none for one body on BoundBone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FName ChainEnd;

	/** How far each bone may swing off its parent, degrees (a cone round the bone's axis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float ConeDegrees = 35.f;

	/** Stiffness back toward the animation's pose; higher holds the shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float AngularSpring = 60.f;

	/** Linear damping (the engine ignores values under 0.7). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float Damping = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float GravityScale = 1.f;

	/** The share of the mesh's own acceleration the bodies feel, and the drag from its velocity, per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float AccelerationScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float VelocityDrag = 0.2f;

	/** The most acceleration (cm/s^2, each axis) the actor's motion can pass to the bodies: 18 m/s and a 6000 cm/s^2
	 * zip clamped to this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float MaxAcceleration = 1200.f;

	/** Clamps on the actor's turning passed in, rad/s and rad/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float MaxAngularVelocity = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float MaxAngularAcceleration = 60.f;

	/** Size of each body's box, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FVector BoxExtents = FVector(4.f, 2.f, 2.f);
};

/**
 * Builds a body look's AnimBlueprint headless (Tools/Editor/create_kate_look.py calls it): one Retarget Pose
 * From Mesh node, source the parent skeletal mesh component (the hidden driver), with the given IK Retargeter,
 * into the output pose. Epic's Game Animation Sample shows its retargeted characters the same way. With dynamics
 * chains the pose goes to component space through one AnimDynamics node per chain and back. Editor
 * builds only; in a cooked game the function logs an error and returns null.
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
	 * BuildRetargetFromParent with an AnimDynamics node per chain in Chains after the retarget. A chain whose bones are
	 * not on Skeleton is left out and logged (the look's skeleton may not have its simulation bones yet); with none
	 * left the graph is the plain one. Returns the Blueprint dirty and unsaved, or null (reasons logged).
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

	/** How many Hawkeye Body Pose nodes Blueprint's anim graph holds, for verify scripts. -1 without a graph. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static int32 CountBodyPoseNodes(UAnimBlueprint* Blueprint);

	/** How many AnimDynamics nodes Blueprint's anim graph holds, for verify scripts. -1 without a graph. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static int32 CountDynamicsNodes(UAnimBlueprint* Blueprint);

private:
	static UAnimBlueprint* BuildGraph(const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter,
		const TArray<FHawkeyeDynamicsChain>& Chains, bool bBodyPose);
};
