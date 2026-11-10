#include "Animation/AimOffsetBlendSpace.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/BowIKAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The bow hands: the post-process AnimBP's inputs come from UHawkeyeBowIKAnimInstance, which puts
 * the bow hand on a grip point in the aim frame, moves the string hand from the string at rest to
 * the cheek as the draw fraction rises, blends each hand on and off over 0.15 s, and turns the
 * spine side-on toward the aim. UBowComponent sets the AnimBP as its mesh's post-process override
 * at BeginPlay. No content is loaded: the anim instance class stands in for the Blueprint.
 */
namespace HawkeyeBowIKTest
{
	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, TSubclassOf<UAnimInstance> HandsClass)
	{
		UWorld* World = TestWorld.Get();
		if (!World)
		{
			return nullptr;
		}
		const FTransform Where(FRotator::ZeroRotator, FVector(0.f, 0.f, 100.f));
		AHawkeyeCharacter* Kate = World->SpawnActorDeferred<AHawkeyeCharacter>(AHawkeyeCharacter::StaticClass(), Where,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Kate && Kate->GetBowComponent())
		{
			Kate->GetBowComponent()->HandsIKClass = HandsClass;
		}
		if (Kate)
		{
			Kate->FinishSpawning(Where);
		}
		return Kate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowIKStringFollowsDraw, "Hawkeye.BowIK.TargetsFromDrawFraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowIKStringFollowsDraw::RunTest(const FString& Parameters)
{
	const FBowHandIKSettings Settings;
	const FTransform Identity = FTransform::Identity;

	const FBowHandIKTargets Rest = UHawkeyeBowIKAnimInstance::ComputeHandTargets(Identity, 0.f, Settings);
	const FBowHandIKTargets Full = UHawkeyeBowIKAnimInstance::ComputeHandTargets(Identity, 1.f, Settings);
	const FBowHandIKTargets Half = UHawkeyeBowIKAnimInstance::ComputeHandTargets(Identity, 0.5f, Settings);
	const FBowHandIKTargets Over = UHawkeyeBowIKAnimInstance::ComputeHandTargets(Identity, 3.f, Settings);

	TestTrue(TEXT("No draw: the string hand is on the string by the bow"), Rest.StringHand.Equals(Settings.StringHandRestOffset, 0.01));
	TestTrue(TEXT("Full draw: the string hand is at the cheek"), Full.StringHand.Equals(Settings.StringHandFullOffset, 0.01));
	TestTrue(TEXT("Half draw: half way along"), Half.StringHand.Equals(
		(Settings.StringHandRestOffset + Settings.StringHandFullOffset) * 0.5, 0.01));
	TestTrue(TEXT("Past full draw clamps to the cheek"), Over.StringHand.Equals(Full.StringHand, 0.01));
	TestTrue(TEXT("The bow hand stays on the grip through the draw"), Rest.BowHand.Equals(Full.BowHand, 0.01)
		&& Rest.BowHand.Equals(Settings.GripOffset, 0.01));
	TestTrue(TEXT("The draw elbow swings from its rest hint to its full hint"),
		Rest.StringElbow.Equals(Settings.StringElbowHintRest, 0.01) && Full.StringElbow.Equals(Settings.StringElbowHintFull, 0.01));
	TestTrue(TEXT("Full draw brings the string hand back behind the grip"), Full.StringHand.X < Full.BowHand.X - 40.0);

	// Turned to face +Y (yaw 90) at (100, 0, 150), scaled: forward offsets land along +Y, scale ignored.
	const FTransform Turned(FRotator(0.f, 90.f, 0.f), FVector(100.f, 0.f, 150.f), FVector(2.f));
	const FBowHandIKTargets TurnedFull = UHawkeyeBowIKAnimInstance::ComputeHandTargets(Turned, 1.f, Settings);
	const FVector Expected = FVector(100.f, 0.f, 150.f) + FRotator(0.f, 90.f, 0.f).RotateVector(Settings.GripOffset);
	TestTrue(TEXT("The grip is placed in the aim frame, unscaled"), TurnedFull.BowHand.Equals(Expected, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowIKAlphaBlend, "Hawkeye.BowIK.AlphasBlendOverPoint15s",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowIKAlphaBlend::RunTest(const FString& Parameters)
{
	const float Blend = FBowHandIKSettings().BlendSeconds;
	TestEqual(TEXT("The settings blend over 0.15 s"), Blend, 0.15f);

	float Alpha = UHawkeyeBowIKAnimInstance::StepAlpha(0.f, 1.f, 0.075f, Blend);
	TestEqual(TEXT("Half way on after 0.075 s"), Alpha, 0.5f, 0.001f);
	Alpha = UHawkeyeBowIKAnimInstance::StepAlpha(Alpha, 1.f, 0.075f, Blend);
	TestEqual(TEXT("Fully on after 0.15 s"), Alpha, 1.f, 0.001f);
	Alpha = UHawkeyeBowIKAnimInstance::StepAlpha(Alpha, 1.f, 1.f, Blend);
	TestEqual(TEXT("Never past 1"), Alpha, 1.f);
	Alpha = UHawkeyeBowIKAnimInstance::StepAlpha(Alpha, 0.f, 0.05f, Blend);
	TestEqual(TEXT("A third of the way off after 0.05 s"), Alpha, 2.f / 3.f, 0.001f);
	Alpha = UHawkeyeBowIKAnimInstance::StepAlpha(Alpha, 0.f, 0.1f, Blend);
	TestEqual(TEXT("Fully off 0.15 s after letting go"), Alpha, 0.f, 0.001f);
	TestEqual(TEXT("A zero blend snaps"), UHawkeyeBowIKAnimInstance::StepAlpha(0.f, 1.f, 0.001f, 0.f), 1.f);

	FBowHandIKSettings Settings;
	Settings.SideOnDegrees = 15.f;
	Settings.MaxAimTwistDegrees = 45.f;
	TestEqual(TEXT("Facing the aim: only the side-on turn"), UHawkeyeBowIKAnimInstance::ComputeSpineTwistDegrees(30.f, 30.f, Settings), 15.f, 0.01f);
	TestEqual(TEXT("Aim 20 deg right of the body"), UHawkeyeBowIKAnimInstance::ComputeSpineTwistDegrees(10.f, -10.f, Settings), 35.f, 0.01f);
	TestEqual(TEXT("Aim far right clamps"), UHawkeyeBowIKAnimInstance::ComputeSpineTwistDegrees(170.f, 0.f, Settings), 60.f, 0.01f);
	TestEqual(TEXT("Across the wrap: 350 is 10 left of 0"), UHawkeyeBowIKAnimInstance::ComputeSpineTwistDegrees(350.f, 0.f, Settings), 5.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowIKOverrideAtBeginPlay, "Hawkeye.BowIK.PostProcessOverrideSetAtBeginPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowIKOverrideAtBeginPlay::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	const TSubclassOf<UAnimInstance> HandsClass = UHawkeyeBowIKAnimInstance::StaticClass();
	AHawkeyeCharacter* Kate = HawkeyeBowIKTest::SpawnKate(TestWorld, HandsClass);
	AHawkeyeCharacter* Plain = HawkeyeBowIKTest::SpawnKate(TestWorld, nullptr);
	if (!TestNotNull(TEXT("Kate spawned"), Kate) || !TestNotNull(TEXT("A second character spawned"), Plain))
	{
		return false;
	}
	TestTrue(TEXT("Kate's mesh runs the bow hands post-process after BeginPlay"),
		Kate->GetMesh()->GetPostProcessAnimBPClassToBeUsed() == HandsClass);
	TestTrue(TEXT("Without a HandsIKClass the mesh is left alone"), Plain->GetMesh()->GetPostProcessAnimBPClassToBeUsed() == nullptr);

	// Applying again is a no-op, not a second re-initialise.
	Kate->GetBowComponent()->ApplyHandsIK();
	TestTrue(TEXT("Still the same override"), Kate->GetMesh()->GetPostProcessAnimBPClassToBeUsed() == HandsClass);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowIKHandsOnly, "Hawkeye.BowIK.NoBowNoCrash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowIKHandsOnly::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = HawkeyeBowIKTest::SpawnKate(TestWorld, UHawkeyeBowIKAnimInstance::StaticClass());
	UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
	if (!TestNotNull(TEXT("Kate with a bow component"), Bow))
	{
		return false;
	}

	// Hands only: no bow in the inventory.
	UHawkeyeBowIKAnimInstance* Hands = NewObject<UHawkeyeBowIKAnimInstance>(Kate->GetMesh());
	Hands->UpdateFromBow(Bow, 0.1f);
	Hands->UpdateFromBow(nullptr, 0.1f);
	TestFalse(TEXT("Nothing to draw"), Bow->StartDraw());
	TestEqual(TEXT("Bow hand stays off"), Hands->GetBowAlpha(), 0.f);
	TestEqual(TEXT("String hand stays off"), Hands->GetDrawAlpha(), 0.f);
	TestTrue(TEXT("No spine turn"), Hands->SpineTwist.IsNearlyZero());
	Bow->bHolsterWhenIdle = false;
	Bow->ApplyHandsIK();
	TestNull(TEXT("No bow, so none in the hand to show"), Bow->GetBowMeshComponent());

	// With a bow and a draw the hands come on over the blend and the targets are filled in.
	UBowDefinition* Definition = NewObject<UBowDefinition>(Kate);
	Kate->GetInventoryComponent()->GiveBow(Definition);
	Bow->SetTestTimeSeconds(10.0);
	TestTrue(TEXT("Draws once a bow is given"), Bow->StartDraw());
	Bow->SetTestTimeSeconds(10.0 + Definition->FullDrawSeconds);
	Hands->UpdateFromBow(Bow, 0.075f);
	TestEqual(TEXT("Half way on"), Hands->GetBowAlpha(), 0.5f, 0.001f);
	Hands->UpdateFromBow(Bow, 0.075f);
	TestEqual(TEXT("Bow hand on"), Hands->GetBowAlpha(), 1.f, 0.001f);
	TestEqual(TEXT("String hand on"), Hands->GetDrawAlpha(), 1.f, 0.001f);
	TestFalse(TEXT("The string hand has somewhere to go"), Hands->RightHandTarget.IsNearlyZero());
	TestTrue(TEXT("The spine turns side-on"), Hands->SpineTwist.Yaw > 0.f);

	// The bow taken away mid-draw: the hands let go, nothing dereferences a missing definition.
	Kate->GetInventoryComponent()->GiveBow(nullptr);
	Hands->UpdateFromBow(Bow, 0.15f);
	TestEqual(TEXT("Bow hand let go"), Hands->GetBowAlpha(), 0.f, 0.001f);
	TestEqual(TEXT("String hand let go"), Hands->GetDrawAlpha(), 0.f, 0.001f);
	return true;
}

/**
 * Bow clips at their own pace (gameplay-semantics.md, "Combat animation clips", 2026-09-28): the draw
 * clip is never stretched to the draw time; the aim clip takes over when the draw clip is at its end,
 * the nock when the fire clip is over; the bow stays in the hand while a clip still holds the arms.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowClipOwnPace, "Hawkeye.BowIK.ClipsPlayAtTheirOwnPace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowClipOwnPace::RunTest(const FString& Parameters)
{
	using ERole = ECombatAnimRole;
	TestTrue(TEXT("A draw clip still pulling stays"), UBowComponent::NextBowClip(ERole::BowDraw, true, false) == ERole::None);
	TestTrue(TEXT("At the anchor the aim clip takes over, whatever the draw fraction"), UBowComponent::NextBowClip(ERole::BowDraw, true, true) == ERole::BowAimIdle);
	TestTrue(TEXT("A draw clip let go of is not moved on"), UBowComponent::NextBowClip(ERole::BowDraw, false, true) == ERole::None);
	TestTrue(TEXT("The aim clip loops until the release"), UBowComponent::NextBowClip(ERole::BowAimIdle, true, true) == ERole::None);
	TestTrue(TEXT("The fire clip plays out"), UBowComponent::NextBowClip(ERole::BowFire, false, false) == ERole::None);
	TestTrue(TEXT("Then the nock"), UBowComponent::NextBowClip(ERole::BowFire, false, true) == ERole::BowNock);
	TestTrue(TEXT("A new draw over the fire clip is the draw's business"), UBowComponent::NextBowClip(ERole::BowFire, true, true) == ERole::None);

	TestTrue(TEXT("Raised: in the hand"), UBowComponent::KeepsBowInHand(true, true, 0.f));
	TestTrue(TEXT("Holstering off: in the hand"), UBowComponent::KeepsBowInHand(false, false, 0.f));
	TestTrue(TEXT("A clip still holding the arms keeps it in the hand"), UBowComponent::KeepsBowInHand(false, true, 0.5f));
	TestFalse(TEXT("Once the clip has let go it goes on the back"), UBowComponent::KeepsBowInHand(false, true, 0.04f));
	TestEqual(TEXT("A let-down blends the clip out over 0.25 s"), UBowComponent::ClipLetDownSeconds, 0.25f);
	TestEqual(TEXT("Sparrow's 0.43 s fire clip follows through until it starts to blend out, 0.18 s"),
		UBowComponent::ComputeClipFollowThrough(0.43f, 0.25f), 0.18f, 0.001f);
	TestEqual(TEXT("Never negative"), UBowComponent::ComputeClipFollowThrough(0.1f, 0.25f), 0.f);
	return true;
}

/**
 * Under a clip the string hand is the clip's: the IK only corrects it toward the arrow line, from 0 within
 * 3 cm to at most 0.5 at 15 cm, at the clip's own draw length along the line.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowClipStringCorrection, "Hawkeye.BowIK.ClipStringHandOnlyCorrected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowClipStringCorrection::RunTest(const FString& Parameters)
{
	const FBowHandIKSettings Settings;
	TestEqual(TEXT("Dead zone 3 cm"), Settings.ClipCorrectionDeadZone, 3.f);
	TestEqual(TEXT("Full at 15 cm"), Settings.ClipCorrectionFullDistance, 15.f);
	TestEqual(TEXT("At most half way"), Settings.ClipCorrectionMaxAlpha, 0.5f);
	TestEqual(TEXT("On the line: no correction"), UHawkeyeBowIKAnimInstance::ComputeStringCorrectionAlpha(0.f, Settings), 0.f);
	TestEqual(TEXT("Within the dead zone: none"), UHawkeyeBowIKAnimInstance::ComputeStringCorrectionAlpha(2.9f, Settings), 0.f);
	TestEqual(TEXT("Half way along: a quarter"), UHawkeyeBowIKAnimInstance::ComputeStringCorrectionAlpha(9.f, Settings), 0.25f, 0.001f);
	TestEqual(TEXT("At 15 cm: the most"), UHawkeyeBowIKAnimInstance::ComputeStringCorrectionAlpha(15.f, Settings), 0.5f, 0.001f);
	TestEqual(TEXT("Never an override"), UHawkeyeBowIKAnimInstance::ComputeStringCorrectionAlpha(80.f, Settings), 0.5f, 0.001f);

	// The bow at the origin shooting along +X: the line runs back along -X.
	const FVector Rest = FVector::ZeroVector;
	const FVector Launch(1.f, 0.f, 0.f);
	const FVector OnLine = UHawkeyeBowIKAnimInstance::ComputeArrowLinePoint(Rest, Launch, FVector(-70.f, 8.f, -3.f), 10.f);
	TestTrue(TEXT("The nearest point on the line keeps the clip's draw length"), OnLine.Equals(FVector(-70.f, 0.f, 0.f), 0.01));
	const FVector Short = UHawkeyeBowIKAnimInstance::ComputeArrowLinePoint(Rest, Launch, FVector(5.f, 4.f, 0.f), 10.f);
	TestTrue(TEXT("Never less than the shortest draw behind the bow"), Short.Equals(FVector(-10.f, 0.f, 0.f), 0.01));

	// The clip alpha is the slot's weight, applied as it is.
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = HawkeyeBowIKTest::SpawnKate(TestWorld, UHawkeyeBowIKAnimInstance::StaticClass());
	UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
	if (!TestNotNull(TEXT("Kate with a bow component"), Bow))
	{
		return false;
	}
	Kate->GetInventoryComponent()->GiveBow(NewObject<UBowDefinition>(Kate));
	UHawkeyeBowIKAnimInstance* Hands = NewObject<UHawkeyeBowIKAnimInstance>(Kate->GetMesh());
	Hands->UpdateFromBowWithClipWeight(Bow, 0.01f, 0.4f);
	TestEqual(TEXT("A clip 40% blended in is 40% of the arms"), Hands->GetClipAlpha(), 0.4f, 0.001f);
	TestEqual(TEXT("With no clip playing the bow reads none"), Bow->GetBowClipWeight(), 0.f);
	return true;
}

/**
 * The aim offset's inputs: the aim's yaw off the body and its pitch (scaled by 1.57 up and 1.85 down, lifted 5 degrees,
 * so a 30 degree aim gives a 30 degree arrow on Sparrow's poses), clamped to 90 and to the asset's own axes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowAimOffsetInput, "Hawkeye.BowIK.AimOffsetInputsClamped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowAimOffsetInput::RunTest(const FString& Parameters)
{
	FBowHandIKSettings Settings;
	TestEqual(TEXT("Pitch scale 1.57 up"), Settings.AimOffsetPitchScale, 1.57f);
	TestEqual(TEXT("Pitch scale 1.85 down"), Settings.AimOffsetPitchScaleDown, 1.85f);
	TestEqual(TEXT("Pitch bias 5"), Settings.AimOffsetPitchBias, 5.f);
	FVector2D In = UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(FRotator(30.f, 100.f, 0.f), 90.f, Settings, nullptr);
	TestEqual(TEXT("Yaw is off the body"), In.X, 10.0, 0.01);
	TestEqual(TEXT("Pitch is the aim's, scaled and lifted"), In.Y, 30.0 * 1.57 + 5.0, 0.01);
	In = UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(FRotator(0.f, 0.f, 0.f), 0.f, Settings, nullptr);
	TestEqual(TEXT("Level aim lifts the centre pose's droop"), In.Y, 5.0, 0.01);
	In = UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(FRotator(-30.f, 350.f, 0.f), 0.f, Settings, nullptr);
	TestEqual(TEXT("Across the wrap: 350 is 10 left"), In.X, -10.0, 0.01);
	TestEqual(TEXT("Down is negative, on the down scale"), In.Y, -30.0 * 1.85 + 5.0, 0.01);
	In = UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(FRotator(-60.f, 0.f, 0.f), 0.f, Settings, nullptr);
	TestEqual(TEXT("Scaled past the axis, it clamps at -90"), In.Y, -90.0, 0.01);
	Settings.AimOffsetPitchScale = 1.f;
	Settings.AimOffsetPitchScaleDown = 1.f;
	Settings.AimOffsetPitchBias = 0.f;
	In = UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(FRotator(30.f, 0.f, 0.f), 0.f, Settings, nullptr);
	TestEqual(TEXT("Unscaled, the pitch is the aim's"), In.Y, 30.0, 0.01);
	Settings = FBowHandIKSettings();
	In = UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(FRotator(80.f, 180.f, 0.f), 0.f, Settings, nullptr);
	TestEqual(TEXT("Behind her clamps to 90"), In.X, 90.0, 0.01);

	// An asset whose axes are narrower: its range wins.
	UAimOffsetBlendSpace* Space = NewObject<UAimOffsetBlendSpace>(GetTransientPackage());
	const FProperty* Axes = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"));
	if (!TestNotNull(TEXT("Blend spaces keep their axes in BlendParameters"), Axes))
	{
		return false;
	}
	FBlendParameter* Parameters3 = Axes->ContainerPtrToValuePtr<FBlendParameter>(Space);
	Parameters3[0].Min = -60.f;
	Parameters3[0].Max = 60.f;
	Parameters3[1].Min = -45.f;
	Parameters3[1].Max = 45.f;
	In = UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(FRotator(80.f, 170.f, 0.f), 0.f, Settings, Space);
	TestEqual(TEXT("Yaw held to the asset's 60"), In.X, 60.0, 0.01);
	TestEqual(TEXT("Pitch held to the asset's 45"), In.Y, 45.0, 0.01);
	return true;
}

#endif
