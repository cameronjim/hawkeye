#include "Misc/AutomationTest.h"
#include "Mission/MissionFlowController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeFlowTest
{
	static FString Name(EMissionFlowStep Step)
	{
		switch (Step)
		{
		case EMissionFlowStep::Idle:          return TEXT("Idle");
		case EMissionFlowStep::EndCard:       return TEXT("EndCard");
		case EMissionFlowStep::Flashback:     return TEXT("Flashback");
		case EMissionFlowStep::FinalCard:     return TEXT("FinalCard");
		case EMissionFlowStep::OpenNextLevel: return TEXT("OpenNextLevel");
		case EMissionFlowStep::OpenMenuLevel: return TEXT("OpenMenuLevel");
		case EMissionFlowStep::Done:          return TEXT("Done");
		default:                              return TEXT("?");
		}
	}

	/**
	 * Every step the flow visits from Begin until it reaches Done, as one readable string, so a
	 * failure says which sequence came out rather than "the two values are not equal".
	 */
	static FString RunToEnd(UMissionFlowController* Flow, bool bFlashback, bool bNextLevel)
	{
		TArray<FString> Steps;
		Steps.Add(Name(Flow->Begin(bFlashback, bNextLevel)));

		// Generous bound: any sequence longer than this is a cycle, not a mission ending.
		for (int32 Guard = 0; Guard < 16 && Flow->IsRunning(); ++Guard)
		{
			Steps.Add(Name(Flow->Advance()));
		}

		return FString::Join(Steps, TEXT(" -> "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlowCardFlashbackNextLevel, "Hawkeye.MissionFlow.CardFlashbackNextLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlowCardFlashbackNextLevel::RunTest(const FString& Parameters)
{
	UMissionFlowController* Flow = NewObject<UMissionFlowController>();

	const FString Steps = HawkeyeFlowTest::RunToEnd(Flow, /*bFlashback=*/true, /*bNextLevel=*/true);

	TestEqual(TEXT("Card, flashback, next level"), Steps,
		FString(TEXT("EndCard -> Flashback -> OpenNextLevel -> Done")));
	TestFalse(TEXT("The sequence is over"), Flow->IsRunning());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlowNoFlashbackSkipsIt, "Hawkeye.MissionFlow.NoFlashbackSkipsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlowNoFlashbackSkipsIt::RunTest(const FString& Parameters)
{
	UMissionFlowController* Flow = NewObject<UMissionFlowController>();

	const FString Steps = HawkeyeFlowTest::RunToEnd(Flow, /*bFlashback=*/false, /*bNextLevel=*/true);

	TestEqual(TEXT("A mission with no flashback travels straight on"), Steps,
		FString(TEXT("EndCard -> OpenNextLevel -> Done")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlowNoNextLevelReturnsToMenu, "Hawkeye.MissionFlow.NoNextLevelReturnsToMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlowNoNextLevelReturnsToMenu::RunTest(const FString& Parameters)
{
	UMissionFlowController* Flow = NewObject<UMissionFlowController>();

	// The last mission of the campaign: card, flashback, then the card again until a key.
	const FString Steps = HawkeyeFlowTest::RunToEnd(Flow, /*bFlashback=*/true, /*bNextLevel=*/false);

	TestEqual(TEXT("The campaign end returns to the menu"), Steps,
		FString(TEXT("EndCard -> Flashback -> FinalCard -> OpenMenuLevel -> Done")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlowBareMissionStillEnds, "Hawkeye.MissionFlow.BareMissionStillEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlowBareMissionStillEnds::RunTest(const FString& Parameters)
{
	UMissionFlowController* Flow = NewObject<UMissionFlowController>();

	const FString Steps = HawkeyeFlowTest::RunToEnd(Flow, /*bFlashback=*/false, /*bNextLevel=*/false);

	TestEqual(TEXT("A mission with neither still ends cleanly"), Steps,
		FString(TEXT("EndCard -> FinalCard -> OpenMenuLevel -> Done")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlowIdleAndDoneAreTerminal, "Hawkeye.MissionFlow.IdleAndDoneAreTerminal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlowIdleAndDoneAreTerminal::RunTest(const FString& Parameters)
{
	UMissionFlowController* Flow = NewObject<UMissionFlowController>();

	TestEqual(TEXT("Starts idle"), HawkeyeFlowTest::Name(Flow->GetStep()), FString(TEXT("Idle")));
	TestFalse(TEXT("And is not running"), Flow->IsRunning());

	// An end card that fires twice, or a stray Advance, must not start a sequence on its own.
	TestEqual(TEXT("Advancing from idle does nothing"), HawkeyeFlowTest::Name(Flow->Advance()), FString(TEXT("Idle")));

	HawkeyeFlowTest::RunToEnd(Flow, /*bFlashback=*/true, /*bNextLevel=*/true);
	TestEqual(TEXT("Ends at Done"), HawkeyeFlowTest::Name(Flow->GetStep()), FString(TEXT("Done")));
	TestEqual(TEXT("Advancing from Done does nothing"), HawkeyeFlowTest::Name(Flow->Advance()), FString(TEXT("Done")));

	Flow->Reset();
	TestEqual(TEXT("Reset goes back to idle"), HawkeyeFlowTest::Name(Flow->GetStep()), FString(TEXT("Idle")));
	TestFalse(TEXT("And clears the flashback flag"), Flow->HasFlashback());
	TestFalse(TEXT("And the next-level flag"), Flow->HasNextLevel());

	// The same object has to be reusable, because one world can complete two missions.
	TestEqual(TEXT("It can run again after a reset"),
		HawkeyeFlowTest::Name(Flow->Begin(/*bFlashback=*/false, /*bNextLevel=*/true)), FString(TEXT("EndCard")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
