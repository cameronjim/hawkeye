#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The bow rules from claude-docs/gameplay-semantics.md, "bow and arrows": draw is a hold from 0
 * to FullDrawSeconds, a release below 25% cancels, power scales speed (40% to 100% of 6000 cm/s),
 * damage (40% to 100%) and spread (4 degrees to 0.5), a release within 0.1 s of full draw adds 25%,
 * head bones multiply, arrows are projectiles that stick and can be recovered. Definitions are
 * built in code.
 */
namespace HawkeyeBowTest
{
	static UBowDefinition* MakeKateBow(UObject* Outer)
	{
		UBowDefinition* Bow = NewObject<UBowDefinition>(Outer);
		Bow->FullDrawSeconds = 0.8f;
		return Bow;
	}

	static UArrowDefinition* MakeStandard(UObject* Outer, bool bRecoverable = true)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->Slot = 1;
		Arrow->Cap = 30;
		Arrow->Damage = 40.f;
		Arrow->bRecoverable = bRecoverable;
		Arrow->ProjectileClass = AArrowProjectile::StaticClass();
		return Arrow;
	}

	/** Kate with DA_Bow_Kate's numbers and Arrows standard arrows, on a test clock at 10 s. */
	static AHawkeyeCharacter* SpawnArcher(const FHawkeyeTestWorld& TestWorld, int32 Arrows)
	{
		AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
		UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr;
		if (!Inventory || !Kate->GetBowComponent())
		{
			return nullptr;
		}
		Inventory->GiveBow(MakeKateBow(Inventory));
		Inventory->AddArrows(MakeStandard(Inventory), Arrows);
		Kate->GetBowComponent()->SetTestTimeSeconds(10.0);
		return Kate;
	}

	static int32 CountArrows(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			Count += It->IsActorBeingDestroyed() ? 0 : 1;
		}
		return Count;
	}

	static AArrowProjectile* SpawnArrow(const FHawkeyeTestWorld& TestWorld, const FVector& Location,
		UArrowDefinition* Definition, UBowDefinition* Bow, AActor* Shooter, UBowComponent* Source, float Damage)
	{
		AArrowProjectile* Arrow = Cast<AArrowProjectile>(
			TestWorld.SpawnActor(AArrowProjectile::StaticClass(), Location, FRotator::ZeroRotator));
		if (Arrow)
		{
			Arrow->InitArrow(Definition, Bow, Damage, Shooter, Source);
		}
		return Arrow;
	}

	/** Steps the arrow's flight until it sticks or MaxSeconds pass. */
	static void Fly(AArrowProjectile* Arrow, float MaxSeconds)
	{
		for (float Elapsed = 0.f; Arrow && !Arrow->IsStuck() && Elapsed < MaxSeconds; Elapsed += 0.01f)
		{
			Arrow->AdvanceFlight(0.01f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowDrawFractionAndCancel, "Hawkeye.Bow.DrawFractionAndCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowDrawFractionAndCancel::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBowTest;
	const UBowDefinition* Bow = MakeKateBow(GetTransientPackage());
	TestEqual(TEXT("Half the draw time is half drawn"), Bow->ComputeDrawFraction(0.4f), 0.5f);
	TestEqual(TEXT("Held past full draw stays full"), Bow->ComputeDrawFraction(3.f), 1.f);
	TestEqual(TEXT("Nothing held, nothing drawn"), Bow->ComputeDrawFraction(0.f), 0.f);
	TestTrue(TEXT("24% cancels"), Bow->IsBelowMinDraw(0.24f));
	TestFalse(TEXT("25% fires"), Bow->IsBelowMinDraw(0.25f));

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnArcher(TestWorld, 5);
	if (!Kate)
	{
		AddError(TEXT("Could not spawn an archer."));
		return false;
	}
	UBowComponent* BowComponent = Kate->GetBowComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	BowComponent->OnDrawChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleDrawChanged);
	BowComponent->OnArrowFired.AddDynamic(Listener, &UHawkeyeTestListener::HandleArrowFired);

	TestTrue(TEXT("The draw starts"), BowComponent->StartDraw());
	TestTrue(TEXT("Drawing"), BowComponent->IsDrawing());
	TestTrue(TEXT("Drawing forces the aim"), Kate->IsAiming());
	TestFalse(TEXT("A second press while drawing does nothing"), BowComponent->StartDraw());
	BowComponent->SetTestTimeSeconds(10.1);
	TestEqual(TEXT("0.1 s of 0.8 is an eighth"), BowComponent->GetDrawFraction(), 0.125f, 1e-4f);

	TestFalse(TEXT("Released at 12.5% it cancels"), BowComponent->ReleaseDraw());
	TestFalse(TEXT("The string is let down"), BowComponent->IsDrawing());
	TestFalse(TEXT("And the forced aim ends"), Kate->IsAiming());
	TestEqual(TEXT("No arrow spent"), Kate->GetInventoryComponent()->GetArrowCount(1), 5);
	TestEqual(TEXT("None fired"), Listener->ArrowFiredCount, 0);
	TestEqual(TEXT("None spawned"), CountArrows(TestWorld.Get()), 0);
	TestEqual(TEXT("OnDrawChanged ends on zero"), Listener->LastDrawFraction, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowSpeedAndSpreadByDraw, "Hawkeye.Bow.SpeedSpreadAndDamageByDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowSpeedAndSpreadByDraw::RunTest(const FString& Parameters)
{
	const UBowDefinition* Bow = HawkeyeBowTest::MakeKateBow(GetTransientPackage());
	TestEqual(TEXT("Full draw flies at 6000 cm/s"), Bow->ComputeSpeed(1.f), 6000.f, 0.01f);
	TestEqual(TEXT("Zero draw at 40% of that"), Bow->ComputeSpeed(0.f), 2400.f, 0.01f);
	TestEqual(TEXT("Half draw halfway"), Bow->ComputeSpeed(0.5f), 4200.f, 0.01f);
	TestEqual(TEXT("Full draw spread 0.5 degrees"), Bow->ComputeSpread(1.f), 0.5f, 0.01f);
	TestEqual(TEXT("Zero draw spread 4 degrees"), Bow->ComputeSpread(0.f), 4.f, 0.01f);
	TestEqual(TEXT("Half draw 2.25 degrees"), Bow->ComputeSpread(0.5f), 2.25f, 0.01f);
	TestEqual(TEXT("Full draw does the arrow's damage"), Bow->ComputeReleaseDamage(40.f, 1.f, false), 40.f, 0.01f);
	TestEqual(TEXT("Zero draw 40% of it"), Bow->ComputeReleaseDamage(40.f, 0.f, false), 16.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowPerfectWindow, "Hawkeye.Bow.PerfectWindowBonus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowPerfectWindow::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBowTest;
	const UBowDefinition* Bow = MakeKateBow(GetTransientPackage());
	TestFalse(TEXT("Before full draw is not perfect"), Bow->IsPerfectRelease(0.79f));
	TestTrue(TEXT("At full draw it is"), Bow->IsPerfectRelease(0.8f));
	TestTrue(TEXT("0.1 s after, still"), Bow->IsPerfectRelease(0.9f));
	TestFalse(TEXT("0.15 s after, not any more"), Bow->IsPerfectRelease(0.95f));
	TestEqual(TEXT("A perfect release adds 25%"), Bow->ComputeReleaseDamage(40.f, 1.f, true), 50.f);

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnArcher(TestWorld, 2);
	if (!Kate)
	{
		AddError(TEXT("Could not spawn an archer."));
		return false;
	}
	UBowComponent* BowComponent = Kate->GetBowComponent();
	BowComponent->StartDraw();
	BowComponent->SetTestTimeSeconds(10.85);
	TestTrue(TEXT("The HUD sees the window"), BowComponent->IsInPerfectWindow());
	TestTrue(TEXT("A perfect release fires"), BowComponent->ReleaseDraw());
	AArrowProjectile* Arrow = nullptr;
	for (TActorIterator<AArrowProjectile> It(TestWorld.Get()); It; ++It)
	{
		Arrow = *It;
	}
	TestTrue(TEXT("The arrow carries the bonus"), Arrow && FMath::IsNearlyEqual(Arrow->GetDamage(), 50.f));

	BowComponent->SetTestTimeSeconds(20.0);
	BowComponent->StartDraw();
	BowComponent->SetTestTimeSeconds(21.5);
	TestFalse(TEXT("Held long past full draw is not perfect"), BowComponent->IsInPerfectWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowArrowCount, "Hawkeye.Bow.ArrowCountAndRefusalAtZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowArrowCount::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBowTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnArcher(TestWorld, 1);
	if (!Kate)
	{
		AddError(TEXT("Could not spawn an archer."));
		return false;
	}
	UBowComponent* BowComponent = Kate->GetBowComponent();
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	BowComponent->OnArrowFired.AddDynamic(Listener, &UHawkeyeTestListener::HandleArrowFired);

	BowComponent->StartDraw();
	BowComponent->SetTestTimeSeconds(11.5);
	TestTrue(TEXT("A full draw fires"), BowComponent->ReleaseDraw());
	TestEqual(TEXT("The last arrow is spent"), Inventory->GetArrowCount(1), 0);
	TestEqual(TEXT("OnArrowFired once"), Listener->ArrowFiredCount, 1);
	TestEqual(TEXT("One projectile in the world"), CountArrows(TestWorld.Get()), 1);
	for (TActorIterator<AArrowProjectile> It(TestWorld.Get()); It; ++It)
	{
		TestTrue(TEXT("Flying"), It->IsInFlight());
		TestEqual(TEXT("At full-draw speed"), static_cast<float>(It->GetProjectileMovement()->Velocity.Size()), 6000.f, 1.f);
	}

	BowComponent->SetTestTimeSeconds(20.0);
	BowComponent->StartDraw();
	BowComponent->SetTestTimeSeconds(21.5);
	TestFalse(TEXT("With the quiver empty the release is refused"), BowComponent->ReleaseDraw());
	TestEqual(TEXT("Still none"), Inventory->GetArrowCount(1), 0);
	TestEqual(TEXT("Nothing fired"), Listener->ArrowFiredCount, 1);
	TestEqual(TEXT("Nothing spawned"), CountArrows(TestWorld.Get()), 1);

	Inventory->GiveBow(nullptr);
	TestFalse(TEXT("Without a bow there is no draw"), BowComponent->StartDraw());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowHeadshotByBone, "Hawkeye.Bow.HeadshotMultiplierByBone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowHeadshotByBone::RunTest(const FString& Parameters)
{
	const UBowDefinition* Bow = HawkeyeBowTest::MakeKateBow(GetTransientPackage());
	TestEqual(TEXT("head triples"), Bow->ApplyHeadshot(40.f, FName(TEXT("head"))), 120.f);
	TestEqual(TEXT("neck_01 counts as the head"), Bow->ApplyHeadshot(40.f, FName(TEXT("neck_01"))), 120.f);
	TestEqual(TEXT("spine_03 does not"), Bow->ApplyHeadshot(40.f, FName(TEXT("spine_03"))), 40.f);
	TestEqual(TEXT("No bone, no multiplier"), Bow->ApplyHeadshot(40.f, NAME_None), 40.f);

	// A 100 HP thug: one full-draw headshot, or three body arrows.
	UHealthComponent* Thug = NewObject<UHealthComponent>();
	Thug->SetMaxHealth(100.f, true);
	Thug->ApplyDamage(Bow->ApplyHeadshot(40.f, FName(TEXT("head"))));
	TestTrue(TEXT("One headshot kills"), Thug->IsDead());
	UHealthComponent* Other = NewObject<UHealthComponent>();
	Other->SetMaxHealth(100.f, true);
	Other->ApplyDamage(40.f);
	Other->ApplyDamage(40.f);
	TestTrue(TEXT("Two body arrows do not"), Other->IsAlive());
	Other->ApplyDamage(40.f);
	TestTrue(TEXT("The third does"), Other->IsDead());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowArrowSticks, "Hawkeye.Bow.ProjectileSticksOnHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowArrowSticks::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBowTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(600.f, 0.f, 100.f), FRotator::ZeroRotator));
	UArrowDefinition* Definition = MakeStandard(GetTransientPackage());
	AArrowProjectile* Arrow = SpawnArrow(TestWorld, FVector(0.f, 0.f, 100.f), Definition, nullptr, nullptr, nullptr, 40.f);
	if (!Wall || !Arrow)
	{
		AddError(TEXT("Could not spawn the wall or the arrow."));
		return false;
	}
	Wall->SetExtent(FVector(20.f, 300.f, 300.f));

	Arrow->LaunchWithVelocity(FVector(6000.f, 0.f, 0.f));
	Arrow->AdvanceFlight(0.02f);
	TestTrue(TEXT("It falls as it flies"), Arrow->GetProjectileMovement()->Velocity.Z < 0.f);
	TestTrue(TEXT("Nose along the velocity"), Arrow->GetActorForwardVector().Equals(
		Arrow->GetProjectileMovement()->Velocity.GetSafeNormal(), 1e-3f));
	Fly(Arrow, 1.f);

	TestTrue(TEXT("It stuck"), Arrow->IsStuck());
	TestFalse(TEXT("And stopped flying"), Arrow->IsInFlight());
	TestTrue(TEXT("In the wall"), Arrow->GetStuckInActor() == Wall);
	TestTrue(TEXT("Attached to it"), Arrow->GetAttachParentActor() == Wall);
	TestEqual(TEXT("Tip sunk past the face"), static_cast<float>(Arrow->GetActorLocation().X), 580.f + Arrow->EmbedDepth, 3.f);
	TestFalse(TEXT("Movement off"), Arrow->GetProjectileMovement()->IsActive());
	TestEqual(TEXT("It stays 30 s"), Arrow->GetLifeSpan(), 30.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowRecovery, "Hawkeye.Bow.RecoveryAddsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowRecovery::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBowTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnArcher(TestWorld, 10);
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(1000.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!Kate || !Wall)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	Wall->SetExtent(FVector(20.f, 300.f, 300.f));
	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	UArrowDefinition* Standard = Inventory->GetArrowSlot(1).Arrow;

	AArrowProjectile* Arrow = SpawnArrow(TestWorld, FVector(700.f, 0.f, 100.f), Standard, nullptr, nullptr, nullptr, 40.f);
	AArrowProjectile* Lost = SpawnArrow(TestWorld, FVector(700.f, 50.f, 100.f), MakeStandard(GetTransientPackage(), false),
		nullptr, nullptr, nullptr, 40.f);
	if (!Arrow || !Lost)
	{
		AddError(TEXT("Could not spawn the arrows."));
		return false;
	}
	Arrow->LaunchWithVelocity(FVector(6000.f, 0.f, 0.f));
	Lost->LaunchWithVelocity(FVector(6000.f, 0.f, 0.f));
	Fly(Arrow, 1.f);
	Fly(Lost, 1.f);
	TestTrue(TEXT("Both stuck"), Arrow->IsStuck() && Lost->IsStuck());

	TestFalse(TEXT("Nothing is picked up from across the street"), Arrow->TryRecoverBy(Kate));
	Kate->SetActorLocation(Arrow->GetActorLocation() - FVector(120.f, 0.f, 0.f));
	TestFalse(TEXT("An unrecoverable arrow stays where it is"), Lost->TryRecoverBy(Kate));
	TestTrue(TEXT("Within 150 cm the arrow comes back"), Arrow->TryRecoverBy(Kate));
	TestEqual(TEXT("One back in slot 1"), Inventory->GetArrowCount(1), 11);
	TestTrue(TEXT("And it is gone from the wall"), Arrow->IsActorBeingDestroyed() || !IsValid(Arrow));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBowArrowHitsThug, "Hawkeye.Bow.ArrowDamagesAndStaggersThug",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBowArrowHitsThug::RunTest(const FString& Parameters)
{
	using namespace HawkeyeBowTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnArcher(TestWorld, 10);
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(800.f, 0.f, 100.f), FRotator(0.f, 180.f, 0.f)));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UBowComponent* BowComponent = Kate->GetBowComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	BowComponent->OnHit.AddDynamic(Listener, &UHawkeyeTestListener::HandleArrowHit);

	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	AArrowProjectile* Arrow = SpawnArrow(TestWorld, FVector(300.f, 0.f, 130.f), Inventory->GetArrowSlot(1).Arrow,
		Inventory->GetBow(), Kate, BowComponent, 40.f);
	if (!Arrow)
	{
		AddError(TEXT("Could not spawn the arrow."));
		return false;
	}
	Arrow->LaunchWithVelocity(FVector(6000.f, 0.f, 0.f));
	Fly(Arrow, 1.f);

	TestTrue(TEXT("It stuck in the thug"), Arrow->IsStuck() && Arrow->GetStuckInActor() == Thug);
	TestEqual(TEXT("40 off his 100"), Thug->GetHealthComponent()->GetCurrentHealth(), 60.f);
	TestTrue(TEXT("He staggers"), Thug->IsStaggered());
	TestEqual(TEXT("The bow heard the hit"), Listener->ArrowHitCount, 1);
	TestEqual(TEXT("With its damage"), Listener->LastArrowHitDamage, 40.f);
	TestFalse(TEXT("A capsule is not a head"), Listener->bLastArrowHitHeadshot);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
