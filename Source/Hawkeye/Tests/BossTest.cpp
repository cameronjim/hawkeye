#include "Boss/BossArenaHazard.h"
#include "Boss/BossDefinition.h"
#include "Boss/BossFightSubsystem.h"
#include "Boss/BossRules.h"
#include "Combat/BossPhaseComponent.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "Mission/CampaignOrder.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Boss.*: the boss fight's rules (claude-docs/gameplay-semantics.md, "Bosses"), pure; the phase component's
 * crew entry and checkpoint restore; the campaign order; and UBossFightSubsystem running a three-phase boss built in
 * code (a crew, the boss, the boss with the hazard loose) to the chapter's end in a test world.
 */
namespace HawkeyeBossTest
{
	static FBossPhase Phase(const TCHAR* Name, float Threshold, EBossPhaseEntry Entry, bool bBossInvulnerable = false,
		float TransitionSeconds = 0.f)
	{
		FBossPhase Out;
		Out.PhaseName = FName(Name);
		Out.BehaviorTag = FName(Name);
		Out.HealthThresholdPercent = Threshold;
		Out.Entry = Entry;
		Out.bBossInvulnerable = bBossInvulnerable;
		Out.bInvulnerableDuringTransition = TransitionSeconds > 0.f;
		Out.TransitionSeconds = TransitionSeconds;
		return Out;
	}

	/** Chapter 4's shape: his guys (he is out of it), him (opened by the crew going down), him and the hazard under 0.4. */
	static TArray<FBossPhase> TomasPhases()
	{
		return { Phase(TEXT("Crew"), 1.f, EBossPhaseEntry::HealthBelow, true), Phase(TEXT("Tomas"), 1.f, EBossPhaseEntry::CrewDown),
			Phase(TEXT("Santa"), 0.4f, EBossPhaseEntry::HealthBelow) };
	}

	static UMissionDefinition* MakeMission(const TArray<FName>& Ids)
	{
		UMissionDefinition* Mission = NewObject<UMissionDefinition>();
		for (const FName Id : Ids)
		{
			UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
			Objective->ObjectiveId = Id;
			Mission->Objectives.Add(Objective);
		}
		return Mission;
	}

	/** A flat 20 x 20 m roof at 10 m, two crew, the boss, a hazard lane across it. */
	static UBossDefinition* MakeBoss()
	{
		UBossDefinition* Def = NewObject<UBossDefinition>();
		Def->BossName = FText::FromString(TEXT("Test boss"));
		Def->MaxHealth = 300.f;
		Def->MeetObjective = TEXT("meet");
		Def->Moves.WindedAfterSwings = 3;
		Def->Moves.WindedSeconds = 2.f;
		FBossArena& Arena = Def->Arena;
		Arena.RoofId = TEXT("test_roof");
		Arena.Outline = { FVector2D(-1000.f, -1000.f), FVector2D(1000.f, -1000.f), FVector2D(1000.f, 1000.f), FVector2D(-1000.f, 1000.f) };
		Arena.RoofZ = 1000.f;
		Arena.Centre = FVector(0.f, 0.f, 1000.f);
		Arena.BossSpot = FVector(600.f, 0.f, 1000.f);
		Arena.HazardStart = FVector(-800.f, 700.f, 1000.f);
		Arena.HazardEnd = FVector(800.f, 700.f, 1000.f);
		const TArray<FBossPhase> Rules = TomasPhases();
		const TCHAR* Ids[] = { TEXT("beat_crew"), TEXT("beat_boss"), TEXT("survive") };
		for (int32 Index = 0; Index < Rules.Num(); ++Index)
		{
			FBossPhaseDefinition Phase;
			Phase.Phase = Rules[Index];
			Phase.ObjectiveId = Ids[Index];
			Phase.bBossFights = Index > 0;
			Phase.bHazardActive = Index == 2;
			Def->Phases.Add(Phase);
		}
		FBossCrewMember Member;
		Member.Weapon = EThugWeapon::Fists;
		Member.Location = FVector(-300.f, -300.f, 1000.f);
		Def->Phases[0].Crew.Add(Member);
		Member.Location = FVector(-300.f, 300.f, 1000.f);
		Def->Phases[0].Crew.Add(Member);
		return Def;
	}
}

