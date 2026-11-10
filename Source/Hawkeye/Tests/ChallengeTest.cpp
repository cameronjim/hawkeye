#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeRules.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Challenge/ChallengeTarget.h"
#include "Challenge/ChallengeTracker.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Save/HawkeyeCampaignState.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/ChallengePanelWidget.h"
#include "UI/ChallengeResultsWidget.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The side challenge rules from claude-docs/gameplay-semantics.md, "Side challenges": what an arrow
 * scores, the medal lines, how a run fails, bests and their save, the secondary markers, and calm
 * thugs. The scripted runs through the district are Hawkeye.Lap.ArcheryChallenge1 and
 * Hawkeye.Lap.TraversalChallenge1 (ChallengeLapTest.cpp), run in -game.
 */
namespace HawkeyeChallengeTest
{
	/** An archery challenge with Count still targets in a row 20 m out, medals at 12 / 20 / 28. */
	static UChallengeDefinition* MakeArchery(int32 Count, float TimeLimit = 60.f)
	{
		UChallengeDefinition* Definition = NewObject<UChallengeDefinition>();
		Definition->Id = TEXT("archery_test");
		Definition->Type = EChallengeType::Archery;
		Definition->Name = FText::FromString(TEXT("[Archery test]"));
		Definition->TimeLimitSeconds = TimeLimit;
		Definition->AreaRadius = 10000.f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FChallengeTargetSpawn Spawn;
			Spawn.Transform = FTransform(FRotator(0.f, 180.f, 0.f), FVector(2000.f, 200.f * Index, 0.f));
			Definition->Targets.Add(Spawn);
		}
		Definition->BronzeValue = 12.f;
		Definition->SilverValue = 20.f;
		Definition->GoldValue = 28.f;
		Definition->Reward = EChallengeReward::ArrowRefill;
		return Definition;
	}

	/** A traversal route of Count rings 10 m apart along X at 1 m up, medals under 60 / 90 / 120 s. */
	static UChallengeDefinition* MakeTraversal(int32 Count, float TimeLimit = 150.f)
	{
		UChallengeDefinition* Definition = NewObject<UChallengeDefinition>();
		Definition->Id = TEXT("traversal_test");
		Definition->Type = EChallengeType::Traversal;
		Definition->Name = FText::FromString(TEXT("[Traversal test]"));
		Definition->TimeLimitSeconds = TimeLimit;
		Definition->AreaRadius = 10000.f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Definition->Checkpoints.Add(FTransform(FVector(1000.f * (Index + 1), 0.f, 100.f)));
			Definition->CheckpointLegs.Add(EChallengeLeg::Run);
		}
		Definition->BronzeValue = 120.f;
		Definition->SilverValue = 90.f;
		Definition->GoldValue = 60.f;
		return Definition;
	}

	/** Runs a traversal to the end in Seconds, going through each ring in turn. */
	static void RunRoute(UChallengeTracker* Tracker, const UChallengeDefinition* Definition, float Seconds)
	{
		const float Step = Seconds / Definition->Checkpoints.Num();
		for (const FTransform& Ring : Definition->Checkpoints)
		{
			Tracker->Advance(Step, Ring.GetLocation());
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeScoringRings, "Hawkeye.Challenge.ScoringRings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeScoringRings::RunTest(const FString& Parameters)
{
	const UChallengeDefinition* Definition = HawkeyeChallengeTest::MakeArchery(1);
	TestEqual(TEXT("Dead centre is a bullseye"), UChallengeRules::ScoreHit(Definition, 0.f), 10);
	TestEqual(TEXT("On the bullseye's edge still counts as it"), UChallengeRules::ScoreHit(Definition, 8.f), 10);
	TestEqual(TEXT("Just outside it is the ring"), UChallengeRules::ScoreHit(Definition, 8.1f), 5);
	TestEqual(TEXT("On the ring's edge is the ring"), UChallengeRules::ScoreHit(Definition, 18.f), 5);
	TestEqual(TEXT("Past the ring is the outer face"), UChallengeRules::ScoreHit(Definition, 18.1f), 2);
	TestEqual(TEXT("The face's edge scores"), UChallengeRules::ScoreHit(Definition, 30.f), 2);
	TestEqual(TEXT("Off the face scores nothing"), UChallengeRules::ScoreHit(Definition, 30.5f), 0);
	TestEqual(TEXT("No definition, no points"), UChallengeRules::ScoreHit(nullptr, 0.f), 0);

	// The distance is measured in the plane of the face, whatever depth the tip sank to.
	const FVector Centre(100.f, 0.f, 150.f);
	TestEqual(TEXT("Depth along the normal is ignored"),
		UChallengeRules::RadialDistanceOnFace(Centre + FVector(-12.f, 0.f, 6.f), Centre, FVector(1.f, 0.f, 0.f)), 6.f, 0.01f);
	TestEqual(TEXT("Across the face it counts"),
		UChallengeRules::RadialDistanceOnFace(Centre + FVector(0.f, 3.f, 4.f), Centre, FVector(1.f, 0.f, 0.f)), 5.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeMedalThresholds, "Hawkeye.Challenge.MedalThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeMedalThresholds::RunTest(const FString& Parameters)
{
	using EM = EChallengeMedal;
	// Archery: at least the line.
	TestEqual(TEXT("Archery gold at the line"), UChallengeRules::ComputeMedal(EChallengeType::Archery, 84.f, 36.f, 60.f, 84.f), EM::Gold);
	TestEqual(TEXT("Archery silver below gold"), UChallengeRules::ComputeMedal(EChallengeType::Archery, 83.f, 36.f, 60.f, 84.f), EM::Silver);
	TestEqual(TEXT("Archery bronze at its line"), UChallengeRules::ComputeMedal(EChallengeType::Archery, 36.f, 36.f, 60.f, 84.f), EM::Bronze);
	TestEqual(TEXT("Archery nothing under bronze"), UChallengeRules::ComputeMedal(EChallengeType::Archery, 35.f, 36.f, 60.f, 84.f), EM::None);
	// Traversal: under the time.
	TestEqual(TEXT("Traversal gold under 60 s"), UChallengeRules::ComputeMedal(EChallengeType::Traversal, 59.9f, 120.f, 90.f, 60.f), EM::Gold);
	TestEqual(TEXT("Exactly 60 s is silver"), UChallengeRules::ComputeMedal(EChallengeType::Traversal, 60.f, 120.f, 90.f, 60.f), EM::Silver);
	TestEqual(TEXT("Exactly 90 s is bronze"), UChallengeRules::ComputeMedal(EChallengeType::Traversal, 90.f, 120.f, 90.f, 60.f), EM::Bronze);
	TestEqual(TEXT("119.9 s is bronze"), UChallengeRules::ComputeMedal(EChallengeType::Traversal, 119.9f, 120.f, 90.f, 60.f), EM::Bronze);
	TestEqual(TEXT("120 s earns nothing"), UChallengeRules::ComputeMedal(EChallengeType::Traversal, 120.f, 120.f, 90.f, 60.f), EM::None);
	TestEqual(TEXT("An unset line is never earned"), UChallengeRules::ComputeMedal(EChallengeType::Traversal, 1.f, 0.f, 0.f, 0.f), EM::None);
	TestEqual(TEXT("Nor is an unset archery line"), UChallengeRules::ComputeMedal(EChallengeType::Archery, 500.f, 0.f, 0.f, 0.f), EM::None);

	// A whole run through the tracker picks the medal from the definition.
	UChallengeTracker* Tracker = NewObject<UChallengeTracker>();
	UChallengeDefinition* Route = HawkeyeChallengeTest::MakeTraversal(8);
	Tracker->StartRun(Route);
	HawkeyeChallengeTest::RunRoute(Tracker, Route, 75.f);
	TestFalse(TEXT("The route is over"), Tracker->IsRunning());
	TestEqual(TEXT("75 s is silver"), Tracker->GetLastResult().Medal, EM::Silver);
	TestEqual(TEXT("The time is the clock"), Tracker->GetLastResult().Seconds, 75.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeArcheryScoresOnce, "Hawkeye.Challenge.ArcheryScoresEachTargetOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeArcheryScoresOnce::RunTest(const FString& Parameters)
{
	UChallengeTracker* Tracker = NewObject<UChallengeTracker>();
	UChallengeDefinition* Range = HawkeyeChallengeTest::MakeArchery(3);
	TestEqual(TEXT("A hit with no run scores nothing"), Tracker->RegisterHit(0, 0.f), 0);
	TestTrue(TEXT("Starts"), Tracker->StartRun(Range));
	AddExpectedMessagePlain(TEXT("is already running"), ELogVerbosity::Warning);
	TestFalse(TEXT("A second start is refused while it runs"), Tracker->StartRun(Range));

	int32 Ended = 0;
	Tracker->OnRunEnded.AddLambda([&Ended](const FChallengeResult&) { ++Ended; });
	TestEqual(TEXT("Bullseye"), Tracker->RegisterHit(0, 2.f), 10);
	TestEqual(TEXT("The same target again scores nothing"), Tracker->RegisterHit(0, 0.f), 0);
	TestEqual(TEXT("A bad index scores nothing"), Tracker->RegisterHit(7, 0.f), 0);
	TestEqual(TEXT("Ring"), Tracker->RegisterHit(1, 12.f), 5);
	TestEqual(TEXT("Two down"), Tracker->GetProgress(), 2);
	TestEqual(TEXT("Score so far"), Tracker->GetScore(), 15);
	TestTrue(TEXT("Still running with one up"), Tracker->IsRunning());
	TestEqual(TEXT("Outer"), Tracker->RegisterHit(2, 25.f), 2);
	TestFalse(TEXT("The last target completes the run"), Tracker->IsRunning());
	TestEqual(TEXT("It ended once"), Ended, 1);
	const FChallengeResult& Result = Tracker->GetLastResult();
	TestEqual(TEXT("Completed"), Result.Reason, EChallengeEndReason::Completed);
	TestEqual(TEXT("Score 17"), Result.Score, 17);
	TestEqual(TEXT("17 is bronze (12 / 20 / 28)"), Result.Medal, EChallengeMedal::Bronze);
	TestTrue(TEXT("A first completion is a best"), Result.bNewBest);
	TestEqual(TEXT("Recorded"), Tracker->GetRecord(Range->Id).BestScore, 17);
	TestEqual(TEXT("No hit after the end"), Tracker->RegisterHit(1, 0.f), 0);

	UChallengeDefinition* Empty = HawkeyeChallengeTest::MakeArchery(0);
	AddExpectedMessagePlain(TEXT("has no targets"), ELogVerbosity::Warning);
	TestFalse(TEXT("A range with no targets never starts"), Tracker->StartRun(Empty));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeCheckpointsInOrder, "Hawkeye.Challenge.CheckpointsInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeCheckpointsInOrder::RunTest(const FString& Parameters)
{
	UChallengeTracker* Tracker = NewObject<UChallengeTracker>();
	UChallengeDefinition* Route = HawkeyeChallengeTest::MakeTraversal(3);
	Tracker->StartRun(Route);
	TestEqual(TEXT("Heading for the first"), Tracker->GetNextCheckpointIndex(), 0);
	Tracker->Advance(1.f, Route->Checkpoints[1].GetLocation());
	TestEqual(TEXT("The second ring out of turn does not count"), Tracker->GetProgress(), 0);
	Tracker->Advance(1.f, Route->Checkpoints[0].GetLocation() + FVector(0.f, 140.f, 0.f));
	TestEqual(TEXT("Within 150 cm of the first counts"), Tracker->GetProgress(), 1);
	Tracker->Advance(1.f, Route->Checkpoints[1].GetLocation() + FVector(0.f, 0.f, 160.f));
	TestEqual(TEXT("160 cm over the second does not"), Tracker->GetProgress(), 1);
	Tracker->Advance(1.f, Route->Checkpoints[1].GetLocation());
	TestEqual(TEXT("Heading for the last"), Tracker->GetNextCheckpointIndex(), 2);
	Tracker->Advance(1.f, Route->Checkpoints[2].GetLocation());
	TestFalse(TEXT("The last ring ends the run"), Tracker->IsRunning());
	TestEqual(TEXT("Completed"), Tracker->GetLastResult().Reason, EChallengeEndReason::Completed);
	TestEqual(TEXT("After 5 s"), Tracker->GetLastResult().Seconds, 5.f, 0.01f);
	TestEqual(TEXT("5 s is gold"), Tracker->GetLastResult().Medal, EChallengeMedal::Gold);
	TestEqual(TEXT("No next checkpoint once it ends"), Tracker->GetNextCheckpointIndex(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeFailConditions, "Hawkeye.Challenge.FailConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeFailConditions::RunTest(const FString& Parameters)
{
	UChallengeTracker* Tracker = NewObject<UChallengeTracker>();
	UChallengeDefinition* Range = HawkeyeChallengeTest::MakeArchery(4, 60.f);

	// Time up: a hit or two, then the clock runs out.
	Tracker->StartRun(Range);
	Tracker->RegisterHit(0, 0.f);
	TestFalse(TEXT("59.9 s is still on"), Tracker->Advance(59.9f, FVector::ZeroVector));
	TestTrue(TEXT("60 s ends it"), Tracker->Advance(0.1f, FVector::ZeroVector));
	TestEqual(TEXT("As time up"), Tracker->GetLastResult().Reason, EChallengeEndReason::TimeUp);
	TestEqual(TEXT("No medal for a failed run"), Tracker->GetLastResult().Medal, EChallengeMedal::None);
	TestEqual(TEXT("The score it had is reported"), Tracker->GetLastResult().Score, 10);
	TestFalse(TEXT("A failed run records nothing"), Tracker->GetRecord(Range->Id).HasCompleted());

	// Leaving the area: 100 m on the ground plane; height does not count.
	Tracker->StartRun(Range);
	TestFalse(TEXT("99 m out and 50 m up is inside"), Tracker->Advance(0.1f, FVector(9900.f, 0.f, 5000.f)));
	TestTrue(TEXT("101 m out is outside"), Tracker->Advance(0.1f, FVector(0.f, 10100.f, 0.f)));
	TestEqual(TEXT("As left the area"), Tracker->GetLastResult().Reason, EChallengeEndReason::LeftArea);

	// Given up.
	Tracker->StartRun(Range);
	Tracker->AbortRun();
	TestFalse(TEXT("Abort stops the run"), Tracker->IsRunning());
	TestEqual(TEXT("As abandoned"), Tracker->GetLastResult().Reason, EChallengeEndReason::Aborted);
	TestFalse(TEXT("Still no record"), Tracker->GetRecord(Range->Id).HasCompleted());

	// The same rule on its own: time is checked before the area.
	EChallengeEndReason Reason = EChallengeEndReason::Completed;
	TestTrue(TEXT("Both at once"), UChallengeRules::CheckFailure(61.f, 60.f, FVector(20000.f, 0.f, 0.f), FVector::ZeroVector, 10000.f, Reason));
	TestEqual(TEXT("Time wins"), Reason, EChallengeEndReason::TimeUp);

	// A traversal's last ring on the frame the clock runs out still counts.
	UChallengeDefinition* Route = HawkeyeChallengeTest::MakeTraversal(1, 10.f);
	Tracker->StartRun(Route);
	Tracker->Advance(10.f, Route->Checkpoints[0].GetLocation());
	TestEqual(TEXT("Through the ring at the buzzer"), Tracker->GetLastResult().Reason, EChallengeEndReason::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeBestPersists, "Hawkeye.Challenge.BestRecordPersists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeBestPersists::RunTest(const FString& Parameters)
{
	UChallengeTracker* Tracker = NewObject<UChallengeTracker>();
	UChallengeDefinition* Route = HawkeyeChallengeTest::MakeTraversal(2);
	UChallengeDefinition* Range = HawkeyeChallengeTest::MakeArchery(2);

	Tracker->StartRun(Route);
	HawkeyeChallengeTest::RunRoute(Tracker, Route, 100.f);
	TestTrue(TEXT("First run is a best"), Tracker->GetLastResult().bNewBest);
	Tracker->StartRun(Route);
	HawkeyeChallengeTest::RunRoute(Tracker, Route, 110.f);
	TestFalse(TEXT("A slower run is not"), Tracker->GetLastResult().bNewBest);
	TestEqual(TEXT("The best stays the faster"), Tracker->GetRecord(Route->Id).BestSeconds, 100.f, 0.01f);
	Tracker->StartRun(Route);
	HawkeyeChallengeTest::RunRoute(Tracker, Route, 55.f);
	TestTrue(TEXT("A faster one is"), Tracker->GetLastResult().bNewBest);
	TestEqual(TEXT("Best medal gold"), Tracker->GetRecord(Route->Id).BestMedal, EChallengeMedal::Gold);
	TestEqual(TEXT("Three completions"), Tracker->GetRecord(Route->Id).Completions, 3);

	Tracker->StartRun(Range);
	Tracker->RegisterHit(0, 0.f);
	Tracker->RegisterHit(1, 20.f);
	Tracker->StartRun(Range);
	Tracker->RegisterHit(0, 20.f);
	Tracker->RegisterHit(1, 20.f);
	TestFalse(TEXT("A lower score is not a best"), Tracker->GetLastResult().bNewBest);
	TestEqual(TEXT("The best score stays 12"), Tracker->GetRecord(Range->Id).BestScore, 12);

	// Into the campaign state as the save stores it, and back into a fresh tracker.
	UHawkeyeCampaignState* Campaign = NewObject<UHawkeyeCampaignState>();
	Tracker->ExportRecords(Campaign->ChallengeIds, Campaign->ChallengeBestScores, Campaign->ChallengeBestSeconds,
		Campaign->ChallengeBestMedals, Campaign->ChallengeCompletions);
	TestEqual(TEXT("Two challenges saved"), Campaign->ChallengeIds.Num(), 2);
	UChallengeTracker* Loaded = NewObject<UChallengeTracker>();
	Loaded->ImportRecords(Campaign->ChallengeIds, Campaign->ChallengeBestScores, Campaign->ChallengeBestSeconds,
		Campaign->ChallengeBestMedals, Campaign->ChallengeCompletions);
	const FChallengeRecord Time = Loaded->GetRecord(Route->Id);
	const FChallengeRecord Score = Loaded->GetRecord(Range->Id);
	TestEqual(TEXT("Loaded best time"), Time.BestSeconds, 55.f, 0.01f);
	TestEqual(TEXT("Loaded best medal"), Time.BestMedal, EChallengeMedal::Gold);
	TestEqual(TEXT("Loaded completions"), Time.Completions, 3);
	TestEqual(TEXT("Loaded best score"), Score.BestScore, 12);
	TestEqual(TEXT("Loaded archery completions"), Score.Completions, 2);

	// A loaded best is still the line to beat.
	Loaded->StartRun(Route);
	HawkeyeChallengeTest::RunRoute(Loaded, Route, 70.f);
	TestFalse(TEXT("70 s does not beat a loaded 55 s"), Loaded->GetLastResult().bNewBest);

	Campaign->ResetCampaign();
	TestEqual(TEXT("A new game forgets the bests"), Campaign->ChallengeIds.Num(), 0);
	Loaded->ImportRecords(Campaign->ChallengeIds, Campaign->ChallengeBestScores, Campaign->ChallengeBestSeconds,
		Campaign->ChallengeBestMedals, Campaign->ChallengeCompletions);
	TestFalse(TEXT("And so does the tracker"), Loaded->GetRecord(Route->Id).HasCompleted());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeObjectiveSecondaryMarkers, "Hawkeye.Objective.SecondaryMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeObjectiveSecondaryMarkers::RunTest(const FString& Parameters)
{
	UMissionTracker* Tracker = NewObject<UMissionTracker>();
	TestFalse(TEXT("None to start with"), Tracker->HasSecondaryMarkers());

	Tracker->SetSecondaryMarkers(TEXT("challenge"), { FVector(1.f, 0.f, 0.f), FVector(2.f, 0.f, 0.f) });
	Tracker->SetSecondaryMarkers(TEXT("other"), { FVector(3.f, 0.f, 0.f) });
	TArray<FVector> Points = Tracker->GetSecondaryMarkers();
	TestEqual(TEXT("Both sources' points"), Points.Num(), 3);
	TestEqual(TEXT("In the order the sources were set"), Points.Num() == 3 ? Points[2].X : 0.0, 3.0);

	Tracker->SetSecondaryMarkers(TEXT("challenge"), { FVector(5.f, 0.f, 0.f) });
	Points = Tracker->GetSecondaryMarkers();
	TestEqual(TEXT("Setting a source replaces its points"), Points.Num(), 2);
	TestEqual(TEXT("And keeps its place"), Points.Num() == 2 ? Points[0].X : 0.0, 5.0);

	// Kept across a mission change, like registered points.
	UMissionDefinition* Mission = NewObject<UMissionDefinition>();
	UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
	Objective->ObjectiveId = TEXT("reach_roof");
	Mission->Objectives.Add(Objective);
	Tracker->StartMission(Mission);
	TestEqual(TEXT("A mission start keeps them"), Tracker->GetSecondaryMarkers().Num(), 2);

	Tracker->SetSecondaryMarkers(TEXT("challenge"), {});
	TestEqual(TEXT("An empty list clears a source"), Tracker->GetSecondaryMarkers().Num(), 1);
	Tracker->ClearSecondaryMarkers(TEXT("other"));
	TestFalse(TEXT("Clearing the last leaves none"), Tracker->HasSecondaryMarkers());
	Tracker->ClearSecondaryMarkers(TEXT("never_set"));
	TestFalse(TEXT("Clearing an unknown source is harmless"), Tracker->HasSecondaryMarkers());

	Tracker->SetSecondaryMarkers(TEXT("challenge"), { FVector::ZeroVector });
	Tracker->Reset();
	TestFalse(TEXT("Reset drops them"), Tracker->HasSecondaryMarkers());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeWorldRun, "Hawkeye.Challenge.WorldRunCalmsThugsAndMarks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeWorldRun::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UChallengeSubsystem* Challenges = World ? World->GetSubsystem<UChallengeSubsystem>() : nullptr;
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	if (!Challenges || !Missions)
	{
		AddError(TEXT("The test world has no challenge or mission subsystem."));
		return false;
	}
	Challenges->bShowResultsCard = false;

	UChallengeDefinition* Range = HawkeyeChallengeTest::MakeArchery(2);
	AChallengeStart* Start = Cast<AChallengeStart>(TestWorld.SpawnActor(AChallengeStart::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	APawn* Player = Cast<APawn>(TestWorld.SpawnActor(ADefaultPawn::StaticClass(), FVector(150.f, 0.f, 100.f), FRotator::ZeroRotator));
	auto SpawnThug = [&TestWorld](const FVector& At)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator::ZeroRotator));
		AThugAIController* Brain = Cast<AThugAIController>(TestWorld.SpawnActor(AThugAIController::StaticClass(), At, FRotator::ZeroRotator));
		if (Thug && Brain)
		{
			Brain->Possess(Thug);
		}
		return Brain;
	};
	AThugAIController* Near = SpawnThug(FVector(5000.f, 0.f, 0.f));
	AThugAIController* Far = SpawnThug(FVector(7000.f, 0.f, 0.f));
	if (!Start || !Player || !Near || !Far)
	{
		AddError(TEXT("Could not spawn the pedestal, the player or the thugs."));
		return false;
	}
	Start->Definition = Range;
	Near->ReportStimulus(EStimulusKind::Hearing, FVector::ZeroVector, true, 3.f);
	TestEqual(TEXT("The near thug is alerted before the run"), Near->GetAlertState(), EThugAlertState::Alerted);

	TestTrue(TEXT("The run starts"), Challenges->StartChallenge(Start, Player));
	TestTrue(TEXT("It is running"), Challenges->IsRunning());
	TestEqual(TEXT("Both targets spawned"), Challenges->GetTargets().Num(), 2);
	TestEqual(TEXT("One thug within 60 m calmed"), Challenges->GetCalmedThugCount(), 1);
	TestTrue(TEXT("The near thug is pacified"), Near->IsPacified());
	TestEqual(TEXT("And calm"), Near->GetAlertState(), EThugAlertState::Calm);
	TestFalse(TEXT("The far one is not"), Far->IsPacified());
	Near->ReportStimulus(EStimulusKind::Hearing, FVector::ZeroVector, true, 3.f);
	TestEqual(TEXT("A gunshot does not wake a calmed thug"), Near->GetAlertState(), EThugAlertState::Calm);
	Near->ReportStimulus(EStimulusKind::Sight, Player->GetActorLocation(), true);
	Near->Think(1.f);
	TestEqual(TEXT("Nor does seeing her"), Near->GetAlertState(), EThugAlertState::Calm);
	TestEqual(TEXT("Both targets marked"), Missions->GetSecondaryMarkers().Num(), 2);

	AChallengeTarget* First = Challenges->GetTargets()[0].Get();
	AChallengeTarget* Second = Challenges->GetTargets()[1].Get();
	First->HandleArrowHit(First->GetFaceCentre(), Near->GetPawn());
	TestFalse(TEXT("Someone else's arrow does not score"), First->IsDown());
	First->HandleArrowHit(First->GetFaceCentre() + First->GetActorRightVector() * 12.f, Player);
	TestTrue(TEXT("Hers does"), First->IsDown());
	TestEqual(TEXT("For the ring"), Challenges->GetTracker()->GetScore(), 5);
	TestEqual(TEXT("One target still marked"), Missions->GetSecondaryMarkers().Num(), 1);
	Second->HandleArrowHit(Second->GetFaceCentre(), Player);
	TestFalse(TEXT("The last target ends it"), Challenges->IsRunning());
	TestEqual(TEXT("Score 15"), Challenges->GetRecord(Range->Id).BestScore, 15);
	TestEqual(TEXT("Markers cleared"), Missions->GetSecondaryMarkers().Num(), 0);
	TestFalse(TEXT("The thug has his senses back"), Near->IsPacified());
	Near->ReportStimulus(EStimulusKind::Hearing, FVector::ZeroVector, true, 3.f);
	TestEqual(TEXT("And hears a gunshot again"), Near->GetAlertState(), EThugAlertState::Alerted);

	// Retry puts her back at the pedestal and runs it again.
	Player->SetActorLocation(FVector(3000.f, 3000.f, 100.f));
	TestTrue(TEXT("Retry starts a run"), Challenges->RetryLastChallenge());
	TestTrue(TEXT("From the pedestal"), FVector::Dist2D(Player->GetActorLocation(), Start->GetStandLocation()) < 10.f);
	Challenges->AbortChallenge();
	TestFalse(TEXT("Abort ends it"), Challenges->IsRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeChallengeHudText, "Hawkeye.Challenge.HudText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeChallengeHudText::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Clock format"), UChallengeRules::FormatSeconds(65.37f).ToString(), FString(TEXT("1:05.3")));
	TestEqual(TEXT("Never below zero"), UChallengeRules::FormatSeconds(-3.f).ToString(), FString(TEXT("0:00.0")));

	UChallengeTracker* Tracker = NewObject<UChallengeTracker>();
	UChallengeDefinition* Route = HawkeyeChallengeTest::MakeTraversal(8);
	Tracker->StartRun(Route);
	Tracker->Advance(2.f, Route->Checkpoints[0].GetLocation());
	FText Title, Clock, Progress, Best;
	UChallengePanelWidget::FormatLines(Tracker, Title, Clock, Progress, Best);
	TestEqual(TEXT("Panel title is the name"), Title.ToString(), FString(TEXT("[Traversal test]")));
	TestEqual(TEXT("Panel clock is the time left"), Clock.ToString(), FString(TEXT("2:28.0")));
	TestEqual(TEXT("Panel shows the checkpoint being run to"), Progress.ToString(), FString(TEXT("Checkpoint 2/8")));
	TestEqual(TEXT("No best yet"), Best.ToString(), FString(TEXT("Best -")));

	Tracker->AbortRun();
	Tracker->StartRun(Route);
	HawkeyeChallengeTest::RunRoute(Tracker, Route, 58.f);
	const TArray<FText> Lines = UChallengeResultsWidget::FormatLines(Tracker->GetLastResult());
	TestEqual(TEXT("Six lines"), Lines.Num(), 6);
	if (Lines.Num() == 6)
	{
		TestEqual(TEXT("Outcome"), Lines[1].ToString(), FString(TEXT("COMPLETE")));
		TestEqual(TEXT("Time"), Lines[2].ToString(), FString(TEXT("Time 0:58.0")));
		TestEqual(TEXT("Checkpoints"), Lines[3].ToString(), FString(TEXT("Checkpoints 8/8")));
		TestEqual(TEXT("Medal"), Lines[4].ToString(), FString(TEXT("Gold")));
		TestEqual(TEXT("New best"), Lines[5].ToString(), FString(TEXT("New best 0:58.0 (Gold)")));
	}
	return true;
}

#endif
