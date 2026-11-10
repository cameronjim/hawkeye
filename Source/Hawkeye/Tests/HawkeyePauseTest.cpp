#include "HawkeyeGameMode.h"
#include "Combat/HealthComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyePauseTest
{
	static AHawkeyePauseTestController* Spawn(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyePauseTestController>(TestWorld.SpawnActor(
			AHawkeyePauseTestController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePauseTogglesBothWays, "Hawkeye.Pause.TogglesBothWays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePauseTogglesBothWays::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyePauseTestController* PC = HawkeyePauseTest::Spawn(TestWorld);
	if (!PC)
	{
		AddError(TEXT("Could not spawn the test player controller."));
		return false;
	}

	TestFalse(TEXT("The game starts unpaused"), PC->IsPauseMenuOpen());
	TestTrue(TEXT("And pausing is allowed"), PC->CanTogglePause());

	PC->TogglePause();
	TestTrue(TEXT("Escape opens the pause menu"), PC->IsPauseMenuOpen());

	PC->TogglePause();
	TestFalse(TEXT("Escape again closes it"), PC->IsPauseMenuOpen());

	PC->TogglePause();
	TestTrue(TEXT("And it opens a third time"), PC->IsPauseMenuOpen());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePauseIgnoredDuringFlashback, "Hawkeye.Pause.IgnoredDuringFlashback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePauseIgnoredDuringFlashback::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyePauseTestController* PC = HawkeyePauseTest::Spawn(TestWorld);
	if (!PC)
	{
		AddError(TEXT("Could not spawn the test player controller."));
		return false;
	}

	PC->TestSetFlashbackActive(true);
	TestFalse(TEXT("Pausing is refused while a flashback plays"), PC->CanTogglePause());

	PC->TogglePause();
	TestFalse(TEXT("So Escape does nothing"), PC->IsPauseMenuOpen());

	PC->TestSetFlashbackActive(false);
	TestTrue(TEXT("Pausing is allowed again once the slideshow ends"), PC->CanTogglePause());

	PC->TogglePause();
	TestTrue(TEXT("And Escape works"), PC->IsPauseMenuOpen());

	// The lockout is symmetric: Escape cannot close an already-open menu mid-flashback either,
	// so the slideshow is the only thing that can hand the pause back.
	PC->TestSetFlashbackActive(true);
	PC->TogglePause();
	TestTrue(TEXT("The menu cannot be closed by Escape mid-flashback either"), PC->IsPauseMenuOpen());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePauseIgnoredWhileDead, "Hawkeye.Pause.IgnoredWhileDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePauseIgnoredWhileDead::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyePauseTestController* PC = HawkeyePauseTest::Spawn(TestWorld);
	AHawkeyeCharacter* Frank = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!PC || !Frank)
	{
		AddError(TEXT("Could not spawn the controller and pawn."));
		return false;
	}

	PC->Possess(Frank);
	TestTrue(TEXT("Pausing is allowed while alive"), PC->CanTogglePause());

	UHealthComponent* Health = Frank->GetHealthComponent();
	if (!Health)
	{
		AddError(TEXT("The pawn has no health component."));
		return false;
	}

	Health->ApplyDamage(Health->MaxHealth * 2.f, nullptr);
	TestFalse(TEXT("The pawn is dead"), Health->IsAlive());
	TestFalse(TEXT("Pausing is refused once dead"), PC->CanTogglePause());

	PC->TogglePause();
	TestFalse(TEXT("So Escape does nothing over the death screen"), PC->IsPauseMenuOpen());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGameModeRestartDelay, "Hawkeye.Mission.RestartDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGameModeRestartDelay::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeGameMode* GameMode = Cast<AHawkeyeGameMode>(TestWorld.SpawnActor(
		AHawkeyeGameMode::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!GameMode)
	{
		AddError(TEXT("Could not spawn the game mode."));
		return false;
	}

	TestEqual(TEXT("Death waits two seconds by default"), GameMode->RestartDelaySeconds, 2.f);
	TestFalse(TEXT("No restart pending at the start"), GameMode->IsRestartPending());

	// A negative delay means "use RestartDelaySeconds", which is what dying passes.
	GameMode->RestartMission();
	TestTrue(TEXT("Restart is pending"), GameMode->IsRestartPending());

	// Repeat calls are ignored, so the pause menu cannot stack a second travel on a death.
	GameMode->RestartMission(0.f);
	TestTrue(TEXT("Still exactly one restart pending"), GameMode->IsRestartPending());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
