#include "Engine/GameInstance.h"
#include "HawkeyeGameMode.h"
#include "Misc/AutomationTest.h"
#include "Phone/PhoneInbox.h"
#include "Player/HawkeyeTapHold.h"
#include "Save/HawkeyeCampaignState.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Vfx/SnowfallComponent.h"
#include "World/HawkeyeFrameWatch.h"
#include "World/HawkeyeResidentAssets.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The small rules from the load-time and stability pass: phone texts on their delay survive a save,
 * the D-pad down tap and hold, snow only outdoors, the screenshot file check, and the frame watch
 * the game mode logs after the playable mark.
 */
namespace HawkeyeStabilityTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static FHawkeyePhoneMessage Message(const TCHAR* Objective, float Delay)
	{
		FHawkeyePhoneMessage Row;
		Row.Sender = FText::FromString(TEXT("[Grills]"));
		Row.Text = FText::FromString(TEXT("[text]"));
		Row.Trigger = EHawkeyeMessageTrigger::ObjectiveCompleted;
		Row.TriggerObjectiveId = FName(Objective);
		Row.DelaySeconds = Delay;
		return Row;
	}

	static UPhoneInbox* MakeInbox()
	{
		UPhoneInbox* Inbox = NewObject<UPhoneInbox>();
		Inbox->SetMessages({
			{ TEXT("slow"), Message(TEXT("reach_roof"), 5.f) },
			{ TEXT("quick"), Message(TEXT("reach_roof"), 1.f) },
			{ TEXT("later"), Message(TEXT("cross_block"), 3.f) },
		});
		return Inbox;
	}
}

// --- Phone: pending messages ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePhonePendingSurvivesSave, "Hawkeye.Phone.PendingSurvivesSave",
	HawkeyeStabilityTest::Flags)

bool FHawkeyePhonePendingSurvivesSave::RunTest(const FString& Parameters)
{
	using namespace HawkeyeStabilityTest;
	UPhoneInbox* Inbox = MakeInbox();
	TestEqual(TEXT("reach_roof schedules its two texts"), Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveCompleted,
		TEXT("reach_roof"), NAME_None), 2);
	TestEqual(TEXT("After 2 s the 1 s text is in"), Inbox->Advance(2.f).Num(), 1);

	// The save is written now: "slow" has 3 s left.
	UHawkeyeCampaignState* Campaign = NewObject<UHawkeyeCampaignState>();
	Inbox->Export(Campaign->ReceivedMessages, Campaign->ReadMessages);
	Inbox->ExportPending(Campaign->PendingMessages, Campaign->PendingMessageSeconds);
	TestEqual(TEXT("One text is on its delay"), Campaign->PendingMessages.Num(), 1);
	TestEqual(TEXT("It is the 5 s one"), Campaign->PendingMessages.Num() > 0 ? Campaign->PendingMessages[0] : NAME_None, FName(TEXT("slow")));
	TestEqual(TEXT("With 3 s left"), Campaign->PendingMessageSeconds.Num() > 0 ? Campaign->PendingMessageSeconds[0] : -1.f, 3.f, 0.01f);

	// A load: a fresh inbox reads the save back.
	UPhoneInbox* Loaded = MakeInbox();
	Loaded->Import(Campaign->ReceivedMessages, Campaign->ReadMessages);
	Loaded->ImportPending(Campaign->PendingMessages, Campaign->PendingMessageSeconds);
	TestTrue(TEXT("The arrived text is still in"), Loaded->HasArrived(TEXT("quick")));
	TestTrue(TEXT("The pending text is scheduled again"), Loaded->IsScheduled(TEXT("slow")));
	TestEqual(TEXT("With the delay it had left"), Loaded->GetSecondsLeft(TEXT("slow")), 3.f, 0.01f);
	TestEqual(TEXT("Not before its time"), Loaded->Advance(2.9f).Num(), 0);
	const TArray<FName> Arrived = Loaded->Advance(0.2f);
	TestTrue(TEXT("Then it arrives"), Arrived.Num() == 1 && Arrived[0] == FName(TEXT("slow")));
	TestEqual(TEXT("A trigger after the load cannot schedule it twice"), Loaded->HandleTrigger(
		EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("reach_roof"), NAME_None), 0);

	// Bad data: unknown ids, arrived ones, repeats and a missing delay.
	UPhoneInbox* Odd = MakeInbox();
	Odd->Import({ TEXT("quick") }, {});
	Odd->ImportPending({ TEXT("nobody"), TEXT("quick"), TEXT("later"), TEXT("later"), TEXT("slow") }, { 1.f, 1.f, -4.f, 9.f });
	TestFalse(TEXT("An unknown id is dropped"), Odd->IsScheduled(TEXT("nobody")));
	TestFalse(TEXT("An arrived text is not scheduled again"), Odd->IsScheduled(TEXT("quick")));
	TestEqual(TEXT("A repeat is dropped, a negative delay is 0"), Odd->GetSecondsLeft(TEXT("later")), 0.f);
	TestEqual(TEXT("A missing delay is 0"), Odd->GetSecondsLeft(TEXT("slow")), 0.f);
	TestEqual(TEXT("Two scheduled"), Odd->GetScheduledCount(), 2);

	Campaign->ResetCampaign();
	TestTrue(TEXT("A new game forgets the pending texts"), Campaign->PendingMessages.IsEmpty() && Campaign->PendingMessageSeconds.IsEmpty());
	return true;
}

