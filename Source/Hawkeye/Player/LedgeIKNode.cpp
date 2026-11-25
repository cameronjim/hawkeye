#include "Player/LedgeIKNode.h"

#include "Animation/AnimInstanceProxy.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Hawkeye.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "ReferenceSkeleton.h"

using namespace HawkeyeBodyPose;

namespace
{
	/** How much of the bind pose's finger fan the hang's grip closes (1: parallel to the middle finger). */
	constexpr double ClosedFingers = 0.95;

	const TCHAR* LedgeFingerNames[5] = {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};

	/** The finger and joint (0..2) a bone name is, from `<finger>_0<n>_<side>`; false for anything else (the tip bone included). */
	bool ParseLedgeFingerBone(const FString& Name, int32& OutFinger, int32& OutJoint)
	{
		for (int32 Finger = 0; Finger < 5; ++Finger)
		{
			for (int32 Joint = 0; Joint < 3; ++Joint)
			{
				if (Name.StartsWith(FString::Printf(TEXT("%s_%02d_"), LedgeFingerNames[Finger], Joint + 1)))
				{
					OutFinger = Finger;
					OutJoint = Joint;
					return true;
				}
			}
		}
		return false;
	}

	/** Slots (the list's own indices, parent first) of Ancestor and everything under it. */
	TArray<int32> SlotsFrom(const TArray<int32>& ParentSlot, int32 Ancestor)
	{
		TArray<int32> Out;
		if (Ancestor == INDEX_NONE)
		{
			return Out;
		}
		for (int32 Slot = Ancestor; Slot < ParentSlot.Num(); ++Slot)
		{
			if (Slot == Ancestor || (ParentSlot[Slot] != INDEX_NONE && Out.Contains(ParentSlot[Slot])))
			{
				Out.Add(Slot);
			}
		}
		return Out;
	}

	void RotateSlotsAbout(TArray<FTransform>& Comp, const TArray<int32>& Slots, const FVector& Pivot, const FQuat& Turn)
	{
		for (const int32 Slot : Slots)
		{
			FTransform& T = Comp[Slot];
			T.SetLocation(Pivot + Turn.RotateVector(T.GetLocation() - Pivot));
			T.SetRotation((Turn * T.GetRotation()).GetNormalized());
		}
	}
}

void UHawkeyeKateBodyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	LedgePose = FLedgeBodyPose();
	if (const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(GetOwningActor()))
	{
		if (const UParkourComponent* Parkour = Kate->GetParkourComponent())
		{
			Parkour->GetLedgeBodyPose(LedgePose);
		}
	}
}

void FAnimNode_HawkeyeLedgeIK::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	if (const UHawkeyeKateBodyAnimInstance* Instance = Cast<UHawkeyeKateBodyAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject()))
	{
		Pose = Instance->GetLedgePose();
	}
	else
	{
		Pose = FLedgeBodyPose();
	}
}

bool FAnimNode_HawkeyeLedgeIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return bReady;
}

