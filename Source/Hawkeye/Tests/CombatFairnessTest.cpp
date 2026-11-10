#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/FightMetrics.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "World/ThugTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Fair, readable fights: the archer's hold and loose window, the heavy's shield (what it blocks, what
 * gets through, what staggers him), the bash's knockback, the alert glyph and health bar timing, and
 * the annoyance and telegraph metric the scripted fights report. Definitions are built in code.
 */
namespace HawkeyeFairnessTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At, float Yaw, EThugWeapon Weapon,
		AThugAIController** OutBrain = nullptr)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
		if (!Thug)
		{
			return nullptr;
		}
		Thug->Weapon = Weapon;
		if (OutBrain)
		{
			AThugAIController* Brain = Cast<AThugAIController>(
				TestWorld.SpawnActor(AThugAIController::StaticClass(), At, FRotator::ZeroRotator));
			if (AController* Existing = Thug->GetController())
			{
				Existing->UnPossess();
			}
			if (Brain)
			{
				Brain->Possess(Thug);
			}
			*OutBrain = Brain;
		}
		return Thug;
	}

	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& At, float Yaw = 0.f)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
	}

	/** DA_Bow_Archer's numbers. */
	static void ArmArcher(AThugCharacter* Thug)
	{
		UBowDefinition* Bow = NewObject<UBowDefinition>(Thug);
		Bow->FullDrawSeconds = 1.2f;
		Bow->MaxSpeed = 5000.f;
		Bow->PerfectBonus = 0.f;
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Thug);
		Arrow->Slot = 1;
		Arrow->Damage = 30.f;
		Thug->GetBowComponent()->OwnBow = Bow;
		Thug->GetBowComponent()->OwnArrow = Arrow;
	}

	/** Flies an arrow of Damage from From along Direction until it sticks (or a second passes). */
	static AArrowProjectile* Shoot(const FHawkeyeTestWorld& TestWorld, AActor* Shooter, const FVector& From,
		const FVector& Direction, float Damage)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Shooter);
		Arrow->Slot = 1;
		Arrow->Damage = Damage;
		UBowDefinition* Bow = NewObject<UBowDefinition>(Shooter);
		Bow->HeadshotMultiplier = 1.f;
		AArrowProjectile* Projectile = Cast<AArrowProjectile>(
			TestWorld.SpawnActor(AArrowProjectile::StaticClass(), From, Direction.Rotation()));
		if (!Projectile)
		{
			return nullptr;
		}
		Projectile->InitArrow(Arrow, Bow, Damage, Shooter, nullptr);
		Projectile->LaunchWithVelocity(Direction.GetSafeNormal() * 5000.f);
		for (float Elapsed = 0.f; !Projectile->IsStuck() && Elapsed < 1.f; Elapsed += 0.01f)
		{
			Projectile->AdvanceFlight(0.01f);
		}
		return Projectile;
	}
}

// --- Archer: hold and loose window ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArcherHoldClockTest, "Hawkeye.Archer.HoldClockTiming", HawkeyeFairnessTest::Flags)

