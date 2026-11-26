#include "Combat/HitContactRules.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/HitRecoil.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The stagger a landed blow gives (claude-docs/gameplay-semantics.md, "Hit stagger and recoil"): how far and how fast a
 * light and a heavy hit move the victim, the way they go (attacker to victim), that nothing is knocked through a wall
 * (the sweep and the clamp), and the procedural spine and head recoil that reacts with no clip at all.
 */
namespace HawkeyeHitStaggerTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Runs a spring from a kick and returns its largest displacement along the kick and when that was. */
	static void MeasurePeak(float PeakDegrees, float Hz, float Damping, float& OutPeak, float& OutPeakAt, float& OutRestAt)
	{
		FHawkeyeRecoilSpring Spring;
		Spring.Kick(FVector::ForwardVector, PeakDegrees, Hz, Damping);
		constexpr float Step = 1.f / 240.f;
		OutPeak = 0.f;
		OutPeakAt = 0.f;
		OutRestAt = -1.f;
		for (int32 Frame = 1; Frame <= 480; ++Frame)
		{
			Spring.Advance(Step, Hz, Damping);
			const float Time = Step * static_cast<float>(Frame);
			if (Spring.Offset.X > OutPeak)
			{
				OutPeak = Spring.Offset.X;
				OutPeakAt = Time;
			}
			if (OutRestAt < 0.f && Spring.IsAtRest(0.15f))
			{
				OutRestAt = Time;
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStaggerDistanceByWeight, "Hawkeye.HitStagger.DistanceByWeight", HawkeyeHitStaggerTest::Flags)

bool FHawkeyeStaggerDistanceByWeight::RunTest(const FString& Parameters)
{
	const FHawkeyeWeldTuning Weld;
	const float Light = UHawkeyeHitContactRules::ResolveKnockbackCm(-1.f, false, Weld);
	const float Heavy = UHawkeyeHitContactRules::ResolveKnockbackCm(-1.f, true, Weld);
	TestTrue(FString::Printf(TEXT("A light hit moves him 15 to 20 cm (%.1f)"), Light), Light >= 15.f && Light <= 20.f);
	TestTrue(FString::Printf(TEXT("A heavy one 45 to 60 cm (%.1f)"), Heavy), Heavy >= 45.f && Heavy <= 60.f);
	TestTrue(TEXT("A heavy moves him at least twice a light"), Heavy >= Light * 2.f);

	const float LightSeconds = UHawkeyeHitContactRules::ResolveKnockbackSeconds(false, Weld);
	const float HeavySeconds = UHawkeyeHitContactRules::ResolveKnockbackSeconds(true, Weld);
	TestTrue(FString::Printf(TEXT("A light stagger is a snap of about 0.15 s (%.2f)"), LightSeconds), LightSeconds >= 0.12f && LightSeconds <= 0.2f);
	TestTrue(TEXT("A heavy takes longer (a step and a stumble)"), HeavySeconds > LightSeconds);

	// A clip's baked distance (the old 10 and 28 cm) does not shrink the stagger unless the tuning says clips win.
	TestEqual(TEXT("A short baked clip distance does not win by default"), UHawkeyeHitContactRules::ResolveKnockbackCm(10.f, false, Weld), Light);
	FHawkeyeWeldTuning ClipWins = Weld;
	ClipWins.bClipKnockbackWins = true;
	TestEqual(TEXT("It wins when asked to"), UHawkeyeHitContactRules::ResolveKnockbackCm(10.f, false, ClipWins), 10.f);
	TestEqual(TEXT("A clip with none baked (-1) falls back to the tuning even then"), UHawkeyeHitContactRules::ResolveKnockbackCm(-1.f, true, ClipWins), Heavy);

	TestEqual(TEXT("The scale doubles it"), UHawkeyeHitContactRules::ResolveKnockbackCm(-1.f, false, Weld, 2.f), Light * 2.f);
	TestEqual(TEXT("A scale of 0 turns the knock-back off"), UHawkeyeHitContactRules::ResolveKnockbackCm(-1.f, true, Weld, 0.f), 0.f);
	TestEqual(TEXT("A negative scale is none, not a pull"), UHawkeyeHitContactRules::ResolveKnockbackCm(-1.f, true, Weld, -3.f), 0.f);

	// The eased shove covers exactly the distance, light and heavy, in their own times.
	for (const bool bHeavy : { false, true })
	{
		float Peak = 0.f;
		TArray<float> Strengths;
		const float Distance = UHawkeyeHitContactRules::ResolveKnockbackCm(-1.f, bHeavy, Weld);
		const float Seconds = UHawkeyeHitContactRules::ResolveKnockbackSeconds(bHeavy, Weld);
		UHawkeyeHitContactRules::ComputeKnockbackProfile(Distance, Seconds, Weld.KnockbackEase, 9, Peak, Strengths);
		float Area = 0.f;
		for (int32 Index = 1; Index < Strengths.Num(); ++Index)
		{
			Area += 0.5f * (Strengths[Index - 1] + Strengths[Index]) / 8.f;
		}
		TestEqual(bHeavy ? TEXT("A heavy's profile covers its distance") : TEXT("A light's profile covers its distance"),
			Peak * Seconds * Area, Distance, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStaggerDirection, "Hawkeye.HitStagger.DirectionIsAttackerToVictim", HawkeyeHitStaggerTest::Flags)

bool FHawkeyeStaggerDirection::RunTest(const FString& Parameters)
{
	const FVector Victim(100.f, 100.f, 50.f);
	const FVector Attacker(0.f, 0.f, 0.f);
	const FVector Away = UHawkeyeHitContactRules::ComputeKnockbackDirection(&Attacker, Victim, FVector::ForwardVector);
	TestEqual(TEXT("Along the line from the attacker through the victim: X"), Away.X, FMath::Sqrt(0.5f), 0.001f);
	TestEqual(TEXT("Y"), Away.Y, FMath::Sqrt(0.5f), 0.001f);
	TestEqual(TEXT("Flat, whatever the heights"), Away.Z, 0.f);

	const FVector Behind(300.f, 0.f, 0.f);
	const FVector FromBehind = UHawkeyeHitContactRules::ComputeKnockbackDirection(&Behind, FVector(100.f, 0.f, 0.f), FVector::ForwardVector);
	TestEqual(TEXT("From the other side the shove flips with him"), FromBehind.X, -1.f, 0.001f);

	const FVector Fallback = UHawkeyeHitContactRules::ComputeKnockbackDirection(nullptr, Victim, FVector(0.f, -5.f, 9.f));
	TestEqual(TEXT("No attacker: the fallback, flattened and normalised"), Fallback.Y, -1.f, 0.001f);
	const FVector Same = UHawkeyeHitContactRules::ComputeKnockbackDirection(&Victim, Victim, FVector::ZeroVector);
	TestEqual(TEXT("Standing on him with no fallback: straight back along X, never a zero vector"), Same.X, -1.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStaggerWallClamp, "Hawkeye.HitStagger.ClampToClearance", HawkeyeHitStaggerTest::Flags)

bool FHawkeyeStaggerWallClamp::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Open ground (no hit, -1): all of it"), UHawkeyeHitContactRules::ClampKnockbackToClearance(52.f, -1.f, 3.f), 52.f);
	TestEqual(TEXT("A wall 30 cm off: 27 cm (the skin kept)"), UHawkeyeHitContactRules::ClampKnockbackToClearance(52.f, 30.f, 3.f), 27.f);
	TestEqual(TEXT("A wall further than the shove: the whole shove"), UHawkeyeHitContactRules::ClampKnockbackToClearance(18.f, 40.f, 3.f), 18.f);
	TestEqual(TEXT("A wall touching him: none"), UHawkeyeHitContactRules::ClampKnockbackToClearance(52.f, 0.f, 3.f), 0.f);
	TestEqual(TEXT("A wall nearer than the skin: none, never negative"), UHawkeyeHitContactRules::ClampKnockbackToClearance(52.f, 1.f, 3.f), 0.f);
	TestEqual(TEXT("Nothing wanted, nothing moved"), UHawkeyeHitContactRules::ClampKnockbackToClearance(-4.f, -1.f, 3.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStaggerNoKnockbackThroughWalls, "Hawkeye.HitStagger.NoKnockbackThroughWalls", HawkeyeHitStaggerTest::Flags)

bool FHawkeyeStaggerNoKnockbackThroughWalls::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(0.f, 0.f, 120.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("A thug"), Thug) || !TestNotNull(TEXT("His hit reaction"), Thug->GetHitReactionComponent()))
	{
		return false;
	}
	UHitReactionComponent* Reaction = Thug->GetHitReactionComponent();
	const float Radius = Thug->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector Away(1.f, 0.f, 0.f);

	TestEqual(TEXT("Open ground: nothing in the way"), Reaction->ComputeClearanceAhead(Away, 60.f), -1.f);
	Reaction->ApplyKnockbackNow(Away, 18.f, 0.15f);
	TestEqual(TEXT("A light shove in the open goes the whole 18 cm"), Reaction->GetLastKnockbackCm(), 18.f, 0.01f);
	TestEqual(TEXT("over the 0.15 s it was given"), Reaction->GetLastKnockbackSeconds(), 0.15f, 0.001f);

	// A wall whose near face is 8 cm from his capsule's edge, 2 m wide and 4 m tall.
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(),
		FVector(Radius + 8.f + 10.f, 0.f, 120.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("A wall"), Wall))
	{
		return false;
	}
	Wall->SetExtent(FVector(10.f, 100.f, 200.f));
	const float Clearance = Reaction->ComputeClearanceAhead(Away, 60.f);
	TestTrue(FString::Printf(TEXT("The sweep finds the wall about 8 cm ahead (%.1f)"), Clearance), Clearance > 6.f && Clearance < 10.f);

	Reaction->ApplyKnockbackNow(Away, 52.f, 0.3f);
	TestTrue(FString::Printf(TEXT("A heavy shove into it moves him no more than the gap less the skin (%.1f cm)"), Reaction->GetLastKnockbackCm()),
		Reaction->GetLastKnockbackCm() <= 5.5f);

	// Away from the wall, the same shove is free again.
	TestEqual(TEXT("The other way: clear"), Reaction->ComputeClearanceAhead(FVector(-1.f, 0.f, 0.f), 60.f), -1.f);
	Reaction->ApplyKnockbackNow(FVector(-1.f, 0.f, 0.f), 52.f, 0.3f);
	TestEqual(TEXT("so all 52 cm"), Reaction->GetLastKnockbackCm(), 52.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRecoilSpringShape, "Hawkeye.HitStagger.RecoilPeaksAndSettles", HawkeyeHitStaggerTest::Flags)

bool FHawkeyeRecoilSpringShape::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHitStaggerTest;
	const FHawkeyeHitRecoilTuning Tuning;

	float SpinePeak = 0.f;
	float SpinePeakAt = 0.f;
	float SpineRestAt = 0.f;
	MeasurePeak(Tuning.LightSpineDegrees, Tuning.SpineHz, Tuning.SpineDamping, SpinePeak, SpinePeakAt, SpineRestAt);
	TestEqual(TEXT("The spine peaks at the degrees the tuning gives a light"), SpinePeak, Tuning.LightSpineDegrees, 0.3f);
	TestTrue(FString::Printf(TEXT("quickly (%.3f s)"), SpinePeakAt), SpinePeakAt > 0.02f && SpinePeakAt < 0.12f);
	TestTrue(FString::Printf(TEXT("and is back at rest well inside a second (%.2f s)"), SpineRestAt), SpineRestAt > 0.f && SpineRestAt < 0.8f);

	float HeadPeak = 0.f;
	float HeadPeakAt = 0.f;
	float HeadRestAt = 0.f;
	MeasurePeak(Tuning.LightHeadDegrees, Tuning.HeadHz, Tuning.HeadDamping, HeadPeak, HeadPeakAt, HeadRestAt);
	TestEqual(TEXT("The head peaks at its own number"), HeadPeak, Tuning.LightHeadDegrees, 0.3f);
	TestTrue(TEXT("The head snaps further than the spine tips"), HeadPeak > SpinePeak);
	TestTrue(TEXT("and gets there first"), HeadPeakAt < SpinePeakAt);

	float HeavyPeak = 0.f;
	float Unused = 0.f;
	MeasurePeak(Tuning.HeavySpineDegrees, Tuning.SpineHz, Tuning.SpineDamping, HeavyPeak, Unused, Unused);
	TestTrue(TEXT("A heavy tips the spine further than a light"), HeavyPeak > SpinePeak * 1.5f);

	// The closed form is stable at any frame length: one big step is the many small ones.
	FHawkeyeRecoilSpring Coarse;
	FHawkeyeRecoilSpring Fine;
	Coarse.Kick(FVector(0.f, 1.f, 0.f), 9.f, Tuning.SpineHz, Tuning.SpineDamping);
	Fine.Kick(FVector(0.f, 1.f, 0.f), 9.f, Tuning.SpineHz, Tuning.SpineDamping);
	Coarse.Advance(0.2f, Tuning.SpineHz, Tuning.SpineDamping);
	for (int32 Index = 0; Index < 120; ++Index)
	{
		Fine.Advance(0.2f / 120.f, Tuning.SpineHz, Tuning.SpineDamping);
	}
	TestEqual(TEXT("One 0.2 s step equals 120 small ones"), Coarse.Offset.Y, Fine.Offset.Y, 0.01f);
	TestEqual(TEXT("and the other axis stays put"), Coarse.Offset.X, 0.f, 0.001f);

	// A second blow adds to the first, and the scale switches it off.
	FHawkeyeHitRecoil Recoil;
	Recoil.Kick(FVector::ForwardVector, false, Tuning);
	const float OneRate = Recoil.Spine.Rate.X;
	Recoil.Kick(FVector::ForwardVector, false, Tuning);
	TestEqual(TEXT("Two blows in a row: twice the kick"), Recoil.Spine.Rate.X, OneRate * 2.f, 0.01f);
	FHawkeyeHitRecoil Off;
	Off.Kick(FVector::ForwardVector, true, Tuning, 0.f);
	TestFalse(TEXT("A scale of 0: no recoil at all"), Off.IsActive(Tuning));
	FHawkeyeHitRecoil Zero;
	Zero.Kick(FVector::ZeroVector, true, Tuning);
	TestFalse(TEXT("No direction: nothing to tip toward, no recoil"), Zero.IsActive(Tuning));

	// And it ends: active, then at rest, then inactive.
	FHawkeyeHitRecoil Ends;
	Ends.Kick(FVector::RightVector, true, Tuning);
	TestTrue(TEXT("Kicked: active"), Ends.IsActive(Tuning));
	for (int32 Index = 0; Index < 120; ++Index)
	{
		Ends.Advance(1.f / 60.f, Tuning);
	}
	TestFalse(TEXT("Two seconds on: at rest and switched off"), Ends.IsActive(Tuning));
	TestEqual(TEXT("with the offsets zeroed, not left a hair off"), Ends.GetHeadOffset().Size(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRecoilRotations, "Hawkeye.HitStagger.RecoilTipsAwayFromTheBlow", HawkeyeHitStaggerTest::Flags)

bool FHawkeyeRecoilRotations::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("No offset: no rotation"), FHawkeyeHitRecoil::MakeTipRotation(FVector::ZeroVector).IsNearlyZero());

	// The body's up tips toward the push (+X): the upper body leans away from the attacker.
	const FVector Tipped = FHawkeyeHitRecoil::MakeTipRotation(FVector(10.f, 0.f, 0.f)).RotateVector(FVector::UpVector);
	TestEqual(TEXT("10 degrees toward +X: up leans by sin(10)"), Tipped.X, FMath::Sin(FMath::DegreesToRadians(10.f)), 0.005f);
	const FVector Sideways = FHawkeyeHitRecoil::MakeTipRotation(FVector(0.f, -10.f, 0.f)).RotateVector(FVector::UpVector);
	TestTrue(TEXT("A push to -Y leans him to -Y"), Sideways.Y < -0.15f);

	// The head: a blow from the right (pushing him left, -Y) turns the face left; from the left, right.
	const FVector Right = FVector::RightVector;
	const FVector FaceFromRight = FHawkeyeHitRecoil::MakeHeadRotation(FVector(0.f, -10.f, 0.f), Right, 0.6f).RotateVector(FVector::ForwardVector);
	TestTrue(FString::Printf(TEXT("Hit from the right: the face turns left (Y %.3f)"), FaceFromRight.Y), FaceFromRight.Y < -0.05f);
	const FVector FaceFromLeft = FHawkeyeHitRecoil::MakeHeadRotation(FVector(0.f, 10.f, 0.f), Right, 0.6f).RotateVector(FVector::ForwardVector);
	TestTrue(TEXT("Hit from the left: it turns right"), FaceFromLeft.Y > 0.05f);
	const FVector FaceNod = FHawkeyeHitRecoil::MakeHeadRotation(FVector(-10.f, 0.f, 0.f), Right, 0.6f).RotateVector(FVector::ForwardVector);
	TestEqual(TEXT("A blow straight on only nods (no yaw)"), FaceNod.Y, 0.f, 0.001f);
	const FVector FaceNoYaw = FHawkeyeHitRecoil::MakeHeadRotation(FVector(0.f, -10.f, 0.f), Right, 0.f).RotateVector(FVector::ForwardVector);
	TestEqual(TEXT("A yaw share of 0 keeps a sideways blow a pure tilt"), FaceNoYaw.Y, 0.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugRecoilsWithoutAClip, "Hawkeye.HitStagger.ThugRecoilsWithoutAClip", HawkeyeHitStaggerTest::Flags)

bool FHawkeyeThugRecoilsWithoutAClip::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	AActor* Attacker = TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(100.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f));
	if (!TestNotNull(TEXT("A thug"), Thug) || !TestNotNull(TEXT("An attacker"), Attacker) || !TestNotNull(TEXT("A hit reaction"), Thug->GetHitReactionComponent()))
	{
		return false;
	}
	const UHitReactionComponent* Reaction = Thug->GetHitReactionComponent();
	TestFalse(TEXT("Before the blow: still"), Reaction->GetRecoil().IsActive(Reaction->RecoilTuning));

	Thug->HitReaction(Attacker);
	TestTrue(TEXT("No clip set, no impact, and the thug still recoils"), Reaction->GetRecoil().IsActive(Reaction->RecoilTuning));
	TestTrue(TEXT("away from the attacker (the attacker is at +X, so he tips to -X)"), Reaction->GetRecoil().GetSpineOffset().X < 0.f
		|| Reaction->GetRecoil().Spine.Rate.X < 0.f);
	TestTrue(TEXT("and the head's kick is the larger"), Reaction->GetRecoil().Head.Rate.Size() > Reaction->GetRecoil().Spine.Rate.Size());
	return true;
}

#endif
