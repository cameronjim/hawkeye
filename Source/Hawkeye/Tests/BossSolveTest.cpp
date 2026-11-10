#include "Boss/BossArenaHazard.h"
#include "Boss/BossDefinition.h"
#include "Boss/BossFightSubsystem.h"
#include "Boss/BossRules.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/SmokeCloud.h"
#include "Combat/CombatReadability.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Boss.Rules.SolvesAndBeats and Hawkeye.Boss.World.*: the boss fight's second pass (claude-docs/gameplay-semantics.md,
 * "Bosses"): a finisher only in his winded window (never off a parry), the unblockable swing, his guard and what
 * breaks it, the hazard catching him and pushing props, the putty pin, the phase and defeat beats, and the lines a
 * load must not repeat. Pure rules first, then a three-phase boss built in code run in a test world.
 */
namespace HawkeyeBossSolveTest
{
	static FBossPhase Phase(const TCHAR* Name, float Threshold, EBossPhaseEntry Entry, bool bBossInvulnerable = false)
	{
		FBossPhase Out;
		Out.PhaseName = FName(Name);
		Out.BehaviorTag = FName(Name);
		Out.HealthThresholdPercent = Threshold;
		Out.Entry = Entry;
		Out.bBossInvulnerable = bBossInvulnerable;
		// No protected transition: a test world runs no timer to end one.
		Out.bInvulnerableDuringTransition = false;
		Out.TransitionSeconds = 0.f;
		return Out;
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

	static FHawkeyeMeleeAttack Attack(const TCHAR* Name, float Windup, bool bUnblockable)
	{
		FHawkeyeMeleeAttack Out;
		Out.Name = FName(Name);
		Out.Damage = 12.f;
		Out.WindupSeconds = Windup;
		Out.RecoverSeconds = 0.5f;
		Out.Range = 150.f;
		Out.bUnblockable = bUnblockable;
		return Out;
	}

	static FBossBeat Beat(float Seconds, FName Line)
	{
		FBossBeat Out;
		Out.Seconds = Seconds;
		Out.Lines = { Line };
		return Out;
	}

	/** Chapter 4's shape on a 20 x 20 m roof at 10 m: his crew, him (guarding), him with the hazard (guarding, catchable). */
	static UBossDefinition* MakeBoss()
	{
		UBossDefinition* Def = NewObject<UBossDefinition>();
		Def->BossName = FText::FromString(TEXT("Test boss"));
		Def->MaxHealth = 400.f;
		Def->MeetObjective = TEXT("meet");
		Def->Moves.WindedAfterSwings = 3;
		Def->Moves.WindedSeconds = 2.f;
		Def->Moves.Combo = { Attack(TEXT("jab"), 0.75f, false), Attack(TEXT("jab_2"), 0.6f, false), Attack(TEXT("haymaker"), 1.1f, true) };
		FBossArena& Arena = Def->Arena;
		Arena.RoofId = TEXT("test_roof");
		Arena.Outline = { FVector2D(-1000.f, -1000.f), FVector2D(1000.f, -1000.f), FVector2D(1000.f, 1000.f), FVector2D(-1000.f, 1000.f) };
		Arena.RoofZ = 1000.f;
		Arena.Centre = FVector(0.f, 0.f, 1000.f);
		Arena.BossSpot = FVector(600.f, 0.f, 1000.f);
		Arena.HazardStart = FVector(-800.f, 700.f, 1000.f);
		Arena.HazardEnd = FVector(800.f, 700.f, 1000.f);
		const TArray<FBossPhase> Rules = { Phase(TEXT("Crew"), 1.f, EBossPhaseEntry::HealthBelow, true),
			Phase(TEXT("Boss"), 1.f, EBossPhaseEntry::CrewDown), Phase(TEXT("Hazard"), 0.4f, EBossPhaseEntry::HealthBelow) };
		const TCHAR* Ids[] = { TEXT("beat_crew"), TEXT("beat_boss"), TEXT("survive") };
		for (int32 Index = 0; Index < Rules.Num(); ++Index)
		{
			FBossPhaseDefinition P;
			P.Phase = Rules[Index];
			P.ObjectiveId = Ids[Index];
			P.bBossFights = Index > 0;
			P.bHazardActive = Index == 2;
			P.StartLines = { FName(*FString::Printf(TEXT("start_%d"), Index)) };
			if (Index > 0)
			{
				P.GuardDamageFraction = 0.35f;
				P.GuardBreakEffects = { EArrowHitEffect::Smoke };
				P.TransitionBeat = Beat(2.f, FName(*FString::Printf(TEXT("beat_%d"), Index)));
			}
			P.bHazardWindsBoss = Index == 2;
			FBossBark Bark;
			Bark.Event = EBossBarkEvent::Winded;
			Bark.Lines = { FName(*FString::Printf(TEXT("winded_%d"), Index)) };
			P.Barks.Add(Bark);
			Def->Phases.Add(P);
		}
		Def->DefeatBeat = Beat(2.f, TEXT("down"));
		FBossCrewMember Member;
		Member.Weapon = EThugWeapon::Fists;
		Member.Location = FVector(-300.f, -300.f, 1000.f);
		Def->Phases[0].Crew.Add(Member);
		return Def;
	}

	/** Kate on the roof facing +X, 150 cm short of where he stands. */
	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, float BossZ)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(450.f, 0.f, BossZ), FRotator::ZeroRotator));
	}

	static void TickFor(UBossFightSubsystem* Fight, float Seconds)
	{
		for (float S = 0.f; S < Seconds; S += 1.f / 30.f)
		{
			Fight->Tick(1.f / 30.f);
		}
	}

	/** A smoke (confetti) arrow's effect landed at Where, shot by Shooter. */
	static void LandSmoke(const FHawkeyeTestWorld& TestWorld, const FVector& Where, AActor* Shooter)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>();
		Arrow->OnHitEffect = EArrowHitEffect::Smoke;
		ASmokeCloud* Cloud = Cast<ASmokeCloud>(TestWorld.SpawnActor(ASmokeCloud::StaticClass(), Where, FRotator::ZeroRotator));
		if (Cloud)
		{
			FHitResult Hit;
			Hit.ImpactPoint = Where;
			Hit.Location = Where;
			Hit.ImpactNormal = FVector::UpVector;
			Cloud->InitEffect(Arrow, Shooter, Hit);
			Cloud->Activate();
		}
	}
}