bool FHawkeyeArcherHoldClockTest::RunTest(const FString& Parameters)
{
	// Nobody comes back: he holds for 2.5 s, then relaxes.
	FHawkeyeArcherHoldClock Clock;
	TestEqual(TEXT("Not holding: nothing"), Clock.Advance(0.1f, true, true), EArcherHoldStep::None);
	Clock.BeginHold();
	TestTrue(TEXT("Holding"), Clock.IsHolding());
	TestFalse(TEXT("No new draw while holding"), Clock.CanStartDraw());
	EArcherHoldStep Step = EArcherHoldStep::Hold;
	float Elapsed = 0.f;
	while (Step == EArcherHoldStep::Hold && Elapsed < 5.f)
	{
		Step = Clock.Advance(0.05f, false, true);
		Elapsed += 0.05f;
	}
	TestEqual(TEXT("He relaxes"), Step, EArcherHoldStep::Relax);
	TestEqual(TEXT("After 2.5 s"), Elapsed, 2.5f, 0.051f);
	TestFalse(TEXT("And the hold is over"), Clock.IsHolding());

	// She shows again inside the cone at full draw: he looses within 0.2 s.
	Clock.BeginHold();
	Clock.Advance(1.f, false, true);
	float Reaction = 0.f;
	Step = EArcherHoldStep::Hold;
	while (Step == EArcherHoldStep::Hold && Reaction < 1.f)
	{
		Step = Clock.Advance(0.05f, true, true);
		Reaction += 0.05f;
	}
	TestEqual(TEXT("He fires on her"), Step, EArcherHoldStep::Fire);
	TestTrue(TEXT("Within 0.2 s of her showing"), Reaction <= 0.2f + KINDA_SMALL_NUMBER);

	// Back before the draw is full: he waits for full draw, then fires.
	Clock.BeginHold();
	TestEqual(TEXT("In the cone but only half drawn: still holding"), Clock.Advance(0.2f, true, false), EArcherHoldStep::Hold);
	TestEqual(TEXT("Full draw with her still there: fire"), Clock.Advance(0.05f, true, true), EArcherHoldStep::Fire);

	// A glimpse shorter than the reaction does not count, and resets.
	Clock.BeginHold();
	Clock.Advance(0.05f, true, true);
	TestEqual(TEXT("Gone again: holding"), Clock.Advance(0.05f, false, true), EArcherHoldStep::Hold);
	TestEqual(TEXT("The reaction starts over"), Clock.Advance(0.05f, true, true), EArcherHoldStep::Hold);
	TestEqual(TEXT("Then fires"), Clock.Advance(0.05f, true, true), EArcherHoldStep::Fire);
	TestTrue(TEXT("ReactionSeconds is under 0.2"), Clock.ReactionSeconds < 0.2f);

	// The loose window: 0.6 s with no draw.
	Clock.BeginLoose();
	TestFalse(TEXT("No draw in the loose window"), Clock.CanStartDraw());
	Clock.AdvanceLoose(0.55f);
	TestTrue(TEXT("Still loose at 0.55 s"), Clock.IsLoose());
	Clock.AdvanceLoose(0.06f);
	TestFalse(TEXT("Over at 0.61 s"), Clock.IsLoose());
	TestTrue(TEXT("He may draw again"), Clock.CanStartDraw());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArcherHoldsLostLine, "Hawkeye.Archer.HoldsDrawAfterLosingLine", HawkeyeFairnessTest::Flags)

