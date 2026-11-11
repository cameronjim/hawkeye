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
		/** Two swinging at once: the narrowest angle between them round her, degrees. */
		float NarrowestPairDegrees = 360.f;
		/** The shortest time between two thugs' blows landing (each wind-up's end), seconds. */
		float ShortestImpactGap = TNumericLimits<float>::Max();
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
		TMap<const AThugCharacter*, bool> WasWindingUp;
		const AThugCharacter* LastImpactBy = nullptr;
		double LastImpactAt = -1.0;
		for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += Dt)
		{
			TestWorld.AdvanceClock(Dt);
			int32 Melee = 0;
			int32 Ranged = 0;
			TArray<float> SwingBearings;
			for (AThugAIController* Brain : Brains)
			{
				AThugCharacter* Thug = Brain->GetThug();
				if (!Thug)
				{
					continue;
				}
				ThinkThug(Brain, Dt);
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
				// A wind-up ending with the swing still on is its blow landing.
				const bool bWindingUp = Swing->IsWindingUp();
				if (WasWindingUp.FindRef(Thug) && !bWindingUp && bSwinging)
				{
					const double Now = TestWorld.Get()->GetTimeSeconds();
					if (LastImpactBy && LastImpactBy != Thug)
					{
						Log.ShortestImpactGap = FMath::Min(Log.ShortestImpactGap, static_cast<float>(Now - LastImpactAt));
					}
					LastImpactBy = Thug;
					LastImpactAt = Now;
				}
				WasWindingUp.Add(Thug, bWindingUp);
				if (bSwinging)
				{
					SwingBearings.Add((Thug->GetActorLocation() - Kate->GetActorLocation()).Rotation().Yaw);
				}
				Melee += bSwinging ? 1 : 0;
				Ranged += IsRangedAttacking(Brain) ? 1 : 0;
			}
			Log.MostMeleeAtOnce = FMath::Max(Log.MostMeleeAtOnce, Melee);
			for (int32 A = 0; A < SwingBearings.Num(); ++A)
			{
				for (int32 B = A + 1; B < SwingBearings.Num(); ++B)
				{
					Log.NarrowestPairDegrees = FMath::Min(Log.NarrowestPairDegrees,
						FMath::Abs(FMath::FindDeltaAngleDegrees(SwingBearings[A], SwingBearings[B])));
				}
			}
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
	const float Melee[] = {1.f, 2.f, 3.f};
	const float Ranged[] = {1.f, 1.f, 2.f};
	const float Gap[] = {0.9f, 0.45f, 0.3f};
	const float OffScreen[] = {1.2f, 1.0f, 0.8f};
	const float Share[] = {0.5f, 0.5f, 0.5f};
	const float Separation[] = {90.f, 60.f, 45.f};
	const float Interval[] = {0.6f, 0.45f, 0.35f};
	const float Grace[] = {0.8f, 0.6f, 0.45f};
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
		TestEqual(TEXT("Group share on ") + Name, UDifficultySubsystem::GetTableValue(Levels[Level], S::TokenGroupShare),
			Share[Level]);
		TestEqual(TEXT("Melee separation on ") + Name,
			UDifficultySubsystem::GetTableValue(Levels[Level], S::MeleeSeparationDegrees), Separation[Level]);
		TestEqual(TEXT("Impact interval on ") + Name,
			UDifficultySubsystem::GetTableValue(Levels[Level], S::ImpactIntervalSeconds), Interval[Level]);
		TestEqual(TEXT("Hit grace on ") + Name, UDifficultySubsystem::GetTableValue(Levels[Level], S::HitGraceSeconds),
			Grace[Level]);
		// The grace outlasts her stagger, so a second blow never lands while she is still reeling from the first.
		TestTrue(TEXT("The grace outlasts her 0.35 s stagger on ") + Name, Grace[Level] > 0.35f);
	}
	TestTrue(TEXT("Story stays gentle: one melee and one ranged"), Melee[0] == 1.f && Ranged[0] == 1.f);
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
	TestFalse(TEXT("A second melee thug is not: two thugs share one token on Normal"),
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
	TestWorld.AdvanceClock(0.3f);
	TestFalse(TEXT("Nor 0.3 s after (Normal's gap is 0.45 s)"),
		Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestWorld.AdvanceClock(0.2f);
	const FHawkeyeTokenGrant Second = Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, true);
	TestTrue(TEXT("0.5 s after, the thug at her side"), Second.bGranted);
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

	// Hard: two at once out of three, from different sides.
	FHawkeyeScopedSettingsOverride Hard(On(EHawkeyeDifficulty::Hard));
	ACharacter* A = SpawnBody(TestWorld, FVector(-200.f, 0.f, 0.f));
	ACharacter* B = SpawnBody(TestWorld, FVector(0.f, -200.f, 0.f));
	ACharacter* C = SpawnBody(TestWorld, FVector(0.f, 200.f, 0.f));
	TestWorld.AdvanceClock(1.f);
	Tokens->ReleaseToken(Shooter);
	Tokens->RequestToken(C, Kate, EHawkeyeAttackKind::Melee, false);
	TestTrue(TEXT("Hard: one"), Tokens->RequestToken(A, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestTrue(TEXT("Hard: and a second"), Tokens->RequestToken(B, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestEqual(
		TEXT("Hard: three attackers share two tokens"), Tokens->GetBudgetView(Kate, EHawkeyeAttackKind::Melee).Budget, 2);
	const FHawkeyeTokenPressure Pressure = Tokens->GetPressure(Kate);
	TestEqual(TEXT("The pressure count saw two melee holders at once"), Pressure.PeakMeleeHolders, 2);
	TestTrue(TEXT("And a grant that overlapped another"), Pressure.OverlappingMeleeGrants >= 1);
	Tokens->ResetPressure(Kate);
	TestEqual(TEXT("Reset starts the count again"), Tokens->GetPressure(Kate).MeleeGrants, 0);
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
		// Four share half: two at once on Normal and on Hard (Hard's three needs six).
		const int32 Budget = UHawkeyeCombatTokenRules::ScaleBudgetForGroup(
			FMath::RoundToInt(UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::MeleeTokens)), 4,
			UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::TokenGroupShare));
		AddInfo(FString::Printf(TEXT("%s: at most %d swinging at once (narrowest pair %.0f deg apart, blows at least "
									  "%.2f s apart); %d swings from off her screen, shortest wind-up %.2f s."),
			*Name, Fight.MostMeleeAtOnce, Fight.NarrowestPairDegrees, Fight.ShortestImpactGap, Fight.OffScreenSwings,
			Fight.ShortestOffScreenWindup));
		TestEqual(Name + TEXT(": four thugs share two tokens"), Budget, 2);
		TestTrue(Name + TEXT(": never more swinging at once than the budget"), Fight.MostMeleeAtOnce <= Budget);
		TestEqual(Name + TEXT(": the budget is used: two come at her at once"), Fight.MostMeleeAtOnce, Budget);
		// Each was granted at least the separation apart; the step in can close it a little, never to one side.
		const float Separation = UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::MeleeSeparationDegrees);
		TestTrue(Name + TEXT(": two at once come from different sides"), Fight.NarrowestPairDegrees >= Separation * 0.75f);
		const float Interval = UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::ImpactIntervalSeconds);
		TestTrue(Name + TEXT(": no two blows land closer than the impact interval (one step of slack)"),
			Fight.ShortestImpactGap >= Interval - 0.051f);
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
	ThinkThug(Brain, Brain->ThinkIntervalSeconds);
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
	TestEqual(TEXT("Two melee thugs share one token on Normal: never together"), Fight.MostMeleeAtOnce, 1);
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
	ThinkThug(Holder, Think);
	TestFalse(TEXT("A staggered holder gives it back within one think"), Tokens->HoldsToken(Holder->GetThug()));

	// Killed: the same.
	Holder = RunUntilHeld(TestWorld, Kate, Brains, 3.f);
	if (!TestNotNull(TEXT("Somebody else holds it"), Holder))
	{
		return false;
	}
	Holder->GetThug()->GetHealthComponent()->ApplyDamage(1000.f, Kate);
	TestWorld.AdvanceClock(Think);
	ThinkThug(Holder, Think);
	TestFalse(TEXT("A killed holder gives it back within one think"), Tokens->HoldsToken(Holder->GetThug()));

	// Kate going down takes it from everyone: the stand-off.
	TestNotNull(TEXT("The fight goes on with a new holder"), RunUntilHeld(TestWorld, Kate, Brains, 3.f));
	Kate->GetHealthComponent()->SetInvulnerable(false);
	Kate->GetHealthComponent()->ApplyDamage(1000.f, nullptr);
	for (AThugAIController* Brain : Brains)
	{
		ThinkThug(Brain, Think);
	}
	TestNull(TEXT("Nobody holds a token on a downed Kate"), FindHolder(Tokens, Brains));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTokensGroupScale, "Hawkeye.Tokens.BudgetScalesWithGroup", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensGroupScale::RunTest(const FString& Parameters)
{
	using R = UHawkeyeCombatTokenRules;
	// Normal's melee budget is 2: half the group, rounded up.
	TestEqual(TEXT("Alone: one"), R::ScaleBudgetForGroup(2, 1, 0.5f), 1);
	TestEqual(TEXT("Two thugs share one: never both always swinging"), R::ScaleBudgetForGroup(2, 2, 0.5f), 1);
	TestEqual(TEXT("Three: two"), R::ScaleBudgetForGroup(2, 3, 0.5f), 2);
	TestEqual(TEXT("Five: still Normal's two"), R::ScaleBudgetForGroup(2, 5, 0.5f), 2);
	// Hard's 3.
	TestEqual(TEXT("Hard, two thugs: one"), R::ScaleBudgetForGroup(3, 2, 0.5f), 1);
	TestEqual(TEXT("Hard, four: two"), R::ScaleBudgetForGroup(3, 4, 0.5f), 2);
	TestEqual(TEXT("Hard, five: three"), R::ScaleBudgetForGroup(3, 5, 0.5f), 3);
	TestEqual(TEXT("Hard, nine: never over three"), R::ScaleBudgetForGroup(3, 9, 0.5f), 3);
	// Story's 1, and the edges.
	TestEqual(TEXT("Story, six: one"), R::ScaleBudgetForGroup(1, 6, 0.5f), 1);
	TestEqual(TEXT("Nobody on it yet: one, so the first to ask can be granted"), R::ScaleBudgetForGroup(2, 0, 0.5f), 1);
	TestEqual(TEXT("A whole share: the budget"), R::ScaleBudgetForGroup(2, 2, 1.f), 2);

	// In the subsystem, Normal: two melee thugs never hold together; a third makes room for a second.
	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(TestWorld.Get());
	ACharacter* Kate = SpawnBody(TestWorld, FVector::ZeroVector);
	ACharacter* Front = SpawnBody(TestWorld, FVector(200.f, 0.f, 0.f));
	ACharacter* Behind = SpawnBody(TestWorld, FVector(-200.f, 0.f, 0.f));
	ACharacter* Side = SpawnBody(TestWorld, FVector(0.f, 200.f, 0.f));
	if (!Tokens || !Kate || !Front || !Behind || !Side)
	{
		return false;
	}
	TestWorld.AdvanceClock(1.f);
	TestTrue(TEXT("The first of two"), Tokens->RequestToken(Front, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestFalse(TEXT("Not the second of two"), Tokens->RequestToken(Behind, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	Tokens->RequestToken(Side, Kate, EHawkeyeAttackKind::Melee, false);
	TestTrue(TEXT("With a third in the fight, the one behind her too"),
		Tokens->RequestToken(Behind, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHawkeyeTokensAngleSeparation, "Hawkeye.Tokens.TwoAtOnceFromDifferentSides", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensAngleSeparation::RunTest(const FString& Parameters)
{
	using R = UHawkeyeCombatTokenRules;
	TestTrue(TEXT("Nobody holding: free"), R::IsSeparatedFromHolders(10.f, {}, 60.f));
	TestFalse(TEXT("30 deg from the holder: the same side"), R::IsSeparatedFromHolders(30.f, {0.f}, 60.f));
	TestTrue(TEXT("60 deg: just far enough"), R::IsSeparatedFromHolders(60.f, {0.f}, 60.f));
	TestTrue(TEXT("Behind her: far enough"), R::IsSeparatedFromHolders(180.f, {0.f}, 60.f));
	TestFalse(TEXT("Across the wrap: 170 and -170 are 20 apart"), R::IsSeparatedFromHolders(-170.f, {170.f}, 60.f));
	TestFalse(TEXT("Clear of one holder but not the other"), R::IsSeparatedFromHolders(100.f, {0.f, 130.f}, 60.f));

	// In the subsystem, Normal (60 deg): the thug next to the holder waits; the one across from him goes first.
	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(TestWorld.Get());
	ACharacter* Kate = SpawnBody(TestWorld, FVector::ZeroVector);
	ACharacter* Front = SpawnBody(TestWorld, FVector(200.f, 0.f, 0.f));
	ACharacter* Beside = SpawnBody(TestWorld, FVector(200.f, 60.f, 0.f));
	ACharacter* Across = SpawnBody(TestWorld, FVector(-300.f, 0.f, 0.f));
	if (!Tokens || !Kate || !Front || !Beside || !Across)
	{
		return false;
	}
	TestWorld.AdvanceClock(1.f);
	Tokens->RequestToken(Across, Kate, EHawkeyeAttackKind::Melee, false);
	TestTrue(TEXT("The first"), Tokens->RequestToken(Front, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestFalse(TEXT("The one 17 deg beside him: nearer and on screen, but the same side"),
		Tokens->RequestToken(Beside, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestTrue(TEXT("The one behind her, 180 deg round: granted though he ranks after"),
		Tokens->RequestToken(Across, Kate, EHawkeyeAttackKind::Melee, true).bGranted);
	TestEqual(TEXT("Two out: the group's budget, none left for the one beside"),
		Tokens->GetBudgetView(Kate, EHawkeyeAttackKind::Melee).Holders.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTokensImpactStagger, "Hawkeye.Tokens.ImpactStagger", HawkeyeTokenTest::Flags)

bool FHawkeyeTokensImpactStagger::RunTest(const FString& Parameters)
{
	using R = UHawkeyeCombatTokenRules;
	TestEqual(TEXT("Nothing else due: as wanted"), R::StaggerImpactDelay(0.8f, 10.0, {}, 0.45f), 0.8f, 0.001f);
	TestEqual(TEXT("Far from the other: as wanted"), R::StaggerImpactDelay(0.8f, 10.0, {12.0}, 0.45f), 0.8f, 0.001f);
	TestEqual(TEXT("Landing with another: pushed 0.45 s past it"), R::StaggerImpactDelay(0.8f, 10.0, {10.8}, 0.45f),
		1.25f, 0.001f);
	TestEqual(TEXT("Just before another: pushed past it, never earlier"), R::StaggerImpactDelay(0.8f, 10.0, {10.9}, 0.45f),
		1.35f, 0.001f);
	TestEqual(TEXT("Two others in a row: past both"), R::StaggerImpactDelay(0.8f, 10.0, {11.25, 10.8}, 0.45f), 1.7f, 0.001f);
	TestEqual(TEXT("Room between two: it fits there"), R::StaggerImpactDelay(0.8f, 10.0, {10.2, 11.5}, 0.45f), 0.8f, 0.001f);
	TestEqual(TEXT("Exactly the interval apart is fine"), R::StaggerImpactDelay(0.8f, 10.0, {10.35}, 0.45f), 0.8f, 0.001f);

	// In the subsystem: booked blows on one target keep Normal's 0.45 s; a release frees the slot; Clint's are his own.
	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(TestWorld.Get());
	ACharacter* Kate = SpawnBody(TestWorld, FVector::ZeroVector);
	ACharacter* Clint = SpawnBody(TestWorld, FVector(3000.f, 0.f, 0.f));
	ACharacter* A = SpawnBody(TestWorld, FVector(200.f, 0.f, 0.f));
	ACharacter* B = SpawnBody(TestWorld, FVector(-200.f, 0.f, 0.f));
	ACharacter* Gunner = SpawnBody(TestWorld, FVector(800.f, 300.f, 0.f));
	if (!Tokens || !Kate || !Clint || !A || !B || !Gunner)
	{
		return false;
	}
	TestWorld.AdvanceClock(1.f);
	TestEqual(TEXT("The first blow as wanted"), Tokens->ScheduleImpact(A, Kate, 0.8f), 0.8f, 0.001f);
	TestEqual(TEXT("A second due at once waits"), Tokens->ScheduleImpact(B, Kate, 0.8f), 1.25f, 0.001f);
	TestEqual(TEXT("A gunner's first shot too: past both"), Tokens->ScheduleImpact(Gunner, Kate, 0.8f), 1.7f, 0.001f);
	TestEqual(TEXT("Clint's attacker is not held by Kate's"), Tokens->ScheduleImpact(Gunner, Clint, 0.8f), 0.8f, 0.001f);
	TestEqual(TEXT("The pressure count saw the two pushed"), Tokens->GetPressure(Kate).StaggeredImpacts, 2);
	Tokens->ReleaseToken(A);
	Tokens->ReleaseToken(Gunner);
	TestEqual(TEXT("A's blow broken: the slot is free again"), Tokens->ScheduleImpact(A, Kate, 0.8f), 0.8f, 0.001f);
	TestWorld.AdvanceClock(2.f);
	TestEqual(TEXT("Blows already landed hold nothing"), Tokens->ScheduleImpact(B, Kate, 0.5f), 0.5f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHealthHitGrace, "Hawkeye.Health.PostHitGrace", HawkeyeTokenTest::Flags)

bool FHawkeyeHealthHitGrace::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Never hit: no grace"), UHealthComponent::IsWithinHitGrace(5.0, -1.0, 0.6f));
	TestTrue(TEXT("0.3 s after a hit: held off"), UHealthComponent::IsWithinHitGrace(5.3, 5.0, 0.6f));
	TestFalse(TEXT("0.6 s after: lands"), UHealthComponent::IsWithinHitGrace(5.6, 5.0, 0.6f));
	TestFalse(TEXT("No grace set: lands"), UHealthComponent::IsWithinHitGrace(5.1, 5.0, 0.f));

	using namespace HawkeyeTokenTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	ACharacter* Thug = SpawnBody(TestWorld, FVector(200.f, 0.f, 0.f));
	ACharacter* Other = SpawnBody(TestWorld, FVector(-200.f, 0.f, 0.f));
	UHealthComponent* Health = Kate ? Kate->GetHealthComponent() : nullptr;
	if (!Health || !Thug || !Other)
	{
		return false;
	}
	TestEqual(TEXT("Kate's grace is Normal's"), Health->HitGraceSeconds, 0.6f);
	TestWorld.AdvanceClock(1.f);
	TestEqual(TEXT("The first blow lands"), Health->ApplyMeleeDamage(15.f, Thug), 15.f);
	TestTrue(TEXT("And starts the grace"), Health->IsInHitGrace());
	TestEqual(TEXT("A second thug's blow at the same moment is held off"), Health->ApplyMeleeDamage(15.f, Other), 0.f);
	TestWorld.AdvanceClock(0.3f);
	TestEqual(TEXT("A shot 0.3 s later too"), Health->ApplyDamage(12.f, Other), 0.f);
	TestEqual(TEXT("Her own boomerang is not held off"), Health->ApplyDamage(5.f, Kate), 5.f);
	TestEqual(TEXT("Nor a fall"), Health->ApplyDamage(5.f, nullptr), 5.f);
	TestWorld.AdvanceClock(0.31f);
	TestEqual(TEXT("0.61 s after the first, the next lands"), Health->ApplyMeleeDamage(15.f, Other), 15.f);
	TestEqual(TEXT("She counts two hits taken"), Kate->GetHitsTakenCount(), 2);
	TestEqual(TEXT("And the health they took"), Kate->GetDamageTakenTotal(), 30.f, 0.01f);

	// A thug's component has none: a burst on him lands shot by shot.
	UHealthComponent* Plain = NewObject<UHealthComponent>(Thug);
	TestEqual(TEXT("Nobody else has a grace"), Plain->HitGraceSeconds, 0.f);
	TestTrue(TEXT("Shot one"), Plain->ApplyDamage(5.f, Kate) > 0.f);
	TestTrue(TEXT("Shot two at once"), Plain->ApplyDamage(5.f, Kate) > 0.f);

	// Hard's is shorter, Story's longer.
	FHawkeyeSettings HardSettings;
	HardSettings.Difficulty = EHawkeyeDifficulty::Hard;
	Kate->ApplySettings(HardSettings);
	TestEqual(TEXT("Hard: 0.45 s"), Health->HitGraceSeconds, 0.45f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugRingFidget, "Hawkeye.Tokens.WaitersFidget", HawkeyeTokenTest::Flags)

bool FHawkeyeThugRingFidget::RunTest(const FString& Parameters)
{
	FHawkeyeRingFidget Fidget;
	Fidget.Reset();
	TestTrue(TEXT("Reset asks for a re-path"), Fidget.ConsumeSpotChanged());
	TestFalse(TEXT("Once"), Fidget.ConsumeSpotChanged());
	TestTrue(TEXT("Nothing before the first wait is up"),
		Fidget.Advance(Fidget.MinIntervalSeconds - 0.1f, 0.9f, 0.f, 0.9f) == EHawkeyeRingFidgetMove::None);
	TestTrue(TEXT("Then a shift (roll over the feint chance)"),
		Fidget.Advance(0.2f, 0.9f, 0.f, 0.9f) == EHawkeyeRingFidgetMove::Shift);
	TestEqual(TEXT("To the right of his place"), Fidget.GetSlotOffsetDegrees(), Fidget.ShiftDegrees);
	TestTrue(TEXT("A new spot"), Fidget.ConsumeSpotChanged());
	TestTrue(TEXT("Next a shift left: back across his place, never drifting"),
		Fidget.Advance(Fidget.MinIntervalSeconds, 0.9f, 0.f, 0.1f) == EHawkeyeRingFidgetMove::Shift
			&& FMath::IsNearlyEqual(Fidget.GetSlotOffsetDegrees(), -Fidget.ShiftDegrees));

	TestTrue(TEXT("A low roll feints"), Fidget.Advance(Fidget.MinIntervalSeconds, 0.1f, 0.f, 0.5f) == EHawkeyeRingFidgetMove::Feint);
	TestTrue(TEXT("In first"), Fidget.IsFeintingIn());
	TestEqual(TEXT("FeintDepth inside the ring"), Fidget.GetRadiusInset(), Fidget.FeintDepth);
	Fidget.ConsumeSpotChanged();
	Fidget.Advance(Fidget.FeintSeconds * 0.6f, 0.9f, 0.f, 0.5f);
	TestFalse(TEXT("Then back out"), Fidget.IsFeintingIn());
	TestTrue(TEXT("Turning back is a new spot"), Fidget.ConsumeSpotChanged());
	TestEqual(TEXT("On the ring again"), Fidget.GetRadiusInset(), 0.f);
	TestTrue(TEXT("No new move while the feint runs"),
		Fidget.Advance(Fidget.FeintSeconds * 0.3f, 0.1f, 0.f, 0.5f) == EHawkeyeRingFidgetMove::None);
	Fidget.Reset();
	TestEqual(TEXT("Reset: his own place"), Fidget.GetSlotOffsetDegrees(), 0.f);
	TestFalse(TEXT("Not feinting"), Fidget.IsFeinting());
	return true;
}

#endif
