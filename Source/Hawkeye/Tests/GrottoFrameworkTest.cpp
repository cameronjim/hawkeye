#include "Combat/ArrowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/ReturnFlight.h"
#include "Combat/ReturningArrowProjectile.h"
#include "Components/StaticMeshComponent.h"
#include "Dialogue/DialogueLine.h"
#include "Dialogue/DialogueQueue.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Mission/ChapterEvents.h"
#include "Mission/ChapterMaps.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/ChapterSiteSubsystem.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSceneReturn.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ArrowSwitch.h"
#include "World/ChapterEndInteractable.h"
#include "World/DoorActor.h"
#include "World/InteriorEntrance.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The general pieces chapter 3 ("The Grotto") added, in code (claude-docs/gameplay-semantics.md): returning arrows and
 * the switch they trip, which map a chapter's content belongs in and the chapter carrying on into an interior, stealth
 * objectives and site triggers, a door an objective opens, an examine that waits for an objective, and reactive lines.
 */
namespace HawkeyeGrottoTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static UMissionObjective* AddObjective(UMissionDefinition* Mission, const TCHAR* Id, bool bOptional = false)
	{
		UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
		Objective->ObjectiveId = Id;
		Objective->Title = FText::FromString(Id);
		Objective->bOptional = bOptional;
		Mission->Objectives.Add(Objective);
		return Objective;
	}

	/** Chapter 3's shape: find, get in unseen (stealth), open, Santa, the roof; the ornament after, optional. */
	static UMissionDefinition* MakeChapterThree()
	{
		UMissionDefinition* Mission = NewObject<UMissionDefinition>();
		AddObjective(Mission, TEXT("find_sleigh"));
		UMissionObjective* Enter = AddObjective(Mission, TEXT("enter_grotto"));
		Enter->SpottedTitle = FText::FromString(TEXT("Get to the workshop door. They know you're here"));
		Enter->SpottedHint = FText::FromString(TEXT("Seen"));
		AddObjective(Mission, TEXT("open_vault"));
		AddObjective(Mission, TEXT("beat_santa"));
		AddObjective(Mission, TEXT("leave_roof"));
		AddObjective(Mission, TEXT("examine_ornament"), true)->CampaignFlag = TEXT("found_ornament");
		return Mission;
	}

	static UArrowDefinition* MakeBoomerang(int32 MaxTargets)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>();
		Arrow->Slot = 4;
		Arrow->Cap = 2;
		Arrow->Damage = 15.f;
		Arrow->bRecoverable = false;
		Arrow->ProjectileClass = AReturningArrowProjectile::StaticClass();
		Arrow->ReturnFlight.MaxTargets = MaxTargets;
		Arrow->ReturnFlight.MaxRange = 1500.f;
		Arrow->ReturnFlight.Speed = 2600.f;
		Arrow->ReturnFlight.SelfHitDamage = 2.f;
		return Arrow;
	}

	/** Looses Arrow from Kate along +X and runs the flight until it is home (6 s at most). */
	static AReturningArrowProjectile* Loose(const FHawkeyeTestWorld& TestWorld, AHawkeyeCharacter* Kate, UArrowDefinition* Arrow)
	{
		const FVector From = Kate->GetActorLocation() + FVector(60.f, 0.f, 40.f);
		AReturningArrowProjectile* Flight = Cast<AReturningArrowProjectile>(TestWorld.SpawnActor(AReturningArrowProjectile::StaticClass(), From,
			FRotator::ZeroRotator));
		if (!Flight)
		{
			return nullptr;
		}
		Flight->InitArrow(Arrow, nullptr, Arrow->Damage, Kate, nullptr);
		Flight->LaunchWithVelocity(FVector(6000.f, 0.f, 0.f));
		for (float Elapsed = 0.f; Flight->GetReturnPhase() != EHawkeyeReturnPhase::Home && Elapsed < 6.f; Elapsed += 0.01f)
		{
			Flight->AdvanceReturn(0.01f);
		}
		return Flight;
	}
}

// --- Returning arrows: the rules -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeReturnFlightRules, "Hawkeye.ReturnFlight.Rules", HawkeyeGrottoTest::Flags)

