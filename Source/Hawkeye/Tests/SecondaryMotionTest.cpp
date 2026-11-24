#include "Animation/Skeleton.h"
#include "Misc/AutomationTest.h"
#include "Player/SecondaryMotion.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The rules of the body look's secondary-motion chains (SecondaryMotion.h; gameplay-semantics.md does not list them, they
 * are the builder's rules, claude-docs/animation.md "Secondary motion"): which bones a chain simulates, what the builder
 * refuses to hand the Kawaii Physics plugin, and where a collider sits in a bone's own axes. Content-free: the skeletons
 * are built here, the plugin's node is not touched.
 */
namespace HawkeyeSecondaryTest
{
	struct FBone
	{
		FName Name;
		FName Parent;
		FVector Offset;
		FQuat Rotation = FQuat::Identity;
	};

	/** A skeleton shaped like Kate's: root, Head with the ponytail down its back (ponytail_01..05, 05 a leaf) and a side
	 * lock off ponytail_02, spine_03 with the quiver (quiver_01..02) on it. cm. */
	struct FKateSkeleton
	{
		TStrongObjectPtr<USkeleton> Skeleton;

		explicit FKateSkeleton(const TArray<FBone>& Overrides = TArray<FBone>())
		{
			Skeleton.Reset(NewObject<USkeleton>(GetTransientPackage()));
			TArray<FBone> Bones = {
				{TEXT("root"), NAME_None, FVector::ZeroVector},
				{TEXT("spine_03"), TEXT("root"), FVector(0., 0., 130.)},
				{TEXT("neck_01"), TEXT("spine_03"), FVector(0., 0., 15.)},
				{TEXT("Head"), TEXT("neck_01"), FVector(0., 0., 7.)},
				{TEXT("ponytail_01"), TEXT("Head"), FVector(0., -9., 8.)},
				{TEXT("ponytail_02"), TEXT("ponytail_01"), FVector(0., -2., -3.)},
				{TEXT("ponytail_03"), TEXT("ponytail_02"), FVector(0., -1., -6.)},
				{TEXT("ponytail_04"), TEXT("ponytail_03"), FVector(0., -1., -7.)},
				{TEXT("ponytail_05"), TEXT("ponytail_04"), FVector(0., -0.5, -2.)},
				{TEXT("side_lock"), TEXT("ponytail_02"), FVector(3., 0., -3.)},
				{TEXT("quiver_01"), TEXT("spine_03"), FVector(-12., -9., 10.)},
				{TEXT("quiver_02"), TEXT("quiver_01"), FVector(0., 0., -43.)},
			};
			for (const FBone& Override : Overrides)
			{
				for (FBone& Bone : Bones)
				{
					if (Bone.Name == Override.Name)
					{
						Bone = Override;
					}
				}
			}
			FReferenceSkeletonModifier Modifier(Skeleton.Get());
			for (const FBone& Bone : Bones)
			{
				const int32 Parent = Bone.Parent.IsNone() ? INDEX_NONE : Modifier.FindBoneIndex(Bone.Parent);
				Modifier.Add(FMeshBoneInfo(Bone.Name, Bone.Name.ToString(), Parent), FTransform(Bone.Rotation, Bone.Offset));
			}
		}

		const FReferenceSkeleton& Reference() const { return Skeleton->GetReferenceSkeleton(); }
	};

	static FHawkeyeDynamicsChain Ponytail()
	{
		FHawkeyeDynamicsChain Chain;
		Chain.BoundBone = TEXT("ponytail_01");
		Chain.ChainEnd = TEXT("ponytail_05");
		FHawkeyeDynamicsCollider Head;
		Head.Bone = TEXT("Head");
		Head.Radius = 6.5f;
		Head.Offset = FVector(0., 0., 8.);
		FHawkeyeDynamicsCollider Neck;
		Neck.Bone = TEXT("neck_01");
		Neck.Radius = 5.f;
		Neck.Length = 6.f;
		Chain.Colliders = {Head, Neck};
		return Chain;
	}