// --- Controller: D-pad down tap and hold ---------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInputPhoneHoldVsSlotTap, "Hawkeye.Input.PhoneHoldVsSlotTap",
	HawkeyeStabilityTest::Flags)

bool FHawkeyeInputPhoneHoldVsSlotTap::RunTest(const FString& Parameters)
{
	FHawkeyeTapHold Button;
	Button.HoldSeconds = 0.4f;

	// A tap: nothing on the press or while down, slot 2 on the release.
	Button.Press(10.0);
	TestTrue(TEXT("A tap does nothing while down"), Button.Tick(10.1) == EHawkeyeTapHold::None);
	TestTrue(TEXT("A tap fires on release"), Button.Release(10.2) == EHawkeyeTapHold::Tap);
	TestTrue(TEXT("And only once"), Button.Release(10.3) == EHawkeyeTapHold::None);

	// A hold: the phone at 0.4 s, and the release after it selects nothing.
	Button.Press(20.0);
	TestTrue(TEXT("Not a hold at 0.39 s"), Button.Tick(20.39) == EHawkeyeTapHold::None);
	TestTrue(TEXT("The hold fires at 0.4 s"), Button.Tick(20.4) == EHawkeyeTapHold::Hold);
	TestTrue(TEXT("Once"), Button.Tick(20.8) == EHawkeyeTapHold::None);
	TestTrue(TEXT("Letting go of a hold is not a tap"), Button.Release(21.0) == EHawkeyeTapHold::None);

	// A hold that no tick saw (a long frame) still counts as the hold on release, never as a tap.
	Button.Press(30.0);
	TestTrue(TEXT("A late hold is a hold"), Button.Release(30.5) == EHawkeyeTapHold::Hold);

	// A release with no press (the pad was held through a level change).
	FHawkeyeTapHold Fresh;
	TestTrue(TEXT("A stray release is nothing"), Fresh.Release(1.0) == EHawkeyeTapHold::None);
	TestFalse(TEXT("And the button is up"), Fresh.IsDown());
	return true;
}

// --- Snow only outdoors ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeVfxSnowOnlyOutdoors, "Hawkeye.Vfx.SnowOnlyOutdoors", HawkeyeStabilityTest::Flags)

