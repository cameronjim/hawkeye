#include "Misc/AutomationTest.h"
#include "Dialogue/DialogueQueue.h"
#include "Flashback/FlashbackDefinition.h"
#include "HawkeyePlayerController.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionFlowController.h"
#include "Phone/PhoneInbox.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSceneReturn.h"
#include "UI/ChapterTitleClock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeNarrativeTest
{
	static const FName Chapter(TEXT("DA_CH01_Test"));

	static FHawkeyePhoneMessage Message(const TCHAR* Sender, const TCHAR* Text, EHawkeyeMessageTrigger Trigger,
		const TCHAR* Key, float Delay, FName InChapter = Chapter, bool bRead = false)
	{
		FHawkeyePhoneMessage Row;
		Row.Sender = FText::FromString(Sender);
		Row.Text = FText::FromString(Text);
		Row.Trigger = Trigger;
		(Trigger == EHawkeyeMessageTrigger::Event ? Row.TriggerEvent : Row.TriggerObjectiveId) = FName(Key);
		Row.DelaySeconds = Delay;
		Row.ChapterId = InChapter;
		Row.bRead = bRead;
		return Row;
	}

	static FHawkeyeDialogueLine Line(const TCHAR* Speaker, float Seconds)
	{
		FHawkeyeDialogueLine Row;
		Row.Speaker = FName(Speaker);
		Row.Situation = EHawkeyeBanterSituation::Scripted;
		Row.Text = FText::FromString(TEXT("[line]"));
		Row.DurationSeconds = Seconds;
		return Row;
	}

	static FHawkeyeDialogueSequenceRow Step(const TCHAR* Sequence, int32 Order, const TCHAR* LineRow, float Gap)
	{
		FHawkeyeDialogueSequenceRow Row;
		Row.Sequence = FName(Sequence);
		Row.Order = Order;
		Row.Line = FName(LineRow);
		Row.GapSeconds = Gap;
		return Row;
	}

	/** Three one-second lines and a sequence of them, listed out of order, with gaps 2, 1, 1. */
	static UDialogueQueue* MakeQueue()
	{
		UDialogueQueue* Queue = NewObject<UDialogueQueue>();
		Queue->SetLines({
			{ TEXT("l1"), Line(TEXT("Kate"), 1.f) },
			{ TEXT("l2"), Line(TEXT("Clint"), 1.f) },
			{ TEXT("l3"), Line(TEXT("Kate"), 1.f) },
			{ TEXT("solo"), Line(TEXT("Clint"), 0.f) },
		});
		Queue->SetSequenceRows({
			Step(TEXT("seq"), 3, TEXT("l3"), 1.f),
			Step(TEXT("seq"), 1, TEXT("l1"), 2.f),
			Step(TEXT("seq"), 2, TEXT("l2"), 1.f),
		});
		return Queue;
	}

	static FString FlowName(EMissionFlowStep Step)
	{
		return UEnum::GetValueAsString(Step).RightChop(FString(TEXT("EMissionFlowStep::")).Len());
	}

	static FString RunRoute(const FMissionFlowRoute& Route)
	{
		UMissionFlowController* Flow = NewObject<UMissionFlowController>();
		TArray<FString> Steps;
		Steps.Add(FlowName(Flow->BeginRoute(Route)));
		for (int32 Guard = 0; Guard < 16 && Flow->IsRunning(); ++Guard)
		{
			Steps.Add(FlowName(Flow->Advance()));
		}
		return FString::Join(Steps, TEXT(" -> "));
	}
}

