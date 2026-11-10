#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "EngineUtils.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "World/ThugTreeBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The enemy types (docs/plans/03-core-systems.md, "Combat, finished"): the gunner's cover, burst,
 * telegraph and retreat; the archer's band, draw telegraph, lead and arrows; the squad alert; the
 * mode priority ST_Thug keeps. Definitions are built in code.
 */
namespace HawkeyeEnemyTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static AThugAIController* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& Location, EThugWeapon Weapon,
		AThugCharacter*& OutThug)
	{
		OutThug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		AThugAIController* Brain = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), Location, FRotator::ZeroRotator));
		if (!OutThug || !Brain)
		{
			return nullptr;
		}
		OutThug->Weapon = Weapon;
		Brain->Possess(OutThug);
		return Brain;
	}

	/** BP_Archer's numbers: DA_Bow_Archer (1.2 s, 5000 cm/s, no perfect bonus, no headshot) and DA_Arrow_Trickshot. */
	static void ArmArcher(AThugCharacter* Thug, UArrowDefinition* Standard = nullptr)
	{
		UBowDefinition* Bow = NewObject<UBowDefinition>(Thug);
		Bow->FullDrawSeconds = 1.2f;
		Bow->MaxSpeed = 5000.f;
		Bow->PerfectBonus = 0.f;
		Bow->HeadshotMultiplier = 1.f;
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Thug);
		Arrow->Slot = 1;
		Arrow->Damage = 30.f;
		Arrow->bRecoverable = true;
		Arrow->RecoverAs = Standard;
		Arrow->PickupToast = FText::FromString(TEXT("Trickshot's arrow"));
		Thug->GetBowComponent()->OwnBow = Bow;
		Thug->GetBowComponent()->OwnArrow = Arrow;
	}

	static void Alert(AThugAIController* Brain, AActor* Target)
	{
		Brain->SetTarget(Target);
		Brain->ReportStimulus(EStimulusKind::Hearing, Target->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
	}

	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), Location, FRotator::ZeroRotator));
	}
}

// --- Gunner ----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGunnerCoverBlocksLine, "Hawkeye.Gunner.CoverBlocksLine", HawkeyeEnemyTest::Flags)

bool FHawkeyeGunnerCoverBlocksLine::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Gunner = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Pistol, Gunner);
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(1000.f, 0.f, 0.f));
	if (!Brain || !Kate)
	{
		AddError(TEXT("Could not spawn the gunner and Kate."));
		return false;
	}
	Brain->SetTarget(Kate);

	FVector Cover;
	TestFalse(TEXT("Open ground: no cover to be had"), Brain->FindCoverPoint(Kate->GetActorLocation(), Cover));

	// A crate between them, off to his left.
	AHawkeyeTestBlocker* Crate = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(300.f, 350.f, 0.f), FRotator::ZeroRotator));
	Crate->SetExtent(FVector(100.f, 150.f, 200.f));
	if (!TestTrue(TEXT("Behind the crate is cover"), Brain->FindCoverPoint(Kate->GetActorLocation(), Cover)))
	{
		return false;
	}
	TestTrue(TEXT("Within 800 cm"), FVector::Dist2D(Cover, Gunner->GetActorLocation()) <= 800.f + 1.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GunnerCoverTest), false, Kate);
	Params.AddIgnoredActor(Gunner);
	TestTrue(TEXT("Her line to it is blocked"), TestWorld.Get()->LineTraceTestByChannel(
		Kate->GetActorLocation() + FVector(0.f, 0.f, 60.f), Cover, ECC_Visibility, Params));
	TestEqual(TEXT("The nearest ring that works: 320 cm"), static_cast<float>(FVector::Dist2D(Cover, Gunner->GetActorLocation())), 320.f, 1.f);

	FVector Other;
	TestTrue(TEXT("Relocating skips the cover he is leaving"), Brain->FindCoverPoint(Kate->GetActorLocation(), Other, &Cover));
	TestTrue(TEXT("A different point"), FVector::Dist2D(Other, Cover) >= 200.f);

	// After a burst he goes there.
	Alert(Brain, Kate);
	Gunner->GetWeaponComponent()->SetTestTimeSeconds(0.0);
	float Clock = 0.f;
	for (int32 Step = 0; Step < 12 && Brain->GetGunnerPhase() == EGunnerPhase::Open; ++Step)
	{
		Clock += 0.25f;
		Gunner->GetWeaponComponent()->SetTestTimeSeconds(Clock);
		ThinkThug(Brain, 0.25f);
	}
	TestEqual(TEXT("Three shots, then cover"), Gunner->GetWeaponComponent()->CurrentAmmo, 9);
	TestEqual(TEXT("He is covering"), Brain->GetGunnerPhase(), EGunnerPhase::Covering);
	TestEqual(TEXT("The mode is Cover"), Brain->GetMode(), EThugMode::Cover);

	// The aim rule.
	TestTrue(TEXT("Dead on is aimed at"),
		AThugAIController::IsAimedAtPoint(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), FVector(1000.f, 50.f, 0.f), 6.f));
	TestFalse(TEXT("Ten degrees off is not"),
		AThugAIController::IsAimedAtPoint(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), FVector(1000.f, 180.f, 0.f), 6.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGunnerBurstClock, "Hawkeye.Gunner.BurstClock", HawkeyeEnemyTest::Flags)