// --- Pure rules ----------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossRulesSolves, "Hawkeye.Boss.Rules.SolvesAndBeats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossRulesSolves::RunTest(const FString& Parameters)
{
	// The finisher's opening: a boss only in the window the fight grants; a thug on any of his openings.
	TestFalse(TEXT("Boss parry-staggered: no finisher"), UBossRules::IsFinisherOpen(false, true, 0.f, 0.f, true));
	TestFalse(TEXT("Boss after a combo ender: none"), UBossRules::IsFinisherOpen(false, false, 1.f, 0.f, true));
	TestFalse(TEXT("Boss knocked down (a bola): none"), UBossRules::IsFinisherOpen(true, false, 0.f, 0.f, true));
	TestTrue(TEXT("Boss winded (granted): open"), UBossRules::IsFinisherOpen(false, false, 0.f, 0.5f, true));
	TestTrue(TEXT("Thug parry-staggered: open (unchanged)"), UBossRules::IsFinisherOpen(false, true, 0.f, 0.f, false));
	TestTrue(TEXT("Thug after a combo ender: open"), UBossRules::IsFinisherOpen(false, false, 0.4f, 0.f, false));
	TestTrue(TEXT("Thug knocked down: open"), UBossRules::IsFinisherOpen(true, false, 0.f, 0.f, false));
	TestFalse(TEXT("Thug on nothing: shut"), UBossRules::IsFinisherOpen(false, false, 0.f, 0.f, false));

	// The guard.
	TestTrue(TEXT("Guarding: a guard, fighting, not winded"), UBossRules::IsGuarding(0.35f, true, false));
	TestFalse(TEXT("Not winded... winded drops it"), UBossRules::IsGuarding(0.35f, true, true));
	TestFalse(TEXT("No guard at a fraction of 1"), UBossRules::IsGuarding(1.f, true, false));
	TestFalse(TEXT("Not while he watches"), UBossRules::IsGuarding(0.35f, false, false));
	TestEqual(TEXT("Guarding: hits land at the fraction"), UBossRules::BossDamageScale(false, true, 0.35f), 0.35f);
	TestEqual(TEXT("Not guarding: whole"), UBossRules::BossDamageScale(false, false, 0.35f), 1.f);
	TestEqual(TEXT("In a beat: nothing lands"), UBossRules::BossDamageScale(true, false, 0.35f), 0.f);
	const TArray<EArrowHitEffect> Sources = { EArrowHitEffect::Smoke };
	TestTrue(TEXT("Confetti (smoke) on him while he guards: broken"), UBossRules::BreaksGuard(EArrowHitEffect::Smoke, 100.f, Sources, 300.f, true));
	TestFalse(TEXT("...not from too far"), UBossRules::BreaksGuard(EArrowHitEffect::Smoke, 400.f, Sources, 300.f, true));
	TestFalse(TEXT("...not an arrow the phase does not name"), UBossRules::BreaksGuard(EArrowHitEffect::Putty, 0.f, Sources, 300.f, true));
	TestFalse(TEXT("...not when he is not guarding"), UBossRules::BreaksGuard(EArrowHitEffect::Smoke, 0.f, Sources, 300.f, false));

	// The decisive finisher (the slow camera): 0.15 of 400 is 60, 90 winded.
	TestTrue(TEXT("Kills: decisive"), UBossRules::IsDecisiveFinisher(80.f, 400.f, 0.15f, 1.5f, -1.f));
	TestTrue(TEXT("Under the next threshold: decisive (180 - 90 = 90, 0.225 < 0.4)"), UBossRules::IsDecisiveFinisher(180.f, 400.f, 0.15f, 1.5f, 0.4f));
	TestFalse(TEXT("Leaves him over it: not (400 - 90 = 310)"), UBossRules::IsDecisiveFinisher(400.f, 400.f, 0.15f, 1.5f, 0.4f));
	TestFalse(TEXT("No next threshold and he lives: not"), UBossRules::IsDecisiveFinisher(200.f, 400.f, 0.15f, 1.5f, -1.f));

	// The beat's camera and clock.
	FBossBeat B;
	B.CameraOffset = FVector(0.f, 600.f, 0.f);
	B.LookAtBoss = 0.5f;
	B.FieldOfView = 60.f;
	FVector Location;
	FRotator Rotation;
	UBossRules::BeatCamera(FVector(0.f, 0.f, 0.f), FVector(400.f, 0.f, 0.f), B, Location, Rotation);
	TestTrue(TEXT("Camera: 600 to the right of their middle (both already in the shot)"), Location.Equals(FVector(200.f, 600.f, 0.f), 0.1f));
	const FVector Look = (FVector(200.f, 0.f, 40.f) - Location).GetSafeNormal();
	TestTrue(TEXT("...looking between them (chest height)"), Rotation.Vector().Equals(Look, 0.001f));
	UBossRules::BeatCamera(FVector(0.f, 0.f, 0.f), FVector(0.f, 400.f, 0.f), B, Location, Rotation);
	TestTrue(TEXT("The frame turns with the line between them"), Location.Equals(FVector(-600.f, 200.f, 0.f), 0.1f));
	FBossBeat Wide;
	Wide.CameraOffset = FVector(-250.f, 330.f, 140.f);
	Wide.FieldOfView = 55.f;
	UBossRules::BeatCamera(FVector(0.f, 0.f, 0.f), FVector(1200.f, 0.f, 0.f), Wide, Location, Rotation);
	const float Limit = FMath::DegreesToRadians(55.f * 0.5f * UBossRules::BeatFrameFraction) + 0.01f;
	const auto Off = [&](const FVector& Who) { return FMath::Acos(FVector::DotProduct(Rotation.Vector(), (Who + FVector(0.f, 0.f, 40.f) - Location).GetSafeNormal())); };
	TestTrue(TEXT("Twelve metres apart, the camera backs off until Kate is in the shot"), Off(FVector::ZeroVector) <= Limit);
	TestTrue(TEXT("...and him"), Off(FVector(1200.f, 0.f, 0.f)) <= Limit);
	TestTrue(TEXT("...further out than its offset"), FVector::Dist(Location, FVector(600.f, 0.f, 0.f)) > FVector(-250.f, 330.f, 140.f).Size());
	TestFalse(TEXT("Beat: running"), UBossRules::IsBeatOver(1.f, 2.f, false));
	TestTrue(TEXT("Beat: over at its length"), UBossRules::IsBeatOver(2.f, 2.f, false));
	TestFalse(TEXT("Beat: no skip in its first 0.4 s"), UBossRules::IsBeatOver(0.2f, 2.f, true));
	TestTrue(TEXT("Beat: skipped after it"), UBossRules::IsBeatOver(0.5f, 2.f, true));

	// Lines heard are not heard again.
	const TArray<FName> Unheard = UBossRules::UnheardLines({ TEXT("a"), TEXT("b"), TEXT("a"), NAME_None, TEXT("c") }, { TEXT("b") });
	TestTrue(TEXT("Only the lines not yet heard, once each, in order"), Unheard == TArray<FName>({ TEXT("a"), TEXT("c") }));

	// The hazard: the putty pin, the props it pushes.
	FBossHazardTuning T;
	T.HalfWidth = 170.f;
	T.HalfLength = 160.f;
	T.PinSeconds = 4.f;
	T.PinRadius = 250.f;
	TestTrue(TEXT("Putty on its body pins it"), UBossRules::PinsHazard(EArrowHitEffect::Putty, 100.f, 900.f, T));
	TestTrue(TEXT("...on its tether too"), UBossRules::PinsHazard(EArrowHitEffect::Putty, 900.f, 200.f, T));
	TestFalse(TEXT("...not from afar"), UBossRules::PinsHazard(EArrowHitEffect::Putty, 400.f, 400.f, T));
	TestFalse(TEXT("...not a smoke arrow"), UBossRules::PinsHazard(EArrowHitEffect::Smoke, 0.f, 0.f, T));
	T.PinSeconds = 0.f;
	TestFalse(TEXT("...not when the tuning never pins"), UBossRules::PinsHazard(EArrowHitEffect::Putty, 0.f, 0.f, T));
	TestEqual(TEXT("A present just ahead goes a body's reach ahead"), UBossRules::PushedAlong(550.f, 0.f, 30.f, 500.f, 1.f, 1400.f, T), 660.f);
	TestEqual(TEXT("...going back, the other way"), UBossRules::PushedAlong(450.f, 0.f, 30.f, 500.f, -1.f, 1400.f, T), 340.f);
	TestEqual(TEXT("One out of the lane stays"), UBossRules::PushedAlong(550.f, 250.f, 30.f, 500.f, 1.f, 1400.f, T), 550.f);
	TestEqual(TEXT("One well ahead stays (for now)"), UBossRules::PushedAlong(900.f, 0.f, 30.f, 500.f, 1.f, 1400.f, T), 900.f);
	TestEqual(TEXT("One behind it stays: never dragged back"), UBossRules::PushedAlong(300.f, 0.f, 30.f, 500.f, 1.f, 1400.f, T), 300.f);
	TestEqual(TEXT("Never more than a reach off the end"), UBossRules::PushedAlong(1390.f, 0.f, 30.f, 1380.f, 1.f, 1400.f, T), 1540.f);

	// Forced winded: once, counted, and not stacked.
	FBossComboClock Clock;
	Clock.Reset(4);
	TestTrue(TEXT("A broken guard winds him"), Clock.ForceWinded(5, 3.f));
	TestTrue(TEXT("...winded"), Clock.IsWinded() && Clock.WindedCount == 1);
	TestFalse(TEXT("...a second while winded changes nothing"), Clock.ForceWinded(5, 3.f));
	TestEqual(TEXT("...still once"), Clock.WindedCount, 1);
	return true;
}

