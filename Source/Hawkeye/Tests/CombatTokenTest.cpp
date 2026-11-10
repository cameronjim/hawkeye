#include "Combat/CombatTokenRules.h"
#include "Combat/CombatTokenSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The attack tokens (gameplay-semantics.md, "Attack tokens"): the pure rules, the subsystem's budgets and leases
 * on bare characters, and fights between real thugs and Kate in a test world stepped by hand.
 */
namespace HawkeyeTokenTest
{
	static constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static FHawkeyeSettings On(EHawkeyeDifficulty Difficulty)
	{
		FHawkeyeSettings Settings;
		Settings.Difficulty = Difficulty;
		return Settings;
	}

	static FHawkeyeTokenRequest Request(float Distance, float ViewAngle, float Wait, double LastGrant = -1.0)
	{
		FHawkeyeTokenRequest Out;
		Out.Distance = Distance;
		Out.ViewAngleDegrees = ViewAngle;
		Out.WaitSeconds = Wait;
		Out.LastGrantSeconds = LastGrant;
		return Out;
	}

	/** The ranking as "1,0", so a failure prints the order it got. */
	static FString Rank(const TArray<FHawkeyeTokenRequest>& Requests)
	{
		TArray<int32> Order;
		UHawkeyeCombatTokenRules::RankRequests(Requests, Order);
		return FString::JoinBy(Order, TEXT(","), [](int32 Index) { return FString::FromInt(Index); });
	}