bool FHawkeyeArcherHoldsLostLine::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFairnessTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(2000.f, 0.f, 0.f));
	AThugAIController* Brain = nullptr;
	AThugCharacter* Archer = SpawnThug(TestWorld, FVector::ZeroVector, 0.f, EThugWeapon::Bow, &Brain);
	AHawkeyeTestBlocker* Parapet = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(1000.f, 0.f, 0.f) + FVector(0.f, 0.f, -5000.f),
			FRotator::ZeroRotator));
	if (!Kate || !Brain || !Archer || !Parapet)
	{
		AddError(TEXT("Could not spawn the archer, Kate and the parapet."));
		return false;
	}
	Parapet->SetExtent(FVector(20.f, 400.f, 400.f));
	ArmArcher(Archer);
	// The test world's clock only runs when the test moves it, so the 2 s between shots is taken out: only the
	// hold and the loose window gate his draws here. The world's clock follows the bow's, since the attack
	// token he takes for each draw comes back with a gap timed on it.
	Brain->ArcherShotCooldownSeconds = 0.f;
	UBowComponent* Bow = Archer->GetBowComponent();
	auto SetClock = [&TestWorld, Bow](double Seconds)
	{
		Bow->SetTestTimeSeconds(Seconds);
		TestWorld.AdvanceClock(FMath::Max(0.f, static_cast<float>(Seconds - TestWorld.Get()->GetTimeSeconds())));
	};
	SetClock(0.0);
	Brain->SetTarget(Kate);
	Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
	ThinkThug(Brain, 0.25f);
	TestTrue(TEXT("With a line he draws"), Bow->IsDrawing());

	// She drops behind the parapet 0.6 s into his draw.
	SetClock(0.6);
	Parapet->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	ThinkThug(Brain, 0.25f);
	TestTrue(TEXT("He holds the draw instead of letting it down"), Brain->IsHoldingDraw() && Bow->IsDrawing());
	TestTrue(TEXT("The glint stays on: the hold still telegraphs"), Archer->IsTelegraphGlintOn());
	SetClock(1.6);
	Brain->TickArcherHold(1.f);
	TestTrue(TEXT("Still holding 1 s later, now at full draw"), Brain->IsHoldingDraw());
	TestEqual(TEXT("Nothing loosed at a wall"), Brain->GetArrowsLoosed(), 0);

	// She stands up where she went down: he looses within 0.2 s.
	Parapet->SetActorLocation(FVector(1000.f, 0.f, -5000.f));
	float Reaction = 0.f;
	while (Brain->GetArrowsLoosed() == 0 && Reaction < 1.f)
	{
		Brain->TickArcherHold(0.05f);
		Reaction += 0.05f;
	}
	TestEqual(TEXT("One arrow at her"), Brain->GetArrowsLoosed(), 1);
	TestTrue(TEXT("Within 0.2 s of her reappearing"), Reaction <= 0.2f + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("The loose window opens"), Brain->IsInLooseWindow());

	// In the loose window he cannot draw, even with a line and no cooldown.
	SetClock(10.0);
	ThinkThug(Brain, 0.25f);
	TestFalse(TEXT("No draw in the loose window"), Bow->IsDrawing());
	Brain->TickArcherHold(0.55f);
	TestTrue(TEXT("Still loose 0.55 s after the shot"), Brain->IsInLooseWindow());
	ThinkThug(Brain, 0.25f);
	TestFalse(TEXT("Still no draw"), Bow->IsDrawing());
	Brain->TickArcherHold(0.06f);
	TestFalse(TEXT("0.61 s after the shot the window is shut"), Brain->IsInLooseWindow());
	ThinkThug(Brain, 0.25f);
	TestTrue(TEXT("And he draws again"), Bow->IsDrawing());

	// Nobody comes back: he relaxes after 2.5 s.
	Parapet->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	SetClock(10.5);
	ThinkThug(Brain, 0.25f);
	if (TestTrue(TEXT("Holding again behind the parapet"), Brain->IsHoldingDraw()))
	{
		SetClock(13.0);
		Brain->TickArcherHold(2.45f);
		TestTrue(TEXT("Held at 2.45 s"), Brain->IsHoldingDraw());
		Brain->TickArcherHold(0.1f);
		TestFalse(TEXT("Relaxed past 2.5 s"), Brain->IsHoldingDraw() || Bow->IsDrawing());
		TestEqual(TEXT("Still only the one arrow"), Brain->GetArrowsLoosed(), 1);
	}

	// A hit still breaks a draw, held or not.
	Parapet->SetActorLocation(FVector(1000.f, 0.f, -5000.f));
	SetClock(20.0);
	ThinkThug(Brain, 0.25f);
	TestTrue(TEXT("A fresh draw"), Bow->IsDrawing());
	Parapet->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	SetClock(20.5);
	ThinkThug(Brain, 0.25f);
	TestTrue(TEXT("A fresh hold"), Brain->IsHoldingDraw());
	Archer->GetHealthComponent()->ApplyDamage(5.f, Kate);
	TestFalse(TEXT("A hit breaks the hold"), Brain->IsHoldingDraw() || Bow->IsDrawing());
	return true;
}

// --- The heavy -------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHeavyShieldArc, "Hawkeye.Heavy.ShieldArcBlocks", HawkeyeFairnessTest::Flags)

