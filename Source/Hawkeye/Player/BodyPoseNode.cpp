#include "Player/BodyPoseNode.h"

#include "Animation/AnimInstanceProxy.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Hawkeye.h"
#include "ReferenceSkeleton.h"
#include "World/ThugCharacter.h"

using namespace HawkeyeBodyPose;

namespace
{
	const TCHAR* FingerNames[5] = {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};

	/** The finger and joint (0..2) a bone name is, from `<finger>_0<n>_<side>`; false for anything else (the tip bone included). */
	bool ParseFingerBone(const FString& Name, int32& OutFinger, int32& OutJoint)
	{
		for (int32 Finger = 0; Finger < 5; ++Finger)
		{
			for (int32 Joint = 0; Joint < 3; ++Joint)
			{
				if (Name.StartsWith(FString::Printf(TEXT("%s_%02d_"), FingerNames[Finger], Joint + 1)))
				{
					OutFinger = Finger;
					OutJoint = Joint;
					return true;
				}
			}
		}
		return false;
	}

	void RotateAbout(TArray<FTransform>& Comp, const TArray<int32>& Slots, const FVector& Pivot, const FQuat& Turn)
	{
		for (const int32 Slot : Slots)
		{
			FTransform& T = Comp[Slot];
			T.SetLocation(Pivot + Turn.RotateVector(T.GetLocation() - Pivot));
			T.SetRotation((Turn * T.GetRotation()).GetNormalized());
		}
	}
}

void UHawkeyeBodyAnimInstance::StepToward(const FWish& Wish, float DeltaSeconds)
{
	for (int32 Side = 0; Side < 2; ++Side)
	{
		State.Fist[Side] = StepAlpha(State.Fist[Side], Wish.bFist[Side] ? 1.f : 0.f, DeltaSeconds);
		State.Grip[Side] = StepAlpha(State.Grip[Side], Wish.bGrip[Side] ? 1.f : 0.f, DeltaSeconds);
		State.PropAxis[Side] = Wish.PropAxis[Side];
	}
	State.ArmRest = StepAlpha(State.ArmRest, Wish.ArmRest, DeltaSeconds, 0.2f);
}

void UHawkeyeBodyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	FWish Wish;
	if (const AThugCharacter* Thug = Cast<AThugCharacter>(GetOwningActor()))
	{
		Wish = Thug->GetBodyPoseWish();
	}
	StepToward(Wish, DeltaSeconds);
}

void FAnimNode_HawkeyeBodyPose::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	if (const UHawkeyeBodyAnimInstance* Instance = Cast<UHawkeyeBodyAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject()))
	{
		State = Instance->GetState();
	}
}

bool FAnimNode_HawkeyeBodyPose::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return bReady;
}

