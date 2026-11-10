#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/WeaponComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/ThugGroupObjective.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The first fight's rules: thug swings (telegraph, sweep, stagger), gunner or brawler by weapon,
 * Kate's light and heavy, the dodge's invulnerability, and the clear_roof objective.
 */
namespace HawkeyeMeleeTest
{
	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At, float Yaw)
	{
		return Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
	}

	static AThugAIController* Possess(const FHawkeyeTestWorld& TestWorld, AThugCharacter* Thug)
	{
		AThugAIController* Brain = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Brain && Thug)
		{
			// A thug spawned by the test world is auto-possessed by the default controller class;
			// swap it for a fresh one the test holds.
			if (AController* Existing = Thug->GetController())
			{
				Existing->UnPossess();
			}
			Brain->Possess(Thug);
		}
		return Brain;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeThugSweepStaggersPlayer, "Hawkeye.Melee.ThugSwingHitsAndStaggersPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeThugSweepStaggersPlayer::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector::ZeroVector, 0.f);
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(100.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f)));
	if (!Thug || !Kate || !Thug->GetMeleeComponent() || !Kate->GetHealthComponent())
	{
		AddError(TEXT("Failed to spawn the thug and Kate."));
		return false;
	}

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Kate->GetHealthComponent()->OnStaggered.AddDynamic(Listener, &UHawkeyeTestListener::HandleStaggered);

	for (const EThugWeapon Weapon : { EThugWeapon::Fists, EThugWeapon::Bat })
	{
		Kate->GetHealthComponent()->SetMaxHealth(100.f, true);
		Thug->Weapon = Weapon;
		const FHawkeyeMeleeAttack Attack = Thug->GetMeleeAttack();
		TestTrue(TEXT("The swing starts"), Thug->GetMeleeComponent()->StartAttack(Attack));
		Thug->GetMeleeComponent()->AdvanceAttack(Attack.WindupSeconds + 0.01f);

		const float Expected = Weapon == EThugWeapon::Bat ? 25.f : 15.f;
		TestEqual(FString::Printf(TEXT("%s takes %.0f off Kate"), *Attack.Name.ToString(), Expected),
			Kate->GetHealthComponent()->GetCurrentHealth(), 100.f - Expected);
		TestTrue(TEXT("And staggers her"), Kate->IsStaggered());
		Thug->GetMeleeComponent()->AdvanceAttack(Attack.RecoverSeconds);
		TestFalse(TEXT("The swing is over after its recovery"), Thug->GetMeleeComponent()->IsAttacking());
	}
	TestEqual(TEXT("One stagger per swing"), Listener->StaggeredCount, 2);

	// A thug's swing never hits the thug beside him.
	AThugCharacter* Friend = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(0.f, 0.f, 0.f), 0.f);
	Kate->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	Friend->SetActorLocation(FVector(90.f, 0.f, 0.f));
	Thug->GetMeleeComponent()->StartAttack(Thug->GetMeleeAttack());
	Thug->GetMeleeComponent()->AdvanceAttack(1.f);
	TestEqual(TEXT("The thug beside him is untouched"), Friend->GetHealthComponent()->GetCurrentHealth(), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeWindupBeforeDamage, "Hawkeye.Melee.WindupTelegraphsBeforeDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeWindupBeforeDamage::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector::ZeroVector, 0.f);
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(100.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f)));
	if (!Thug || !Kate)
	{
		AddError(TEXT("Failed to spawn the thug and Kate."));
		return false;
	}

	UMeleeComponent* Melee = Thug->GetMeleeComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Listener->WatchedMeleeVictim = Kate->GetHealthComponent();
	Melee->OnAttackWindup.AddDynamic(Listener, &UHawkeyeTestListener::HandleMeleeWindup);
	Melee->OnAttackLanded.AddDynamic(Listener, &UHawkeyeTestListener::HandleMeleeLanded);

	Thug->Weapon = EThugWeapon::Fists;
	TestEqual(TEXT("The fists wind up for 0.6 s"), Thug->GetMeleeAttack().WindupSeconds, 0.6f);
	Melee->StartAttack(Thug->GetMeleeAttack());
	TestEqual(TEXT("The telegraph fires as the swing starts"), Listener->MeleeWindupCount, 1);
	TestEqual(TEXT("Kate is untouched when it does"), Listener->VictimHealthAtWindup, 100.f);
	TestTrue(TEXT("He is winding up"), Melee->IsWindingUp());
	TestFalse(TEXT("A second swing is refused mid-swing"), Melee->StartAttack(Thug->GetMeleeAttack()));

	Melee->AdvanceAttack(0.55f);
	TestEqual(TEXT("Nothing lands during the wind-up"), Kate->GetHealthComponent()->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("No hit yet"), Listener->MeleeLandedCount, 0);

	Melee->AdvanceAttack(0.1f);
	TestEqual(TEXT("It lands at the end of the wind-up"), Listener->MeleeLandedCount, 1);
	TestEqual(TEXT("For 15"), Listener->LastMeleeDamage, 15.f);
	TestTrue(TEXT("On Kate"), Listener->LastMeleeHitActor == Kate);

	// Hitting him first interrupts the swing: the counter.
	Kate->GetHealthComponent()->SetMaxHealth(100.f, true);
	Melee->AdvanceAttack(1.f);
	Melee->StartAttack(Thug->GetMeleeAttack());
	Thug->GetHealthComponent()->ApplyMeleeDamage(15.f, Kate, true);
	TestFalse(TEXT("A punch during the wind-up cancels it"), Melee->IsAttacking());
	Melee->AdvanceAttack(1.f);
	TestEqual(TEXT("And the interrupted swing never lands"), Kate->GetHealthComponent()->GetCurrentHealth(), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugWeaponPicksBehaviour, "Hawkeye.Thug.WeaponPicksGunnerOrMelee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugWeaponPicksBehaviour::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	AThugCharacter* Gunner = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(500.f, 0.f, 0.f), 180.f);
	AThugCharacter* Brawler = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(0.f, 110.f, 0.f), -90.f);
	AThugAIController* GunnerBrain = HawkeyeMeleeTest::Possess(TestWorld, Gunner);
	AThugAIController* BrawlerBrain = HawkeyeMeleeTest::Possess(TestWorld, Brawler);
	if (!Kate || !GunnerBrain || !BrawlerBrain)
	{
		AddError(TEXT("Failed to spawn Kate and two possessed thugs."));
		return false;
	}

	Gunner->Weapon = EThugWeapon::Pistol;
	Brawler->Weapon = EThugWeapon::Bat;
	TestTrue(TEXT("Pistol is the gunner"), Gunner->IsGunner());
	TestFalse(TEXT("Bat is not"), Brawler->IsGunner());

	for (AThugAIController* Brain : { GunnerBrain, BrawlerBrain })
	{
		Brain->SetTarget(Kate);
		Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
		ThinkThug(Brain, 1.f);
	}
	// The gunner telegraphs first (0.8 s, pistol raised), then his burst starts.
	TestTrue(TEXT("The gunner telegraphs before he shoots"), GunnerBrain->IsTelegraphing());
	TestEqual(TEXT("Nothing fired during the telegraph"), Gunner->GetWeaponComponent()->CurrentAmmo, 12);
	ThinkThug(GunnerBrain, 1.f);

	TestEqual(TEXT("The gunner shot"), Gunner->GetWeaponComponent()->CurrentAmmo, 11);
	TestFalse(TEXT("The gunner did not swing"), Gunner->GetMeleeComponent()->IsAttacking());
	TestTrue(TEXT("The bat thug swung"), Brawler->GetMeleeComponent()->IsAttacking());
	TestEqual(TEXT("With the bat"), Brawler->GetMeleeComponent()->GetCurrentAttack().Name, FName(TEXT("bat")));
	TestEqual(TEXT("And never fired"), Brawler->GetWeaponComponent()->CurrentAmmo, 12);

	// A thug on the floor does nothing until he is up.
	Brawler->GetMeleeComponent()->AdvanceAttack(5.f);
	Brawler->Knockdown(Kate);
	ThinkThug(BrawlerBrain, 5.f);
	TestFalse(TEXT("Knocked down, he does not swing"), Brawler->GetMeleeComponent()->IsAttacking());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeKateLight, "Hawkeye.Melee.KateLightDamageAndTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeKateLight::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator(0.f, 30.f, 0.f)));
	AThugCharacter* Thug = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f), 180.f);
	if (!Kate || !Thug || !Kate->GetMeleeComponent())
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();

	TestTrue(TEXT("The light starts"), Kate->StartLightAttack());
	TestTrue(TEXT("The blow goes at the thug in reach at once"),
		FVector::DotProduct(Melee->GetSwingDirection(), FVector::ForwardVector) > 0.99f);
	Kate->AdvanceMeleeFlow(0.1f);
	TestTrue(TEXT("She has turned to him 0.1 s later"),
		FVector::DotProduct(Kate->GetActorForwardVector(), FVector::ForwardVector) > 0.99f);
	TestEqual(TEXT("The light lands 0.1 s in"), Melee->GetCurrentAttack().WindupSeconds, 0.1f);
	TestFalse(TEXT("No second swing while one is going"), Kate->StartLightAttack());

	Melee->AdvanceAttack(0.11f);
	TestEqual(TEXT("15 off the thug"), Thug->GetHealthComponent()->GetCurrentHealth(), 85.f);
	TestTrue(TEXT("He staggers (above the threshold of 10)"), Thug->IsStaggered());
	TestTrue(TEXT("He flashes"), Thug->GetHitFlashAlpha() > 0.99f);
	TestTrue(TEXT("The swing is still recovering"), Kate->IsMeleeAttacking());

	Melee->AdvanceAttack(0.17f);
	TestTrue(TEXT("Still recovering at 0.28 s"), Kate->IsMeleeAttacking());
	Melee->AdvanceAttack(0.03f);
	TestFalse(TEXT("0.3 s in all"), Kate->IsMeleeAttacking());
	TestTrue(TEXT("And she can go again"), Kate->StartLightAttack());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeKateHeavy, "Hawkeye.Melee.KateHeavyKnocksDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeKateHeavy::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	AThugCharacter* Thug = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(110.f, 0.f, 0.f), 180.f);
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();

	TestTrue(TEXT("The heavy starts"), Kate->StartHeavyAttack());
	Melee->AdvanceAttack(0.55f);
	TestEqual(TEXT("Nothing before the 0.6 s wind-up ends"), Thug->GetHealthComponent()->GetCurrentHealth(), 100.f);
	Melee->AdvanceAttack(0.1f);
	TestEqual(TEXT("35 off the thug"), Thug->GetHealthComponent()->GetCurrentHealth(), 65.f);
	TestTrue(TEXT("He is on the floor"), Thug->IsKnockedDown());
	TestFalse(TEXT("A knockdown, not a stagger"), Thug->IsStaggered());
	TestTrue(TEXT("Knocked down counts as out of the fight"), Thug->IsIncapacitated());

	Thug->UpdateKnockdown(Thug->KnockdownSeconds - 0.1f);
	TestTrue(TEXT("Still down just before KnockdownSeconds"), Thug->IsKnockedDown());
	Thug->UpdateKnockdown(0.2f);
	TestFalse(TEXT("Up again after it"), Thug->IsKnockedDown());

	// A heavy that kills does not leave a knocked-down corpse.
	Melee->AdvanceAttack(1.f);
	Thug->GetHealthComponent()->ApplyDamage(40.f, Kate);
	Kate->StartHeavyAttack();
	Melee->AdvanceAttack(0.61f);
	TestFalse(TEXT("The killing heavy leaves him dead, not knocked down"), Thug->IsKnockedDown());
	TestTrue(TEXT("And limp"), Thug->IsLimp());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDodgeInvulnerability, "Hawkeye.Dodge.InvulnerabilityWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDodgeInvulnerability::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	UHealthComponent* Health = Kate ? Kate->GetHealthComponent() : nullptr;
	if (!Health)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}

	TestFalse(TEXT("No direction, no dodge"), Kate->TryDodge(FVector::ZeroVector));
	TestTrue(TEXT("A dodge sideways starts"), Kate->TryDodge(FVector(0.f, 1.f, 0.f)));
	TestTrue(TEXT("She is dodging"), Kate->IsDodging());
	TestTrue(TEXT("And invulnerable"), Health->IsInvulnerable());
	TestEqual(TEXT("A hit in the window does nothing"), Health->ApplyMeleeDamage(25.f, nullptr, true), 0.f);
	TestFalse(TEXT("Nor staggers her"), Kate->IsStaggered());

	Kate->TestTickDodge(0.2f);
	TestTrue(TEXT("Still invulnerable at 0.2 s"), Health->IsInvulnerable());
	Kate->TestTickDodge(0.06f);
	TestFalse(TEXT("Not after 0.25 s"), Health->IsInvulnerable());
	TestTrue(TEXT("Though the dash runs its 0.4 s"), Kate->IsDodging());
	TestEqual(TEXT("Hits land again"), Health->ApplyDamage(10.f, nullptr), 10.f);

	TestFalse(TEXT("No second dodge inside the 0.8 s cooldown"), Kate->TryDodge(FVector(1.f, 0.f, 0.f)));
	Kate->TestTickDodge(0.6f);
	TestFalse(TEXT("The dash is over"), Kate->IsDodging());
	Kate->TestSetSprintFlag(true);
	TestFalse(TEXT("Sprinting, Ctrl slides; it never dodges"), Kate->TryDodge(FVector(1.f, 0.f, 0.f)));
	Kate->TestSetSprintFlag(false);
	TestTrue(TEXT("After the cooldown she can go again"), Kate->TryDodge(FVector(1.f, 0.f, 0.f)));

	// A dodge never switches off invulnerability something else turned on.
	Kate->TestTickDodge(1.f);
	Health->SetInvulnerable(true);
	Kate->TryDodge(FVector(1.f, 0.f, 0.f));
	Kate->TestTickDodge(1.f);
	TestTrue(TEXT("Scripted invulnerability survives a dodge"), Health->IsInvulnerable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLowHealthAlpha, "Hawkeye.Health.LowHealthDesaturation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLowHealthAlpha::RunTest(const FString& Parameters)
{
	const AHawkeyeCharacter* Kate = GetDefault<AHawkeyeCharacter>();
	TestEqual(TEXT("Full health: none"), Kate->ComputeLowHealthAlpha(1.f), 0.f);
	TestEqual(TEXT("At 40%: none yet"), Kate->ComputeLowHealthAlpha(0.4f), 0.f);
	TestEqual(TEXT("At 20%: half"), Kate->ComputeLowHealthAlpha(0.2f), 0.5f);
	TestEqual(TEXT("At 0: all of it"), Kate->ComputeLowHealthAlpha(0.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeClearRoofObjective, "Hawkeye.Mission.ClearRoofWhenGroupIsDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeClearRoofObjective::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	if (!Missions)
	{
		AddError(TEXT("The test world has no UMissionSubsystem."));
		return false;
	}

	UMissionDefinition* Mission = NewObject<UMissionDefinition>();
	for (const TCHAR* Id : { TEXT("cross_block"), TEXT("clear_roof"), TEXT("find_arrow") })
	{
		UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
		Objective->ObjectiveId = FName(Id);
		Mission->Objectives.Add(Objective);
	}
	Missions->StartMission(Mission);

	static const FName RoofPair(TEXT("RoofPair"));
	AThugCharacter* First = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(0.f, 0.f, 0.f), 0.f);
	AThugCharacter* Second = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(300.f, 0.f, 0.f), 0.f);
	AThugCharacter* Stranger = HawkeyeMeleeTest::SpawnThug(TestWorld, FVector(600.f, 0.f, 0.f), 0.f);
	First->Tags.Add(RoofPair);
	Second->Tags.Add(RoofPair);

	AThugGroupObjective* Group = World->SpawnActorDeferred<AThugGroupObjective>(
		AThugGroupObjective::StaticClass(), FTransform::Identity);
	Group->ObjectiveId = FName(TEXT("clear_roof"));
	Group->GroupTag = RoofPair;
	Group->FinishSpawning(FTransform::Identity);

	TestEqual(TEXT("The group is the two tagged thugs"), Group->GetGroupSize(), 2);
	auto IsDone = [Missions](const TCHAR* Id)
	{
		for (const UMissionObjective* Objective : Missions->GetActiveObjectives())
		{
			if (Objective->ObjectiveId == FName(Id))
			{
				return Objective->IsCompleted();
			}
		}
		return false;
	};

	Stranger->GetHealthComponent()->ApplyDamage(999.f, nullptr);
	TestFalse(TEXT("An untagged thug does not count"), IsDone(TEXT("clear_roof")));
	First->GetHealthComponent()->ApplyDamage(999.f, nullptr);
	TestFalse(TEXT("One of two down is not clear"), IsDone(TEXT("clear_roof")));
	TestEqual(TEXT("One left"), Group->GetAliveCount(), 1);
	// A takedown kills through health like anything else.
	ITakedownable::Execute_OnTakedown(Second, nullptr);
	TestTrue(TEXT("Both down completes clear_roof"), IsDone(TEXT("clear_roof")));
	TestTrue(TEXT("The group reports cleared"), Group->IsCleared());
	TestFalse(TEXT("Nothing else completed with it"), IsDone(TEXT("find_arrow")));
	return true;
}

#endif
