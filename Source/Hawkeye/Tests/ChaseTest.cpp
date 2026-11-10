#include "Chase/ChaseRouteDefinition.h"
#include "Chase/ChaseRules.h"
#include "Chase/ChaseSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/Takedownable.h"
#include "Engine/World.h"
#include "HawkeyeGameMode.h"
#include "Partner/HawkeyePartnerController.h"
#include "Phone/PhoneInbox.h"
#include "Player/HawkeyeCharacter.h"
#include "Save/HawkeyeCampaignState.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Chase.*: the chase's rules (claude-docs/gameplay-semantics.md, "Chases"), pure, and three runs of
 * UChaseSubsystem over a scripted route in a test world: a catch, a loss, and the beat's fallback.
 */
namespace HawkeyeChaseTest
{
	static TArray<FVector> Line()
	{
		return { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 1000.f, 0.f) };
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

	static void AddPoint(UChaseRouteDefinition* Route, const FVector& At, EChaseLeg Leg, FName Objective = NAME_None)
	{
		FChaseWaypoint Point;
		Point.Location = At;
		Point.Leg = Leg;
		Point.CompletesObjective = Objective;
		Route->Waypoints.Add(Point);
	}

	/** 120 m along X: a run, a vault, a run with run_a at its end, a leap down, a drop, a run to the end. */
	static UChaseRouteDefinition* MakeRoute()
	{
		UChaseRouteDefinition* Route = NewObject<UChaseRouteDefinition>();
		AddPoint(Route, FVector(0.f, 0.f, 1000.f), EChaseLeg::Run);
		AddPoint(Route, FVector(1500.f, 0.f, 1000.f), EChaseLeg::Run);
		AddPoint(Route, FVector(1700.f, 0.f, 1000.f), EChaseLeg::Vault);
		AddPoint(Route, FVector(2500.f, 0.f, 1000.f), EChaseLeg::Run, TEXT("run_a"));
		AddPoint(Route, FVector(3100.f, 0.f, 900.f), EChaseLeg::JumpGap);
		AddPoint(Route, FVector(3400.f, 0.f, 900.f), EChaseLeg::Run);
		AddPoint(Route, FVector(3550.f, 0.f, 0.f), EChaseLeg::Drop);
		AddPoint(Route, FVector(12000.f, 0.f, 0.f), EChaseLeg::Run);
		Route->CatchObjective = TEXT("catch_it");
		Route->ThiefObjectives = { TEXT("catch_it") };
		return Route;
	}

