#include "HawkeyePlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/ChallengeResultsWidget.h"
#include "UI/HawkeyeInventoryWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The Tab screen used to have no way out once it was open: Slate's own keyboard navigation treats
 * an unhandled Tab as "focus the next widget" and swallows it before the game ever sees it, so the
 * pawn's InventoryAction binding (Tab tap closes) never fired. UHawkeyeInventoryWidget now claims
 * Tab, Escape and the pad's View/B itself while it holds keyboard focus (claude-docs, "Inventory
 * and hotbar"). These tests exercise that state machine directly; real Slate key delivery is not
 * something a -nullrhi run can observe (claude-docs/testing.md).
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryTabTapTogglesOpenAndClosed,
	"Hawkeye.Inventory.TabTapTogglesOpenAndClosed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryTabTapTogglesOpenAndClosed::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(
		TestWorld.SpawnActor(AHawkeyePlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!PC)
	{
		AddError(TEXT("Could not spawn the test player controller."));
		return false;
	}
	PC->InventoryWidgetClass = UHawkeyeInventoryWidget::StaticClass();

	TestFalse(TEXT("Closed at the start"), PC->IsInventoryOpen());

	PC->ToggleInventory();
	TestTrue(TEXT("A tap opens it"), PC->IsInventoryOpen());

	PC->ToggleInventory();
	TestFalse(TEXT("A second tap closes it"), PC->IsInventoryOpen());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryEscapeClosesWithoutOpeningPause,
	"Hawkeye.Inventory.EscapeClosesWithoutOpeningPause", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryEscapeClosesWithoutOpeningPause::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyePauseTestController* PC = Cast<AHawkeyePauseTestController>(
		TestWorld.SpawnActor(AHawkeyePauseTestController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!PC)
	{
		AddError(TEXT("Could not spawn the test player controller."));
		return false;
	}
	PC->InventoryWidgetClass = UHawkeyeInventoryWidget::StaticClass();

	PC->SetInventoryOpen(true);
	TestTrue(TEXT("The inventory is open"), PC->IsInventoryOpen());

	PC->TestInputPause();
	TestFalse(TEXT("Escape closes the inventory"), PC->IsInventoryOpen());
	TestFalse(TEXT("and does not also open the pause menu on the same press"), PC->IsPauseMenuOpen());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryWidgetEscapeTabPadClose,
	"Hawkeye.Inventory.WidgetEscapeTabPadClose", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryWidgetEscapeTabPadClose::RunTest(const FString& Parameters)
{
	// UUserWidget::SetOwningPlayer / CreateWidget hard-require a real ULocalPlayer (an engine
	// check, not something this project's code can route around), which a -nullrhi automation
	// world never has. So this cannot drive the fix end to end through PC->IsInventoryOpen(); what
	// it CAN prove is the actual regression: every one of these keys must come back Handled from
	// the widget itself, or Slate's own "Tab focuses the next widget" navigation (or a plain
	// bubble-up) swallows the press before AHawkeyePlayerController ever sees it. That swallowing,
	// while this screen had focus, was the whole bug.
	UHawkeyeInventoryWidgetTestHelper* Widget =
		NewObject<UHawkeyeInventoryWidgetTestHelper>(GetTransientPackage(), UHawkeyeInventoryWidgetTestHelper::StaticClass());
	if (!Widget)
	{
		AddError(TEXT("Could not create the test widget."));
		return false;
	}

	TestTrue(TEXT("Escape is claimed, not left for Slate to bubble away"),
		Widget->TestKeyDown(EKeys::Escape).IsEventHandled());
	TestTrue(TEXT("Pad B is claimed"), Widget->TestKeyDown(EKeys::Gamepad_FaceButton_Right).IsEventHandled());
	TestTrue(TEXT("Tab is claimed (Slate's default is to eat it for focus navigation instead)"),
		Widget->TestKeyDown(EKeys::Tab).IsEventHandled());
	Widget->TestKeyUp(EKeys::Tab);
	TestTrue(TEXT("Pad View (the same IA_Inventory binding as Tab) is claimed"),
		Widget->TestKeyDown(EKeys::Gamepad_Special_Left).IsEventHandled());
	Widget->TestKeyUp(EKeys::Gamepad_Special_Left);
	TestTrue(TEXT("Anything unrelated is swallowed too, not left to leak into the paused game"),
		Widget->TestKeyDown(EKeys::A).IsEventHandled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryTabHoldOpensWheelReleaseCloses,
	"Hawkeye.Inventory.TabHoldOpensWheelReleaseCloses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryTabHoldOpensWheelReleaseCloses::RunTest(const FString& Parameters)
{
	// No PC or pawn here (see WidgetEscapeTabPadClose's comment: this harness has no ULocalPlayer,
	// so nothing can own this widget). EnterWheelFromHold/FinishWheelFromHold null-check the pawn
	// and controller they would otherwise drive, so the tap/hold state machine itself - the actual
	// fix - still runs and is what this checks. Driven with an injected clock (TestPressTab/
	// TestTickTab/TestReleaseTab), the same way the generic FHawkeyeTapHold tests are: a bare
	// FHawkeyeTestWorld never ticks, so GetRealTimeSeconds() never advances on its own.
	UHawkeyeInventoryWidgetTestHelper* Widget = NewObject<UHawkeyeInventoryWidgetTestHelper>(
		GetTransientPackage(), UHawkeyeInventoryWidgetTestHelper::StaticClass());
	if (!Widget)
	{
		AddError(TEXT("Could not create the test widget."));
		return false;
	}

	Widget->TestPressTab(0.25f, 10.0);
	TestFalse(TEXT("Not a hold yet"), Widget->TestIsWheelOpenedFromHold());

	Widget->TestTickTab(10.1);
	TestFalse(TEXT("Not at 0.1 s"), Widget->TestIsWheelOpenedFromHold());

	Widget->TestTickTab(10.25);
	TestTrue(TEXT("Holding Tab over the open inventory swaps it for the wheel at 0.25 s"),
		Widget->TestIsWheelOpenedFromHold());

	Widget->TestReleaseTab(10.4);
	TestFalse(TEXT("Releasing Tab finishes the swap back"), Widget->TestIsWheelOpenedFromHold());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeResultsEnterAndEscapeKeys,
	"Hawkeye.Challenge.ResultsEnterAndEscapeKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeResultsEnterAndEscapeKeys::RunTest(const FString& Parameters)
{
	UChallengeResultsWidgetTestHelper* Widget =
		NewObject<UChallengeResultsWidgetTestHelper>(GetTransientPackage(), UChallengeResultsWidgetTestHelper::StaticClass());
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	if (!Widget || !Listener)
	{
		AddError(TEXT("Could not create the results widget or its listener."));
		return false;
	}
	Widget->OnRetryClicked.AddDynamic(Listener, &UHawkeyeTestListener::HandleChallengeRetry);
	Widget->OnLeaveClicked.AddDynamic(Listener, &UHawkeyeTestListener::HandleChallengeLeave);

	Widget->TestKeyDown(EKeys::Enter);
	TestEqual(TEXT("Enter retries"), Listener->ChallengeRetryCount, 1);
	TestEqual(TEXT("and does not leave"), Listener->ChallengeLeaveCount, 0);

	Widget->TestKeyDown(EKeys::Escape);
	TestEqual(TEXT("Escape leaves"), Listener->ChallengeLeaveCount, 1);
	TestEqual(TEXT("without also retrying"), Listener->ChallengeRetryCount, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
