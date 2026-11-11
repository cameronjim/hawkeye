#include "Combat/HealthComponent.h"
#include "Combat/HitContactRules.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Contact, reaction and knockout (claude-docs/gameplay-semantics.md): the warp's caps and the honest whiff, the region a
 * blow aims at, the limb and the way it travels, the reaction it picks, the hit-stop and who it holds, and a knockout's
 * throw. Pure rules first, then a thug and Kate in a test world for what must not change (parry, guard, the telegraph).
 */
namespace HawkeyeHitContactTest
{
	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator(0.f, 180.f, 0.f)));
		if (Thug)
		{
			Thug->Weapon = EThugWeapon::Fists;
		}
		return Thug;
	}

	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector,
			FRotator::ZeroRotator));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitContactWarpCaps, "Hawkeye.HitContact.WarpCapAndWhiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHitContactWarpCaps::RunTest(const FString& Parameters)
{
	const FVector Origin(0.f, 0.f, 90.f);
	const FVector Forward = FVector::ForwardVector;

	// 200 cm ahead, contact 80: slide 120, inside a 150 cap; stands 80 short at her own height, facing him.
	FHawkeyeContactPlan Plan = UHawkeyeHitContactRules::PlanContact(Origin, Forward, FVector(200.f, 0.f, 150.f), 80.f, 150.f, 75.f);
	TestTrue(TEXT("In reach"), Plan.bReachable);
	TestEqual(TEXT("Slides 120"), Plan.Slide, 120.f, 0.01f);
	TestEqual(TEXT("Stands 80 short of the region's bone"), static_cast<float>(Plan.StandPoint.X), 120.f, 0.01f);
	TestEqual(TEXT("At her own height (the bone's height is not the floor)"), static_cast<float>(Plan.StandPoint.Z), 90.f, 0.01f);
	TestEqual(TEXT("No turn"), Plan.TurnDegrees, 0.f, 0.01f);

	// Already inside the contact distance: no slide and no backing off, only the turn.
	Plan = UHawkeyeHitContactRules::PlanContact(Origin, Forward, FVector(0.f, 60.f, 90.f), 80.f, 150.f, 120.f);
	TestTrue(TEXT("Inside the contact distance: in reach"), Plan.bReachable);
	TestEqual(TEXT("No slide"), Plan.Slide, 0.f);
	TestTrue(TEXT("She stays where she is"), Plan.StandPoint.Equals(Origin, 0.01f));
	TestEqual(TEXT("A quarter turn"), Plan.TurnDegrees, 90.f, 0.01f);
	TestTrue(TEXT("Facing him"), Plan.Facing.Equals(FVector(0.f, 1.f, 0.f), 0.001f));

	// Exactly at the slide cap is in; a centimetre past it whiffs.
	Plan = UHawkeyeHitContactRules::PlanContact(Origin, Forward, FVector(230.f, 0.f, 90.f), 80.f, 150.f, 75.f);
	TestTrue(TEXT("At the cap: in reach"), Plan.bReachable);
	Plan = UHawkeyeHitContactRules::PlanContact(Origin, Forward, FVector(231.f, 0.f, 90.f), 80.f, 150.f, 75.f);
	TestFalse(TEXT("Past the slide cap: a whiff"), Plan.bReachable);
	TestEqual(TEXT("Named"), Plan.WhiffReason, FName(TEXT("too far")));

	// Round the turn cap: a whiff, whatever the distance.
	Plan = UHawkeyeHitContactRules::PlanContact(Origin, Forward, FVector(-100.f, 0.f, 90.f), 80.f, 150.f, 75.f);
	TestFalse(TEXT("Behind him, 180 degrees round a 75 cap: a whiff"), Plan.bReachable);
	TestEqual(TEXT("Named"), Plan.WhiffReason, FName(TEXT("too far round")));
	Plan = UHawkeyeHitContactRules::PlanContact(Origin, Forward, FVector(-100.f, 0.f, 90.f), 80.f, 150.f, 180.f);
	TestTrue(TEXT("Kate's 180 cap turns her any way"), Plan.bReachable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitContactRegion, "Hawkeye.HitContact.RegionSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHitContactRegion::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Standing: the attack's region"),
		UHawkeyeHitContactRules::SelectContactRegion(EHawkeyeContactRegion::Head, false), EHawkeyeContactRegion::Head);
	TestEqual(TEXT("Down: the torso, whatever the attack says"),
		UHawkeyeHitContactRules::SelectContactRegion(EHawkeyeContactRegion::Head, true), EHawkeyeContactRegion::Torso);

	// The UE4 mannequin (thugs): spine_01..03, head, thighs. The UEFN mannequin (Kate) adds spine_04, 05.
	const TSet<FName> Mannequin = { TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("neck_01"),
		TEXT("head"), TEXT("thigh_l"), TEXT("thigh_r") };
	auto Has = [&Mannequin](FName Bone) { return Mannequin.Contains(Bone); };
	TestEqual(TEXT("Head: the head"), UHawkeyeHitContactRules::SelectRegionBone(EHawkeyeContactRegion::Head, Has), FName(TEXT("head")));
	TestEqual(TEXT("Torso: the chest"), UHawkeyeHitContactRules::SelectRegionBone(EHawkeyeContactRegion::Torso, Has),
		FName(TEXT("spine_03")));
	TestEqual(TEXT("Legs: the right thigh"), UHawkeyeHitContactRules::SelectRegionBone(EHawkeyeContactRegion::Legs, Has),
		FName(TEXT("thigh_r")));

	const TSet<FName> Sparse = { TEXT("pelvis"), TEXT("spine_02") };
	auto HasSparse = [&Sparse](FName Bone) { return Sparse.Contains(Bone); };
	TestEqual(TEXT("A skeleton without spine_03 falls back down the spine"),
		UHawkeyeHitContactRules::SelectRegionBone(EHawkeyeContactRegion::Torso, HasSparse), FName(TEXT("spine_02")));
	TestEqual(TEXT("No head bone: none (the capsule centre is used)"),
		UHawkeyeHitContactRules::SelectRegionBone(EHawkeyeContactRegion::Head, HasSparse), NAME_None);

	// A test-world thug has no mesh: his capsule centre is the contact point.
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = HawkeyeHitContactTest::SpawnThug(TestWorld, FVector(150.f, 0.f, 0.f));
	AHawkeyeCharacter* Kate = HawkeyeHitContactTest::SpawnKate(TestWorld);
	if (!Thug || !Kate)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	TestTrue(TEXT("No bones: the capsule centre"),
		Kate->GetMeleeComponent()->ComputeContactPoint(*Thug, FHawkeyeStrikeContact()).Equals(Thug->GetActorLocation(), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitContactReaction, "Hawkeye.HitContact.LimbDirectionAndReaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHitContactReaction::RunTest(const FString& Parameters)
{
	// The limb nearest his body is the one that struck.
	const TArray<FVector> Limbs = { FVector(30.f, 20.f, 140.f), FVector(60.f, -20.f, 145.f), FVector(10.f, 15.f, 20.f) };
	TestEqual(TEXT("The jab's left fist, furthest out"), UHawkeyeHitContactRules::PickStrikeLimb(Limbs, FVector(80.f, 0.f, 150.f)), 1);
	TestEqual(TEXT("No limbs: none"), UHawkeyeHitContactRules::PickStrikeLimb({}, FVector::ZeroVector), INDEX_NONE);

	// A hook comes from the side: the fist's motion, not the line between them, says which way the blow travels.
	const FVector Line(100.f, 0.f, 0.f);
	TestTrue(TEXT("A fast fist sweeping across (+Y) travels +Y"),
		UHawkeyeHitContactRules::ComputeStrikeDirection(FVector(0.f, 800.f, 0.f), Line, 150.f).Equals(FVector(0.f, 1.f, 0.f), 0.001f));
	TestTrue(TEXT("A slow fist: the line from striker to target"),
		UHawkeyeHitContactRules::ComputeStrikeDirection(FVector(0.f, 100.f, 0.f), Line, 150.f).Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	TestTrue(TEXT("A fist coming back from him: the line"),
		UHawkeyeHitContactRules::ComputeStrikeDirection(FVector(-900.f, 0.f, 0.f), Line, 150.f).Equals(FVector(1.f, 0.f, 0.f), 0.001f));

	// The reaction is the side the blow came from, for a body facing +X.
	const FVector Facing = FVector::ForwardVector;
	TestEqual(TEXT("Travelling at his face (-X): from the front"),
		UHawkeyeHitContactRules::ClassifyReaction(Facing, FVector(-1.f, 0.f, 0.f)), EHawkeyeHitDirection::Front);
	TestEqual(TEXT("Travelling with him (+X): from behind"),
		UHawkeyeHitContactRules::ClassifyReaction(Facing, FVector(1.f, 0.f, 0.f)), EHawkeyeHitDirection::Back);
	TestEqual(TEXT("Travelling to his right (+Y): from his left"),
		UHawkeyeHitContactRules::ClassifyReaction(Facing, FVector(0.f, 1.f, 0.f)), EHawkeyeHitDirection::Left);
	TestEqual(TEXT("Travelling to his left (-Y): from his right"),
		UHawkeyeHitContactRules::ClassifyReaction(Facing, FVector(0.f, -1.f, 0.f)), EHawkeyeHitDirection::Right);
	TestEqual(TEXT("The hit role follows it"), UCombatAnimSet::HitRoleFor(EHawkeyeHitDirection::Left),
		UCombatAnimSet::HitRoleFor(UHawkeyeHitContactRules::ClassifyReaction(Facing, FVector(0.f, 1.f, 0.f))));

	// The reaction's impulse and blend scale with weight, and less of it while guarding.
	const FHawkeyeHitReactionTuning Tuning;
	const float Light = static_cast<float>(UHawkeyeHitContactRules::ComputeReactionImpulse(FVector::ForwardVector, 0.3f, false, Tuning).Size());
	const float Heavy = static_cast<float>(UHawkeyeHitContactRules::ComputeReactionImpulse(FVector::ForwardVector, 1.f, false, Tuning).Size());
	const float Guarded = static_cast<float>(UHawkeyeHitContactRules::ComputeReactionImpulse(FVector::ForwardVector, 1.f, true, Tuning).Size());
	TestEqual(TEXT("Weight 1: the full impulse"), Heavy, Tuning.ImpulseSpeed, 0.01f);
	TestTrue(TEXT("A light pushes less"), Light < Heavy && Light >= Tuning.ImpulseSpeed * Tuning.MinShare);
	TestEqual(TEXT("A guard takes it at GuardScale"), Guarded, Heavy * Tuning.GuardScale, 0.01f);
	TestEqual(TEXT("Blend: none at the hit"), UHawkeyeHitContactRules::ComputeReactionBlend(0.f, 1.f, false, Tuning), 0.f);
	TestEqual(TEXT("Blend: the peak once risen"), UHawkeyeHitContactRules::ComputeReactionBlend(Tuning.RiseSeconds, 1.f, false, Tuning),
		Tuning.PeakBlend, 0.001f);
	TestEqual(TEXT("Blend: all animation again by RecoverSeconds"),
		UHawkeyeHitContactRules::ComputeReactionBlend(Tuning.RecoverSeconds, 1.f, false, Tuning), 0.f);
	TestTrue(TEXT("Blend: falling in between"),
		UHawkeyeHitContactRules::ComputeReactionBlend(Tuning.RecoverSeconds * 0.6f, 1.f, false, Tuning) < Tuning.PeakBlend);
	TestTrue(TEXT("Blend: less while guarding"),
		UHawkeyeHitContactRules::ComputeReactionBlend(Tuning.RiseSeconds, 1.f, true, Tuning) < Tuning.PeakBlend * 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitContactHitStop, "Hawkeye.HitContact.HitStopScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHitContactHitStop::RunTest(const FString& Parameters)
{
	const FHawkeyeHitStopTuning Tuning;
	TestEqual(TEXT("Weight 0: the minimum"), UHawkeyeHitContactRules::ComputeHitStopSeconds(0.f, Tuning), Tuning.MinSeconds, 0.0001f);
	TestEqual(TEXT("Weight 1: the maximum"), UHawkeyeHitContactRules::ComputeHitStopSeconds(1.f, Tuning), Tuning.MaxSeconds, 0.0001f);
	TestEqual(TEXT("Clamped above 1"), UHawkeyeHitContactRules::ComputeHitStopSeconds(3.f, Tuning), Tuning.MaxSeconds, 0.0001f);
	TestTrue(TEXT("Heavier, longer"), UHawkeyeHitContactRules::ComputeHitStopSeconds(0.3f, Tuning)
		< UHawkeyeHitContactRules::ComputeHitStopSeconds(0.6f, Tuning));
	TestTrue(TEXT("A few frames: two to five at 60"), Tuning.MinSeconds >= 0.015f && Tuning.MaxSeconds <= 0.09f);

	TestTrue(TEXT("Alive and not winding up: reacts after the stop"), UHawkeyeHitContactRules::ShouldHitStop(true, false));
	TestFalse(TEXT("Winding up a swing: keeps his swing"), UHawkeyeHitContactRules::ShouldHitStop(true, true));
	TestFalse(TEXT("Dead: the ragdoll, no reaction"), UHawkeyeHitContactRules::ShouldHitStop(false, false));

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = HawkeyeHitContactTest::SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	// Her light (weight 0.3) about two frames, her heavy (1.0) about four, as before the per-body stop.
	TestEqual(TEXT("Her light"), Kate->GetMeleeHitStopSeconds(false), 0.035f, 0.001f);
	TestEqual(TEXT("Her heavy"), Kate->GetMeleeHitStopSeconds(true), 0.07f, 0.001f);

	// The stop is the world's, as the parry's, so every clock holds together; the body only remembers it.
	Kate->ApplyHitStop(0.05f, FHawkeyeHitStopTuning().Dilation);
	TestEqual(TEXT("The world's time slows"), Kate->GetWorld()->GetWorldSettings()->TimeDilation, FHawkeyeHitStopTuning().Dilation,
		0.0001f);
	TestEqual(TEXT("Her own does not (no clock of hers falls behind the others)"), Kate->CustomTimeDilation, 1.f);
	UHitReactionComponent* Reaction = Kate->GetHitReactionComponent();
	TestNotNull(TEXT("She has a reaction component"), Reaction);
	if (Reaction)
	{
		Reaction->BeginHitStop(0.05f);
		TestTrue(TEXT("Her reaction waits for it"), Reaction->IsHitStopped());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitContactKnockout, "Hawkeye.HitContact.KnockoutImpulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHitContactKnockout::RunTest(const FString& Parameters)
{
	const FHawkeyeKnockoutTuning Tuning;
	FVector Bone;
	FVector Body;
	UHawkeyeHitContactRules::ComputeKnockoutImpulse(FVector(1.f, 0.f, 0.f), 1.f, EHawkeyeImpactSource::Strike, Tuning, Bone, Body);
	TestEqual(TEXT("A heavy strike: the struck bone at BoneSpeed"), static_cast<float>(Bone.Size()), Tuning.BoneSpeed, 0.01f);
	TestTrue(TEXT("Along the blow"), Bone.GetSafeNormal().Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	TestEqual(TEXT("The body along the blow"), static_cast<float>(Body.X), Tuning.BodySpeed, 0.01f);
	TestEqual(TEXT("And lifted a little"), static_cast<float>(Body.Z), Tuning.Lift, 0.01f);
	TestTrue(TEXT("Never past the body cap"), Body.Size() <= Tuning.MaxBodySpeed + 0.01f);

	FVector LightBone;
	FVector LightBody;
	UHawkeyeHitContactRules::ComputeKnockoutImpulse(FVector(1.f, 0.f, 0.f), 0.f, EHawkeyeImpactSource::Strike, Tuning, LightBone,
		LightBody);
	TestEqual(TEXT("Weight 0: MinShare of it"), static_cast<float>(LightBone.Size()), Tuning.BoneSpeed * Tuning.MinShare, 0.01f);

	// An arrow from a roof, 30 degrees down: along its flight (never into the floor), no lift, and less.
	const FVector Down = FVector(0.866f, 0.f, -0.5f);
	FVector ArrowBone;
	FVector ArrowBody;
	UHawkeyeHitContactRules::ComputeKnockoutImpulse(Down, 1.f, EHawkeyeImpactSource::Arrow, Tuning, ArrowBone, ArrowBody);
	TestTrue(TEXT("An arrow throws less than a strike"), ArrowBone.Size() < Bone.Size() && ArrowBody.Size() < Body.Size());
	TestEqual(TEXT("At ArrowScale"), static_cast<float>(ArrowBone.Size()), Tuning.BoneSpeed * Tuning.ArrowScale, 0.5f);
	TestTrue(TEXT("Not driven into the floor"), ArrowBone.Z >= 0.f && ArrowBody.Z == 0.f);
	TestTrue(TEXT("Along the arrow's way"), ArrowBone.X > 0.f && ArrowBody.X > 0.f);

	// The caps hold whatever the tuning asks.
	FHawkeyeKnockoutTuning Wild = Tuning;
	Wild.BoneSpeed = 5000.f;
	Wild.BodySpeed = 5000.f;
	Wild.Lift = 3000.f;
	UHawkeyeHitContactRules::ComputeKnockoutImpulse(FVector(0.f, 1.f, 0.f), 1.f, EHawkeyeImpactSource::Finisher, Wild, Bone, Body);
	TestEqual(TEXT("Bone speed capped"), static_cast<float>(Bone.Size()), Wild.MaxBoneSpeed, 0.5f);
	TestEqual(TEXT("Body speed capped: a body falls, it does not fly"), static_cast<float>(Body.Size()), Wild.MaxBodySpeed, 0.5f);

	TestEqual(TEXT("Camera kick: light at 0"), UHawkeyeHitContactRules::ComputeCameraKick(0.f, 0.8f, 2.5f), 0.8f, 0.001f);
	TestEqual(TEXT("Camera kick: heavy at 1"), UHawkeyeHitContactRules::ComputeCameraKick(1.f, 0.8f, 2.5f), 2.5f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitContactUnchanged, "Hawkeye.HitContact.ParryGuardTelegraphUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHitContactUnchanged::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHitContactTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(130.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* His = Thug->GetMeleeComponent();

	// His swing, aimed at her: the telegraph keeps its length, the parry still answers it, and the plan is in reach.
	FHawkeyeMeleeAttack Swing = Thug->GetMeleeAttack(0);
	His->SetNextAttackTarget(Kate);
	TestTrue(TEXT("His swing starts"), His->StartAttack(Swing));
	TestEqual(TEXT("The telegraph is the attack's wind-up"), His->GetPhaseRemaining(), Swing.WindupSeconds, 0.0001f);
	TestEqual(TEXT("A parry still answers it"), UHawkeyeMeleeRules::ClassifyParry(Thug), EHawkeyeParryKind::Swing);
	His->AdvanceAttack(Swing.WindupSeconds - Swing.Contact.ApproachSeconds + 0.01f);
	TestTrue(TEXT("In the slide's stretch: planned"), His->GetContactPlan().bReachable);
	TestEqual(TEXT("A 130 cm stand-off slides 58 to his fists' 72"), His->GetContactPlan().Slide, 130.f - 72.f, 0.5f);
	TestFalse(TEXT("Not a whiff"), His->IsContactWhiff());
	TestTrue(TEXT("Still winding up on the same clock"), His->IsWindingUp());
	const int32 Parries = Kate->GetParryCount();
	TestTrue(TEXT("Parried"), Kate->TryParry());
	TestEqual(TEXT("Counted"), Kate->GetParryCount(), Parries + 1);
	TestTrue(TEXT("Parry-staggered, his finisher open"), Thug->IsParryStaggered() && Thug->IsFinisherOpen());
	TestFalse(TEXT("His swing is gone"), His->IsAttacking());

	// She steps back past his slide cap while he winds up: no slide, a whiff.
	Thug->SetActorLocation(FVector(330.f, 0.f, 0.f));
	Thug->StaggerFor(nullptr, 0.f);
	His->CancelAttack();
	His->SetNextAttackTarget(Kate);
	TestTrue(TEXT("Another swing"), His->StartAttack(Swing));
	His->AdvanceAttack(Swing.WindupSeconds - Swing.Contact.ApproachSeconds + 0.01f);
	TestTrue(TEXT("330 cm off, 252 past a 110 cap: a whiff"), His->IsContactWhiff());
	TestEqual(TEXT("Named"), His->GetContactPlan().WhiffReason, FName(TEXT("too far")));
	His->CancelAttack();

	// A boss swinging through her light: the light lands at his guard's share and he is not held (his clock runs on), she is.
	Thug->SetActorLocation(FVector(100.f, 0.f, 0.f));
	UHealthComponent* Health = Thug->GetHealthComponent();
	Health->StaggerThreshold = 40.f;
	Health->IncomingDamageScale = 0.3f;
	FHawkeyeMeleeAttack Through = Swing;
	Through.WindupSeconds = 1.f;
	Through.bUnblockable = true;
	His->SetNextAttackTarget(Kate);
	TestTrue(TEXT("His unblockable swing starts"), His->StartAttack(Through));
	const float Before = Health->GetCurrentHealth();
	const float TelegraphLeft = His->GetPhaseRemaining();
	TestTrue(TEXT("Her light starts"), Kate->StartLightAttack());
	Kate->GetMeleeComponent()->AdvanceAttack(Kate->GetMeleeComponent()->GetCurrentAttack().WindupSeconds + 0.001f);
	TestEqual(TEXT("It lands at his guard's share"), Before - Health->GetCurrentHealth(), 15.f * 0.3f, 0.01f);
	TestTrue(TEXT("Still winding up: a guarded boss is not staggered by a light"), His->IsWindingUp());
	TestEqual(TEXT("His telegraph's clock did not move"), His->GetPhaseRemaining(), TelegraphLeft, 0.0001f);
	TestEqual(TEXT("Nobody's own clock is touched"), Thug->CustomTimeDilation + Kate->CustomTimeDilation, 2.f);
	TestEqual(TEXT("The world holds, as for the parry"), Kate->GetWorld()->GetWorldSettings()->TimeDilation,
		FHawkeyeHitStopTuning().Dilation, 0.0001f);
	TestTrue(TEXT("The impact is known: its weight is her light's"),
		Kate->GetMeleeComponent()->GetLastImpact().bValid
			&& FMath::IsNearlyEqual(Kate->GetMeleeComponent()->GetLastImpact().Weight, 0.3f));
	TestFalse(TEXT("The pending impact is cleared once resolved"), Thug->GetHitReactionComponent()->GetPendingImpact().bValid);
	return true;
}

#endif