// --- No parry-finisher on a boss -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossNoParryFinisher, "Hawkeye.Boss.World.NoParryFinisherOnABoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossNoParryFinisher::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossSolveTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UBossFightSubsystem* Fight = World ? World->GetSubsystem<UBossFightSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("boss fight"), Fight))
	{
		return false;
	}
	Fight->bSkipLines = true;
	Missions->StartMission(MakeMission({ TEXT("meet"), TEXT("beat_crew"), TEXT("beat_boss"), TEXT("survive") }));
	UBossDefinition* Def = MakeBoss();
	Def->Phases[1].TransitionBeat = FBossBeat();
	TestTrue(TEXT("Set up"), Fight->SetUpFight(Def));
	AThugCharacter* Boss = Fight->GetBoss();
	if (!TestNotNull(TEXT("A boss"), Boss))
	{
		return false;
	}
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, Boss->GetActorLocation().Z);
	if (!TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	Fight->PlayerOverride = Kate;
	Missions->GetTracker()->RestoreCompletedObjectives({ TEXT("meet"), TEXT("beat_crew") });
	Fight->RestoreFromCampaign();
	TestEqual(TEXT("His phase"), Fight->GetPhaseIndex(), 1);
	TestTrue(TEXT("The definition makes him need his window"), Boss->bFinisherNeedsGrantedWindow);
	UMeleeComponent* Melee = Boss->GetMeleeComponent();
	UFinisherComponent* Finisher = Kate->GetFinisherComponent();
	if (!TestNotNull(TEXT("His melee"), Melee) || !TestNotNull(TEXT("Her finisher"), Finisher))
	{
		return false;
	}

	// His haymaker is unblockable: no parry, its own telegraph, a dodge.
	Melee->StartAttack(Def->Moves.Combo[2]);
	float Elapsed = 0.f;
	float Windup = 0.f;
	TestEqual(TEXT("The haymaker is no parry's"), UHawkeyeMeleeRules::ClassifyParry(Boss), EHawkeyeParryKind::None);
	TestTrue(TEXT("...it is an unblockable wind-up"), UHawkeyeMeleeRules::IsUnblockableWindup(Boss));
	TestFalse(TEXT("...the parry's glyph is not for it"), HawkeyeCombatReadability::GetStrikeTelegraph(Boss, Kate->GetActorLocation(), 600.f, Elapsed, Windup));
	TestTrue(TEXT("...its own telegraph is"), HawkeyeCombatReadability::GetUnblockableTelegraph(Boss, Elapsed, Windup));
	TestEqual(TEXT("...for its whole wind-up"), Windup, 1.1f);
	TestFalse(TEXT("Kate cannot parry it"), Kate->TryParry());
	const FLinearColor Base(1.f, 0.85f, 0.05f, 1.f);
	TestTrue(TEXT("The glyph flashes white on the beat"), HawkeyeCombatReadability::ComputeUnblockableColor(Base, 0.01f, 1.f).Equals(FLinearColor::White));
	TestTrue(TEXT("...and is its colour between"), HawkeyeCombatReadability::ComputeUnblockableColor(Base, 0.1f, 1.f).Equals(Base));
	Melee->CancelAttack();

	// His jab is parryable: the parry lands, staggers him briefly, and opens no finisher.
	Melee->StartAttack(Def->Moves.Combo[0]);
	TestEqual(TEXT("The jab is a parryable swing"), UHawkeyeMeleeRules::ClassifyParry(Boss), EHawkeyeParryKind::Swing);
	TestTrue(TEXT("Kate parries it"), Kate->TryParry());
	TestTrue(TEXT("He is parry-staggered"), Boss->IsParryStaggered());
	TestTrue(TEXT("...briefly (at most the move set's 0.6 s, not her 1.5)"), Boss->GetStaggerRemaining() <= 0.61f);
	TestFalse(TEXT("A parry opens no finisher on a boss"), Boss->IsFinisherOpen());
	TestNull(TEXT("...so the finisher finds nobody"), Finisher->FindTarget());
	TestFalse(TEXT("...and F does nothing"), Kate->TryFinisher());
	Boss->OpenFinisherWindow(1.f);
	TestFalse(TEXT("A combo ender opens none either"), Boss->IsFinisherOpen());
	Boss->CloseFinisherWindow();
	Melee->CancelAttack();

	// Winded: the one window, and then a finisher may take him.
	Boss->GrantFinisherWindow(2.f);
	TestTrue(TEXT("Winded (granted): open"), Boss->IsFinisherOpen());
	Boss->CloseFinisherWindow();

	// An ordinary thug is unchanged: his parry opens the finisher.
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(-2000.f, 0.f, 90.f), FRotator(0.f, 180.f, 0.f)));
	if (TestNotNull(TEXT("A thug"), Thug))
	{
		Thug->Parried(Kate, 1.5f);
		TestTrue(TEXT("A thug's parry still opens his finisher"), Thug->IsFinisherOpen());
		TestEqual(TEXT("...for her full 1.5 s"), Thug->GetStaggerRemaining(), 1.5f, 0.01f);
	}
	return true;
}