	static ACharacter* SpawnBody(const FHawkeyeTestWorld& TestWorld, const FVector& At, float Yaw = 0.f)
	{
		return Cast<ACharacter>(TestWorld.SpawnActor(ACharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
	}

	/** A possessed, alerted thug on Target who keeps her however long the test runs without perception. */
	static AThugAIController* SpawnAlertedThug(
		const FHawkeyeTestWorld& TestWorld, const FVector& At, EThugWeapon Weapon, AActor* Target)
	{
		const FRotator Facing = (Target->GetActorLocation() - At).Rotation();
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, Facing));
		AThugAIController* Brain =
			Cast<AThugAIController>(TestWorld.SpawnActor(AThugAIController::StaticClass(), At, FRotator::ZeroRotator));
		if (!Thug || !Brain)
		{
			return nullptr;
		}
		Thug->Weapon = Weapon;
		if (AController* Existing = Thug->GetController())
		{
			Existing->UnPossess();
		}
		Brain->Possess(Thug);
		Brain->LoseTargetSeconds = 1000.f;
		Brain->SetTarget(Target);
		Brain->ReportStimulus(
			EStimulusKind::Hearing, Target->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
		return Brain;
	}

	/** An invulnerable Kate at the origin looking down +X, the way a test fight wants her. */
	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Kate && Kate->GetHealthComponent())
		{
			Kate->GetHealthComponent()->SetInvulnerable(true);
		}
		return Kate;
	}

	/** What a stepped fight saw. */
	struct FFightLog
	{
		int32 MostMeleeAtOnce = 0;
		int32 MeleeAndRangedOverlaps = 0;
		TMap<const AThugCharacter*, int32> SwingsBy;
		int32 OffScreenSwings = 0;
		float ShortestOffScreenWindup = TNumericLimits<float>::Max();
	};

	static bool IsRangedAttacking(const AThugAIController* Brain)
	{
		return Brain->IsBursting();
	}

	/**
	 * Steps the world's clock, every brain's think and every thug's swing by Dt for Seconds, the way the game's
	 * timers and component ticks would, and records who was attacking at each instant.
	 */
	static FFightLog RunFight(const FHawkeyeTestWorld& TestWorld, const AActor* Kate,
		const TArray<AThugAIController*>& Brains, float Seconds, float Dt = 0.05f)
	{
		FFightLog Log;
		TMap<const AThugCharacter*, bool> WasSwinging;
		for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += Dt)
		{
			TestWorld.AdvanceClock(Dt);
			int32 Melee = 0;
			int32 Ranged = 0;
			for (AThugAIController* Brain : Brains)
			{
				AThugCharacter* Thug = Brain->GetThug();
				if (!Thug)
				{
					continue;
				}
				Brain->Think(Dt);
				UMeleeComponent* Swing = Thug->GetMeleeComponent();
				Swing->AdvanceAttack(Dt);
				const bool bSwinging = Swing->IsAttacking();
				if (bSwinging && !WasSwinging.FindRef(Thug))
				{
					++Log.SwingsBy.FindOrAdd(Thug);
					FVector View;
					FRotator Look;
					Kate->GetActorEyesViewPoint(View, Look);
					const float Angle =
						UHawkeyeCombatTokenRules::ComputeViewAngle(View, Look.Vector(), Thug->GetActorLocation());
					if (!UHawkeyeCombatTokenRules::IsOnScreen(Angle))
					{
						++Log.OffScreenSwings;
						Log.ShortestOffScreenWindup =
							FMath::Min(Log.ShortestOffScreenWindup, Swing->GetCurrentAttack().WindupSeconds);
					}
				}
				WasSwinging.Add(Thug, bSwinging);
				Melee += bSwinging ? 1 : 0;
				Ranged += IsRangedAttacking(Brain) ? 1 : 0;
			}
			Log.MostMeleeAtOnce = FMath::Max(Log.MostMeleeAtOnce, Melee);
			Log.MeleeAndRangedOverlaps += Melee > 0 && Ranged > 0 ? 1 : 0;
		}
		return Log;
	}

	/** Four fists thugs 200 cm from Kate: in front of her, at either side and behind. */
	static TArray<AThugAIController*> SurroundKate(const FHawkeyeTestWorld& TestWorld, AActor* Kate)
	{
		TArray<AThugAIController*> Brains;
		for (const FVector& At :
			{FVector(200.f, 0.f, 0.f), FVector(0.f, 200.f, 0.f), FVector(-200.f, 0.f, 0.f), FVector(0.f, -200.f, 0.f)})
		{
			if (AThugAIController* Brain = SpawnAlertedThug(TestWorld, At, EThugWeapon::Fists, Kate))
			{
				Brains.Add(Brain);
			}
		}
		return Brains;
	}

	static AThugAIController* FindHolder(const UCombatTokenSubsystem* Tokens, const TArray<AThugAIController*>& Brains)
	{
		for (AThugAIController* Brain : Brains)
		{
			if (Tokens->HoldsToken(Brain->GetThug()))
			{
				return Brain;
			}
		}
		return nullptr;
	}

	/** Steps the fight until somebody holds the melee token (at most MaxSeconds); him, or null. */
	static AThugAIController* RunUntilHeld(const FHawkeyeTestWorld& TestWorld, const AActor* Kate,
		const TArray<AThugAIController*>& Brains, float MaxSeconds)
	{
		const UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(TestWorld.Get());
		for (float Elapsed = 0.f; Elapsed < MaxSeconds; Elapsed += 0.05f)
		{
			if (AThugAIController* Holder = FindHolder(Tokens, Brains))
			{
				return Holder;
			}
			RunFight(TestWorld, Kate, Brains, 0.05f);
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTokensBudgetPerDifficulty, "Hawkeye.Tokens.BudgetPerDifficulty", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensBudgetPerDifficulty::RunTest(const FString& Parameters)
{
	using S = EDifficultyStat;
	const EHawkeyeDifficulty Levels[] = {
		EHawkeyeDifficulty::Story, EHawkeyeDifficulty::Normal, EHawkeyeDifficulty::Hard};
	const float Melee[] = {1.f, 1.f, 2.f};
	const float Ranged[] = {1.f, 1.f, 2.f};
	const float Gap[] = {0.9f, 0.6f, 0.4f};
	const float OffScreen[] = {1.2f, 1.0f, 0.8f};
	for (int32 Level = 0; Level < 3; ++Level)
	{
		const FString Name = UDifficultySubsystem::GetDifficultyName(Levels[Level]).ToString();
		const EDifficultyStat MeleeStat = UHawkeyeCombatTokenRules::GetBudgetStat(EHawkeyeAttackKind::Melee);
		const EDifficultyStat RangedStat = UHawkeyeCombatTokenRules::GetBudgetStat(EHawkeyeAttackKind::Ranged);
		TestEqual(TEXT("Melee tokens on ") + Name, UDifficultySubsystem::GetTableValue(Levels[Level], MeleeStat),
			Melee[Level]);
		TestEqual(TEXT("Ranged tokens on ") + Name, UDifficultySubsystem::GetTableValue(Levels[Level], RangedStat),
			Ranged[Level]);
		TestEqual(
			TEXT("Gap on ") + Name, UDifficultySubsystem::GetTableValue(Levels[Level], S::TokenGapSeconds), Gap[Level]);
		TestEqual(TEXT("Off-screen telegraph on ") + Name,
			UDifficultySubsystem::GetTableValue(Levels[Level], S::OffScreenTelegraphSeconds), OffScreen[Level]);
	}
	TestTrue(TEXT("Story is never quicker between tokens than Normal, nor Hard slower"),
		Gap[0] >= Gap[1] && Gap[2] <= Gap[1]);

	// The budget and the gap.
	TestTrue(
		TEXT("A free token, long after the last: granted"), UHawkeyeCombatTokenRules::CanGrant(1, 0, 10.0, 0.0, 0.6f));
	TestFalse(TEXT("None free: refused"), UHawkeyeCombatTokenRules::CanGrant(1, 1, 10.0, 0.0, 0.6f));
	TestTrue(TEXT("Hard's second token"), UHawkeyeCombatTokenRules::CanGrant(2, 1, 10.0, 0.0, 0.4f));
	TestFalse(
		TEXT("0.5 s after one came back: still the gap"), UHawkeyeCombatTokenRules::CanGrant(1, 0, 10.5, 10.0, 0.6f));
	TestTrue(TEXT("0.6 s after: granted"), UHawkeyeCombatTokenRules::CanGrant(1, 0, 10.6, 10.0, 0.6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTokensPriorityOrder, "Hawkeye.Tokens.PriorityOrder", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensPriorityOrder::RunTest(const FString& Parameters)
{
	using HawkeyeTokenTest::Rank;
	using HawkeyeTokenTest::Request;

	// On her screen beats off it, even off it and nearer.
	TestEqual(TEXT("On screen at 400 cm before off screen at 150"),
		Rank({Request(150.f, 120.f, 1.f), Request(400.f, 20.f, 0.f)}), TEXT("1,0"));
	// Both on screen: the nearer.
	TestEqual(TEXT("Nearer first"), Rank({Request(300.f, 10.f, 2.f), Request(200.f, 40.f, 0.f)}), TEXT("1,0"));
	// Same distance step: the longer wait.
	TestEqual(TEXT("Within one 50 cm step the longer wait wins"),
		Rank({Request(210.f, 10.f, 0.5f), Request(240.f, 10.f, 1.5f)}), TEXT("1,0"));
	TestEqual(TEXT("Off screen among themselves: nearest, then wait"),
		Rank({Request(300.f, 170.f, 3.f), Request(200.f, 90.f, 0.f), Request(200.f, 100.f, 1.f)}), TEXT("2,1,0"));
	TestEqual(TEXT("Everything equal: the order asked"), Rank({Request(200.f, 10.f, 1.f), Request(200.f, 10.f, 1.f)}),
		TEXT("0,1"));

	// Nobody starves: four seconds of waiting goes to the front, off screen or not.
	const float Overdue = UHawkeyeCombatTokenRules::OverdueSeconds;
	TestEqual(TEXT("An overdue thug behind her beats a fresh one in front"),
		Rank({Request(150.f, 0.f, 0.f), Request(300.f, 180.f, Overdue)}), TEXT("1,0"));
	TestEqual(TEXT("Two overdue: the longer"),
		Rank({Request(150.f, 0.f, Overdue), Request(300.f, 180.f, Overdue + 1.f)}), TEXT("1,0"));

	// On screen is within 50 degrees of where she looks.
	TestTrue(TEXT("50 deg is on screen"), UHawkeyeCombatTokenRules::IsOnScreen(50.f));
	TestFalse(TEXT("51 is not"), UHawkeyeCombatTokenRules::IsOnScreen(51.f));
	TestEqual(TEXT("A thug at her side is 90 deg off"),
		UHawkeyeCombatTokenRules::ComputeViewAngle(
			FVector::ZeroVector, FVector::ForwardVector, FVector(0.f, 300.f, 0.f)),
		90.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTokensBackOfQueue, "Hawkeye.Tokens.BackOfQueue", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensBackOfQueue::RunTest(const FString& Parameters)
{
	using HawkeyeTokenTest::Rank;
	using HawkeyeTokenTest::Request;

	// The one who just attacked goes behind everyone, even behind a thug off screen and further off.
	TestEqual(TEXT("The last attacker, in front and nearest, goes last"),
		Rank({Request(150.f, 0.f, 0.5f, 10.0), Request(300.f, 150.f, 0.f, 4.0), Request(250.f, 30.f, 0.f)}),
		TEXT("2,1,0"));
	// Only the most recent: the one before him is back in the usual order.
	TestEqual(TEXT("An older attacker is ranked as usual"),
		Rank({Request(150.f, 0.f, 0.f, 4.0), Request(300.f, 0.f, 0.f, 10.0)}), TEXT("0,1"));
	// Alone, he gets it again.
	TestEqual(TEXT("Alone he is first"), Rank({Request(150.f, 0.f, 0.f, 10.0)}), TEXT("0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTokensOffScreenTelegraph, "Hawkeye.Tokens.OffScreenTelegraph", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensOffScreenTelegraph::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("On screen: no minimum"), UHawkeyeCombatTokenRules::GetMinTelegraphSeconds(30.f, 1.f), 0.f);
	TestEqual(TEXT("Behind her: the minimum"), UHawkeyeCombatTokenRules::GetMinTelegraphSeconds(170.f, 1.f), 1.f);

	// The gunner's raised pistol stretches to the minimum and fires nothing before it.
	FHawkeyeBurstClock Burst;
	Burst.Start(1.f);
	TestEqual(TEXT("The telegraph is the token's minimum"), Burst.GetTelegraphSeconds(), 1.f);
	TestEqual(TEXT("No shot at 0.9 s"), Burst.Advance(0.9f), 0);
	TestTrue(TEXT("Still telegraphing"), Burst.IsTelegraphing());
	TestEqual(TEXT("The first shot at 1.0 s"), Burst.Advance(0.11f), 1);
	Burst.Cancel();
	Burst.Start(0.f);
	TestEqual(TEXT("On screen the pistol keeps its own 0.8 s"), Burst.GetTelegraphSeconds(), Burst.TelegraphSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTokensRingSlots, "Hawkeye.Tokens.RingSlots", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensRingSlots::RunTest(const FString& Parameters)
{
	TArray<float> Slots;
	UHawkeyeCombatTokenRules::SpreadRingSlots({0.f, 0.f}, 40.f, Slots);
	TestEqual(TEXT("Two on one spot split evenly either side"), Slots[0], -20.f, 0.01f);
	TestEqual(TEXT("The second the other way"), Slots[1], 20.f, 0.01f);

	const TArray<float> Apart = {10.f, 100.f, -170.f, -80.f};
	UHawkeyeCombatTokenRules::SpreadRingSlots(Apart, 40.f, Slots);
	for (int32 Index = 0; Index < Apart.Num(); ++Index)
	{
		TestEqual(TEXT("Already 90 apart: nobody moves"), Slots[Index], Apart[Index], 0.01f);
	}

	// Three bunched across the back (over the wrap at 180): 40 apart, in the order they stood, moved as little as
	// can be (the moves add up to nothing).
	UHawkeyeCombatTokenRules::SpreadRingSlots({175.f, -175.f, 170.f}, 40.f, Slots);
	TestEqual(TEXT("The middle one barely moves"), Slots[0], 176.67f, 0.05f);
	TestEqual(TEXT("The one at -175 goes round to -143"), Slots[1], -143.33f, 0.05f);
	TestEqual(TEXT("The one at 170 goes back to 137"), Slots[2], 136.67f, 0.05f);

	// More than fit: evenly round.
	UHawkeyeCombatTokenRules::SpreadRingSlots({0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f}, 40.f, Slots);
	for (int32 Index = 0; Index + 1 < Slots.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Ten thugs: %d and %d are 36 apart"), Index, Index + 1),
			FMath::UnwindDegrees(Slots[Index + 1] - Slots[Index]), 36.f, 0.5f);
	}

	// Deterministic.
	TArray<float> Again;
	UHawkeyeCombatTokenRules::SpreadRingSlots({175.f, -175.f, 170.f}, 40.f, Slots);
	UHawkeyeCombatTokenRules::SpreadRingSlots({175.f, -175.f, 170.f}, 40.f, Again);
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		TestEqual(TEXT("The same stand gives the same places"), Slots[Index], Again[Index]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTokensSubsystemBudgets, "Hawkeye.Tokens.SubsystemBudgetsAndLeases", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensSubsystemBudgets::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(TestWorld.Get());
	ACharacter* Kate = SpawnBody(TestWorld, FVector::ZeroVector);
	ACharacter* Clint = SpawnBody(TestWorld, FVector(2000.f, 0.f, 0.f));
	ACharacter* Front = SpawnBody(TestWorld, FVector(200.f, 0.f, 0.f));
	ACharacter* Side = SpawnBody(TestWorld, FVector(0.f, 250.f, 0.f));
	ACharacter* Shooter = SpawnBody(TestWorld, FVector(900.f, 100.f, 0.f));
	ACharacter* Other = SpawnBody(TestWorld, FVector(2200.f, 0.f, 0.f));
	if (!TestNotNull(TEXT("The token subsystem exists in a game world"), Tokens) || !Kate || !Clint || !Front ||
		!Side || !Shooter || !Other)
	{
		return false;
	}
	TestWorld.AdvanceClock(1.f);

	const FHawkeyeTokenGrant First = Tokens->RequestToken(Front, Kate, EHawkeyeAttackKind::Melee, true);
	TestTrue(TEXT("The thug in front is granted"), First.bGranted);
	TestEqual(TEXT("On her screen: no minimum telegraph"), First.MinTelegraphSeconds, 0.f);
	TestFalse(TEXT("A second melee thug is not (Normal: 1)"),
		Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestTrue(TEXT("A shooter is: the ranged budget is its own"),
		Tokens->RequestToken(Shooter, Kate, EHawkeyeAttackKind::Ranged, true).bGranted);
	TestTrue(
		TEXT("Clint has his own budget"), Tokens->RequestToken(Other, Clint, EHawkeyeAttackKind::Melee, true).bGranted);
	const FHawkeyeTokenBudgetView View = Tokens->GetBudgetView(Kate, EHawkeyeAttackKind::Melee);
	TestTrue(TEXT("The view: the thug in front holds it"), View.Holders.Num() == 1 && View.Holders[0] == Front);
	TestTrue(TEXT("And the one at her side waits"), View.Waiters.Num() == 1 && View.Waiters[0] == Side);
	TestEqual(TEXT("Normal's budget"), View.Budget, 1);

	// Given back: the gap before the next.
	Tokens->ReleaseToken(Front);
	TestFalse(TEXT("Nobody straight after a token comes back"),
		Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestWorld.AdvanceClock(0.5f);
	TestFalse(TEXT("Nor 0.5 s after"), Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestWorld.AdvanceClock(0.15f);
	const FHawkeyeTokenGrant Second = Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true);
	TestTrue(TEXT("0.65 s after, the thug at her side"), Second.bGranted);
	TestEqual(TEXT("From off her screen, his telegraph is at least Normal's 1.0 s"), Second.MinTelegraphSeconds, 1.f);

	// A holder who stops asking lapses; a destroyed one simply goes.
	TestWorld.AdvanceClock(UHawkeyeCombatTokenRules::LeaseSeconds + 0.1f);
	Tokens->RequestToken(Front, Kate, EHawkeyeAttackKind::Melee, true);
	TestFalse(TEXT("The silent holder lost his token"), Tokens->HoldsToken(Side));
	TestWorld.AdvanceClock(0.7f);
	TestTrue(TEXT("And the next is granted after the gap"),
		Tokens->RequestToken(Front, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	Front->Destroy();
	TestWorld.AdvanceClock(0.7f);
	Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true);
	TestWorld.AdvanceClock(0.7f);
	TestTrue(TEXT("A destroyed holder's token comes back"),
		Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true).bGranted);

	// Asking about another target withdraws him from the first.
	Tokens->RequestToken(Side, Clint, EHawkeyeAttackKind::Melee, true);
	TestEqual(TEXT("Turning on Clint gives Kate's token back"),
		Tokens->GetBudgetView(Kate, EHawkeyeAttackKind::Melee).Holders.Num(), 0);
	TestFalse(TEXT("Not ready, he is never granted"),
		Tokens->RequestToken(Shooter, Clint, EHawkeyeAttackKind::Ranged, false).bGranted);

	// Hard: two at once.
	FHawkeyeScopedSettingsOverride Hard(On(EHawkeyeDifficulty::Hard));
	ACharacter* A = SpawnBody(TestWorld, FVector(-200.f, 0.f, 0.f));
	ACharacter* B = SpawnBody(TestWorld, FVector(0.f, -200.f, 0.f));
	TestWorld.AdvanceClock(1.f);
	Tokens->ReleaseToken(Shooter);
	TestTrue(TEXT("Hard: one"), Tokens->RequestToken(A, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestTrue(TEXT("Hard: and a second"), Tokens->RequestToken(B, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTokensFourMelee, "Hawkeye.Tokens.FourMeleeTakeTurns", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensFourMelee::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTokenTest;
	for (const EHawkeyeDifficulty Difficulty : {EHawkeyeDifficulty::Normal, EHawkeyeDifficulty::Hard})
	{
		FHawkeyeScopedSettingsOverride Level(On(Difficulty));
		const FString Name = UDifficultySubsystem::GetDifficultyName(Difficulty).ToString();
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
		const TArray<AThugAIController*> Brains = Kate ? SurroundKate(TestWorld, Kate) : TArray<AThugAIController*>();
		if (!TestEqual(TEXT("Kate and four thugs spawned"), Brains.Num(), 4))
		{
			return false;
		}
		const FFightLog Fight = RunFight(TestWorld, Kate, Brains, 30.f);
		const int32 Budget = Difficulty == EHawkeyeDifficulty::Hard ? 2 : 1;
		AddInfo(FString::Printf(
			TEXT("%s: at most %d swinging at once; %d swings from off her screen, shortest wind-up %.2f s."), *Name,
			Fight.MostMeleeAtOnce, Fight.OffScreenSwings, Fight.ShortestOffScreenWindup));
		TestTrue(Name + TEXT(": never more swinging at once than the budget"), Fight.MostMeleeAtOnce <= Budget);
		TestEqual(Name + TEXT(": the budget is used"), Fight.MostMeleeAtOnce, Budget);
		for (const AThugAIController* Brain : Brains)
		{
			const int32 Swings = Fight.SwingsBy.FindRef(Brain->GetThug());
			AddInfo(FString::Printf(TEXT("%s: %s swung %d times."), *Name, *GetNameSafe(Brain->GetThug()), Swings));
			TestTrue(Name + TEXT(": every thug gets his turns (no starvation)"), Swings >= 2);
		}
		const float OffScreenMin =
			UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::OffScreenTelegraphSeconds);
		TestTrue(Name + TEXT(": thugs behind her did swing"), Fight.OffScreenSwings > 0);
		TestTrue(Name + TEXT(": and never faster than the off-screen minimum"),
			Fight.ShortestOffScreenWindup >= OffScreenMin - KINDA_SMALL_NUMBER);
	}
	return true;
}

/**
 * A waiting thug walking to his place stops short of it by the move's acceptance and his own radius (about 95 cm):
 * he still counts as on the ring. Found in token_ring.png, where four stood 396 cm out and nobody ever swung.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTokensShortOfTheRing, "Hawkeye.Tokens.ThugShortOfTheRingIsReady", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensShortOfTheRing::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugAIController* Brain =
		Kate ? SpawnAlertedThug(TestWorld, FVector(400.f, 0.f, 0.f), EThugWeapon::Fists, Kate) : nullptr;
	const UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(TestWorld.Get());
	if (!Brain || !Tokens)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	TestEqual(TEXT("The ring is 300 cm out"), Brain->MeleeWaitRingDistance, 300.f);
	Brain->Think(Brain->ThinkIntervalSeconds);
	TestTrue(
		TEXT("100 cm outside the ring he is ready, and alone he is granted"), Tokens->HoldsToken(Brain->GetThug()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTokensMeleeAndRanged, "Hawkeye.Tokens.MeleeAndRangedOverlap", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensMeleeAndRanged::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		return false;
	}
	const TArray<AThugAIController*> Brains = {
		SpawnAlertedThug(TestWorld, FVector(200.f, 0.f, 0.f), EThugWeapon::Fists, Kate),
		SpawnAlertedThug(TestWorld, FVector(150.f, 150.f, 0.f), EThugWeapon::Bat, Kate),
		SpawnAlertedThug(TestWorld, FVector(700.f, -200.f, 0.f), EThugWeapon::Pistol, Kate),
	};
	if (Brains.Contains(nullptr))
	{
		AddError(TEXT("Failed to spawn the three thugs."));
		return false;
	}
	const FFightLog Fight = RunFight(TestWorld, Kate, Brains, 20.f);
	AddInfo(FString::Printf(TEXT("Melee at once at most %d; steps with a swing and a burst together %d."),
		Fight.MostMeleeAtOnce, Fight.MeleeAndRangedOverlaps));
	TestEqual(TEXT("Two melee thugs never swing together on Normal"), Fight.MostMeleeAtOnce, 1);
	TestTrue(TEXT("A swing and a gunner's burst may overlap"), Fight.MeleeAndRangedOverlaps > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTokensNeverLeak, "Hawkeye.Tokens.StaggeredOrKilledHolderFreesIt", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensNeverLeak::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	const TArray<AThugAIController*> Brains = Kate ? SurroundKate(TestWorld, Kate) : TArray<AThugAIController*>();
	UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(TestWorld.Get());
	if (!Tokens || !TestEqual(TEXT("Kate and four thugs spawned"), Brains.Num(), 4))
	{
		return false;
	}
	const float Think = Brains[0]->ThinkIntervalSeconds;
	TestWorld.AdvanceClock(1.f);

	// Staggered mid-swing: the token is free after one think.
	AThugAIController* Holder = RunUntilHeld(TestWorld, Kate, Brains, 3.f);
	if (!TestNotNull(TEXT("Somebody holds the token"), Holder))
	{
		return false;
	}
	Holder->GetThug()->StaggerFor(Kate, 1.5f);
	TestWorld.AdvanceClock(Think);
	Holder->Think(Think);
	TestFalse(TEXT("A staggered holder gives it back within one think"), Tokens->HoldsToken(Holder->GetThug()));

	// Killed: the same.
	Holder = RunUntilHeld(TestWorld, Kate, Brains, 3.f);
	if (!TestNotNull(TEXT("Somebody else holds it"), Holder))
	{
		return false;
	}
	Holder->GetThug()->GetHealthComponent()->ApplyDamage(1000.f, Kate);
	TestWorld.AdvanceClock(Think);
	Holder->Think(Think);
	TestFalse(TEXT("A killed holder gives it back within one think"), Tokens->HoldsToken(Holder->GetThug()));

	// Kate going down takes it from everyone: the stand-off.
	TestNotNull(TEXT("The fight goes on with a new holder"), RunUntilHeld(TestWorld, Kate, Brains, 3.f));
	Kate->GetHealthComponent()->SetInvulnerable(false);
	Kate->GetHealthComponent()->ApplyDamage(1000.f, nullptr);
	for (AThugAIController* Brain : Brains)
	{
		Brain->Think(Think);
	}
	TestNull(TEXT("Nobody holds a token on a downed Kate"), FindHolder(Tokens, Brains));
	return true;
}

#endif