	static FHawkeyeDynamicsChain Quiver()
	{
		FHawkeyeDynamicsChain Chain;
		Chain.BoundBone = TEXT("quiver_01");
		Chain.ChainEnd = TEXT("quiver_02");
		Chain.Stiffness = 0.6f;
		Chain.LimitAngleDegrees = 12.f;
		return Chain;
	}

	/** Bone names, space separated, for comparing a list in one go. */
	static FString Join(const TArray<FName>& Names)
	{
		FString Out;
		for (const FName& Name : Names)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + Name.ToString();
		}
		return Out;
	}

	static FString Names(const FReferenceSkeleton& Reference, const TArray<int32>& Bones)
	{
		TArray<FName> Out;
		for (const int32 Bone : Bones)
		{
			Out.Add(Reference.GetBoneName(Bone));
		}
		return Join(Out);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSecondarySimulatedBones, "Hawkeye.SecondaryMotion.SimulatedBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSecondarySimulatedBones::RunTest(const FString& Parameters)
{
	HawkeyeSecondaryTest::FKateSkeleton Kate;
	const FReferenceSkeleton& Reference = Kate.Reference();
	TestEqual(TEXT("The skeleton was built"), Reference.GetNum(), 12);

	// With a ChainEnd: the path from the root down to it, none of the side lock.
	const FHawkeyeDynamicsChain Ponytail = HawkeyeSecondaryTest::Ponytail();
	TestEqual(TEXT("The ponytail is five bones"), HawkeyeSecondary::SimulatedBones(Reference, Ponytail).Num(), 5);
	TestEqual(TEXT("Parents come before children"),
		HawkeyeSecondaryTest::Names(Reference, HawkeyeSecondary::SimulatedBones(Reference, Ponytail)),
		FString(TEXT("ponytail_01 ponytail_02 ponytail_03 ponytail_04 ponytail_05")));
	TestEqual(TEXT("The side lock is the one bone left out"),
		HawkeyeSecondaryTest::Join(HawkeyeSecondary::ExcludedBones(Reference, Ponytail)), FString(TEXT("side_lock")));

	// The quiver is two.
	const FHawkeyeDynamicsChain Quiver = HawkeyeSecondaryTest::Quiver();
	TestEqual(TEXT("The quiver is two bones"), HawkeyeSecondary::SimulatedBones(Reference, Quiver).Num(), 2);
	TestEqual(TEXT("The quiver leaves nothing out"), HawkeyeSecondary::ExcludedBones(Reference, Quiver).Num(), 0);

	// No ChainEnd: the whole subtree, the side lock too, and nothing excluded.
	FHawkeyeDynamicsChain Whole = Ponytail;
	Whole.ChainEnd = NAME_None;
	TestEqual(TEXT("No end takes the subtree"), HawkeyeSecondary::SimulatedBones(Reference, Whole).Num(), 6);
	TestEqual(TEXT("...and excludes none"), HawkeyeSecondary::ExcludedBones(Reference, Whole).Num(), 0);

	// An end part way down: the bones under it are left out.
	FHawkeyeDynamicsChain Short = Ponytail;
	Short.ChainEnd = TEXT("ponytail_03");
	TestEqual(TEXT("An end at ponytail_03 is three bones"), HawkeyeSecondary::SimulatedBones(Reference, Short).Num(), 3);
	const TArray<FName> Excluded = HawkeyeSecondary::ExcludedBones(Reference, Short);
	TestTrue(TEXT("...and leaves ponytail_04 out"), Excluded.Contains(TEXT("ponytail_04")));
	TestTrue(TEXT("...and the side lock"), Excluded.Contains(TEXT("side_lock")));
	TestFalse(TEXT("...but not the bones it simulates"), Excluded.Contains(TEXT("ponytail_03")));

	// Chains that are not on the skeleton simulate nothing.
	FHawkeyeDynamicsChain NotThere = Ponytail;
	NotThere.BoundBone = TEXT("plait_01");
	TestEqual(TEXT("A missing root simulates nothing"), HawkeyeSecondary::SimulatedBones(Reference, NotThere).Num(), 0);
	FHawkeyeDynamicsChain Elsewhere = Ponytail;
	Elsewhere.ChainEnd = TEXT("quiver_02");
	TestEqual(TEXT("An end that is not under the root simulates nothing"),
		HawkeyeSecondary::SimulatedBones(Reference, Elsewhere).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSecondaryProblems, "Hawkeye.SecondaryMotion.RejectsWhatItCannotRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSecondaryProblems::RunTest(const FString& Parameters)
{
	HawkeyeSecondaryTest::FKateSkeleton Kate;
	const FReferenceSkeleton& Reference = Kate.Reference();
	const FHawkeyeDynamicsChain Ponytail = HawkeyeSecondaryTest::Ponytail();

	TestEqual(TEXT("Kate's ponytail is fine"), HawkeyeSecondary::DescribeProblem(Reference, Ponytail), FString());
	TestEqual(TEXT("Kate's quiver is fine"),
		HawkeyeSecondary::DescribeProblem(Reference, HawkeyeSecondaryTest::Quiver()), FString());
	TestEqual(TEXT("The defaults are fine"),
		HawkeyeSecondary::DescribeProblem(Reference, [&Ponytail]
			{
				FHawkeyeDynamicsChain Defaults;
				Defaults.BoundBone = Ponytail.BoundBone;
				return Defaults;
			}()),
		FString());

	const auto Rejects = [this, &Reference](const TCHAR* What, const FHawkeyeDynamicsChain& Chain)
	{
		TestFalse(What, HawkeyeSecondary::DescribeProblem(Reference, Chain).IsEmpty());
	};
	FHawkeyeDynamicsChain Chain = Ponytail;
	Chain.BoundBone = NAME_None;
	Rejects(TEXT("No bone"), Chain);
	Chain = Ponytail;
	Chain.BoundBone = TEXT("plait_01");
	Rejects(TEXT("A bone the skeleton lacks"), Chain);
	Chain = Ponytail;
	Chain.ChainEnd = TEXT("quiver_02");
	Rejects(TEXT("An end that is not under the root"), Chain);
	Chain = Ponytail;
	Chain.BoundBone = TEXT("ponytail_05");
	Chain.ChainEnd = NAME_None;
	Rejects(TEXT("A chain of one bone has nothing to swing"), Chain);

	Chain = Ponytail;
	Chain.Damping = 1.5f;
	Rejects(TEXT("Damping over 1"), Chain);
	Chain = Ponytail;
	Chain.Stiffness = -0.1f;
	Rejects(TEXT("Negative stiffness"), Chain);
	Chain = Ponytail;
	Chain.LimitAngleDegrees = 200.f;
	Rejects(TEXT("A limit past 180 degrees"), Chain);
	Chain = Ponytail;
	Chain.Radius = std::numeric_limits<float>::quiet_NaN();
	Rejects(TEXT("A NaN radius"), Chain);
	Chain = Ponytail;
	Chain.GravityScale = std::numeric_limits<float>::infinity();
	Rejects(TEXT("An infinite gravity"), Chain);
	Chain = Ponytail;
	Chain.TargetFramerate = 0;
	Rejects(TEXT("A step rate of zero (a division by it)"), Chain);

	Chain = Ponytail;
	Chain.Colliders[0].Bone = TEXT("Skull");
	Rejects(TEXT("A collider on a missing bone"), Chain);
	Chain = Ponytail;
	Chain.Colliders[0].Radius = 0.f;
	Rejects(TEXT("A collider with no radius"), Chain);
	Chain = Ponytail;
	Chain.Colliders[1].Axis = FVector::ZeroVector;
	Rejects(TEXT("A capsule with no axis"), Chain);

	// The reference-pose cases the engine's AnimDynamics turned into NaNs: a bone on its parent, a zero scale.
	{
		HawkeyeSecondaryTest::FKateSkeleton OnParent({{TEXT("ponytail_03"), TEXT("ponytail_02"), FVector::ZeroVector}});
		TestFalse(TEXT("A zero-length bone is rejected"),
			HawkeyeSecondary::DescribeProblem(OnParent.Reference(), Ponytail).IsEmpty());
	}
	{
		HawkeyeSecondaryTest::FKateSkeleton NotANumber(
			{{TEXT("ponytail_02"), TEXT("ponytail_01"), FVector(0., std::numeric_limits<double>::quiet_NaN(), -3.)}});
		TestFalse(TEXT("A non-finite bone is rejected"),
			HawkeyeSecondary::DescribeProblem(NotANumber.Reference(), Ponytail).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSecondaryColliderSpace, "Hawkeye.SecondaryMotion.ColliderInBoneSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSecondaryColliderSpace::RunTest(const FString& Parameters)
{
	// A bone turned 90 degrees about X, as a skeleton exported with another bone axis has it: "8 cm up the head" and a
	// capsule that stands up must come out in the bone's own axes.
	FHawkeyeDynamicsCollider Collider;
	Collider.Bone = TEXT("Head");
	Collider.Offset = FVector(0., 0., 8.);
	Collider.Length = 6.f;
	const FTransform Bone(FRotator(0., 0., 90.), FVector(0., 0., 150.));

	FVector Offset;
	FRotator Rotation;
	HawkeyeSecondary::ColliderInBoneSpace(Collider, Bone, Offset, Rotation);
	TestTrue(TEXT("The centre lands 8 cm up in the component's axes"),
		Bone.GetRotation().RotateVector(Offset).Equals(FVector(0., 0., 8.), 0.01));
	TestTrue(TEXT("The capsule stands up in the component's axes"),
		(Bone.GetRotation() * Rotation.Quaternion()).RotateVector(FVector::UpVector).Equals(FVector::UpVector, 0.001));

	// A capsule laid along the character's forward (+Y) axis.
	Collider.Axis = FVector(0., 1., 0.);
	HawkeyeSecondary::ColliderInBoneSpace(Collider, Bone, Offset, Rotation);
	TestTrue(TEXT("The capsule lies along +Y in the component's axes"),
		(Bone.GetRotation() * Rotation.Quaternion()).RotateVector(FVector::UpVector).Equals(FVector(0., 1., 0.), 0.001));

	// A bone with no turn: nothing changes, and an upside-down capsule is the half turn, not a failure.
	HawkeyeSecondary::ColliderInBoneSpace(Collider, FTransform::Identity, Offset, Rotation);
	TestTrue(TEXT("An unturned bone keeps the offset"), Offset.Equals(FVector(0., 0., 8.), 0.01));
	Collider.Axis = FVector(0., 0., -1.);
	HawkeyeSecondary::ColliderInBoneSpace(Collider, FTransform::Identity, Offset, Rotation);
	TestTrue(TEXT("A downward capsule is turned over"),
		Rotation.Quaternion().RotateVector(FVector::UpVector).Equals(FVector(0., 0., -1.), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSecondaryRefPose, "Hawkeye.SecondaryMotion.RefBoneInComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSecondaryRefPose::RunTest(const FString& Parameters)
{
	HawkeyeSecondaryTest::FKateSkeleton Kate;
	const FReferenceSkeleton& Reference = Kate.Reference();
	// Head is root -> spine_03 (130) -> neck_01 (15) -> Head (7) straight up.
	const FTransform Head = HawkeyeSecondary::RefBoneInComponent(Reference, Reference.FindBoneIndex(TEXT("Head")));
	TestTrue(TEXT("Head is 152 cm up"), Head.GetLocation().Equals(FVector(0., 0., 152.), 0.01));
	TestTrue(TEXT("A bad index is the identity"),
		HawkeyeSecondary::RefBoneInComponent(Reference, 999).Equals(FTransform::Identity));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
