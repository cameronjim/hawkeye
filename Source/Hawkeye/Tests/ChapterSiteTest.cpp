#include "Combat/ArrowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Mission/ChapterEvents.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/ChapterSiteSubsystem.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Mission/ObjectiveTriggerVolume.h"
#include "Mission/ThugGroupObjective.h"
#include "GameFramework/PlayerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "World/ChapterEndInteractable.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The framework chapter 2 ("Reindeer Games") added, built in code (claude-docs/gameplay-semantics.md, "Mission",
 * "Chapter events", "Chapter sites"): an optional unmarked objective never blocks the chapter or shows on the HUD; an
 * objective's campaign flag goes up once and is saved; chapter events fire once a run and grant arrows; a site spawns,
 * wakes its crew on its alert objective, gates its examine on keycards, and a chapter built like chapter 2 completes
 * with nothing but what a player does.
 */
namespace HawkeyeSiteTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static UMissionObjective* AddObjective(UMissionDefinition* Mission, const TCHAR* Id, bool bOptional = false, bool bUnmarked = false)
	{
		UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
		Objective->ObjectiveId = Id;
		Objective->Title = FText::FromString(Id);
		Objective->bOptional = bOptional;
		Objective->bUnmarked = bUnmarked;
		Mission->Objectives.Add(Objective);
		return Objective;
	}

	/** Chapter 2's shape: reach, fight, return; the examine optional and unmarked, raising a flag. */
	static UMissionDefinition* MakeChapterTwo()
	{
		UMissionDefinition* Mission = NewObject<UMissionDefinition>();
		AddObjective(Mission, TEXT("reach_drive"));
		AddObjective(Mission, TEXT("stop_crew"));
		AddObjective(Mission, TEXT("return_toys"));
		AddObjective(Mission, TEXT("examine_courier"), true, true)->CampaignFlag = TEXT("saw_courier");
		return Mission;
	}

	static bool HasSaveGameFlag(const UClass* Class, const TCHAR* Property)
	{
		const FProperty* Found = Class->FindPropertyByName(Property);
		return Found && Found->HasAnyPropertyFlags(CPF_SaveGame);
	}
}

// --- Optional, unmarked objectives ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMissionUnmarkedOptional, "Hawkeye.Mission.UnmarkedOptionalNeverBlocksOrShows", HawkeyeSiteTest::Flags)

bool FHawkeyeMissionUnmarkedOptional::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSiteTest;
	UMissionTracker* Tracker = NewObject<UMissionTracker>();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Tracker->OnObjectiveUpdated.AddDynamic(Listener, &UHawkeyeTestListener::HandleObjectiveUpdated);
	Tracker->OnMissionComplete.AddDynamic(Listener, &UHawkeyeTestListener::HandleMissionComplete);
	TestTrue(TEXT("Chapter 2's shape starts"), Tracker->StartMission(MakeChapterTwo()));

	UMissionObjective* Examine = Tracker->FindObjective(TEXT("examine_courier"));
	TestTrue(TEXT("Optional and unmarked: hidden from the HUD"), Examine && Examine->IsHiddenFromHud());
	TestEqual(TEXT("The current objective is the first required one"), Tracker->GetCurrentObjective()->ObjectiveId, FName(TEXT("reach_drive")));

	// Examined first: it completes quietly and the chapter does not.
	TestTrue(TEXT("The examine completes"), Tracker->CompleteObjective(TEXT("examine_courier")));
	TestEqual(TEXT("It fires OnObjectiveUpdated (the save and the flag hear it)"), Listener->ObjectiveUpdatedCount, 1);
	TestFalse(TEXT("...but no toast"), UHawkeyeObjectiveWidget::ShouldToastCompletion(Examine));
	TestEqual(TEXT("The current objective is unchanged"), Tracker->GetCurrentObjective()->ObjectiveId, FName(TEXT("reach_drive")));
	TestFalse(TEXT("The chapter is not complete"), Tracker->IsMissionComplete());
	TestEqual(TEXT("No completion"), Listener->MissionCompleteCount, 0);

	for (const TCHAR* Id : { TEXT("reach_drive"), TEXT("stop_crew"), TEXT("return_toys") })
	{
		Tracker->CompleteObjective(Id);
		TestTrue(FString::Printf(TEXT("A required one toasts (%s)"), Id), UHawkeyeObjectiveWidget::ShouldToastCompletion(Tracker->FindObjective(Id)));
	}
	TestTrue(TEXT("The three required ones complete the chapter"), Tracker->IsMissionComplete());
	TestEqual(TEXT("Once"), Listener->MissionCompleteCount, 1);

	// Not examined at all: the chapter completes all the same, and an examine after it never completes it again.
	UMissionTracker* Second = NewObject<UMissionTracker>();
	UHawkeyeTestListener* SecondListener = NewObject<UHawkeyeTestListener>();
	Second->OnMissionComplete.AddDynamic(SecondListener, &UHawkeyeTestListener::HandleMissionComplete);
	Second->StartMission(MakeChapterTwo());
	for (const TCHAR* Id : { TEXT("reach_drive"), TEXT("stop_crew"), TEXT("return_toys") })
	{
		Second->CompleteObjective(Id);
	}
	TestTrue(TEXT("Without the examine the chapter completes"), Second->IsMissionComplete());
	TestTrue(TEXT("The examine after the end still completes"), Second->CompleteObjective(TEXT("examine_courier")));
	TestEqual(TEXT("...and the chapter completed once"), SecondListener->MissionCompleteCount, 1);

	// Unmarked on a required objective is ignored: the HUD has to show it.
	UMissionObjective* Required = NewObject<UMissionObjective>();
	Required->bUnmarked = true;
	Required->bCompleted = true;
	TestFalse(TEXT("A required objective is never hidden"), Required->IsHiddenFromHud());
	TestTrue(TEXT("...and toasts"), UHawkeyeObjectiveWidget::ShouldToastCompletion(Required));
	return true;
}