bool FHawkeyeReturnFlightRules::RunTest(const FString& Parameters)
{
	using R = UHawkeyeReturnFlightRules;
	// Max targets.
	TestTrue(TEXT("None hit of two: another may be"), R::CanHitMore(0, 2));
	TestTrue(TEXT("One of two: another"), R::CanHitMore(1, 2));
	TestFalse(TEXT("Two of two: no more"), R::CanHitMore(2, 2));
	TestFalse(TEXT("A max under one counts as one"), R::CanHitMore(1, 0));

	// Why it turns: a switch, then a wall, then its targets, then its range.
	TestEqual(TEXT("A switch turns it"), R::TurnBack(10.f, 2000.f, 0, 2, true, true), EHawkeyeReturnTurn::Switch);
	TestEqual(TEXT("A wall turns it"), R::TurnBack(10.f, 2000.f, 0, 2, true, false), EHawkeyeReturnTurn::Wall);
	TestEqual(TEXT("Its last target turns it"), R::TurnBack(10.f, 2000.f, 2, 2, false, false), EHawkeyeReturnTurn::Targets);
	TestEqual(TEXT("Its range turns it"), R::TurnBack(2000.f, 2000.f, 1, 2, false, false), EHawkeyeReturnTurn::Range);
	TestEqual(TEXT("Otherwise on it goes"), R::TurnBack(1999.f, 2000.f, 1, 2, false, false), EHawkeyeReturnTurn::None);

	// The way back: the path from its end to its start, then to her (who has moved).
	const TArray<FVector> Path = { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 500.f, 0.f) };
	const FVector Kate(0.f, -300.f, 0.f);
	TestEqual(TEXT("Its length: back along both legs, then 300 to her"), R::ReturnLength(Path, Kate), 1800.f);
	TestTrue(TEXT("It starts where it turned"), R::PointAlongReturn(Path, Kate, 0.f).Equals(FVector(1000.f, 500.f, 0.f), 0.01f));
	TestTrue(TEXT("Back down the second leg first"), R::PointAlongReturn(Path, Kate, 250.f).Equals(FVector(1000.f, 250.f, 0.f), 0.01f));
	TestTrue(TEXT("Then the first leg, the way it came"), R::PointAlongReturn(Path, Kate, 1000.f).Equals(FVector(500.f, 0.f, 0.f), 0.01f));
	TestTrue(TEXT("Then to where she is now"), R::PointAlongReturn(Path, Kate, 1650.f).Equals(FVector(0.f, -150.f, 0.f), 0.01f));
	TestTrue(TEXT("Past the end: at her"), R::PointAlongReturn(Path, Kate, 5000.f).Equals(Kate, 0.01f));
	TestTrue(TEXT("No path: at her"), R::PointAlongReturn({}, Kate, 10.f).Equals(Kate, 0.01f));

	// Caught: coming at her from in front, and she is free.
	const FVector Coming(-1.f, 0.f, 0.f);   // flying toward -X: it comes from +X
	TestTrue(TEXT("Facing it: caught"), R::IsCaught(Coming, FVector(1.f, 0.f, 0.f), false, 70.f));
	TestTrue(TEXT("60 degrees off: still caught"), R::IsCaught(Coming, FRotator(0.f, 60.f, 0.f).Vector(), false, 70.f));
	TestFalse(TEXT("80 degrees off: it hits her"), R::IsCaught(Coming, FRotator(0.f, 80.f, 0.f).Vector(), false, 70.f));
	TestFalse(TEXT("Her back to it: it hits her"), R::IsCaught(Coming, FVector(-1.f, 0.f, 0.f), false, 70.f));
	TestFalse(TEXT("Busy (a takedown, a zip, staggered): it hits her"), R::IsCaught(Coming, FVector(1.f, 0.f, 0.f), true, 70.f));

	// The self-hit: a token, and never the last of her health.
	TestEqual(TEXT("Full health: the 2 lands"), R::SelfHitDamage(2.f, 100.f), 2.f);
	TestEqual(TEXT("At 2 health: 1, leaving 1"), R::SelfHitDamage(2.f, 2.f), 1.f);
	TestEqual(TEXT("At 1 health: nothing"), R::SelfHitDamage(2.f, 1.f), 0.f);
	TestEqual(TEXT("At 0.5: nothing, never negative"), R::SelfHitDamage(2.f, 0.5f), 0.f);

	// The switch it trips.
	TestTrue(TEXT("A returning arrow trips a switch that wants one"), AArrowSwitch::Trips(false, true, true));
	TestFalse(TEXT("A plain arrow does not"), AArrowSwitch::Trips(false, true, false));
	TestTrue(TEXT("...unless any arrow will do"), AArrowSwitch::Trips(false, false, false));
	TestFalse(TEXT("Tripped once, never again"), AArrowSwitch::Trips(true, false, true));
	TestEqual(TEXT("Its event"), AArrowSwitch::EventFor(TEXT("alarm")), FName(TEXT("switch:alarm")));
	return true;
}