bool FHawkeyeGunnerBurstClock::RunTest(const FString& Parameters)
{
	FHawkeyeBurstClock Burst;
	TestEqual(TEXT("Nothing before it starts"), Burst.Advance(5.f), 0);
	Burst.Start();
	TestTrue(TEXT("Telegraphing"), Burst.IsTelegraphing());
	TestEqual(TEXT("No shot at 0.79 s"), Burst.Advance(0.79f), 0);
	TestEqual(TEXT("The first at 0.8 s"), Burst.Advance(0.02f), 1);
	TestTrue(TEXT("Firing"), Burst.IsFiring());
	TestEqual(TEXT("The second 0.25 s later"), Burst.Advance(0.25f), 1);
	TestEqual(TEXT("The third"), Burst.Advance(0.25f), 1);
	TestFalse(TEXT("And it is over"), Burst.IsActive());
	TestEqual(TEXT("One burst done"), Burst.GetBurstsCompleted(), 1);
	Burst.Start();
	TestEqual(TEXT("A long step still fires only three"), Burst.Advance(10.f), 3);
	Burst.Start();
	Burst.Advance(0.5f);
	Burst.Cancel();
	TestEqual(TEXT("Cancelled: nothing more"), Burst.Advance(5.f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGunnerBurstTelegraph, "Hawkeye.Gunner.BurstTimingAndTelegraph", HawkeyeEnemyTest::Flags)

bool FHawkeyeGunnerBurstTelegraph::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Gunner = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Pistol, Gunner);
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(600.f, 0.f, 0.f));
	if (!Brain || !Kate)
	{
		AddError(TEXT("Could not spawn the gunner and Kate."));
		return false;
	}
	UWeaponComponent* Pistol = Gunner->GetWeaponComponent();
	TestEqual(TEXT("12 a shot"), Pistol->Damage, 12.f);
	TestEqual(TEXT("6 degree base cone (widened by her sideways speed)"), Brain->AimSpreadDegrees, 6.f);
	TestEqual(TEXT("0.8 s telegraph"), Brain->Burst.TelegraphSeconds, 0.8f);
	TestEqual(TEXT("3-shot bursts"), Brain->Burst.ShotsPerBurst, 3);

	Alert(Brain, Kate);
	float Clock = 0.f;
	auto Step = [&]()
	{
		Clock += 0.25f;
		Pistol->SetTestTimeSeconds(Clock);
		// The world's clock too: a burst's attack token comes back with a gap timed on it.
		TestWorld.AdvanceClock(0.25f);
		ThinkThug(Brain, 0.25f);
	};
	Step();
	TestTrue(TEXT("He telegraphs"), Brain->IsTelegraphing());
	TestTrue(TEXT("Pistol raised"), Gunner->IsWeaponRaised());
	TestTrue(TEXT("Muzzle glint on"), Gunner->IsTelegraphGlintOn());
	Step();
	Step();
	Step();
	TestEqual(TEXT("Nothing in the first 0.75 s of it"), Pistol->CurrentAmmo, 12);
	TestTrue(TEXT("Still telegraphing at 0.75 s"), Brain->IsTelegraphing());
	Step();
	TestEqual(TEXT("The first shot once 0.8 s are up"), Pistol->CurrentAmmo, 11);
	TestFalse(TEXT("The glint goes out when he fires"), Gunner->IsTelegraphGlintOn());
	Step();
	TestEqual(TEXT("The second"), Pistol->CurrentAmmo, 10);
	Step();
	TestEqual(TEXT("The third"), Pistol->CurrentAmmo, 9);
	TestFalse(TEXT("The burst is over"), Brain->IsBursting());
	TestFalse(TEXT("Pistol down"), Gunner->IsWeaponRaised());
	// No cover here: he pauses and comes again.
	Step();
	Step();
	Step();
	TestEqual(TEXT("A pause between bursts"), Pistol->CurrentAmmo, 9);
	Step();
	Step();
	TestTrue(TEXT("Then the next telegraph"), Brain->IsTelegraphing());

	// A hit breaks it: the arrow staggers him and the burst is gone.
	Kate->GetHealthComponent()->SetMaxHealth(100.f, true);
	Gunner->GetHealthComponent()->ApplyDamage(20.f, Kate);
	Gunner->GetHealthComponent()->Stagger(Kate);
	TestFalse(TEXT("The hit broke the burst"), Brain->IsBursting());
	TestFalse(TEXT("Glint out"), Gunner->IsTelegraphGlintOn());
	TestTrue(TEXT("Staggered"), Gunner->IsStaggered());
	const int32 Ammo = Pistol->CurrentAmmo;
	Step();
	Step();
	TestEqual(TEXT("No shot while staggered"), Pistol->CurrentAmmo, Ammo);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGunnerRetreat, "Hawkeye.Gunner.RetreatRule", HawkeyeEnemyTest::Flags)

