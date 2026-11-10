#include "Combat/WeaponComponent.h"
#include "Misc/AutomationTest.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugAIController.h"
#include "World/ThugAim.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * How archers and gunners aim (claude-docs/gameplay-semantics.md, "Ranged thug accuracy"): the lagged track,
 * the cone, the forced miss after a jink, the archer's cadence, and the strafing-Kate hit rate per difficulty,
 * measured with the thugs' own aim functions and a flown arrow against her capsule.
 */
namespace HawkeyeRangedTest
{
	static constexpr float KateRadius = 34.f;
	static constexpr float KateHalfHeight = 88.f;
	static constexpr float ChestHeight = 30.f;
	static constexpr float ArrowSpeed = 5000.f;
	static constexpr float BowSpreadDegrees = 0.5f;
	static constexpr float Gravity = -980.f;

	static const EHawkeyeDifficulty Levels[] = { EHawkeyeDifficulty::Story, EHawkeyeDifficulty::Normal, EHawkeyeDifficulty::Hard };

	static const TCHAR* Name(EHawkeyeDifficulty Difficulty)
	{
		return Difficulty == EHawkeyeDifficulty::Story ? TEXT("Story") : Difficulty == EHawkeyeDifficulty::Hard ? TEXT("Hard") : TEXT("Normal");
	}

	/** True when Point is inside a standing capsule centred on Centre. */
	static bool InCapsule(const FVector& Point, const FVector& Centre)
	{
		const float Half = KateHalfHeight - KateRadius;
		const FVector OnAxis(Centre.X, Centre.Y, FMath::Clamp(Point.Z, Centre.Z - Half, Centre.Z + Half));
		return FVector::Dist(Point, OnAxis) <= KateRadius;
	}

	/** Kate's centre at time T on a straight run from Start at Velocity. */
	static FVector KateAt(const FVector& Start, const FVector& Velocity, float T)
	{
		return Start + Velocity * T;
	}

	/** The track a thug would have of a straight run: a look every 0.05 s for the 1.5 s before 0. */
	static FHawkeyeTargetTrack TrackOf(const FVector& Start, const FVector& Velocity)
	{
		FHawkeyeTargetTrack Track;
		for (double T = -1.5; T <= 1e-6; T += HawkeyeThugAim::SampleSeconds)
		{
			Track.AddSample(T, KateAt(Start, Velocity, static_cast<float>(T)) + FVector(0.f, 0.f, ChestHeight), Velocity, false, false);
		}
		return Track;
	}