// --- Returning arrows: in a world ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeReturnFlightWorld, "Hawkeye.ReturnFlight.OutThroughTwoAndBackInAWorld", HawkeyeGrottoTest::Flags)

bool FHawkeyeReturnFlightWorld::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrottoTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	AHawkeyeCharacter* Kate = World ? Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, 0.f, 100.f),
		FRotator::ZeroRotator)) : nullptr;
	if (!TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	UArrowDefinition* Boomerang = MakeBoomerang(2);
	Inventory->AddArrows(Boomerang, 1);
	Inventory->ConsumeArrow(4);
	TestEqual(TEXT("Loosed: none left in the quiver"), Inventory->GetArrowCount(4), 0);

	// Two in a line and a third behind them: it goes through the first two and turns.
	TArray<AThugCharacter*> Line;
	for (const float X : { 500.f, 750.f, 1000.f })
	{
		Line.Add(Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(X, 0.f, 100.f), FRotator(0.f, 180.f, 0.f))));
	}
	if (!TestTrue(TEXT("three thugs"), Line[0] && Line[1] && Line[2]))
	{
		return false;
	}
	const int32 CaughtBefore = AReturningArrowProjectile::CaughtCount;
	const int32 SelfHitBefore = AReturningArrowProjectile::SelfHitCount;
	AReturningArrowProjectile* First = Loose(TestWorld, Kate, Boomerang);
	if (!TestNotNull(TEXT("the flight"), First))
	{
		return false;
	}
	TestEqual(TEXT("Home"), First->GetReturnPhase(), EHawkeyeReturnPhase::Home);
	TestEqual(TEXT("Two bodies hurt"), First->GetHits(), 2);
	TestEqual(TEXT("...and its last target turned it"), First->GetTurnReason(), EHawkeyeReturnTurn::Targets);
	TestTrue(TEXT("The first is hurt"), Line[0]->GetHealthComponent()->GetCurrentHealth() < Line[0]->GetHealthComponent()->GetMaxHealth());
	TestTrue(TEXT("The second is hurt"), Line[1]->GetHealthComponent()->GetCurrentHealth() < Line[1]->GetHealthComponent()->GetMaxHealth());
	TestEqual(TEXT("The third is not (two at most)"), Line[2]->GetHealthComponent()->GetCurrentHealth(), Line[2]->GetHealthComponent()->GetMaxHealth());
	TestTrue(TEXT("Facing it (she looks down +X): caught"), First->WasCaught());
	TestEqual(TEXT("Caught counted"), AReturningArrowProjectile::CaughtCount, CaughtBefore + 1);
	TestEqual(TEXT("It is back in the quiver"), Inventory->GetArrowCount(4), 1);
	const float Full = Kate->GetHealthComponent()->GetCurrentHealth();
	TestEqual(TEXT("A catch costs her nothing"), Full, Kate->GetHealthComponent()->GetMaxHealth());

	// Her back to it: it comes back into her. A token hurt and a stagger, and never the last of her health.
	for (AThugCharacter* Thug : Line)
	{
		Thug->Destroy();
	}
	Kate->SetActorRotation(FRotator(0.f, 180.f, 0.f));
	Inventory->ConsumeArrow(4);
	AReturningArrowProjectile* Second = Loose(TestWorld, Kate, Boomerang);
	if (!TestNotNull(TEXT("the second flight"), Second))
	{
		return false;
	}
	TestEqual(TEXT("Nothing in the way: its range turned it"), Second->GetTurnReason(), EHawkeyeReturnTurn::Range);
	TestFalse(TEXT("Her back to it: not caught"), Second->WasCaught());
	TestEqual(TEXT("Self-hit counted"), AReturningArrowProjectile::SelfHitCount, SelfHitBefore + 1);
	TestEqual(TEXT("2 damage"), Kate->GetHealthComponent()->GetCurrentHealth(), Full - 2.f);
	TestTrue(TEXT("...and a stagger"), Kate->IsStaggered());
	TestEqual(TEXT("Back in the quiver either way"), Inventory->GetArrowCount(4), 1);

	// At 1 health it cannot take the last of it.
	Kate->GetHealthComponent()->ApplyDamage(Kate->GetHealthComponent()->GetCurrentHealth() - 1.f, nullptr);
	Inventory->ConsumeArrow(4);
	AReturningArrowProjectile* Third = Loose(TestWorld, Kate, Boomerang);
	TestTrue(TEXT("At 1 health: it hit her again"), Third && !Third->WasCaught());
	TestEqual(TEXT("...and she still has 1"), Kate->GetHealthComponent()->GetCurrentHealth(), 1.f);
	TestTrue(TEXT("...alive"), Kate->GetHealthComponent()->IsAlive());

	// A switch on a wall turns it and trips; a plain arrow's hit on another does nothing.
	Kate->GetHealthComponent()->Heal(1000.f);
	Kate->SetActorRotation(FRotator::ZeroRotator);
	AArrowSwitch* Switch = Cast<AArrowSwitch>(TestWorld.SpawnActor(AArrowSwitch::StaticClass(), FVector(700.f, 0.f, 140.f), FRotator(0.f, 180.f, 0.f)));
	if (!TestNotNull(TEXT("a switch"), Switch))
	{
		return false;
	}
	Switch->SwitchId = TEXT("alarm");
	Inventory->ConsumeArrow(4);
	AReturningArrowProjectile* Fourth = Loose(TestWorld, Kate, Boomerang);
	TestTrue(TEXT("The switch turned it"), Fourth && Fourth->GetTurnReason() == EHawkeyeReturnTurn::Switch);
	TestTrue(TEXT("...and it is tripped"), Switch->IsTripped());
	AArrowSwitch* Other = Cast<AArrowSwitch>(TestWorld.SpawnActor(AArrowSwitch::StaticClass(), FVector(700.f, 300.f, 140.f), FRotator::ZeroRotator));
	TestFalse(TEXT("A plain arrow's hit does not trip a switch that wants a returning one"), Other && Other->HandleArrowHit(Kate, Kate));
	return true;
}