bool FHawkeyeGunnerRetreat::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	FVector Goal;
	TestFalse(TEXT("At 300 cm, no retreat"),
		AThugAIController::ComputeRetreatGoal(FVector(300.f, 0.f, 0.f), FVector::ZeroVector, 300.f, 600.f, Goal));
	TestTrue(TEXT("At 200 cm, retreat"),
		AThugAIController::ComputeRetreatGoal(FVector(200.f, 0.f, 0.f), FVector::ZeroVector, 300.f, 600.f, Goal));
	TestTrue(TEXT("To 600 cm from her, away along the same line"), Goal.Equals(FVector(600.f, 0.f, 0.f), 0.1f));

	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Gunner = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector(200.f, 0.f, 0.f), EThugWeapon::Pistol, Gunner);
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
	if (!Brain || !Kate)
	{
		AddError(TEXT("Could not spawn the gunner and Kate."));
		return false;
	}
	Alert(Brain, Kate);
	ThinkThug(Brain, 0.25f);
	TestEqual(TEXT("She is inside 300 cm: Reposition"), Brain->GetMode(), EThugMode::Reposition);
	TestTrue(TEXT("He backs off"), Brain->IsRetreating());
	TestEqual(TEXT("To 600 cm from her"), static_cast<float>(FVector::Dist2D(Brain->GetRetreatGoal(), Kate->GetActorLocation())), 600.f, 1.f);
	TestFalse(TEXT("No telegraph while backing off"), Brain->IsTelegraphing());

	Gunner->SetActorLocation(FVector(580.f, 0.f, 0.f));
	ThinkThug(Brain, 0.25f);
	TestFalse(TEXT("At 580 cm he is done"), Brain->IsRetreating());
	ThinkThug(Brain, 0.25f);
	TestEqual(TEXT("And back to the fight"), Brain->GetMode(), EThugMode::Attack);
	return true;
}