bool FHawkeyeHeavyShieldArc::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFairnessTest;
	const FVector Forward(1.f, 0.f, 0.f);
	TestTrue(TEXT("Dead ahead is in the arc"), AThugCharacter::IsInShieldArc(Forward, FVector(1.f, 0.f, 0.f), 120.f));
	TestTrue(TEXT("59 degrees off is in"), AThugCharacter::IsInShieldArc(Forward, FRotator(0.f, 59.f, 0.f).Vector(), 120.f));
	TestTrue(TEXT("60 degrees is the edge, in"), AThugCharacter::IsInShieldArc(Forward, FRotator(0.f, -60.f, 0.f).Vector(), 120.f));
	TestFalse(TEXT("61 degrees is out"), AThugCharacter::IsInShieldArc(Forward, FRotator(0.f, 61.f, 0.f).Vector(), 120.f));
	TestFalse(TEXT("Behind is out"), AThugCharacter::IsInShieldArc(Forward, FVector(-1.f, 0.f, 0.f), 120.f));
	TestTrue(TEXT("Height does not matter: an arrow from a roof ahead"),
		AThugCharacter::IsInShieldArc(Forward, FVector(1.f, 0.f, 3.f), 120.f));

	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Heavy = SpawnThug(TestWorld, FVector::ZeroVector, 0.f, EThugWeapon::Shield);
	AThugCharacter* Bat = SpawnThug(TestWorld, FVector(0.f, 3000.f, 0.f), 0.f, EThugWeapon::Bat);
	if (!Heavy || !Bat)
	{
		AddError(TEXT("Could not spawn the heavy."));
		return false;
	}
	TestTrue(TEXT("The heavy is the Shield weapon"), Heavy->IsHeavy() && Heavy->IsShieldRaised());
	TestTrue(TEXT("From the front: blocked"), Heavy->TryBlock(nullptr, FVector(1.f, 0.2f, 0.f), TEXT("test")));
	TestFalse(TEXT("From behind: not"), Heavy->TryBlock(nullptr, FVector(-1.f, 0.f, 0.f), TEXT("test")));
	TestFalse(TEXT("Another thug has no shield"), Bat->TryBlock(nullptr, FVector(1.f, 0.f, 0.f), TEXT("test")));
	TestEqual(TEXT("One block counted"), Heavy->GetBlockCount(), 1);
	TestEqual(TEXT("Bash on even swings"), Heavy->GetMeleeAttack(0).Name, FName(TEXT("bash")));
	TestEqual(TEXT("The slow bat on odd ones"), Heavy->GetMeleeAttack(1).Name, FName(TEXT("heavy_bat")));
	TestEqual(TEXT("The bash telegraphs 0.8 s"), Heavy->ShieldBashAttack.WindupSeconds, 0.8f);
	TestEqual(TEXT("For 30"), Heavy->ShieldBashAttack.Damage, 30.f);
	TestEqual(TEXT("Knocking back 250 cm"), Heavy->ShieldBashAttack.KnockbackDistance, 250.f);
	TestTrue(TEXT("The bat is slower than a street thug's"), Heavy->GetMeleeAttack(1).WindupSeconds > Bat->GetMeleeAttack().WindupSeconds);

	// The shield is up while he stands, down while staggered.
	Heavy->HitReaction(nullptr);
	TestFalse(TEXT("Staggered: guard open"), Heavy->BlocksHitFrom(FVector(1.f, 0.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHeavyStrikes, "Hawkeye.Heavy.WhatGetsThroughAndStaggers", HawkeyeFairnessTest::Flags)

bool FHawkeyeHeavyStrikes::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFairnessTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector, 0.f);
	AThugCharacter* Heavy = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f), 180.f, EThugWeapon::Shield);
	if (!Kate || !Heavy)
	{
		AddError(TEXT("Could not spawn Kate and the heavy."));
		return false;
	}
	Heavy->GetHealthComponent()->SetMaxHealth(200.f, true);
	UMeleeComponent* Melee = Kate->GetMeleeComponent();

	// A light from the front: the shield takes it.
	TestTrue(TEXT("The light starts"), Kate->StartLightAttack());
	Melee->AdvanceAttack(0.11f);
	TestEqual(TEXT("A light into the shield does nothing"), Heavy->GetHealthComponent()->GetCurrentHealth(), 200.f);
	TestFalse(TEXT("Nor staggers him"), Heavy->IsStaggered());
	TestEqual(TEXT("Counted as blocked"), Melee->GetBlockedCount(), 1);
	Melee->AdvanceAttack(1.f);

	// A heavy from the front: through the guard, and it staggers him rather than knocking him down.
	TestTrue(TEXT("The heavy starts"), Kate->StartHeavyAttack());
	Melee->AdvanceAttack(0.61f);
	TestEqual(TEXT("35 through the shield"), Heavy->GetHealthComponent()->GetCurrentHealth(), 165.f);
	TestTrue(TEXT("Guard broken: staggered"), Heavy->IsStaggered());
	TestFalse(TEXT("Not on the floor"), Heavy->IsKnockedDown());
	Melee->AdvanceAttack(1.f);

	// While staggered a light lands.
	TestTrue(TEXT("A light into the open guard"), Kate->StartLightAttack());
	Melee->AdvanceAttack(0.11f);
	TestEqual(TEXT("15 lands"), Heavy->GetHealthComponent()->GetCurrentHealth(), 150.f);
	Melee->AdvanceAttack(1.f);

	// A second heavy far down the street with his back to her: a light hurts but does not stagger.
	const FVector Street(0.f, 5000.f, 0.f);
	AHawkeyeCharacter* Behind = SpawnKate(TestWorld, Street, 0.f);
	AThugCharacter* Turned = SpawnThug(TestWorld, Street + FVector(100.f, 0.f, 0.f), 0.f, EThugWeapon::Shield);
	if (!Behind || !Turned)
	{
		AddError(TEXT("Could not spawn the second pair."));
		return false;
	}
	Turned->GetHealthComponent()->SetMaxHealth(200.f, true);
	TestTrue(TEXT("A light at his back"), Behind->StartLightAttack());
	Behind->GetMeleeComponent()->AdvanceAttack(0.11f);
	TestEqual(TEXT("15 in the back"), Turned->GetHealthComponent()->GetCurrentHealth(), 185.f);
	TestFalse(TEXT("A light never staggers the heavy"), Turned->IsStaggered());

	// An arrow from the front sticks in the shield; one from behind hurts and does not stagger.
	Turned->UpdateShieldPose();
	AArrowProjectile* Front = Shoot(TestWorld, Behind, Street + FVector(900.f, 0.f, 20.f), FVector(-1.f, 0.f, 0.f), 40.f);
	if (TestNotNull(TEXT("An arrow from the front"), Front))
	{
		TestTrue(TEXT("It stuck"), Front->IsStuck());
		TestEqual(TEXT("No damage through the shield"), Turned->GetHealthComponent()->GetCurrentHealth(), 185.f);
		TestTrue(TEXT("It is stuck in the shield"), Front->GetAttachParentActor() == Turned
			&& Front->GetRootComponent()->GetAttachParent() == Turned->GetShieldComponent());
	}
	AArrowProjectile* Back = Shoot(TestWorld, Behind, Street + FVector(-300.f, 0.f, 20.f), FVector(1.f, 0.f, 0.f), 40.f);
	if (TestNotNull(TEXT("An arrow from behind"), Back))
	{
		TestEqual(TEXT("40 in the back"), Turned->GetHealthComponent()->GetCurrentHealth(), 145.f);
		TestFalse(TEXT("An arrow never staggers the heavy"), Turned->IsStaggered());
	}

	// The bola (a trip) and the blast knock him over like anyone.
	Turned->KnockdownFor(Behind, 2.5f);
	TestTrue(TEXT("The bola trips him"), Turned->IsKnockedDown());
	TestFalse(TEXT("Down, the shield blocks nothing"), Turned->BlocksHitFrom(FVector(1.f, 0.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHeavyBashKnockback, "Hawkeye.Heavy.BashKnocksBack", HawkeyeFairnessTest::Flags)

bool FHawkeyeHeavyBashKnockback::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFairnessTest;
	const FVector Force = UMeleeComponent::ComputeKnockbackForce(FVector::ZeroVector, FVector(100.f, 0.f, 30.f), 250.f, 0.35f);
	TestTrue(TEXT("Away from the swinger, flat"), Force.GetSafeNormal().Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	TestEqual(TEXT("250 cm over 0.35 s"), static_cast<float>(Force.Size()), 250.f / 0.35f, 0.5f);

	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Heavy = SpawnThug(TestWorld, FVector::ZeroVector, 0.f, EThugWeapon::Shield);
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(110.f, 0.f, 0.f), 180.f);
	if (!Heavy || !Kate)
	{
		AddError(TEXT("Could not spawn the heavy and Kate."));
		return false;
	}
	UMeleeComponent* Melee = Heavy->GetMeleeComponent();
	TestTrue(TEXT("The bash starts"), Melee->StartAttack(Heavy->GetMeleeAttack(0)));
	TestTrue(TEXT("It telegraphs (the wind-up the mask glows through)"), Melee->IsWindingUp());
	Melee->AdvanceAttack(0.75f);
	TestEqual(TEXT("Nothing before 0.8 s"), Kate->GetHealthComponent()->GetCurrentHealth(),
		Kate->GetHealthComponent()->GetMaxHealth());
	Melee->AdvanceAttack(0.06f);
	TestEqual(TEXT("30 off Kate"), Kate->GetHealthComponent()->GetCurrentHealth(), Kate->GetHealthComponent()->GetMaxHealth() - 30.f);
	TestTrue(TEXT("And she is staggered"), Kate->IsStaggered());
	const TSharedPtr<FRootMotionSource> Shove = Kate->GetCharacterMovement()->GetRootMotionSource(FName(TEXT("MeleeKnockback")));
	if (TestTrue(TEXT("A knockback shove is on her"), Shove.IsValid()))
	{
		const FRootMotionSource_ConstantForce* Constant = static_cast<const FRootMotionSource_ConstantForce*>(Shove.Get());
		TestEqual(TEXT("250 cm worth"), static_cast<float>(Constant->Force.Size() * Constant->Duration), 250.f, 1.f);
		TestTrue(TEXT("Away from him"), Constant->Force.X > 0.f);
	}

	// A dodge's invulnerability means no hit and no shove.
	AThugCharacter* Again = SpawnThug(TestWorld, FVector(0.f, 5000.f, 0.f), 0.f, EThugWeapon::Shield);
	AHawkeyeCharacter* Dodging = SpawnKate(TestWorld, FVector(110.f, 5000.f, 0.f), 180.f);
	if (!Again || !Dodging)
	{
		AddError(TEXT("Could not spawn the second pair."));
		return false;
	}
	Dodging->GetHealthComponent()->SetInvulnerable(true);
	Again->GetMeleeComponent()->StartAttack(Again->GetMeleeAttack(0));
	Again->GetMeleeComponent()->AdvanceAttack(0.81f);
	TestFalse(TEXT("No shove through invulnerability"),
		Dodging->GetCharacterMovement()->GetRootMotionSource(FName(TEXT("MeleeKnockback"))).IsValid());
	return true;
}

// --- Readability -----------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAlertGlyphTimingTest, "Hawkeye.Thug.AlertGlyphTiming", HawkeyeFairnessTest::Flags)

bool FHawkeyeAlertGlyphTimingTest::RunTest(const FString& Parameters)
{
	FHawkeyeAlertGlyph Glyph;
	Glyph.Trigger(true, false);
	TestEqual(TEXT("Alerted: !"), Glyph.GetGlyph(), TCHAR('!'));
	Glyph.Advance(0.55f);
	TestEqual(TEXT("Still up at 0.55 s"), Glyph.GetGlyph(), TCHAR('!'));
	Glyph.Advance(0.06f);
	TestEqual(TEXT("Gone at 0.61 s"), Glyph.GetGlyph(), TCHAR(0));
	Glyph.Trigger(false, true);
	TestEqual(TEXT("Suspicious: ?"), Glyph.GetGlyph(), TCHAR('?'));
	Glyph.Trigger(false, false);
	TestEqual(TEXT("Calm clears it"), Glyph.GetGlyph(), TCHAR(0));

	using FGlyph = FHawkeyeAlertGlyph;
	TestEqual(TEXT("Unhurt: no bar"), FGlyph::ComputeHealthBarAlpha(1.f, 500.f, 0.f), 0.f);
	TestEqual(TEXT("Hurt and near: bar"), FGlyph::ComputeHealthBarAlpha(0.6f, 500.f, 0.f), 1.f);
	TestEqual(TEXT("Beyond 1500 cm: none"), FGlyph::ComputeHealthBarAlpha(0.6f, 1501.f, 0.f), 0.f);
	TestEqual(TEXT("At 3 s still full"), FGlyph::ComputeHealthBarAlpha(0.6f, 500.f, 3.f), 1.f);
	TestEqual(TEXT("Fading at 3.25 s"), FGlyph::ComputeHealthBarAlpha(0.6f, 500.f, 3.25f), 0.5f, 0.001f);
	TestEqual(TEXT("Gone by 3.5 s"), FGlyph::ComputeHealthBarAlpha(0.6f, 500.f, 3.5f), 0.f);
	TestEqual(TEXT("Dead: none"), FGlyph::ComputeHealthBarAlpha(0.f, 500.f, 0.f), 0.f);

	// On a thug: the state change raises the glyph.
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector,
		FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("A thug"), Thug))
	{
		return false;
	}
	Thug->SetAlertState(EThugAlertState::Suspicious);
	TestEqual(TEXT("? over a suspicious thug"), Thug->GetAlertGlyph(), TCHAR('?'));
	Thug->SetAlertState(EThugAlertState::Alerted);
	TestEqual(TEXT("! when he goes alerted"), Thug->GetAlertGlyph(), TCHAR('!'));
	Thug->AdvanceReadability(0.61f);
	TestEqual(TEXT("For 0.6 s"), Thug->GetAlertGlyph(), TCHAR(0));
	TestEqual(TEXT("No bar before he is hurt"), Thug->GetHealthBarAlpha(300.f), 0.f);
	Thug->GetHealthComponent()->ApplyDamage(10.f, nullptr);
	TestEqual(TEXT("A bar once he is"), Thug->GetHealthBarAlpha(300.f), 1.f);
	TestEqual(TEXT("Not from across the street"), Thug->GetHealthBarAlpha(2000.f), 0.f);
	return true;
}