// --- Which map content belongs in ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChapterMapsRules, "Hawkeye.ChapterMaps.ContentBelongsInItsMap", HawkeyeGrottoTest::Flags)

bool FHawkeyeChapterMapsRules::RunTest(const FString& Parameters)
{
	using M = UHawkeyeChapterMaps;
	const FString District(TEXT("/Game/Maps/L_District_EastVillage"));
	const FString Grotto(TEXT("/Game/Maps/L_Int_Grotto"));
	const FString Sample(TEXT("/Game/Maps/L_Int_Sample"));
	TestTrue(TEXT("No map named: the district"), M::BelongsInWorld(FString(), District, false));
	TestFalse(TEXT("No map named: never inside an interior"), M::BelongsInWorld(FString(), Grotto, true));
	TestTrue(TEXT("The grotto's: in the grotto"), M::BelongsInWorld(Grotto, Grotto, true));
	TestFalse(TEXT("The grotto's: not in another interior"), M::BelongsInWorld(Grotto, Sample, true));
	TestFalse(TEXT("The grotto's: not in the district"), M::BelongsInWorld(Grotto, District, false));
	const TArray<FString> Sites = { FString(), Grotto };
	TestEqual(TEXT("The district dresses the district's site"), M::PickSite(Sites, District, false), 0);
	TestEqual(TEXT("The grotto dresses the grotto's"), M::PickSite(Sites, Grotto, true), 1);
	TestEqual(TEXT("Another interior dresses none"), M::PickSite(Sites, Sample, true), INDEX_NONE);

	// A chapter carries on into an interior one of its sites names; chapter 2, with only a district site, into none.
	UMissionDefinition* Mission = HawkeyeGrottoTest::MakeChapterThree();
	UChapterSiteDefinition* Outside = NewObject<UChapterSiteDefinition>();
	UChapterSiteDefinition* Inside = NewObject<UChapterSiteDefinition>();
	Inside->Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_Int_Grotto.L_Int_Grotto")));
	Mission->Site = Outside;
	TestFalse(TEXT("A district site alone: no interior"), M::ContinuesInto(Mission, Grotto));
	Mission->InteriorSites.Add(Inside);
	TestTrue(TEXT("Its interior site names the grotto: it carries on into it"), M::ContinuesInto(Mission, Grotto));
	TestFalse(TEXT("...but not into the auction house"), M::ContinuesInto(Mission, Sample));
	TestFalse(TEXT("No chapter: no"), M::ContinuesInto(nullptr, Grotto));

	// The save's rule: a scene or a plain interior refuses saves; a chapter interior allows them; the way back never.
	FHawkeyeSceneReturn Scene;
	TestTrue(TEXT("In the district: saves allowed"), Scene.AllowsSave());
	Scene.BeginInterior(District, TEXT("City_InteriorReturn_Sample"), true);
	TestFalse(TEXT("An interior the chapter does not carry into: refused"), Scene.AllowsSave());
	Scene.bChapterContinues = true;
	TestTrue(TEXT("A chapter interior: allowed"), Scene.AllowsSave());
	Scene.Leave(TEXT("City_InteriorRoof_Grotto"));
	TestFalse(TEXT("On the way out: refused"), Scene.AllowsSave());
	TestEqual(TEXT("The roof door comes out on the roof"), Scene.ReturnPointLabel, FName(TEXT("City_InteriorRoof_Grotto")));
	FHawkeyeSceneReturn Flashback;
	Flashback.bInScene = true;
	TestFalse(TEXT("A flashback's scene: refused"), Flashback.AllowsSave());

	// SPUD writes SaveGame properties only.
	for (const TCHAR* Field : { TEXT("InteriorReturnMap"), TEXT("InteriorReturnPoint"), TEXT("SpottedObjectives") })
	{
		const FProperty* Found = UHawkeyeCampaignState::StaticClass()->FindPropertyByName(Field);
		TestTrue(FString::Printf(TEXT("%s is saved"), Field), Found && Found->HasAnyPropertyFlags(CPF_SaveGame));
	}
	UHawkeyeCampaignState* Campaign = NewObject<UHawkeyeCampaignState>();
	Campaign->InteriorReturnMap = District;
	Campaign->SpottedObjectives = { TEXT("enter_grotto") };
	Campaign->ResetCampaign();
	TestTrue(TEXT("A new game forgets both"), Campaign->InteriorReturnMap.IsEmpty() && Campaign->SpottedObjectives.Num() == 0);
	return true;
}

