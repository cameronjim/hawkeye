#include "Camera/CameraComponent.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The third-person camera and movement rules from claude-docs/gameplay-semantics.md (PLANNED:
 * third-person camera and look; PLANNED: traversal), plus the stage 2 feel numbers: camera
 * targets and blend, gait selection, jump apex, slide, landing roll and fall damage.
 */
namespace HawkeyeMovementTest
{
	static AHawkeyeAimTestCharacter* Spawn(const FHawkeyeTestWorld& TestWorld, const FVector& Location = FVector::ZeroVector)
	{
		return Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
			AHawkeyeAimTestCharacter::StaticClass(), Location, FRotator::ZeroRotator));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraTargetsTest, "Hawkeye.Camera.Targets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraTargetsTest::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}

	const FHawkeyeCameraTargets Hip = Kate->ComputeCameraTargets(false);
	TestEqual(TEXT("Hip arm is 350"), Hip.ArmLength, 350.f);
	TestEqual(TEXT("Hip socket is lifted 60 and 70 right"), Hip.SocketOffset, FVector(0.f, 70.f, 60.f));
	TestEqual(TEXT("Hip FOV is 90"), Hip.FieldOfView, 90.f);

	const FHawkeyeCameraTargets Aim = Kate->ComputeCameraTargets(true);
	TestEqual(TEXT("Aim arm is 180"), Aim.ArmLength, 180.f);
	TestEqual(TEXT("Aim socket is out over the right shoulder, clear of the bow arm"), Aim.SocketOffset, FVector(0.f, 70.f, 65.f));
	TestEqual(TEXT("Aim FOV is 70"), Aim.FieldOfView, 70.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraLookUp, "Hawkeye.Camera.LookUpShortensAndLifts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraLookUp::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}

	struct FCase
	{
		float Pitch;
		float Arm;
		float SocketZ;
		float Lift;
	};
	// 350 / 60 / 0 up to +20 degrees, a straight blend to 220 / 110 / 150 at +60, held beyond.
	const FCase Cases[] = {
		{ -40.f, 350.f, 60.f, 0.f }, { 0.f, 350.f, 60.f, 0.f }, { 20.f, 350.f, 60.f, 0.f },
		{ 30.f, 317.5f, 72.5f, 37.5f }, { 40.f, 285.f, 85.f, 75.f }, { 60.f, 220.f, 110.f, 150.f },
		{ 75.f, 220.f, 110.f, 150.f },
	};
	for (const FCase& Case : Cases)
	{
		const FHawkeyeCameraTargets Hip = Kate->ComputeCameraTargets(false, Case.Pitch);
		TestEqual(FString::Printf(TEXT("Arm at %.0f degrees"), Case.Pitch), Hip.ArmLength, Case.Arm, 0.01f);
		TestEqual(FString::Printf(TEXT("Socket Z at %.0f degrees"), Case.Pitch), static_cast<float>(Hip.SocketOffset.Z), Case.SocketZ, 0.01f);
		TestEqual(FString::Printf(TEXT("Pivot lift at %.0f degrees"), Case.Pitch), Hip.PivotLift, Case.Lift, 0.01f);
		TestEqual(FString::Printf(TEXT("Shoulder offset unchanged at %.0f degrees"), Case.Pitch), static_cast<float>(Hip.SocketOffset.Y), 70.f);
		TestEqual(FString::Printf(TEXT("FOV unchanged at %.0f degrees"), Case.Pitch), Hip.FieldOfView, 90.f);
	}
	TestEqual(TEXT("Aiming ignores the pitch"), Kate->ComputeCameraTargets(true, 60.f).ArmLength, 180.f);
	TestEqual(TEXT("The one-argument form is the level camera"), Kate->ComputeCameraTargets(false).ArmLength, 350.f);

	TestEqual(TEXT("Pitch clamps at +75"), Kate->ClampCameraPitch(89.f), 75.f);
	TestEqual(TEXT("Pitch clamps at -70"), Kate->ClampCameraPitch(-89.f), -70.f);
	TestEqual(TEXT("Pitch inside the limits is untouched"), Kate->ClampCameraPitch(30.f), 30.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraAimBlend, "Hawkeye.Camera.AimBlendEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraAimBlend::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	if (!Kate || !Kate->GetCameraBoom())
	{
		AddError(TEXT("Could not spawn a character with a camera boom."));
		return false;
	}
	USpringArmComponent* Boom = Kate->GetCameraBoom();

	Kate->TestTickAim(0.f);
	TestEqual(TEXT("Starts on the hip arm"), Boom->TargetArmLength, 350.f);

	Kate->StartAim();
	Kate->TestTickAim(0.f);
	TestEqual(TEXT("No snap on the frame aim starts"), Boom->TargetArmLength, 350.f);

	Kate->TestTickAim(Kate->TestAimBlendSeconds() * 0.5f);
	TestTrue(TEXT("Half a blend is half way in"), FMath::IsNearlyEqual(Boom->TargetArmLength, 265.f, 1.f));

	Kate->TestTickAim(Kate->TestAimBlendSeconds());
	TestTrue(TEXT("A full blend reaches 180"), FMath::IsNearlyEqual(Boom->TargetArmLength, 180.f, 0.01f));
	TestTrue(TEXT("Over the right shoulder"), Boom->SocketOffset.Equals(FVector(0.f, 70.f, 65.f), 0.01f));
	TestTrue(TEXT("At the aim FOV"), FMath::IsNearlyEqual(Kate->GetCurrentFOV(), 70.f, 0.01f));

	Kate->StopAim();
	Kate->TestTickAim(Kate->TestAimBlendSeconds());
	TestTrue(TEXT("Back to 350"), FMath::IsNearlyEqual(Boom->TargetArmLength, 350.f, 0.01f));
	TestTrue(TEXT("Back to the hip offset"), Boom->SocketOffset.Equals(FVector(0.f, 70.f, 60.f), 0.01f));
	TestTrue(TEXT("Back to the hip FOV"), FMath::IsNearlyEqual(Kate->GetCurrentFOV(), 90.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMovementGaitSelection, "Hawkeye.Movement.GaitSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMovementGaitSelection::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}

	auto Gait = [Kate](float Magnitude, float Held, bool bSprint, bool bCrouch, bool bSlide, bool bAim)
	{
		return Kate->SelectGait(Magnitude, Held, bSprint, bCrouch, bSlide, bAim);
	};

	TestEqual(TEXT("Keyboard runs at once"), Gait(1.f, 0.f, false, false, false, false), EHawkeyeGait::Run);
	TestEqual(TEXT("A full stick runs at once"), Gait(0.95f, 0.f, false, false, false, false), EHawkeyeGait::Run);
	TestEqual(TEXT("A medium stick walks at first"), Gait(0.6f, 0.1f, false, false, false, false), EHawkeyeGait::Walk);
	TestEqual(TEXT("And runs once held past 0.2 s"), Gait(0.6f, 0.25f, false, false, false, false), EHawkeyeGait::Run);
	TestEqual(TEXT("A light stick always walks"), Gait(0.3f, 5.f, false, false, false, false), EHawkeyeGait::Walk);
	TestEqual(TEXT("Shift sprints"), Gait(1.f, 1.f, true, false, false, false), EHawkeyeGait::Sprint);
	TestEqual(TEXT("Aiming walks"), Gait(1.f, 1.f, false, false, false, true), EHawkeyeGait::Walk);
	TestEqual(TEXT("Crouch beats sprint"), Gait(1.f, 1.f, true, true, false, false), EHawkeyeGait::Crouch);
	TestEqual(TEXT("Slide beats crouch"), Gait(1.f, 1.f, true, true, true, false), EHawkeyeGait::Slide);

	TestEqual(TEXT("Walk is 250"), Kate->GetGaitSpeed(EHawkeyeGait::Walk), 250.f);
	TestEqual(TEXT("Run is 500"), Kate->GetGaitSpeed(EHawkeyeGait::Run), 500.f);
	TestEqual(TEXT("Sprint is 700"), Kate->GetGaitSpeed(EHawkeyeGait::Sprint), 700.f);
	TestEqual(TEXT("Crouch is 200"), Kate->GetGaitSpeed(EHawkeyeGait::Crouch), 200.f);

	Kate->TestSetMoveInput(1.f, 0.f);
	TestEqual(TEXT("The movement component gets the run speed"), Kate->MaxWalkSpeed(), 500.f);
	Kate->TestSetSprinting(true);
	TestEqual(TEXT("And the sprint speed"), Kate->MaxWalkSpeed(), 700.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFallDamageCurve, "Hawkeye.Movement.FallDamageCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFallDamageCurve::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}

	TestEqual(TEXT("Nothing at 899 cm"), Kate->ComputeFallDamageFraction(899.f), 0.f);
	TestTrue(TEXT("Something at 900 cm"), Kate->ComputeFallDamageFraction(900.f) > 0.f);
	TestTrue(TEXT("More from higher up"),
		Kate->ComputeFallDamageFraction(1700.f) > Kate->ComputeFallDamageFraction(900.f));
	TestTrue(TEXT("60% at 2500 cm"), FMath::IsNearlyEqual(Kate->ComputeFallDamageFraction(2500.f), 0.6f));
	TestTrue(TEXT("Capped at 60% above it"), FMath::IsNearlyEqual(Kate->ComputeFallDamageFraction(6000.f), 0.6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFallNeverKills, "Hawkeye.Movement.FallNeverKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFallNeverKills::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	UHealthComponent* Health = Kate ? Kate->GetHealthComponent() : nullptr;
	if (!Health)
	{
		AddError(TEXT("Could not spawn a character with health."));
		return false;
	}

	const float Max = Health->GetMaxHealth();
	Kate->TestApplyLanding(2500.f);
	TestTrue(TEXT("A tenement drop costs 60%"), FMath::IsNearlyEqual(Health->GetCurrentHealth(), Max * 0.4f, 0.01f));
	TestEqual(TEXT("The landing height is recorded"), Kate->GetLastFallHeight(), 2500.f);

	Kate->TestApplyLanding(2500.f);
	TestTrue(TEXT("A second one leaves her standing"), Health->IsAlive());
	TestTrue(TEXT("On one point"), FMath::IsNearlyEqual(Health->GetCurrentHealth(), 1.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLandingRoll, "Hawkeye.Movement.LandingRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLandingRoll::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	UCharacterMovementComponent* Movement = Kate ? Kate->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Could not spawn a character with movement."));
		return false;
	}
	Movement->SetMovementMode(MOVE_Walking);
	const float Standing = Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float MaxHealth = Kate->GetHealthComponent()->GetMaxHealth();

	Kate->TestSetMoveInput(1.f, 1.f);
	Kate->TestSetMoveDirection(FVector(0.f, 1.f, 0.f));
	Kate->TestApplyLanding(390.f);
	TestFalse(TEXT("A 3.9 m drop does not roll"), Kate->IsRecoveringFromLanding());
	TestEqual(TEXT("Nor cost health"), Kate->GetHealthComponent()->GetCurrentHealth(), MaxHealth);

	Kate->TestApplyLanding(450.f);
	TestTrue(TEXT("A 4.5 m drop while moving rolls"), Kate->IsRolling());
	TestEqual(TEXT("The roll lasts 0.5 s"), Kate->TestRollSeconds(), 0.5f);
	TestTrue(TEXT("Along the move direction"), Kate->GetRollDirection().Equals(FVector(0.f, 1.f, 0.f), 0.01f));
	TestTrue(TEXT("Facing it"), FMath::IsNearlyEqual(Kate->GetActorRotation().Yaw, 90.f, 0.5f));
	TestEqual(TEXT("The capsule drops to half-height 50"), Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), 50.f);
	TestTrue(TEXT("Input is locked at the start"), Kate->IsLandingInputLocked());
	TestEqual(TEXT("No fall damage below 9 m"), Kate->GetHealthComponent()->GetCurrentHealth(), MaxHealth);
	const FRootMotionSource* Carry = Movement->GetRootMotionSource(FName(TEXT("LandingRoll"))).Get();
	if (TestNotNull(TEXT("A root motion force carries the roll"), Carry))
	{
		TestEqual(TEXT("Over 0.5 s"), Carry->Duration, 0.5f);
		const FRootMotionSource_ConstantForce* Force = static_cast<const FRootMotionSource_ConstantForce*>(Carry);
		TestTrue(TEXT("200 cm in 0.5 s along the stick"), Force->Force.Equals(FVector(0.f, 400.f, 0.f), 0.5f));
	}

	Kate->TestTickLanding(0.25f);
	TestTrue(TEXT("Halfway the camera is pitched 8 degrees down"), FMath::IsNearlyEqual(Kate->GetLandingCameraPitch(), -8.f, 0.01f));
	Kate->TestTickLanding(0.09f);
	TestTrue(TEXT("At 0.34 s input is still locked"), Kate->IsLandingInputLocked());
	Kate->TestTickLanding(0.02f);
	TestFalse(TEXT("At 0.36 s it is free"), Kate->IsLandingInputLocked());
	TestTrue(TEXT("Still rolling"), Kate->IsRolling());
	TestEqual(TEXT("Still low"), Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), 50.f);
	Kate->TestTickLanding(0.13f);
	TestTrue(TEXT("At 0.49 s still rolling"), Kate->IsRolling());
	Kate->TestTickLanding(0.02f);
	TestFalse(TEXT("At 0.51 s the roll is over"), Kate->IsRecoveringFromLanding());
	TestEqual(TEXT("Standing height again"), Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), Standing);
	TestEqual(TEXT("The camera is level again"), Kate->GetLandingCameraPitch(), 0.f);
	TestTrue(TEXT("The body is back on its mount"),
		Kate->GetMesh()->GetRelativeRotation().Equals(GetDefault<AHawkeyeAimTestCharacter>()->GetMesh()->GetRelativeRotation(), 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLandingStumble, "Hawkeye.Movement.LandingStumble",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLandingStumble::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	UCharacterMovementComponent* Movement = Kate ? Kate->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Could not spawn a character with movement."));
		return false;
	}
	Movement->SetMovementMode(MOVE_Walking);
	const float Standing = Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

	Kate->TestSetMoveInput(0.f, 0.f);
	Kate->TestApplyLanding(600.f);
	TestTrue(TEXT("A 6 m drop standing still stumbles"), Kate->IsStumbling());
	TestFalse(TEXT("Not a roll"), Kate->IsRolling());
	TestEqual(TEXT("The stumble lasts 0.4 s"), Kate->TestStumbleSeconds(), 0.4f);
	TestEqual(TEXT("The capsule stays at standing height"), Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), Standing);
	TestFalse(TEXT("Input is not locked"), Kate->IsLandingInputLocked());
	TestEqual(TEXT("Speed starts at 0"), Kate->GetLandingSpeedFactor(), 0.f);

	Kate->TestSetMoveInput(1.f, 1.f);
	Kate->TestTickLanding(0.2f);
	TestTrue(TEXT("Halfway it is half"), FMath::IsNearlyEqual(Kate->GetLandingSpeedFactor(), 0.5f, 0.01f));
	TestTrue(TEXT("Half of a run"), FMath::IsNearlyEqual(Kate->MaxWalkSpeed(), Kate->TestRunSpeed() * 0.5f, 1.f));
	TestEqual(TEXT("No camera pitch in a stumble"), Kate->GetLandingCameraPitch(), 0.f);
	Kate->TestTickLanding(0.19f);
	TestTrue(TEXT("At 0.39 s still stumbling"), Kate->IsStumbling());
	Kate->TestTickLanding(0.02f);
	TestFalse(TEXT("At 0.41 s it is over"), Kate->IsRecoveringFromLanding());
	TestEqual(TEXT("Full speed"), Kate->GetLandingSpeedFactor(), 1.f);
	TestEqual(TEXT("A run again"), Kate->MaxWalkSpeed(), Kate->TestRunSpeed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSlideDurationAndCapsule, "Hawkeye.Movement.SlideDurationAndCapsule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSlideDurationAndCapsule::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld);
	UCharacterMovementComponent* Movement = Kate ? Kate->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Could not spawn a character with movement."));
		return false;
	}

	const float Standing = Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->Velocity = FVector(700.f, 0.f, 0.f);

	TestFalse(TEXT("No slide without a sprint"), Kate->StartSlide());

	Kate->TestSetSprinting(true);
	Movement->Velocity = FVector(100.f, 0.f, 0.f);
	TestFalse(TEXT("No slide from a crawl"), Kate->StartSlide());

	Movement->Velocity = FVector(700.f, 0.f, 0.f);
	TestTrue(TEXT("Crouch mid-sprint slides"), Kate->StartSlide());
	TestTrue(TEXT("Sliding"), Kate->IsSliding());
	TestEqual(TEXT("On the slide gait"), Kate->GetGait(), EHawkeyeGait::Slide);
	TestTrue(TEXT("The capsule is shrunk"),
		Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() < Standing - 10.f);

	Kate->TestTickSlide(Kate->TestSlideSeconds() - 0.05f);
	TestTrue(TEXT("Still sliding just before the end"), Kate->IsSliding());

	Kate->TestTickSlide(0.1f);
	TestFalse(TEXT("Done after 0.7 s"), Kate->IsSliding());
	TestEqual(TEXT("The capsule is back to standing height"),
		Kate->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), Standing);
	TestFalse(TEXT("And she is not left crouched"), Kate->bIsCrouched);
	return true;
}

/**
 * The jump apex in a real, ticking movement component: no floor, no controller, the component
 * ticked by hand at 60 Hz. The engine integrates falling with the midpoint rule, so the apex is
 * exact to within one frame's travel.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeJumpHeight, "Hawkeye.Movement.JumpHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeJumpHeight::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeMovementTest::Spawn(TestWorld, FVector(0.f, 0.f, 50000.f));
	UCharacterMovementComponent* Movement = Kate ? Kate->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Could not spawn a character with movement."));
		return false;
	}

	Movement->bRunPhysicsWithNoController = true;
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.f, 0.f, Movement->JumpZVelocity);

	const float StartZ = Kate->GetActorLocation().Z;
	float ApexZ = StartZ;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Movement->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
		ApexZ = FMath::Max(ApexZ, Kate->GetActorLocation().Z);
	}

	const float Apex = ApexZ - StartZ;
	AddInfo(FString::Printf(TEXT("JumpZVelocity %.1f, apex %.1f cm"), Movement->JumpZVelocity, Apex));
	TestTrue(TEXT("The jump rose at all"), Apex > 1.f);
	TestTrue(TEXT("The apex is JumpHeight within 5 cm"), FMath::IsNearlyEqual(Apex, Kate->TestJumpHeight(), 5.f));
	TestTrue(TEXT("Air control is 0.3"), FMath::IsNearlyEqual(Movement->AirControl, 0.3f));
	return true;
}

/**
 * hawkeye.DebugMovement on a ledge: the line reads the hang, the shimmy at the speed she really moves (the capsule
 * is placed by hand, so its velocity is 0 and the gait reads idle), and each traversal move by name.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMovementDebugReadsTheHang, "Hawkeye.Movement.DebugLineReadsTheHang",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMovementDebugReadsTheHang::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A hang"), AHawkeyeCharacter::FormatTraversalDebugState(true, false, EHawkeyeParkourMove::None, 0.f, false),
		FString(TEXT("hang")));
	TestEqual(TEXT("A shimmy"), AHawkeyeCharacter::FormatTraversalDebugState(true, true, EHawkeyeParkourMove::None, 0.f, false),
		FString(TEXT("hang shimmy")));
	TestEqual(TEXT("A corner, with its time"),
		AHawkeyeCharacter::FormatTraversalDebugState(false, false, EHawkeyeParkourMove::HangCorner, 0.21f, false),
		FString(TEXT("corner 0.21 s")));
	TestEqual(TEXT("A leap"), AHawkeyeCharacter::FormatTraversalDebugState(false, false, EHawkeyeParkourMove::HangLeap, 0.1f, false),
		FString(TEXT("leap 0.10 s")));
	TestEqual(TEXT("A hop"), AHawkeyeCharacter::FormatTraversalDebugState(false, false, EHawkeyeParkourMove::HangHop, 0.f, false),
		FString(TEXT("hop 0.00 s")));
	TestEqual(TEXT("A move wins over the hang it starts from"),
		AHawkeyeCharacter::FormatTraversalDebugState(true, true, EHawkeyeParkourMove::Climb, 0.3f, false), FString(TEXT("climb 0.30 s")));
	TestEqual(TEXT("The sample's traversal"), AHawkeyeCharacter::FormatTraversalDebugState(false, false, EHawkeyeParkourMove::None, 0.f, true),
		FString(TEXT("traversal")));
	TestTrue(TEXT("On the ground, nothing: the gait line"),
		AHawkeyeCharacter::FormatTraversalDebugState(false, false, EHawkeyeParkourMove::None, 0.f, false).IsEmpty());

	// A real hang and shimmy, the line read through the character's tick.
	static constexpr float HalfHeight = 88.f;
	static constexpr float Top = 800.f;
	static constexpr float Step = 0.05f;
	const FHawkeyeTestWorld TestWorld;
	auto Box = [&TestWorld](const FVector& Min, const FVector& Max)
	{
		if (AHawkeyeTestBlocker* Blocker = Cast<AHawkeyeTestBlocker>(
				TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), (Min + Max) * 0.5f, FRotator::ZeroRotator)))
		{
			Blocker->SetExtent((Max - Min) * 0.5f);
		}
	};
	Box(FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	Box(FVector(100.f, -400.f, 0.f), FVector(130.f, 400.f, Top));
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(),
		FVector(55.f, 0.f, Top - 200.f + HalfHeight), FRotator::ZeroRotator));
	UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
	if (!TestNotNull(TEXT("Kate with parkour"), Parkour))
	{
		return false;
	}
	Kate->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Kate->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, -150.f);
	if (!TestTrue(TEXT("She catches the wall's top"), Parkour->TryCatchLedge()))
	{
		return false;
	}
	for (float Time = 0.f; Time < 1.5f && Parkour->IsPerformingMove(); Time += Step)
	{
		Parkour->AdvanceMove(Step);
		static_cast<AActor*>(Kate)->Tick(Step);
	}
	if (!TestTrue(TEXT("Hanging"), Parkour->IsHanging()))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		static_cast<AActor*>(Kate)->Tick(Step);
	}
	const FString Still = Kate->GetMovementDebugText();
	AddInfo(FString::Printf(TEXT("Hanging still: \"%s\"."), *Still));
	TestTrue(TEXT("Still on the ledge it reads the hang, not idle"), Still.StartsWith(TEXT("hang  0 cm/s")));
	TestTrue(TEXT("with how far along the ledge"), Still.Contains(TEXT("  along ")));

	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Parkour->SetHangInput(FVector(0.f, 1.f, 0.f));
		Parkour->AdvanceHang(Step);
		static_cast<AActor*>(Kate)->Tick(Step);
	}
	const FString Shimmy = Kate->GetMovementDebugText();
	AddInfo(FString::Printf(TEXT("Shimmying: \"%s\"."), *Shimmy));
	TestTrue(TEXT("Shimmying it says so"), Shimmy.StartsWith(TEXT("hang shimmy  ")));
	const int32 SpeedAt = FString(TEXT("hang shimmy  ")).Len();
	const float Speed = FCString::Atof(*Shimmy.Mid(SpeedAt));
	TestEqual(TEXT("at the shimmy's speed, 120 cm/s (not the velocity's 0)"), Speed, Parkour->ShimmySpeed, 12.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