// --- Campaign flags ------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCampaignFlags, "Hawkeye.Campaign.FlagRaisedOnceAndSaved", HawkeyeSiteTest::Flags)

bool FHawkeyeCampaignFlags::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSiteTest;
	UHawkeyeCampaignState* Campaign = NewObject<UHawkeyeCampaignState>();
	UMissionObjective* Examine = NewObject<UMissionObjective>();
	Examine->ObjectiveId = TEXT("examine_courier");
	Examine->CampaignFlag = TEXT("saw_courier");

	TestFalse(TEXT("Not completed: no flag"), Campaign->ApplyObjectiveFlag(Examine));
	TestFalse(TEXT("...none up"), Campaign->HasFlag(TEXT("saw_courier")));
	Examine->bCompleted = true;
	TestTrue(TEXT("Completed: the flag goes up"), Campaign->ApplyObjectiveFlag(Examine));
	TestTrue(TEXT("...and is there"), Campaign->HasFlag(TEXT("saw_courier")));
	TestFalse(TEXT("Twice: nothing new"), Campaign->ApplyObjectiveFlag(Examine));
	TestEqual(TEXT("One flag"), Campaign->CampaignFlags.Num(), 1);

	UMissionObjective* Plain = NewObject<UMissionObjective>();
	Plain->bCompleted = true;
	TestFalse(TEXT("An objective with no flag raises none"), Campaign->ApplyObjectiveFlag(Plain));
	TestFalse(TEXT("None is never a flag"), Campaign->HasFlag(NAME_None));

	// A chapter change keeps it (only the objectives are per chapter); a new game clears it.
	Campaign->CompletedObjectives = { TEXT("examine_courier") };
	Campaign->MissionPath = TEXT("/Game/Missions/DA_CH04_SocialClub.DA_CH04_SocialClub");
	Campaign->CompletedObjectives.Reset();
	TestTrue(TEXT("Across chapters the flag stays"), Campaign->HasFlag(TEXT("saw_courier")));
	Campaign->FiredChapterEvents = { TEXT("parry") };
	Campaign->ResetCampaign();
	TestFalse(TEXT("A new game clears it"), Campaign->HasFlag(TEXT("saw_courier")));
	TestEqual(TEXT("...and the fired events"), Campaign->FiredChapterEvents.Num(), 0);

	// SPUD writes SaveGame properties only: both new fields must carry the flag to survive a save.
	TestTrue(TEXT("CampaignFlags is saved"), HasSaveGameFlag(UHawkeyeCampaignState::StaticClass(), TEXT("CampaignFlags")));
	TestTrue(TEXT("FiredChapterEvents is saved"), HasSaveGameFlag(UHawkeyeCampaignState::StaticClass(), TEXT("FiredChapterEvents")));
	TestTrue(TEXT("Kate's keycards are saved"), HasSaveGameFlag(AHawkeyeCharacter::StaticClass(), TEXT("SavedKeycards")));
	return true;
}