// --- Guard, beats, hazard, lines -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossGuardAndBeats, "Hawkeye.Boss.World.GuardBeatsAndHazard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossGuardAndBeats::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossSolveTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UBossFightSubsystem* Fight = World ? World->GetSubsystem<UBossFightSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("boss fight"), Fight))
	{
		return false;
	}
	Fight->bSkipLines = true;
	Missions->StartMission(MakeMission({ TEXT("meet"), TEXT("beat_crew"), TEXT("beat_boss"), TEXT("survive") }));
	UBossDefinition* Def = MakeBoss();
	Fight->SetUpFight(Def);
	AThugCharacter* Boss = Fight->GetBoss();
	AHawkeyeCharacter* Kate = Boss ? SpawnKate(TestWorld, Boss->GetActorLocation().Z) : nullptr;
	if (!TestNotNull(TEXT("A boss"), Boss) || !TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	Fight->PlayerOverride = Kate;
	Fight->Tick(1.f / 30.f);
	Fight->Tick(1.f / 30.f);
	TestEqual(TEXT("Met: fighting"), Fight->GetState(), EBossFightState::Fight);

	// The crew down: his phase opens with its beat. She is held, nobody is hurt, then control comes back.
	for (AThugCharacter* Thug : Fight->GetCrew())
	{
		Thug->GetHealthComponent()->ApplyDamage(9999.f, Kate);
	}
	Fight->Tick(1.f / 30.f);
	TestEqual(TEXT("His phase"), Fight->GetPhaseIndex(), 1);
	TestTrue(TEXT("Its beat plays"), Fight->IsInBeat());
	TestTrue(TEXT("Kate is held"), Kate->IsScriptedLocked() && Kate->IsLockedOutByTakedown());
	UHealthComponent* BossHealth = Boss->GetHealthComponent();
	UHealthComponent* KateHealth = Kate->GetHealthComponent();
	BossHealth->ApplyDamage(100.f, Kate);
	KateHealth->ApplyDamage(30.f, Boss);
	TestEqual(TEXT("Nothing lands on him in a beat"), BossHealth->GetCurrentHealth(), BossHealth->GetMaxHealth());
	TestEqual(TEXT("...nor on her"), KateHealth->GetCurrentHealth(), KateHealth->GetMaxHealth());
	TestTrue(TEXT("The beat's line is spoken"), Fight->GetLinesSpoken().Contains(FName(TEXT("beat_1"))));
	TestFalse(TEXT("The camera has an angle"), Fight->GetBeatCameraLocation().IsZero());
	TickFor(Fight, 2.1f);
	TestFalse(TEXT("Over after its 2 s"), Fight->IsInBeat());
	TestFalse(TEXT("Kate is let go"), Kate->IsScriptedLocked());

	// His guard: a 100 hit lands at 0.35.
	TestTrue(TEXT("He guards"), Fight->IsGuarding());
	BossHealth->ApplyDamage(100.f, Kate);
	TestEqual(TEXT("Guarded: 35 of 100"), BossHealth->GetCurrentHealth(), BossHealth->GetMaxHealth() - 35.f, 0.1f);
	KateHealth->ApplyDamage(10.f, Boss);
	TestEqual(TEXT("Kate takes hits again"), KateHealth->GetCurrentHealth(), KateHealth->GetMaxHealth() - 10.f, 0.1f);

	// The confetti (a smoke effect) on him breaks it: winded, the window open, hits whole and then some.
	LandSmoke(TestWorld, Boss->GetActorLocation(), Kate);
	Fight->Tick(1.f / 30.f);
	TestEqual(TEXT("Guard broken once"), Fight->GetGuardBreakCount(), 1);
	TestTrue(TEXT("Winded"), Fight->IsWinded());
	TestFalse(TEXT("No guard while winded"), Fight->IsGuarding());
	TestTrue(TEXT("The finisher window is open"), Boss->IsFinisherOpen());
	const float Before = BossHealth->GetCurrentHealth();
	BossHealth->ApplyDamage(20.f, Kate);
	TestEqual(TEXT("Winded: 20 does 30"), Before - BossHealth->GetCurrentHealth(), 30.f, 0.1f);
	LandSmoke(TestWorld, Boss->GetActorLocation(), Kate);
	Fight->Tick(1.f / 30.f);
	TestEqual(TEXT("A second cloud while winded breaks nothing more"), Fight->GetGuardBreakCount(), 1);
	TickFor(Fight, 2.2f);
	TestFalse(TEXT("The window closes"), Fight->IsWinded() || Boss->IsFinisherOpen());

	// Under 0.4: the hazard phase and its beat, skipped.
	BossHealth->IncomingDamageScale = 1.f;
	BossHealth->ApplyDamage(BossHealth->GetCurrentHealth() - BossHealth->GetMaxHealth() * 0.35f, Kate);
	TestEqual(TEXT("The hazard phase"), Fight->GetPhaseIndex(), 2);
	TestTrue(TEXT("Its beat"), Fight->IsInBeat());
	ABossArenaHazard* Hazard = Fight->GetHazard();
	TestTrue(TEXT("The hazard holds through it"), Hazard && Hazard->IsHeld());
	Fight->SkipBeat();
	Fight->Tick(1.f / 30.f);
	TestTrue(TEXT("No skip in its first 0.4 s"), Fight->IsInBeat());
	TickFor(Fight, 0.4f);
	Fight->SkipBeat();
	Fight->Tick(1.f / 30.f);
	TestFalse(TEXT("Skipped after it"), Fight->IsInBeat());
	TestEqual(TEXT("Two beats, one skipped"), Fight->GetBeatsPlayed() * 10 + Fight->GetBeatsSkipped(), 21);
	TestFalse(TEXT("The hazard runs again"), Hazard && Hazard->IsHeld());

	// The hazard's sweep catches him in its lane: winded, shoved, counted.
	if (Hazard)
	{
		const FBossHazardTuning& T = Hazard->GetTuning();
		Hazard->TargetOverride = nullptr;
		for (float S = 0.f; S < T.FirstDelaySeconds + T.TelegraphSeconds + T.SweepSeconds * 0.5f; S += 1.f / 30.f)
		{
			Hazard->AdvanceHazard(1.f / 30.f);
		}
		TestEqual(TEXT("Mid sweep"), Hazard->GetState().Phase, EBossHazardPhase::Sweep);
		Boss->SetActorLocation(Hazard->GetBodyLocation() + FVector(0.f, 30.f, 100.f));
		Fight->Tick(1.f / 30.f);
		TestEqual(TEXT("It caught him"), Fight->GetHazardBossHits(), 1);
		TestTrue(TEXT("...winded"), Fight->IsWinded());

		// A prop in its lane is pushed ahead, never passed through.
		AActor* Present = TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FMath::Lerp(Hazard->GetLaneStart(), Hazard->GetLaneEnd(), 0.9f),
			FRotator::ZeroRotator);
		Hazard->AddPushable(Present, 30.f);
		const FVector PresentStart = Present ? Present->GetActorLocation() : FVector::ZeroVector;
		for (float S = 0.f; S < T.SweepSeconds; S += 1.f / 30.f)
		{
			Hazard->AdvanceHazard(1.f / 30.f);
		}
		TestTrue(TEXT("The present was pushed"), Hazard->GetPushCount() > 0);
		TestTrue(TEXT("...on along the lane"), Present && FVector::Dist2D(Present->GetActorLocation(), PresentStart) > 50.f);

		// The putty pin: the clock stops for its seconds.
		const float Clock = Hazard->GetSeconds();
		Hazard->Pin(4.f);
		Hazard->AdvanceHazard(1.f);
		TestEqual(TEXT("Pinned: its clock stands"), Hazard->GetSeconds(), Clock);
		Hazard->AdvanceHazard(3.1f);
		Hazard->AdvanceHazard(0.5f);
		TestTrue(TEXT("...then runs on"), Hazard->GetSeconds() > Clock);
	}

	// Down: the defeat beat, then the after lines, then done.
	TickFor(Fight, 2.2f);
	BossHealth->IncomingDamageScale = 1.f;
	BossHealth->ApplyDamage(9999.f, Kate);
	TestEqual(TEXT("Defeated"), Fight->GetState(), EBossFightState::Defeated);
	TestTrue(TEXT("The defeat beat"), Fight->IsInBeat());
	TestTrue(TEXT("...its line"), Fight->GetLinesSpoken().Contains(FName(TEXT("down"))));
	TickFor(Fight, 2.2f);
	TestEqual(TEXT("Then the end"), Fight->GetState(), EBossFightState::Done);
	TestFalse(TEXT("Kate free"), Kate->IsScriptedLocked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBossHeardLines, "Hawkeye.Boss.World.LoadRepeatsNoLineAndNoBeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBossHeardLines::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBossSolveTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UBossFightSubsystem* Fight = World ? World->GetSubsystem<UBossFightSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("boss fight"), Fight))
	{
		return false;
	}
	Fight->bSkipLines = true;
	Missions->StartMission(MakeMission({ TEXT("meet"), TEXT("beat_crew"), TEXT("beat_boss"), TEXT("survive") }));
	UBossDefinition* Def = MakeBoss();
	Fight->SetUpFight(Def);
	AThugCharacter* Boss = Fight->GetBoss();
	AHawkeyeCharacter* Kate = Boss ? SpawnKate(TestWorld, Boss->GetActorLocation().Z) : nullptr;
	if (!TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	Fight->PlayerOverride = Kate;
	Fight->Tick(1.f / 30.f);
	Fight->Tick(1.f / 30.f);
	for (AThugCharacter* Thug : Fight->GetCrew())
	{
		Thug->GetHealthComponent()->ApplyDamage(9999.f, Kate);
	}
	Fight->Tick(1.f / 30.f);
	TickFor(Fight, 2.1f);
	// A broken guard: winded, and its bark.
	LandSmoke(TestWorld, Fight->GetBoss()->GetActorLocation(), Kate);
	Fight->Tick(1.f / 30.f);
	const TArray<FName> Spoken = Fight->GetLinesSpoken();
	TestTrue(TEXT("His phase's start line spoke"), Spoken.Contains(FName(TEXT("start_1"))));
	TestTrue(TEXT("...its beat's"), Spoken.Contains(FName(TEXT("beat_1"))));
	TestTrue(TEXT("...the winded bark"), Spoken.Contains(FName(TEXT("winded_1"))));
	const TArray<FString> Heard = Fight->ExportHeard();
	TestTrue(TEXT("Heard: the lines and the bark"), Heard.Contains(TEXT("line:start_1")) && Heard.Contains(TEXT("line:beat_1"))
		&& Heard.Contains(TEXT("bark:1:Winded")));

	// A load: a fresh fight, the save's heard list, the checkpoint at his phase.
	Fight->SetUpFight(Def);
	Fight->PlayerOverride = Kate;
	Fight->ImportHeard(Heard);
	Missions->GetTracker()->RestoreCompletedObjectives({ TEXT("meet"), TEXT("beat_crew") });
	Fight->RestartAtPhase(1);
	TestEqual(TEXT("At his phase"), Fight->GetPhaseIndex(), 1);
	TestFalse(TEXT("The checkpoint is the beat's end: no beat"), Fight->IsInBeat());
	TestFalse(TEXT("Kate is not held"), Kate->IsScriptedLocked());
	TestFalse(TEXT("The start line does not repeat"), Fight->GetLinesSpoken().Contains(FName(TEXT("start_1"))));
	LandSmoke(TestWorld, Fight->GetBoss()->GetActorLocation(), Kate);
	Fight->Tick(1.f / 30.f);
	TestTrue(TEXT("Winded again"), Fight->IsWinded());
	TestFalse(TEXT("...without the bark again (once a fight, the load included)"), Fight->GetLinesSpoken().Contains(FName(TEXT("winded_1"))));
	return true;
}

#endif