void FAnimNode_HawkeyeBodyPose::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	bReady = false;
	const FReferenceSkeleton& Reference = RequiredBones.GetReferenceSkeleton();
	const int32 Num = RequiredBones.GetCompactPoseNumBones();

	// Reference (bind) pose in mesh space for every bone in the compact pose, and each one's name.
	TArray<FTransform> Bind;
	TArray<FName> Names;
	TMap<FName, int32> ByName;
	Bind.SetNum(Num);
	Names.SetNum(Num);
	for (int32 I = 0; I < Num; ++I)
	{
		const FCompactPoseBoneIndex Compact(I);
		const FCompactPoseBoneIndex Parent = RequiredBones.GetParentBoneIndex(Compact);
		const FTransform& Local = RequiredBones.GetRefPoseTransform(Compact);
		Bind[I] = Parent.GetInt() == INDEX_NONE ? Local : Local * Bind[Parent.GetInt()];
		Names[I] = Reference.GetBoneName(RequiredBones.GetBoneIndicesArray()[I]);
		ByName.Add(Names[I], I);
	}
	auto Find = [&ByName](const FString& Name)
	{
		const int32* Found = ByName.Find(FName(*Name));
		return Found ? *Found : INDEX_NONE;
	};

	const int32 UpperL = Find(TEXT("upperarm_l"));
	const int32 UpperR = Find(TEXT("upperarm_r"));
	if (UpperL == INDEX_NONE || UpperR == INDEX_NONE)
	{
		return;
	}
	// The body's own axes from the reference pose: out along the shoulders, forward where the feet point.
	FVector Lateral = (Bind[UpperL].GetLocation() - Bind[UpperR].GetLocation());
	Lateral.Z = 0.0;
	Lateral = Lateral.GetSafeNormal();
	FVector Forward = FVector::CrossProduct(Lateral, FVector::UpVector).GetSafeNormal();
	const int32 Foot = Find(TEXT("foot_l"));
	const int32 Ball = Find(TEXT("ball_l"));
	if (Foot != INDEX_NONE && Ball != INDEX_NONE)
	{
		FVector Toes = Bind[Ball].GetLocation() - Bind[Foot].GetLocation();
		Toes.Z = 0.0;
		if (!Toes.IsNearlyZero())
		{
			Forward = Toes.GetSafeNormal();
		}
	}

	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		FSide& Side = Sides[SideIndex];
		Side = FSide();
		const FString Suffix = SideSuffix(SideIndex);
		const int32 Upper = Find(TEXT("upperarm_") + Suffix);
		const int32 Hand = Find(TEXT("hand_") + Suffix);
		const int32 Lower = Find(TEXT("lowerarm_") + Suffix);
		Side.Frame = ComputeHandFrame(Reference, SideIndex);
		if (Upper == INDEX_NONE || Hand == INDEX_NONE || Lower == INDEX_NONE || !Side.Frame.bValid)
		{
			continue;
		}
		Side.Outward = SideIndex == Left ? Lateral : -Lateral;
		Side.Forward = Forward;

		const FQuat HandBind = Bind[Hand].GetRotation();
		const FVector Along = HandBind.RotateVector(Side.Frame.Along);
		const FVector Palm = HandBind.RotateVector(Side.Frame.Palm);

		// Everything under the upper arm, in compact (parent first) order.
		TArray<int32> SlotOfBone;
		SlotOfBone.Init(INDEX_NONE, Num);
		for (int32 I = Upper; I < Num; ++I)
		{
			const FCompactPoseBoneIndex Parent = RequiredBones.GetParentBoneIndex(FCompactPoseBoneIndex(I));
			if (I != Upper && (Parent.GetInt() == INDEX_NONE || SlotOfBone[Parent.GetInt()] == INDEX_NONE))
			{
				continue;
			}
			FJoint Joint;
			Joint.Bone = FCompactPoseBoneIndex(I);
			Joint.ParentSlot = I == Upper ? INDEX_NONE : SlotOfBone[Parent.GetInt()];
			Joint.RefLocalRotation = RequiredBones.GetRefPoseTransform(Joint.Bone).GetRotation();
			const FString Name = Names[I].ToString();
			if (ParseFingerBone(Name, Joint.Finger, Joint.JointIndex) && Name.EndsWith(TEXT("_") + Suffix))
			{
				// The bone's own direction at the reference pose, toward its child along the finger (the last joint: the tip
				// bone, else the hand's own direction).
				int32 Child = Find(FString::Printf(TEXT("%s_%02d_%s"), FingerNames[Joint.Finger], Joint.JointIndex + 2, *Suffix));
				if (Child == INDEX_NONE)
				{
					Child = Find(FString::Printf(TEXT("%s_04_leaf_%s"), FingerNames[Joint.Finger], *Suffix));
				}
				FVector Dir = Child != INDEX_NONE ? (Bind[Child].GetLocation() - Bind[I].GetLocation()).GetSafeNormal() : Along;
				if (Dir.IsNearlyZero())
				{
					Dir = Along;
				}
				const FQuat ParentBind = Bind[Parent.GetInt()].GetRotation();
				const FVector Flex = FVector::CrossProduct(Dir, Palm).GetSafeNormal();
				// The thumb swings toward the fingers; the others' swing is zero in the table.
				double Sign = 1.0;
				if (Joint.Finger == static_cast<int32>(EFinger::Thumb))
				{
					Sign = FVector::DotProduct(FVector::CrossProduct(Palm, Dir), Along) >= 0.0 ? 1.0 : -1.0;
				}
				Joint.BindDir = Dir;
				Joint.BindRot = Bind[I].GetRotation();
				Joint.ParentBindRot = ParentBind;
				Joint.FlexAxis = ParentBind.UnrotateVector(Flex);
				Joint.SpreadAxis = ParentBind.UnrotateVector(Palm * Sign);
			}
			else
			{
				Joint.Finger = INDEX_NONE;
				Joint.JointIndex = INDEX_NONE;
			}
			SlotOfBone[I] = Side.Joints.Num();
			if (I == Hand)
			{
				Side.HandSlot = Side.Joints.Num();
			}
			if (I == Lower)
			{
				Side.LowerSlot = Side.Joints.Num();
			}
			Side.Joints.Add(Joint);
		}
		Side.bValid = Side.HandSlot != INDEX_NONE && Side.LowerSlot != INDEX_NONE;
		bReady = bReady || Side.bValid;
	}
}

