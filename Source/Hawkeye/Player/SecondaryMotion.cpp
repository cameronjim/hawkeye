#include "Player/SecondaryMotion.h"

#include "ReferenceSkeleton.h"

namespace HawkeyeSecondary
{
	TArray<int32> SimulatedBones(const FReferenceSkeleton& Reference, const FHawkeyeDynamicsChain& Chain)
	{
		TArray<int32> Out;
		const int32 Root = Chain.BoundBone.IsNone() ? INDEX_NONE : Reference.FindBoneIndex(Chain.BoundBone);
		if (Root == INDEX_NONE)
		{
			return Out;
		}
		// With a ChainEnd, the bones on the path up from it to the root; the rest of the subtree stays out.
		TSet<int32> Path;
		if (!Chain.ChainEnd.IsNone())
		{
			const int32 End = Reference.FindBoneIndex(Chain.ChainEnd);
			if (End == INDEX_NONE)
			{
				return Out;
			}
			int32 Walk = End;
			while (Walk != INDEX_NONE && Walk != Root)
			{
				Path.Add(Walk);
				Walk = Reference.GetParentIndex(Walk);
			}
			if (Walk != Root)
			{
				return Out;
			}
		}
		TSet<int32> In;
		In.Add(Root);
		Out.Add(Root);
		// A skeleton lists parents before their children, so one pass over the bones after the root finds the subtree.
		for (int32 Bone = Root + 1; Bone < Reference.GetNum(); ++Bone)
		{
			if (In.Contains(Reference.GetParentIndex(Bone)) && (Chain.ChainEnd.IsNone() || Path.Contains(Bone)))
			{
				In.Add(Bone);
				Out.Add(Bone);
			}
		}
		return Out;
	}

	TArray<FName> ExcludedBones(const FReferenceSkeleton& Reference, const FHawkeyeDynamicsChain& Chain)
	{
		TArray<FName> Out;
		const TArray<int32> Simulated = SimulatedBones(Reference, Chain);
		const TSet<int32> In(Simulated);
		for (int32 Bone = 0; Bone < Reference.GetNum(); ++Bone)
		{
			if (!In.Contains(Bone) && In.Contains(Reference.GetParentIndex(Bone)))
			{
				Out.Add(Reference.GetBoneName(Bone));
			}
		}
		return Out;
	}

	FTransform RefBoneInComponent(const FReferenceSkeleton& Reference, int32 BoneIndex)
	{
		const TArray<FTransform>& Pose = Reference.GetRefBonePose();
		if (!Pose.IsValidIndex(BoneIndex))
		{
			return FTransform::Identity;
		}
		FTransform Out = Pose[BoneIndex];
		for (int32 Parent = Reference.GetParentIndex(BoneIndex); Parent != INDEX_NONE;
			Parent = Reference.GetParentIndex(Parent))
		{
			Out = Out * Pose[Parent];
		}
		return Out;
	}

	void ColliderInBoneSpace(const FHawkeyeDynamicsCollider& Collider, const FTransform& RefBoneComponent,
		FVector& OutOffset, FRotator& OutRotation)
	{
		const FQuat Rotation = RefBoneComponent.GetRotation();
		OutOffset = Rotation.UnrotateVector(Collider.Offset);
		const FVector Axis = Collider.Axis.GetSafeNormal();
		const FVector LocalAxis = Rotation.UnrotateVector(Axis.IsNearlyZero() ? FVector::UpVector : Axis);
		// The plugin lays a capsule along its rotation's Z.
		OutRotation = FQuat::FindBetweenNormals(FVector::UpVector, LocalAxis).Rotator();
	}

	namespace
	{
		bool IsFinite(const FVector& V)
		{
			return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
		}