// --- Archer ----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArcherLead, "Hawkeye.Archer.LeadCalculation", HawkeyeEnemyTest::Flags)

bool FHawkeyeArcherLead::RunTest(const FString& Parameters)
{
	const FVector Still = AThugAIController::ComputeLeadAimPoint(FVector::ZeroVector, FVector(2000.f, 0.f, 0.f),
		FVector::ZeroVector, 5000.f, 0.f);
	TestTrue(TEXT("A still target, no gravity: aim at her"), Still.Equals(FVector(2000.f, 0.f, 0.f), 0.1f));

	// Running across at 500 cm/s, 2000 cm away: t solves |(2000, 500 t)| = 5000 t.
	const FVector From = FVector::ZeroVector;
	const FVector Target(2000.f, 0.f, 0.f);
	const FVector Velocity(0.f, 500.f, 0.f);
	const FVector Aim = AThugAIController::ComputeLeadAimPoint(From, Target, Velocity, 5000.f, 0.f);
	const float T = static_cast<float>(Aim.Y) / 500.f;
	TestTrue(TEXT("Leads her the way she runs"), Aim.Y > 0.f);
	TestEqual(TEXT("The arrow and she arrive together"), static_cast<float>(FVector::Dist(From, Aim)), 5000.f * T, 0.5f);
	TestEqual(TEXT("t = 2000 / sqrt(5000^2 - 500^2)"), T, 2000.f / FMath::Sqrt(5000.f * 5000.f - 500.f * 500.f), 0.001f);

	// Gravity: aim high by the drop over the flight.
	const FVector Drop = AThugAIController::ComputeLeadAimPoint(From, Target, FVector::ZeroVector, 5000.f, -980.f);
	TestEqual(TEXT("0.5 g t^2 above her at t = 0.4 s"), static_cast<float>(Drop.Z), 0.5f * 980.f * 0.4f * 0.4f, 0.1f);

	// Faster than the arrow and running away: no intercept, aim at her.
	const FVector Hopeless = AThugAIController::ComputeLeadAimPoint(From, Target, FVector(6000.f, 0.f, 0.f), 5000.f, 0.f);
	TestTrue(TEXT("No intercept: straight at her"), Hopeless.Equals(Target, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArcherRange, "Hawkeye.Archer.RangeKeeping", HawkeyeEnemyTest::Flags)

bool FHawkeyeArcherRange::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	using ERange = EArcherRangeAction;
	TestEqual(TEXT("600 cm: relocate"), AThugAIController::ChooseArcherRangeAction(600.f, 800.f, 1500.f, 2500.f), ERange::Relocate);
	TestEqual(TEXT("1200 cm: step back"), AThugAIController::ChooseArcherRangeAction(1200.f, 800.f, 1500.f, 2500.f), ERange::StepBack);
	TestEqual(TEXT("2000 cm: hold"), AThugAIController::ChooseArcherRangeAction(2000.f, 800.f, 1500.f, 2500.f), ERange::Hold);
	TestEqual(TEXT("1500 and 2500 are in the band"), AThugAIController::ChooseArcherRangeAction(1500.f, 800.f, 1500.f, 2500.f), ERange::Hold);
	TestEqual(TEXT("2500 is in"), AThugAIController::ChooseArcherRangeAction(2500.f, 800.f, 1500.f, 2500.f), ERange::Hold);
	TestEqual(TEXT("2800 cm: approach"), AThugAIController::ChooseArcherRangeAction(2800.f, 800.f, 1500.f, 2500.f), ERange::Approach);

	// She is on top of him: he zips to the anchor that lands him back in the band.
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Archer = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Bow, Archer);
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(500.f, 0.f, 0.f));
	AGrappleAnchor* Near = Cast<AGrappleAnchor>(TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), FVector(-300.f, 0.f, 0.f), FRotator::ZeroRotator));
	AGrappleAnchor* Band = Cast<AGrappleAnchor>(TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), FVector(-1500.f, 0.f, 0.f), FRotator::ZeroRotator));
	if (!Brain || !Kate || !Near || !Band)
	{
		AddError(TEXT("Could not spawn the archer, Kate and two anchors."));
		return false;
	}
	ArmArcher(Archer);
	Alert(Brain, Kate);
	ThinkThug(Brain, 0.25f);
	if (!TestTrue(TEXT("Inside 800 cm he relocates"), Brain->IsZipping()))
	{
		return false;
	}
	TestTrue(TEXT("To the anchor 2000 cm from her, not the one 800 cm away"),
		FVector::Dist2D(Brain->GetZipEnd(), Band->GetLandingLocation()) < 5.f);
	TestFalse(TEXT("No draw while he goes"), Archer->GetBowComponent()->IsDrawing());
	for (int32 Step = 0; Step < 8 && Brain->IsZipping(); ++Step)
	{
		ThinkThug(Brain, 0.25f);
	}
	TestFalse(TEXT("Landed"), Brain->IsZipping());
	const float Distance = FVector::Dist2D(Archer->GetActorLocation(), Kate->GetActorLocation());
	TestTrue(TEXT("Back in the 1500 to 2500 cm band"), Distance >= 1500.f && Distance <= 2500.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArcherTelegraph, "Hawkeye.Archer.TelegraphInterrupt", HawkeyeEnemyTest::Flags)

