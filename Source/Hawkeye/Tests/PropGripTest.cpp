#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/BodyPose.h"
#include "Player/BodyPoseNode.h"
#include "Player/PropGrip.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A prop in a thug's closed hand (claude-docs/animation.md, "Props in the hand"): where the handle lies (per-prop data),
 * that the pistol's grip block sits in the fist with its barrel well clear of it, the live override's parser, and the wrist's
 * turn limit as data. Plain rules, no content; the real hands are seen in Hawkeye.Screenshot.Bodies.
 */
namespace HawkeyePropGripTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A right hand hanging at his side: fingers down, the knuckle line running forward, the palm toward the thigh (to his left). */
	struct FHangingHand
	{
		FVector Along = FVector(0.f, 0.f, -1.f);
		FVector Across = FVector(1.f, 0.f, 0.f);
		FVector Palm = FVector(0.f, -1.f, 0.f);
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePropGripCenter, "Hawkeye.PropGrip.HandleCenter", HawkeyePropGripTest::Flags)

bool FHawkeyePropGripCenter::RunTest(const FString& Parameters)
{
	using namespace HawkeyePropGripTest;
	const FHangingHand Hand;
	const FVector Knuckles(100.f, 50.f, 20.f);
	FHawkeyePropGrip Grip;
	Grip.PalmCm = 3.f;
	Grip.AlongCm = -1.f;
	Grip.AcrossCm = 0.5f;
	const FVector Center = HawkeyePropGrip::ComputeHandleCenter(Knuckles, Hand.Along, Hand.Across, Hand.Palm, Grip, 1.f);
	// 3 into the palm (-Y), 1 back toward the wrist (+Z), 0.5 forward across the knuckles (+X).
	TestTrue(FString::Printf(TEXT("Palm, along and across, each on its own axis (%s)"), *Center.ToCompactString()),
		Center.Equals(FVector(100.5f, 47.f, 21.f), 0.001f));
	const FVector Scaled = HawkeyePropGrip::ComputeHandleCenter(Knuckles, Hand.Along, Hand.Across, Hand.Palm, Grip, 2.f);
	TestTrue(TEXT("A body at twice the size moves the handle twice as far from the knuckles"), Scaled.Equals(FVector(101.f, 44.f, 22.f), 0.001f));

	// The defaults: deeper in the palm and behind the knuckle line, where the old offsets (2.4 palm, +0.8 along the fingers)
	// put the handle in front of the fingers.
	const AThugCharacter* Defaults = GetDefault<AThugCharacter>();
	TestTrue(TEXT("The bat's handle is behind the knuckle line (negative along)"), Defaults->BatGrip.AlongCm < 0.f);
	TestTrue(TEXT("and deeper in the palm than the old 2.4 cm"), Defaults->BatGrip.PalmCm > 2.4f);
	TestTrue(TEXT("The pistol's handle likewise"), Defaults->PistolHandGrip.AlongCm < 0.f && Defaults->PistolHandGrip.PalmCm > 2.4f);
	TestTrue(TEXT("The bat's wrist turns further than the pistol's (the bat is held on a diagonal)"),
		Defaults->BatGrip.WristTurnDegrees > Defaults->PistolHandGrip.WristTurnDegrees);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePropGripPistol, "Hawkeye.PropGrip.PistolSitsInTheFistBarrelClear", HawkeyePropGripTest::Flags)

