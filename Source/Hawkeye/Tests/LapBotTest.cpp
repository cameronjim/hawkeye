#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Tests/LapBot.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The laps' shared aim bot (Tests/LapBot.h): it turns at a human rate, the short way round, and aims the crosshair, not
 * the lens, at the target. The camera audit's angular-speed rule depends on it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLapBotAim, "Hawkeye.Camera.LapBotAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLapBotAim::RunTest(const FString& Parameters)
{
	// A frame is at most AimTurnRate / 60 degrees, however far the target is.
	const float Frame = HawkeyeLapBot::AimTurnRate / 60.f;
	const FRotator Step = HawkeyeLapBot::StepAim(FRotator(0.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f), Frame);
	TestEqual(TEXT("A half turn is one 9-degree frame, not a flip"), FMath::Abs(Step.Yaw), Frame, 0.001f);
	TestEqual(TEXT("Pitch is limited the same way"), HawkeyeLapBot::StepAim(FRotator::ZeroRotator, FRotator(60.f, 0.f, 0.f), Frame).Pitch, Frame, 0.001f);

	// It takes the short way round the circle.
	TestEqual(TEXT("350 to 10 degrees is 20 degrees the short way"), HawkeyeLapBot::StepAim(FRotator(0.f, 350.f, 0.f), FRotator(0.f, 10.f, 0.f), 100.f).Yaw,
		10.f, 0.001f);
	TestEqual(TEXT("and a step inside the limit lands on the target"), HawkeyeLapBot::StepAim(FRotator(0.f, 5.f, 0.f), FRotator(0.f, 8.f, 0.f), Frame).Yaw,
		8.f, 0.001f);

	// A half turn takes a third of a second, in 60 frames of 9 degrees: 20 of them.
	FRotator View = FRotator::ZeroRotator;
	int32 Frames = 0;
	while (FMath::Abs(FRotator::NormalizeAxis(180.f - View.Yaw)) > 0.01f && Frames < 100)
	{
		View = HawkeyeLapBot::StepAim(View, FRotator(0.f, 180.f, 0.f), Frame);
		++Frames;
	}
	TestEqual(TEXT("A target behind her is reached in 20 frames at 60 fps (a third of a second)"), Frames, 20);

	// It aims the crosshair: from the lens position the view will have, so the target ends up on the line the lens looks along.
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(),
		FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Kate spawned"), Kate) || !TestNotNull(TEXT("with a boom"), Kate->GetCameraBoom()))
	{
		return false;
	}
	for (const FVector& Target : { FVector(4000.f, 600.f, 700.f), FVector(-900.f, 2500.f, 200.f), FVector(-3000.f, -100.f, 150.f) })
	{
		const FRotator Want = HawkeyeLapBot::AimWanted(Kate, FRotator::ZeroRotator, Target);
		const FVector Lens = HawkeyeLapBot::IdealLens(Kate, Want);
		const FVector Along = Want.Vector();
		const FVector ToTarget = (Target - Lens).GetSafeNormal();
		TestTrue(FString::Printf(TEXT("The crosshair is on %s (%.2f degrees off)"), *Target.ToCompactString(),
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Along, ToTarget), -1.f, 1.f)))),
			FVector::DotProduct(Along, ToTarget) > FMath::Cos(FMath::DegreesToRadians(0.5f)));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