	/** One arrow at a Kate running from Start at Velocity, from From at time 0: flown until it passes her. True on a hit. */
	static bool ArrowHits(const FVector& From, const FVector& Start, const FVector& Velocity, EHawkeyeDifficulty Difficulty,
		FRandomStream& Stream)
	{
		const FHawkeyeTargetTrack Track = TrackOf(Start, Velocity);
		const FHawkeyeRangedShot Shot = HawkeyeThugAim::BuildShot(Track, 0.0, From, Start + FVector(0.f, 0.f, ChestHeight), Velocity,
			UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::RangedTrackingLagSeconds), ArrowSpeed, Gravity,
			HawkeyeThugAim::ArcherBaseConeDegrees, UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::RangedConeScale),
			KateRadius);
		const FHawkeyeRangedAim Aim = HawkeyeThugAim::ComputeShot(Shot, Stream);
		// The archer's bow adds its own full-draw spread, as UBowComponent::FireArrow does.
		const FVector Dir = UWeaponComponent::ApplyConeSpread((Aim.AimPoint - From).GetSafeNormal(), BowSpreadDegrees, Stream);
		for (float T = 0.f; T < 1.5f; T += 0.002f)
		{
			const FVector Arrow = From + Dir * ArrowSpeed * T + FVector(0.f, 0.f, 0.5f * Gravity * T * T);
			if (InCapsule(Arrow, KateAt(Start, Velocity, T)))
			{
				return true;
			}
			if (FVector::DotProduct(Arrow - KateAt(Start, Velocity, T), (Start - From).GetSafeNormal2D()) > 200.f)
			{
				break;
			}
		}
		return false;
	}

	/** One pistol shot (hitscan) from From at Kate running from Start at Velocity. */
	static bool BulletHits(const FVector& From, const FVector& Start, const FVector& Velocity, EHawkeyeDifficulty Difficulty,
		FRandomStream& Stream)
	{
		const FHawkeyeTargetTrack Track = TrackOf(Start, Velocity);
		const FHawkeyeRangedShot Shot = HawkeyeThugAim::BuildShot(Track, 0.0, From, Start + FVector(0.f, 0.f, ChestHeight), Velocity,
			UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::RangedTrackingLagSeconds), 0.f, Gravity,
			HawkeyeThugAim::GunnerBaseConeDegrees, UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::RangedConeScale),
			KateRadius);
		const FHawkeyeRangedAim Aim = HawkeyeThugAim::ComputeShot(Shot, Stream);
		const FVector Dir = (Aim.AimPoint - From).GetSafeNormal();
		for (float Along = 0.f; Along < 3000.f; Along += 2.f)
		{
			if (InCapsule(From + Dir * Along, Start))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * 20 shots at Kate sprinting (Speed, cm/s) sideways past a thug Distance away: each pass starts on a
	 * different spot of the crossing and alternates direction, with a straight run in the history.
	 */
	static int32 CountHits(bool bArrow, float Distance, float Speed, EHawkeyeDifficulty Difficulty, int32 Seed)
	{
		FRandomStream Stream(Seed);
		const FVector From(0.f, 0.f, 60.f);
		int32 Hits = 0;
		for (int32 Shot = 0; Shot < 20; ++Shot)
		{
			const float Sign = (Shot % 2 == 0) ? 1.f : -1.f;
			const FVector Start(Distance, -Sign * (150.f - 15.f * (Shot % 10)), 0.f);
			const FVector Velocity(0.f, Sign * Speed, 0.f);
			Hits += (bArrow ? ArrowHits(From, Start, Velocity, Difficulty, Stream) : BulletHits(From, Start, Velocity, Difficulty, Stream)) ? 1 : 0;
		}
		return Hits;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRangedTrack, "Hawkeye.Thug.TargetTrackLagAndJinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeRangedTrack::RunTest(const FString& Parameters)
{
	FHawkeyeTargetTrack Track;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const double T = Step * 0.05;
		Track.AddSample(T, FVector(0.f, 700.f * T, 0.f), FVector(0.f, 700.f, 0.f), false, false);
	}
	Track.AddSample(1.01, FVector(0.f, 707.f, 0.f), FVector(0.f, 700.f, 0.f), false, false);
	TestEqual(TEXT("A look inside 0.05 s of the last is dropped"), Track.Num(), 21);
	FVector Seen;
	FVector Velocity;
	TestTrue(TEXT("A lagged look"), Track.GetLagged(1.0, 0.32f, Seen, Velocity));
	TestEqual(TEXT("0.32 s late: where she was then"), static_cast<float>(Seen.Y), 700.f * 0.68f, 0.5f);
	TestEqual(TEXT("And how fast she was going"), static_cast<float>(Velocity.Y), 700.f, 0.5f);
	TestFalse(TEXT("A straight run is not a jink"), Track.IsErratic(1.0));

	// A reversal 0.3 s ago is a jink; 0.6 s ago it is not any more.
	FHawkeyeTargetTrack Reversal;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const double T = Step * 0.05;
		Reversal.AddSample(T, FVector::ZeroVector, FVector(0.f, T < 0.7 ? 700.f : -700.f, 0.f), false, false);
	}
	TestTrue(TEXT("Turned 180 degrees 0.3 s ago: erratic"), Reversal.IsErratic(1.0));
	TestFalse(TEXT("0.8 s later it has passed"), Reversal.IsErratic(1.8));

	// A 45 degree turn is not; a slow shuffle does not count; a dodge always does.
	FHawkeyeTargetTrack Veer;
	FHawkeyeTargetTrack Shuffle;
	FHawkeyeTargetTrack Dodge;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const double T = Step * 0.05;
		Veer.AddSample(T, FVector::ZeroVector, T < 0.7 ? FVector(0.f, 700.f, 0.f) : FRotator(0.f, 45.f, 0.f).RotateVector(FVector(0.f, 700.f, 0.f)),
			false, false);
		Shuffle.AddSample(T, FVector::ZeroVector, FVector(0.f, T < 0.7 ? 100.f : -100.f, 0.f), false, false);
		Dodge.AddSample(T, FVector::ZeroVector, FVector(0.f, 700.f, 0.f), false, Step == 15);
	}
	TestFalse(TEXT("A 45 degree veer is not a jink"), Veer.IsErratic(1.0));
	TestFalse(TEXT("A shuffle under 150 cm/s is not"), Shuffle.IsErratic(1.0));
	TestTrue(TEXT("A dodge 0.25 s ago is"), Dodge.IsErratic(1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRangedCone, "Hawkeye.Thug.RangedConeWidensWithHerSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeRangedCone::RunTest(const FString& Parameters)
{
	const FVector From = FVector::ZeroVector;
	const FVector At8m(800.f, 0.f, 0.f);
	TestEqual(TEXT("Standing: the archer's base"),
		HawkeyeThugAim::ComputeConeDegrees(From, At8m, FVector::ZeroVector, HawkeyeThugAim::ArcherBaseConeDegrees, 1.f, false), 0.75f, 1e-3f);
	const float Sprint = HawkeyeThugAim::ComputeConeDegrees(From, At8m, FVector(0.f, 700.f, 0.f), 0.75f, 1.f, false);
	TestEqual(TEXT("Sprinting across at 8 m: base + atan(700 * 0.2 / 800)"), Sprint, 0.75f + FMath::RadiansToDegrees(FMath::Atan(140.f / 800.f)),
		1e-3f);
	AddInfo(FString::Printf(TEXT("Sprinting sideways at 8 m: %.1f degree cone at Normal."), Sprint));
	TestEqual(TEXT("Running straight at him adds nothing"),
		HawkeyeThugAim::ComputeConeDegrees(From, At8m, FVector(-700.f, 0.f, 0.f), 0.75f, 1.f, false), 0.75f, 1e-3f);
	TestEqual(TEXT("In the air: 3 more"), HawkeyeThugAim::ComputeConeDegrees(From, At8m, FVector::ZeroVector, 0.75f, 1.f, true), 3.75f, 1e-3f);
	TestTrue(TEXT("Further away the same speed sweeps less"),
		HawkeyeThugAim::ComputeConeDegrees(From, At8m * 3.f, FVector(0.f, 700.f, 0.f), 0.75f, 1.f, false) < Sprint);
	TestEqual(TEXT("Hard's scale"), HawkeyeThugAim::ComputeConeDegrees(From, At8m, FVector::ZeroVector, 5.f, 0.7f, false), 3.5f, 1e-3f);

	TestEqual(TEXT("Lag: Story 0.4"), UDifficultySubsystem::GetTableValue(EHawkeyeDifficulty::Story, EDifficultyStat::RangedTrackingLagSeconds), 0.4f);
	TestEqual(TEXT("Lag: Normal 0.32"), UDifficultySubsystem::GetTableValue(EHawkeyeDifficulty::Normal, EDifficultyStat::RangedTrackingLagSeconds), 0.32f);
	TestEqual(TEXT("Lag: Hard 0.25"), UDifficultySubsystem::GetTableValue(EHawkeyeDifficulty::Hard, EDifficultyStat::RangedTrackingLagSeconds), 0.25f);
	TestEqual(TEXT("Cone: Story 1.4"), UDifficultySubsystem::GetTableValue(EHawkeyeDifficulty::Story, EDifficultyStat::RangedConeScale), 1.4f);
	TestEqual(TEXT("Cone: Hard 0.7"), UDifficultySubsystem::GetTableValue(EHawkeyeDifficulty::Hard, EDifficultyStat::RangedConeScale), 0.7f);

	// A steady runner is led right: the believed point is where she will be.
	FHawkeyeTargetTrack Track;
	for (double T = -1.5; T <= 1e-6; T += 0.05)
	{
		Track.AddSample(T, FVector(1000.f, 700.f * T, 30.f), FVector(0.f, 700.f, 0.f), false, false);
	}
	FHawkeyeRangedShot Shot = HawkeyeThugAim::BuildShot(Track, 0.0, FVector::ZeroVector, FVector(1000.f, 0.f, 30.f), FVector(0.f, 700.f, 0.f),
		0.32f, 0.f, -980.f, 0.f, 1.f, 34.f);
	// No cone at all: the lead alone.
	Shot.ConeScale = 0.f;
	FRandomStream Stream(1);
	const FHawkeyeRangedAim Led = HawkeyeThugAim::ComputeShot(Shot, Stream);
	TestTrue(TEXT("The pistol's point is on her now after 0.32 s of lag"), FVector::Dist(Led.AimPoint, FVector(1000.f, 0.f, 30.f)) < 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRangedJinkMisses, "Hawkeye.Thug.JinkMakesTheShotMiss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeRangedJinkMisses::RunTest(const FString& Parameters)
{
	FHawkeyeTargetTrack Track;
	for (int32 Step = 0; Step <= 30; ++Step)
	{
		const double T = -1.5 + Step * 0.05;
		Track.AddSample(T, FVector(1000.f, 0.f, 30.f), FVector(0.f, T < -0.3 ? 700.f : -700.f, 0.f), false, false);
	}
	FRandomStream Stream(7);
	for (const float Distance : { 1000.f, 2000.f })
	{
		const FVector Chest(Distance, 0.f, 30.f);
		const FHawkeyeRangedShot Shot = HawkeyeThugAim::BuildShot(Track, 0.0, FVector::ZeroVector, Chest, FVector(0.f, -700.f, 0.f), 0.32f,
			5000.f, -980.f, 0.75f, 1.f, 34.f);
		TestTrue(TEXT("She reversed 0.3 s ago"), Shot.bErratic);
		const FHawkeyeRangedAim Aim = HawkeyeThugAim::ComputeShot(Shot, Stream);
		TestTrue(FString::Printf(TEXT("At %.0f m it goes wide on purpose"), Distance / 100.f), Aim.bForcedMiss);
		// The drop is added above; across his line it is her radius plus 50 wide, on the side she came from (+Y).
		TestEqual(TEXT("84 cm wide"), static_cast<float>(Aim.AimPoint.Y), 84.f, 1.f);
	}
	const FHawkeyeRangedShot Close = HawkeyeThugAim::BuildShot(Track, 0.0, FVector::ZeroVector, FVector(500.f, 0.f, 30.f),
		FVector(0.f, -700.f, 0.f), 0.32f, 5000.f, -980.f, 0.75f, 1.f, 34.f);
	TestFalse(TEXT("Inside 6 m a jink does not save her"), HawkeyeThugAim::ComputeShot(Close, Stream).bForcedMiss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRangedArcherGap, "Hawkeye.Thug.ArcherShotGapPerDifficulty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeRangedArcherGap::RunTest(const FString& Parameters)
{
	using namespace HawkeyeRangedTest;
	struct FRange
	{
		EHawkeyeDifficulty Difficulty;
		float Min;
		float Max;
	};
	const FRange Ranges[] = { { EHawkeyeDifficulty::Story, 3.0f, 4.0f }, { EHawkeyeDifficulty::Normal, 1.5f, 2.5f }, { EHawkeyeDifficulty::Hard, 1.2f, 1.8f } };
	for (const FRange& Range : Ranges)
	{
		FHawkeyeSettings Settings;
		Settings.Difficulty = Range.Difficulty;
		UHawkeyeSettingsSubsystem::SetTestSettingsOverride(&Settings);
		FHawkeyeTestWorld TestWorld;
		AThugAIController* Brain = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (!Brain)
		{
			UHawkeyeSettingsSubsystem::SetTestSettingsOverride(nullptr);
			AddError(TEXT("No controller."));
			return false;
		}
		float Lowest = 100.f;
		float Highest = 0.f;
		for (int32 Roll = 0; Roll < 60; ++Roll)
		{
			Brain->RollArcherGap();
			Lowest = FMath::Min(Lowest, Brain->GetNextArcherGapSeconds());
			Highest = FMath::Max(Highest, Brain->GetNextArcherGapSeconds());
		}
		TestTrue(FString::Printf(TEXT("%s: every gap in %.1f-%.1f s (saw %.2f-%.2f)"), Name(Range.Difficulty), Range.Min, Range.Max, Lowest,
			Highest), Lowest >= Range.Min - 1e-3f && Highest <= Range.Max + 1e-3f);
		TestTrue(FString::Printf(TEXT("%s: and it varies"), Name(Range.Difficulty)), Highest - Lowest > 0.2f);
		Brain->ArcherShotCooldownSeconds = 0.f;
		Brain->RollArcherGap();
		TestEqual(TEXT("A fixed cooldown (tests) wins"), Brain->GetNextArcherGapSeconds(), 0.f);
		UHawkeyeSettingsSubsystem::SetTestSettingsOverride(nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRangedStrafingKate, "Hawkeye.Thug.StrafingKateHitRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeRangedStrafingKate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeRangedTest;
	int32 ArcherSprint[3] = {};
	int32 GunnerSprint[3] = {};
	int32 ArcherStanding[3] = {};
	int32 GunnerStanding[3] = {};
	int32 ArcherFar[3] = {};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const EHawkeyeDifficulty Difficulty = Levels[Index];
		ArcherSprint[Index] = CountHits(true, 800.f, 700.f, Difficulty, 11);
		GunnerSprint[Index] = CountHits(false, 800.f, 700.f, Difficulty, 12);
		ArcherStanding[Index] = CountHits(true, 800.f, 0.f, Difficulty, 13);
		GunnerStanding[Index] = CountHits(false, 800.f, 0.f, Difficulty, 14);
		ArcherFar[Index] = CountHits(true, 2000.f, 500.f, Difficulty, 15);
		AddInfo(FString::Printf(TEXT("%s, 20 shots each: archer at a sideways sprint at 8 m %d, standing %d, running at 20 m %d; gunner sprint %d, standing %d."),
			Name(Difficulty), ArcherSprint[Index], ArcherStanding[Index], ArcherFar[Index], GunnerSprint[Index], GunnerStanding[Index]));
	}
	// Normal: a sideways sprint past an archer at 8 m is hit no more than about one shot in three.
	TestTrue(FString::Printf(TEXT("Normal archer, sprint at 8 m: %d of 20, at most 7"), ArcherSprint[1]), ArcherSprint[1] <= 7);
	TestTrue(FString::Printf(TEXT("Normal gunner, sprint at 8 m: %d of 20, at most 7"), GunnerSprint[1]), GunnerSprint[1] <= 7);
	TestTrue(FString::Printf(TEXT("Story is kinder than Normal (%d vs %d)"), ArcherSprint[0], ArcherSprint[1]), ArcherSprint[0] <= ArcherSprint[1]);
	TestTrue(FString::Printf(TEXT("Hard is harder than Normal (%d vs %d)"), ArcherSprint[2], ArcherSprint[1]), ArcherSprint[2] >= ArcherSprint[1]);
	TestTrue(FString::Printf(TEXT("Hard archer, sprint at 8 m: %d of 20, at most 12"), ArcherSprint[2]), ArcherSprint[2] <= 12);
	TestTrue(FString::Printf(TEXT("Story archer, sprint: %d of 20, at most 5"), ArcherSprint[0]), ArcherSprint[0] <= 5);
	// They still track: standing still in the open gets you hit.
	TestTrue(FString::Printf(TEXT("Normal archer, standing: %d of 20, at least 15"), ArcherStanding[1]), ArcherStanding[1] >= 15);
	TestTrue(FString::Printf(TEXT("Normal gunner, standing: %d of 20, at least 6"), GunnerStanding[1]), GunnerStanding[1] >= 6);
	TestTrue(FString::Printf(TEXT("Story archer, standing: %d of 20, at least 10"), ArcherStanding[0]), ArcherStanding[0] >= 10);
	return true;
}

#endif
