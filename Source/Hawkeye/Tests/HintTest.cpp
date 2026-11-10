#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "HawkeyePlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Settings/HawkeyeSettingsSave.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "SpudState.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHintSubsystem.h"
#include "UI/HawkeyeHints.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The first-time hints (claude-docs/gameplay-semantics.md, HUD, "First-time hints"): key tokens for keys and a
 * pad, the once-a-campaign count through a SPUD round trip, the queue's priority and single line, suppression
 * under chapter beats and menus, and the Settings toggle. The line on screen is in the Kate pass
 * (hint_sprint.png, hint_grapple.png, hint_parry.png; HintScreenshotTest.cpp).
 */
namespace HawkeyeHintTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using T = EHawkeyeHintTrigger;
	using A = EHawkeyeHintAction;

	static FHawkeyeHintRule Rule(const TCHAR* Id, T Trigger, A Dismiss, int32 Priority, bool bUrgent = false, float Hold = 0.f)
	{
		FHawkeyeHintRule Out;
		Out.Id = Id;
		Out.Text = FText::FromString(Id);
		Out.Trigger = Trigger;
		Out.Dismiss = Dismiss;
		Out.Priority = Priority;
		Out.bUrgent = bUrgent;
		Out.HoldSeconds = Hold;
		return Out;
	}

	/** A queue and the counts a save would hold, stepped a frame at a time. */
	struct FRig
	{
		TArray<FHawkeyeHintRule> Rules;
		FHawkeyeHintQueue Queue;
		TMap<FName, int32> Counts;
		TArray<FName> Started;

		explicit FRig(const TArray<FHawkeyeHintRule>& InRules) : Rules(InRules) { Queue.Reset(Rules); }

		FHawkeyeHintStep Step(float Seconds, uint64 Triggers, uint64 Actions = 0, bool bSuppressed = false, bool bEnabled = true)
		{
			FHawkeyeHintStep Last;
			// 0.05 s frames, like the game at 20 fps: long enough to be quick, short enough to see the order.
			const int32 Frames = FMath::Max(1, FMath::RoundToInt(Seconds / 0.05f));
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Last = Queue.Tick(Seconds / Frames, Triggers, Frame == 0 ? Actions : 0, bSuppressed, bEnabled,
					[this](FName Id) { const int32* Count = Counts.Find(Id); return Count ? *Count : 0; });
				if (!Last.Started.IsNone())
				{
					Counts.FindOrAdd(Last.Started) += 1;
					Started.Add(Last.Started);
				}
				for (const FName& Learned : Last.Learned)
				{
					Counts.Add(Learned, 1);
				}
			}
			return Last;
		}

		FName Shown() const { return Queue.IsVisible() && Queue.GetCurrent() ? Queue.GetCurrent()->Id : NAME_None; }
	};

	static uint64 Bits(std::initializer_list<T> Triggers)
	{
		uint64 Out = 0;
		for (const T Trigger : Triggers)
		{
			Out |= FHawkeyeHintQueue::Bit(Trigger);
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHintTokens, "Hawkeye.Hints.TokensResolveForBothDevices", HawkeyeHintTest::Flags)

bool FHawkeyeHintTokens::RunTest(const FString& Parameters)
{
	const TArray<FHawkeyeHintRule>& Rules = UHawkeyeHintRules::GetDefaultRules();
	auto Line = [&Rules](const TCHAR* Id, bool bGamepad, EHawkeyeSprintMode Mode = EHawkeyeSprintMode::Default)
	{
		const FHawkeyeHintRule* Found = UHawkeyeHintRules::FindRule(Rules, Id);
		FHawkeyeHintContext Context;
		Context.bGamepad = bGamepad;
		Context.SprintMode = Mode;
		return Found ? UHawkeyeHintRules::ResolveToString(*Found, Context) : FString(TEXT("<no rule>"));
	};

	// The sprint line follows the Sprint setting: hold on keys, a press where it toggles.
	TestEqual(TEXT("Sprint on keys"), Line(TEXT("sprint"), false), FString(TEXT("Hold [Shift] to sprint")));
	TestEqual(TEXT("Sprint on a pad toggles by default"), Line(TEXT("sprint"), true), FString(TEXT("Press [L3] to sprint")));
	TestEqual(TEXT("Toggle on keys"), Line(TEXT("sprint"), false, EHawkeyeSprintMode::Toggle), FString(TEXT("Press [Shift] to sprint")));
	TestEqual(TEXT("Hold on a pad"), Line(TEXT("sprint"), true, EHawkeyeSprintMode::Hold), FString(TEXT("Hold [L3] to sprint")));

	TestEqual(TEXT("Grapple on keys"), Line(TEXT("grapple"), false), FString(TEXT("[Q] to zip")));
	TestEqual(TEXT("Grapple on a pad"), Line(TEXT("grapple"), true), FString(TEXT("[RB] to zip")));
	TestEqual(TEXT("Parry on keys"), Line(TEXT("parry"), false), FString(TEXT("[V] as the ! flashes to parry")));
	TestEqual(TEXT("Parry on a pad"), Line(TEXT("parry"), true), FString(TEXT("[X] as the ! flashes to parry")));
	TestEqual(TEXT("Draw on keys"), Line(TEXT("draw"), false), FString(TEXT("[RMB] then [LMB], release to fire")));
	TestEqual(TEXT("Draw on a pad"), Line(TEXT("draw"), true), FString(TEXT("[LT] then [RT], release to fire")));
	TestEqual(TEXT("Vault on a pad"), Line(TEXT("vault"), true), FString(TEXT("[A] or sprint into it to vault")));
	TestEqual(TEXT("The map is a hold on a pad"), Line(TEXT("map"), true), FString(TEXT("[Hold D-pad Up] for the map")));
	TestEqual(TEXT("The phone on keys"), Line(TEXT("phone"), false), FString(TEXT("[P] to read the text")));
	TestEqual(TEXT("The finisher and the takedown are F and Y"), Line(TEXT("finisher"), false) + Line(TEXT("takedown"), true),
		FString(TEXT("[F] to finish him[Y] from behind to take him down")));
	TestEqual(TEXT("The safehouse door is E"), Line(TEXT("safehouse"), false), FString(TEXT("[E] at the door to use the safehouse")));
	TestEqual(TEXT("Switch names the partner"), Line(TEXT("switch"), true), FString(TEXT("[LB] to play as Clint")));

	// Keys come back as their own segments, for the keycaps.
	FHawkeyeHintContext Pad;
	Pad.bGamepad = true;
	const TArray<FHawkeyeHintSegment> Segments = UHawkeyeHintRules::ResolveLine(TEXT("{Aim} then {Draw}, release"), Pad);
	TestEqual(TEXT("Four segments"), Segments.Num(), 4);
	if (Segments.Num() == 4)
	{
		TestTrue(TEXT("key, text, key, text"), Segments[0].bKey && !Segments[1].bKey && Segments[2].bKey && !Segments[3].bKey);
		TestEqual(TEXT("LT first"), Segments[0].Text, FString(TEXT("LT")));
	}
	TestEqual(TEXT("An unknown token is left as written"),
		UHawkeyeHintRules::ResolveLine(TEXT("{Nope} here"), Pad).Num() > 0 ? UHawkeyeHintRules::ResolveLine(TEXT("{Nope} here"), Pad)[0].Text : FString(),
		FString(TEXT("{Nope} here")));

	// Every token the rules may use has a name on both devices, and no shipped line is left with one unresolved.
	for (const TCHAR* Token : { TEXT("Sprint"), TEXT("Jump"), TEXT("Grapple"), TEXT("Draw"), TEXT("Aim"), TEXT("Strike"), TEXT("Dodge"),
		TEXT("Crouch"), TEXT("Interact"), TEXT("Takedown"), TEXT("Phone"), TEXT("Map"), TEXT("Switch"), TEXT("Wheel") })
	{
		TestFalse(FString::Printf(TEXT("{%s} on keys"), Token), UHawkeyeHintRules::GetKeyName(Token, false).IsEmpty());
		TestFalse(FString::Printf(TEXT("{%s} on a pad"), Token), UHawkeyeHintRules::GetKeyName(Token, true).IsEmpty());
	}
	TSet<FName> Ids;
	for (const FHawkeyeHintRule& Each : Rules)
	{
		for (const bool bGamepad : { false, true })
		{
			FHawkeyeHintContext Context;
			Context.bGamepad = bGamepad;
			const FString Resolved = UHawkeyeHintRules::ResolveToString(Each, Context);
			TestFalse(FString::Printf(TEXT("%s resolves every token (%s)"), *Each.Id.ToString(), *Resolved), Resolved.Contains(TEXT("{")));
			TestTrue(FString::Printf(TEXT("%s is one short line"), *Each.Id.ToString()), Resolved.Len() > 0 && Resolved.Len() <= 60);
		}
		TestFalse(FString::Printf(TEXT("%s is a unique id"), *Each.Id.ToString()), Ids.Contains(Each.Id));
		Ids.Add(Each.Id);
		TestEqual(FString::Printf(TEXT("%s shows once a campaign"), *Each.Id.ToString()), Each.MaxShows, 1);
		TestNotEqual(FString::Printf(TEXT("%s has a trigger"), *Each.Id.ToString()), Each.Trigger, EHawkeyeHintTrigger::None);
	}
	TestEqual(TEXT("The shipped set"), Rules.Num(), 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHintOncePerCampaign, "Hawkeye.Hints.OncePerCampaignThroughSave", HawkeyeHintTest::Flags)

bool FHawkeyeHintOncePerCampaign::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHintTest;
	UHawkeyeSaveSubsystem* Save = NewObject<UHawkeyeSaveSubsystem>(NewObject<UGameInstance>(GEngine));
	UHawkeyeCampaignState* Campaign = Save->GetCampaignState();
	if (!TestNotNull(TEXT("A campaign"), Campaign))
	{
		return false;
	}
	TestEqual(TEXT("Nothing shown yet"), Save->GetHintShowCount(TEXT("sprint")), 0);

	// The shipped sprint rule, shown once in a queue that reads and writes the save's counts.
	const TArray<FHawkeyeHintRule>& Rules = UHawkeyeHintRules::GetDefaultRules();
	FHawkeyeHintQueue Queue;
	Queue.Reset(Rules);
	auto Tick = [&Queue, Save, &Rules](float Seconds, uint64 Triggers)
	{
		for (float Done = 0.f; Done < Seconds; Done += 0.05f)
		{
			const FHawkeyeHintStep Step = Queue.Tick(0.05f, Triggers, 0, false, true, [Save](FName Id) { return Save->GetHintShowCount(Id); });
			if (!Step.Started.IsNone())
			{
				Save->SetHintShowCount(Step.Started, Save->GetHintShowCount(Step.Started) + 1);
			}
		}
	};
	const uint64 Walking = Bits({ T::WalkingOpenGround });
	Tick(2.5f, Walking);
	TestFalse(TEXT("Not before 3 s of walking"), Queue.IsVisible());
	Tick(0.6f, Walking);
	TestTrue(TEXT("The sprint hint after 3 s"), Queue.IsVisible() && Queue.GetCurrent() && Queue.GetCurrent()->Id == TEXT("sprint"));
	TestEqual(TEXT("Counted when it came up"), Save->GetHintShowCount(TEXT("sprint")), 1);
	Save->SetHintShowCount(TEXT("vault"), 1);

	// Through SPUD the way the campaign is saved: the global object's SaveGame fields, written and read back.
	USpudState* Written = NewObject<USpudState>();
	Written->StoreGlobalObject(Campaign, TEXT("HawkeyeCampaign"));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Written->SaveToArchive(Writer);
	TestTrue(TEXT("Wrote a save"), Bytes.Num() > 0);
	USpudState* Read = NewObject<USpudState>();
	FMemoryReader Reader(Bytes);
	Read->LoadFromArchive(Reader, true);
	UHawkeyeSaveSubsystem* Loaded = NewObject<UHawkeyeSaveSubsystem>(NewObject<UGameInstance>(GEngine));
	Read->RestoreGlobalObject(Loaded->GetCampaignState(), TEXT("HawkeyeCampaign"));
	TestEqual(TEXT("The sprint hint's show came back"), Loaded->GetHintShowCount(TEXT("sprint")), 1);
	TestEqual(TEXT("And the vault's"), Loaded->GetHintShowCount(TEXT("vault")), 1);
	TestEqual(TEXT("A hint never shown is still 0"), Loaded->GetHintShowCount(TEXT("grapple")), 0);

	// After the load, walking again does not bring it back; a hint with its show left still comes up.
	FHawkeyeHintQueue Again;
	Again.Reset(Rules);
	bool bSprintAgain = false;
	bool bGrapple = false;
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		const FHawkeyeHintStep Step = Again.Tick(0.05f, Walking | Bits({ T::GrappleReady }), 0, false, true,
			[Loaded](FName Id) { return Loaded->GetHintShowCount(Id); });
		bSprintAgain |= Step.Started == TEXT("sprint");
		bGrapple |= Step.Started == TEXT("grapple");
	}
	TestFalse(TEXT("Once a campaign: no second sprint hint after the load"), bSprintAgain);
	TestTrue(TEXT("The grapple hint still has its show"), bGrapple);

	// A new campaign starts every hint again.
	Loaded->GetCampaignState()->ResetCampaign();
	TestEqual(TEXT("New game: the count is gone"), Loaded->GetHintShowCount(TEXT("sprint")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHintQueueOrder, "Hawkeye.Hints.QueuePriorityAndSingleLine", HawkeyeHintTest::Flags)

bool FHawkeyeHintQueueOrder::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHintTest;
	FRig Rig({
		Rule(TEXT("low"), T::WalkingOpenGround, A::Sprinted, 10),
		Rule(TEXT("mid"), T::GrappleReady, A::Zipped, 50),
		Rule(TEXT("urgent"), T::ParryTelegraph, A::Parried, 100, true),
		Rule(TEXT("held"), T::NearVaultable, A::Vaulted, 40, false, 1.f),
	});

	Rig.Step(0.1f, Bits({ T::WalkingOpenGround }));
	TestEqual(TEXT("The one armed hint shows"), Rig.Shown(), FName(TEXT("low")));
	Rig.Step(0.5f, Bits({ T::WalkingOpenGround, T::GrappleReady }));
	TestEqual(TEXT("A higher one waits while the shown one has had under 1.5 s"), Rig.Shown(), FName(TEXT("low")));
	TestEqual(TEXT("It waits in the queue"), Rig.Queue.GetWaiting().Num() > 0 ? Rig.Queue.GetWaiting()[0] : NAME_None, FName(TEXT("mid")));
	Rig.Step(1.2f, Bits({ T::WalkingOpenGround, T::GrappleReady }));
	TestEqual(TEXT("then takes the line"), Rig.Shown(), FName(TEXT("mid")));
	TestEqual(TEXT("Each came up once, one after the other"), Rig.Started.Num(), 2);
	Rig.Step(0.05f, Bits({ T::WalkingOpenGround, T::GrappleReady, T::ParryTelegraph }));
	TestEqual(TEXT("An urgent hint takes the line at once"), Rig.Shown(), FName(TEXT("urgent")));
	TestEqual(TEXT("One line: the others are not on it"), Rig.Queue.GetCurrent() ? Rig.Queue.GetCurrent()->Id : NAME_None, FName(TEXT("urgent")));

	// The shown hint stays 6 s after its trigger clears, then goes; the lower ones it pushed off have had their show.
	Rig.Step(5.5f, 0);
	TestEqual(TEXT("Still up 5.5 s after the telegraph"), Rig.Shown(), FName(TEXT("urgent")));
	Rig.Step(0.7f, 0);
	TestEqual(TEXT("Gone 6 s after it cleared"), Rig.Shown(), NAME_None);
	TestEqual(TEXT("No hint shows twice"), Rig.Started.Num(), 3);

	// Hold: the trigger has to last HoldSeconds.
	Rig.Step(0.5f, Bits({ T::NearVaultable }));
	TestEqual(TEXT("Not before its hold"), Rig.Shown(), NAME_None);
	Rig.Step(0.6f, Bits({ T::NearVaultable }));
	TestEqual(TEXT("After it"), Rig.Shown(), FName(TEXT("held")));
	// Doing the thing takes it down at once and counts it learned.
	const FHawkeyeHintStep Step = Rig.Step(0.05f, Bits({ T::NearVaultable }), FHawkeyeHintQueue::Bit(A::Vaulted));
	TestEqual(TEXT("The vault takes the hint down"), Rig.Shown(), NAME_None);
	TestTrue(TEXT("and it is learned"), Step.Learned.Contains(TEXT("held")));

	// A lesson learned before its hint ever showed: it never shows.
	FRig Knows({ Rule(TEXT("sprint"), T::WalkingOpenGround, A::Sprinted, 10) });
	Knows.Step(0.05f, 0, FHawkeyeHintQueue::Bit(A::Sprinted));
	Knows.Step(3.f, Bits({ T::WalkingOpenGround }));
	TestEqual(TEXT("She sprinted first: no sprint hint"), Knows.Shown(), NAME_None);
	TestEqual(TEXT("Never started"), Knows.Started.Num(), 0);

	// A waiting hint whose trigger cleared long enough ago drops out of the queue.
	FRig Stale({ Rule(TEXT("a"), T::WalkingOpenGround, A::Sprinted, 10), Rule(TEXT("b"), T::GrappleReady, A::Zipped, 5) });
	Stale.Step(0.05f, Bits({ T::WalkingOpenGround, T::GrappleReady }));
	Stale.Step(7.f, Bits({ T::WalkingOpenGround }));
	TestEqual(TEXT("The lower one's moment passed while the other was up"), Stale.Queue.GetWaiting().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHintSuppressionTest, "Hawkeye.Hints.SuppressedDuringBeatsAndMenus", HawkeyeHintTest::Flags)

bool FHawkeyeHintSuppressionTest::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHintTest;
	FHawkeyeHintSuppression None;
	TestFalse(TEXT("Nothing suppresses by default"), None.Any());
	for (int32 Which = 0; Which < 4; ++Which)
	{
		FHawkeyeHintSuppression One;
		One.bChapterBeat = Which == 0;
		One.bMenu = Which == 1;
		One.bDowned = Which == 2;
		One.bChallengeResults = Which == 3;
		TestTrue(FString::Printf(TEXT("Suppression %d hides hints"), Which), One.Any());
	}

	FRig Rig({ Rule(TEXT("grapple"), T::GrappleReady, A::Zipped, 50), Rule(TEXT("fast"), T::BothSafehousesFound, A::FastTravelled, 20) });
	Rig.Step(0.5f, Bits({ T::GrappleReady }));
	TestEqual(TEXT("Up"), Rig.Shown(), FName(TEXT("grapple")));
	Rig.Step(10.f, 0, 0, true);
	TestEqual(TEXT("A menu hides it"), Rig.Shown(), NAME_None);
	TestFalse(TEXT("and nothing is visible"), Rig.Queue.IsVisible());
	Rig.Step(0.05f, 0);
	TestEqual(TEXT("It comes back after: its 6 s did not run under the menu"), Rig.Shown(), FName(TEXT("grapple")));
	Rig.Step(0.05f, 0, FHawkeyeHintQueue::Bit(A::Zipped));

	// A one-frame trigger under a menu (the second safehouse found in its own menu) waits for the menu to close.
	Rig.Step(0.05f, Bits({ T::BothSafehousesFound }), 0, true);
	Rig.Step(3.f, 0, 0, true);
	TestEqual(TEXT("Not during the menu"), Rig.Shown(), NAME_None);
	Rig.Step(0.05f, 0);
	TestEqual(TEXT("Straight after it"), Rig.Shown(), FName(TEXT("fast")));

	// The controller's screens: the map is a menu, closing it is not.
	const FHawkeyeTestWorld TestWorld;
	AHawkeyePauseTestController* PC = Cast<AHawkeyePauseTestController>(
		TestWorld.SpawnActor(AHawkeyePauseTestController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("A player controller"), PC))
	{
		return false;
	}
	TestFalse(TEXT("Roaming: nothing held back"), UHawkeyeHintSubsystem::GatherSuppression(PC).Any());
	PC->SetMapOpen(true);
	TestTrue(TEXT("The map holds hints back"), UHawkeyeHintSubsystem::GatherSuppression(PC).bMenu);
	PC->SetMapOpen(false);
	PC->TestInputPause();
	TestTrue(TEXT("So does the pause menu"), UHawkeyeHintSubsystem::GatherSuppression(PC).bMenu);
	PC->TestInputPause();
	TestFalse(TEXT("And neither once closed"), UHawkeyeHintSubsystem::GatherSuppression(PC).Any());
	TestFalse(TEXT("No controller: nothing to hold back"), UHawkeyeHintSubsystem::GatherSuppression(nullptr).Any());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHintToggle, "Hawkeye.Hints.SettingsToggle", HawkeyeHintTest::Flags)

bool FHawkeyeHintToggle::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHintTest;
	// Off: nothing shows and nothing waits, so turning them on later starts clean.
	FRig Rig({ Rule(TEXT("grapple"), T::GrappleReady, A::Zipped, 50) });
	Rig.Step(1.f, Bits({ T::GrappleReady }), 0, false, false);
	TestEqual(TEXT("Hints off: no line"), Rig.Shown(), NAME_None);
	TestEqual(TEXT("and none counted"), Rig.Started.Num(), 0);
	Rig.Step(0.05f, Bits({ T::GrappleReady }));
	TestEqual(TEXT("On again: it shows"), Rig.Shown(), FName(TEXT("grapple")));
	Rig.Step(0.05f, Bits({ T::GrappleReady }), 0, false, false);
	TestEqual(TEXT("Off takes the shown one down"), Rig.Shown(), NAME_None);

	// The setting: on by default, saved, version 10, and a version 9 save migrates to on keeping the rest.
	static const TCHAR* Slot = TEXT("HawkeyeHintsAutomationTest");
	auto ClearSlot = []()
	{
		if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(Slot, 0);
		}
	};
	auto MakeSettings = []()
	{
		UHawkeyeSettingsSubsystem* Settings = NewObject<UHawkeyeSettingsSubsystem>(NewObject<UGameInstance>(GEngine));
		Settings->SlotNameOverride = Slot;
		return Settings;
	};
	TestEqual(TEXT("Settings version 10 (10 added Hints)"), FHawkeyeSettings::CurrentVersion, 10);
	ClearSlot();
	UHawkeyeSettingsSubsystem* Writer = MakeSettings();
	Writer->Load();
	TestTrue(TEXT("Hints are on by default"), Writer->GetSettings().bShowHints);
	Writer->SetShowHints(false);
	UHawkeyeSettingsSubsystem* Reader = MakeSettings();
	Reader->Load();
	TestFalse(TEXT("Off survives the save"), Reader->GetSettings().bShowHints);

	ClearSlot();
	UHawkeyeSettingsSave* Old = Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!TestNotNull(TEXT("A save object"), Old))
	{
		return false;
	}
	Old->Settings.LookSensitivity = 0.33f;
	Old->Settings.MusicVolume = 0.25f;
	Old->Settings.Version = 9;
	UGameplayStatics::SaveGameToSlot(Old, Slot, 0);
	UHawkeyeSettingsSubsystem* Migrated = MakeSettings();
	Migrated->Load();
	TestEqual(TEXT("Version 9 keeps the sensitivity"), Migrated->GetLookSensitivity(), 0.33f);
	TestEqual(TEXT("And the music"), Migrated->GetMusicVolume(), 0.25f);
	TestTrue(TEXT("And comes up with hints on"), Migrated->GetStoredSettings().bShowHints);
	TestEqual(TEXT("Stamped version 10"), Migrated->GetStoredSettings().Version, 10);
	ClearSlot();

	// The run gate: automation never sees a hint unless it asks (hawkeye.Hints 2).
	IConsoleVariable* Hints = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.Hints"));
	if (TestNotNull(TEXT("hawkeye.Hints"), Hints))
	{
		const int32 Was = Hints->GetInt();
		Hints->Set(1, ECVF_SetByCode);
		TestFalse(TEXT("Under automation hints are off by default"), UHawkeyeHintSubsystem::IsRunAllowed());
		Hints->Set(2, ECVF_SetByCode);
		TestTrue(TEXT("hawkeye.Hints 2 turns them on"), UHawkeyeHintSubsystem::IsRunAllowed());
		Hints->Set(0, ECVF_SetByCode);
		TestFalse(TEXT("hawkeye.Hints 0 turns them off"), UHawkeyeHintSubsystem::IsRunAllowed());
		Hints->Set(Was, ECVF_SetByCode);
	}
	return true;
}

#endif