// --- Stealth objectives and site triggers --------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStealthObjective, "Hawkeye.Mission.StealthObjectiveSpottedAndTriggers", HawkeyeGrottoTest::Flags)

bool FHawkeyeStealthObjective::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrottoTest;
	using S = UHawkeyeChapterSiteRules;
	TestTrue(TEXT("A stealth objective, a thug alerted: spotted"), S::ShouldMarkSpotted(true, false, 1));
	TestFalse(TEXT("Nobody alerted: not"), S::ShouldMarkSpotted(true, false, 0));
	TestFalse(TEXT("Already spotted: not again"), S::ShouldMarkSpotted(true, true, 2));
	TestFalse(TEXT("Not a stealth objective: never"), S::ShouldMarkSpotted(false, false, 2));
	TestTrue(TEXT("Inside a trigger: it fires"), S::ShouldFireTrigger(true, false, true, false));
	TestFalse(TEXT("Outside: no"), S::ShouldFireTrigger(false, false, false, false));
	TestFalse(TEXT("Fired already: no"), S::ShouldFireTrigger(true, false, false, true));
	TestFalse(TEXT("An unseen-only trigger while a thug is alerted: no"), S::ShouldFireTrigger(true, true, true, false));
	TestTrue(TEXT("...and with nobody alerted: yes"), S::ShouldFireTrigger(true, true, false, false));
	TestEqual(TEXT("The spotted event"), S::SpottedEvent(TEXT("enter_grotto")), FName(TEXT("spotted:enter_grotto")));

	// The tracker: only the current objective, once; the title swaps; it is not complete; a load puts it back.
	UMissionTracker* Tracker = NewObject<UMissionTracker>();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Tracker->OnObjectiveUpdated.AddDynamic(Listener, &UHawkeyeTestListener::HandleObjectiveUpdated);
	Tracker->StartMission(MakeChapterThree());
	TestFalse(TEXT("Not current yet: not spotted"), Tracker->MarkObjectiveSpotted(TEXT("enter_grotto")));
	Tracker->CompleteObjective(TEXT("find_sleigh"));
	const int32 Updates = Listener->ObjectiveUpdatedCount;
	TestTrue(TEXT("Current: spotted"), Tracker->MarkObjectiveSpotted(TEXT("enter_grotto")));
	TestEqual(TEXT("It tells the HUD (OnObjectiveUpdated)"), Listener->ObjectiveUpdatedCount, Updates + 1);
	const UMissionObjective* Enter = Tracker->FindObjective(TEXT("enter_grotto"));
	TestEqual(TEXT("The title is the spotted one"), Enter->Title.ToString(), FString(TEXT("Get to the workshop door. They know you're here")));
	TestFalse(TEXT("...not completed"), Enter->IsCompleted());
	TestFalse(TEXT("Twice: no"), Tracker->MarkObjectiveSpotted(TEXT("enter_grotto")));
	TestEqual(TEXT("The save's list"), Tracker->GetSpottedObjectiveIds(), TArray<FName>({ TEXT("enter_grotto") }));
	TestFalse(TEXT("A plain objective is never spotted"), Tracker->FindObjective(TEXT("open_vault"))->MarkSpotted());
	UMissionTracker* Loaded = NewObject<UMissionTracker>();
	Loaded->StartMission(MakeChapterThree());
	TestEqual(TEXT("A load restores it"), Loaded->RestoreSpottedObjectives({ TEXT("enter_grotto") }), 1);
	TestTrue(TEXT("...with its title"), Loaded->FindObjective(TEXT("enter_grotto"))->Title.ToString().StartsWith(TEXT("Get to the workshop door")));
	TestTrue(TEXT("Completing a spotted objective still works"), Tracker->CompleteObjective(TEXT("enter_grotto")));

	// In a world: the site's watch spots the current stealth objective when a thug is alerted, and fires triggers.
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UChapterSiteSubsystem* Site = World ? World->GetSubsystem<UChapterSiteSubsystem>() : nullptr;
	UChapterEventSubsystem* Events = World ? World->GetSubsystem<UChapterEventSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("site"), Site) || !TestNotNull(TEXT("events"), Events))
	{
		return false;
	}
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	if (APlayerController* PC = Cast<APlayerController>(TestWorld.SpawnActor(APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator)))
	{
		PC->Possess(Kate);
	}
	Site->PlayerOverride = Kate;
	UChapterSiteDefinition* Def = NewObject<UChapterSiteDefinition>();
	FChapterSiteTrigger Overheard;
	Overheard.Event = TEXT("trigger:overheard");
	Overheard.Location = FVector(1000.f, 0.f, 100.f);
	Overheard.Extent = FVector(200.f, 200.f, 200.f);
	Overheard.bOnlyUnseen = true;
	Def->Triggers.Add(Overheard);
	FChapterSiteTrigger FirstElf;
	FirstElf.Event = TEXT("trigger:first_elf");
	FirstElf.Location = FVector(0.f, 0.f, 100.f);
	Def->Triggers.Add(FirstElf);
	FChapterSiteExamine Ornament;
	Ornament.ObjectiveId = TEXT("examine_ornament");
	Ornament.Location = FVector(100.f, 0.f, 0.f);
	Ornament.bRequireCurrentObjective = false;
	Ornament.RequiredObjective = TEXT("leave_roof");
	Def->Examines.Add(Ornament);
	FChapterSiteReturnPoint Roof;
	Roof.Label = TEXT("City_InteriorRoof_Grotto");
	Roof.Location = FVector(0.f, 2000.f, 1200.f);
	Def->ReturnPoints.Add(Roof);
	FChapterSiteInteriorDoor Door;
	Door.Interior = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/L_Int_Grotto.L_Int_Grotto")));
	Door.Location = FVector(-500.f, 0.f, 0.f);
	Door.ReturnPointLabel = TEXT("City_InteriorReturn_Grotto");
	Door.Tag = TEXT("Site_GrottoDoor");
	Def->InteriorDoors.Add(Door);
	UMissionDefinition* Mission = MakeChapterThree();
	Mission->Site = Def;
	FHawkeyeChapterEvent Overhear;
	Overhear.Event = Overheard.Event;
	FHawkeyeChapterEvent Spotted;
	Spotted.Event = S::SpottedEvent(TEXT("enter_grotto"));
	FHawkeyeChapterEvent First;
	First.Event = FirstElf.Event;
	Mission->Events = { Overhear, Spotted, First };
	Events->bSkipLines = true;
	Events->BindMissions();
	Site->BindMissions();
	Missions->StartMission(Mission);
	TestEqual(TEXT("The door is on the street"), Site->GetInteriorDoors().Num(), 1);
	TestTrue(TEXT("...tagged for the marker"), Site->GetInteriorDoors().Num() == 1 && Site->GetInteriorDoors()[0]->ActorHasTag(TEXT("Site_GrottoDoor")));
	TestTrue(TEXT("The roof's return point carries its label as a tag"), Site->GetReturnPoints().Num() == 1
		&& Site->GetReturnPoints()[0]->ActorHasTag(TEXT("City_InteriorRoof_Grotto")));

	Site->Watch();
	TestTrue(TEXT("Kate in the first trigger: it fired"), Events->GetFired().Contains(FirstElf.Event));
	TestFalse(TEXT("...not the one she is not in"), Events->GetFired().Contains(Overheard.Event));
	Missions->CompleteObjective(TEXT("find_sleigh"));
	AThugCharacter* Elf = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(1500.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("an elf"), Elf))
	{
		return false;
	}
	Elf->SetAlertState(EThugAlertState::Alerted);
	Kate->SetActorLocation(FVector(1000.f, 0.f, 100.f));
	Site->Watch();
	TestTrue(TEXT("An elf alerted while enter_grotto is current: spotted"), Missions->GetTracker()->FindObjective(TEXT("enter_grotto"))->bSpotted);
	TestTrue(TEXT("...its event fired"), Events->GetFired().Contains(Spotted.Event));
	TestFalse(TEXT("The overheard talk does not play once she is seen"), Events->GetFired().Contains(Overheard.Event));
	Elf->SetAlertState(EThugAlertState::Calm);
	Site->Watch();
	TestTrue(TEXT("Nobody alerted again: it plays"), Events->GetFired().Contains(Overheard.Event));

	// The ornament waits for the roof.
	AChapterEndInteractable* OrnamentActor = Site->GetExamines().Num() == 1 ? Site->GetExamines()[0] : nullptr;
	if (!TestNotNull(TEXT("the ornament"), OrnamentActor))
	{
		return false;
	}
	TestFalse(TEXT("Before the roof: not offered"), IInteractable::Execute_CanInteract(OrnamentActor, Kate));
	for (const TCHAR* Id : { TEXT("enter_grotto"), TEXT("open_vault"), TEXT("beat_santa"), TEXT("leave_roof") })
	{
		Missions->CompleteObjective(Id);
	}
	TestTrue(TEXT("The chapter is complete"), Missions->IsMissionComplete());
	TestTrue(TEXT("After the roof: offered"), IInteractable::Execute_CanInteract(OrnamentActor, Kate));
	return true;
}