void FAnimNode_HawkeyeBodyPose::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		if (Sides[SideIndex].bValid)
		{
			EvaluateSide(Sides[SideIndex], SideIndex, Output, OutBoneTransforms);
		}
	}
	OutBoneTransforms.Sort([](const FBoneTransform& A, const FBoneTransform& B) { return A.BoneIndex < B.BoneIndex; });
}

void FAnimNode_HawkeyeBodyPose::EvaluateSide(const FSide& Side, int32 SideIndex, FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms) const
{
	const int32 Count = Side.Joints.Num();
	TArray<FTransform> Comp;
	TArray<FTransform> Local;
	Comp.SetNum(Count);
	Local.SetNum(Count);
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		Comp[Slot] = Output.Pose.GetComponentSpaceTransform(Side.Joints[Slot].Bone);
		Local[Slot] = Output.Pose.GetLocalSpaceTransform(Side.Joints[Slot].Bone);
	}
	auto Slots = [Count](int32 From)
	{
		TArray<int32> Out;
		for (int32 Slot = From; Slot < Count; ++Slot)
		{
			Out.Add(Slot);
		}
		return Out;
	};

	// 1. The rest arm: upper arm down beside the thigh, forearm nearly straight on it.
	if (State.ArmRest > UE_KINDA_SMALL_NUMBER)
	{
		const FArmRest Rest = ArmRestDirections(FVector::UpVector, Side.Outward, Side.Forward);
		const FVector Shoulder = Comp[0].GetLocation();
		const FVector Elbow = Comp[Side.LowerSlot].GetLocation();
		const FQuat Upper = FQuat::Slerp(
			FQuat::Identity, FQuat::FindBetweenNormals((Elbow - Shoulder).GetSafeNormal(), Rest.Upper), State.ArmRest);
		RotateAbout(Comp, Slots(0), Shoulder, Upper);
		const FVector Elbow2 = Comp[Side.LowerSlot].GetLocation();
		const FVector Wrist = Comp[Side.HandSlot].GetLocation();
		const FQuat Lower = FQuat::Slerp(
			FQuat::Identity, FQuat::FindBetweenNormals((Wrist - Elbow2).GetSafeNormal(), Rest.Lower), State.ArmRest);
		RotateAbout(Comp, Slots(Side.LowerSlot), Elbow2, Lower);
	}

	// 2. A gripping wrist turns toward the prop's handle line, as far as a wrist goes.
	const float Grip = State.Grip[SideIndex];
	if (Grip > UE_KINDA_SMALL_NUMBER && !State.PropAxis[SideIndex].IsNearlyZero())
	{
		const FVector Axis = Output.AnimInstanceProxy->GetComponentTransform()
								 .InverseTransformVectorNoScale(State.PropAxis[SideIndex])
								 .GetSafeNormal();
		const FTransform& HandT = Comp[Side.HandSlot];
		const FVector Across = HandT.GetRotation().RotateVector(Side.Frame.Across);
		const FVector Wanted = FVector::DotProduct(Across, Axis) >= 0.0 ? Axis : -Axis;
		FQuat Turn = FQuat::FindBetweenNormals(Across, Wanted);
		const float Angle = FMath::RadiansToDegrees(Turn.GetAngle());
		if (Angle > MaxWristTurnDegrees)
		{
			Turn = FQuat::Slerp(FQuat::Identity, Turn, MaxWristTurnDegrees / Angle);
		}
		Turn = FQuat::Slerp(FQuat::Identity, Turn, Grip);
		RotateAbout(Comp, Slots(Side.HandSlot), HandT.GetLocation(), Turn);
	}

	// 3. Every finger joint from the table, written outright.
	FFingerAngles Table[5];
	for (int32 Finger = 0; Finger < 5; ++Finger)
	{
		Table[Finger] = Blend(static_cast<EFinger>(Finger), State.Fist[SideIndex], Grip);
	}
	for (int32 Slot = Side.HandSlot + 1; Slot < Count; ++Slot)
	{
		const FJoint& Joint = Side.Joints[Slot];
		FTransform Own = Local[Slot];
		if (Joint.Finger != INDEX_NONE)
		{
			const FFingerAngles& A = Table[Joint.Finger];
			FQuat Rotation = FQuat(Joint.FlexAxis, FMath::DegreesToRadians(A.Curl[Joint.JointIndex])) * Joint.RefLocalRotation;
			if (Joint.JointIndex == 0 && !FMath::IsNearlyZero(A.Spread))
			{
				Rotation = FQuat(Joint.SpreadAxis, FMath::DegreesToRadians(A.Spread)) * Rotation;
			}
			const float ClosedWeight = FMath::Max(State.Fist[SideIndex], Grip);
			if (Joint.Finger == static_cast<int32>(EFinger::Thumb) && ClosedWeight > UE_KINDA_SMALL_NUMBER)
			{
				// A closed thumb is aimed (it lies across the fingers), in the hand's axes as the pose has turned them.
				const FQuat HandRot = Comp[Side.HandSlot].GetRotation();
				const float GripShare = Grip / FMath::Max(State.Fist[SideIndex] + Grip, UE_KINDA_SMALL_NUMBER);
				const FVector W = FMath::Lerp(ClosedThumbDirection(EHandPose::Fist, Joint.JointIndex),
					ClosedThumbDirection(EHandPose::Grip, Joint.JointIndex), static_cast<double>(GripShare));
				const FVector Wanted = (HandRot.RotateVector(Side.Frame.Along) * W.X + HandRot.RotateVector(Side.Frame.Across) * W.Y
										   + HandRot.RotateVector(Side.Frame.Palm) * W.Z)
										   .GetSafeNormal();
				const FQuat ParentNow = Comp[Joint.ParentSlot].GetRotation();
				const FQuat Carried = ParentNow * Joint.ParentBindRot.Inverse();
				const FQuat Aimed = FQuat::FindBetweenNormals(Carried.RotateVector(Joint.BindDir), Wanted) * Carried * Joint.BindRot;
				Rotation = FQuat::Slerp(Rotation, ParentNow.Inverse() * Aimed, FMath::Min(ClosedWeight, 1.f));
			}
			Own.SetRotation(Rotation.GetNormalized());
		}
		Comp[Slot] = Own * Comp[Joint.ParentSlot];
	}

	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		OutBoneTransforms.Add(FBoneTransform(Side.Joints[Slot].Bone, Comp[Slot]));
	}
}