// --- Phone --------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePhoneObjectiveTriggers, "Hawkeye.Phone.ObjectiveTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePhoneObjectiveTriggers::RunTest(const FString& Parameters)
{
	using namespace HawkeyeNarrativeTest;
	UPhoneInbox* Inbox = NewObject<UPhoneInbox>();
	Inbox->SetMessages({
		{ TEXT("done"), Message(TEXT("[A]"), TEXT("[A text 1]"), EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("reach_roof"), 2.f) },
		{ TEXT("started"), Message(TEXT("[B]"), TEXT("[B text 1]"), EHawkeyeMessageTrigger::ObjectiveStarted, TEXT("cross_block"), 0.f) },
		{ TEXT("elsewhere"), Message(TEXT("[A]"), TEXT("[A text 2]"), EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("reach_roof"), 0.f, FName(TEXT("DA_CH02"))) },
		{ TEXT("opening"), Message(TEXT("[A]"), TEXT("[A text 3]"), EHawkeyeMessageTrigger::ChapterStart, TEXT(""), 1.f) },
	});

	TestEqual(TEXT("Completing reach_roof schedules its message, not the other chapter's"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("reach_roof"), Chapter), 1);
	TestEqual(TEXT("Starting reach_roof schedules nothing"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveStarted, TEXT("reach_roof"), Chapter), 0);
	TestEqual(TEXT("A second completion does not schedule it twice"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("reach_roof"), Chapter), 0);
	TestTrue(TEXT("Nothing arrives before the delay"), Inbox->Advance(1.9f).IsEmpty());
	const TArray<FName> Arrived = Inbox->Advance(0.2f);
	TestTrue(TEXT("It arrives after its 2 s"), Arrived.Num() == 1 && Arrived[0] == FName(TEXT("done")));
	TestEqual(TEXT("Once arrived it never comes again"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("reach_roof"), Chapter), 0);

	TestEqual(TEXT("cross_block becoming current schedules the started message"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveStarted, TEXT("cross_block"), Chapter), 1);
	TestEqual(TEXT("A zero delay arrives on the next step"), Inbox->Advance(0.f).Num(), 1);

	TestEqual(TEXT("The chapter start schedules its message"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ChapterStart, NAME_None, Chapter), 1);
	TestEqual(TEXT("Another chapter's start does not"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ChapterStart, NAME_None, FName(TEXT("DA_CH02"))), 0);
	TestTrue(TEXT("The other chapter's reach_roof message never came"), !Inbox->HasArrived(TEXT("elsewhere")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePhoneEventTriggers, "Hawkeye.Phone.EventTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePhoneEventTriggers::RunTest(const FString& Parameters)
{
	using namespace HawkeyeNarrativeTest;
	UPhoneInbox* Inbox = NewObject<UPhoneInbox>();
	Inbox->SetMessages({
		{ TEXT("late"), Message(TEXT("[A]"), TEXT("[A text 1]"), EHawkeyeMessageTrigger::Event, TEXT("saw_tower"), 3.f, NAME_None) },
		{ TEXT("soon"), Message(TEXT("[B]"), TEXT("[B text 1]"), EHawkeyeMessageTrigger::Event, TEXT("saw_tower"), 1.f, NAME_None) },
		{ TEXT("objective"), Message(TEXT("[B]"), TEXT("[B text 2]"), EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("saw_tower"), 0.f) },
	});

	TestEqual(TEXT("An unknown event schedules nothing"), Inbox->HandleTrigger(EHawkeyeMessageTrigger::Event, TEXT("nope"), Chapter), 0);
	TestEqual(TEXT("The event schedules both of its messages, not the objective row with the same key"),
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::Event, TEXT("saw_tower"), Chapter), 2);
	const TArray<FName> First = Inbox->Advance(1.f);
	TestTrue(TEXT("The 1 s message first"), First.Num() == 1 && First[0] == FName(TEXT("soon")));
	const TArray<FName> Second = Inbox->Advance(5.f);
	TestTrue(TEXT("Then the 3 s one"), Second.Num() == 1 && Second[0] == FName(TEXT("late")));
	TestFalse(TEXT("The objective row stayed unsent"), Inbox->HasArrived(TEXT("objective")));
	TestEqual(TEXT("A notification shows the first line"),
		UPhoneInbox::FirstLine(FText::FromString(TEXT("[line one]\n[line two]"))).ToString(), FString(TEXT("[line one]")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePhoneUnreadCount, "Hawkeye.Phone.UnreadCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePhoneUnreadCount::RunTest(const FString& Parameters)
{
	using namespace HawkeyeNarrativeTest;
	UPhoneInbox* Inbox = NewObject<UPhoneInbox>();
	const TMap<FName, FHawkeyePhoneMessage> Rows = {
		{ TEXT("a1"), Message(TEXT("[A]"), TEXT("[A text 1]"), EHawkeyeMessageTrigger::Event, TEXT("go"), 0.f, NAME_None) },
		{ TEXT("a2"), Message(TEXT("[A]"), TEXT("[A text 2]"), EHawkeyeMessageTrigger::Event, TEXT("go"), 1.f, NAME_None) },
		{ TEXT("b1"), Message(TEXT("[B]"), TEXT("[B text 1]"), EHawkeyeMessageTrigger::Event, TEXT("go"), 2.f, NAME_None) },
		{ TEXT("old"), Message(TEXT("[B]"), TEXT("[B text 2]"), EHawkeyeMessageTrigger::Event, TEXT("go"), 3.f, NAME_None, true) },
	};
	Inbox->SetMessages(Rows);
	TestEqual(TEXT("Nothing unread before anything arrives"), Inbox->GetUnreadCount(), 0);
	Inbox->HandleTrigger(EHawkeyeMessageTrigger::Event, TEXT("go"), NAME_None);
	Inbox->Advance(10.f);
	TestEqual(TEXT("Four arrived, one already read: three unread"), Inbox->GetUnreadCount(), 3);
	TestEqual(TEXT("B wrote last, so B is the first contact"), Inbox->GetContacts()[0], FString(TEXT("[B]")));
	TestEqual(TEXT("A has two unread"), Inbox->GetUnreadCountFrom(TEXT("[A]")), 2);

	TestEqual(TEXT("Reading A's thread reads two"), Inbox->MarkThreadRead(TEXT("[A]")), 2);
	TestEqual(TEXT("One left, B's"), Inbox->GetUnreadCount(), 1);
	TestEqual(TEXT("Reading it again changes nothing"), Inbox->MarkThreadRead(TEXT("[A]")), 0);
	TestEqual(TEXT("A's thread is oldest first"), Inbox->GetThread(TEXT("[A]"))[0].Id, FName(TEXT("a1")));

	// The save mirror: what goes out comes back, read state included.
	TArray<FName> Received;
	TArray<FName> Read;
	Inbox->Export(Received, Read);
	UPhoneInbox* Loaded = NewObject<UPhoneInbox>();
	Loaded->SetMessages(Rows);
	Loaded->Import(Received, Read);
	TestEqual(TEXT("A loaded phone has the same four"), Loaded->GetArrivedCount(), 4);
	TestEqual(TEXT("And the same one unread"), Loaded->GetUnreadCount(), 1);
	TestEqual(TEXT("An arrived message is not scheduled again after a load"),
		Loaded->HandleTrigger(EHawkeyeMessageTrigger::Event, TEXT("go"), NAME_None), 0);
	return true;
}

// --- Chapter title ------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterTitleTiming, "Hawkeye.ChapterTitle.Timing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChapterTitleTiming::RunTest(const FString& Parameters)
{
	FHawkeyeTitleCardClock Clock;
	Clock.Start();
	TestEqual(TEXT("Clear at the start"), Clock.GetAlpha(), 0.f);
	Clock.Advance(0.375f);
	TestEqual(TEXT("Half faded in at half the fade"), Clock.GetAlpha(), 0.5f, 0.01f);
	Clock.Advance(0.375f);
	TestEqual(TEXT("Full at the end of the fade in"), Clock.GetAlpha(), 1.f, 0.01f);
	TestFalse(TEXT("Holding"), Clock.Advance(2.9f));
	TestEqual(TEXT("Still full 3 s in the hold"), Clock.GetAlpha(), 1.f, 0.01f);
	TestEqual(TEXT("The card lasts fade in + 3 s + fade out"), Clock.GetTotalSeconds(), 4.5f, 0.001f);
	Clock.Advance(0.1f + 0.375f);
	TestEqual(TEXT("Half faded out"), Clock.GetAlpha(), 0.5f, 0.01f);
	TestTrue(TEXT("Over at 4.5 s"), Clock.Advance(0.4f));
	TestFalse(TEXT("And not running"), Clock.IsRunning());
	TestFalse(TEXT("Never skipped"), Clock.WasSkipped());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterTitleSkip, "Hawkeye.ChapterTitle.Skip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChapterTitleSkip::RunTest(const FString& Parameters)
{
	FHawkeyeTitleCardClock Clock;
	Clock.Start();
	Clock.Advance(0.3f);
	TestFalse(TEXT("A press in the first 0.5 s is ignored"), Clock.TrySkip());
	Clock.Advance(1.2f);
	TestTrue(TEXT("A press in the hold skips"), Clock.TrySkip());
	TestTrue(TEXT("It fades out from where it was"), Clock.IsFadingOut());
	TestEqual(TEXT("Starting at full"), Clock.GetAlpha(), 1.f, 0.01f);
	TestFalse(TEXT("A second press does nothing"), Clock.TrySkip());
	TestEqual(TEXT("Over 0.75 s after the skip"), Clock.GetTotalSeconds(), 2.25f, 0.001f);
	TestFalse(TEXT("Not over just before"), Clock.Advance(0.7f));
	TestTrue(TEXT("Over at the end of the fade"), Clock.Advance(0.06f));

	// A skip during the fade in fades out from the partial alpha, not from full.
	FHawkeyeTitleCardClock Early;
	Early.FadeInSeconds = 2.f;
	Early.Start();
	Early.Advance(1.f);
	TestTrue(TEXT("Skip in the fade in"), Early.TrySkip());
	TestEqual(TEXT("From half"), Early.GetAlpha(), 0.5f, 0.01f);
	return true;
}

// --- Chapter end --------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterEndFlowOrder, "Hawkeye.MissionFlow.ChapterEndOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChapterEndFlowOrder::RunTest(const FString& Parameters)
{
	using namespace HawkeyeNarrativeTest;
	FMissionFlowRoute Route;
	Route.bHasCloseUp = true;
	Route.bReturnToRoaming = true;
	TestEqual(TEXT("Examine: close-up, end card, back to roaming"), RunRoute(Route),
		FString(TEXT("CloseUp -> EndCard -> ReturnToRoaming -> Done")));

	Route.bHasFlashback = true;
	TestEqual(TEXT("With a flashback it plays between the card and roaming"), RunRoute(Route),
		FString(TEXT("CloseUp -> EndCard -> Flashback -> ReturnToRoaming -> Done")));

	Route.bHasPlayableScene = true;
	TestEqual(TEXT("A flashback with a scene goes to the scene"), RunRoute(Route),
		FString(TEXT("CloseUp -> EndCard -> Flashback -> PlayableScene -> ReturnToRoaming -> Done")));

	Route.bHasNextLevel = true;
	TestEqual(TEXT("A next level wins over roaming"), RunRoute(Route),
		FString(TEXT("CloseUp -> EndCard -> Flashback -> PlayableScene -> OpenNextLevel -> Done")));

	FMissionFlowRoute NoRoam;
	TestEqual(TEXT("Without the roaming flag the campaign end is unchanged"), RunRoute(NoRoam),
		FString(TEXT("EndCard -> FinalCard -> OpenMenuLevel -> Done")));

	// Back from the scene the district's controller picks the route up where it left it.
	UMissionFlowController* Flow = NewObject<UMissionFlowController>();
	Route.bHasNextLevel = false;
	Flow->ResumeAt(Route, EMissionFlowStep::PlayableScene);
	TestEqual(TEXT("After the scene, roaming"), Flow->Advance(), EMissionFlowStep::ReturnToRoaming);
	TestEqual(TEXT("Then done"), Flow->Advance(), EMissionFlowStep::Done);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterEndRoute, "Hawkeye.MissionFlow.RouteFromMission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChapterEndRoute::RunTest(const FString& Parameters)
{
	UMissionDefinition* Mission = NewObject<UMissionDefinition>();
	Mission->bReturnToRoamingAtEnd = true;
	FMissionFlowRoute Route = AHawkeyePlayerController::MakeFlowRoute(Mission, /*bCloseUp=*/true);
	TestTrue(TEXT("The running close-up is in the route"), Route.bHasCloseUp);
	TestFalse(TEXT("No flashback"), Route.bHasFlashback || Route.bHasPlayableScene);
	TestTrue(TEXT("Roaming, from the mission"), Route.bReturnToRoaming);

	UFlashbackDefinition* Flashback = NewObject<UFlashbackDefinition>();
	Flashback->PlayableScene = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_Scene_Placeholder.L_Scene_Placeholder")));
	Mission->FlashbackToPlay = Flashback;
	Route = AHawkeyePlayerController::MakeFlowRoute(Mission, /*bCloseUp=*/false);
	TestTrue(TEXT("A flashback"), Route.bHasFlashback);
	TestTrue(TEXT("That ends in a playable scene"), Route.bHasPlayableScene);
	TestFalse(TEXT("No close-up this time"), Route.bHasCloseUp);
	return true;
}

// --- Playable scenes ----------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSceneReturnPoint, "Hawkeye.Save.SceneReturnPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSceneReturnPoint::RunTest(const FString& Parameters)
{
	auto Candidate = [](const TCHAR* Label, TArray<FName> Tags, float X)
	{
		FHawkeyeReturnCandidate Out;
		Out.Label = FName(Label);
		Out.Tags = MoveTemp(Tags);
		Out.Transform = FTransform(FVector(X, 0.f, 0.f));
		return Out;
	};
	const TArray<FHawkeyeReturnCandidate> Candidates = {
		Candidate(TEXT("City_Bldg_1"), { FName(TEXT("City")), FName(TEXT("City_SceneReturn_FB00")) }, 100.f),
		Candidate(TEXT("City_SceneReturn_FB00"), { FName(TEXT("City")) }, 200.f),
		Candidate(TEXT("TargetPoint_7"), { FName(TEXT("City_SceneReturn_FB01")) }, 300.f),
	};
	TestEqual(TEXT("The labelled actor, even with a tagged one before it"),
		FHawkeyeSceneReturn::SelectReturnPoint(Candidates, TEXT("City_SceneReturn_FB00")), 1);
	TestEqual(TEXT("A tag stands in for a label (packaged builds have none)"),
		FHawkeyeSceneReturn::SelectReturnPoint(Candidates, TEXT("City_SceneReturn_FB01")), 2);
	TestEqual(TEXT("None keeps the saved position"), FHawkeyeSceneReturn::SelectReturnPoint(Candidates, NAME_None), INDEX_NONE);
	TestEqual(TEXT("So does a label nothing carries"),
		FHawkeyeSceneReturn::SelectReturnPoint(Candidates, TEXT("City_Nowhere")), INDEX_NONE);
	TestEqual(TEXT("And an empty world"), FHawkeyeSceneReturn::SelectReturnPoint({}, TEXT("City_SceneReturn_FB00")), INDEX_NONE);

	UFlashbackDefinition* Flashback = NewObject<UFlashbackDefinition>();
	TestFalse(TEXT("A plain slideshow has no scene"), Flashback->HasPlayableScene());
	Flashback->PlayableScene = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_Scene_Placeholder.L_Scene_Placeholder")));
	TestTrue(TEXT("Setting one gives it one"), Flashback->HasPlayableScene());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeNarrativeSaveFields, "Hawkeye.Save.NarrativeFieldsReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeNarrativeSaveFields::RunTest(const FString& Parameters)
{
	UHawkeyeCampaignState* Campaign = NewObject<UHawkeyeCampaignState>();
	Campaign->ReceivedMessages = { TEXT("m1") };
	Campaign->ReadMessages = { TEXT("m1") };
	Campaign->SeenChapterTitles = { TEXT("DA_CH01_Rooftops") };
	Campaign->PlayedDialogueSequences = { TEXT("seq_ch01_open") };
	Campaign->ResetCampaign();
	TestTrue(TEXT("A new game forgets the phone, the titles and the sequences"),
		Campaign->ReceivedMessages.IsEmpty() && Campaign->ReadMessages.IsEmpty() && Campaign->SeenChapterTitles.IsEmpty()
		&& Campaign->PlayedDialogueSequences.IsEmpty());
	for (const TCHAR* Field : { TEXT("ReceivedMessages"), TEXT("ReadMessages"), TEXT("SeenChapterTitles"), TEXT("PlayedDialogueSequences") })
	{
		const FProperty* Property = UHawkeyeCampaignState::StaticClass()->FindPropertyByName(Field);
		TestTrue(FString::Printf(TEXT("%s is a SaveGame property, so SPUD writes it"), Field),
			Property && Property->HasAnyPropertyFlags(CPF_SaveGame));
	}
	return true;
}

// --- Dialogue -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDialogueQueueOrder, "Hawkeye.Dialogue.QueueOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDialogueQueueOrder::RunTest(const FString& Parameters)
{
	UDialogueQueue* Queue = HawkeyeNarrativeTest::MakeQueue();
	AddExpectedMessagePlain(TEXT("no dialogue line 'missing'"), ELogVerbosity::Warning);
	TestFalse(TEXT("An unknown row is refused"), Queue->PlayLine(TEXT("missing")));
	TestTrue(TEXT("A line"), Queue->PlayLine(TEXT("l1")));
	TestTrue(TEXT("Another behind it"), Queue->PlayLine(TEXT("solo")));
	TestEqual(TEXT("The first starts at once"), Queue->Advance(0.f, false), FName(TEXT("l1")));
	TestTrue(TEXT("Busy"), Queue->IsBusy());
	TestEqual(TEXT("The second waits for the first's second"), Queue->Advance(0.9f, false), FName(NAME_None));
	TestEqual(TEXT("And follows it"), Queue->Advance(0.1f, false), FName(TEXT("solo")));
	TestEqual(TEXT("A line with no duration gets the default 4 s"), Queue->GetCurrentLineSecondsLeft(), 4.f, 0.001f);
	Queue->Advance(4.f, false);
	TestFalse(TEXT("Quiet again"), Queue->IsBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDialogueSequenceGaps, "Hawkeye.Dialogue.SequenceGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDialogueSequenceGaps::RunTest(const FString& Parameters)
{
	UDialogueQueue* Queue = HawkeyeNarrativeTest::MakeQueue();
	TestTrue(TEXT("The sequence queues"), Queue->PlaySequence(TEXT("seq")));
	TestFalse(TEXT("Not twice"), Queue->PlaySequence(TEXT("seq")));
	TestEqual(TEXT("Its first gap is 2 s"), Queue->Advance(1.9f, false), FName(NAME_None));
	TestEqual(TEXT("Then Order 1's line"), Queue->Advance(0.1f, false), FName(TEXT("l1")));
	TestEqual(TEXT("It speaks for its 1 s"), Queue->Advance(1.f, false), FName(NAME_None));
	TestEqual(TEXT("Then 1 s of quiet"), Queue->Advance(0.5f, false), FName(NAME_None));
	TestEqual(TEXT("Then Order 2's line"), Queue->Advance(0.5f, false), FName(TEXT("l2")));
	TestFalse(TEXT("Not played until the last line starts"), Queue->HasPlayedSequence(TEXT("seq")));
	TestEqual(TEXT("A gap of 1 s after a 1 s line: Order 3 at 2 s"), Queue->Advance(2.f, false), FName(TEXT("l3")));
	TestTrue(TEXT("Played once the last line starts"), Queue->HasPlayedSequence(TEXT("seq")));
	Queue->Advance(1.f, false);
	TestFalse(TEXT("A played sequence does not play again"), Queue->PlaySequence(TEXT("seq")));
	TestTrue(TEXT("Unless asked to"), Queue->PlaySequence(TEXT("seq"), /*bEvenIfPlayed=*/true));
	TestEqual(TEXT("The save keeps it"), Queue->ExportPlayed().Num(), 1);
	AddExpectedMessagePlain(TEXT("no dialogue sequence 'nope'"), ELogVerbosity::Warning);
	TestFalse(TEXT("An unknown sequence is refused"), Queue->PlaySequence(TEXT("nope")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDialogueCombatPauses, "Hawkeye.Dialogue.CombatPausesSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDialogueCombatPauses::RunTest(const FString& Parameters)
{
	UDialogueQueue* Queue = HawkeyeNarrativeTest::MakeQueue();
	Queue->PlaySequence(TEXT("seq"));
	Queue->Advance(1.5f, false);
	TestEqual(TEXT("Half a second of the first gap left"), Queue->GetNextGapSecondsLeft(), 0.5f, 0.001f);

	TestEqual(TEXT("A fight holds the next line"), Queue->Advance(10.f, true), FName(NAME_None));
	TestTrue(TEXT("Held for combat"), Queue->IsHeldForCombat());
	TestEqual(TEXT("The gap does not run in the fight"), Queue->GetNextGapSecondsLeft(), 0.5f, 0.001f);

	TestEqual(TEXT("After the fight it carries on where it was"), Queue->Advance(0.5f, false), FName(TEXT("l1")));
	TestFalse(TEXT("No longer held"), Queue->IsHeldForCombat());
	TestEqual(TEXT("A fight starting mid-line lets the line finish"), Queue->Advance(0.5f, true), FName(NAME_None));
	TestTrue(TEXT("Still speaking"), Queue->IsSpeaking());
	Queue->Advance(0.5f, true);
	TestFalse(TEXT("The line ended"), Queue->IsSpeaking());
	TestTrue(TEXT("The next waits out the fight"), Queue->IsHeldForCombat() && Queue->GetPendingCount() == 2);
	TestEqual(TEXT("And comes after it"), Queue->Advance(1.f, false), FName(TEXT("l2")));
	return true;
}

#endif