void FAnimNode_HawkeyeLedgeIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	bReady = false;
	Bones.Reset();
	ParentSlot.Reset();
	Arms[0] = FArm();
	Arms[1] = FArm();
	Legs[0] = FLeg();
	Legs[1] = FLeg();
	const FReferenceSkeleton& Reference = RequiredBones.GetReferenceSkeleton();
	const int32 Num = RequiredBones.GetCompactPoseNumBones();

	// Reference (bind) pose in mesh space for every bone in the compact pose, and each one's name (as the body pose node does).
	TArray<FTransform> Bind;
	TArray<FName> Names;
	TMap<FName, int32> ByName;
	Bind.SetNum(Num);
	Names.SetNum(Num);
	for (int32 I = 0; I < Num; ++I)
	{
		const FCompactPoseBoneIndex Compact(I);
		const FCompactPoseBoneIndex Parent = RequiredBones.GetParentBoneIndex(Compact);
		const FTransform& LocalRef = RequiredBones.GetRefPoseTransform(Compact);
		Bind[I] = Parent.GetInt() == INDEX_NONE ? LocalRef : LocalRef * Bind[Parent.GetInt()];
		Names[I] = Reference.GetBoneName(RequiredBones.GetBoneIndicesArray()[I]);
		ByName.Add(Names[I], I);
	}
	auto FindCompact = [&ByName](const FString& Name)
	{
		const int32* Found = ByName.Find(FName(*Name));
		return Found ? *Found : INDEX_NONE;
	};

	// The pelvis and everything under it: the body that is moved as one.
	const int32 Pelvis = FindCompact(TEXT("pelvis"));
	if (Pelvis == INDEX_NONE)
	{
		return;
	}
	TArray<int32> SlotOfCompact;
	SlotOfCompact.Init(INDEX_NONE, Num);
	for (int32 I = Pelvis; I < Num; ++I)
	{
		const FCompactPoseBoneIndex Parent = RequiredBones.GetParentBoneIndex(FCompactPoseBoneIndex(I));
		if (I != Pelvis && (Parent.GetInt() == INDEX_NONE || SlotOfCompact[Parent.GetInt()] == INDEX_NONE))
		{
			continue;
		}
		SlotOfCompact[I] = Bones.Num();
		ParentSlot.Add(I == Pelvis ? INDEX_NONE : SlotOfCompact[Parent.GetInt()]);
		Bones.Add(FCompactPoseBoneIndex(I));
	}
	auto FindSlot = [&](const FString& Name)
	{
		const int32 Compact = FindCompact(Name);
		return Compact == INDEX_NONE ? INDEX_NONE : SlotOfCompact[Compact];
	};

	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const FString Suffix = SideSuffix(SideIndex);
		FArm& Arm = Arms[SideIndex];
		Arm.Upper = FindSlot(TEXT("upperarm_") + Suffix);
		Arm.Lower = FindSlot(TEXT("lowerarm_") + Suffix);
		Arm.Hand = FindSlot(TEXT("hand_") + Suffix);
		Arm.Frame = ComputeHandFrame(Reference, SideIndex);
		Arm.bValid = Arm.Upper != INDEX_NONE && Arm.Lower != INDEX_NONE && Arm.Hand != INDEX_NONE;
		if (Arm.bValid)
		{
			Arm.FromUpper = SlotsFrom(ParentSlot, Arm.Upper);
			Arm.FromLower = SlotsFrom(ParentSlot, Arm.Lower);
			Arm.FromHand = SlotsFrom(ParentSlot, Arm.Hand);
			const int32 HandCompact = Bones[Arm.Hand].GetInt();
			const FQuat HandBind = Bind[HandCompact].GetRotation();
			const FVector Along = Arm.Frame.bValid ? HandBind.RotateVector(Arm.Frame.Along) : FVector::ForwardVector;
			const FVector Palm = Arm.Frame.bValid ? HandBind.RotateVector(Arm.Frame.Palm) : FVector::UpVector;
			for (const int32 Slot : Arm.FromHand)
			{
				if (Slot == Arm.Hand)
				{
					continue;
				}
				const int32 I = Bones[Slot].GetInt();
				FHandJoint Joint;
				Joint.Slot = Slot;
				Joint.ParentSlot = ParentSlot[Slot];
				Joint.RefLocalRotation = RequiredBones.GetRefPoseTransform(Bones[Slot]).GetRotation();
				const FString Name = Names[I].ToString();
				if (ParseLedgeFingerBone(Name, Joint.Finger, Joint.Joint) && Name.EndsWith(TEXT("_") + Suffix))
				{
					// The bone's own direction at the reference pose, toward its child along the finger (the last joint: the tip
					// bone, else the hand's own direction); it curls about that direction crossed with the palm's.
					int32 Child = FindCompact(FString::Printf(TEXT("%s_%02d_%s"), LedgeFingerNames[Joint.Finger], Joint.Joint + 2, *Suffix));
					if (Child == INDEX_NONE)
					{
						Child = FindCompact(FString::Printf(TEXT("%s_04_leaf_%s"), LedgeFingerNames[Joint.Finger], *Suffix));
					}
					FVector Dir = Child != INDEX_NONE ? (Bind[Child].GetLocation() - Bind[I].GetLocation()).GetSafeNormal() : Along;
					if (Dir.IsNearlyZero())
					{
						Dir = Along;
					}
					const FCompactPoseBoneIndex ParentCompact = RequiredBones.GetParentBoneIndex(Bones[Slot]);
					const FQuat ParentBind = Bind[ParentCompact.GetInt()].GetRotation();
					Joint.FlexAxis = ParentBind.UnrotateVector(FVector::CrossProduct(Dir, Palm).GetSafeNormal());
					// The body's bind pose fans its fingers (a T-pose hand): the angle about the palm's normal from this joint's
					// direction to the middle finger's same joint, which the grip takes out so the four close side by side.
					if (Joint.Finger >= 1)
					{
						const int32 MiddleHere = FindCompact(FString::Printf(TEXT("middle_%02d_%s"), Joint.Joint + 1, *Suffix));
						int32 MiddleChild = FindCompact(FString::Printf(TEXT("middle_%02d_%s"), Joint.Joint + 2, *Suffix));
						if (MiddleChild == INDEX_NONE)
						{
							MiddleChild = FindCompact(FString::Printf(TEXT("middle_04_leaf_%s"), *Suffix));
						}
						if (MiddleHere != INDEX_NONE && MiddleChild != INDEX_NONE)
						{
							const FVector MiddleDir = (Bind[MiddleChild].GetLocation() - Bind[MiddleHere].GetLocation()).GetSafeNormal();
							const FVector Mine = (Dir - Palm * FVector::DotProduct(Dir, Palm)).GetSafeNormal();
							const FVector Theirs = (MiddleDir - Palm * FVector::DotProduct(MiddleDir, Palm)).GetSafeNormal();
							if (!Mine.IsNearlyZero() && !Theirs.IsNearlyZero())
							{
								Joint.AdductAxis = ParentBind.UnrotateVector(Palm);
								Joint.AdductRadians = static_cast<float>(FMath::Atan2(
									FVector::DotProduct(FVector::CrossProduct(Mine, Theirs), Palm), FVector::DotProduct(Mine, Theirs)));
							}
						}
					}
				}
				else
				{
					Joint.Finger = INDEX_NONE;
					Joint.Joint = INDEX_NONE;
				}
				Arm.Joints.Add(Joint);
			}
		}
		bReady = bReady || Arm.bValid;

		FLeg& Leg = Legs[SideIndex];
		Leg.Thigh = FindSlot(TEXT("thigh_") + Suffix);
		Leg.Calf = FindSlot(TEXT("calf_") + Suffix);
		Leg.Foot = FindSlot(TEXT("foot_") + Suffix);
		Leg.Ball = FindSlot(TEXT("ball_") + Suffix);
		Leg.bValid = Leg.Thigh != INDEX_NONE && Leg.Calf != INDEX_NONE && Leg.Foot != INDEX_NONE && Leg.Ball != INDEX_NONE;
		if (Leg.bValid)
		{
			Leg.FromThigh = SlotsFrom(ParentSlot, Leg.Thigh);
			Leg.FromCalf = SlotsFrom(ParentSlot, Leg.Calf);
			Leg.FromFoot = SlotsFrom(ParentSlot, Leg.Foot);
		}
	}
}

