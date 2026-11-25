#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/BodyPose.h"
#include "Player/BodyPoseNode.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The thug and heavy hand poses (claude-docs/animation.md, "Hands, third pass"): the authored table, its blends, the
 * rest arm's directions and the state's easing. Plain rules, no content.
 */
using namespace HawkeyeBodyPose;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBodyPoseTable, "Hawkeye.BodyPose.Table",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBodyPoseTable::RunTest(const FString& Parameters)
{
	// The thumb is aimed when closed (ClosedThumbDirection), its curls are only the relaxed hand's.
	for (int32 Finger = static_cast<int32>(EFinger::Index); Finger < static_cast<int32>(EFinger::Count); ++Finger)
	{
		const EFinger F = static_cast<EFinger>(Finger);
		const FFingerAngles Relaxed = Angles(EHandPose::Relaxed, F);
		const FFingerAngles Fist = Angles(EHandPose::Fist, F);
		const FFingerAngles Grip = Angles(EHandPose::Grip, F);
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			TestTrue(TEXT("A fist closes further than a relaxed hand"), Fist.Curl[Joint] > Relaxed.Curl[Joint]);
			TestTrue(TEXT("A grip is looser than a fist"), Grip.Curl[Joint] < Fist.Curl[Joint]);
			TestTrue(TEXT("A grip still closes on the handle"), Grip.Curl[Joint] > Relaxed.Curl[Joint]);
		}
	}
	// A relaxed hand is staggered: each finger curls a little more than the one before it, toward the little finger.
	for (int32 Finger = static_cast<int32>(EFinger::Index); Finger < static_cast<int32>(EFinger::Pinky); ++Finger)
	{
		TestTrue(TEXT("Staggered toward the little finger"),
			Angles(EHandPose::Relaxed, static_cast<EFinger>(Finger + 1)).Curl[0] > Angles(EHandPose::Relaxed, static_cast<EFinger>(Finger)).Curl[0]);
	}
	// The relaxed thumb stays clear of the index finger: it swings less than half the way a fist's does.
	TestTrue(TEXT("The relaxed thumb is alongside, not across"),
		Angles(EHandPose::Relaxed, EFinger::Thumb).Spread < Angles(EHandPose::Fist, EFinger::Thumb).Spread * 0.6f);

	const FFingerAngles Half = Blend(EFinger::Index, 0.5f, 0.f);
	TestEqual(TEXT("Half a fist is half way"), Half.Curl[1],
		0.5f * (Angles(EHandPose::Relaxed, EFinger::Index).Curl[1] + Angles(EHandPose::Fist, EFinger::Index).Curl[1]), 0.01f);
	TestEqual(TEXT("A full grip is the grip"), Blend(EFinger::Ring, 1.f, 1.f).Curl[0], Angles(EHandPose::Grip, EFinger::Ring).Curl[0], 0.01f);
	TestEqual(TEXT("No alpha is relaxed"), Blend(EFinger::Pinky, 0.f, 0.f).Curl[2], Angles(EHandPose::Relaxed, EFinger::Pinky).Curl[2], 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBodyPoseArm, "Hawkeye.BodyPose.RestArm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBodyPoseArm::RunTest(const FString& Parameters)
{
	const FVector Up = FVector::UpVector;
	const FVector Out = FVector::RightVector;
	const FVector Forward = FVector::ForwardVector;
	const FArmRest Rest = ArmRestDirections(Up, Out, Forward);
	TestTrue(TEXT("The upper arm hangs down"), Rest.Upper.Z < -0.9f);
	TestTrue(TEXT("and a little out from the body"), Rest.Upper.Y > 0.f);
	TestTrue(TEXT("The elbow is nearly straight"), FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Rest.Upper, Rest.Lower))) < 15.f);
	TestTrue(TEXT("and bends forward"), Rest.Lower.X > Rest.Upper.X);
	TestTrue(TEXT("Unit directions"), FMath::IsNearlyEqual(Rest.Upper.Size(), 1.f, 0.001f) && FMath::IsNearlyEqual(Rest.Lower.Size(), 1.f, 0.001f));

	TestEqual(TEXT("Standing at ease: the full rest arm"), RestArmWeight(false, 0.f), 1.f);
	TestEqual(TEXT("Swinging: none"), RestArmWeight(true, 0.f), 0.f);
	TestEqual(TEXT("Running: none"), RestArmWeight(false, 400.f), 0.f);
	const float Walking = RestArmWeight(false, 90.f);
	TestTrue(TEXT("A slow walk is part way"), Walking > 0.f && Walking < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBodyPoseLegs, "Hawkeye.BodyPose.LegAdduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBodyPoseLegs::RunTest(const FString& Parameters)
{
	const FVector Up = FVector::UpVector;
	const FVector Out = FVector::RightVector;
	const FVector Hip(0.0, 11.4, 90.0);
	// A foot 25 cm out from the hip, 90 cm down: the straddle.
	const FVector Wide(0.0, 36.4, 0.0);
	const FQuat Turn = LegAdductionTurn(Hip, Wide, Out, Up, 11.4f, 1.f);
	const FVector Moved = Hip + Turn.RotateVector(Wide - Hip);
	TestTrue(TEXT("A wide foot is brought in"), Moved.Y < Wide.Y - 10.0);
	TestEqual(TEXT("to the share of the hip's own distance out"), static_cast<float>(Moved.Y), 11.4f * FeetUnderHipsRatio, 0.2f);
	TestEqual(TEXT("and the leg keeps its length"), static_cast<float>((Moved - Hip).Size()), static_cast<float>((Wide - Hip).Size()), 0.01f);
	TestTrue(TEXT("Weight 0 turns nothing"), LegAdductionTurn(Hip, Wide, Out, Up, 11.4f, 0.f).Equals(FQuat::Identity, 1e-6f));
	const FVector Half = Hip + LegAdductionTurn(Hip, Wide, Out, Up, 11.4f, 0.5f).RotateVector(Wide - Hip);
	TestTrue(TEXT("Half weight is part way"), Half.Y < Wide.Y && Half.Y > Moved.Y);
	const FVector Under(0.0, 10.2, 0.0);
	TestTrue(TEXT("A foot already under the hip is left alone"), LegAdductionTurn(Hip, Under, Out, Up, 11.4f, 1.f).Equals(FQuat::Identity, 1e-6f));
	const FVector Crossed(0.0, -5.0, 0.0);
	TestTrue(TEXT("A foot across the midline is not pushed out"), LegAdductionTurn(Hip, Crossed, Out, Up, 11.4f, 1.f).Equals(FQuat::Identity, 1e-6f));
	const FVector VeryWide(0.0, 80.0, 0.0);
	const FQuat Capped = LegAdductionTurn(Hip, VeryWide, Out, Up, 11.4f, 1.f);
	TestTrue(TEXT("The turn is capped"), FMath::RadiansToDegrees(Capped.GetAngle()) <= MaxLegAdductionDegrees + 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBodyPoseEase, "Hawkeye.BodyPose.Ease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBodyPoseEase::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A hand closes in 0.12 s"), StepAlpha(0.f, 1.f, 0.12f), 1.f, 0.001f);
	TestEqual(TEXT("not in one frame"), StepAlpha(0.f, 1.f, 0.016f), 0.133f, 0.01f);
	TestEqual(TEXT("and opens the same way"), StepAlpha(1.f, 0.f, 0.06f), 0.5f, 0.001f);
	TestEqual(TEXT("It stops at the target"), StepAlpha(0.95f, 1.f, 0.5f), 1.f);

	UHawkeyeBodyAnimInstance* Instance = NewObject<UHawkeyeBodyAnimInstance>(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	FWish Wish;
	Wish.bGrip[HandRight] = true;
	Wish.bFist[HandLeft] = true;
	Wish.ArmRest = 1.f;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Instance->StepToward(Wish, 0.016f);
	}
	TestEqual(TEXT("The right hand holds"), Instance->GetState().Grip[HandRight], 1.f, 0.001f);
	TestEqual(TEXT("The left is a fist"), Instance->GetState().Fist[HandLeft], 1.f, 0.001f);
	TestEqual(TEXT("and the other hand is relaxed"), Instance->GetState().Fist[HandRight], 0.f);
	TestEqual(TEXT("The arms are at rest"), Instance->GetState().ArmRest, 1.f, 0.001f);
	return true;
}

#endif