	/** A plain character for Kate, its feet on P. */
	static ACharacter* SpawnKate(FHawkeyeTestWorld& World, const FVector& Feet)
	{
		ACharacter* Kate = Cast<ACharacter>(World.SpawnActor(ACharacter::StaticClass(), Feet + FVector(0.f, 0.f, 90.f), FRotator::ZeroRotator));
		return Kate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseBands, "Hawkeye.Chase.Rules.LeadBandsAndSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseBands::RunTest(const FString& Parameters)
{
	const FChaseTuning T;
	TestEqual(TEXT("Under CloseLead is Close"), UChaseRules::Band(T.CloseLead - 1.f, T), EChaseLeadBand::Close);
	TestEqual(TEXT("CloseLead itself is Steady"), UChaseRules::Band(T.CloseLead, T), EChaseLeadBand::Steady);
	TestEqual(TEXT("FarLead itself is Far"), UChaseRules::Band(T.FarLead, T), EChaseLeadBand::Far);
	TestEqual(TEXT("LoseLead itself is Losing"), UChaseRules::Band(T.LoseLead, T), EChaseLeadBand::Losing);
	TestEqual(TEXT("On her heels he finds CloseBoost"), UChaseRules::TargetSpeed(100.f, T), T.BaseSpeed * T.CloseBoost);
	TestEqual(TEXT("A steady lead runs at BaseSpeed"), UChaseRules::TargetSpeed(1500.f, T), T.BaseSpeed);
	TestEqual(TEXT("At LoseLead he is at MinSpeedScale"), UChaseRules::TargetSpeed(T.LoseLead, T), T.BaseSpeed * T.MinSpeedScale, 0.01f);
	TestEqual(TEXT("Far past it, never slower"), UChaseRules::TargetSpeed(T.LoseLead * 10.f, T), T.BaseSpeed * T.MinSpeedScale, 0.01f);
	const float Mid = UChaseRules::TargetSpeed((T.FarLead + T.LoseLead) * 0.5f, T);
	TestTrue(TEXT("Half way through Far he is half way down"), FMath::IsNearlyEqual(Mid, T.BaseSpeed * (1.f + T.MinSpeedScale) * 0.5f, 0.5f));
	float Previous = BIG_NUMBER;
	bool bNeverFaster = true;
	for (float Lead = 0.f; Lead < 10000.f; Lead += 50.f)
	{
		const float Speed = UChaseRules::TargetSpeed(Lead, T);
		bNeverFaster &= Speed <= Previous + 0.01f && Speed > 0.f;
		Previous = Speed;
	}
	TestTrue(TEXT("The further ahead he is, the slower he runs, and he never stops"), bNeverFaster);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseSpeedStep, "Hawkeye.Chase.Rules.SpeedChangesGently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseSpeedStep::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("At most Rate * dt toward a higher target"), UChaseRules::StepSpeed(600.f, 700.f, 400.f, 0.1f), 640.f);
	TestEqual(TEXT("At most Rate * dt toward a lower one"), UChaseRules::StepSpeed(600.f, 400.f, 400.f, 0.1f), 560.f);
	TestEqual(TEXT("Reaches a near target exactly"), UChaseRules::StepSpeed(600.f, 610.f, 400.f, 0.1f), 610.f);
	TestEqual(TEXT("No time, no change"), UChaseRules::StepSpeed(600.f, 700.f, 400.f, 0.f), 600.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseLoseClock, "Hawkeye.Chase.Rules.LoseClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseLoseClock::RunTest(const FString& Parameters)
{
	const FChaseTuning T;
	float Clock = 0.f;
	for (int32 Frame = 0; Frame < 59; ++Frame)
	{
		Clock = UChaseRules::StepLoseClock(Clock, T.LoseLead, 0.1f, false, T);
	}
	TestFalse(TEXT("5.9 s at LoseLead is not yet lost"), UChaseRules::IsLost(Clock, T));
	TestTrue(TEXT("6 s is"), UChaseRules::IsLost(UChaseRules::StepLoseClock(Clock, T.LoseLead, 0.11f, false, T), T));
	TestEqual(TEXT("Back under LoseLead the clock is 0 at once"), UChaseRules::StepLoseClock(Clock, T.LoseLead - 1.f, 0.1f, false, T), 0.f);
	TestEqual(TEXT("Held (the beat, Kate down), it does not move"), UChaseRules::StepLoseClock(3.f, T.LoseLead * 2.f, 0.5f, true, T), 3.f);
	TestEqual(TEXT("Held under the lead it keeps its value too"), UChaseRules::StepLoseClock(3.f, 0.f, 0.5f, true, T), 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseProgress, "Hawkeye.Chase.Rules.ProgressAndLead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseProgress::RunTest(const FString& Parameters)
{
	using namespace HawkeyeChaseTest;
	const FChaseTuning T;
	const TArray<FVector> Points = Line();
	const TArray<float> D = UChaseRules::Cumulative(Points);
	TestEqual(TEXT("Cumulative ends at the length"), D.Last(), 2000.f);
	TestTrue(TEXT("PointAt the middle of the second leg"), UChaseRules::PointAt(Points, D, 1500.f).Equals(FVector(1000.f, 500.f, 0.f), 0.1f));
	TestTrue(TEXT("PointAt clamps past the end"), UChaseRules::PointAt(Points, D, 9000.f).Equals(Points.Last(), 0.1f));
	float Along = 0.f;
	TestTrue(TEXT("Beside the first leg she is on the route"), UChaseRules::Project(Points, D, FVector(400.f, 300.f, 0.f), 0.f, T, Along));
	TestEqual(TEXT("...400 cm along"), Along, 400.f, 0.5f);
	TestTrue(TEXT("Round the corner: 1000 + 600"), UChaseRules::Project(Points, D, FVector(1200.f, 600.f, 0.f), 400.f, T, Along));
	TestEqual(TEXT("...1600 cm along"), Along, 1600.f, 0.5f);
	TestFalse(TEXT("25 m off the route she is off it"), UChaseRules::Project(Points, D, FVector(500.f, -2500.f, 0.f), 400.f, T, Along));
	TestEqual(TEXT("...and her progress holds"), Along, 400.f);
	FChaseTuning Narrow = T;
	Narrow.AheadWindow = 300.f;
	TestTrue(TEXT("Beyond the window ahead the search stops at its edge"),
		UChaseRules::Project(Points, D, FVector(1000.f, 900.f, 0.f), 0.f, Narrow, Along) && Along <= 300.f + 0.5f);
	TestEqual(TEXT("Lead on the route is the difference"), UChaseRules::Lead(1500.f, 400.f, true, 50.f), 1100.f);
	TestEqual(TEXT("Never under 0 (she got ahead of him on a grapple)"), UChaseRules::Lead(1500.f, 1900.f, true, 50.f), 0.f);
	TestEqual(TEXT("Off the route it is the straight line"), UChaseRules::Lead(1500.f, 400.f, false, 3200.f), 3200.f);
	TestEqual(TEXT("Her progress never goes back"), UChaseRules::StepProgress(1500.f, 1200.f, 50.f, 0.1f), 1500.f);
	TestEqual(TEXT("It follows what she really moved"), UChaseRules::StepProgress(1500.f, 1540.f, 40.f, 0.1f), 1540.f);
	TestEqual(TEXT("A jump to another part of the route is held to 1.5 times her move plus 300 cm/s"),
		UChaseRules::StepProgress(1500.f, 5800.f, 60.f, 0.1f), 1620.f);
	TestTrue(TEXT("A waypoint is reached ReachTolerance early"), UChaseRules::HasReached(1700.f, 2000.f, T));
	TestFalse(TEXT("...not before"), UChaseRules::HasReached(1699.f, 2000.f, T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseLegs, "Hawkeye.Chase.Rules.Legs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseLegs::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Vault 0.5 s"), UChaseRules::LegSeconds(EChaseLeg::Vault, 200.f, 0.f), 0.5f);
	TestEqual(TEXT("Climb 0.8 s"), UChaseRules::LegSeconds(EChaseLeg::Climb, 100.f, -100.f), 0.8f);
	TestEqual(TEXT("A 5 m leap 0.75 s"), UChaseRules::LegSeconds(EChaseLeg::JumpGap, 500.f, 0.f), 0.75f, 0.001f);
	TestEqual(TEXT("A leap is never over 1.4 s"), UChaseRules::LegSeconds(EChaseLeg::JumpGap, 5000.f, 0.f), 1.4f);
	TestEqual(TEXT("A 15 m drop 1.97 s"), UChaseRules::LegSeconds(EChaseLeg::Drop, 140.f, 1500.f), 1500.f / 900.f + 0.3f, 0.001f);
	TestEqual(TEXT("A run is the rubber band's business"), UChaseRules::LegSeconds(EChaseLeg::Run, 500.f, 0.f), 0.f);
	const FVector From(0.f, 0.f, 1000.f);
	const FVector To(600.f, 0.f, 900.f);
	for (const EChaseLeg Leg : { EChaseLeg::Run, EChaseLeg::Vault, EChaseLeg::Climb, EChaseLeg::JumpGap, EChaseLeg::Drop })
	{
		const FString Name = UEnum::GetValueAsString(Leg);
		TestTrue(Name + TEXT(" starts where it starts"), UChaseRules::LegPosition(Leg, From, To, 0.f).Equals(From, 0.5f));
		TestTrue(Name + TEXT(" ends where it ends"), UChaseRules::LegPosition(Leg, From, To, 1.f).Equals(To, 0.5f));
	}
	TestTrue(TEXT("A leap rises over the straight line half way"),
		UChaseRules::LegPosition(EChaseLeg::JumpGap, From, To, 0.5f).Z > FMath::Lerp(From.Z, To.Z, 0.5f) + 50.f);
	TestTrue(TEXT("A vault clears 90 cm over the higher end"), UChaseRules::LegPosition(EChaseLeg::Vault, From, To, 0.5f).Z >= From.Z + 90.f);
	const FVector Drop = UChaseRules::LegPosition(EChaseLeg::Drop, From, FVector(140.f, 0.f, 0.f), 0.5f);
	TestTrue(TEXT("A drop is out over the edge, then straight down"), FMath::IsNearlyEqual(Drop.X, 140.f, 0.5f) && Drop.Z < From.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseCatchAndBeat, "Hawkeye.Chase.Rules.CatchBeatAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseCatchAndBeat::RunTest(const FString& Parameters)
{
	const FChaseTuning T;
	TestFalse(TEXT("Running free, he cannot be caught"), UChaseRules::IsCatchable(false));
	TestTrue(TEXT("Cornered he can"), UChaseRules::IsCatchable(true));
	TestFalse(TEXT("Running free is not caught"), UChaseRules::IsCaught(false, false, false, false));
	TestTrue(TEXT("A takedown catches him"), UChaseRules::IsCaught(true, false, false, false));
	TestTrue(TEXT("So does a bola or a knockdown"), UChaseRules::IsCaught(false, false, true, false));
	TestTrue(TEXT("Or anything that leaves him limp"), UChaseRules::IsCaught(false, true, false, false));
	TestEqual(TEXT("Cornered, his back is to her"), UChaseRules::CorneredYaw(FVector(100.f, 0.f, 0.f), FVector::ZeroVector), 0.f, 0.01f);
	TestEqual(TEXT("...from whichever side she comes"), UChaseRules::CorneredYaw(FVector::ZeroVector, FVector(0.f, 100.f, 0.f)), -90.f, 0.01f);
	TestTrue(TEXT("The beat: a zip started in the zone at its anchor"), UChaseRules::ShouldStartBeat(false, true, true, 120.f));
	TestFalse(TEXT("...not once done"), UChaseRules::ShouldStartBeat(true, true, true, 120.f));
	TestFalse(TEXT("...not from outside the zone"), UChaseRules::ShouldStartBeat(false, true, false, 120.f));
	TestFalse(TEXT("...not at another anchor"), UChaseRules::ShouldStartBeat(false, true, true, UChaseRules::AnchorTolerance + 1.f));
	TestFalse(TEXT("...not without a zip starting"), UChaseRules::ShouldStartBeat(false, false, true, 0.f));
	TestFalse(TEXT("The first time it cannot be skipped"), UChaseRules::CanSkipBeat(false, 2.f));
	TestFalse(TEXT("A replay not in its first 0.3 s"), UChaseRules::CanSkipBeat(true, 0.29f));
	TestTrue(TEXT("A replay after that"), UChaseRules::CanSkipBeat(true, 0.3f));
	TestFalse(TEXT("A 3.1 s beat is not stuck"), UChaseRules::IsBeatStuck(3.1f));
	TestTrue(TEXT("A 5 s one is"), UChaseRules::IsBeatStuck(5.f));
	TestEqual(TEXT("A load keeps a close thief where he was"), UChaseRules::RestoredThiefProgress(3000.f, 2000.f, T), 3000.f);
	TestEqual(TEXT("...and never puts one further than RestoreMaxLead ahead"),
		UChaseRules::RestoredThiefProgress(9000.f, 2000.f, T), 2000.f + T.RestoreMaxLead);
	return true;
}

// --- The subsystem over a scripted route ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseWorldCatch, "Hawkeye.Chase.World.ScriptedRouteCatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseWorldCatch::RunTest(const FString& Parameters)
{
	using namespace HawkeyeChaseTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UChaseSubsystem* Chase = World ? World->GetSubsystem<UChaseSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("chase"), Chase))
	{
		return false;
	}
	Missions->StartMission(MakeMission({ TEXT("run_a"), TEXT("catch_it") }));
	UChaseRouteDefinition* Route = MakeRoute();
	ACharacter* Kate = SpawnKate(TestWorld, Route->Waypoints[0].Location);
	Chase->PlayerOverride = Kate;
	Chase->bSkipCaughtHold = true;
	TestTrue(TEXT("The chase starts"), Chase->StartChase(Route, false));
	AThugCharacter* Thief = Chase->GetThief();
	if (!TestNotNull(TEXT("A thief"), Thief))
	{
		return false;
	}
	TestTrue(TEXT("Tagged for the takedown and the chase"), Thief->ActorHasTag(TEXT("Thug")) && Thief->ActorHasTag(UChaseSubsystem::ThiefTag));
	TestEqual(TEXT("He starts StartLead ahead"), Chase->GetThiefProgress(), Route->Tuning.StartLead);

	// Kate runs the route at 650 cm/s; he never jumps more than a frame's worth, never goes back.
	const float Dt = 1.f / 30.f;
	FVector LastThief = Thief->GetActorLocation();
	float LastProgress = Chase->GetThiefProgress();
	float WorstStep = 0.f;
	bool bNeverBack = true;
	float KateAlong = 0.f;
	const TArray<FVector> Points = Route->GetPoints();
	const TArray<float> D = UChaseRules::Cumulative(Points);
	bool bObjectiveOnTime = false;
	for (int32 Frame = 0; Frame < 30 * 40 && Chase->GetState() == EChaseState::Running; ++Frame)
	{
		KateAlong = FMath::Min(KateAlong + 650.f * Dt, Chase->GetThiefProgress() - 250.f);
		Kate->SetActorLocation(UChaseRules::PointAt(Points, D, KateAlong) + FVector(0.f, 0.f, 90.f));
		Chase->Tick(Dt);
		WorstStep = FMath::Max(WorstStep, FVector::Dist(LastThief, Thief->GetActorLocation()));
		bNeverBack &= Chase->GetThiefProgress() >= LastProgress - KINDA_SMALL_NUMBER;
		LastThief = Thief->GetActorLocation();
		LastProgress = Chase->GetThiefProgress();
		const UMissionObjective* RunA = Missions->GetTracker()->FindObjective(TEXT("run_a"));
		if (!bObjectiveOnTime && RunA->IsCompleted())
		{
			bObjectiveOnTime = KateAlong >= D[3] - Route->Tuning.ReachTolerance - 30.f;
		}
	}
	TestTrue(FString::Printf(TEXT("He never moves more than 60 cm in a frame (worst %.1f)"), WorstStep), WorstStep <= 60.f);
	TestTrue(TEXT("His progress never goes back"), bNeverBack);
	TestTrue(TEXT("run_a completed as Kate reached its waypoint"), bObjectiveOnTime);
	TestEqual(TEXT("At the route's end he is cornered"), Chase->GetState(), EChaseState::Cornered);
	Kate->SetActorLocation(Points.Last() + FVector(-400.f, 300.f, 90.f));
	Chase->Tick(Dt);
	const float Facing = Thief->GetActorRotation().Yaw;
	const float Away = UChaseRules::CorneredYaw(Thief->GetActorLocation(), Kate->GetActorLocation());
	TestTrue(TEXT("Cornered, he turns his back to her"), FMath::Abs(FMath::FindDeltaAngleDegrees(Facing, Away)) < 1.f);
	TestFalse(TEXT("catch_it waits for the catch"), Missions->GetTracker()->FindObjective(TEXT("catch_it"))->IsCompleted());
	TestFalse(TEXT("Cornered, he can be taken down"), Thief->bRefusesTakedown);

	ITakedownable::Execute_OnTakedown(Thief, Kate);
	Chase->Tick(Dt);
	Chase->Tick(Dt);
	TestTrue(TEXT("The takedown catches him and completes catch_it"), Missions->GetTracker()->FindObjective(TEXT("catch_it"))->IsCompleted());
	TestTrue(TEXT("...which completes the chapter"), Missions->IsMissionComplete());
	TestFalse(TEXT("The chase is over"), Chase->IsChaseRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseWorldLost, "Hawkeye.Chase.World.FallingBehindLoses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseWorldLost::RunTest(const FString& Parameters)
{
	using namespace HawkeyeChaseTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UChaseSubsystem* Chase = World ? World->GetSubsystem<UChaseSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("chase"), Chase))
	{
		return false;
	}
	Missions->StartMission(MakeMission({ TEXT("run_a"), TEXT("catch_it") }));
	UChaseRouteDefinition* Route = MakeRoute();
	ACharacter* Kate = SpawnKate(TestWorld, Route->Waypoints[0].Location);
	Chase->PlayerOverride = Kate;
	Chase->StartChase(Route, false);
	TWeakObjectPtr<AThugCharacter> Thief = Chase->GetThief();
	const float Dt = 0.1f;
	float SlowestSpeed = BIG_NUMBER;
	float LostAt = -1.f;
	float LoseLeadAt = -1.f;
	for (int32 Frame = 0; Frame < 400 && LostAt < 0.f; ++Frame)
	{
		Chase->Tick(Dt);
		if (Chase->GetState() == EChaseState::Running)
		{
			SlowestSpeed = FMath::Min(SlowestSpeed, Chase->GetThiefSpeed());
			if (LoseLeadAt < 0.f && Chase->GetLead() >= Route->Tuning.LoseLead)
			{
				LoseLeadAt = Frame * Dt;
			}
		}
		if (Chase->GetState() == EChaseState::Lost)
		{
			LostAt = Frame * Dt;
		}
	}
	TestTrue(TEXT("Standing still she loses him"), LostAt > 0.f);
	TestTrue(FString::Printf(TEXT("LoseSeconds after the lead reached LoseLead (%.1f then %.1f s)"), LoseLeadAt, LostAt),
		LoseLeadAt >= 0.f && FMath::IsNearlyEqual(LostAt - LoseLeadAt, Route->Tuning.LoseSeconds, 0.25f));
	TestTrue(FString::Printf(TEXT("Easing off, he never ran under MinSpeedScale (%.0f)"), SlowestSpeed),
		SlowestSpeed >= Route->Tuning.BaseSpeed * Route->Tuning.MinSpeedScale - 1.f);
	TestFalse(TEXT("The thief goes with the loss"), Thief.IsValid() && !Thief->IsActorBeingDestroyed());
	TestFalse(TEXT("Nothing was completed"), Missions->GetTracker()->FindObjective(TEXT("catch_it"))->IsCompleted());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseWorldBeatFallback, "Hawkeye.Chase.World.BeatPassedByOtherWays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseWorldBeatFallback::RunTest(const FString& Parameters)
{
	using namespace HawkeyeChaseTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	UChaseSubsystem* Chase = World ? World->GetSubsystem<UChaseSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions) || !TestNotNull(TEXT("chase"), Chase))
	{
		return false;
	}
	Missions->StartMission(MakeMission({ TEXT("run_a"), TEXT("catch_it") }));
	UChaseRouteDefinition* Route = MakeRoute();
	Route->bHasBeat = true;
	Route->BeatWaypoint = 4;
	Route->BeatZoneCentre = FVector(2500.f, 0.f, 1080.f);
	Route->BeatAnchor = FVector(3200.f, 0.f, 1000.f);
	Route->BeatExit = FVector(2800.f, 200.f, 0.f);
	ACharacter* Kate = SpawnKate(TestWorld, Route->Waypoints[0].Location);
	Chase->PlayerOverride = Kate;
	Chase->StartChase(Route, false);
	TestFalse(TEXT("A route with a beat starts with it to play"), Chase->IsBeatDone());
	Chase->Tick(1.f / 30.f);
	TestTrue(TEXT("Running, the thief refuses a takedown"), Chase->GetThief() && Chase->GetThief()->bRefusesTakedown);
	// She falls off the roof short of the gap and runs on along the street under it: no zip, no beat.
	const float Dt = 1.f / 30.f;
	for (int32 Frame = 0; Frame < 30 * 2; ++Frame)
	{
		Kate->SetActorLocation(FVector(1200.f + Frame * 20.f, 0.f, 1090.f));
		Chase->Tick(Dt);
	}
	for (int32 Frame = 0; Frame < 30 * 4 && !Chase->IsBeatDone(); ++Frame)
	{
		Kate->SetActorLocation(FVector(2400.f + Frame * 30.f, 0.f, 90.f));
		Chase->Tick(Dt);
	}
	TestTrue(TEXT("Past the gap another way, the beat is marked passed"), Chase->IsBeatDone());
	TestFalse(TEXT("...and never played"), Chase->DidBeatPlay());
	TestEqual(TEXT("The chase goes on"), Chase->GetState(), EChaseState::Running);
	TestEqual(TEXT("No beat phase is left running"), Chase->GetBeatPhase(), 0);
	TestTrue(TEXT("Her progress followed her past the gap"), Chase->GetKateProgress() >= Chase->GetWaypointProgress(4));
	Chase->Tick(1.f / 30.f);
	TestTrue(TEXT("Past the beat but still running, he still refuses it"), Chase->GetThief() && Chase->GetThief()->bRefusesTakedown);
	return true;
}

// --- The chapter's cast, the phone's threads, the save ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseChapterCast, "Hawkeye.Chase.World.ChapterWithoutPartnerBenchesHim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseChapterCast::RunTest(const FString& Parameters)
{
	using namespace HawkeyeChaseTest;
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	AHawkeyeCharacter* Clint = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, 0.f, 100.f),
		FRotator::ZeroRotator));
	AHawkeyePartnerController* Brain = Cast<AHawkeyePartnerController>(TestWorld.SpawnActor(AHawkeyePartnerController::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator));
	AActor* Ours = TestWorld.SpawnActor(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	AActor* Theirs = TestWorld.SpawnActor(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Clint"), Clint) || !TestNotNull(TEXT("his brain"), Brain) || !Ours || !Theirs)
	{
		return false;
	}
	Brain->Possess(Clint);
	Ours->Tags.Add(TEXT("objective:chase_thief"));
	Theirs->Tags.Add(TEXT("objective:find_arrow"));
	UMissionDefinition* NightShift = MakeMission({ TEXT("chase_thief"), TEXT("catch_thief") });
	NightShift->bPartnerPresent = false;
	TestNotNull(TEXT("Before: a partner"), Brain->GetPartner());

	AHawkeyeGameMode::ApplyChapterCast(World, NightShift);
	TestTrue(TEXT("A chapter without him benches him"), Brain->IsBenched());
	TestNull(TEXT("...so nothing sees a partner (HUD line, switch, banter)"), Brain->GetPartner());
	TestTrue(TEXT("...his pawn hidden"), Clint->IsHidden());
	TestFalse(TEXT("...with no collision"), Clint->GetActorEnableCollision());
	TestFalse(TEXT("...not thinking"), Brain->IsThinkingEnabled());
	TestFalse(TEXT("...and never in a fight"), Brain->IsInFight());
	TestFalse(TEXT("...and no revive from the bench"), Brain->RequestRevive(Clint));
	TestFalse(TEXT("This chapter's beacon shows"), Ours->IsHidden());
	TestTrue(TEXT("Another chapter's beacon is dark"), Theirs->IsHidden());

	UMissionDefinition* WithHim = MakeMission({ TEXT("find_arrow") });
	AHawkeyeGameMode::ApplyChapterCast(World, WithHim);
	TestFalse(TEXT("A chapter with him (the default) brings him back"), Brain->IsBenched());
	TestNotNull(TEXT("...a partner again"), Brain->GetPartner());
	TestFalse(TEXT("...shown"), Clint->IsHidden());
	TestTrue(TEXT("...thinking"), Brain->IsThinkingEnabled());
	TestTrue(TEXT("The default is a partner present"), GetDefault<UMissionDefinition>()->bPartnerPresent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseQuietFurniture, "Hawkeye.Chase.World.OtherChaptersFurnitureIsQuiet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseQuietFurniture::RunTest(const FString& Parameters)
{
	using namespace HawkeyeChaseTest;
	FHawkeyeTestWorld TestWorld;
	UMissionSubsystem* Missions = TestWorld.Get() ? TestWorld.Get()->GetSubsystem<UMissionSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("missions"), Missions))
	{
		return false;
	}
	Missions->StartMission(MakeMission({ TEXT("chase_thief"), TEXT("catch_thief") }));
	TestTrue(TEXT("The running chapter has chase_thief"), Missions->HasObjective(TEXT("chase_thief")));
	TestFalse(TEXT("...and not the old chapter's find_arrow, so its volume and arrow keep quiet"), Missions->HasObjective(TEXT("find_arrow")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChasePhoneThreads, "Hawkeye.Phone.RepliesSitInTheThreadTheyAnswer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChasePhoneThreads::RunTest(const FString& Parameters)
{
	auto Row = [](const TCHAR* Sender, const TCHAR* Thread, float Delay, bool bRead)
	{
		FHawkeyePhoneMessage Out;
		Out.Sender = FText::FromString(Sender);
		Out.Thread = FText::FromString(Thread);
		Out.Text = FText::FromString(TEXT("[text]"));
		Out.Trigger = EHawkeyeMessageTrigger::ObjectiveCompleted;
		Out.TriggerObjectiveId = TEXT("catch_thief");
		Out.DelaySeconds = Delay;
		Out.bRead = bRead;
		Out.bDraft = true;
		return Out;
	};
	UPhoneInbox* Inbox = NewObject<UPhoneInbox>();
	Inbox->SetMessages({
		{ TEXT("c1"), Row(TEXT("Clint"), TEXT(""), 1.f, false) },
		{ TEXT("k1"), Row(TEXT("Kate"), TEXT("Clint"), 2.f, true) },
		{ TEXT("c2"), Row(TEXT("Clint"), TEXT(""), 3.f, false) },
	});
	Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveCompleted, TEXT("catch_thief"), NAME_None);
	Inbox->Advance(10.f);
	TestEqual(TEXT("One contact: Clint"), Inbox->GetContacts(), TArray<FString>({ TEXT("Clint") }));
	const TArray<FHawkeyeReceivedMessage> Thread = Inbox->GetThread(TEXT("Clint"));
	TestEqual(TEXT("His thread holds Kate's reply between his texts"), Thread.Num(), 3);
	TestEqual(TEXT("...shown as hers"), Thread.Num() == 3 ? Thread[1].Sender.ToString() : FString(), FString(TEXT("Kate")));
	TestEqual(TEXT("Her own reply is not unread"), Inbox->GetUnreadCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChaseSaveMirror, "Hawkeye.Chase.World.SaveMirrorRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChaseSaveMirror::RunTest(const FString& Parameters)
{
	UHawkeyeCampaignState* Campaign = NewObject<UHawkeyeCampaignState>();
	Campaign->ChaseRoutePath = TEXT("/Game/Missions/Chases/DA_Chase_X.DA_Chase_X");
	Campaign->ChaseThiefProgress = 4200.f;
	Campaign->ChaseKateProgress = 3000.f;
	Campaign->ChaseReachedWaypoints = 7;
	Campaign->bChaseBeatDone = true;
	Campaign->bChasePulledAhead = true;
	Campaign->bChaseVaulted = true;
	Campaign->ResetCampaign();
	TestTrue(TEXT("A new campaign has no chase"), Campaign->ChaseRoutePath.IsEmpty());
	TestTrue(TEXT("...and every chase field back to its start"), Campaign->ChaseThiefProgress == 0.f && Campaign->ChaseKateProgress == 0.f
		&& Campaign->ChaseReachedWaypoints == 0 && !Campaign->bChaseBeatDone && !Campaign->bChasePulledAhead && !Campaign->bChaseVaulted);
	int32 SaveGameFields = 0;
	for (TFieldIterator<FProperty> It(UHawkeyeCampaignState::StaticClass()); It; ++It)
	{
		SaveGameFields += It->GetName().StartsWith(TEXT("Chase")) || It->GetName().StartsWith(TEXT("bChase"))
			? (It->HasAnyPropertyFlags(CPF_SaveGame) ? 1 : 0) : 0;
	}
	TestEqual(TEXT("The seven chase fields are SaveGame properties (SPUD writes them)"), SaveGameFields, 7);
	return true;
}

#endif