void FAnimNode_HawkeyeLedgeIK::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	if (!bReady || !Pose.IsActive())
	{
		return;
	}
	const FTransform& ToWorld = Output.AnimInstanceProxy->GetComponentTransform();
	const float Scale = FMath::Max(static_cast<float>(ToWorld.GetScale3D().X), 0.01f);
	const FHawkeyeLedgeBodySettings& Tuning = Pose.Tuning;

	const int32 Count = Bones.Num();
	TArray<FTransform> Comp;
	TArray<FTransform> Local;
	Comp.SetNum(Count);
	Local.SetNum(Count);
	TArray<int32> All;
	All.Reserve(Count);
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		Comp[Slot] = Output.Pose.GetComponentSpaceTransform(Bones[Slot]);
		Local[Slot] = Output.Pose.GetLocalSpaceTransform(Bones[Slot]);
		All.Add(Slot);
	}

	const FVector NormalC = ToWorld.InverseTransformVectorNoScale(Pose.Normal).GetSafeNormal();
	const FVector UpC = ToWorld.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();

	// 1. The body hangs from its hands: swung about their line, the shoulders brought under them.
	if (Pose.BodyAlpha > 0.001f && Arms[0].bValid && Arms[1].bValid && Pose.HandAlpha[0] > 0.001f && Pose.HandAlpha[1] > 0.001f)
	{
		const FVector HandMidW = 0.5 * (Pose.Hand[0] + Pose.Hand[1]);
		const FVector HandMidC = ToWorld.InverseTransformPosition(HandMidW);
		const FVector SwingAxis = FVector::CrossProduct(NormalC, UpC).GetSafeNormal();
		const FQuat SwingC = SwingAxis.IsNearlyZero()
			? FQuat::Identity
			: FQuat(SwingAxis, FMath::DegreesToRadians(Pose.SwingDegrees * Pose.BodyAlpha));
		RotateSlotsAbout(Comp, All, HandMidC, SwingC);

		const FVector ShoulderMidC = 0.5 * (Comp[Arms[0].Upper].GetLocation() + Comp[Arms[1].Upper].GetLocation());
		float ArmLengthC = 0.f;
		for (const FArm& Arm : Arms)
		{
			ArmLengthC += 0.5f * static_cast<float>((Comp[Arm.Lower].GetLocation() - Comp[Arm.Upper].GetLocation()).Size()
				+ (Comp[Arm.Hand].GetLocation() - Comp[Arm.Lower].GetLocation()).Size());
		}
		const FVector DesiredW = HawkeyeLedge::ShoulderTarget(HandMidW, Pose.Normal, ArmLengthC * Scale, Tuning);
		FVector DesiredC = ToWorld.InverseTransformPosition(DesiredW);
		DesiredC = HandMidC + SwingC.RotateVector(DesiredC - HandMidC);
		FVector Shift = (DesiredC - ShoulderMidC) * Pose.BodyAlpha;
		const double MaxShift = Tuning.MaxBodyShift / Scale;
		if (Shift.Size() > MaxShift)
		{
			Shift = Shift.GetSafeNormal() * MaxShift;
		}
		for (FTransform& T : Comp)
		{
			T.SetLocation(T.GetLocation() + Shift);
		}
	}

	// 2. The hands onto the edge, the elbows out and down.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		SolveArm(Side, Comp, Local, ToWorld, Scale, NormalC, UpC);
	}

	// 3. The feet braced on the wall, only while the body is near it (a swung-out body has no wall under its toes).
	const float Near = 1.f - FMath::SmoothStep(2.f, 12.f, Pose.SwingDegrees);
	const float FootWeight = Pose.FootAlpha * Near;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		SolveLeg(Side, Comp, ToWorld, Scale, NormalC, UpC, FootWeight);
	}

	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		OutBoneTransforms.Add(FBoneTransform(Bones[Slot], Comp[Slot]));
	}
	// Parent first is index order in a compact pose, but the engine wants it sorted: do not rely on it.
	OutBoneTransforms.Sort([](const FBoneTransform& A, const FBoneTransform& B) { return A.BoneIndex < B.BoneIndex; });
}

