#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/CombatAnimSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Traversal/TraversalPoseRules.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The traversal body pose as the character feeds it (claude-docs/gameplay-semantics.md, "Traversal body pose"): what the
 * input reads from the zip, that the pose is only visual (the zip goes exactly where it did), and that the landing outcomes
 * the fall height chose are the ones they were.
 */
namespace HawkeyeTraversalPoseCharacterTest
{
	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		if (UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr)
		{
			Movement->SetMovementMode(MOVE_Falling);
			Movement->Velocity = FVector::ZeroVector;
		}
		return Kate;
	}

	static FHawkeyeZipLine LineTo(const ACharacter* Kate, const FVector& End, bool bHop)
	{
		FHawkeyeZipLine Line;
		Line.Launch = Kate->GetActorLocation() + (bHop ? FVector(0.f, 0.f, 100.f) : FVector::ZeroVector);
		Line.End = End;
		Line.bHop = bHop;
		return Line;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseInput, "Hawkeye.Traversal.Pose.InputFollowsTheZip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseInput::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseCharacterTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}

	FTraversalPoseInput Idle = Kate->BuildTraversalPoseInput(1.f / 60.f);
	TestFalse(TEXT("before a zip nothing is zipping"), Idle.bZipping);
	TestTrue(TEXT("she is falling (the test spawns her in the air)"), Idle.bFalling);
	TestFalse(TEXT("and not grounded"), Idle.bGrounded);

	// A zip from the ground: the hop first, then the line.
	Movement->StartZip(LineTo(Kate, FVector(4000.f, 0.f, 1600.f), /*bHop=*/true));
	Kate->TestTickMovement(0.05f);
	FTraversalPoseInput Hop = Kate->BuildTraversalPoseInput(0.05f);
	TestTrue(TEXT("the zip is on"), Hop.bZipping);
	TestTrue(TEXT("in its hop"), Hop.bHopping);
	TestEqual(TEXT("a third of the way through the 0.15 s hop"), Hop.HopFraction, 1.f / 3.f, 0.08f);
	TestFalse(TEXT("not redirecting"), Hop.bRedirecting);

	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Kate->TestTickMovement(0.02f);
	}
	FTraversalPoseInput Line = Kate->BuildTraversalPoseInput(0.02f);
	TestTrue(TEXT("on the line after the hop"), Line.bZipping && !Line.bHopping);
	TestEqual(TEXT("no hop fraction on the line"), Line.HopFraction, 0.f);
	TestTrue(TEXT("the line speed is at least the zip's slowest start"), Line.ZipLineSpeed >= Movement->ZipMinStartSpeed - 1.f);
	TestTrue(TEXT("and her velocity is the real one"), Line.Velocity.Equals(Kate->GetVelocity(), 0.01f));

	// A chain from the air turns her onto the new line.
	Movement->StartZip(LineTo(Kate, FVector(3000.f, 3000.f, 1900.f), /*bHop=*/false));
	FTraversalPoseInput Turn = Kate->BuildTraversalPoseInput(0.02f);
	TestTrue(TEXT("a chain mid-line is a redirect"), Turn.bZipping && Turn.bRedirecting);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseVisualOnly, "Hawkeye.Traversal.Pose.NeverMovesHer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseVisualOnly::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseCharacterTest;
	const FHawkeyeTestWorld TestWorld;
	// Two identical zips, one far from the other; one of them has its body pose stepped every tick.
	AHawkeyeAimTestCharacter* Plain = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	AHawkeyeAimTestCharacter* Posed = SpawnKate(TestWorld, FVector(0.f, 20000.f, 1000.f));
	if (!Plain || !Posed || !Plain->GetHawkeyeMovement() || !Posed->GetHawkeyeMovement())
	{
		AddError(TEXT("Could not spawn two characters with movement."));
		return false;
	}
	Plain->GetHawkeyeMovement()->StartZip(LineTo(Plain, Plain->GetActorLocation() + FVector(3000.f, 0.f, 500.f), true));
	Posed->GetHawkeyeMovement()->StartZip(LineTo(Posed, Posed->GetActorLocation() + FVector(3000.f, 0.f, 500.f), true));
	const FHawkeyeTraversalPoseSettings& Settings = Posed->GetTraversalPoseSettings();
	float MostTilt = 0.f;
	for (int32 Tick = 0; Tick < 90; ++Tick)
	{
		Plain->TestTickMovement(1.f / 60.f);
		Posed->TestTickMovement(1.f / 60.f);
		const FTraversalPoseOutput Pose = Posed->GetTraversalPoseState().Step(Posed->BuildTraversalPoseInput(1.f / 60.f), Settings);
		MostTilt = FMath::Max(MostTilt, static_cast<float>(Pose.Tilt.Size()));
	}
	TestTrue(TEXT("the pose did something (the body tipped)"), MostTilt > 5.f);
	const FVector PlainAt = Plain->GetActorLocation();
	const FVector PosedAt = Posed->GetActorLocation() - FVector(0.f, 20000.f, 0.f);
	TestTrue(TEXT("the zip went to the same place with the pose on"), PlainAt.Equals(PosedAt, 0.01f));
	TestTrue(TEXT("with the same velocity"), Plain->GetVelocity().Equals(Posed->GetVelocity(), 0.01f));
	TestEqual(TEXT("and the same mode"), static_cast<int32>(Plain->GetCharacterMovement()->MovementMode.GetValue()),
		static_cast<int32>(Posed->GetCharacterMovement()->MovementMode.GetValue()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseLandings, "Hawkeye.Traversal.Pose.LandingOutcomesKept",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseLandings::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalPoseCharacterTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UCharacterMovementComponent* Movement = Kate ? Kate->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Could not spawn a character with movement."));
		return false;
	}
	Movement->SetMovementMode(MOVE_Walking);

	// A step down: the sample's. No pose, no outcome.
	Kate->TestApplyLanding(60.f);
	TestFalse(TEXT("a 60 cm drop is not ours"), Kate->GetTraversalPoseState().IsLanding());
	TestFalse(TEXT("and has no outcome"), Kate->IsRecoveringFromLanding());

	// Moving, from 450 cm: today's roll, and the pose layer adds a landing look to it without touching it.
	Kate->TestSetMoveInput(1.f, 1.f);
	Kate->TestSetMoveDirection(FVector(0.f, 1.f, 0.f));
	Kate->TestApplyLanding(450.f);
	TestTrue(TEXT("450 cm moving is still a roll"), Kate->IsRolling());
	TestEqual(TEXT("still 0.5 s long"), Kate->TestRollSeconds(), 0.5f);
	TestEqual(TEXT("still half-height 50"), Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), 50.f);
	TestTrue(TEXT("the pose has a landing under way (no clip in a test: the legs absorb it)"), Kate->GetTraversalPoseState().IsLanding());
	TestFalse(TEXT("no roll clip plays, so the scripted spin is the roll's body"), Kate->IsRollClipPlaying());
	Kate->TestTickLanding(0.6f);
	TestFalse(TEXT("and the roll still ends on time"), Kate->IsRecoveringFromLanding());

	// Standing, from 600 cm: today's stumble.
	Kate->TestSetMoveInput(0.f, 0.f);
	Kate->TestApplyLanding(600.f);
	TestTrue(TEXT("600 cm standing is still a stumble"), Kate->IsStumbling());
	TestEqual(TEXT("still 0.4 s"), Kate->TestStumbleSeconds(), 0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseRoles, "Hawkeye.Traversal.Pose.LandingRoles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseRoles::RunTest(const FString& Parameters)
{
	auto Role = [this](const TCHAR* What, ELandingClip Clip, ECombatAnimRole Want)
	{ TestEqual(What, static_cast<int32>(UCombatAnimSet::LandingRoleFor(Clip)), static_cast<int32>(Want)); };
	Role(TEXT("light stand"), ELandingClip::StandLight, ECombatAnimRole::LandStandLight);
	Role(TEXT("heavy stand"), ELandingClip::StandHeavy, ECombatAnimRole::LandStandHeavy);
	Role(TEXT("light walk"), ELandingClip::WalkLight, ECombatAnimRole::LandWalkLight);
	Role(TEXT("heavy walk"), ELandingClip::WalkHeavy, ECombatAnimRole::LandWalkHeavy);
	Role(TEXT("light run"), ELandingClip::RunLight, ECombatAnimRole::LandRunLight);
	Role(TEXT("heavy run"), ELandingClip::RunHeavy, ECombatAnimRole::LandRunHeavy);
	Role(TEXT("light sprint"), ELandingClip::SprintLight, ECombatAnimRole::LandSprintLight);
	Role(TEXT("heavy sprint"), ELandingClip::SprintHeavy, ECombatAnimRole::LandSprintHeavy);
	Role(TEXT("roll"), ELandingClip::Roll, ECombatAnimRole::LandRoll);
	Role(TEXT("stumble"), ELandingClip::Stumble, ECombatAnimRole::LandStumble);
	Role(TEXT("no landing, no role"), ELandingClip::None, ECombatAnimRole::None);

	// Every landing role has a slot on the set: assign a montage to each and read it back.
	UCombatAnimSet* Set = NewObject<UCombatAnimSet>();
	for (uint8 Value = static_cast<uint8>(ECombatAnimRole::LandStandLight); Value <= static_cast<uint8>(ECombatAnimRole::FallLoop); ++Value)
	{
		TestFalse(*FString::Printf(TEXT("role %d starts empty"), Value), Set->HasMontage(static_cast<ECombatAnimRole>(Value)));
	}
	Set->LandRunHeavy = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Test/AM_LandRunHeavy_Lfoot.AM_LandRunHeavy_Lfoot")));
	Set->FallLoop = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Test/AM_FallLoop_Neutral.AM_FallLoop_Neutral")));
	TestTrue(TEXT("the heavy run slot is the heavy run role's"), Set->HasMontage(ECombatAnimRole::LandRunHeavy));
	TestTrue(TEXT("the fall loop slot is the fall loop role's"), Set->HasMontage(ECombatAnimRole::FallLoop));
	TestFalse(TEXT("and the light run is still empty"), Set->HasMontage(ECombatAnimRole::LandRunLight));
	TestEqual(TEXT("two slots assigned"), Set->CountAssigned(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPoseApply, "Hawkeye.Traversal.Pose.AppliedToTheGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalPoseApply::RunTest(const FString& Parameters)
{
	// The anim instance turns an actor-frame pose into the graph's variables: an inactive pose clears them, an active one
	// puts the alpha on and the turns in component space. No mesh in a test, so only the clearing and the alpha are read.
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UHawkeyeBowIKAnimInstance* Instance = NewObject<UHawkeyeBowIKAnimInstance>(Mesh);
	FTraversalPoseOutput Pose;
	Pose.Weight = 1.f;
	Pose.Pelvis = FTraversalPoseRules::QuatFromTilt(20.f, 0.f);
	Instance->ApplyTraversalPose(Pose);
	TestEqual(TEXT("with no owner nothing is written"), Instance->TraversalAlpha, 0.f);
	Instance->ApplyTraversalPose(FTraversalPoseOutput());
	TestEqual(TEXT("an inactive pose is alpha 0"), Instance->TraversalAlpha, 0.f);
	TestTrue(TEXT("and every turn is the identity"), Instance->TraversalPelvisRotation.IsNearlyZero()
		&& Instance->TraversalThighLRotation.IsNearlyZero() && Instance->TraversalNeckRotation.IsNearlyZero());
	TestTrue(TEXT("hawkeye.ZipPose is on by default"), UHawkeyeBowIKAnimInstance::IsTraversalPoseEnabled());
	return true;
}

#endif