// --- A door an objective opens -------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDoorOpensOnObjective, "Hawkeye.Door.OpensOnObjective", HawkeyeGrottoTest::Flags)

bool FHawkeyeDoorOpensOnObjective::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrottoTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions))
	{
		return false;
	}
	Missions->StartMission(MakeChapterThree());
	ADoorActor* Door = Cast<ADoorActor>(TestWorld.SpawnActor(ADoorActor::StaticClass(), FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator));
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(300.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("door"), Door) || !TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	Door->bLocked = true;
	Door->OpensOnObjective = TEXT("open_vault");
	TestFalse(TEXT("Waiting: it answers no interaction (the keypad opens it)"), IInteractable::Execute_CanInteract(Door, Kate));
	TestFalse(TEXT("Not done yet: shut"), Door->CheckOpensOnObjective());
	Missions->CompleteObjective(TEXT("find_sleigh"));
	Missions->CompleteObjective(TEXT("enter_grotto"));
	TestFalse(TEXT("Another objective: still shut"), Door->IsOpen());
	// Bound at BeginPlay, which a spawned door ran before its objective was set: check by hand as a load does.
	Missions->CompleteObjective(TEXT("open_vault"));
	Door->CheckOpensOnObjective();
	TestTrue(TEXT("Its objective done: open"), Door->IsOpen());
	TestFalse(TEXT("...and unlocked"), Door->bLocked);

	// One placed with its objective already done (a load past it) opens at once.
	ADoorActor* Late = Cast<ADoorActor>(TestWorld.SpawnActor(ADoorActor::StaticClass(), FVector(900.f, 0.f, 0.f), FRotator::ZeroRotator));
	Late->bLocked = true;
	Late->OpensOnObjective = TEXT("open_vault");
	TestTrue(TEXT("A load past its objective opens it"), Late->CheckOpensOnObjective() && Late->IsOpen());
	return true;
}