void FAnimNode_HawkeyeLedgeIK::SolveArm(int32 Side, TArray<FTransform>& Comp, const TArray<FTransform>& Local, const FTransform& ToWorld,
	float Scale, const FVector& NormalC, const FVector& UpC) const
{
	const FArm& Arm = Arms[Side];
	if (!Arm.bValid || Pose.HandAlpha[Side] <= 0.001f)
	{
		return;
	}
	const FHawkeyeLedgeBodySettings& Tuning = Pose.Tuning;
	const FVector ShoulderC = Comp[Arm.Upper].GetLocation();
	const FVector ElbowC = Comp[Arm.Lower].GetLocation();
	const FVector WristC = Comp[Arm.Hand].GetLocation();
	const float UpperLength = static_cast<float>((ElbowC - ShoulderC).Size());
	const float LowerLength = static_cast<float>((WristC - ElbowC).Size());
	const FVector TargetC = ToWorld.InverseTransformPosition(Pose.Hand[Side]);
	// A target further than the arm goes is the clip's to reach, not ours to stretch toward.
	const float Reach = HawkeyeLedge::ReachWeight(
		static_cast<float>((TargetC - ShoulderC).Size()) * Scale, (UpperLength + LowerLength) * Scale, Tuning);
	const float ArmAlpha = FMath::Clamp(Pose.HandAlpha[Side] * Reach, 0.f, 1.f);
	if (ArmAlpha <= 0.001f)
	{
		return;
	}
	const FVector Goal = FMath::Lerp(WristC, TargetC, static_cast<double>(ArmAlpha));

	// The elbow goes out to this side, away from the wall and a little down.
	const FVector Right = FVector::CrossProduct(UpC, -NormalC).GetSafeNormal();
	const FVector Outward = Side == HandLeft ? -Right : Right;
	const FVector Pole = 0.5 * (ShoulderC + Goal) + Outward * (30.f / Scale) + NormalC * (18.f / Scale) - UpC * (14.f / Scale);
	const FTwoBoneSolution Solution = HawkeyeLedge::SolveTwoBone(ShoulderC, UpperLength, LowerLength, Goal, Pole);

	// The upper arm turns onto the elbow's place, the forearm onto the hand's.
	const FQuat UpperTurn = FQuat::FindBetweenNormals((ElbowC - ShoulderC).GetSafeNormal(), (Solution.Joint - ShoulderC).GetSafeNormal());
	RotateSlotsAbout(Comp, Arm.FromUpper, ShoulderC, UpperTurn);
	const FVector Elbow2 = Comp[Arm.Lower].GetLocation();
	const FVector Wrist2 = Comp[Arm.Hand].GetLocation();
	const FQuat LowerTurn = FQuat::FindBetweenNormals((Wrist2 - Elbow2).GetSafeNormal(), (Solution.End - Elbow2).GetSafeNormal());
	RotateSlotsAbout(Comp, Arm.FromLower, Elbow2, LowerTurn);

	if (!Arm.Frame.bValid)
	{
		return;
	}
	// The hand: fingers into the wall tipped down, the palm on the top.
	FVector Fingers;
	FVector Palm;
	HawkeyeLedge::HandAxes(Pose.Normal, Tuning.FingerDownDegrees, Fingers, Palm);
	const FQuat Wanted = HawkeyeLedge::AlignFrames(Arm.Frame.Along, Arm.Frame.Palm,
		ToWorld.InverseTransformVectorNoScale(Fingers).GetSafeNormal(), ToWorld.InverseTransformVectorNoScale(Palm).GetSafeNormal());
	FQuat HandTurn = Wanted * Comp[Arm.Hand].GetRotation().Inverse();
	HandTurn = FQuat::Slerp(FQuat::Identity, HandTurn, static_cast<double>(ArmAlpha));
	RotateSlotsAbout(Comp, Arm.FromHand, Comp[Arm.Hand].GetLocation(), HandTurn);

	// The fingers from the authored Hang table by the grip, each joint rebuilt on its own local so the turn carries it.
	const float Grip = FMath::Clamp(Pose.Grip[Side] * ArmAlpha, 0.f, 1.f);
	for (const FHandJoint& Joint : Arm.Joints)
	{
		FTransform Own = Local[Joint.Slot];
		if (Joint.Finger != INDEX_NONE && Grip > UE_KINDA_SMALL_NUMBER)
		{
			const FFingerAngles Angles = BlendHang(static_cast<EFinger>(Joint.Finger), 1.f);
			// Closed up first (the fingers side by side), then curled about the finger's own flexion axis.
			const FQuat Close = Joint.AdductAxis.IsNearlyZero()
				? FQuat::Identity
				: FQuat(Joint.AdductAxis, static_cast<double>(Joint.AdductRadians) * ClosedFingers);
			const FQuat Authored =
				Close * FQuat(Joint.FlexAxis, FMath::DegreesToRadians(Angles.Curl[Joint.Joint])) * Joint.RefLocalRotation;
			Own.SetRotation(FQuat::Slerp(Own.GetRotation(), Authored, static_cast<double>(Grip)).GetNormalized());
		}
		Comp[Joint.Slot] = Own * Comp[Joint.ParentSlot];
	}
}

