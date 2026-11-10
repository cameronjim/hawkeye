#include "Combat/TakedownComponent.h"
#include "Combat/Takedownable.h"
#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeTakedownTest
{
	/** A point Degrees away from directly behind a target that stands at the origin facing +X. */
	static FVector BehindAt(float Degrees, float Distance)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector(-FMath::Cos(Radians) * Distance, FMath::Sin(Radians) * Distance, 0.f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTakedownAngleCheck, "Hawkeye.Takedown.AngleCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTakedownAngleCheck::RunTest(const FString& Parameters)
{
	const FVector TargetLocation = FVector::ZeroVector;
	const FVector TargetForward = FVector::ForwardVector;
	const float MaxAngle = 60.f;

	TestTrue(TEXT("Directly behind passes"),
		UTakedownComponent::IsBehindTarget(HawkeyeTakedownTest::BehindAt(0.f, 100.f), TargetLocation, TargetForward, MaxAngle));

	TestTrue(TEXT("Just inside the cone passes"),
		UTakedownComponent::IsBehindTarget(HawkeyeTakedownTest::BehindAt(59.f, 100.f), TargetLocation, TargetForward, MaxAngle));

	TestTrue(TEXT("Exactly at MaxAngleDegrees passes"),
		UTakedownComponent::IsBehindTarget(HawkeyeTakedownTest::BehindAt(60.f, 100.f), TargetLocation, TargetForward, MaxAngle));

	TestFalse(TEXT("Just outside the cone fails"),
		UTakedownComponent::IsBehindTarget(HawkeyeTakedownTest::BehindAt(61.f, 100.f), TargetLocation, TargetForward, MaxAngle));

	TestFalse(TEXT("Directly in front fails"),
		UTakedownComponent::IsBehindTarget(FVector(100.f, 0.f, 0.f), TargetLocation, TargetForward, MaxAngle));

	TestFalse(TEXT("Directly beside fails"),
		UTakedownComponent::IsBehindTarget(FVector(0.f, 100.f, 0.f), TargetLocation, TargetForward, MaxAngle));

	// Height must not matter: the check is ground-projected.
	TestTrue(TEXT("Standing on a crate behind the target still passes"),
		UTakedownComponent::IsBehindTarget(FVector(-100.f, 0.f, 300.f), TargetLocation, TargetForward, MaxAngle));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTakedownRangeCheck, "Hawkeye.Takedown.RangeCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTakedownRangeCheck::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UTakedownComponent* Takedown = NewObject<UTakedownComponent>();

	TestEqual(TEXT("Default range"), Takedown->Range, 150.f);
	TestEqual(TEXT("Default cone"), Takedown->MaxAngleDegrees, 60.f);

	AActor* Thug = TestWorld.SpawnActor(AHawkeyeTestTakedownTarget::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Thug)
	{
		AddError(TEXT("Failed to spawn the test thug."));
		return false;
	}
	Thug->Tags.Add(FName(TEXT("Thug")));

	TestTrue(TEXT("Inside range is valid"), Takedown->IsValidTakedownTarget(Thug, FVector(-100.f, 0.f, 0.f)));
	TestFalse(TEXT("Outside range is not"), Takedown->IsValidTakedownTarget(Thug, FVector(-200.f, 0.f, 0.f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTakedownTagRequired, "Hawkeye.Takedown.TagRequired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTakedownTagRequired::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UTakedownComponent* Takedown = NewObject<UTakedownComponent>();

	AActor* Untagged = TestWorld.SpawnActor(AHawkeyeTestTakedownTarget::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AActor* PlainActor = TestWorld.SpawnActor(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Untagged || !PlainActor)
	{
		AddError(TEXT("Failed to spawn the test actors."));
		return false;
	}
	PlainActor->Tags.Add(FName(TEXT("Thug")));

	const FVector Behind(-100.f, 0.f, 0.f);

	TestFalse(TEXT("An untagged takedownable is rejected"), Takedown->IsValidTakedownTarget(Untagged, Behind));
	TestFalse(TEXT("A tagged actor that is not ITakedownable is rejected"), Takedown->IsValidTakedownTarget(PlainActor, Behind));
	TestFalse(TEXT("Null is rejected"), Takedown->IsValidTakedownTarget(nullptr, Behind));

	Untagged->Tags.Add(FName(TEXT("Thug")));

	// Asserted piece by piece so a failure says which half of the check went wrong.
	TestTrue(TEXT("Tag present"), Untagged->ActorHasTag(FName(TEXT("Thug"))));
	TestTrue(TEXT("Implements ITakedownable"), Untagged->GetClass()->ImplementsInterface(UTakedownable::StaticClass()));
	TestTrue(TEXT("Attacker is behind"),
		UTakedownComponent::IsBehindTarget(Behind, Untagged->GetActorLocation(), Untagged->GetActorForwardVector(), 60.f));
	TestTrue(TEXT("Target does not veto"), ITakedownable::Execute_CanBeTakenDown(Untagged, nullptr));
	TestTrue(TEXT("Tagged and takedownable is accepted"), Takedown->IsValidTakedownTarget(Untagged, Behind));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTakedownAlertedThugRejected, "Hawkeye.Takedown.AlertedThugRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTakedownAlertedThugRejected::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UTakedownComponent* Takedown = NewObject<UTakedownComponent>();

	AHawkeyeTestTakedownTarget* Thug = Cast<AHawkeyeTestTakedownTarget>(
		TestWorld.SpawnActor(AHawkeyeTestTakedownTarget::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Thug)
	{
		AddError(TEXT("Failed to spawn the test thug."));
		return false;
	}
	Thug->Tags.Add(FName(TEXT("Thug")));

	const FVector Behind(-100.f, 0.f, 0.f);

	TestTrue(TEXT("A calm thug can be taken down"), Takedown->IsValidTakedownTarget(Thug, Behind));

	Thug->bAlerted = true;
	TestFalse(TEXT("An alerted thug cannot"), Takedown->IsValidTakedownTarget(Thug, Behind));

	Takedown->bAlertedThugsAreValid = true;
	TestTrue(TEXT("bAlertedThugsAreValid overrides the veto"), Takedown->IsValidTakedownTarget(Thug, Behind));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