bool FHawkeyeArcherTelegraph::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(2000.f, 0.f, 0.f));
	AThugCharacter* Archer = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Bow, Archer);
	AThugCharacter* Second = nullptr;
	AThugAIController* SecondBrain = SpawnThug(TestWorld, FVector(0.f, 600.f, 0.f), EThugWeapon::Bow, Second);
	if (!Brain || !SecondBrain || !Kate)
	{
		AddError(TEXT("Could not spawn two archers and Kate."));
		return false;
	}
	ArmArcher(Archer);
	ArmArcher(Second);
	UBowComponent* Bow = Archer->GetBowComponent();
	Bow->SetTestTimeSeconds(0.0);
	Alert(Brain, Kate);
	ThinkThug(Brain, 0.25f);
	TestTrue(TEXT("In the band he draws"), Bow->IsDrawing());
	TestTrue(TEXT("The purple glint is on"), Archer->IsTelegraphGlintOn());
	Bow->SetTestTimeSeconds(0.7);
	ThinkThug(Brain, 0.25f);
	TestTrue(TEXT("Still drawing at 0.7 s of 1.2"), Bow->IsDrawing());

	Archer->GetHealthComponent()->ApplyDamage(5.f, Kate);
	TestFalse(TEXT("Any hit breaks the draw"), Bow->IsDrawing());
	TestFalse(TEXT("Glint out"), Archer->IsTelegraphGlintOn());
	TestEqual(TEXT("No arrow left the bow"), Brain->GetArrowsLoosed(), 0);

	// Left alone, the second draws for the full 1.2 s and looses: once the gap after the first's broken draw has
	// passed, since the two take turns on her ranged token.
	TestWorld.AdvanceClock(1.f);
	UBowComponent* SecondBow = Second->GetBowComponent();
	SecondBow->SetTestTimeSeconds(0.0);
	Alert(SecondBrain, Kate);
	ThinkThug(SecondBrain, 0.25f);
	SecondBow->SetTestTimeSeconds(1.1);
	ThinkThug(SecondBrain, 0.25f);
	TestEqual(TEXT("Nothing before 1.2 s"), SecondBrain->GetArrowsLoosed(), 0);
	SecondBow->SetTestTimeSeconds(1.21);
	ThinkThug(SecondBrain, 0.25f);
	TestEqual(TEXT("Loosed at full draw"), SecondBrain->GetArrowsLoosed(), 1);
	TestFalse(TEXT("And the glint is out"), Second->IsTelegraphGlintOn());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArcherArrowPickup, "Hawkeye.Archer.ArrowPickedUpAsStandard", HawkeyeEnemyTest::Flags)