// --- Chapter events ------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterEventRules, "Hawkeye.ChapterEvents.FireOnceARun", HawkeyeSiteTest::Flags)

bool FHawkeyeChapterEventRules::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSiteTest;
	FHawkeyeChapterEvent Parry;
	Parry.Event = UHawkeyeChapterEventRules::Parry;
	Parry.Lines = { TEXT("ch02_parry") };
	FHawkeyeChapterEvent Arrive;
	Arrive.Event = UHawkeyeChapterEventRules::ObjectiveCompleted(TEXT("reach_drive"));
	Arrive.Lines = { TEXT("ch02_arrive"), TEXT("ch02_masks") };
	FHawkeyeChapterEvent Unlock;
	Unlock.Event = Arrive.Event;
	Unlock.Lines = { TEXT("ch02_foam_unlocked") };
	const TArray<FHawkeyeChapterEvent> Events = { Parry, Arrive, Unlock };

	TestEqual(TEXT("Names"), UHawkeyeChapterEventRules::ObjectiveStarted(TEXT("stop_crew")), FName(TEXT("objective_started:stop_crew")));
	const TArray<int32> Both = UHawkeyeChapterEventRules::Match(Events, Arrive.Event, {});
	TestTrue(TEXT("Every entry for an event fires, in order"), Both.Num() == 2 && Both[0] == 1 && Both[1] == 2);
	TestEqual(TEXT("Once fired, never again in the run"), UHawkeyeChapterEventRules::Match(Events, Arrive.Event, { Arrive.Event }).Num(), 0);
	TestEqual(TEXT("Another event is untouched"), UHawkeyeChapterEventRules::Match(Events, Parry.Event, { Arrive.Event }).Num(), 1);
	TestEqual(TEXT("An event the chapter does not list: nothing"), UHawkeyeChapterEventRules::Match(Events, TEXT("dodge"), {}).Num(), 0);
	TestEqual(TEXT("None: nothing"), UHawkeyeChapterEventRules::Match(Events, NAME_None, {}).Num(), 0);

	// In a world: the subsystem hears the objectives itself, fires once, grants its arrows, and a new run starts clean.
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UChapterEventSubsystem* Fire = World ? World->GetSubsystem<UChapterEventSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("chapter events"), Fire))
	{
		return false;
	}
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	UArrowDefinition* Foam = NewObject<UArrowDefinition>();
	Foam->Slot = 3;
	Foam->Cap = 4;
	FHawkeyeArrowGrant Grant;
	Grant.Arrow = Foam;
	Grant.Count = 3;
	FHawkeyeChapterEvent FoamUnlock = Unlock;
	FoamUnlock.Arrows = { Grant };
	FHawkeyeChapterEvent FirstUse;
	FirstUse.Event = UHawkeyeChapterEventRules::ArrowLoosed(Foam);
	FirstUse.Lines = { TEXT("ch02_foam_used") };
	UMissionDefinition* Mission = MakeChapterTwo();
	FHawkeyeChapterEvent FightStarts;
	FightStarts.Event = UHawkeyeChapterEventRules::ObjectiveStarted(TEXT("stop_crew"));
	Mission->Events = { Parry, Arrive, FoamUnlock, FirstUse, FightStarts };
	Fire->InventoryOverride = Inventory;
	Fire->bSkipLines = true;
	Fire->BindMissions();
	Missions->StartMission(Mission);
	TestEqual(TEXT("Nothing fired at the start"), Fire->GetFired().Num(), 0);
	TestEqual(TEXT("No foam yet"), Inventory->GetArrowCount(3), 0);
	Missions->CompleteObjective(TEXT("reach_drive"));
	TestTrue(TEXT("Arriving fired"), Fire->GetFired().Contains(Arrive.Event));
	TestEqual(TEXT("Three lines, in order"), Fire->GetSpokenLines(), TArray<FName>({ TEXT("ch02_arrive"), TEXT("ch02_masks"), TEXT("ch02_foam_unlocked") }));
	TestEqual(TEXT("The foam arrows granted"), Inventory->GetArrowCount(3), 3);
	TestTrue(TEXT("stop_crew became current: its start fired too"), Fire->GetFired().Contains(UHawkeyeChapterEventRules::ObjectiveStarted(TEXT("stop_crew"))));
	TestEqual(TEXT("A parry fires its line"), Fire->Notify(UHawkeyeChapterEventRules::Parry), 1);
	TestEqual(TEXT("The second parry: nothing"), Fire->Notify(UHawkeyeChapterEventRules::Parry), 0);
	TestEqual(TEXT("The first foam arrow loosed: its line"), Fire->Notify(UHawkeyeChapterEventRules::ArrowLoosed(Foam)), 1);
	TestEqual(TEXT("Five lines in all"), Fire->GetSpokenLines().Num(), 5);
	Inventory->ConsumeArrow(3);
	TestEqual(TEXT("A fired grant is not given twice"), Fire->Notify(Arrive.Event), 0);
	TestEqual(TEXT("...the quiver keeps what she spent"), Inventory->GetArrowCount(3), 2);
	Missions->AbortMission();
	Missions->StartMission(Mission);
	TestEqual(TEXT("A new run of the chapter starts with nothing fired"), Fire->GetFired().Num(), 0);
	return true;
}