// --- Reactive lines ------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeReactiveLines, "Hawkeye.Dialogue.ReactiveLinesJumpOrDrop", HawkeyeGrottoTest::Flags)

bool FHawkeyeReactiveLines::RunTest(const FString& Parameters)
{
	UDialogueQueue* Queue = NewObject<UDialogueQueue>();
	TMap<FName, FHawkeyeDialogueLine> Lines;
	for (const TCHAR* Row : { TEXT("arrive"), TEXT("masks"), TEXT("bark"), TEXT("reply"), TEXT("parry"), TEXT("foam_used") })
	{
		FHawkeyeDialogueLine Line;
		Line.DurationSeconds = 3.f;
		Lines.Add(Row, Line);
	}
	Queue->SetLines(Lines);
	Queue->ReactiveMaxLateSeconds = 3.f;

	// Chapter 2's arrival: four story lines queued, the first speaking.
	for (const TCHAR* Row : { TEXT("arrive"), TEXT("masks"), TEXT("bark"), TEXT("reply") })
	{
		Queue->PlayLine(Row);
	}
	TestEqual(TEXT("The first speaks"), Queue->Advance(0.f, false), FName(TEXT("arrive")));
	Queue->Advance(1.f, true);
	// A parry 1 s into the first line: it goes ahead of the three still queued.
	TestTrue(TEXT("The parry line queues"), Queue->PlayLine(TEXT("parry"), true));
	TestEqual(TEXT("...ahead of the story lines"), Queue->GetPendingLines(), TArray<FName>({ TEXT("parry"), TEXT("masks"), TEXT("bark"), TEXT("reply") }));
	TestEqual(TEXT("It speaks the moment the line before it ends (2 s late)"), Queue->Advance(2.f, true), FName(TEXT("parry")));
	TestEqual(TEXT("Nothing dropped"), Queue->GetDroppedCount(), 0);
	TestEqual(TEXT("Then the story goes on"), Queue->Advance(3.f, true), FName(TEXT("masks")));

	// Two reactive lines in a row keep their order behind each other, ahead of the story.
	Queue->PlayLine(TEXT("parry"), true);
	Queue->PlayLine(TEXT("foam_used"), true);
	TestEqual(TEXT("Reactive lines queue first in, first out, ahead of the story"), Queue->GetPendingLines(),
		TArray<FName>({ TEXT("parry"), TEXT("foam_used"), TEXT("bark"), TEXT("reply") }));
	// "masks" has 3 s; the parry starts after it (3 s late, at the limit), the foam line would wait 6 s: dropped.
	TestEqual(TEXT("The first reactive line starts at the limit"), Queue->Advance(3.f, true), FName(TEXT("parry")));
	Queue->Advance(2.9f, true);
	TestEqual(TEXT("The second, 5.9 s waiting, is dropped (past 3 s)"), Queue->GetDroppedCount(), 1);
	TestEqual(TEXT("...the foam line was the one"), Queue->GetLastDropped(), FName(TEXT("foam_used")));
	TestEqual(TEXT("The story lines are never dropped"), Queue->GetPendingLines(), TArray<FName>({ TEXT("bark"), TEXT("reply") }));

	// A story line still waits however late it is.
	UDialogueQueue* Story = NewObject<UDialogueQueue>();
	Story->SetLines(Lines);
	Story->PlayLine(TEXT("arrive"));
	Story->Advance(0.f, false);
	Story->PlayLine(TEXT("masks"));
	Story->Advance(2.9f, false);
	TestEqual(TEXT("Not dropped"), Story->GetDroppedCount(), 0);
	TestEqual(TEXT("It speaks after the first"), Story->Advance(0.2f, false), FName(TEXT("masks")));
	return true;
}

#endif