// --- Pure rules ----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossRulesResolvePhase, "Hawkeye.Boss.Rules.PhaseThresholdsAndCrew",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossRulesResolvePhase::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossTest;
	const TArray<FBossPhase> P = TomasPhases();
	TestEqual(TEXT("Full health, crew up: the crew phase"), UBossRules::ResolvePhase(P, 0, 1.f, false, false), 0);
	TestEqual(TEXT("Health alone never skips the crew (he is out of it, but if a hit landed)"), UBossRules::ResolvePhase(P, 0, 0.1f, false, false), 0);
	TestEqual(TEXT("The crew down opens his own phase"), UBossRules::ResolvePhase(P, 0, 1.f, true, false), 1);
	TestEqual(TEXT("On the threshold is not under it"), UBossRules::ResolvePhase(P, 1, 0.4f, false, false), 1);
	TestEqual(TEXT("Strictly under 0.4: the hazard phase"), UBossRules::ResolvePhase(P, 1, 0.39f, false, false), 2);
	TestEqual(TEXT("The killing blow never advances"), UBossRules::ResolvePhase(P, 1, 0.f, false, true), 1);
	TestEqual(TEXT("A crew down with him already low goes on through"), UBossRules::ResolvePhase(P, 0, 0.3f, true, false), 2);
	TestEqual(TEXT("Another phase's crew flag opens nothing later"), UBossRules::ResolvePhase(
		{ Phase(TEXT("A"), 1.f, EBossPhaseEntry::HealthBelow), Phase(TEXT("B"), 0.5f, EBossPhaseEntry::HealthBelow),
			Phase(TEXT("C"), 0.5f, EBossPhaseEntry::CrewDown) }, 0, 0.2f, true, false), 1);
	TestTrue(TEXT("Thresholds that never rise are in order"), UBossRules::IsPhaseOrderValid(P));
	TestFalse(TEXT("A rising threshold is not"), UBossRules::IsPhaseOrderValid({ Phase(TEXT("A"), 0.5f, EBossPhaseEntry::HealthBelow),
		Phase(TEXT("B"), 0.8f, EBossPhaseEntry::HealthBelow) }));
	TestFalse(TEXT("Not damageable in a phase that holds him out"), UBossRules::IsBossDamageable(true, false, false));
	TestFalse(TEXT("Not damageable in a protected transition"), UBossRules::IsBossDamageable(false, true, true));
	TestTrue(TEXT("Damageable in an unprotected one"), UBossRules::IsBossDamageable(false, true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossRulesWinded, "Hawkeye.Boss.Rules.WindedWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossRulesWinded::RunTest(const FString& Parameters)
{
	FBossMoveSet Moves;
	Moves.WindedAfterSwings = 3;
	Moves.WindedSeconds = 3.f;
	Moves.WindedDamageScale = 1.5f;
	FBossComboClock Clock;
	Clock.Reset(5);
	TestFalse(TEXT("Two swings in: not yet"), Clock.Step(0.1f, 7, false, Moves));
	TestFalse(TEXT("The third swing still going: not until it is over"), Clock.Step(0.1f, 8, true, Moves));
	TestTrue(TEXT("The third swing over: winded"), Clock.Step(0.1f, 8, false, Moves));
	TestTrue(TEXT("Winded"), Clock.IsWinded());
	TestEqual(TEXT("Once"), Clock.WindedCount, 1);
	TestFalse(TEXT("Winded does not start again while it runs"), Clock.Step(1.f, 8, false, Moves));
	Clock.Step(2.1f, 8, false, Moves);
	TestFalse(TEXT("Over after WindedSeconds"), Clock.IsWinded());
	TestFalse(TEXT("A fresh combo after it: one swing is not three"), Clock.Step(0.1f, 9, false, Moves));
	Moves.WindedAfterSwings = 0;
	FBossComboClock Never;
	TestFalse(TEXT("WindedAfterSwings 0: never winded"), Never.Step(0.1f, 50, false, Moves));

	TestEqual(TEXT("Winded: a 20 hit does 10 more at 1.5"), UBossRules::WindedBonusDamage(20.f, true, 1.5f), 10.f);
	TestEqual(TEXT("Not winded: nothing more"), UBossRules::WindedBonusDamage(20.f, false, 1.5f), 0.f);
	TestEqual(TEXT("A finisher on a boss: 0.15 of 400"), UBossRules::FinisherDamage(300.f, 400.f, 0.15f), 60.f);
	TestEqual(TEXT("...all he has when that is less"), UBossRules::FinisherDamage(20.f, 400.f, 0.15f), 20.f);
	TestEqual(TEXT("A thug's finisher (fraction 0): everything"), UBossRules::FinisherDamage(80.f, 100.f, 0.f), 80.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossRulesHazard, "Hawkeye.Boss.Rules.HazardCycleAndHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossRulesHazard::RunTest(const FString& Parameters)
{
	FBossHazardTuning T;
	T.FirstDelaySeconds = 1.f;
	T.TelegraphSeconds = 1.5f;
	T.SweepSeconds = 2.f;
	T.RestSeconds = 3.f;
	T.HitCooldownSeconds = 2.f;
	T.HalfWidth = 160.f;
	T.HalfLength = 150.f;
	TestEqual(TEXT("Waiting first"), UBossRules::HazardStateAt(0.5f, T).Phase, EBossHazardPhase::Waiting);
	const FBossHazardState Warn = UBossRules::HazardStateAt(1.2f, T);
	TestEqual(TEXT("Then the telegraph"), Warn.Phase, EBossHazardPhase::Telegraph);
	TestEqual(TEXT("...at the start"), Warn.Along, 0.f);
	const FBossHazardState Mid = UBossRules::HazardStateAt(1.f + 1.5f + 1.f, T);
	TestEqual(TEXT("Then the sweep"), Mid.Phase, EBossHazardPhase::Sweep);
	TestTrue(TEXT("...half way along"), FMath::IsNearlyEqual(Mid.Along, 0.5f, 0.01f));
	const FBossHazardState Rest = UBossRules::HazardStateAt(1.f + 1.5f + 2.f + 1.f, T);
	TestEqual(TEXT("Then rest at the far end"), Rest.Phase, EBossHazardPhase::Rest);
	TestEqual(TEXT("...at the end"), Rest.Along, 1.f);
	const FBossHazardState Back = UBossRules::HazardStateAt(1.f + 6.5f + 0.5f, T);
	TestEqual(TEXT("Every sweep is telegraphed again"), Back.Phase, EBossHazardPhase::Telegraph);
	TestEqual(TEXT("...from where it stopped"), Back.Along, 1.f);
	const FBossHazardState Return = UBossRules::HazardStateAt(1.f + 6.5f + 1.5f + 1.f, T);
	TestTrue(TEXT("The second sweep comes back"), Return.Phase == EBossHazardPhase::Sweep && FMath::IsNearlyEqual(Return.Along, 0.5f, 0.01f));
	// Every sweep has a full telegraph in front of it: sampled every 50 ms, a sweep frame always follows one.
	float Telegraphed = 0.f;
	bool bFair = true;
	for (float S = 0.f; S < 60.f; S += 0.05f)
	{
		const FBossHazardState At = UBossRules::HazardStateAt(S, T);
		Telegraphed = At.Phase == EBossHazardPhase::Telegraph ? Telegraphed + 0.05f : (At.Phase == EBossHazardPhase::Sweep ? Telegraphed : 0.f);
		bFair &= At.Phase != EBossHazardPhase::Sweep || Telegraphed >= T.TelegraphSeconds - 0.06f;
	}
	TestTrue(TEXT("No sweep without its whole telegraph first"), bFair);

	TestTrue(TEXT("In the lane while it sweeps: a hit"), UBossRules::HazardHits(EBossHazardPhase::Sweep, 50.f, 20.f, 10.f, T));
	TestFalse(TEXT("Not while it telegraphs"), UBossRules::HazardHits(EBossHazardPhase::Telegraph, 0.f, 0.f, 10.f, T));
	TestFalse(TEXT("Not at rest"), UBossRules::HazardHits(EBossHazardPhase::Rest, 0.f, 0.f, 10.f, T));
	TestFalse(TEXT("Not out of the lane"), UBossRules::HazardHits(EBossHazardPhase::Sweep, 170.f, 0.f, 10.f, T));
	TestFalse(TEXT("Not ahead of its body"), UBossRules::HazardHits(EBossHazardPhase::Sweep, 0.f, 200.f, 10.f, T));
	TestFalse(TEXT("Not twice inside the cooldown"), UBossRules::HazardHits(EBossHazardPhase::Sweep, 0.f, 0.f, 1.f, T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossRulesCheckpoint, "Hawkeye.Boss.Rules.CheckpointFromObjectives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossRulesCheckpoint::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossTest;
	const TArray<FName> Ids = { TEXT("beat_crew"), TEXT("beat_tomas"), TEXT("survive_santa") };
	const FName Meet(TEXT("meet_tomas"));
	TestEqual(TEXT("Before the meet: the approach"), UBossRules::CheckpointPhase(Meet, Ids, {}), -1);
	TestEqual(TEXT("Met: the crew phase"), UBossRules::CheckpointPhase(Meet, Ids, { Meet }), 0);
	TestEqual(TEXT("Crew beaten: his phase"), UBossRules::CheckpointPhase(Meet, Ids, { Meet, TEXT("beat_crew") }), 1);
	TestEqual(TEXT("Him beaten: the hazard phase"), UBossRules::CheckpointPhase(Meet, Ids, { Meet, TEXT("beat_crew"), TEXT("beat_tomas") }), 2);
	TestEqual(TEXT("All done: past the end"), UBossRules::CheckpointPhase(Meet, Ids, { Meet, TEXT("beat_crew"), TEXT("beat_tomas"),
		TEXT("survive_santa") }), 3);
	TestEqual(TEXT("A later objective done puts everything before it behind her"),
		UBossRules::CheckpointPhase(Meet, Ids, { Meet, TEXT("beat_tomas") }), 2);
	const TArray<FBossPhase> P = TomasPhases();
	TestEqual(TEXT("Crew phase: full health"), UBossRules::CheckpointHealthFraction(P, 0), 1.f);
	TestEqual(TEXT("His phase (the crew opened it): full health"), UBossRules::CheckpointHealthFraction(P, 1), 1.f);
	TestEqual(TEXT("The hazard phase: on its threshold"), UBossRules::CheckpointHealthFraction(P, 2), 0.4f);
	TestEqual(TEXT("...and from there the next phase is not open"), UBossRules::ResolvePhase(P, 2, 0.4f, false, false), 2);

	TestTrue(TEXT("A bark speaks the first time"), UBossRules::CanBark(true, 0, 0.f, 10.f));
	TestFalse(TEXT("A once-a-fight bark never again"), UBossRules::CanBark(true, 1, 100.f, 10.f));
	TestFalse(TEXT("A repeating one waits its cooldown"), UBossRules::CanBark(false, 1, 5.f, 10.f));
	TestTrue(TEXT("...then speaks"), UBossRules::CanBark(false, 1, 11.f, 10.f));
	TestFalse(TEXT("Intro: not over in its first second"), UBossRules::IsIntroOver(0.5f, false, false, 40.f));
	TestTrue(TEXT("Intro: over when the lines are"), UBossRules::IsIntroOver(1.5f, false, false, 40.f));
	TestTrue(TEXT("Intro: over when she hits someone"), UBossRules::IsIntroOver(0.1f, true, true, 40.f));
	TestTrue(TEXT("Intro: over at its longest"), UBossRules::IsIntroOver(40.f, true, false, 40.f));
	const TArray<FVector2D> Square = { FVector2D(0.f, 0.f), FVector2D(100.f, 0.f), FVector2D(100.f, 100.f), FVector2D(0.f, 100.f) };
	TestTrue(TEXT("On the roof"), UBossRules::IsOnRoof(Square, 1000.f, FVector(50.f, 50.f, 1000.f)));
	TestFalse(TEXT("Under it (the street)"), UBossRules::IsOnRoof(Square, 1000.f, FVector(50.f, 50.f, 0.f)));
	TestFalse(TEXT("Beside it"), UBossRules::IsOnRoof(Square, 1000.f, FVector(150.f, 50.f, 1000.f)));
	return true;
}

// --- The phase component -------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossPhaseCrewEntry, "Hawkeye.BossPhase.CrewOpensThePhaseAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossPhaseCrewEntry::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossTest;
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(100.f, true);
	UBossPhaseComponent* Boss = NewObject<UBossPhaseComponent>();
	Boss->Phases = TomasPhases();
	Boss->Bind(Health);
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Boss->OnPhaseChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandlePhaseChanged);

	TestTrue(TEXT("Phase 0 holds him out of the fight"), Health->IsInvulnerable());
	Health->ApplyDamage(50.f);
	TestEqual(TEXT("...so a hit does nothing"), Health->GetCurrentHealth(), 100.f);
	Boss->NotifyCrewDown();
	TestEqual(TEXT("The crew down opens his phase"), Boss->GetCurrentPhaseIndex(), 1);
	TestEqual(TEXT("One broadcast"), Listener->PhaseChangedCount, 1);
	TestFalse(TEXT("His phase lets damage in (the transition is over: no world)"), Health->IsInvulnerable());
	Boss->NotifyCrewDown();
	TestEqual(TEXT("A second crew-down in his phase opens nothing"), Boss->GetCurrentPhaseIndex(), 1);
	Health->ApplyDamage(61.f);
	TestEqual(TEXT("Under 0.4: the hazard phase"), Boss->GetCurrentPhaseIndex(), 2);

	// A checkpoint at phase 0 puts his invulnerability back; at 2 takes it away; neither broadcasts.
	Boss->RestorePhase(0);
	TestEqual(TEXT("Restored to 0"), Boss->GetCurrentPhaseIndex(), 0);
	TestTrue(TEXT("...out of the fight again"), Health->IsInvulnerable());
	Boss->RestorePhase(2);
	TestFalse(TEXT("Restored to 2: in it"), Health->IsInvulnerable());
	TestEqual(TEXT("Restores broadcast nothing"), Listener->PhaseChangedCount, 2);

	// Scripted invulnerability (set before Bind) outlives a crew phase's.
	UHealthComponent* Scripted = NewObject<UHealthComponent>();
	Scripted->SetMaxHealth(100.f, true);
	Scripted->SetInvulnerable(true);
	UBossPhaseComponent* Other = NewObject<UBossPhaseComponent>();
	Other->Phases = TomasPhases();
	Other->Bind(Scripted);
	Other->NotifyCrewDown();
	TestTrue(TEXT("Scripted invulnerability survives leaving a crew phase"), Scripted->IsInvulnerable());
	return true;
}

// --- The campaign order --------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCampaignOrder, "Hawkeye.Campaign.OrderAndOpenChapters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCampaignOrder::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossTest;
	UMissionDefinition* One = MakeMission({ TEXT("a") });
	UMissionDefinition* Four = MakeMission({ TEXT("b") });
	One->NextChapter = Four;
	const TArray<UMissionDefinition*> Order = UHawkeyeCampaignOrder::Build(One);
	TestEqual(TEXT("Two chapters in order"), Order.Num(), 2);
	TestTrue(TEXT("Chapter 1 then 4"), Order.Num() == 2 && Order[0] == One && Order[1] == Four);
	Four->NextChapter = One;
	TestEqual(TEXT("A loop is cut"), UHawkeyeCampaignOrder::Build(One).Num(), 2);

	TestTrue(TEXT("Nothing running: the first is open"), UHawkeyeCampaignOrder::IsOpen(0, INDEX_NONE, false, 2));
	TestFalse(TEXT("...the second is not"), UHawkeyeCampaignOrder::IsOpen(1, INDEX_NONE, false, 2));
	TestFalse(TEXT("Chapter 1 running: 4 is shut"), UHawkeyeCampaignOrder::IsOpen(1, 0, false, 2));
	TestTrue(TEXT("Chapter 1 complete: 4 opens"), UHawkeyeCampaignOrder::IsOpen(1, 0, true, 2));
	TestTrue(TEXT("...and may be started"), UHawkeyeCampaignOrder::CanStart(1, 0, true, 2));
	TestFalse(TEXT("The chapter in progress is not started again"), UHawkeyeCampaignOrder::CanStart(0, 0, false, 2));
	TestTrue(TEXT("An earlier chapter may be replayed"), UHawkeyeCampaignOrder::CanStart(0, 1, false, 2));
	return true;
}

// --- The fight in a world ------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossWorldChapterCompletes, "Hawkeye.Boss.World.ChapterCompletes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossWorldChapterCompletes::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UBossFightSubsystem* Fight = World ? World->GetSubsystem<UBossFightSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("boss fight"), Fight))
	{
		return false;
	}
	ACharacter* Kate = Cast<ACharacter>(TestWorld.SpawnActor(ACharacter::StaticClass(), FVector(0.f, -3000.f, 90.f), FRotator::ZeroRotator));
	UHealthComponent* KateHealth = NewObject<UHealthComponent>(Kate, TEXT("Health"));
	KateHealth->RegisterComponent();
	Fight->PlayerOverride = Kate;
	Fight->bSkipLines = true;
	UMissionDefinition* Mission = MakeMission({ TEXT("meet"), TEXT("beat_crew"), TEXT("beat_boss"), TEXT("survive") });
	UBossDefinition* Def = MakeBoss();
	Missions->StartMission(Mission);
	TestTrue(TEXT("The fight sets up"), Fight->SetUpFight(Def));
	TestEqual(TEXT("Waiting for her"), Fight->GetState(), EBossFightState::Approach);
	AThugCharacter* Boss = Fight->GetBoss();
	if (!TestNotNull(TEXT("A boss"), Boss))
	{
		return false;
	}
	TestEqual(TEXT("Two of his crew"), Fight->GetCrew().Num(), 2);
	TestTrue(TEXT("Tagged"), Boss->ActorHasTag(UBossFightSubsystem::BossTag));
	TestTrue(TEXT("He refuses a takedown"), Boss->bRefusesTakedown);
	TestNotNull(TEXT("He carries the phases"), Fight->GetPhaseComponent());
	TestNotNull(TEXT("The hazard is there, tethered"), Fight->GetHazard());
	TestFalse(TEXT("...not loose"), Fight->GetHazard() && Fight->GetHazard()->IsLoose());

	const float Dt = 1.f / 30.f;
	Fight->Tick(Dt);
	TestFalse(TEXT("On the street the meet waits"), Missions->GetTracker()->FindObjective(TEXT("meet"))->IsCompleted());
	Kate->SetActorLocation(FVector(0.f, -600.f, 1090.f));
	Fight->Tick(Dt);
	TestTrue(TEXT("On the roof: met"), Missions->GetTracker()->FindObjective(TEXT("meet"))->IsCompleted());
	Fight->Tick(Dt);
	TestEqual(TEXT("The fight is on"), Fight->GetState(), EBossFightState::Fight);
	TestEqual(TEXT("Phase 0, the crew"), Fight->GetPhaseIndex(), 0);
	UHealthComponent* BossHealth = Boss->GetHealthComponent();
	BossHealth->ApplyDamage(100.f, Kate);
	TestEqual(TEXT("He is out of the crew phase's fight"), BossHealth->GetCurrentHealth(), BossHealth->GetMaxHealth());
	for (AThugCharacter* Thug : Fight->GetCrew())
	{
		TestTrue(TEXT("The crew are alerted"), Thug->IsAlerted());
		Thug->GetHealthComponent()->ApplyDamage(9999.f, Kate);
	}
	Fight->Tick(Dt);
	TestTrue(TEXT("Crew down: beat_crew"), Missions->GetTracker()->FindObjective(TEXT("beat_crew"))->IsCompleted());
	TestEqual(TEXT("His phase"), Fight->GetPhaseIndex(), 1);
	TestTrue(TEXT("He fights now"), Boss->IsAlerted());

	BossHealth->ApplyDamage(BossHealth->GetMaxHealth() * 0.65f, Kate);
	TestTrue(TEXT("Under 0.4: beat_boss"), Missions->GetTracker()->FindObjective(TEXT("beat_boss"))->IsCompleted());
	TestEqual(TEXT("The hazard phase"), Fight->GetPhaseIndex(), 2);
	ABossArenaHazard* Hazard = Fight->GetHazard();
	TestTrue(TEXT("The hazard is loose"), Hazard && Hazard->IsLoose());
	TestFalse(TEXT("survive waits for him to go down"), Missions->GetTracker()->FindObjective(TEXT("survive"))->IsCompleted());

	// She stands in its lane through a whole cycle: it hits her only while sweeping, once a cooldown.
	if (Hazard)
	{
		Hazard->TargetOverride = Kate;
		Kate->SetActorLocation(FVector(0.f, 700.f, 1090.f));
		const float Before = KateHealth->GetCurrentHealth();
		const FBossHazardTuning& T = Hazard->GetTuning();
		const float Cycle = T.FirstDelaySeconds + T.TelegraphSeconds + T.SweepSeconds;
		bool bHitOutsideSweep = false;
		for (float S = 0.f; S < Cycle + 0.2f; S += Dt)
		{
			const int32 Hits = Hazard->GetHitCount();
			Hazard->AdvanceHazard(Dt);
			Kate->SetActorLocation(FVector(0.f, 700.f, 1090.f));
			bHitOutsideSweep |= Hazard->GetHitCount() > Hits && Hazard->GetState().Phase != EBossHazardPhase::Sweep;
		}
		TestEqual(TEXT("One hit in one sweep"), Hazard->GetHitCount(), 1);
		TestFalse(TEXT("Never outside a sweep"), bHitOutsideSweep);
		TestTrue(TEXT("...after a full telegraph"), Hazard->GetTelegraphBeforeLastHit() >= T.TelegraphSeconds - 0.1f);
		TestTrue(TEXT("It cost her health"), KateHealth->GetCurrentHealth() < Before);
	}

	BossHealth->ApplyDamage(9999.f, Kate);
	Fight->Tick(Dt);
	Fight->Tick(Dt);
	TestTrue(TEXT("He is down: survive completes"), Missions->GetTracker()->FindObjective(TEXT("survive"))->IsCompleted());
	TestTrue(TEXT("...and with it the chapter"), Missions->IsMissionComplete());
	TestEqual(TEXT("The fight is done"), Fight->GetState(), EBossFightState::Done);
	TestFalse(TEXT("The hazard has settled"), Hazard && Hazard->IsLoose());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossWorldCheckpoint, "Hawkeye.Boss.World.RestartsAtThePhaseCheckpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossWorldCheckpoint::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UBossFightSubsystem* Fight = World ? World->GetSubsystem<UBossFightSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("boss fight"), Fight))
	{
		return false;
	}
	ACharacter* Kate = Cast<ACharacter>(TestWorld.SpawnActor(ACharacter::StaticClass(), FVector(0.f, -600.f, 1090.f), FRotator::ZeroRotator));
	Fight->PlayerOverride = Kate;
	Fight->bSkipLines = true;
	Missions->StartMission(MakeMission({ TEXT("meet"), TEXT("beat_crew"), TEXT("beat_boss"), TEXT("survive") }));
	UBossDefinition* Def = MakeBoss();
	Fight->SetUpFight(Def);

	// The save had the meet and the crew done: a load restarts at his phase, full health, no crew, fighting.
	Missions->GetTracker()->RestoreCompletedObjectives({ TEXT("meet"), TEXT("beat_crew") });
	Fight->RestoreFromCampaign();
	TestEqual(TEXT("Fighting"), Fight->GetState(), EBossFightState::Fight);
	TestEqual(TEXT("At his phase"), Fight->GetPhaseIndex(), 1);
	TestEqual(TEXT("No crew"), Fight->GetCrew().Num(), 0);
	AThugCharacter* Boss = Fight->GetBoss();
	TestTrue(TEXT("Full health"), Boss && Boss->GetHealthComponent()->GetHealthPercent() > 0.999f);
	TestTrue(TEXT("Damageable"), Boss && !Boss->GetHealthComponent()->IsInvulnerable());

	// And with his phase done too: the hazard phase, on its threshold, the hazard loose.
	Missions->GetTracker()->RestoreCompletedObjectives({ TEXT("meet"), TEXT("beat_crew"), TEXT("beat_boss") });
	Fight->RestoreFromCampaign();
	Boss = Fight->GetBoss();
	TestEqual(TEXT("At the hazard phase"), Fight->GetPhaseIndex(), 2);
	TestTrue(TEXT("On its threshold"), Boss && FMath::IsNearlyEqual(Boss->GetHealthComponent()->GetHealthPercent(), 0.4f, 0.001f));
	TestTrue(TEXT("The hazard loose"), Fight->GetHazard() && Fight->GetHazard()->IsLoose());
	TestFalse(TEXT("The checkpoint completed nothing more"), Missions->GetTracker()->FindObjective(TEXT("survive"))->IsCompleted());

	// Before the meet: back to the approach, the crew on the roof again.
	Fight->RestartAtPhase(-1);
	TestEqual(TEXT("The approach"), Fight->GetState(), EBossFightState::Approach);
	TestEqual(TEXT("The crew back"), Fight->GetCrew().Num(), 2);
	return true;
}

#endif