		/** The first number of Chain out of range or not finite, named; empty when they are all right. */
		FString DescribeNumbers(const FHawkeyeDynamicsChain& Chain)
		{
			const auto Bad = [](float Value, float Min, float Max)
			{
				return !FMath::IsFinite(Value) || Value < Min || Value > Max;
			};
			if (Bad(Chain.Damping, 0.f, 1.f))
			{
				return TEXT("Damping is not in 0..1");
			}
			if (Bad(Chain.Stiffness, 0.f, 1.f))
			{
				return TEXT("Stiffness is not in 0..1");
			}
			if (Bad(Chain.WorldDampingLocation, 0.f, 1.f) || Bad(Chain.WorldDampingRotation, 0.f, 1.f))
			{
				return TEXT("a world damping is not in 0..1");
			}
			if (Bad(Chain.Radius, 0.f, 1000.f))
			{
				return TEXT("Radius is negative or not finite");
			}
			if (Bad(Chain.LimitAngleDegrees, 0.f, 180.f))
			{
				return TEXT("LimitAngleDegrees is not in 0..180");
			}
			if (Bad(Chain.GravityScale, 0.f, 100.f))
			{
				return TEXT("GravityScale is negative or not finite");
			}
			if (Bad(Chain.TeleportDistance, 0.f, 1.0e7f) || Bad(Chain.TeleportRotationDegrees, 0.f, 360.f))
			{
				return TEXT("a teleport threshold is negative or not finite");
			}
			if (Chain.WarmUpFrames < 0 || Chain.TargetFramerate < 1)
			{
				return TEXT("WarmUpFrames is negative or TargetFramerate is under 1");
			}
			return FString();
		}
	}

	FString DescribeProblem(const FReferenceSkeleton& Reference, const FHawkeyeDynamicsChain& Chain)
	{
		if (Chain.BoundBone.IsNone())
		{
			return TEXT("no BoundBone");
		}
		if (Reference.FindBoneIndex(Chain.BoundBone) == INDEX_NONE)
		{
			return FString::Printf(TEXT("%s is not on the skeleton"), *Chain.BoundBone.ToString());
		}
		if (!Chain.ChainEnd.IsNone() && Reference.FindBoneIndex(Chain.ChainEnd) == INDEX_NONE)
		{
			return FString::Printf(TEXT("%s is not on the skeleton"), *Chain.ChainEnd.ToString());
		}
		const TArray<int32> Bones = SimulatedBones(Reference, Chain);
		if (Bones.Num() == 0)
		{
			return FString::Printf(TEXT("%s is not under %s"), *Chain.ChainEnd.ToString(), *Chain.BoundBone.ToString());
		}
		if (Bones.Num() < 2)
		{
			return FString::Printf(TEXT("%s has no bone under it to simulate"), *Chain.BoundBone.ToString());
		}
		const FString Numbers = DescribeNumbers(Chain);
		if (!Numbers.IsEmpty())
		{
			return Numbers;
		}
		// The pose maths normalises each bone's direction off its parent: a bone sitting on its parent, or a
		// non-finite or unscaled bone, is a NaN in the pose on the first frame.
		const TArray<FTransform>& Pose = Reference.GetRefBonePose();
		for (int32 Slot = 0; Slot < Bones.Num(); ++Slot)
		{
			const FTransform& Local = Pose[Bones[Slot]];
			const FString Name = Reference.GetBoneName(Bones[Slot]).ToString();
			if (!IsFinite(Local.GetTranslation()) || !IsFinite(Local.GetScale3D()) || Local.GetRotation().ContainsNaN())
			{
				return FString::Printf(TEXT("%s has a non-finite reference pose"), *Name);
			}
			if (Local.GetScale3D().GetAbsMin() < UE_KINDA_SMALL_NUMBER)
			{
				return FString::Printf(TEXT("%s has a zero scale"), *Name);
			}
			if (Slot > 0 && Local.GetTranslation().Size() < 0.01)
			{
				return FString::Printf(TEXT("%s sits on its parent (a zero-length bone)"), *Name);
			}
		}
		for (const FHawkeyeDynamicsCollider& Collider : Chain.Colliders)
		{
			if (Collider.Bone.IsNone() || Reference.FindBoneIndex(Collider.Bone) == INDEX_NONE)
			{
				return FString::Printf(TEXT("a collider's bone %s is not on the skeleton"), *Collider.Bone.ToString());
			}
			if (!FMath::IsFinite(Collider.Radius) || Collider.Radius <= 0.f || !FMath::IsFinite(Collider.Length) ||
				Collider.Length < 0.f || !IsFinite(Collider.Offset) || !IsFinite(Collider.Axis))
			{
				return FString::Printf(TEXT("the collider on %s has a bad radius, length, offset or axis"),
					*Collider.Bone.ToString());
			}
			if (Collider.Length > 0.f && Collider.Axis.IsNearlyZero())
			{
				return FString::Printf(TEXT("the capsule on %s has no axis"), *Collider.Bone.ToString());
			}
		}
		return FString();
	}
}
