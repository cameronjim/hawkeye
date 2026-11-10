#include "Combat/BowComponent.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeCombo.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Combat/StrikePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Arkham-lite melee: the light chain and its damage, the combo counter and its bonus, the parry per
 * enemy type, the finisher's eligibility and invulnerability, the hit lean's direction, the strike
 * poses' hand targets, and the soft lock's pick and turn.
 */
namespace HawkeyeMeleeDepthTest
{
	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At, float Yaw, EThugWeapon Weapon)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
		if (Thug)
		{
			Thug->Weapon = Weapon;
		}
		return Thug;
	}

	static AThugAIController* Possess(const FHawkeyeTestWorld& TestWorld, AThugCharacter* Thug)
	{
		AThugAIController* Brain = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Brain && Thug)
		{
			if (AController* Existing = Thug->GetController())
			{
				Existing->UnPossess();
			}
			Brain->Possess(Thug);
		}
		return Brain;
	}

	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& At = FVector::ZeroVector, float Yaw = 0.f)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
	}

	/** One of Kate's swings from start to idle: the wind-up past its end, then the recovery. */
	static void LandSwing(UMeleeComponent* Melee)
	{
		Melee->AdvanceAttack(Melee->GetCurrentAttack().WindupSeconds + 0.01f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeComboWindowAndDamage, "Hawkeye.Melee.ComboWindowAndDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeComboWindowAndDamage::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;

	// The chain on its own.
	FHawkeyeComboTracker Chain;
	TestEqual(TEXT("A chain starts at step 0"), Chain.GetNextStep(), 0);
	Chain.NotifyLightLanded(0);
	TestEqual(TEXT("A landed first light opens step 1"), Chain.GetNextStep(), 1);
	Chain.Advance(0.34f);
	TestTrue(TEXT("Still open at 0.34 s"), Chain.IsChainOpen());
	Chain.Advance(0.02f);
	TestFalse(TEXT("Shut after 0.35 s"), Chain.IsChainOpen());
	TestEqual(TEXT("And back to step 0"), Chain.GetNextStep(), 0);
	Chain.NotifyLightLanded(0);
	Chain.NotifyLightLanded(1);
	TestEqual(TEXT("Two landed: the third is next"), Chain.GetNextStep(), 2);
	Chain.NotifyLightLanded(2);
	TestEqual(TEXT("The third ends the chain"), Chain.GetNextStep(), 0);
	TestFalse(TEXT("With no window after it"), Chain.IsChainOpen());
	Chain.NotifyLightLanded(0);
	Chain.NotifyMiss();
	TestEqual(TEXT("A heavy's miss starts it over"), Chain.GetNextStep(), 0);

	// A light that misses carries the chain on, with the same window, and never counts.
	FHawkeyeComboTracker Air;
	Air.NotifyLightMissed(0);
	TestEqual(TEXT("A missed first light opens step 1"), Air.GetNextStep(), 1);
	TestEqual(TEXT("For the same 0.35 s"), Air.GetChainWindowRemaining(), 0.35f, 0.001f);
	TestEqual(TEXT("The miss does not count"), Air.GetCount(), 0);
	Air.NotifyLightMissed(1);
	TestEqual(TEXT("A missed second opens the third"), Air.GetNextStep(), 2);
	Air.NotifyLightMissed(2);
	TestEqual(TEXT("A missed third ends the chain"), Air.GetNextStep(), 0);
	TestFalse(TEXT("With no window after it"), Air.IsChainOpen());
	Air.NotifyLightMissed(0);
	Air.Advance(0.36f);
	TestEqual(TEXT("A miss's window lapses like a hit's"), Air.GetNextStep(), 0);
	TestEqual(TEXT("And nothing ever counted"), Air.GetCount(), 0);

	// Kate's three lights on a thug: 15, 15, 25 and the knockback, the second pressed in the recovery.
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f), 180.f, EThugWeapon::Fists);
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	UHealthComponent* Health = Thug->GetHealthComponent();
	Health->SetMaxHealth(500.f, true);

	TestTrue(TEXT("The first light starts"), Kate->StartLightAttack());
	TestEqual(TEXT("It is the chain's first"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	LandSwing(Melee);
	TestEqual(TEXT("15"), Health->GetCurrentHealth(), 485.f, 0.01f);
	TestTrue(TEXT("Pressed in the recovery, the next light waits"), Kate->StartLightAttack());
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.2f);
	TestEqual(TEXT("It went when the recovery ended: the chain's second"), Melee->GetCurrentAttack().Name, FName(TEXT("light2")));
	LandSwing(Melee);
	TestEqual(TEXT("15 again"), Health->GetCurrentHealth(), 470.f, 0.01f);
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.2f);
	TestTrue(TEXT("The third inside the window"), Kate->StartLightAttack());
	TestEqual(TEXT("The chain's third"), Melee->GetCurrentAttack().Name, FName(TEXT("light3")));
	TestEqual(TEXT("It knocks back 150 cm"), Melee->GetCurrentAttack().KnockbackDistance, 150.f);
	LandSwing(Melee);
	TestEqual(TEXT("25"), Health->GetCurrentHealth(), 445.f, 0.01f);
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(0.1f);
	TestTrue(TEXT("After the third, a light starts a new chain"), Kate->StartLightAttack());
	TestEqual(TEXT("At step 0"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	LandSwing(Melee);

	// The window lapsing.
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(0.36f);
	TestTrue(TEXT("A light 0.36 s after the hit"), Kate->StartLightAttack());
	TestEqual(TEXT("Starts the chain over"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	LandSwing(Melee);

	// A heavy in the chain: 35 and a knockdown, and the chain ends. The counter is cleared first so its
	// bonus (x5 by now) stays out of the numbers.
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(2.1f);
	TestEqual(TEXT("The counter cleared"), Kate->GetComboCount(), 0);
	TestTrue(TEXT("A light to open a chain"), Kate->StartLightAttack());
	LandSwing(Melee);
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(0.1f);
	TestTrue(TEXT("The chain is open"), Kate->GetCombo().IsChainOpen());
	const float Before = Health->GetCurrentHealth();
	TestTrue(TEXT("A heavy mid-chain"), Kate->StartHeavyAttack());
	TestEqual(TEXT("Is the heavy"), Melee->GetCurrentAttack().Name, FName(TEXT("heavy")));
	LandSwing(Melee);
	TestEqual(TEXT("35"), Before - Health->GetCurrentHealth(), 35.f, 0.01f);
	TestTrue(TEXT("He is on the floor"), Thug->IsKnockedDown());
	TestEqual(TEXT("And the chain is over"), Kate->GetCombo().GetNextStep(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeAirChain, "Hawkeye.Melee.AirChainAdvancesOnMiss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeAirChain::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;

	// Nobody to hit: three lights pressed in time still play the chain's first, second and third.
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	TestTrue(TEXT("The first light starts"), Kate->StartLightAttack());
	TestEqual(TEXT("It is the chain's first"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	LandSwing(Melee);
	TestTrue(TEXT("It hit the air, and the chain is open"), Kate->GetCombo().IsChainOpen());
	TestEqual(TEXT("For the second"), Kate->GetCombo().GetNextStep(), 1);
	TestEqual(TEXT("The counter did not move"), Kate->GetComboCount(), 0);
	TestTrue(TEXT("Pressed in the recovery, the next light waits"), Kate->StartLightAttack());
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.2f);
	TestEqual(TEXT("It went when the recovery ended: the chain's second"), Melee->GetCurrentAttack().Name, FName(TEXT("light2")));
	LandSwing(Melee);
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.2f);
	TestTrue(TEXT("The third, inside the window the second's miss opened"), Kate->StartLightAttack());
	TestEqual(TEXT("The chain's third"), Melee->GetCurrentAttack().Name, FName(TEXT("light3")));
	LandSwing(Melee);
	TestEqual(TEXT("After the third the chain starts over"), Kate->GetCombo().GetNextStep(), 0);
	TestEqual(TEXT("Three misses, no count"), Kate->GetComboCount(), 0);

	// A miss's window lapses like a hit's.
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(0.1f);
	TestTrue(TEXT("A new chain"), Kate->StartLightAttack());
	LandSwing(Melee);
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(0.36f);
	TestTrue(TEXT("A light 0.36 s after the miss"), Kate->StartLightAttack());
	TestEqual(TEXT("Starts the chain over"), Melee->GetCurrentAttack().Name, FName(TEXT("light")));
	LandSwing(Melee);

	// A heavy at nobody still ends the chain.
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(0.05f);
	TestTrue(TEXT("The chain is open after the light"), Kate->GetCombo().IsChainOpen());
	TestTrue(TEXT("A heavy at the air"), Kate->StartHeavyAttack());
	Melee->AdvanceAttack(2.f);
	TestEqual(TEXT("And the chain is over"), Kate->GetCombo().GetNextStep(), 0);
	TestFalse(TEXT("With no window"), Kate->GetCombo().IsChainOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeComboCounter, "Hawkeye.Melee.ComboCounterResetAndBonus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeComboCounter::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;

	FHawkeyeComboTracker Counter;
	for (int32 Hit = 0; Hit < 4; ++Hit)
	{
		Counter.NotifyHit();
	}
	TestEqual(TEXT("Four hits"), Counter.GetCount(), 4);
	TestFalse(TEXT("No bonus at x4"), Counter.IsBonusActive());
	TestEqual(TEXT("No multiplier at x4"), Counter.GetDamageMultiplier(), 1.f);
	Counter.NotifyHit();
	TestTrue(TEXT("The bonus at x5"), Counter.IsBonusActive());
	TestEqual(TEXT("+20%"), Counter.GetDamageMultiplier(), 1.2f, 0.001f);
	Counter.Advance(1.9f);
	TestEqual(TEXT("Still x5 at 1.9 s"), Counter.GetCount(), 5);
	Counter.Advance(0.11f);
	TestEqual(TEXT("Cleared 2 s after the last hit"), Counter.GetCount(), 0);
	TestFalse(TEXT("And the bonus with it"), Counter.IsBonusActive());
	Counter.NotifyHit();
	Counter.Advance(1.5f);
	Counter.NotifyHit();
	Counter.Advance(1.9f);
	TestEqual(TEXT("Each hit restarts the 2 s"), Counter.GetCount(), 2);
	Counter.Advance(0.2f);
	TestEqual(TEXT("Then it clears"), Counter.GetCount(), 0);

	TestEqual(TEXT("The HUD reads x3"), UHawkeyeHudWidget::FormatComboCount(3).ToString(), FString(TEXT("x3")));
	TestTrue(TEXT("Nothing under two"), UHawkeyeHudWidget::FormatComboCount(1).IsEmpty());

	// Kate: five lights, then the sixth does 18.
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f), 180.f, EThugWeapon::Fists);
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	UHealthComponent* Health = Thug->GetHealthComponent();
	Health->SetMaxHealth(500.f, true);
	for (int32 Hit = 0; Hit < 5; ++Hit)
	{
		Kate->StartLightAttack();
		LandSwing(Melee);
		Melee->AdvanceAttack(0.5f);
		// Past the chain window each time, so every one is a first light of 15.
		Kate->AdvanceMeleeFlow(0.4f);
	}
	TestEqual(TEXT("Five hits on the counter"), Kate->GetComboCount(), 5);
	TestEqual(TEXT("75 off him"), Health->GetCurrentHealth(), 425.f, 0.01f);
	TestTrue(TEXT("The counter glows"), Kate->IsComboBonusActive());
	Kate->StartLightAttack();
	TestEqual(TEXT("The sixth light carries the bonus"), Melee->GetCurrentAttack().Damage, 18.f, 0.01f);
	LandSwing(Melee);
	TestEqual(TEXT("18 off him"), Health->GetCurrentHealth(), 407.f, 0.01f);
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(2.1f);
	TestEqual(TEXT("2 s without a hit clears Kate's counter"), Kate->GetComboCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeParryWindow, "Hawkeye.Melee.ParryWindowPerEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeParryWindow::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	UHealthComponent* KateHealth = Kate->GetHealthComponent();

	// Fists, the bat, the heavy's bash and his slow bat: each parried in its wind-up.
	struct FCase
	{
		EThugWeapon Weapon;
		int32 Swing;
		EHawkeyeParryKind Kind;
		const TCHAR* Label;
	};
	const FCase Cases[] = {
		{ EThugWeapon::Fists, 0, EHawkeyeParryKind::Swing, TEXT("fists") },
		{ EThugWeapon::Bat, 0, EHawkeyeParryKind::Swing, TEXT("bat") },
		{ EThugWeapon::Shield, 0, EHawkeyeParryKind::Bash, TEXT("heavy's bash") },
		{ EThugWeapon::Shield, 1, EHawkeyeParryKind::Swing, TEXT("heavy's slow bat") },
	};
	for (const FCase& Case : Cases)
	{
		KateHealth->SetMaxHealth(100.f, true);
		AThugCharacter* Thug = SpawnThug(TestWorld, FVector(150.f, 0.f, 0.f), 180.f, Case.Weapon);
		UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
		if (!Melee)
		{
			AddError(TEXT("Failed to spawn a thug."));
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s: nothing to parry before the swing"), Case.Label),
			UHawkeyeMeleeRules::ClassifyParry(Thug), EHawkeyeParryKind::None);
		TestFalse(FString::Printf(TEXT("%s: so V is a strike, not a parry"), Case.Label), Kate->TryParry());
		Melee->StartAttack(Thug->GetMeleeAttack(Case.Swing));
		TestEqual(FString::Printf(TEXT("%s: the wind-up is parryable"), Case.Label), UHawkeyeMeleeRules::ClassifyParry(Thug), Case.Kind);
		const int32 Parries = Kate->GetParryCount();
		TestTrue(FString::Printf(TEXT("%s: parried"), Case.Label), Kate->TryParry());
		TestEqual(FString::Printf(TEXT("%s: counted"), Case.Label), Kate->GetParryCount(), Parries + 1);
		TestFalse(FString::Printf(TEXT("%s: his swing is gone"), Case.Label), Melee->IsAttacking());
		TestTrue(FString::Printf(TEXT("%s: he is staggered"), Case.Label), Thug->IsStaggered());
		TestEqual(FString::Printf(TEXT("%s: for 1.5 s"), Case.Label), Thug->GetStaggerRemaining(), 1.5f, 0.01f);
		Melee->AdvanceAttack(2.f);
		TestEqual(FString::Printf(TEXT("%s: Kate takes nothing"), Case.Label), KateHealth->GetCurrentHealth(), 100.f);
		Thug->Destroy();
	}

	// Out of reach, or behind her: no parry.
	AThugCharacter* Far = SpawnThug(TestWorld, FVector(300.f, 0.f, 0.f), 180.f, EThugWeapon::Fists);
	Far->GetMeleeComponent()->StartAttack(Far->GetMeleeAttack());
	TestFalse(TEXT("A swing wound up 300 cm off is not parried (250)"), Kate->TryParry());
	Far->SetActorLocation(FVector(-150.f, 0.f, 0.f));
	TestFalse(TEXT("Nor one behind her"), Kate->TryParry());
	Far->Destroy();

	// The gunner: his raised pistol loses its first shot, and he is not staggered.
	AThugCharacter* Gunner = SpawnThug(TestWorld, FVector(200.f, 0.f, 0.f), 180.f, EThugWeapon::Pistol);
	AThugAIController* Brain = Possess(TestWorld, Gunner);
	if (!Brain)
	{
		AddError(TEXT("Failed to possess the gunner."));
		return false;
	}
	TestFalse(TEXT("Gunner: nothing before the telegraph"), Kate->TryParry());
	Brain->Burst.Start();
	TestEqual(TEXT("Gunner: the telegraph is parryable"), UHawkeyeMeleeRules::ClassifyParry(Gunner), EHawkeyeParryKind::Burst);
	TestTrue(TEXT("Gunner: parried"), Kate->TryParry());
	TestTrue(TEXT("Gunner: the first shot will be deflected"), Brain->IsFirstShotDeflected());
	TestFalse(TEXT("Gunner: not staggered"), Gunner->IsStaggered());
	Brain->Burst.Advance(Brain->Burst.TelegraphSeconds + 0.01f);
	TestFalse(TEXT("Gunner: after the telegraph there is nothing to parry"), Brain->DeflectFirstShot());
	Gunner->Destroy();

	// The archer: never.
	AThugCharacter* Archer = SpawnThug(TestWorld, FVector(150.f, 0.f, 0.f), 180.f, EThugWeapon::Bow);
	Archer->GetMeleeComponent()->StartAttack(Archer->GetMeleeAttack());
	TestEqual(TEXT("Archer: never parryable"), UHawkeyeMeleeRules::ClassifyParry(Archer), EHawkeyeParryKind::None);
	TestFalse(TEXT("Archer: no parry"), Kate->TryParry());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeFinisher, "Hawkeye.Melee.FinisherEligibilityAndInvulnerability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeFinisher::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(150.f, 0.f, 0.f), 180.f, EThugWeapon::Fists);
	UFinisherComponent* Finisher = Kate ? Kate->GetFinisherComponent() : nullptr;
	if (!Kate || !Thug || !Finisher)
	{
		AddError(TEXT("Failed to spawn Kate (with a finisher) and a thug."));
		return false;
	}
	const FVector Here = Kate->GetActorLocation();

	TestFalse(TEXT("Standing and unhurt: no finisher"), UHawkeyeMeleeRules::IsFinisherTarget(Thug, Here, 200.f));
	TestFalse(TEXT("So F does nothing"), Kate->TryFinisher());
	Thug->StaggerFor(Kate, 1.f);
	TestFalse(TEXT("An ordinary stagger is not a finisher"), UHawkeyeMeleeRules::IsFinisherTarget(Thug, Here, 200.f));
	Thug->Parried(Kate, 1.5f);
	TestTrue(TEXT("Parry-staggered at 150 cm: a target"), UHawkeyeMeleeRules::IsFinisherTarget(Thug, Here, 200.f));
	Thug->SetActorLocation(FVector(250.f, 0.f, 0.f));
	TestFalse(TEXT("At 250 cm: out of reach"), UHawkeyeMeleeRules::IsFinisherTarget(Thug, Here, 200.f));
	Thug->SetActorLocation(FVector(150.f, 0.f, 0.f));

	UHealthComponent* KateHealth = Kate->GetHealthComponent();
	TestTrue(TEXT("F takes him"), Kate->TryFinisher());
	TestTrue(TEXT("The finisher is running"), Kate->IsPerformingFinisher());
	TestTrue(TEXT("Input is locked"), Kate->IsLockedOutByTakedown());
	TestTrue(TEXT("She is invulnerable"), KateHealth->IsInvulnerable());
	TestFalse(TEXT("No strike during it"), Kate->StartLightAttack());
	TestFalse(TEXT("No dodge during it"), Kate->TryDodge(FVector(0.f, 1.f, 0.f)));
	TestFalse(TEXT("No second finisher"), Kate->TryFinisher());
	TestEqual(TEXT("A hit on her does nothing"), KateHealth->ApplyDamage(25.f, Thug), 0.f);

	Finisher->AdvanceFinisher(0.3f);
	TestTrue(TEXT("Time slows by 0.3 s"), Finisher->IsSlowing());
	TestEqual(TEXT("To 0.5"), TestWorld.Get()->GetWorldSettings()->TimeDilation, 0.5f, 0.001f);
	TestTrue(TEXT("He is still up before the blow"), Thug->GetHealthComponent()->IsAlive());
	Finisher->AdvanceFinisher(0.1f);
	TestFalse(TEXT("The blow at 0.35 s is lethal"), Thug->GetHealthComponent()->IsAlive());
	TestEqual(TEXT("The camera is all the way in"), Finisher->GetCameraPushAlpha(), 1.f, 0.001f);
	Finisher->AdvanceFinisher(0.1f);
	TestFalse(TEXT("0.4 s of real time later the slow motion is over"), Finisher->IsSlowing());
	TestEqual(TEXT("Time is back to 1"), TestWorld.Get()->GetWorldSettings()->TimeDilation, 1.f, 0.001f);
	TestTrue(TEXT("Still locked at 0.5 s"), Kate->IsLockedOutByTakedown());
	TestTrue(TEXT("Still invulnerable at 0.5 s"), KateHealth->IsInvulnerable());
	Finisher->AdvanceFinisher(0.71f);
	TestFalse(TEXT("Over after 1.2 s"), Kate->IsPerformingFinisher());
	TestFalse(TEXT("Input back"), Kate->IsLockedOutByTakedown());
	TestFalse(TEXT("Invulnerability off"), KateHealth->IsInvulnerable());
	TestFalse(TEXT("A dead thug is no target"), UHawkeyeMeleeRules::IsFinisherTarget(Thug, Here, 200.f));

	// Knocked down counts; invulnerability something else set survives the finisher.
	AThugCharacter* Down = SpawnThug(TestWorld, FVector(120.f, 0.f, 0.f), 180.f, EThugWeapon::Bat);
	Down->KnockdownFor(Kate, 3.f);
	TestTrue(TEXT("Knocked down: a target"), UHawkeyeMeleeRules::IsFinisherTarget(Down, Here, 200.f));
	KateHealth->SetInvulnerable(true);
	TestTrue(TEXT("F on him"), Finisher->StartFinisher(Down, EHawkeyeFinisherStyle::Bow));
	Finisher->AdvanceFinisher(1.3f);
	TestFalse(TEXT("He is dead"), Down->GetHealthComponent()->IsAlive());
	TestTrue(TEXT("Scripted invulnerability is left on"), KateHealth->IsInvulnerable());
	KateHealth->SetInvulnerable(false);

	// The throw: forward on a strike, to her right with the bow.
	const FVector Strike = UFinisherComponent::ComputeImpulse(FVector::ForwardVector, EHawkeyeFinisherStyle::Strike, 900.f, 450.f);
	const FVector Sweep = UFinisherComponent::ComputeImpulse(FVector::ForwardVector, EHawkeyeFinisherStyle::Bow, 900.f, 450.f);
	TestTrue(TEXT("A strike throws him on and up"), Strike.Equals(FVector(900.f, 0.f, 450.f), 0.01f));
	TestTrue(TEXT("The bow sweep throws him to her right and up"), Sweep.Equals(FVector(0.f, 900.f, 450.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeFinisherGate, "Hawkeye.Melee.FinisherGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeFinisherGate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(100.f, 0.f, 0.f), 180.f, EThugWeapon::Fists);
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	Thug->GetHealthComponent()->SetMaxHealth(500.f, true);
	const FVector Here = Kate->GetActorLocation();
	auto IsTarget = [&]() { return UHawkeyeMeleeRules::IsFinisherTarget(Thug, Here, 200.f); };

	// The first and second lights stagger him but open nothing.
	TestTrue(TEXT("First light"), Kate->StartLightAttack());
	LandSwing(Melee);
	TestTrue(TEXT("He is staggered by it"), Thug->IsStaggered());
	TestFalse(TEXT("A first light opens no finisher"), IsTarget());
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.1f);
	TestTrue(TEXT("Second light"), Kate->StartLightAttack());
	LandSwing(Melee);
	TestFalse(TEXT("Nor does the second"), IsTarget());
	Melee->AdvanceAttack(0.2f);
	Kate->AdvanceMeleeFlow(0.1f);

	// The third, the ender, opens it for 1.0 s whether or not he is still staggered.
	TestTrue(TEXT("Third light"), Kate->StartLightAttack());
	LandSwing(Melee);
	TestTrue(TEXT("The third light opens the finisher"), IsTarget());
	TestEqual(TEXT("For 1.0 s"), Thug->GetFinisherWindowRemaining(), 1.f, 0.001f);
	Thug->UpdateFinisherOpening(0.6f);
	TestTrue(TEXT("Still open at 0.6 s"), IsTarget());
	Thug->UpdateFinisherOpening(0.41f);
	TestFalse(TEXT("Shut after 1.0 s"), IsTarget());

	// A heavy is an ender too (and knocks him down, which is open on its own).
	Melee->AdvanceAttack(0.5f);
	Kate->AdvanceMeleeFlow(0.5f);
	TestTrue(TEXT("A heavy"), Kate->StartHeavyAttack());
	LandSwing(Melee);
	TestTrue(TEXT("The heavy opens the window"), Thug->GetFinisherWindowRemaining() > 0.99f);
	TestTrue(TEXT("And he is down"), Thug->IsKnockedDown());
	Thug->UpdateFinisherOpening(2.f);
	TestTrue(TEXT("Down past the window: still a target"), IsTarget());
	Thug->UpdateKnockdown(5.f);
	TestFalse(TEXT("Up again: not a target"), Thug->IsKnockedDown() || IsTarget());

	// A parry stagger lasts as long as the stagger does.
	AThugCharacter* Swinger = SpawnThug(TestWorld, FVector(-120.f, 0.f, 0.f), 0.f, EThugWeapon::Bat);
	Swinger->Parried(Kate, 1.5f);
	TestTrue(TEXT("Parried: a target"), UHawkeyeMeleeRules::IsFinisherTarget(Swinger, Here, 200.f));
	Swinger->UpdateFinisherOpening(1.6f);
	TestFalse(TEXT("Parry stagger over: not a target"), UHawkeyeMeleeRules::IsFinisherTarget(Swinger, Here, 200.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitLeanDirection, "Hawkeye.Animation.HitLeanDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHitLeanDirection::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;
	const FVector Forward = FVector::ForwardVector;
	TestEqual(TEXT("From in front"), UHawkeyeMeleeRules::ClassifyHitDirection(Forward, FVector(1.f, 0.f, 0.f)), EHawkeyeHitDirection::Front);
	TestEqual(TEXT("From behind"), UHawkeyeMeleeRules::ClassifyHitDirection(Forward, FVector(-1.f, 0.f, 0.f)), EHawkeyeHitDirection::Back);
	TestEqual(TEXT("From the right (+Y)"), UHawkeyeMeleeRules::ClassifyHitDirection(Forward, FVector(0.f, 1.f, 0.f)), EHawkeyeHitDirection::Right);
	TestEqual(TEXT("From the left"), UHawkeyeMeleeRules::ClassifyHitDirection(Forward, FVector(0.f, -1.f, 0.f)), EHawkeyeHitDirection::Left);
	TestEqual(TEXT("45 degrees counts as the front"), UHawkeyeMeleeRules::ClassifyHitDirection(Forward, FVector(1.f, 1.f, 0.f)),
		EHawkeyeHitDirection::Front);
	TestEqual(TEXT("60 degrees is the side"), UHawkeyeMeleeRules::ClassifyHitDirection(Forward, FVector(0.5f, 0.866f, 0.f)),
		EHawkeyeHitDirection::Right);

	TestTrue(TEXT("Hit in front, leans back"),
		UHawkeyeMeleeRules::ComputeLeanDirection(Forward, EHawkeyeHitDirection::Front).Equals(FVector(-1.f, 0.f, 0.f), 0.001f));
	TestTrue(TEXT("Hit behind, leans forward"),
		UHawkeyeMeleeRules::ComputeLeanDirection(Forward, EHawkeyeHitDirection::Back).Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	TestTrue(TEXT("Hit on the left, leans right"),
		UHawkeyeMeleeRules::ComputeLeanDirection(Forward, EHawkeyeHitDirection::Left).Equals(FVector(0.f, 1.f, 0.f), 0.001f));
	TestTrue(TEXT("Hit on the right, leans left"),
		UHawkeyeMeleeRules::ComputeLeanDirection(Forward, EHawkeyeHitDirection::Right).Equals(FVector(0.f, -1.f, 0.f), 0.001f));

	// The rotation tips the bone's up toward the lean by 5 degrees at full alpha.
	const FVector Up = UHawkeyeMeleeRules::ComputeHitLeanRotation(FVector(-1.f, 0.f, 0.f), 1.f, 5.f).RotateVector(FVector::UpVector);
	TestTrue(TEXT("Up tips back"), Up.X < 0.f && FMath::Abs(Up.Y) < 0.001f);
	TestEqual(TEXT("By 5 degrees"), static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(Up.Z))), 5.f, 0.01f);
	const FVector Half = UHawkeyeMeleeRules::ComputeHitLeanRotation(FVector(0.f, 1.f, 0.f), 0.5f, 5.f).RotateVector(FVector::UpVector);
	TestTrue(TEXT("Half alpha, to the right"), Half.Y > 0.f);
	TestEqual(TEXT("By 2.5 degrees"), static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(Half.Z))), 2.5f, 0.01f);
	TestTrue(TEXT("No direction, no lean"), UHawkeyeMeleeRules::ComputeHitLeanRotation(FVector::ZeroVector, 1.f, 5.f).IsNearlyZero());

	// The lean's 0.2 s: up at the middle, gone at the end.
	FHawkeyeHitLeanClock Lean;
	Lean.Start(FVector(-1.f, 0.f, 0.f));
	Lean.Advance(0.05f);
	TestEqual(TEXT("A quarter in"), Lean.GetAlpha(), 0.7071f, 0.001f);
	Lean.Advance(0.05f);
	TestEqual(TEXT("Full at 0.1 s"), Lean.GetAlpha(), 1.f, 0.001f);
	Lean.Advance(0.1f);
	TestFalse(TEXT("Over at 0.2 s"), Lean.IsActive());
	TestEqual(TEXT("Nothing left"), Lean.GetAlpha(), 0.f);

	// A thug hit from in front leans back; from behind, forward. Kate leans when she is hit.
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector(100.f, 0.f, 0.f), 180.f);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector::ZeroVector, 0.f, EThugWeapon::Fists);
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	Thug->GetHealthComponent()->ApplyDamage(5.f, Kate);
	TestEqual(TEXT("Kate in front of him: a front hit"), Thug->GetLastHitDirection(), EHawkeyeHitDirection::Front);
	TestTrue(TEXT("He leans back"), Thug->GetHitLean().IsActive() && Thug->GetHitLean().GetDirection().Equals(FVector(-1.f, 0.f, 0.f), 0.01f));
	Kate->SetActorLocation(FVector(-100.f, 0.f, 0.f));
	Thug->GetHealthComponent()->ApplyDamage(5.f, Kate);
	TestEqual(TEXT("Kate behind him: a back hit"), Thug->GetLastHitDirection(), EHawkeyeHitDirection::Back);
	TestTrue(TEXT("He leans forward"), Thug->GetHitLean().GetDirection().Equals(FVector(1.f, 0.f, 0.f), 0.01f));
	Kate->GetHealthComponent()->ApplyDamage(5.f, Thug);
	TestTrue(TEXT("Kate leans when she is hit"), Kate->GetHitLean().IsActive());

	// The anim instance turns the lean into the rotation the graph adds to spine_01.
	UHawkeyeBowIKAnimInstance* Hands = NewObject<UHawkeyeBowIKAnimInstance>(Kate->GetMesh());
	Hands->SetHitLean(FVector(1.f, 0.f, 0.f), 1.f);
	Hands->ComposeOutputs();
	TestFalse(TEXT("The graph's lean is on"), Hands->HitLean.IsNearlyZero());
	const FVector LeanUp = Kate->GetMesh()->GetComponentTransform().TransformVectorNoScale(Hands->HitLean.RotateVector(FVector::UpVector));
	TestTrue(TEXT("And tips her toward world +X"), LeanUp.X > 0.08f);
	Hands->SetHitLean(FVector(1.f, 0.f, 0.f), 0.f);
	Hands->ComposeOutputs();
	TestTrue(TEXT("Off at alpha 0"), Hands->HitLean.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStrikePoseTargets, "Hawkeye.Animation.StrikePoseTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeStrikePoseTargets::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;

	// The light: the right fist out 70 cm at chest height over 0.1 s, back over 0.2 s.
	FHawkeyeStrikePoseClock Pose;
	Pose.Start(EHawkeyeStrikePose::Light, 0.1f);
	TestEqual(TEXT("Light: nothing at the start"), Pose.Sample().RightAlpha, 0.f);
	Pose.Advance(0.05f);
	TestEqual(TEXT("Light: half out at 0.05 s"), Pose.Sample().RightAlpha, 0.5f, 0.001f);
	Pose.Advance(0.05f);
	FHawkeyeStrikePoseSample Out = Pose.Sample();
	TestEqual(TEXT("Light: all the way out as the hit lands"), Out.RightAlpha, 1.f, 0.001f);
	TestTrue(TEXT("Light: 70 cm forward at chest height"), Out.RightHand.Equals(FVector(70.f, 8.f, 45.f), 0.01f));
	TestEqual(TEXT("Light: the left hand stays"), Out.LeftAlpha, 0.f);
	Pose.Advance(0.1f);
	TestEqual(TEXT("Light: half back at 0.2 s"), Pose.Sample().RightAlpha, 0.5f, 0.001f);
	Pose.Advance(0.11f);
	TestFalse(TEXT("Light: done at 0.3 s"), Pose.IsActive());

	// The heavy: both hands, arriving with the hit 0.6 s in over 0.25 s.
	Pose.Start(EHawkeyeStrikePose::Heavy, 0.6f);
	Pose.Advance(0.3f);
	TestEqual(TEXT("Heavy: still back at 0.3 s"), Pose.Sample().RightAlpha, 0.f);
	Pose.Advance(0.3f);
	Out = Pose.Sample();
	TestEqual(TEXT("Heavy: right out at the hit"), Out.RightAlpha, 1.f, 0.001f);
	TestEqual(TEXT("Heavy: left out at the hit"), Out.LeftAlpha, 1.f, 0.001f);
	TestTrue(TEXT("Heavy: the hands mirror"), Out.LeftHand.Equals(FVector(Out.RightHand.X, -Out.RightHand.Y, Out.RightHand.Z), 0.01f));

	// The bow sweep: the bow hand only, travelling across.
	Pose.Start(EHawkeyeStrikePose::BowSweep, 0.25f);
	Pose.Advance(0.01f);
	const FVector Early = Pose.Sample().LeftHand;
	Pose.Advance(0.24f);
	Out = Pose.Sample();
	TestEqual(TEXT("Sweep: the right hand stays"), Out.RightAlpha, 0.f);
	TestEqual(TEXT("Sweep: the bow hand is out"), Out.LeftAlpha, 1.f, 0.001f);
	TestTrue(TEXT("Sweep: it went from her left across to her right"), Early.Y < 0.f && Out.LeftHand.Y > 0.f);

	// In the anim instance the strike wins over the bow, then lets go.
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	if (!Kate)
	{
		AddError(TEXT("Failed to spawn Kate."));
		return false;
	}
	UHawkeyeBowIKAnimInstance* Hands = NewObject<UHawkeyeBowIKAnimInstance>(Kate->GetMesh());
	FHawkeyeStrikePoseClock Light;
	Light.Start(EHawkeyeStrikePose::Light, 0.1f);
	Light.Advance(0.1f);
	Hands->SetStrikePose(Light.Sample(), Kate->GetActorTransform());
	Hands->UpdateFromBow(Kate->GetBowComponent(), 0.016f);
	TestEqual(TEXT("The right arm is the strike's"), Hands->RightArmAlpha, 1.f, 0.001f);
	TestEqual(TEXT("The left arm is left alone"), Hands->LeftArmAlpha, 0.f);
	const FVector World = Kate->GetMesh()->GetComponentTransform().TransformPosition(Hands->RightHandTarget);
	const FVector Expected = Kate->GetActorTransform().TransformPosition(FVector(70.f, 8.f, 45.f));
	TestTrue(TEXT("Its target is 70 cm in front of her chest"), World.Equals(Expected, 0.1f));
	Hands->SetStrikePose(FHawkeyeStrikePoseSample(), Kate->GetActorTransform());
	Hands->UpdateFromBow(Kate->GetBowComponent(), 0.016f);
	TestEqual(TEXT("Strike over: back to the (idle) bow"), Hands->RightArmAlpha, 0.f);

	// Kate's own strikes start the poses.
	Kate->StartLightAttack();
	TestEqual(TEXT("Her light throws the right hand"), Kate->GetStrikePose().GetPose(), EHawkeyeStrikePose::Light);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSoftLockSelection, "Hawkeye.Melee.SoftLockSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSoftLockSelection::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeDepthTest;
	const FVector Origin = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;
	TArray<FVector> Candidates = {
		FVector(300.f, 0.f, 0.f),    // ahead, 300
		FVector(200.f, 250.f, 0.f),  // 51 degrees off, 320
		FVector(-100.f, 0.f, 0.f),   // behind
		FVector(450.f, 0.f, 0.f),    // past 400
		FVector(150.f, 0.f, 300.f),  // on the roof above
	};
	TestEqual(TEXT("The nearest in front within 400"), UHawkeyeMeleeRules::SelectSoftLockIndex(Origin, Forward, Candidates, 400.f, 60.f, 200.f), 0);
	Candidates[0] = FVector(-300.f, 0.f, 0.f);
	TestEqual(TEXT("Without him, the one 51 degrees off"), UHawkeyeMeleeRules::SelectSoftLockIndex(Origin, Forward, Candidates, 400.f, 60.f, 200.f), 1);
	TestEqual(TEXT("Not inside a 45 degree cone"), UHawkeyeMeleeRules::SelectSoftLockIndex(Origin, Forward, Candidates, 400.f, 45.f, 200.f), INDEX_NONE);
	TestTrue(TEXT("In front within 250"), UHawkeyeMeleeRules::IsInFrontWithin(Origin, Forward, FVector(200.f, 50.f, 0.f), 250.f, 70.f));
	TestFalse(TEXT("Not beside her"), UHawkeyeMeleeRules::IsInFrontWithin(Origin, Forward, FVector(0.f, 200.f, 0.f), 250.f, 70.f));

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(350.f, 50.f, 0.f), 180.f, EThugWeapon::Fists);
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	TestTrue(TEXT("A thug 354 cm ahead is the soft-lock target"), Kate->FindSoftLockTarget() == Thug);
	Thug->SetActorLocation(FVector(450.f, 0.f, 0.f));
	TestNull(TEXT("At 450 cm he is not"), Kate->FindSoftLockTarget());

	// A strike turns her to him over 0.1 s.
	Thug->SetActorLocation(FVector(200.f, 200.f, 0.f));
	TestTrue(TEXT("The light starts"), Kate->StartLightAttack());
	TestTrue(TEXT("She is turning"), Kate->IsSoftTurning());
	TestEqual(TEXT("Not yet round"), static_cast<float>(Kate->GetActorRotation().Yaw), 0.f, 0.01f);
	Kate->AdvanceMeleeFlow(0.05f);
	TestEqual(TEXT("Half way at 0.05 s"), static_cast<float>(Kate->GetActorRotation().Yaw), 22.5f, 0.5f);
	Kate->AdvanceMeleeFlow(0.05f);
	TestEqual(TEXT("Facing him at 0.1 s"), static_cast<float>(Kate->GetActorRotation().Yaw), 45.f, 0.5f);
	TestFalse(TEXT("The turn is over"), Kate->IsSoftTurning());
	Kate->GetMeleeComponent()->AdvanceAttack(1.f);

	// A dodge sideways keeps her facing him; one through his telegraph is a perfect dodge.
	AHawkeyeAimTestCharacter* Dodger = Cast<AHawkeyeAimTestCharacter>(Kate);
	Kate->SetActorRotation(FRotator::ZeroRotator);
	Thug->GetMeleeComponent()->StartAttack(Thug->GetMeleeAttack());
	TestTrue(TEXT("The dodge starts"), Kate->TryDodge(FVector(0.f, -1.f, 0.f)));
	TestTrue(TEXT("She turns toward him as she goes"), Kate->IsSoftTurning());
	Kate->AdvanceMeleeFlow(0.1f);
	TestEqual(TEXT("Facing him mid-dodge"), static_cast<float>(Kate->GetActorRotation().Yaw), 45.f, 0.5f);
	TestEqual(TEXT("A dodge in his wind-up is a perfect dodge"), Kate->GetPerfectDodgeCount(), 1);
	if (Dodger)
	{
		Dodger->TestTickDodge(1.f);
	}
	Thug->GetMeleeComponent()->AdvanceAttack(2.f);
	TestTrue(TEXT("A dodge with nothing coming"), Kate->TryDodge(FVector(0.f, 1.f, 0.f)));
	TestEqual(TEXT("Is not perfect"), Kate->GetPerfectDodgeCount(), 1);
	return true;
}

#endif
