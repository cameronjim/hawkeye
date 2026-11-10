#include "Combat/HealthComponent.h"
#include "Crime/Civilian.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeLoot.h"
#include "Crime/CrimeRules.h"
#include "Crime/CrimeSpot.h"
#include "Crime/CrimeSubsystem.h"
#include "Crime/CrimeTracker.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionSubsystem.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/CrimePanelWidget.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The street crime rules listed on UCrimeRules: when one is due, where it may start, how the mugging's
 * victim and the robbery's loot fail it, what stops it, the cleanup, one at a time, the pause, and the
 * counts in the save. The scripted rescue on the district is Hawkeye.Lap.CrimeMugging (CrimeLapTest.cpp),
 * run in -game.
 */
namespace HawkeyeCrimeTest
{
	static UCrimeDefinition* Make(ECrimeType Type, int32 Thugs)
	{
		UCrimeDefinition* Definition = NewObject<UCrimeDefinition>();
		Definition->Type = Type;
		Definition->Id = UCrimeRules::TypeKey(Type);
		Definition->Name = FText::Format(NSLOCTEXT("HawkeyeTest", "CrimeName", "[Crime: {0}]"), UCrimeRules::TypeText(Type));
		FCrimeRosterEntry Entry;
		Entry.Weapon = EThugWeapon::Fists;
		Entry.Count = Thugs;
		Definition->Roster.Add(Entry);
		Definition->TimeToFailSeconds = 45.f;
		Definition->VictimHitsToFail = 3;
		Definition->RewardArrows = 5;
		return Definition;
	}

	static FCrimeTickState State(int32 Down, int32 OnVictim = 0, float Distance = 1000.f)
	{
		FCrimeTickState Out;
		Out.ThugsDown = Down;
		Out.ThugsOnVictim = OnVictim;
		Out.PlayerDistance = Distance;
		return Out;
	}

	/** Sets hawkeye.CrimeInterval for a scope and puts it back. */
	struct FIntervalScope
	{
		IConsoleVariable* Var = nullptr;
		float Old = 0.f;
		explicit FIntervalScope(float Seconds)
		{
			Var = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.CrimeInterval"));
			Old = Var ? Var->GetFloat() : 0.f;
			if (Var)
			{
				Var->Set(Seconds, ECVF_SetByCode);
			}
		}
		~FIntervalScope()
		{
			if (Var)
			{
				Var->Set(Old, ECVF_SetByCode);
			}
		}
	};

	/** A world with the crime subsystem, a pawn with a quiver, and spots. */
	struct FCrimeWorld
	{
		FHawkeyeTestWorld TestWorld;
		UCrimeSubsystem* Crimes = nullptr;
		UMissionSubsystem* Missions = nullptr;
		APawn* Player = nullptr;
		UInventoryComponent* Inventory = nullptr;