bool FHawkeyePropGripPistol::RunTest(const FString& Parameters)
{
	using namespace HawkeyePropGripTest;
	const FHangingHand Hand;
	const AThugCharacter* Defaults = GetDefault<AThugCharacter>();
	const FHawkeyePropGrip& Grip = Defaults->PistolHandGrip;
	const FVector Knuckles(0.f, 0.f, 100.f);
	const FVector Center = HawkeyePropGrip::ComputeHandleCenter(Knuckles, Hand.Along, Hand.Across, Hand.Palm, Grip, 1.f);

	const FQuat Rotation = HawkeyePropGrip::ComputePistolRotation(Hand.Along, Hand.Across, Hand.Palm, Grip);
	const FVector Location = HawkeyePropGrip::ComputePistolLocation(Center, Rotation, Defaults->PistolGripCenterCm);
	TestTrue(TEXT("The pistol's grip block centre is at the handle's centre in the fist"),
		(Location + Rotation.RotateVector(Defaults->PistolGripCenterCm)).Equals(Center, 0.001f));
	TestTrue(TEXT("The handle runs across the knuckles (the pistol's Z is Across)"), FVector::DotProduct(Rotation.GetAxisZ(), Hand.Across) > 0.999);

	const FVector Muzzle = HawkeyePropGrip::ComputeMuzzleDirection(Hand.Along, Hand.Palm, Grip);
	TestTrue(TEXT("The muzzle is the pistol's X"), FVector::DotProduct(Rotation.GetAxisX(), Muzzle) > 0.999);
	const double OutDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Muzzle, Hand.Along), -1.0, 1.0)));
	TestEqual(TEXT("It turns MuzzleOutDegrees from the fingers' line"), static_cast<float>(OutDegrees), Grip.MuzzleOutDegrees, 0.05f);
	TestTrue(TEXT("away from the thigh (the palm's side)"), FVector::DotProduct(Muzzle, Hand.Palm) < 0.0);

	// The slide's far end (9 cm from its centre along X) is well past the knuckle line along the fingers: the barrel is out
	// beyond the fist, not inside the curl, and the slide's axis is off to the index side of the hand by the grip's height.
	const FVector Tip = Location + Rotation.RotateVector(FVector(9.f, 0.f, 0.f));
	const double BeyondKnuckles = FVector::DotProduct(Tip - Knuckles, Hand.Along);
	TestTrue(FString::Printf(TEXT("The muzzle is at least 8 cm out past the knuckle line (%.1f)"), BeyondKnuckles), BeyondKnuckles >= 8.0);
	const double SlideOffAxis = FVector::DotProduct(Location - Center, Hand.Across);
	TestTrue(FString::Printf(TEXT("and the slide rides above the fist, off the handle's centre by %.1f cm across the knuckles"), SlideOffAxis),
		SlideOffAxis > 5.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePropGripGeometry, "Hawkeye.PropGrip.DistancesAndWrap", HawkeyePropGripTest::Flags)