bool FHawkeyeArcherArrowPickup::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 100.f));
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(1000.f, 0.f, 100.f), FRotator::ZeroRotator));
	AThugCharacter* Archer = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector(0.f, 2000.f, 0.f), EThugWeapon::Bow, Archer);
	if (!Kate || !Wall || !Brain)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	Wall->SetExtent(FVector(20.f, 300.f, 300.f));
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	UArrowDefinition* Standard = Inventory->GetStandardArrowDefinition();
	Inventory->AddArrows(Standard, 10);
	ArmArcher(Archer, Standard);
	UArrowDefinition* Trickshot = Archer->GetBowComponent()->OwnArrow;
	Trickshot->bOverrideColors = true;

	auto Stick = [&](const FVector& At)
	{
		AArrowProjectile* Arrow = Cast<AArrowProjectile>(TestWorld.SpawnActor(AArrowProjectile::StaticClass(), At, FRotator::ZeroRotator));
		Arrow->InitArrow(Trickshot, Archer->GetBowComponent()->OwnBow, 30.f, Archer, Archer->GetBowComponent());
		Arrow->LaunchWithVelocity(FVector(5000.f, 0.f, 0.f));
		for (float Elapsed = 0.f; !Arrow->IsStuck() && Elapsed < 1.f; Elapsed += 0.01f)
		{
			Arrow->AdvanceFlight(0.01f);
		}
		return Arrow;
	};
	AArrowProjectile* First = Stick(FVector(700.f, 0.f, 100.f));
	AArrowProjectile* SecondArrow = Stick(FVector(700.f, 60.f, 100.f));
	TestTrue(TEXT("His arrows stick like hers"), First->IsStuck() && SecondArrow->IsStuck());

	Kate->SetActorLocation(First->GetActorLocation() - FVector(120.f, 0.f, 0.f));
	TestFalse(TEXT("Not picked up before"), Inventory->HasPickedUp(Trickshot));
	TestTrue(TEXT("Picked up within 150 cm"), First->TryRecoverBy(Kate));
	TestEqual(TEXT("As a standard arrow in slot 1"), Inventory->GetArrowCount(1), 11);
	TestTrue(TEXT("Trickshot's arrow noted (the toast shows once)"), Inventory->HasPickedUp(Trickshot));
	TestFalse(TEXT("The toast does not show twice"), Inventory->NoteFirstPickup(Trickshot));
	TestTrue(TEXT("The second one too"), SecondArrow->TryRecoverBy(Kate));
	TestEqual(TEXT("12 standard"), Inventory->GetArrowCount(1), 12);
	return true;
}

// --- Squad and modes -------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSquadAlert, "Hawkeye.Thug.SquadAlertPropagation", HawkeyeEnemyTest::Flags)

