#include "Combat/CombatAnimSet.h"
#include "Combat/HealthComponent.h"
#include "Combat/HitContactRules.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMeleePress.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Vfx/HawkeyeVfxMath.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The weld and the melee feel changes of 2026-11-23 (claude-docs/gameplay-semantics.md, "The weld"): the vibration, the
 * victim's alignment clamps, the knock-back's profile, where a reaction clip starts and which one plays, the variant
 * choice by situation, the light on the press and the heavy on the hold, and the puff's variants. Pure rules first, then
 * Kate and a thug in a test world for the press flow.
 */
namespace HawkeyeMeleeWeldTest
{
	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector,
			FRotator::ZeroRotator));
	}

	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator(0.f, 180.f, 0.f)));
		if (Thug)
		{
			Thug->Weapon = EThugWeapon::Fists;
		}
		return Thug;
	}

	static TSoftObjectPtr<UAnimMontage> Path(const TCHAR* Name)
	{
		return TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(FString::Printf(TEXT("/Game/Test/%s.%s"), Name, Name)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldVibration, "Hawkeye.MeleeWeld.VibrationDecaysAndFlips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldVibration::RunTest(const FString& Parameters)
{
	const FHawkeyeWeldTuning Weld;
	TestEqual(TEXT("A light: 2.5 cm"), UHawkeyeHitContactRules::ComputeVibrationAmplitude(0.3f, Weld), 2.5f, 0.001f);
	TestEqual(TEXT("A heavy: 4 cm"), UHawkeyeHitContactRules::ComputeVibrationAmplitude(1.f, Weld), 4.f, 0.001f);
	TestEqual(TEXT("Lighter than a light is still 2.5"), UHawkeyeHitContactRules::ComputeVibrationAmplitude(0.f, Weld), 2.5f, 0.001f);
	TestEqual(TEXT("A light flips twice"), UHawkeyeHitContactRules::ComputeVibrationFrames(0.3f, Weld), 2);
	TestEqual(TEXT("A heavy four times"), UHawkeyeHitContactRules::ComputeVibrationFrames(1.f, Weld), 4);
	const int32 Mid = UHawkeyeHitContactRules::ComputeVibrationFrames(0.65f, Weld);
	TestTrue(TEXT("Between them in between"), Mid >= 2 && Mid <= 4);

	// A heavy's 90 ms in four equal steps: each the other side of the last and smaller.
	const float Duration = 0.09f;
	TArray<float> Steps;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Steps.Add(UHawkeyeHitContactRules::ComputeVibrationOffset((Step + 0.5f) * Duration / 4.f, Duration, 1.f, false, Weld));
	}
	TestEqual(TEXT("The first step is the full amplitude, along the blow"), Steps[0], 4.f, 0.001f);
	TestTrue(TEXT("Then the other side, smaller"), Steps[1] < 0.f && FMath::Abs(Steps[1]) < Steps[0]);
	TestTrue(TEXT("Then back, smaller still"), Steps[2] > 0.f && Steps[2] < FMath::Abs(Steps[1]));
	TestTrue(TEXT("And the last the smallest, on the far side"), Steps[3] < 0.f && FMath::Abs(Steps[3]) < Steps[2]);
	TestEqual(TEXT("The other party starts the other way"),
		UHawkeyeHitContactRules::ComputeVibrationOffset(0.001f, Duration, 1.f, true, Weld), -4.f, 0.001f);
	TestEqual(TEXT("Nothing after the freeze"), UHawkeyeHitContactRules::ComputeVibrationOffset(Duration + 0.01f, Duration, 1.f, false, Weld), 0.f);
	TestEqual(TEXT("Nothing before it"), UHawkeyeHitContactRules::ComputeVibrationOffset(-0.01f, Duration, 1.f, false, Weld), 0.f);
	TestEqual(TEXT("No freeze, no vibration"), UHawkeyeHitContactRules::ComputeVibrationOffset(0.01f, 0.f, 1.f, false, Weld), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldAlignment, "Hawkeye.MeleeWeld.AlignmentClamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldAlignment::RunTest(const FString& Parameters)
{
	const FHawkeyeWeldTuning Weld;
	// The turn: toward the attacker, the short way round, never more than 25 degrees.
	TestEqual(TEXT("10 degrees off: the whole turn"), UHawkeyeHitContactRules::ComputeVictimTurn(0.f, 10.f, Weld.MaxTurnDegrees), 10.f, 0.001f);
	TestEqual(TEXT("90 off: capped at 25"), UHawkeyeHitContactRules::ComputeVictimTurn(0.f, 90.f, Weld.MaxTurnDegrees), 25.f, 0.001f);
	TestEqual(TEXT("The other way: -25"), UHawkeyeHitContactRules::ComputeVictimTurn(0.f, -120.f, Weld.MaxTurnDegrees), -25.f, 0.001f);
	TestEqual(TEXT("Across the seam: 350 to 10 is +20, not -340"), UHawkeyeHitContactRules::ComputeVictimTurn(350.f, 10.f, Weld.MaxTurnDegrees), 20.f, 0.001f);
	TestEqual(TEXT("Facing him already: none"), UHawkeyeHitContactRules::ComputeVictimTurn(40.f, 40.f, Weld.MaxTurnDegrees), 0.f, 0.001f);

	// The nudge along the line: +away, -toward, 15 at most, none without data.
	TestEqual(TEXT("Too close by 8: moved 8 away"), UHawkeyeHitContactRules::ComputeVictimNudge(70.f, 78.f, Weld.MaxNudgeCm), 8.f, 0.001f);
	TestEqual(TEXT("Too far by 8: moved 8 toward"), UHawkeyeHitContactRules::ComputeVictimNudge(86.f, 78.f, Weld.MaxNudgeCm), -8.f, 0.001f);
	TestEqual(TEXT("Too close by 40: capped at 15"), UHawkeyeHitContactRules::ComputeVictimNudge(40.f, 80.f, Weld.MaxNudgeCm), 15.f, 0.001f);
	TestEqual(TEXT("Too far by 60: capped at -15"), UHawkeyeHitContactRules::ComputeVictimNudge(140.f, 80.f, Weld.MaxNudgeCm), -15.f, 0.001f);
	TestEqual(TEXT("No designed distance: none"), UHawkeyeHitContactRules::ComputeVictimNudge(100.f, 0.f, Weld.MaxNudgeCm), 0.f);

	// Who is aligned: a thug on his feet; not the dead, the winding up, the down, or the player.
	TestTrue(TEXT("A thug on his feet"), UHawkeyeHitContactRules::ShouldAlignVictim(true, false, false, false));
	TestFalse(TEXT("Dead"), UHawkeyeHitContactRules::ShouldAlignVictim(false, false, false, false));
	TestFalse(TEXT("Winding up a swing: he keeps it"), UHawkeyeHitContactRules::ShouldAlignVictim(true, true, false, false));
	TestFalse(TEXT("On the floor"), UHawkeyeHitContactRules::ShouldAlignVictim(true, false, true, false));
	TestFalse(TEXT("Kate: her facing and her place are hers"), UHawkeyeHitContactRules::ShouldAlignVictim(true, false, false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldKnockback, "Hawkeye.MeleeWeld.KnockbackProfileCoversTheDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldKnockback::RunTest(const FString& Parameters)
{
	const FHawkeyeWeldTuning Weld;
	for (const float Distance : { Weld.KnockbackLightCm, Weld.KnockbackHeavyCm, 5.f })
	{
		float Peak = 0.f;
		TArray<float> Strengths;
		UHawkeyeHitContactRules::ComputeKnockbackProfile(Distance, Weld.KnockbackSeconds, Weld.KnockbackEase, 9, Peak, Strengths);
		TestEqual(TEXT("Nine samples"), Strengths.Num(), 9);
		TestEqual(TEXT("It starts at full strength"), Strengths[0], 1.f, 0.0001f);
		TestEqual(TEXT("And ends at none"), Strengths.Last(), 0.f, 0.0001f);
		// The displacement a linear curve over the samples gives (trapezoids): exactly the distance asked for.
		float Area = 0.f;
		for (int32 Index = 1; Index < Strengths.Num(); ++Index)
		{
			Area += 0.5f * (Strengths[Index - 1] + Strengths[Index]) / 8.f;
		}
		TestEqual(TEXT("The total displacement is the distance (the old shove's: 10 and 28 cm)"), Peak * Weld.KnockbackSeconds * Area, Distance, 0.01f);
		// Eased: fast at the start, slow to settle.
		TestTrue(TEXT("Over half the distance in the first third of the time"), Strengths[0] + Strengths[1] + Strengths[2] > Strengths[6] + Strengths[7] + Strengths[8] + 1.f);
		for (int32 Index = 1; Index < Strengths.Num(); ++Index)
		{
			TestTrue(TEXT("Never speeds up"), Strengths[Index] <= Strengths[Index - 1] + 0.0001f);
		}
	}
	float Peak = 1.f;
	TArray<float> Strengths;
	UHawkeyeHitContactRules::ComputeKnockbackProfile(0.f, 0.3f, 2.f, 9, Peak, Strengths);
	TestEqual(TEXT("No distance, no speed"), Peak, 0.f);
	UHawkeyeHitContactRules::ComputeKnockbackProfile(10.f, 0.f, 2.f, 9, Peak, Strengths);
	TestEqual(TEXT("No time, no speed"), Peak, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldReactionChoice, "Hawkeye.MeleeWeld.ReactionStartAndChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldReactionChoice::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeWeldTest;
	const FHawkeyeWeldTuning Weld;

	// Where the clip starts: on its authored hit pose with no blend, else from its start blending in a little.
	FHawkeyeReactionStart Start = UHawkeyeHitContactRules::ComputeReactionStart(0.11f, 0.55f, Weld);
	TestEqual(TEXT("At the hit peak"), Start.StartAtSeconds, 0.11f, 0.0001f);
	TestEqual(TEXT("With no blend: a snap"), Start.BlendInSeconds, 0.f);
	Start = UHawkeyeHitContactRules::ComputeReactionStart(-1.f, 0.55f, Weld);
	TestEqual(TEXT("No authored peak: from the start"), Start.StartAtSeconds, 0.f);
	TestEqual(TEXT("Blending in a little"), Start.BlendInSeconds, Weld.UnauthoredBlendInSeconds, 0.0001f);
	Start = UHawkeyeHitContactRules::ComputeReactionStart(0.9f, 0.55f, Weld);
	TestEqual(TEXT("A peak past the clip's end is not one"), Start.StartAtSeconds, 0.f);

	// The flinch only adds to the clip.
	TestEqual(TEXT("The physics blend is capped at 0.5"), UHawkeyeHitContactRules::ClampPhysicalBlend(0.8f, Weld), 0.5f, 0.0001f);
	TestEqual(TEXT("Below the cap it is left"), UHawkeyeHitContactRules::ClampPhysicalBlend(0.2f, Weld), 0.2f, 0.0001f);
	TestEqual(TEXT("The reaction tuning's own peak is the cap now"), FHawkeyeHitReactionTuning().PeakBlend, 0.5f, 0.0001f);

	// The region a struck bone is in, and the one a contact height is nearest.
	TestEqual(TEXT("head"), UHawkeyeHitContactRules::ClassifyBoneRegion(FName(TEXT("head"))), EHawkeyeContactRegion::Head);
	TestEqual(TEXT("neck_01"), UHawkeyeHitContactRules::ClassifyBoneRegion(FName(TEXT("neck_01"))), EHawkeyeContactRegion::Head);
	TestEqual(TEXT("spine_03"), UHawkeyeHitContactRules::ClassifyBoneRegion(FName(TEXT("spine_03"))), EHawkeyeContactRegion::Torso);
	TestEqual(TEXT("upperarm_l"), UHawkeyeHitContactRules::ClassifyBoneRegion(FName(TEXT("upperarm_l"))), EHawkeyeContactRegion::Torso);
	TestEqual(TEXT("thigh_r"), UHawkeyeHitContactRules::ClassifyBoneRegion(FName(TEXT("thigh_r"))), EHawkeyeContactRegion::Legs);
	TestEqual(TEXT("foot_l"), UHawkeyeHitContactRules::ClassifyBoneRegion(FName(TEXT("foot_l"))), EHawkeyeContactRegion::Legs);
	TestEqual(TEXT("No bone: the torso"), UHawkeyeHitContactRules::ClassifyBoneRegion(NAME_None), EHawkeyeContactRegion::Torso);
	TestEqual(TEXT("A fist at 160 cm: his head"), UHawkeyeHitContactRules::SelectRegionForHeight(160.f, 165.f, 125.f, 60.f, EHawkeyeContactRegion::Torso), EHawkeyeContactRegion::Head);
	TestEqual(TEXT("A foot at 90 cm: his gut"), UHawkeyeHitContactRules::SelectRegionForHeight(100.f, 165.f, 125.f, 60.f, EHawkeyeContactRegion::Head), EHawkeyeContactRegion::Torso);
	TestEqual(TEXT("A kick at 50: his legs"), UHawkeyeHitContactRules::SelectRegionForHeight(50.f, 165.f, 125.f, 60.f, EHawkeyeContactRegion::Head), EHawkeyeContactRegion::Legs);
	TestEqual(TEXT("Unknown height: the attack's own region"), UHawkeyeHitContactRules::SelectRegionForHeight(-1.f, 165.f, 125.f, 60.f, EHawkeyeContactRegion::Legs), EHawkeyeContactRegion::Legs);
	// The zone follows where the limb really met him when the clip has no baked height, and the clip's height wins when it has.
	TestEqual(TEXT("Clip height wins"), UHawkeyeHitContactRules::PickStrikeHeight(100.f, 160.f), 100.f);
	TestEqual(TEXT("No clip height: the limb's"), UHawkeyeHitContactRules::PickStrikeHeight(-1.f, 128.f), 128.f);
	TestEqual(TEXT("Neither: unknown"), UHawkeyeHitContactRules::PickStrikeHeight(-1.f, -1.f), -1.f);
	TestEqual(TEXT("A chest-height jab with no clip height: his gut, not his head"),
		UHawkeyeHitContactRules::SelectRegionForHeight(UHawkeyeHitContactRules::PickStrikeHeight(-1.f, 128.f), 165.f, 130.f, 90.f,
			EHawkeyeContactRegion::Head), EHawkeyeContactRegion::Torso);

	// The knock-back nets out to the tuning over the nudge before it.
	TestEqual(TEXT("No nudge: the tuned distance"), UHawkeyeHitContactRules::ComputeNetKnockbackCm(10.f, FVector::ZeroVector, FVector::ForwardVector), 10.f, 0.001f);
	TestEqual(TEXT("Pulled 8 toward the attacker: 8 more"), UHawkeyeHitContactRules::ComputeNetKnockbackCm(10.f, FVector(-8.f, 0.f, 0.f), FVector::ForwardVector), 18.f, 0.001f);
	TestEqual(TEXT("Pushed 4 along it: 4 less"), UHawkeyeHitContactRules::ComputeNetKnockbackCm(10.f, FVector(4.f, 0.f, 0.f), FVector::ForwardVector), 6.f, 0.001f);
	TestEqual(TEXT("Pushed past the tuning: none"), UHawkeyeHitContactRules::ComputeNetKnockbackCm(10.f, FVector(15.f, 0.f, 0.f), FVector::ForwardVector), 0.f, 0.001f);
	TestEqual(TEXT("A sideways nudge changes nothing"), UHawkeyeHitContactRules::ComputeNetKnockbackCm(10.f, FVector(0.f, 9.f, 0.f), FVector::ForwardVector), 10.f, 0.001f);

	// Which clip: by direction, zone and weight.
	UCombatAnimSet* Set = NewObject<UCombatAnimSet>(GetTransientPackage());
	Set->HitFront = Path(TEXT("AM_HitFront"));
	Set->HitLeft = Path(TEXT("AM_HitLeft"));
	Set->HitHeavy = Path(TEXT("AM_HitHeavy"));
	TestEqual(TEXT("No zone clips: the side's"), UCombatAnimSet::HitRoleForImpact(Set, EHawkeyeHitDirection::Front, EHawkeyeContactRegion::Head, 0.3f, 0.6f),
		ECombatAnimRole::HitFront);
	TestEqual(TEXT("No zone clips, heavy: the stagger"), UCombatAnimSet::HitRoleForImpact(Set, EHawkeyeHitDirection::Front, EHawkeyeContactRegion::Torso, 1.f, 0.6f),
		ECombatAnimRole::HitHeavy);
	Set->HitHeadLight = Path(TEXT("AM_HitHeadLight"));
	Set->HitGutHeavy = Path(TEXT("AM_HitGutHeavy"));
	Set->HitLegsLight = Path(TEXT("AM_HitLegsLight"));
	TestEqual(TEXT("A light to the head from the front: the head snap"),
		UCombatAnimSet::HitRoleForImpact(Set, EHawkeyeHitDirection::Front, EHawkeyeContactRegion::Head, 0.3f, 0.6f), ECombatAnimRole::HitHeadLight);
	TestEqual(TEXT("A heavy to the ribs: the gut fold"),
		UCombatAnimSet::HitRoleForImpact(Set, EHawkeyeHitDirection::Front, EHawkeyeContactRegion::Torso, 1.f, 0.6f), ECombatAnimRole::HitGutHeavy);
	TestEqual(TEXT("A light to the legs: the buckle"),
		UCombatAnimSet::HitRoleForImpact(Set, EHawkeyeHitDirection::Front, EHawkeyeContactRegion::Legs, 0.3f, 0.6f), ECombatAnimRole::HitLegsLight);
	TestEqual(TEXT("A light to the gut with no light gut clip: the side's"),
		UCombatAnimSet::HitRoleForImpact(Set, EHawkeyeHitDirection::Front, EHawkeyeContactRegion::Torso, 0.3f, 0.6f), ECombatAnimRole::HitFront);
	TestEqual(TEXT("A hook from his left: the side's clip, whatever the zone"),
		UCombatAnimSet::HitRoleForImpact(Set, EHawkeyeHitDirection::Left, EHawkeyeContactRegion::Head, 0.3f, 0.6f), ECombatAnimRole::HitLeft);
	TestTrue(TEXT("The heavy zone clips are heavy"), UCombatAnimSet::IsHeavyHitRole(ECombatAnimRole::HitGutHeavy) && UCombatAnimSet::IsHeavyHitRole(ECombatAnimRole::HitHeavy));
	TestFalse(TEXT("The light ones are not"), UCombatAnimSet::IsHeavyHitRole(ECombatAnimRole::HitHeadLight));
	TestEqual(TEXT("A zone slot counts as assigned"), Set->CountAssigned(), 6);

	// What a clip says about itself, found by its montage.
	FCombatClipData Data;
	Data.Montage = Path(TEXT("AM_HitHeadLight"));
	Data.HitPeakTime = 0.09f;
	Data.KnockbackCm = 11.f;
	Set->ClipData.Add(Data);
	const FCombatClipData* Found = Set->FindClipDataByPath(FSoftObjectPath(TEXT("/Game/Test/AM_HitHeadLight.AM_HitHeadLight")));
	TestNotNull(TEXT("Found by path"), Found);
	if (Found)
	{
		TestTrue(TEXT("It has its hit pose"), Found->HasHitPeak());
		TestEqual(TEXT("And its knock-back"), Found->KnockbackCm, 11.f);
		TestEqual(TEXT("A strike field it does not have is unknown"), Found->ContactDistance, -1.f);
	}
	TestNull(TEXT("Another clip has none"), Set->FindClipDataByPath(FSoftObjectPath(TEXT("/Game/Test/AM_Other.AM_Other"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldVariantScoring, "Hawkeye.MeleeWeld.VariantChoiceBySituation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldVariantScoring::RunTest(const FString& Parameters)
{
	// Jab (short, head height, straight), cross (longer), hook (wide, from the side).
	TArray<FHawkeyeVariantCandidate> Candidates;
	FHawkeyeVariantCandidate Jab;
	Jab.ContactDistance = 70.f;
	Jab.ContactHeight = 150.f;
	FHawkeyeVariantCandidate Cross;
	Cross.ContactDistance = 95.f;
	Cross.ContactHeight = 145.f;
	FHawkeyeVariantCandidate Hook;
	Hook.ContactDistance = 85.f;
	Hook.ContactHeight = 150.f;
	Hook.bWideSwing = true;
	Candidates = { Jab, Cross, Hook };

	FHawkeyeVariantSituation Close;
	Close.FlatDistance = 70.f;
	Close.AngleDegrees = 5.f;
	Close.TargetHeight = 150.f;
	TestEqual(TEXT("A man in her face, dead ahead: the jab"), UHawkeyeMeleeRules::PickAttackVariant(Candidates, Close), 0);

	FHawkeyeVariantSituation Far = Close;
	Far.FlatDistance = 180.f;
	TestEqual(TEXT("Across the street: the long reach, not the short"), UHawkeyeMeleeRules::PickAttackVariant(Candidates, Far), 1);

	FHawkeyeVariantSituation Side = Close;
	Side.FlatDistance = 90.f;
	Side.AngleDegrees = 70.f;
	TestEqual(TEXT("Well off to the side: the hook"), UHawkeyeMeleeRules::PickAttackVariant(Candidates, Side), 2);

	FHawkeyeVariantSituation Repeat = Close;
	Repeat.LastIndex = 0;
	TestTrue(TEXT("The jab again: the cost of repeating it can send her to the next best"),
		UHawkeyeMeleeRules::ScoreAttackVariant(Jab, Repeat, 0) < UHawkeyeMeleeRules::ScoreAttackVariant(Jab, Close, 0));

	// A man on the floor: the low clip.
	FHawkeyeVariantCandidate Stomp;
	Stomp.ContactDistance = 70.f;
	Stomp.ContactHeight = 30.f;
	FHawkeyeVariantSituation Down = Close;
	Down.TargetHeight = 20.f;
	TestEqual(TEXT("On the floor: the low one"), UHawkeyeMeleeRules::PickAttackVariant({ Jab, Stomp }, Down), 1);

	TestEqual(TEXT("No data on any: none, so the manifest order cycles as before"),
		UHawkeyeMeleeRules::PickAttackVariant({ FHawkeyeVariantCandidate(), FHawkeyeVariantCandidate() }, Close), INDEX_NONE);
	TestEqual(TEXT("No candidates: none"), UHawkeyeMeleeRules::PickAttackVariant({}, Close), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldPressHold, "Hawkeye.MeleeWeld.LightOnPressHeavyOnHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldPressHold::RunTest(const FString& Parameters)
{
	using Outcome = EHawkeyeHoldOutcome;
	using Phase = EHawkeyeSwingPhase;
	TestEqual(TEXT("Nothing swinging: the heavy starts"), FHawkeyeMeleePress::ResolveHold(true, Phase::None), Outcome::StartHeavy);
	TestEqual(TEXT("Her light before its hit window: it converts"), FHawkeyeMeleePress::ResolveHold(true, Phase::BeforeHit), Outcome::ConvertLight);
	TestEqual(TEXT("A swing that is not this press's, before its hit: the heavy is armed behind it"),
		FHawkeyeMeleePress::ResolveHold(false, Phase::BeforeHit), Outcome::ArmHeavy);
	TestEqual(TEXT("Her light already hit: the heavy is armed as the next blow"), FHawkeyeMeleePress::ResolveHold(true, Phase::AfterHit), Outcome::ArmHeavy);

	// A tap: the light started on the press, the release does nothing.
	FHawkeyeMeleePress Tap;
	Tap.Press(true);
	TestEqual(TEXT("0.1 s in: nothing"), Tap.Tick(0.1f, Phase::BeforeHit), Outcome::None);
	TestFalse(TEXT("The release starts nothing: the light went on the press"), Tap.Release());
	TestEqual(TEXT("And no hold follows"), Tap.Tick(1.f, Phase::None), Outcome::None);

	// A hold with the light still winding up converts, once.
	FHawkeyeMeleePress Hold;
	Hold.Press(true);
	TestEqual(TEXT("0.39 s: not yet"), Hold.Tick(0.39f, Phase::BeforeHit), Outcome::None);
	TestEqual(TEXT("0.4 s: the light converts"), Hold.Tick(0.01f, Phase::BeforeHit), Outcome::ConvertLight);
	TestEqual(TEXT("Only once"), Hold.Tick(0.1f, Phase::BeforeHit), Outcome::None);
	TestFalse(TEXT("A converted hold arms nothing"), Hold.IsArmed());

	// The usual hold: the light has hit by 0.4 s, so the heavy is armed and goes when she can swing.
	FHawkeyeMeleePress Armed;
	Armed.Press(true);
	TestEqual(TEXT("The light has hit: the heavy is armed"), Armed.Tick(0.41f, Phase::AfterHit), Outcome::ArmHeavy);
	TestTrue(TEXT("Armed"), Armed.IsArmed());
	TestFalse(TEXT("The release starts no light"), Armed.Release());
	TestTrue(TEXT("Armed still, inside the grace after the release"), Armed.Tick(0.2f, Phase::AfterHit) == Outcome::None && Armed.IsArmed());
	Armed.Tick(0.2f, Phase::AfterHit);
	TestFalse(TEXT("Dropped when the grace (the light buffer's 0.3 s) is spent"), Armed.IsArmed());

	FHawkeyeMeleePress Taken;
	Taken.Press(true);
	Taken.Tick(0.5f, Phase::AfterHit);
	TestTrue(TEXT("The heavy goes once"), Taken.TakeArmed());
	TestFalse(TEXT("And is spent"), Taken.TakeArmed());

	// The old scheme: the light on the release, the heavy at the threshold.
	FHawkeyeMeleePress Old;
	Old.bLightOnPress = false;
	Old.Press(false);
	TestTrue(TEXT("Released early: the release starts the light"), Old.Release());
	Old.Press(false);
	TestEqual(TEXT("Held: the heavy starts at the threshold"), Old.Tick(0.4f, Phase::None), Outcome::StartHeavy);
	TestFalse(TEXT("And the release then starts no light"), Old.Release());

	// A cancelled press (a stagger, a parry) is nothing.
	FHawkeyeMeleePress Cancelled;
	Cancelled.Press(true);
	Cancelled.Cancel();
	TestEqual(TEXT("No hold"), Cancelled.Tick(1.f, Phase::BeforeHit), Outcome::None);
	TestFalse(TEXT("No release"), Cancelled.Release());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldKatePress, "Hawkeye.MeleeWeld.KatePressStartsTheLight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldKatePress::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeWeldTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	Thug->GetHealthComponent()->SetMaxHealth(500.f, true);
	TestTrue(TEXT("The press starts the light by default"), Kate->bLightAttackOnPress);

	// A tap: the light is going the moment the button is down; letting go starts nothing more.
	Kate->PressMelee();
	TestTrue(TEXT("The light starts on the press"), Kate->IsMeleeAttacking());
	TestEqual(TEXT("It is the chain's first"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	Kate->ReleaseMelee();
	TestEqual(TEXT("The release changes nothing"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	Melee->AdvanceAttack(1.f);
	Kate->AdvanceMeleeFlow(1.f);

	// A hold while the light is still winding up: the light converts into the heavy.
	Kate->PressMelee();
	TestEqual(TEXT("A light again"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	TestTrue(TEXT("Winding up"), Melee->IsWindingUp());
	Kate->UpdateMeleeHold(Kate->GetHeavyHoldSeconds() + 0.01f);
	TestEqual(TEXT("The hold turned it into the heavy"), Melee->GetCurrentAttack().Name, FName(TEXT("heavy")));
	Kate->ReleaseMelee();
	Melee->AdvanceAttack(2.f);
	Kate->AdvanceMeleeFlow(2.f);

	// A hold after the light hit: the heavy is armed and goes as soon as she can swing.
	Kate->PressMelee();
	Melee->AdvanceAttack(Melee->GetCurrentAttack().WindupSeconds + 0.01f);
	Kate->UpdateMeleeHold(Kate->GetHeavyHoldSeconds() + 0.01f);
	TestTrue(TEXT("The light hit: the heavy is armed behind it"), Kate->GetMeleePress().IsArmed());
	TestEqual(TEXT("Still the light's recovery"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	Melee->AdvanceAttack(1.f);
	Kate->AdvanceMeleeFlow(0.01f);
	TestEqual(TEXT("The armed heavy went when she could swing"), Melee->GetCurrentAttack().Name, FName(TEXT("heavy")));
	TestFalse(TEXT("And is spent"), Kate->GetMeleePress().IsArmed());
	Kate->ReleaseMelee();
	Melee->AdvanceAttack(2.f);
	Kate->AdvanceMeleeFlow(2.f);

	// The switch back to the old scheme: nothing on the press, the light on the release.
	Kate->bLightAttackOnPress = false;
	Kate->PressMelee();
	TestFalse(TEXT("Off: the press starts nothing"), Kate->IsMeleeAttacking());
	Kate->ReleaseMelee();
	TestTrue(TEXT("The release starts the light"), Kate->IsMeleeAttacking());
	Melee->AdvanceAttack(2.f);
	Kate->AdvanceMeleeFlow(2.f);
	Kate->PressMelee();
	Kate->UpdateMeleeHold(Kate->GetHeavyHoldSeconds() + 0.01f);
	TestEqual(TEXT("Off: a hold is the heavy, as before"), Melee->GetCurrentAttack().Name, FName(TEXT("heavy")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldPuff, "Hawkeye.MeleeWeld.PuffVariantsAndScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldPuff::RunTest(const FString& Parameters)
{
	// Four variants, never the same one twice running, and all four come up.
	int32 Last = -1;
	TSet<int32> Seen;
	for (int32 Blow = 0; Blow < 40; ++Blow)
	{
		const int32 Variant = HawkeyeVfxMath::PickPuffVariant(4, Blow, Last);
		TestTrue(TEXT("In range"), Variant >= 0 && Variant < 4);
		TestNotEqual(TEXT("Never the last again"), Variant, Last);
		Seen.Add(Variant);
		Last = Variant;
	}
	TestEqual(TEXT("All four are used"), Seen.Num(), 4);
	TestEqual(TEXT("The same order every run"), HawkeyeVfxMath::PickPuffVariant(4, 7, -1), HawkeyeVfxMath::PickPuffVariant(4, 7, -1));
	TestEqual(TEXT("One variant: always 0"), HawkeyeVfxMath::PickPuffVariant(1, 3, 0), 0);
	TestEqual(TEXT("A light's puff"), HawkeyeVfxMath::ComputePuffScale(0.f), 0.8f, 0.001f);
	TestEqual(TEXT("A heavy's puff"), HawkeyeVfxMath::ComputePuffScale(1.f), 1.5f, 0.001f);
	TestTrue(TEXT("Heavier, bigger"), HawkeyeVfxMath::ComputePuffScale(0.7f) > HawkeyeVfxMath::ComputePuffScale(0.3f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeldFreezeTwoParties, "Hawkeye.MeleeWeld.BothPartiesFreezeTheWorldRunsOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeldFreezeTwoParties::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeWeldTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f));
	AThugCharacter* Bystander = SpawnThug(TestWorld, FVector(0.f, 300.f, 0.f));
	if (!Kate || !Thug || !Bystander)
	{
		AddError(TEXT("Failed to spawn Kate and two thugs."));
		return false;
	}
	Thug->GetHealthComponent()->SetMaxHealth(500.f, true);
	TestTrue(TEXT("Her light starts"), Kate->StartLightAttack());
	Kate->GetMeleeComponent()->AdvanceAttack(Kate->GetMeleeComponent()->GetCurrentAttack().WindupSeconds + 0.01f);
	const float Frozen = Thug->GetHitReactionComponent()->Weld.FreezeDilation + 0.0001f;
	TestTrue(TEXT("Kate is held on the hit pose"), Kate->GetHitReactionComponent()->IsFrozen() && Kate->CustomTimeDilation <= Frozen);
	TestTrue(TEXT("So is the thug: held by his montage, his own clock left alone"), Thug->GetHitReactionComponent()->IsFrozen()
		&& Thug->CustomTimeDilation > Frozen);
	TestEqual(TEXT("The bystander is not"), Bystander->CustomTimeDilation, 1.f);
	TestEqual(TEXT("And the world's time is not touched"), Kate->GetWorld()->GetWorldSettings()->TimeDilation, 1.f, 0.0001f);
	TestEqual(TEXT("Her freeze is her light's hit-stop (62 ms)"), Kate->GetHitReactionComponent()->GetFreezeSeconds(), 0.062f, 0.002f);

	// The next blow inside the gap still freezes them, for 40% of its own.
	Kate->GetMeleeComponent()->AdvanceAttack(Kate->GetMeleeComponent()->GetPhaseRemaining() + 0.01f);
	Kate->AdvanceMeleeFlow(0.01f);
	TestTrue(TEXT("The second light starts"), Kate->StartLightAttack());
	Kate->GetMeleeComponent()->AdvanceAttack(Kate->GetMeleeComponent()->GetCurrentAttack().WindupSeconds + 0.01f);
	TestEqual(TEXT("A second freeze: the chain freezes on every hit"), Thug->GetHitReactionComponent()->GetFreezeCount(), 2);
	TestEqual(TEXT("For 40% of the first's length"), Thug->GetHitReactionComponent()->GetFreezeSeconds(), 0.062f * 0.4f, 0.002f);
	return true;
}

#endif
