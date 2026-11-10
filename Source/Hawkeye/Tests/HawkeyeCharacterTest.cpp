#include "Camera/CameraComponent.h"
#include "Combat/WeaponComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/LocomotionAnim.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeAimTest
{
	static AHawkeyeAimTestCharacter* Spawn(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
			AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharacterAimSlowsAndTightens, "Hawkeye.Character.AimSlowsAndTightens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharacterAimSlowsAndTightens::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Frank = HawkeyeAimTest::Spawn(TestWorld);
	if (!Frank)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	UWeaponComponent* Weapon = Frank->GetWeaponComponent();
	if (!Weapon)
	{
		AddError(TEXT("The character has no weapon component."));
		return false;
	}
	Weapon->GiveWeapon(12, 24);

	// Keyboard input: always full, so the character runs.
	Frank->TestSetMoveInput(1.f, 0.f);

	TestFalse(TEXT("Starts hip-firing"), Frank->IsAiming());
	TestEqual(TEXT("At run speed"), Frank->MaxWalkSpeed(), Frank->TestRunSpeed());

	Frank->StartAim();
	TestTrue(TEXT("Aiming"), Frank->IsAiming());
	TestEqual(TEXT("Aiming drops to a walk"), Frank->MaxWalkSpeed(), Frank->TestWalkSpeed());
	TestTrue(TEXT("The weapon is aiming too"), Weapon->IsAiming());
	TestEqual(TEXT("And uses the tight cone"), Weapon->GetCurrentSpreadDegrees(), Weapon->AimSpreadDegrees);
	TestTrue(TEXT("Aiming faces the camera"), Frank->GetCharacterMovement()->bUseControllerDesiredRotation);
	TestFalse(TEXT("Not the movement direction"), Frank->GetCharacterMovement()->bOrientRotationToMovement);

	Frank->StopAim();
	TestFalse(TEXT("No longer aiming"), Frank->IsAiming());
	TestEqual(TEXT("Run speed restored"), Frank->MaxWalkSpeed(), Frank->TestRunSpeed());
	TestTrue(TEXT("Back to facing where it moves"), Frank->GetCharacterMovement()->bOrientRotationToMovement);
	TestFalse(TEXT("The weapon lowered as well"), Weapon->IsAiming());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharacterSprintCancelsAim, "Hawkeye.Character.SprintCancelsAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharacterSprintCancelsAim::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Frank = HawkeyeAimTest::Spawn(TestWorld);
	if (!Frank)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	UWeaponComponent* Weapon = Frank->GetWeaponComponent();
	Weapon->GiveWeapon(12, 24);

	Frank->StartAim();
	TestTrue(TEXT("Aiming"), Frank->IsAiming());

	Frank->TestSetSprinting(true);
	TestFalse(TEXT("Breaking into a sprint drops the aim"), Frank->IsAiming());
	TestFalse(TEXT("And the weapon with it"), Weapon->IsAiming());
	TestEqual(TEXT("Sprint speed wins"), Frank->MaxWalkSpeed(), Frank->TestSprintSpeed());

	// Holding the aim button while sprinting must not sneak the aim back on.
	Frank->StartAim();
	TestFalse(TEXT("Aiming is refused while sprinting"), Frank->IsAiming());
	TestEqual(TEXT("Still at sprint speed"), Frank->MaxWalkSpeed(), Frank->TestSprintSpeed());

	Frank->TestSetSprinting(false);
	Frank->StartAim();
	TestTrue(TEXT("Aiming works again once the sprint ends"), Frank->IsAiming());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharacterAimFOVBlend, "Hawkeye.Character.AimFOVBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharacterAimFOVBlend::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Frank = HawkeyeAimTest::Spawn(TestWorld);
	if (!Frank)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	Frank->GetWeaponComponent()->GiveWeapon(12, 24);

	TestEqual(TEXT("Starts at the hip FOV"), Frank->GetCurrentFOV(), Frank->TestHipFOV());

	Frank->StartAim();
	TestEqual(TEXT("The FOV does not snap on the frame the aim starts"),
		Frank->GetCurrentFOV(), Frank->TestHipFOV());

	// Half the blend should land half way between the two values.
	const float Blend = Frank->TestAimBlendSeconds();
	Frank->TestTickAim(Blend * 0.5f);
	const float Midpoint = (Frank->TestHipFOV() + Frank->TestAimFOV()) * 0.5f;
	TestTrue(TEXT("Half a blend is half way down"), FMath::IsNearlyEqual(Frank->GetCurrentFOV(), Midpoint, 0.5f));

	Frank->TestTickAim(Blend);
	TestTrue(TEXT("A full blend arrives at the aim FOV"),
		FMath::IsNearlyEqual(Frank->GetCurrentFOV(), Frank->TestAimFOV(), 0.05f));

	Frank->StopAim();
	Frank->TestTickAim(Blend * 2.f);
	TestTrue(TEXT("Lowering the sights returns to the hip FOV"),
		FMath::IsNearlyEqual(Frank->GetCurrentFOV(), Frank->TestHipFOV(), 0.05f));

	return true;
}

/**
 * Mouse feel. The raw Look value is one degree per mouse unit, which was unusably fast in the
 * first playtest; the sensitivity scales it and aiming scales it again. A test world has no
 * game instance, so this exercises the fallback property rather than UHawkeyeSettingsSubsystem.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharacterLookSensitivity, "Hawkeye.Character.LookSensitivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharacterLookSensitivity::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;

	AHawkeyeAimTestCharacter* Frank = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
		AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Frank spawned"), Frank))
	{
		return false;
	}

	TestEqual(TEXT("Hip look sensitivity is the tuned default"),
		Frank->GetEffectiveLookSensitivity(), 0.2f);

	Frank->GetWeaponComponent()->GiveWeapon(12, 24);
	Frank->StartAim();

	TestEqual(TEXT("Aiming slows the look by AimLookMultiplier"),
		Frank->GetEffectiveLookSensitivity(), 0.2f * 0.7f);

	Frank->StopAim();
	TestEqual(TEXT("Lowering the sights restores it"),
		Frank->GetEffectiveLookSensitivity(), 0.2f);

	return true;
}

/**
 * The third-person rig from claude-docs/gameplay-semantics.md: a 350 cm boom lifted 60, turned
 * by the control rotation, lagging in position and rotation, probing against walls on the Camera
 * channel, the camera on its end, and a body that faces where it walks with nothing hidden.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharacterThirdPersonRig, "Hawkeye.Character.ThirdPersonRig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharacterThirdPersonRig::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeAimTest::Spawn(TestWorld);
	if (!Kate || !Kate->GetCameraBoom() || !Kate->GetFollowCamera())
	{
		AddError(TEXT("Could not spawn a character with a camera boom and a camera."));
		return false;
	}

	const USpringArmComponent* Boom = Kate->GetCameraBoom();
	TestEqual(TEXT("The boom is 350 cm long"), Boom->TargetArmLength, 350.f);
	TestEqual(TEXT("Its socket is lifted 60 cm and 70 right"), Boom->SocketOffset, FVector(0.f, 70.f, 60.f));
	TestTrue(TEXT("The control rotation turns the boom"), Boom->bUsePawnControlRotation);
	TestTrue(TEXT("With camera lag on"), Boom->bEnableCameraLag);
	TestEqual(TEXT("At lag speed 10"), Boom->CameraLagSpeed, 10.f);
	TestTrue(TEXT("Rotation lags too"), Boom->bEnableCameraRotationLag);
	TestTrue(TEXT("The boom probes for walls"), Boom->bDoCollisionTest);
	TestEqual(TEXT("On the Camera channel"), Boom->ProbeChannel.GetValue(), ECC_Camera);
	TestTrue(TEXT("With a real sphere"), Boom->ProbeSize > 0.f);
	TestTrue(TEXT("The camera hangs off the boom"), Kate->GetFollowCamera()->GetAttachParent() == Boom);
	TestFalse(TEXT("And leaves the rotating to it"), Kate->GetFollowCamera()->bUsePawnControlRotation);

	TestFalse(TEXT("The body does not snap to the controller's yaw"), Kate->bUseControllerRotationYaw);
	const UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	TestTrue(TEXT("It turns to face where it moves"), Movement && Movement->bOrientRotationToMovement);

	const USkeletalMeshComponent* Body = Kate->GetBodyMesh();
	TestTrue(TEXT("The owner sees the whole body; no bones are hidden for the camera any more"),
		Body && !Body->bOwnerNoSee);

	return true;
}

/**
 * The body is the mannequin at a -90 degree yaw, exactly like the thugs', so its legs face the
 * way it walks. Same check, same reason: see Hawkeye.Thug.FacesTravelDirection.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharacterBodyFacesForward, "Hawkeye.Character.BodyFacesForward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharacterBodyFacesForward::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator(0.f, 35.f, 0.f)));
	if (!Kate || !Kate->GetMesh())
	{
		AddError(TEXT("Could not spawn a character with a body mesh."));
		return false;
	}

	const FVector Facing = HawkeyeLocomotion::GetMeshFacing(Kate->GetMesh());
	TestTrue(TEXT("The body mesh faces the way the actor does"),
		FVector::DotProduct(Facing.GetSafeNormal2D(), Kate->GetActorForwardVector()) > 0.99f);
	TestTrue(TEXT("Walking forwards, the legs point forwards"),
		HawkeyeLocomotion::GetFacingAlongVelocity(
			Kate->GetMesh(), Kate->GetActorForwardVector() * 450.f) > 0.7f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