bool FHawkeyeSquadAlert::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, -800.f, 0.f));
	AThugCharacter* Spotter = nullptr;
	AThugAIController* SpotterBrain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Bat, Spotter);
	AThugCharacter* Near = nullptr;
	AThugAIController* NearBrain = SpawnThug(TestWorld, FVector(1000.f, 0.f, 0.f), EThugWeapon::Fists, Near);
	AThugCharacter* Far = nullptr;
	AThugAIController* FarBrain = SpawnThug(TestWorld, FVector(-2000.f, 0.f, 0.f), EThugWeapon::Fists, Far);
	AThugCharacter* Walled = nullptr;
	AThugAIController* WalledBrain = SpawnThug(TestWorld, FVector(0.f, 1000.f, 0.f), EThugWeapon::Fists, Walled);
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(0.f, 500.f, 0.f), FRotator::ZeroRotator));
	if (!Kate || !SpotterBrain || !NearBrain || !FarBrain || !WalledBrain || !Wall)
	{
		AddError(TEXT("Could not spawn the squad."));
		return false;
	}
	Wall->SetExtent(FVector(300.f, 20.f, 300.f));

	Alert(SpotterBrain, Kate);
	TestEqual(TEXT("The spotter is alerted"), Spotter->GetAlertState(), EThugAlertState::Alerted);
	TestEqual(TEXT("The alert goes out in 1.5 s"), SpotterBrain->GetSquadAlertRemaining(), 1.5f, 0.001f);
	ThinkThug(SpotterBrain, 1.f);
	TestEqual(TEXT("Nobody told after 1 s"), Near->GetAlertState(), EThugAlertState::Calm);
	ThinkThug(SpotterBrain, 0.6f);
	TestEqual(TEXT("After 1.5 s, the thug 10 m away with a line to him: suspicious"), Near->GetAlertState(),
		EThugAlertState::Suspicious);
	TestTrue(TEXT("Toward where the spotter had her"), NearBrain->GetLastStimulusLocation().Equals(Kate->GetActorLocation(), 1.f));
	TestEqual(TEXT("20 m away: calm"), Far->GetAlertState(), EThugAlertState::Calm);
	TestEqual(TEXT("Behind a wall: calm"), Walled->GetAlertState(), EThugAlertState::Calm);
	TestTrue(TEXT("Told once"), SpotterBrain->GetSquadAlertRemaining() < 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugModePriority, "Hawkeye.Thug.ModePriority", HawkeyeEnemyTest::Flags)

bool FHawkeyeThugModePriority::RunTest(const FString& Parameters)
{
	using namespace HawkeyeEnemyTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Bat, Thug);
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(1000.f, 0.f, 0.f));
	if (!Brain || !Kate)
	{
		AddError(TEXT("Could not spawn the thug and Kate."));
		return false;
	}
	ThinkThug(Brain, 0.f);
	TestEqual(TEXT("Calm: patrol"), Brain->GetMode(), EThugMode::Patrol);
	Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, 1.f);
	ThinkThug(Brain, 0.f);
	TestEqual(TEXT("Suspicious: investigate"), Brain->GetMode(), EThugMode::Investigate);
	Alert(Brain, Kate);
	ThinkThug(Brain, 0.f);
	TestEqual(TEXT("Alerted: attack"), Brain->GetMode(), EThugMode::Attack);
	Brain->SetHeld(true);
	ThinkThug(Brain, 0.f);
	TestEqual(TEXT("Held beats everything"), Brain->GetMode(), EThugMode::Stunned);
	Brain->SetHeld(false);
	Brain->SetThinkingEnabled(false);
	ThinkThug(Brain, 0.f);
	TestEqual(TEXT("Switched off: stunned"), Brain->GetMode(), EThugMode::Stunned);
	Brain->SetThinkingEnabled(true);
	Thug->Knockdown(Kate);
	ThinkThug(Brain, 0.f);
	TestEqual(TEXT("On the floor: stunned"), Brain->GetMode(), EThugMode::Stunned);

	// The cover query the builder writes: a donut of candidates, a blocked-trace filter, nearest first.
	UEnvQuery* Query = NewObject<UEnvQuery>(GetTransientPackage());
	UHawkeyeThugTreeBuilder::FillCoverQuery(Query, 800.f);
	TestEqual(TEXT("EQS_CoverPoints shape"), UHawkeyeThugTreeBuilder::DescribeCoverQuery(Query),
		FString(TEXT("generator Donut (outer 800 cm), tests Trace+Distance")));
	return true;
}

#endif
