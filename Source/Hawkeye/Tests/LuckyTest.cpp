#include "Misc/AutomationTest.h"
#include "World/LuckyCharacter.h"
#include "World/LuckyRules.h"

#if WITH_DEV_AUTOMATION_TESTS

/** gameplay-semantics.md, "Lucky": when he follows, walks, trots, stops and sits. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLuckyState, "Hawkeye.Lucky.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLuckyState::RunTest(const FString& Parameters)
{
	const FLuckyFollowSettings S;   // 1200 notice, 250 height, 350 start, 200 stop, 500 trot, 3 s sit
	auto Choose = [&S](ELuckyState Current, float Distance, float Still = 0.f, float Height = 0.f)
	{
		return FLuckyRules::ChooseState(S, Current, Distance, Height, Still);
	};
	auto Is = [this](const TCHAR* What, ELuckyState Got, ELuckyState Want)
	{
		TestEqual(What, static_cast<int32>(Got), static_cast<int32>(Want));
	};

	Is(TEXT("13 m away: he stays put"), Choose(ELuckyState::Idle, 1300.f), ELuckyState::Idle);
	Is(TEXT("13 m away while trotting: he gives up and stands"), Choose(ELuckyState::Trot, 1300.f), ELuckyState::Idle);
	Is(TEXT("13 m away sitting: he stays sat"), Choose(ELuckyState::Sit, 1300.f), ELuckyState::Sit);
	Is(TEXT("11 m away: he trots after her"), Choose(ELuckyState::Idle, 1100.f), ELuckyState::Trot);
	Is(TEXT("on a roof 4 m up, 5 m away: out of reach"), Choose(ELuckyState::Idle, 500.f, 0.f, 400.f), ELuckyState::Idle);
	Is(TEXT("4 m away from standing: he walks"), Choose(ELuckyState::Idle, 400.f), ELuckyState::Walk);
	Is(TEXT("3 m away from standing: inside the start distance, he stays"), Choose(ELuckyState::Idle, 300.f), ELuckyState::Idle);
	Is(TEXT("3 m away while walking: he keeps walking (hysteresis)"), Choose(ELuckyState::Walk, 300.f), ELuckyState::Walk);
	Is(TEXT("trotting, now 4 m: he slows to a walk"), Choose(ELuckyState::Trot, 400.f), ELuckyState::Walk);
	Is(TEXT("walking, now 6 m: he trots"), Choose(ELuckyState::Walk, 600.f), ELuckyState::Trot);
	Is(TEXT("walking, arrives at 1.9 m, she moves: he stands"), Choose(ELuckyState::Walk, 190.f, 0.5f), ELuckyState::Idle);
	Is(TEXT("walking, arrives at 1.9 m, she has stood 4 s: he sits"), Choose(ELuckyState::Walk, 190.f, 4.f), ELuckyState::Sit);
	Is(TEXT("standing by her, she has stood 2 s: not yet"), Choose(ELuckyState::Idle, 250.f, 2.f), ELuckyState::Idle);
	Is(TEXT("standing by her, she has stood 3 s: he sits"), Choose(ELuckyState::Idle, 250.f, 3.f), ELuckyState::Sit);
	Is(TEXT("sitting, she moves but stays near: he gets up"), Choose(ELuckyState::Sit, 250.f, 0.f), ELuckyState::Idle);
	Is(TEXT("sitting, she walks off to 6 m: he trots after her"), Choose(ELuckyState::Sit, 600.f, 0.f), ELuckyState::Trot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLuckySpeed, "Hawkeye.Lucky.Speed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLuckySpeed::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("at the clip's own speed the clip plays at 1"), FLuckyRules::PlayRateFor(92.f, 92.f, 0.35f, 1.6f), 1.f, 0.001f);
	TestEqual(TEXT("at 1.5 times its speed, 1.5"), FLuckyRules::PlayRateFor(138.f, 92.f, 0.35f, 1.6f), 1.5f, 0.001f);
	TestEqual(TEXT("never past the top rate"), FLuckyRules::PlayRateFor(400.f, 92.f, 0.35f, 1.6f), 1.6f, 0.001f);
	TestEqual(TEXT("never under the bottom rate"), FLuckyRules::PlayRateFor(0.f, 92.f, 0.35f, 1.6f), 0.35f, 0.001f);
	TestEqual(TEXT("a clip with no speed plays at 1"), FLuckyRules::PlayRateFor(50.f, 0.f, 0.35f, 1.6f), 1.f, 0.001f);

	// His top speeds are the clips' speeds times their top rates, so at full speed the feet keep pace.
	const ALuckyCharacter* Lucky = GetDefault<ALuckyCharacter>();
	TestEqual(TEXT("walking speed = walk clip x walk rate"), Lucky->SpeedFor(ELuckyState::Walk), Lucky->WalkClipSpeed * Lucky->WalkRate, 0.01f);
	TestEqual(TEXT("trotting speed = trot clip x trot rate"), Lucky->SpeedFor(ELuckyState::Trot), Lucky->TrotClipSpeed * Lucky->TrotRate, 0.01f);
	TestEqual(TEXT("idle does not move"), Lucky->SpeedFor(ELuckyState::Idle), 0.f);
	TestEqual(TEXT("sitting does not move"), Lucky->SpeedFor(ELuckyState::Sit), 0.f);
	TestTrue(TEXT("he trots faster than he walks"), Lucky->SpeedFor(ELuckyState::Trot) > Lucky->SpeedFor(ELuckyState::Walk));
	TestTrue(TEXT("he stops inside the distance he sets off at"), Lucky->Follow.StopDistance < Lucky->Follow.StartFollowDistance);
	return true;
}

#endif