bool FHawkeyeVfxSnowOnlyOutdoors::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	TestFalse(TEXT("No game mode: no weather"), AHawkeyeGameMode::WantsOutdoorWeather(static_cast<const AGameModeBase*>(nullptr)));
	TestFalse(TEXT("No world: no snow"), USnowfallComponent::ShouldSnowIn(nullptr));
	TestFalse(TEXT("A world without a Hawkeye game mode has none"), USnowfallComponent::ShouldSnowIn(TestWorld.Get()));

	AHawkeyeGameMode* Outdoors = Cast<AHawkeyeGameMode>(TestWorld.SpawnActor(AHawkeyeGameMode::StaticClass(), FVector::ZeroVector,
		FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Game mode spawned"), Outdoors))
	{
		return false;
	}
	TestTrue(TEXT("The default is outdoors (the district)"), AHawkeyeGameMode::WantsOutdoorWeather(Outdoors));
	Outdoors->bOutdoorWeather = false;
	TestFalse(TEXT("An interior (the playable scene) turns it off"), AHawkeyeGameMode::WantsOutdoorWeather(Outdoors));
	return true;
}

// --- Screenshots: the file check -------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotFileCheck, "Hawkeye.Shots.FileCheck", HawkeyeStabilityTest::Flags)

bool FHawkeyeScreenshotFileCheck::RunTest(const FString& Parameters)
{
	using HawkeyeShots::IsFreshCapture;
	const FDateTime Requested(2026, 9, 27, 10, 0, 0);
	TestFalse(TEXT("No file fails"), IsFreshCapture(false, 0, FDateTime::MinValue(), Requested));
	TestFalse(TEXT("An empty file fails"), IsFreshCapture(true, 0, Requested + FTimespan::FromSeconds(1.0), Requested));
	TestFalse(TEXT("Last run's file fails"), IsFreshCapture(true, 900000, Requested - FTimespan::FromMinutes(5.0), Requested));
	TestFalse(TEXT("Just past the slack fails"), IsFreshCapture(true, 900000, Requested - FTimespan::FromSeconds(2.5), Requested));
	TestTrue(TEXT("Within the file-time slack passes"), IsFreshCapture(true, 900000, Requested - FTimespan::FromSeconds(1.0), Requested));
	TestTrue(TEXT("Written after the request passes"), IsFreshCapture(true, 900000, Requested + FTimespan::FromSeconds(0.2), Requested));
	TestEqual(TEXT("A test that asked for nothing has nothing outstanding"), HawkeyeShots::GetOutstanding(this), 0);
	TestEqual(TEXT("And no failures"), HawkeyeShots::GetFailures(this), 0);
	return true;
}

// --- The frame watch after the playable mark ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLoadFrameWatch, "Hawkeye.Load.FrameWatch", HawkeyeStabilityTest::Flags)

bool FHawkeyeLoadFrameWatch::RunTest(const FString& Parameters)
{
	FHawkeyeFrameWatch Watch;
	Watch.ThresholdSeconds = 0.1;
	TestFalse(TEXT("A 16 ms frame is fine"), Watch.AddFrame(0.016, 0.016, false));
	TestTrue(TEXT("A 100 ms frame is a hitch"), Watch.AddFrame(0.1, 0.116, false));
	TestTrue(TEXT("A 480 ms capture is a hitch"), Watch.AddFrame(0.48, 0.6, true));
	TestFalse(TEXT("Another fine frame"), Watch.AddFrame(0.02, 0.62, false));
	TestEqual(TEXT("Four frames"), Watch.Frames, 4);
	TestEqual(TEXT("Two hitches"), Watch.HitchFrames, 2);
	TestEqual(TEXT("One of them a capture"), Watch.ScreenshotHitchFrames, 1);
	TestEqual(TEXT("One the game's"), Watch.GetGameHitchFrames(), 1);
	TestEqual(TEXT("The worst is the capture"), Watch.WorstSeconds, 0.48, 1e-6);
	TestTrue(TEXT("And says so"), Watch.bWorstWasScreenshot);
	TestEqual(TEXT("It was the third frame"), Watch.WorstFrame, 2);
	TestEqual(TEXT("The worst of the game's own is 100 ms"), Watch.WorstGameSeconds, 0.1, 1e-6);
	TestEqual(TEXT("At 0.116 s"), Watch.WorstGameAtSeconds, 0.116, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLoadPerfWindow, "Hawkeye.Load.PerfWindow", HawkeyeStabilityTest::Flags)

bool FHawkeyeLoadPerfWindow::RunTest(const FString& Parameters)
{
	FHawkeyePerfWindow Window;
	TestEqual(TEXT("An empty window's p95 is 0"), Window.PercentileMs(95.f), 0.f);
	for (int32 Index = 1; Index <= 20; ++Index)
	{
		Window.AddFrame(float(Index), 2.f, 3.f, 4.f, false);
	}
	Window.AddFrame(500.f, 0.f, 0.f, 0.f, true);
	Window.AddFrame(60.f, 2.f, 3.f, 4.f, false);
	TestEqual(TEXT("The capture is left out"), Window.Num(), 21);
	TestEqual(TEXT("and counted apart"), Window.CaptureFrames, 1);
	TestEqual(TEXT("One frame over 50 ms"), Window.FramesOver, 1);
	TestEqual(TEXT("Worst is the 60 ms frame"), Window.WorstMs(), 60.f);
	TestEqual(TEXT("p95 of 1..20 and 60 is 20 (rank 20 of 21)"), Window.PercentileMs(95.f), 20.f);
	TestEqual(TEXT("Average"), Window.AverageMs(), (210.f + 60.f) / 21.f, 1e-4f);
	TestEqual(TEXT("Game thread average"), Window.AverageGameMs(), 2.f, 1e-4f);
	TestEqual(TEXT("GPU average"), Window.AverageGpuMs(), 4.f, 1e-4f);
	return true;
}

// --- What stays loaded across a level change -----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLoadResidentAssets, "Hawkeye.Load.ResidentAssets", HawkeyeStabilityTest::Flags)

bool FHawkeyeLoadResidentAssets::RunTest(const FString& Parameters)
{
	// A game instance subsystem must live in a game instance; a bare one will do (no world).
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UHawkeyeResidentAssets* Resident = NewObject<UHawkeyeResidentAssets>(GameInstance);
	UObject* Thing = NewObject<UHawkeyeTestListener>();
	Resident->Keep(nullptr);
	TestEqual(TEXT("Null is ignored"), Resident->GetKeptCount(), 0);
	Resident->Keep(Thing);
	Resident->Keep(Thing);
	TestEqual(TEXT("An object is kept once"), Resident->GetKeptCount(), 1);
	TestEqual(TEXT("A null world adds nothing"), Resident->KeepWorldContent(nullptr), 0);
	TestNull(TEXT("A bare object has no game instance to keep things in"), UHawkeyeResidentAssets::Get(Thing));
	return true;
}

#endif