bool FHawkeyePropGripGeometry::RunTest(const FString& Parameters)
{
	const FVector Origin(0.f, 0.f, 0.f);
	const FVector Z(0.f, 0.f, 1.f);
	TestEqual(TEXT("A point 3 cm off a line"), static_cast<float>(HawkeyePropGrip::DistanceToLine(FVector(3.f, 0.f, 50.f), Origin, Z)), 3.f, 0.001f);
	TestEqual(TEXT("A line has no ends"), static_cast<float>(HawkeyePropGrip::DistanceToLine(FVector(0.f, 4.f, 500.f), Origin, FVector(0.f, 0.f, 2.f))), 4.f, 0.001f);
	TestEqual(TEXT("A segment does"), static_cast<float>(HawkeyePropGrip::DistanceToSegment(FVector(0.f, 3.f, 14.f), Origin, FVector(0.f, 0.f, 10.f))), 5.f, 0.001f);
	TestEqual(TEXT("A degenerate segment is a point"), static_cast<float>(HawkeyePropGrip::DistanceToSegment(FVector(3.f, 4.f, 0.f), Origin, Origin)), 5.f, 0.001f);

	// A bat through the fist has the four fingers' middle joints about it; held out in front, they are not.
	const TArray<FVector> Wrapped = {FVector(2.f, 0.f, 0.f), FVector(0.f, 2.5f, 3.f), FVector(-2.f, 1.f, 6.f), FVector(1.f, -2.f, 9.f)};
	TestTrue(TEXT("Fingers all within 3 cm of the handle line wrap it"), HawkeyePropGrip::FingersWrapHandle(Wrapped, Origin, Z, 3.0));
	const TArray<FVector> InFront = {FVector(9.f, 0.f, 0.f), FVector(9.f, 2.f, 3.f), FVector(8.f, 1.f, 6.f), FVector(9.f, -2.f, 9.f)};
	TestFalse(TEXT("Fingers 9 cm off it do not"), HawkeyePropGrip::FingersWrapHandle(InFront, Origin, Z, 3.0));
	TestFalse(TEXT("No fingers, no wrap"), HawkeyePropGrip::FingersWrapHandle({}, Origin, Z, 3.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePropGripOverride, "Hawkeye.PropGrip.ConsoleOverride", HawkeyePropGripTest::Flags)

bool FHawkeyePropGripOverride::RunTest(const FString& Parameters)
{
	FHawkeyePropGrip Grip;
	TestFalse(TEXT("Empty text changes nothing"), HawkeyePropGrip::ParseOverride(TEXT(""), Grip));
	TestFalse(TEXT("Blanks neither"), HawkeyePropGrip::ParseOverride(TEXT("   "), Grip));
	TestEqual(TEXT("Palm untouched"), Grip.PalmCm, FHawkeyePropGrip().PalmCm);

	TestTrue(TEXT("Two numbers set palm and along"), HawkeyePropGrip::ParseOverride(TEXT("4.5 -2"), Grip));
	TestEqual(TEXT("palm"), Grip.PalmCm, 4.5f);
	TestEqual(TEXT("along"), Grip.AlongCm, -2.f);
	TestEqual(TEXT("and the rest stay as they were (across)"), Grip.AcrossCm, FHawkeyePropGrip().AcrossCm);

	TestTrue(TEXT("Commas work, and five numbers set all of them"), HawkeyePropGrip::ParseOverride(TEXT("1,2,3,4,70"), Grip));
	TestEqual(TEXT("across"), Grip.AcrossCm, 3.f);
	TestEqual(TEXT("muzzle out"), Grip.MuzzleOutDegrees, 4.f);
	TestEqual(TEXT("wrist turn"), Grip.WristTurnDegrees, 70.f);

	FHawkeyePropGrip Junk;
	TestTrue(TEXT("A word in the list is skipped, the numbers either side land"), HawkeyePropGrip::ParseOverride(TEXT("2 x 5"), Junk));
	TestEqual(TEXT("palm"), Junk.PalmCm, 2.f);
	TestEqual(TEXT("along kept (the word was not a number)"), Junk.AlongCm, FHawkeyePropGrip().AlongCm);
	TestEqual(TEXT("across"), Junk.AcrossCm, 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePropGripWristLimit, "Hawkeye.PropGrip.WristTurnIsData", HawkeyePropGripTest::Flags)

bool FHawkeyePropGripWristLimit::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBodyPose;
	UHawkeyeBodyAnimInstance* Instance = NewObject<UHawkeyeBodyAnimInstance>(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	TestEqual(TEXT("The default limit is the old constant"), Instance->GetState().WristTurnDegrees[HandRight], MaxWristTurnDegrees);
	FWish Wish;
	Wish.bGrip[HandRight] = true;
	Wish.PropAxis[HandRight] = FVector(0.f, 1.f, 0.f);
	Wish.WristTurnDegrees[HandRight] = 70.f;
	Instance->StepToward(Wish, 0.016f);
	TestEqual(TEXT("A wish's wrist turn reaches the node's state"), Instance->GetState().WristTurnDegrees[HandRight], 70.f);
	TestEqual(TEXT("The other wrist is untouched"), Instance->GetState().WristTurnDegrees[HandLeft], MaxWristTurnDegrees);
	return true;
}

#endif
