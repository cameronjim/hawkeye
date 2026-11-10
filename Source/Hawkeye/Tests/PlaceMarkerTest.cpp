#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"
#include "World/TimeOfDaySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Finding the safehouses and challenges (claude-docs/gameplay-semantics.md, "Safehouses and fast travel"
 * and "Side challenges"): "Mark nearest" works before anything is found, and the first-time "nearby"
 * checks pick the right place once.
 */
namespace HawkeyePlaceMarkerTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static ASafehouse* SpawnSafehouse(const FHawkeyeTestWorld& TestWorld, FName Id, const FVector& At)
	{
		ASafehouse* Safehouse = Cast<ASafehouse>(TestWorld.SpawnActor(ASafehouse::StaticClass(), At, FRotator::ZeroRotator));
		if (Safehouse)
		{
			Safehouse->SafehouseId = Id;
			Safehouse->DisplayName = FText::FromName(Id);
		}
		return Safehouse;
	}

	static AChallengeStart* SpawnStart(const FHawkeyeTestWorld& TestWorld, FName Id, const FVector& At)
	{
		AChallengeStart* Start = Cast<AChallengeStart>(TestWorld.SpawnActor(AChallengeStart::StaticClass(), At, FRotator::ZeroRotator));
		if (Start)
		{
			UChallengeDefinition* Definition = NewObject<UChallengeDefinition>(Start);
			Definition->Id = Id;
			Start->Definition = Definition;
		}
		return Start;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMarkUnfoundSafehouse, "Hawkeye.FastTravel.MarkNearestEvenUnfound", HawkeyePlaceMarkerTest::Flags)

bool FHawkeyeMarkUnfoundSafehouse::RunTest(const FString& Parameters)
{
	using namespace HawkeyePlaceMarkerTest;
	const FHawkeyeTestWorld TestWorld;
	ASafehouse* Near = SpawnSafehouse(TestWorld, TEXT("sh_near"), FVector(3000.f, 0.f, 0.f));
	ASafehouse* Far = SpawnSafehouse(TestWorld, TEXT("sh_far"), FVector(30000.f, 0.f, 0.f));
	USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(TestWorld.Get());
	UMissionSubsystem* Missions = UMissionSubsystem::Get(TestWorld.Get());
	if (!Near || !Far || !TestNotNull(TEXT("Safehouse subsystem"), Safehouses) || !TestNotNull(TEXT("Mission subsystem"), Missions))
	{
		return false;
	}

	// Nothing found yet (this world has no save): the nearest is marked all the same.
	TestNull(TEXT("None found yet"), Safehouses->FindNearestDiscovered(FVector::ZeroVector));
	TestEqual(TEXT("The nearest of all of them"), Safehouses->FindNearestAny(FVector::ZeroVector), Near);
	TestEqual(TEXT("Mark nearest marks it though it is not found"), Safehouses->MarkNearestSafehouse(FVector::ZeroVector), Near);
	TestEqual(TEXT("It is the marked one"), Safehouses->GetMarkedSafehouse(), Near);
	TestEqual(TEXT("A secondary marker went up over it"), Missions->GetSecondaryMarkers().Num(), 1);
	TestEqual(TEXT("Unfound, it is the unknown safehouse"), USafehouseSubsystem::GetMarkerName(Near, false).ToString(),
		FString(TEXT("[Unknown safehouse]")));
	TestEqual(TEXT("Found, it has its name"), USafehouseSubsystem::GetMarkerName(Near, true).ToString(), FString(TEXT("sh_near")));
	TestEqual(TEXT("From the far side the far one is nearest"), Safehouses->MarkNearestSafehouse(FVector(29000.f, 0.f, 0.f)), Far);
	TestEqual(TEXT("Still the one marker"), Missions->GetSecondaryMarkers().Num(), 1);

	// The nearby toast: within 60 m, not found and not announced before.
	TestEqual(TEXT("The toast radius is 60 m"), Safehouses->NoticeRadius, 6000.f);
	TestNull(TEXT("61 m out: nothing"), Safehouses->FindUnnoticed(FVector(3000.f - 6100.f, 0.f, 0.f), Safehouses->NoticeRadius, {}, {}));
	TestEqual(TEXT("59 m out: that one"), Safehouses->FindUnnoticed(FVector(3000.f - 5900.f, 0.f, 0.f), Safehouses->NoticeRadius, {}, {}), Near);
	TestNull(TEXT("Not once it is found"), Safehouses->FindUnnoticed(FVector::ZeroVector, Safehouses->NoticeRadius, { TEXT("sh_near") }, {}));
	TestNull(TEXT("Not twice"), Safehouses->FindUnnoticed(FVector::ZeroVector, Safehouses->NoticeRadius, {}, { TEXT("sh_near") }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMarkChallenge, "Hawkeye.Challenge.MarkNearestAndNotice", HawkeyePlaceMarkerTest::Flags)

bool FHawkeyeMarkChallenge::RunTest(const FString& Parameters)
{
	using namespace HawkeyePlaceMarkerTest;
	const FHawkeyeTestWorld TestWorld;
	AChallengeStart* Archery = SpawnStart(TestWorld, TEXT("archery_test"), FVector(2000.f, 0.f, 0.f));
	AChallengeStart* Route = SpawnStart(TestWorld, TEXT("traversal_test"), FVector(-9000.f, 0.f, 0.f));
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(TestWorld.Get());
	UMissionSubsystem* Missions = UMissionSubsystem::Get(TestWorld.Get());
	if (!Archery || !Route || !TestNotNull(TEXT("Challenge subsystem"), Challenges) || !TestNotNull(TEXT("Mission subsystem"), Missions))
	{
		return false;
	}
	TestEqual(TEXT("Both pedestals are found"), Challenges->GetStarts().Num(), 2);
	TestEqual(TEXT("Mark nearest challenge marks the near one"), Challenges->MarkNearestChallenge(FVector::ZeroVector), Archery);
	TestEqual(TEXT("It is the marked one"), Challenges->GetMarkedStart(), Archery);
	TestEqual(TEXT("A secondary marker over it"), Missions->GetSecondaryMarkers().Num(), 1);
	Challenges->ClearChallengeMarker();
	TestNull(TEXT("Cleared"), Challenges->GetMarkedStart());
	TestEqual(TEXT("And its marker is gone"), Missions->GetSecondaryMarkers().Num(), 0);

	TestEqual(TEXT("The toast radius is 40 m"), Challenges->NoticeRadius, 4000.f);
	TestEqual(TEXT("20 m from one: that one"), Challenges->FindUnnoticedStart(FVector::ZeroVector, Challenges->NoticeRadius, {}), Archery);
	TestNull(TEXT("Announced already: nothing"), Challenges->FindUnnoticedStart(FVector::ZeroVector, Challenges->NoticeRadius,
		{ TEXT("archery_test") }));
	TestNull(TEXT("41 m from the nearest: nothing"), Challenges->FindUnnoticedStart(FVector(2000.f + 4100.f, 0.f, 0.f),
		Challenges->NoticeRadius, {}));

	// The glow: purple, not a pale lilac that clips to white, and brighter by day.
	TestTrue(TEXT("The cap's night glow stays under the clip"), Archery->CapGlow <= 1.f);
	TestTrue(TEXT("Brighter by day"), UTimeOfDaySubsystem::GetPreset(EHawkeyeTimeOfDay::Day).GlowScale > 1.f);
	return true;
}

#endif