void FAnimNode_HawkeyeLedgeIK::SolveLeg(int32 Side, TArray<FTransform>& Comp, const FTransform& ToWorld, float Scale,
	const FVector& NormalC, const FVector& UpC, float FootWeight) const
{
	const FLeg& Leg = Legs[Side];
	if (!Leg.bValid || FootWeight <= 0.001f)
	{
		return;
	}
	const FVector HipC = Comp[Leg.Thigh].GetLocation();
	const FVector KneeC = Comp[Leg.Calf].GetLocation();
	const FVector AnkleC = Comp[Leg.Foot].GetLocation();
	const FVector BallW = ToWorld.TransformPosition(Comp[Leg.Ball].GetLocation());
	const FVector WantedBallW = HawkeyeLedge::BallOnPlane(BallW, Pose.Normal, Pose.PlaneD, Pose.Tuning.BallFromWall);
	// The foot is moved as it is (not turned) by what the ball needs to be on the plane.
	const FVector Delta = ToWorld.InverseTransformVector(WantedBallW - BallW);
	const FVector Goal = AnkleC + Delta * static_cast<double>(FootWeight);
	const float UpperLength = static_cast<float>((KneeC - HipC).Size());
	const float LowerLength = static_cast<float>((AnkleC - KneeC).Size());
	// Knees toward the wall, a touch outward.
	const FVector Right = FVector::CrossProduct(UpC, -NormalC).GetSafeNormal();
	const FVector Outward = Side == HandLeft ? -Right : Right;
	const FVector Pole = 0.5 * (HipC + Goal) - NormalC * (30.f / Scale) + Outward * (6.f / Scale);
	const FTwoBoneSolution Solution = HawkeyeLedge::SolveTwoBone(HipC, UpperLength, LowerLength, Goal, Pole);

	const FQuat FootBefore = Comp[Leg.Foot].GetRotation();
	const FQuat ThighTurn = FQuat::FindBetweenNormals((KneeC - HipC).GetSafeNormal(), (Solution.Joint - HipC).GetSafeNormal());
	RotateSlotsAbout(Comp, Leg.FromThigh, HipC, ThighTurn);
	const FVector Knee2 = Comp[Leg.Calf].GetLocation();
	const FVector Ankle2 = Comp[Leg.Foot].GetLocation();
	const FQuat CalfTurn = FQuat::FindBetweenNormals((Ankle2 - Knee2).GetSafeNormal(), (Solution.End - Knee2).GetSafeNormal());
	RotateSlotsAbout(Comp, Leg.FromCalf, Knee2, CalfTurn);
	// The foot stays as flat as it was: only the leg turned.
	const FQuat FootTurn = FootBefore * Comp[Leg.Foot].GetRotation().Inverse();
	RotateSlotsAbout(Comp, Leg.FromFoot, Comp[Leg.Foot].GetLocation(), FootTurn);
}