		FCrimeWorld()
		{
			UWorld* World = TestWorld.Get();
			Crimes = World ? World->GetSubsystem<UCrimeSubsystem>() : nullptr;
			Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
			Player = Cast<APawn>(TestWorld.SpawnActor(ADefaultPawn::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
			if (Player)
			{
				Inventory = NewObject<UInventoryComponent>(Player, TEXT("Quiver"));
				Inventory->RegisterComponent();
				Inventory->AddArrows(Inventory->GetStandardArrowDefinition(), 10);
			}
			if (Crimes)
			{
				Crimes->PlayerOverride = Player;
				Crimes->ChapterBeatOverride = false;
				Crimes->bShowToast = false;
			}
		}

		bool IsValid() const { return Crimes && Missions && Player && Inventory; }

		ACrimeSpot* Spot(const FVector& At, UCrimeDefinition* Crime) const
		{
			ACrimeSpot* Spot = Cast<ACrimeSpot>(TestWorld.SpawnActor(ACrimeSpot::StaticClass(), At, FRotator::ZeroRotator));
			if (Spot && Crime)
			{
				Spot->Crimes.Add(Crime);
			}
			return Spot;
		}

		void KillAll() const
		{
			for (const TWeakObjectPtr<AThugCharacter>& Thug : Crimes->GetThugs())
			{
				if (Thug.IsValid())
				{
					Thug->GetHealthComponent()->ApplyDamage(10000.f, Player);
				}
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeSpotSelection, "Hawkeye.Crime.SpotSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeSpotSelection::RunTest(const FString& Parameters)
{
	const FVector Player(0.f, 0.f, 0.f);
	TestTrue(TEXT("25 m counts"), UCrimeRules::IsSpotEligible(FVector(2500.f, 0.f, 0.f), Player, false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f));
	TestTrue(TEXT("40 m counts"), UCrimeRules::IsSpotEligible(FVector(0.f, 4000.f, 0.f), Player, false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f));
	TestFalse(TEXT("20 m is too close"), UCrimeRules::IsSpotEligible(FVector(2000.f, 0.f, 0.f), Player, false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f));
	TestFalse(TEXT("45 m is too far"), UCrimeRules::IsSpotEligible(FVector(4500.f, 0.f, 0.f), Player, false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f));
	TestTrue(TEXT("Height does not count: a roof 15 m up 30 m away"),
		UCrimeRules::IsSpotEligible(FVector(3000.f, 0.f, 1500.f), Player, false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f));
	TestFalse(TEXT("Within 60 m of the last crime"),
		UCrimeRules::IsSpotEligible(FVector(3000.f, 0.f, 0.f), Player, true, FVector(-2000.f, 0.f, 0.f), 2500.f, 4000.f, 6000.f));
	TestTrue(TEXT("60 m from the last crime is fine"),
		UCrimeRules::IsSpotEligible(FVector(3000.f, 0.f, 0.f), Player, true, FVector(-3000.f, 0.f, 0.f), 2500.f, 4000.f, 6000.f));

	FRandomStream Stream(7);
	TArray<FCrimeSpotCandidate> Spots;
	auto Add = [&Spots](const FVector& At, bool bInSight)
	{
		FCrimeSpotCandidate Candidate;
		Candidate.Location = At;
		Candidate.bInSight = bInSight;
		Spots.Add(Candidate);
	};
	Add(FVector(1000.f, 0.f, 0.f), false);   // too close
	Add(FVector(3000.f, 0.f, 0.f), true);    // in band, seen
	Add(FVector(0.f, 3500.f, 0.f), false);   // in band, hidden
	Add(FVector(9000.f, 0.f, 0.f), false);   // too far
	for (int32 Try = 0; Try < 20; ++Try)
	{
		TestEqual(TEXT("The hidden spot in the band wins every time"),
			UCrimeRules::PickSpot(Spots, Player, false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f, Stream), 2);
	}
	TestEqual(TEXT("With the hidden one next to the last crime, the seen one is taken"),
		UCrimeRules::PickSpot(Spots, Player, true, FVector(-1000.f, 6000.f, 0.f), 2500.f, 4000.f, 6000.f, Stream), 1);
	TestEqual(TEXT("Nothing in the band: no spot"),
		UCrimeRules::PickSpot(Spots, FVector(50000.f, 0.f, 0.f), false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f, Stream), INDEX_NONE);
	TestEqual(TEXT("No spots at all: none"),
		UCrimeRules::PickSpot({}, Player, false, FVector::ZeroVector, 2500.f, 4000.f, 6000.f, Stream), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeScheduleTimer, "Hawkeye.Crime.ScheduleTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeScheduleTimer::RunTest(const FString& Parameters)
{
	FRandomStream Stream(11);
	for (int32 Try = 0; Try < 200; ++Try)
	{
		const float Interval = UCrimeRules::RollInterval(90.f, 150.f, 0.f, Stream);
		if (Interval < 90.f || Interval > 150.f)
		{
			AddError(FString::Printf(TEXT("Interval %.1f s is outside 90 to 150 s."), Interval));
			break;
		}
	}
	TestEqual(TEXT("The cvar's value wins"), UCrimeRules::RollInterval(90.f, 150.f, 3.f, Stream), 3.f);

	TestTrue(TEXT("Roaming"), UCrimeRules::IsRoaming(false, false, false, false, false));
	TestFalse(TEXT("Not with a crime on"), UCrimeRules::IsRoaming(true, false, false, false, false));
	TestFalse(TEXT("Not in a fight"), UCrimeRules::IsRoaming(false, true, false, false, false));
	TestFalse(TEXT("Not in a challenge"), UCrimeRules::IsRoaming(false, false, true, false, false));
	TestFalse(TEXT("Not in a chapter beat"), UCrimeRules::IsRoaming(false, false, false, true, false));
	TestFalse(TEXT("Not while she is down"), UCrimeRules::IsRoaming(false, false, false, false, true));

	FCrimeScheduleClock Clock;
	Clock.Restart(100.f);
	TestFalse(TEXT("Not due at 60 s"), Clock.Advance(60.f, true));
	TestFalse(TEXT("A fight's 60 s do not count"), Clock.Advance(60.f, false));
	TestEqual(TEXT("Still 60 s"), Clock.Elapsed, 60.f);
	TestTrue(TEXT("Due at 100 s of roaming"), Clock.Advance(40.f, true));
	TestTrue(TEXT("And stays due until it restarts"), Clock.Advance(0.f, false));
	Clock.Restart(120.f);
	TestFalse(TEXT("A restart starts over"), Clock.IsDue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeFailConditions, "Hawkeye.Crime.FailConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeFailConditions::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No hit before 15 s"), UCrimeRules::VictimHitsAfter(14.9f, 45.f, 3), 0);
	TestEqual(TEXT("First at 15 s"), UCrimeRules::VictimHitsAfter(15.f, 45.f, 3), 1);
	TestEqual(TEXT("Third at 45 s"), UCrimeRules::VictimHitsAfter(45.f, 45.f, 3), 3);
	TestEqual(TEXT("Never more than three"), UCrimeRules::VictimHitsAfter(500.f, 45.f, 3), 3);

	UCrimeTracker* Tracker = NewObject<UCrimeTracker>();
	UCrimeDefinition* Mugging = HawkeyeCrimeTest::Make(ECrimeType::Mugging, 2);
	TestTrue(TEXT("A mugging starts"), Tracker->StartCrime(Mugging, FVector::ZeroVector, 2));
	TestFalse(TEXT("A second is refused while it is on"), Tracker->StartCrime(Mugging, FVector::ZeroVector, 2));
	Tracker->Advance(30.f, HawkeyeCrimeTest::State(0, 0));
	TestEqual(TEXT("Nobody on the victim: his clock stands still"), Tracker->GetVictimHits(), 0);
	Tracker->Advance(16.f, HawkeyeCrimeTest::State(0, 2));
	TestEqual(TEXT("One hit after 16 s on him"), Tracker->GetVictimHits(), 1);
	Tracker->Advance(20.f, HawkeyeCrimeTest::State(1, 0));
	TestEqual(TEXT("A thug busy with her does not hit him"), Tracker->GetVictimHits(), 1);
	TestFalse(TEXT("Still on after two hits"), Tracker->Advance(14.f, HawkeyeCrimeTest::State(1, 1)));
	TestEqual(TEXT("Two hits at 30 s on him"), Tracker->GetVictimHits(), 2);
	TestTrue(TEXT("The third hit ends it"), Tracker->Advance(15.f, HawkeyeCrimeTest::State(1, 1)));
	TestEqual(TEXT("As the victim lost"), Tracker->GetLastResult().Reason, ECrimeEndReason::VictimLost);
	TestEqual(TEXT("No completion counted"), Tracker->GetCompletions(ECrimeType::Mugging), 0);

	UCrimeDefinition* Robbery = HawkeyeCrimeTest::Make(ECrimeType::Robbery, 3);
	Tracker->StartCrime(Robbery, FVector::ZeroVector, 3);
	FCrimeTickState Escaped = HawkeyeCrimeTest::State(1);
	Escaped.bLootEscaped = true;
	Escaped.ThugsOnVictim = 3;
	TestTrue(TEXT("The loot escaping ends a robbery"), Tracker->Advance(0.1f, Escaped));
	TestEqual(TEXT("As got away"), Tracker->GetLastResult().Reason, ECrimeEndReason::LootEscaped);
	TestEqual(TEXT("A robbery has no victim clock"), Tracker->GetLastResult().VictimHits, 0);

	UCrimeDefinition* Ambush = HawkeyeCrimeTest::Make(ECrimeType::Ambush, 4);
	Tracker->StartCrime(Ambush, FVector::ZeroVector, 4);
	TestFalse(TEXT("149 m away is still on"), Tracker->Advance(0.1f, HawkeyeCrimeTest::State(0, 0, 14900.f)));
	TestTrue(TEXT("151 m away abandons it"), Tracker->Advance(0.1f, HawkeyeCrimeTest::State(0, 0, 15100.f)));
	TestEqual(TEXT("As abandoned"), Tracker->GetLastResult().Reason, ECrimeEndReason::Abandoned);
	Tracker->StartCrime(Ambush, FVector::ZeroVector, 4);
	FCrimeTickState Out = HawkeyeCrimeTest::State(0);
	Out.bPlayerOut = true;
	TestTrue(TEXT("Her death abandons it"), Tracker->Advance(0.1f, Out));
	Tracker->StartCrime(Ambush, FVector::ZeroVector, 4);
	Tracker->AbortCrime();
	TestEqual(TEXT("Abort is abandoned"), Tracker->GetLastResult().Reason, ECrimeEndReason::Abandoned);
	TestFalse(TEXT("And ends it"), Tracker->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeCompletionAndSave, "Hawkeye.Crime.CompletionAndSaveCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeCompletionAndSave::RunTest(const FString& Parameters)
{
	UCrimeTracker* Tracker = NewObject<UCrimeTracker>();
	UCrimeDefinition* Mugging = HawkeyeCrimeTest::Make(ECrimeType::Mugging, 2);
	UCrimeDefinition* Robbery = HawkeyeCrimeTest::Make(ECrimeType::Robbery, 3);

	Tracker->StartCrime(Mugging, FVector::ZeroVector, 2);
	TestFalse(TEXT("One of two down is not done"), Tracker->Advance(1.f, HawkeyeCrimeTest::State(1, 1)));
	TestTrue(TEXT("Both down stops it"), Tracker->Advance(1.f, HawkeyeCrimeTest::State(2, 0)));
	TestTrue(TEXT("Completed"), Tracker->GetLastResult().IsCompleted());
	TestEqual(TEXT("Its seconds"), Tracker->GetLastResult().Seconds, 2.f, 0.01f);
	TestEqual(TEXT("One mugging stopped"), Tracker->GetCompletions(ECrimeType::Mugging), 1);

	Tracker->StartCrime(Mugging, FVector::ZeroVector, 2);
	TestTrue(TEXT("Completion comes before the victim's last hit in the same step"),
		Tracker->Advance(100.f, HawkeyeCrimeTest::State(2, 2)) && Tracker->GetLastResult().IsCompleted());
	Tracker->StartCrime(Robbery, FVector::ZeroVector, 3);
	FCrimeTickState Recovered = HawkeyeCrimeTest::State(0);
	Recovered.bLootRecovered = true;
	TestTrue(TEXT("The loot picked up stops a robbery with its thugs standing"), Tracker->Advance(1.f, Recovered));
	TestEqual(TEXT("Two muggings, one robbery"), Tracker->GetCompletions(ECrimeType::Mugging) * 10 + Tracker->GetCompletions(ECrimeType::Robbery), 21);
	TestEqual(TEXT("The result carries the count"), Tracker->GetLastResult().Completions, 1);

	UHawkeyeCampaignState* Campaign = NewObject<UHawkeyeCampaignState>();
	Tracker->ExportCompletions(Campaign->CrimeTypes, Campaign->CrimeCompletions);
	TestEqual(TEXT("Two types saved"), Campaign->CrimeTypes.Num(), 2);
	TestEqual(TEXT("Under their keys"), Campaign->CrimeTypes.Num() == 2 ? Campaign->CrimeTypes[0] : NAME_None, FName(TEXT("Mugging")));
	UCrimeTracker* Loaded = NewObject<UCrimeTracker>();
	Loaded->ImportCompletions(Campaign->CrimeTypes, Campaign->CrimeCompletions);
	TestEqual(TEXT("Loaded muggings"), Loaded->GetCompletions(ECrimeType::Mugging), 2);
	TestEqual(TEXT("Loaded robberies"), Loaded->GetCompletions(ECrimeType::Robbery), 1);
	TestEqual(TEXT("Loaded ambushes"), Loaded->GetCompletions(ECrimeType::Ambush), 0);
	Loaded->StartCrime(Robbery, FVector::ZeroVector, 3);
	Loaded->Advance(1.f, HawkeyeCrimeTest::State(3));
	TestEqual(TEXT("A loaded count keeps counting"), Loaded->GetCompletions(ECrimeType::Robbery), 2);

	AddExpectedMessagePlain(TEXT("unknown crime type"), ELogVerbosity::Warning);
	Loaded->ImportCompletions({ TEXT("Mugging"), TEXT("Jaywalking") }, { 4, 9 });
	TestEqual(TEXT("An unknown key is dropped, the rest kept"), Loaded->GetCompletions(ECrimeType::Mugging), 4);

	Campaign->ResetCampaign();
	TestEqual(TEXT("A new game forgets the counts"), Campaign->CrimeTypes.Num() + Campaign->CrimeCompletions.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeDespawnRules, "Hawkeye.Crime.DespawnRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeDespawnRules::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Stopped: not at 59 s, far away"), UCrimeRules::ShouldDespawn(59.f, true, 9000.f, 60.f, 20.f, 4000.f));
	TestFalse(TEXT("Stopped: not at 61 s beside her"), UCrimeRules::ShouldDespawn(61.f, true, 1000.f, 60.f, 20.f, 4000.f));
	TestTrue(TEXT("Stopped: at 60 s once 40 m away"), UCrimeRules::ShouldDespawn(60.f, true, 4000.f, 60.f, 20.f, 4000.f));
	TestFalse(TEXT("Failed: not at 19 s"), UCrimeRules::ShouldDespawn(19.f, false, 9000.f, 60.f, 20.f, 4000.f));
	TestTrue(TEXT("Failed: at 20 s wherever she is"), UCrimeRules::ShouldDespawn(20.f, false, 100.f, 60.f, 20.f, 4000.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeHudText, "Hawkeye.Crime.HudText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeHudText::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("The HUD line"), UCrimeRules::FormatHudLine(FText::FromString(TEXT("[Crime: mugging]")), 3790.f).ToString(),
		FString(TEXT("[Crime: mugging] 38 m")));
	TestEqual(TEXT("No grouping past a kilometre"), UCrimeRules::FormatHudLine(FText::FromString(TEXT("[x]")), 123400.f).ToString(),
		FString(TEXT("[x] 1234 m")));
	UCrimeDefinition* Unnamed = HawkeyeCrimeTest::Make(ECrimeType::Rooftop, 3);
	Unnamed->Name = FText::GetEmpty();
	TestEqual(TEXT("An unnamed crime shows its type in brackets"), Unnamed->GetDisplayName().ToString(), FString(TEXT("[Crime: rooftop]")));
	TestEqual(TEXT("Roster count"), Unnamed->GetThugCount(), 3);
	ECrimeType Parsed = ECrimeType::Mugging;
	TestTrue(TEXT("The cvar's type parses without case"), UCrimeRules::ParseType(TEXT(" Robbery "), Parsed) && Parsed == ECrimeType::Robbery);
	TestFalse(TEXT("Nonsense does not"), UCrimeRules::ParseType(TEXT("heist"), Parsed));
	TestFalse(TEXT("Nor does empty"), UCrimeRules::ParseType(TEXT(""), Parsed));
	TestEqual(TEXT("Stopped toast"), UCrimeRules::EndReasonText(ECrimeEndReason::Completed).ToString(), FString(TEXT("[Crime stopped]")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeWorldMugging, "Hawkeye.Crime.WorldMuggingOneAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeWorldMugging::RunTest(const FString& Parameters)
{
	HawkeyeCrimeTest::FCrimeWorld W;
	if (!W.IsValid())
	{
		AddError(TEXT("The test world has no crime or mission subsystem, or no player."));
		return false;
	}
	UCrimeDefinition* Mugging = HawkeyeCrimeTest::Make(ECrimeType::Mugging, 2);
	ACrimeSpot* Here = W.Spot(FVector(3000.f, 0.f, 0.f), Mugging);
	ACrimeSpot* There = W.Spot(FVector(-3000.f, 0.f, 0.f), Mugging);

	TestTrue(TEXT("A mugging starts at a spot"), W.Crimes->StartCrimeAt(Here, Mugging, W.Player));
	TestFalse(TEXT("Never two at once"), W.Crimes->StartCrimeAt(There, Mugging, W.Player));
	TestFalse(TEXT("Nor a due one"), W.Crimes->TryStartDueCrime());
	TestEqual(TEXT("Two thugs"), W.Crimes->GetThugs().Num(), 2);
	ACivilian* Victim = W.Crimes->GetVictim();
	TestTrue(TEXT("And a cowering victim"), Victim && Victim->IsCowering());
	TestEqual(TEXT("Both thugs are on him"), W.Crimes->CountThugsOnVictim(), 2);
	for (const TWeakObjectPtr<AThugCharacter>& Thug : W.Crimes->GetThugs())
	{
		TestTrue(TEXT("A crime thug is left out of the save and tagged"), Thug.IsValid() && Thug->bSkipSave
			&& Thug->ActorHasTag(UCrimeSubsystem::CrimeThugTag) && Thug->ActorHasTag(TEXT("Thug")));
	}
	TestEqual(TEXT("One marker, on the victim"), W.Missions->GetSecondaryMarkers().Num(), 1);

	FText Title, Status;
	UCrimePanelWidget::FormatLines(W.Crimes, W.Player->GetActorLocation(), Title, Status);
	TestTrue(TEXT("The HUD line names it with the distance"), Title.ToString().StartsWith(TEXT("[Crime: mugging] 3")));
	TestEqual(TEXT("And the state"), Status.ToString(), FString(TEXT("Down 0/2   Victim hit 0/3")));

	W.Crimes->Tick(16.f);
	TestEqual(TEXT("The victim takes his first hit at 15 s"), Victim ? Victim->GetHitsTaken() : -1, 1);
	W.Crimes->ChapterBeatOverride = true;
	TestTrue(TEXT("A chapter beat pauses it"), W.Crimes->IsCrimePaused());
	W.Crimes->Tick(100.f);
	TestEqual(TEXT("Paused, nothing moves"), W.Crimes->GetTracker()->GetVictimHits(), 1);
	TestEqual(TEXT("And the marker waits"), W.Missions->GetSecondaryMarkers().Num(), 0);
	W.Crimes->ChapterBeatOverride = false;
	W.Crimes->Tick(0.1f);
	TestEqual(TEXT("The marker is back"), W.Missions->GetSecondaryMarkers().Num(), 1);

	W.KillAll();
	W.Crimes->Tick(0.1f);
	TestFalse(TEXT("Both down stops it"), W.Crimes->IsCrimeActive());
	TestTrue(TEXT("Completed"), W.Crimes->GetTracker()->GetLastResult().IsCompleted());
	TestEqual(TEXT("Five arrows"), W.Inventory->GetArrowCount(1), 15);
	TestEqual(TEXT("On the result"), W.Crimes->GetTracker()->GetLastResult().ArrowsGiven, 5);
	TestTrue(TEXT("The victim is freed"), Victim && Victim->IsFreed() && !Victim->IsCowering());
	TestTrue(TEXT("With his placeholder thanks"), Victim && Victim->IsThanksShowing());
	TestEqual(TEXT("Counted"), W.Crimes->GetCompletions(ECrimeType::Mugging), 1);
	TestEqual(TEXT("The marker goes"), W.Missions->GetSecondaryMarkers().Num(), 0);
	TestEqual(TEXT("Two thugs and the victim to clean up"), W.Crimes->GetLeftoverCount(), 3);

	// Stopped: 60 s after, and only once she is 40 m away.
	W.Player->SetActorLocation(FVector(3000.f, 200.f, 100.f));
	W.Crimes->Tick(61.f);
	TestEqual(TEXT("Beside her they stay"), W.Crimes->GetLeftoverCount(), 3);
	W.Player->SetActorLocation(FVector(-20000.f, 0.f, 100.f));
	W.Crimes->Tick(0.1f);
	TestEqual(TEXT("Far away they go"), W.Crimes->GetLeftoverCount(), 0);

	// Failed: 20 s after, wherever she is.
	W.Player->SetActorLocation(FVector(3000.f, 200.f, 100.f));
	TestTrue(TEXT("Another starts"), W.Crimes->StartCrimeAt(There, Mugging, W.Player));
	W.Crimes->Tick(15.1f);
	W.Crimes->Tick(15.1f);
	W.Crimes->Tick(15.1f);
	TestEqual(TEXT("Three hits lose the victim"), W.Crimes->GetTracker()->GetLastResult().Reason, ECrimeEndReason::VictimLost);
	TestEqual(TEXT("No arrows for that"), W.Inventory->GetArrowCount(1), 15);
	TestEqual(TEXT("Still one stopped"), W.Crimes->GetCompletions(ECrimeType::Mugging), 1);
	W.Crimes->Tick(19.f);
	TestEqual(TEXT("Not yet at 19 s"), W.Crimes->GetLeftoverCount(), 3);
	W.Crimes->Tick(1.1f);
	TestEqual(TEXT("Gone at 20 s, though she is right there"), W.Crimes->GetLeftoverCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeWorldSchedule, "Hawkeye.Crime.WorldScheduleAndDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeWorldSchedule::RunTest(const FString& Parameters)
{
	HawkeyeCrimeTest::FCrimeWorld W;
	if (!W.IsValid())
	{
		AddError(TEXT("The test world has no crime or mission subsystem, or no player."));
		return false;
	}
	UCrimeDefinition* Ambush = HawkeyeCrimeTest::Make(ECrimeType::Ambush, 1);
	ACrimeSpot* A = W.Spot(FVector(3000.f, 0.f, 0.f), Ambush);
	ACrimeSpot* B = W.Spot(FVector(3000.f, 7000.f, 0.f), Ambush);
	W.Spot(FVector(3000.f, -7000.f, 0.f), nullptr);

	W.Crimes->Tick(500.f);
	TestFalse(TEXT("Automation runs get no crime without the cvar"), W.Crimes->IsCrimeActive());

	HawkeyeCrimeTest::FIntervalScope Interval(5.f);
	W.Crimes->Tick(4.f);
	TestFalse(TEXT("Not before the cvar's 5 s"), W.Crimes->IsCrimeActive());
	W.Crimes->Tick(1.5f);
	TestTrue(TEXT("Due at 5 s: it starts"), W.Crimes->IsCrimeActive());
	TestTrue(TEXT("At the one spot 25 to 40 m out"), W.Crimes->GetActiveSpot() == A);
	W.Crimes->Tick(500.f);
	TestTrue(TEXT("Only one at a time, however long"), W.Crimes->IsCrimeActive() && W.Crimes->GetActiveSpot() == A);
	W.KillAll();
	W.Crimes->Tick(0.1f);
	TestFalse(TEXT("Stopped"), W.Crimes->IsCrimeActive());

	W.Crimes->Tick(10.f);
	TestFalse(TEXT("Next due, but A was the last crime and B is 70 m out"), W.Crimes->IsCrimeActive());
	W.Player->SetActorLocation(FVector(0.f, 6000.f, 100.f));
	W.Crimes->Tick(3.f);
	TestTrue(TEXT("She walks within 40 m of B: it starts there"), W.Crimes->IsCrimeActive() && W.Crimes->GetActiveSpot() == B);
	W.Crimes->AbortCrime();

	W.Player->SetActorLocation(FVector(0.f, 0.f, 100.f));
	W.Crimes->ChapterBeatOverride = true;
	W.Crimes->Tick(30.f);
	TestFalse(TEXT("Nothing starts during a chapter beat"), W.Crimes->IsCrimeActive());
	TestEqual(TEXT("And the clock does not run"), W.Crimes->GetScheduleClock().Elapsed, 0.f);
	W.Crimes->ChapterBeatOverride = false;
	W.Crimes->DespawnLeftoversNow();
	TestEqual(TEXT("Cleanup on demand"), W.Crimes->GetLeftoverCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeWorldRobbery, "Hawkeye.Crime.WorldRobberyLoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeWorldRobbery::RunTest(const FString& Parameters)
{
	HawkeyeCrimeTest::FCrimeWorld W;
	if (!W.IsValid())
	{
		AddError(TEXT("The test world has no crime or mission subsystem, or no player."));
		return false;
	}
	UCrimeDefinition* Robbery = HawkeyeCrimeTest::Make(ECrimeType::Robbery, 3);
	ACrimeSpot* Shop = W.Spot(FVector(0.f, 0.f, 0.f), Robbery);
	Shop->EscapeLocation = FVector(6000.f, 0.f, 0.f);
	W.Player->SetActorLocation(FVector(0.f, -3000.f, 100.f));

	TestTrue(TEXT("A robbery starts"), W.Crimes->StartCrimeAt(Shop, Robbery, W.Player));
	ACrimeLoot* Loot = W.Crimes->GetLoot();
	AThugCharacter* Runner = W.Crimes->GetRunner();
	TestTrue(TEXT("The runner carries the loot"), Loot && Runner && Loot->GetCarrier() == Runner);
	W.Crimes->Tick(1.f);
	TestFalse(TEXT("He waits while she is 30 m off"), W.Crimes->HasRunnerSetOff());
	W.Player->SetActorLocation(FVector(0.f, -1500.f, 100.f));
	W.Crimes->Tick(0.1f);
	TestTrue(TEXT("At 15 m he runs"), W.Crimes->HasRunnerSetOff());
	FText Title, Status;
	UCrimePanelWidget::FormatLines(W.Crimes, W.Player->GetActorLocation(), Title, Status);
	TestTrue(TEXT("The HUD says how far the loot is from escaping"), Status.ToString().Contains(TEXT("from escape")));
	Runner->SetActorLocation(FVector(5850.f, 0.f, Runner->GetActorLocation().Z));
	W.Crimes->Tick(0.1f);
	TestEqual(TEXT("At the escape point with the loot: they got away"), W.Crimes->GetTracker()->GetLastResult().Reason,
		ECrimeEndReason::LootEscaped);

	W.Crimes->DespawnLeftoversNow();
	W.Player->SetActorLocation(FVector(0.f, -500.f, 100.f));
	TestTrue(TEXT("Another robbery"), W.Crimes->StartCrimeAt(Shop, Robbery, W.Player));
	Loot = W.Crimes->GetLoot();
	Runner = W.Crimes->GetRunner();
	Runner->GetHealthComponent()->ApplyDamage(10000.f, W.Player);
	W.Crimes->Tick(0.1f);
	TestTrue(TEXT("The runner down drops the loot"), Loot && !Loot->IsCarried());
	TestTrue(TEXT("It is still on"), W.Crimes->IsCrimeActive());
	UCrimePanelWidget::FormatLines(W.Crimes, W.Player->GetActorLocation(), Title, Status);
	TestTrue(TEXT("The HUD says it was dropped"), Status.ToString().Contains(TEXT("dropped")));
	W.Player->SetActorLocation(Loot->GetActorLocation() + FVector(50.f, 0.f, 60.f));
	W.Crimes->Tick(0.1f);
	TestTrue(TEXT("Walking over it picks it up"), Loot->IsRecovered());
	TestTrue(TEXT("Which stops the robbery with two thugs still up"), W.Crimes->GetTracker()->GetLastResult().IsCompleted());
	TestEqual(TEXT("Counted as a robbery"), W.Crimes->GetCompletions(ECrimeType::Robbery), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCrimeAlleyRoster, "Hawkeye.Crime.AlleyRosterInALine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCrimeAlleyRoster::RunTest(const FString& Parameters)
{
	// The rule: pairs at either end of the passage, the second pair nearer, never more than 60 cm across.
	const FVector2D A = UCrimeRules::AlleyRosterOffset(0, 4, 500.f);
	const FVector2D B = UCrimeRules::AlleyRosterOffset(1, 4, 500.f);
	const FVector2D C = UCrimeRules::AlleyRosterOffset(2, 4, 500.f);
	const FVector2D D = UCrimeRules::AlleyRosterOffset(3, 4, 500.f);
	TestEqual(TEXT("The first 5 m up the passage"), A.X, 500.0);
	TestEqual(TEXT("The second 5 m down it"), B.X, -500.0);
	TestEqual(TEXT("The third 3 m up"), C.X, 300.0);
	TestEqual(TEXT("The fourth 3 m down"), D.X, -300.0);
	for (const FVector2D& Offset : { A, B, C, D })
	{
		TestTrue(TEXT("Within 60 cm of the passage's middle, so a 3 m alley holds him"), FMath::Abs(Offset.Y) <= 60.f + KINDA_SMALL_NUMBER);
	}
	TestTrue(TEXT("The two up the passage are not on one line"), !FMath::IsNearlyEqual(A.Y, C.Y));
	TestEqual(TEXT("Nobody for an empty roster"), UCrimeRules::AlleyRosterOffset(0, 0, 500.f), FVector2D::ZeroVector);

	// In the world: an ambush at an alley spot facing +X stands its four along X, not on a 5 m ring.
	HawkeyeCrimeTest::FCrimeWorld W;
	if (!W.IsValid())
	{
		AddError(TEXT("The test world has no crime or mission subsystem, or no player."));
		return false;
	}
	UCrimeDefinition* Ambush = HawkeyeCrimeTest::Make(ECrimeType::Ambush, 4);
	Ambush->Radius = 500.f;
	ACrimeSpot* Alley = W.Spot(FVector(3000.f, 0.f, 0.f), Ambush);
	Alley->bAlley = true;
	TestTrue(TEXT("The ambush starts in the alley"), W.Crimes->StartCrimeAt(Alley, Ambush, W.Player));
	TestEqual(TEXT("Four thugs"), W.Crimes->GetThugs().Num(), 4);
	for (const TWeakObjectPtr<AThugCharacter>& Thug : W.Crimes->GetThugs())
	{
		const FVector Offset = Thug.IsValid() ? Thug->GetActorLocation() - Alley->GetActorLocation() : FVector(0.f, 1000.f, 0.f);
		TestTrue(FString::Printf(TEXT("%s stands along the passage (%.0f cm across)"), *GetNameSafe(Thug.Get()), Offset.Y),
			FMath::Abs(Offset.Y) <= 100.f && FMath::Abs(Offset.X) >= 250.f);
	}
	W.Crimes->AbortCrime();
	return true;
}

#endif