// --- Chapter sites -------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterSiteCompletes, "Hawkeye.ChapterSite.ChapterTwoCompletesInAWorld", HawkeyeSiteTest::Flags)

bool FHawkeyeChapterSiteCompletes::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSiteTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UChapterSiteSubsystem* Site = World ? World->GetSubsystem<UChapterSiteSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("site"), Site))
	{
		return false;
	}
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(-3000.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	Site->PlayerOverride = Kate;
	// Examines offer themselves to a player-controlled pawn only.
	if (APlayerController* PC = Cast<APlayerController>(TestWorld.SpawnActor(APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator)))
	{
		PC->Possess(Kate);
	}

	UChapterSiteDefinition* Def = NewObject<UChapterSiteDefinition>();
	Def->PlayerStart = FVector(-2000.f, 0.f, 0.f);
	Def->PlayerStartYaw = 0.f;
	FChapterSiteVolume Arrive;
	Arrive.ObjectiveId = TEXT("reach_drive");
	Arrive.Location = FVector(0.f, 0.f, 150.f);
	Def->Volumes.Add(Arrive);
	for (const FVector& At : { FVector(500.f, -200.f, 0.f), FVector(500.f, 200.f, 0.f) })
	{
		FChapterSiteThug Member;
		Member.Weapon = EThugWeapon::Fists;
		Member.Location = At;
		Def->Crew.Add(Member);
	}
	Def->CrewObjective = TEXT("stop_crew");
	Def->CrewAlertObjective = TEXT("reach_drive");
	for (int32 Sack = 1; Sack <= 2; ++Sack)
	{
		FChapterSitePickup Pickup;
		Pickup.Location = FVector(600.f, Sack * 100.f, 0.f);
		Pickup.KeycardId = FName(*FString::Printf(TEXT("toy_sack_%d"), Sack));
		Def->Pickups.Add(Pickup);
	}
	FChapterSiteExamine Table;
	Table.ObjectiveId = TEXT("return_toys");
	Table.Location = FVector(-100.f, 300.f, 0.f);
	Table.RequiredKeycards = { TEXT("toy_sack_1"), TEXT("toy_sack_2") };
	Def->Examines.Add(Table);
	FChapterSiteExamine Courier;
	Courier.ObjectiveId = TEXT("examine_courier");
	Courier.Location = FVector(-1500.f, 800.f, 0.f);
	Courier.bRequireCurrentObjective = false;
	Def->Examines.Add(Courier);

	UMissionDefinition* Mission = MakeChapterTwo();
	Mission->Site = Def;
	Site->BindMissions();
	Missions->StartMission(Mission);
	TestEqual(TEXT("The mission start dressed the site: two crew"), Site->GetCrew().Num(), 2);
	TestEqual(TEXT("...one volume"), Site->GetVolumes().Num(), 1);
	TestEqual(TEXT("...two sacks"), Site->GetPickups().Num(), 2);
	TestEqual(TEXT("...two examines"), Site->GetExamines().Num(), 2);
	TestNotNull(TEXT("...and the crew objective"), Site->GetCrewObjective());
	TestFalse(TEXT("The crew waits"), Site->IsCrewAlerted());
	for (const AThugCharacter* Thug : Site->GetCrew())
	{
		const AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController());
		TestTrue(TEXT("Still: not thinking"), Brain && !Brain->IsThinkingEnabled());
		TestTrue(TEXT("Tagged for the group"), Thug->ActorHasTag(UChapterSiteSubsystem::CrewTag));
		TestTrue(TEXT("Never saved by SPUD"), Thug->bSkipSave);
	}
	TestTrue(TEXT("The opening puts Kate at the site's start"), Site->BeginChapter(Mission));
	TestTrue(TEXT("...there"), FVector::Dist2D(Kate->GetActorLocation(), Def->PlayerStart) < 5.f);

	AChapterEndInteractable* TableActor = nullptr;
	AChapterEndInteractable* CourierActor = nullptr;
	for (AChapterEndInteractable* Examine : Site->GetExamines())
	{
		(Examine->ObjectiveId == TEXT("return_toys") ? TableActor : CourierActor) = Examine;
	}
	if (!TestNotNull(TEXT("table"), TableActor) || !TestNotNull(TEXT("courier"), CourierActor))
	{
		return false;
	}
	// The courier is offered from the start, out of order, and completes quietly.
	TestTrue(TEXT("The courier is offered before anything else"), IInteractable::Execute_CanInteract(CourierActor, Kate));
	TestFalse(TEXT("The table is not (not current, no sacks)"), IInteractable::Execute_CanInteract(TableActor, Kate));

	Missions->CompleteObjective(TEXT("reach_drive"));
	TestTrue(TEXT("Arriving wakes the crew"), Site->IsCrewAlerted());
	for (AThugCharacter* Thug : Site->GetCrew())
	{
		TestTrue(TEXT("Alerted on her"), Thug->IsAlerted());
		Thug->GetHealthComponent()->ApplyDamage(9999.f, Kate);
	}
	TestTrue(TEXT("The crew down completes stop_crew"), Missions->GetTracker()->FindObjective(TEXT("stop_crew"))->IsCompleted());
	TestFalse(TEXT("The table still wants the sacks"), IInteractable::Execute_CanInteract(TableActor, Kate));
	TArray<APickupActor*> Sacks = Site->GetPickups();
	Sacks[0]->ApplyTo(Kate);
	TestFalse(TEXT("One sack is not enough"), IInteractable::Execute_CanInteract(TableActor, Kate));
	Sacks[1]->ApplyTo(Kate);
	TestTrue(TEXT("Both sacks: the table is offered"), IInteractable::Execute_CanInteract(TableActor, Kate));
	Missions->CompleteObjective(TEXT("return_toys"));
	TestTrue(TEXT("The toys back complete the chapter, examine or not"), Missions->IsMissionComplete());
	TestFalse(TEXT("The courier was never examined"), Missions->GetTracker()->FindObjective(TEXT("examine_courier"))->IsCompleted());

	// A load at stop_crew done, sacks carried: no crew comes back, no carried sack lies on the ground.
	Missions->AbortMission();
	Missions->StartMission(Mission);
	TestEqual(TEXT("Started again: the crew is back"), Site->GetCrew().Num(), 2);
	Missions->GetTracker()->RestoreCompletedObjectives({ TEXT("reach_drive"), TEXT("stop_crew") });
	Site->Reconcile();
	TestEqual(TEXT("After the load: no crew"), Site->GetCrew().Num(), 0);
	TestEqual(TEXT("...and no sack she carries"), Site->GetPickups().Num(), 0);
	Missions->AbortMission();
	Missions->StartMission(Mission);
	Missions->GetTracker()->RestoreCompletedObjectives({ TEXT("reach_drive") });
	Site->Reconcile();
	TestTrue(TEXT("A load mid-fight: the crew comes back awake"), Site->IsCrewAlerted() && Site->GetCrew().Num() == 2);

	// Another chapter clears it.
	UMissionDefinition* Other = NewObject<UMissionDefinition>();
	AddObjective(Other, TEXT("meet_tomas"));
	Missions->AbortMission();
	Missions->StartMission(Other);
	TestEqual(TEXT("Another chapter: no crew"), Site->GetCrew().Num(), 0);
	TestEqual(TEXT("...no examines"), Site->GetExamines().Num(), 0);
	TestNull(TEXT("...no site"), Site->GetDefinition());
	return true;
}

#endif