// --- The fight metric ------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFightMetricTest, "Hawkeye.Fight.AnnoyanceAndTelegraphMetric", HawkeyeFairnessTest::Flags)

bool FHawkeyeFightMetricTest::RunTest(const FString& Parameters)
{
	FHawkeyeFightMetrics Metrics;
	// 10 s of fight at 0.1 s frames, staggered for 1.2 s of it.
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		Metrics.Tick(0.1f, Frame >= 20 && Frame < 32);
	}
	TestEqual(TEXT("10 s fought"), Metrics.GetFightSeconds(), 10.f, 0.001f);
	TestEqual(TEXT("1.2 s staggered"), Metrics.GetDisabledSeconds(), 1.2f, 0.001f);
	TestEqual(TEXT("12% annoyance"), Metrics.GetAnnoyanceFraction(), 0.12f, 0.001f);

	Metrics.NoteTelegraph(TEXT("Bat"), 2.0);
	TestTrue(TEXT("A swing 0.6 s after its telegraph is fair"), Metrics.NoteDamage(TEXT("Bat"), 2.6));
	TestTrue(TEXT("1 s is the edge"), Metrics.NoteDamage(TEXT("Bat"), 3.0));
	TestFalse(TEXT("1.1 s after is not"), Metrics.NoteDamage(TEXT("Bat"), 3.1));
	TestFalse(TEXT("Someone else's telegraph does not count"), Metrics.NoteDamage(TEXT("Gunner"), 2.1));
	Metrics.NoteTelegraph(TEXT("Gunner"), 5.0);
	TestFalse(TEXT("A hit before any telegraph from him"), Metrics.NoteDamage(TEXT("Gunner"), 4.9));
	TestEqual(TEXT("Five hits"), Metrics.GetHits(), 5);
	TestEqual(TEXT("Three untelegraphed"), Metrics.GetUntelegraphedHits(), 3);
	TestEqual(TEXT("Each one noted"), Metrics.GetUntelegraphedNotes().Num(), 3);

	FHawkeyeFightMetrics Empty;
	TestEqual(TEXT("No fight, no annoyance"), Empty.GetAnnoyanceFraction(), 0.f);
	return true;
}

#endif
