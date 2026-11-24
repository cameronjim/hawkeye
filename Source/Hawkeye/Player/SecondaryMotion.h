#pragma once

#include "CoreMinimal.h"
#include "SecondaryMotion.generated.h"

struct FReferenceSkeleton;

/**
 * A shape a simulated chain may not pass through: the head, the neck, the upper back. Drawn from the bone it rides on, in
 * the axes of the skeleton's reference pose (the character stands upright, so Z is up and the numbers read the way a
 * person measures: "8 cm up the head"), whatever way the bone's own axes point. The builder turns it into the plugin's
 * bone-space limit (HawkeyeSecondary::ColliderInBoneSpace).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeDynamicsCollider
{
	GENERATED_BODY()

	/** The bone the shape rides on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FName Bone;

	/** cm. A capsule's radius too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.1"))
	float Radius = 8.f;

	/** cm, between the two end spheres' centres; 0 is a sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float Length = 0.f;

	/** cm from the bone's origin, in the reference pose's axes (Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FVector Offset = FVector::ZeroVector;

	/** Which way a capsule lies, in the reference pose's axes; ignored by a sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FVector Axis = FVector(0., 0., 1.);
};

/**
 * One chain of the body look the Kawaii Physics plugin (MIT, Plugins/KawaiiPhysics) simulates: a ponytail, a quiver.
 * Everything is a data field so a costume's chains are a list in a script (import_bodies.KATE_DYNAMICS), not code. The
 * plugin runs in component space, in the pose the retarget gives it, with a fixed sub-step (frame-rate independent) and
 * its own teleport handling; the chain's root bone stays on the animation and the bones under it follow it with lag. The
 * struct kept its AnimDynamics-era name (UHawkeyeRetargetGraphBuilder::BuildRetargetWithDynamics) so the scripts did not
 * change; none of AnimDynamics is left.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeDynamicsChain
{
	GENERATED_BODY()

	/** The top of the chain (its root stays on the animation; its rotation swings with the bone under it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FName BoundBone;

	/** The last bone of the chain, which must be a leaf the engine reads the previous bone's direction from; none for
	 * everything under BoundBone. Bones off the path from BoundBone to here are left out of the simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	FName ChainEnd;

	/** Per step, 0..1: how much of its speed a bone loses; higher is stiffer air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Damping = 0.15f;

	/** Per step, 0..1: how hard a bone is pulled back toward the animation's pose; higher holds the shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Stiffness = 0.08f;

	/** 0..1: how much of the mesh's own travel the chain does NOT feel (1 follows her exactly, 0 trails fully). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WorldDampingLocation = 0.88f;

	/** The same for her turning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WorldDampingRotation = 0.85f;

	/** cm, each bone's own radius for the collision shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float Radius = 1.2f;

	/** Degrees a bone may swing off its pose direction (the plugin's LimitAngle): the cap that keeps a sprint's trail
	 * and a zip's jerk from throwing the tail about. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float LimitAngleDegrees = 60.f;

	/** Multiplies real gravity (980 cm/s^2 down in the world); 0 is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float GravityScale = 1.f;

	/** cm: a mesh move past this in one frame (a cut, a snap) is a teleport and the chain does not trail it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float TeleportDistance = 150.f;

	/** Degrees: the same for a turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0"))
	float TeleportRotationDegrees = 45.f;

	/** Steps run on the first frame and after every reset (the engine's NotifyTeleported resets), so the chain settles
	 * under gravity before it is seen; 0 skips it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0"))
	int32 WarmUpFrames = 6;

	/** The plugin's step rate, steps per second (it sub-steps at 1 / this). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "1"))
	int32 TargetFramerate = 60;

	/** Shapes the chain's bones stay outside of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	TArray<FHawkeyeDynamicsCollider> Colliders;
};

/**
 * The rules of a body look's secondary-motion chains, with no editor and no plugin in them so they can be tested and
 * the game target links them: which bones a chain simulates, whether a chain can be built on a skeleton at all (the
 * plugin made no promises about zero-length bones or non-finite numbers, and the engine's AnimDynamics put NaNs in
 * ponytail_01 on the first frame), and where a collision shape sits in a bone's own axes.
 */
namespace HawkeyeSecondary
{
	/** The bones (indices into Reference) Chain simulates: BoundBone and everything under it, or, with a ChainEnd, only
	 * the bones on the path from BoundBone down to it. Empty when BoundBone or ChainEnd is missing or ChainEnd is not
	 * under BoundBone. Parents come before children. */
	HAWKEYE_API TArray<int32> SimulatedBones(const FReferenceSkeleton& Reference, const FHawkeyeDynamicsChain& Chain);

	/** The bones to pass the plugin as excluded ("this bone and everything under it") so it simulates exactly
	 * SimulatedBones: the children of a simulated bone that are not themselves simulated. */
	HAWKEYE_API TArray<FName> ExcludedBones(const FReferenceSkeleton& Reference, const FHawkeyeDynamicsChain& Chain);

	/** Why Chain cannot be built on Reference, or an empty string when it can: a missing bone, fewer than two bones, a
	 * zero-length or non-finite bone, numbers out of range or not finite, a collider on a missing bone. */
	HAWKEYE_API FString DescribeProblem(const FReferenceSkeleton& Reference, const FHawkeyeDynamicsChain& Chain);

	/** A bone's reference-pose transform in component space (its locals multiplied up the parents). */
	HAWKEYE_API FTransform RefBoneInComponent(const FReferenceSkeleton& Reference, int32 BoneIndex);

	/** Collider's centre offset and capsule rotation in the bone's own axes, given the bone's reference pose in component
	 * space: what the plugin's OffsetLocation and OffsetRotation take. The capsule's long axis is the rotation's Z. */
	HAWKEYE_API void ColliderInBoneSpace(const FHawkeyeDynamicsCollider& Collider, const FTransform& RefBoneComponent,
		FVector& OutOffset, FRotator& OutRotation);
}
