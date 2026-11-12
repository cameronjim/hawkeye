#include "Combat/HealthComponent.h"
#include "Combat/HitContactRules.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Melee feel (claude-docs/gameplay-semantics.md, "Melee feel"): who a strike goes at (the stick, then the camera, then
 * the nearest), the pull's speed cap, and never through a wall or over a gap or a ledge, for Kate's pick and warp and
 * for a thug's slide; the hit-stop that never stacks and the knockout's grounded damping.
 */
namespace HawkeyeMeleeMagnetismTest
{
	static constexpr float KateHalfHeight = 88.f;
	static constexpr float ThugHalfHeight = 96.f;

	static AHawkeyeTestBlocker* SpawnBox(const FHawkeyeTestWorld& TestWorld, const FVector& Min, const FVector& Max)
	{
		AHawkeyeTestBlocker* Box = Cast<AHawkeyeTestBlocker>(
			TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), (Min + Max) * 0.5f, FRotator::ZeroRotator));
		if (Box)
		{
			Box->SetExtent((Max - Min) * 0.5f);
		}
		return Box;
	}

	/** Kate with her feet at FeetAt, walking, facing +X. */
	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& FeetAt)
	{
		AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(),
			FeetAt + FVector(0.f, 0.f, KateHalfHeight), FRotator::ZeroRotator));
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	/** A fists thug with his feet at FeetAt, facing -X (toward Kate at the origin). */
	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& FeetAt)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(
			AThugCharacter::StaticClass(), FeetAt + FVector(0.f, 0.f, ThugHalfHeight), FRotator(0.f, 180.f, 0.f)));
		if (Thug)
		{
			Thug->Weapon = EThugWeapon::Fists;
			Thug->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Thug;
	}

	/** Runs Thug's fists at Kate into his slide's stretch of the telegraph (the slide is planned there). */
	static void SwingIntoSlide(AThugCharacter* Thug, AHawkeyeCharacter* Kate)
	{
		UMeleeComponent* His = Thug->GetMeleeComponent();
		const FHawkeyeMeleeAttack Swing = Thug->GetMeleeAttack(0);
		His->CancelAttack();
		His->SetNextAttackTarget(Kate);
		His->StartAttack(Swing);
		His->AdvanceAttack(Swing.WindupSeconds - Swing.Contact.ApproachSeconds + 0.01f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeStrikeTargetChoice, "Hawkeye.Melee.StrikeTargetChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeStrikeTargetChoice::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	const FVector Camera = FVector::ForwardVector;
	const FVector Left = -FVector::RightVector;
	TArray<FVector> Thugs = {
		FVector(200.f, 0.f, 0.f),  // ahead, down the camera
		FVector(0.f, -200.f, 0.f), // on her left
		FVector(-120.f, 0.f, 0.f), // just behind her
	};
	const TArray<bool> AllClear = {true, true, true};
	auto Pick = [&](bool bStick, const FVector& Stick, const TArray<bool>& Clear)
	{
		return UHawkeyeMeleeRules::SelectStrikeTarget(
			Origin, bStick, Stick, Camera, Thugs, Clear, 350.f, 60.f, 150.f, 200.f);
	};
	TestEqual(TEXT("The stick first: the one on her left"), Pick(true, Left, AllClear), 1);
	TestEqual(TEXT("No stick: the camera's, the one ahead"), Pick(false, FVector::ZeroVector, AllClear), 0);
	TestEqual(TEXT("The stick pushed at nobody (right): the camera's"), Pick(true, FVector::RightVector, AllClear), 0);
	TestEqual(TEXT("A wall in front of the one ahead: the camera has nobody, so the nearest, behind her"),
		Pick(false, FVector::ZeroVector, {false, true, true}), 2);
	Thugs[2] = FVector(-200.f, 0.f, 0.f);
	TestEqual(TEXT("Nobody in either cone and nobody inside 150: nobody"),
		Pick(false, FVector::ZeroVector, {false, true, true}), INDEX_NONE);
	TestEqual(TEXT("A missing clear flag counts as clear"),
		UHawkeyeMeleeRules::SelectStrikeTarget(
			Origin, false, FVector::ZeroVector, Camera, Thugs, {}, 350.f, 60.f, 150.f, 200.f),
		0);

	// The floor along the pull.
	const TOptional<float> Ground(0.f);
	TestTrue(TEXT("Level all the way"), UHawkeyeMeleeRules::IsSlideFloorWalkable(Ground, {0.f, 2.f, -3.f}, 45.f, 60.f));
	TestFalse(TEXT("A gap (no floor within the drop)"),
		UHawkeyeMeleeRules::IsSlideFloorWalkable(Ground, {0.f, TOptional<float>(), 0.f}, 45.f, 60.f));
	TestFalse(TEXT("A ledge (the floor 150 cm down)"),
		UHawkeyeMeleeRules::IsSlideFloorWalkable(Ground, {0.f, -150.f}, 45.f, 60.f));
	TestFalse(
		TEXT("A step up she would climb"), UHawkeyeMeleeRules::IsSlideFloorWalkable(Ground, {0.f, 80.f}, 45.f, 60.f));
	TestTrue(TEXT("A kerb is walked"), UHawkeyeMeleeRules::IsSlideFloorWalkable(Ground, {0.f, 15.f}, 45.f, 60.f));
	TestTrue(TEXT("No floor under her (in the air): nothing to keep her on"),
		UHawkeyeMeleeRules::IsSlideFloorWalkable(TOptional<float>(), {TOptional<float>()}, 45.f, 60.f));

	// The speed cap.
	TestEqual(TEXT("The slide cap is the closing speed over the time to the hit"),
		UHawkeyeMeleeRules::ComputeSlideCap(290.f, 1100.f, 0.2f), 220.f, 0.01f);
	TestEqual(
		TEXT("Never past the attack's own cap"), UHawkeyeMeleeRules::ComputeSlideCap(110.f, 1100.f, 1.f), 110.f, 0.01f);
	TestEqual(TEXT("Speed 0: no speed cap"), UHawkeyeMeleeRules::ComputeSlideCap(290.f, 0.f, 0.1f), 290.f, 0.01f);
	TestEqual(TEXT("Kate's pull: 1100 cm/s"), FHawkeyeStrikeMagnetism().MaxClosingSpeed, 1100.f);
	TestEqual(TEXT("A thug's: half that"), GetDefault<AThugCharacter>()->GetMeleeComponent()->Magnetism.MaxClosingSpeed,
		550.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeNoPullThroughWalls, "Hawkeye.Melee.NoMagnetismThroughWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeNoPullThroughWalls::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeMagnetismTest;
	FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-2000.f, -2000.f, -100.f), FVector(2000.f, 2000.f, 0.f));
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(220.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	TestTrue(TEXT("In the open she picks him"), Kate->FindMeleeAssistTarget() == Thug);

	// A 30 cm wall between them, taller than both.
	AHawkeyeTestBlocker* Wall = SpawnBox(TestWorld, FVector(95.f, -200.f, 0.f), FVector(125.f, 200.f, 300.f));
	TestNull(TEXT("A wall between: nobody"), Kate->FindMeleeAssistTarget());
	TestFalse(TEXT("No sight through it"), Kate->GetMeleeComponent()->HasStrikeSight(*Thug));
	TestTrue(TEXT("Her light still goes (at the air)"), Kate->StartLightAttack());
	TestNull(TEXT("At nobody"), Kate->GetMeleeComponent()->GetSwingTarget());
	TestEqual(TEXT("No pull: her own step only"), Kate->GetMeleeComponent()->GetLastLungeDistance(),
		Kate->GetMeleeComponent()->GetCurrentAttack().LungeDistance, 0.5f);
	Kate->GetMeleeComponent()->AdvanceAttack(1.f);
	TestEqual(TEXT("And nothing lands through the wall"), Thug->GetHealthComponent()->GetCurrentHealth(),
		Thug->GetHealthComponent()->GetMaxHealth());

	// His slide is stopped by it too: 160 cm off with the wall between them, he swings at the brick.
	Thug->SetActorLocation(FVector(160.f, 0.f, ThugHalfHeight));
	Wall->SetActorLocation(FVector(80.f, 0.f, 150.f));
	Wall->SetExtent(FVector(10.f, 200.f, 150.f));
	SwingIntoSlide(Thug, Kate);
	TestTrue(TEXT("His slide is refused: a whiff"), Thug->GetMeleeComponent()->IsContactWhiff());
	TestEqual(TEXT("Named"), Thug->GetMeleeComponent()->GetContactPlan().WhiffReason, FName(TEXT("a wall between")));
	Wall->Destroy();
	SwingIntoSlide(Thug, Kate);
	TestFalse(TEXT("Without it, his slide goes"), Thug->GetMeleeComponent()->IsContactWhiff());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeNoPullOverLedges, "Hawkeye.Melee.NoMagnetismOverLedges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeNoPullOverLedges::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeMagnetismTest;
	FHawkeyeTestWorld TestWorld;
	// The street (top 0) and a 150 cm block she stands on, its edge at x = 100.
	SpawnBox(TestWorld, FVector(-2000.f, -2000.f, -100.f), FVector(2000.f, 2000.f, 0.f));
	SpawnBox(TestWorld, FVector(-400.f, -300.f, 0.f), FVector(100.f, 300.f, 150.f));
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 150.f));
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(240.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	TestNull(TEXT("Him on the street below the ledge: nobody (no pull off the edge)"), Kate->FindMeleeAssistTarget());
	FName Why;
	TestFalse(TEXT("The path drops"),
		Kate->GetMeleeComponent()->IsStrikePathClear(*Thug, FVector(174.f, 0.f, 150.f + KateHalfHeight), &Why));
	TestEqual(TEXT("Named"), Why, FName(TEXT("no floor on the way")));

	// Across a gap between two roofs (an 80 cm gap down to the street): nobody.
	SpawnBox(TestWorld, FVector(180.f, -300.f, 0.f), FVector(600.f, 300.f, 150.f));
	Thug->SetActorLocation(FVector(260.f, 0.f, 150.f + ThugHalfHeight));
	TestNull(TEXT("Him across a gap: nobody"), Kate->FindMeleeAssistTarget());
	// And he does not slide over it at her.
	Thug->SetActorLocation(FVector(200.f, 0.f, 150.f + ThugHalfHeight));
	Kate->SetActorLocation(FVector(70.f, 0.f, 150.f + KateHalfHeight));
	SwingIntoSlide(Thug, Kate);
	TestTrue(TEXT("His slide over the gap is refused"), Thug->GetMeleeComponent()->IsContactWhiff());

	// Close the gap: the same two now on one roof, she picks him and he slides.
	SpawnBox(TestWorld, FVector(95.f, -300.f, 0.f), FVector(185.f, 300.f, 150.f));
	Kate->SetActorLocation(FVector(0.f, 0.f, 150.f + KateHalfHeight));
	Thug->SetActorLocation(FVector(260.f, 0.f, 150.f + ThugHalfHeight));
	TestTrue(TEXT("One roof: she picks him"), Kate->FindMeleeAssistTarget() == Thug);
	Kate->SetActorLocation(FVector(70.f, 0.f, 150.f + KateHalfHeight));
	Thug->SetActorLocation(FVector(200.f, 0.f, 150.f + ThugHalfHeight));
	SwingIntoSlide(Thug, Kate);
	TestFalse(TEXT("And his slide goes"), Thug->GetMeleeComponent()->IsContactWhiff());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeKnockoutGroundedDamping, "Hawkeye.HitContact.KnockoutGroundedDamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeKnockoutGroundedDamping::RunTest(const FString& Parameters)
{
	const FHawkeyeKnockoutTuning Tuning;
	const FVector2D Falling = UHawkeyeHitContactRules::ComputeKnockoutDamping(-1.f, Tuning);
	TestEqual(TEXT("Falling: the falling damping"), static_cast<float>(Falling.X), Tuning.LinearDamping);
	TestEqual(TEXT("Falling: angular too"), static_cast<float>(Falling.Y), Tuning.AngularDamping);
	const FVector2D Landed = UHawkeyeHitContactRules::ComputeKnockoutDamping(0.f, Tuning);
	TestEqual(TEXT("The instant he is down: still the falling damping"), static_cast<float>(Landed.X),
		Tuning.LinearDamping, 0.001f);
	const FVector2D Halfway =
		UHawkeyeHitContactRules::ComputeKnockoutDamping(Tuning.GroundedBlendSeconds * 0.5f, Tuning);
	TestTrue(TEXT("Rising"), Halfway.X > Tuning.LinearDamping && Halfway.X < Tuning.GroundedLinearDamping);
	const FVector2D Down = UHawkeyeHitContactRules::ComputeKnockoutDamping(Tuning.GroundedBlendSeconds + 1.f, Tuning);
	TestEqual(TEXT("Down: the grounded damping"), static_cast<float>(Down.X), Tuning.GroundedLinearDamping, 0.001f);
	TestEqual(TEXT("Down: angular"), static_cast<float>(Down.Y), Tuning.GroundedAngularDamping, 0.001f);
	TestTrue(TEXT("Grounded is heavier than falling"),
		Tuning.GroundedLinearDamping > Tuning.LinearDamping && Tuning.GroundedAngularDamping > Tuning.AngularDamping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeBufferAfterHeavy, "Hawkeye.Melee.LightBufferedAfterHeavy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeBufferAfterHeavy::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeMagnetismTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	// Pressed in the heavy's wind-up: refused, as before.
	TestTrue(TEXT("The heavy starts"), Kate->StartHeavyAttack());
	Melee->AdvanceAttack(0.1f);
	TestFalse(TEXT("A light in its wind-up is refused"), Kate->StartLightAttack());
	// Pressed in the last 0.3 s of its recovery: kept, and it goes the moment the heavy is over.
	Melee->AdvanceAttack(Melee->GetCurrentAttack().WindupSeconds - 0.1f + 0.05f);
	TestEqual(TEXT("In the recovery"), Melee->GetPhase(), EMeleePhase::Recover);
	TestTrue(TEXT("Inside the buffer"), Melee->GetPhaseRemaining() <= 0.3f);
	TestTrue(TEXT("The press is kept"), Kate->StartLightAttack());
	TestEqual(TEXT("Not yet swung"), Melee->GetCurrentAttack().Name, FName(TEXT("heavy")));
	Melee->AdvanceAttack(1.f);
	Kate->AdvanceMeleeFlow(0.f);
	TestEqual(TEXT("The light goes as the heavy ends"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	TestTrue(TEXT("Winding up"), Melee->IsWindingUp());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRagdollMasses, "Hawkeye.HitContact.RagdollMassesAndFinisherThrow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeRagdollMasses::RunTest(const FString& Parameters)
{
	const UHitReactionComponent* Defaults = GetDefault<UHitReactionComponent>();
	const TArray<FName> Mannequin = {"pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head", "upperarm_l",
		"upperarm_r", "lowerarm_l", "lowerarm_r", "hand_l", "hand_r", "thigh_l", "thigh_r", "calf_l", "calf_r",
		"foot_l", "foot_r", "clavicle_l"};
	float Total = 0.f;
	for (const FName Bone : Mannequin)
	{
		Total +=
			UHitReactionComponent::ComputeBodyMass(Bone, Defaults->RagdollMassKg, Defaults->BodyMassShares, Mannequin);
	}
	TestEqual(TEXT("A thug weighs 78 kg"), Defaults->RagdollMassKg, 78.f);
	TestTrue(
		FString::Printf(TEXT("The bodies add up to about that (%.1f kg)"), Total), FMath::Abs(Total - 78.f) < 1.5f);
	const float Pelvis = UHitReactionComponent::ComputeBodyMass("pelvis", 78.f, Defaults->BodyMassShares, Mannequin);
	const float Hand = UHitReactionComponent::ComputeBodyMass("hand_r", 78.f, Defaults->BodyMassShares, Mannequin);
	TestTrue(TEXT("The trunk outweighs a hand (it was 1 kg each), but not by more than the solver takes"),
		Pelvis > 3.f * Hand && Pelvis < 6.f * Hand);
	TestTrue(TEXT("A body not listed shares what is left"),
		UHitReactionComponent::ComputeBodyMass("clavicle_l", 78.f, Defaults->BodyMassShares, Mannequin) > 0.f);
	TestEqual(TEXT("0 keeps the asset's own"),
		UHitReactionComponent::ComputeBodyMass("pelvis", 0.f, Defaults->BodyMassShares, Mannequin), 0.f);
	TestEqual(TEXT("The heavy is heavier"), GetDefault<AThugCharacter>()->HeavyRagdollMassKg, 100.f);

	// A finisher folds him down and back: less of a kick at the struck bone and less lift than a heavy strike.
	const FHawkeyeKnockoutTuning Tuning;
	FVector StrikeBone, StrikeBody, FinisherBone, FinisherBody;
	UHawkeyeHitContactRules::ComputeKnockoutImpulse(
		FVector::ForwardVector, 1.f, EHawkeyeImpactSource::Strike, Tuning, StrikeBone, StrikeBody);
	UHawkeyeHitContactRules::ComputeKnockoutImpulse(
		FVector::ForwardVector, 1.f, EHawkeyeImpactSource::Finisher, Tuning, FinisherBone, FinisherBody);
	TestTrue(TEXT("Less bone throw"), FinisherBone.Size() < StrikeBone.Size());
	TestTrue(TEXT("Less lift"), FinisherBody.Z < StrikeBody.Z);
	TestTrue(TEXT("Still along the blow"), FinisherBody.X > 0.f && FinisherBone.X > 0.f);
	return true;
}

#endif
