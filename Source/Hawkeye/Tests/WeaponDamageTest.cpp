#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The bullets-do-not-hit-characters regression, now from the thugs' side.
 *
 * Every shot used to trace on ECC_Visibility, which the stock Pawn and CharacterMesh collision
 * profiles both ignore, so the trace passed straight through its target and hit the wall behind.
 * The player no longer carries a gun (TODO(stage2): the bow replaces it), but thugs still shoot
 * the hitscan, so these fire a real thug's weapon at real characters in a real world.
 */
namespace HawkeyeWeaponDamageTest
{
	/** A thug at the origin looking down +X, with no spread and the fire clock under test control. */
	static AThugCharacter* SpawnShootingThug(const FHawkeyeTestWorld& TestWorld, double Now)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(
			AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (!Thug)
		{
			return nullptr;
		}

		if (UWeaponComponent* Weapon = Thug->GetWeaponComponent())
		{
			// No spread: the test asserts that a shot aimed at someone hits them, not that the
			// cone maths is fair.
			Weapon->HipSpreadDegrees = 0.f;
			Weapon->AimSpreadDegrees = 0.f;
			Weapon->SetTestTimeSeconds(Now);
		}

		return Thug;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponThugShotHurtsPlayer, "Hawkeye.Weapon.ThugShotHurtsPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponThugShotHurtsPlayer::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;

	AThugCharacter* Thug = HawkeyeWeaponDamageTest::SpawnShootingThug(TestWorld, 0.0);
	AHawkeyeCharacter* Player = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeCharacter::StaticClass(), FVector(300.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f)));
	if (!TestNotNull(TEXT("Thug spawned"), Thug) || !TestNotNull(TEXT("Player spawned 3 metres in front"), Player))
	{
		return false;
	}

	UWeaponComponent* Weapon = Thug->GetWeaponComponent();
	UHealthComponent* PlayerHealth = Player->GetHealthComponent();
	if (!TestNotNull(TEXT("Player has health"), PlayerHealth))
	{
		return false;
	}

	const float StartingHealth = PlayerHealth->GetCurrentHealth();
	TestTrue(TEXT("The shot goes out"), Weapon->Fire());

	TestEqual(TEXT("The player lost exactly one thug shot of health"),
		PlayerHealth->GetCurrentHealth(), StartingHealth - Weapon->Damage);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponShotDamagesThug, "Hawkeye.Weapon.ShotDamagesThug",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponShotDamagesThug::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;

	// A thug's capsule and mesh have to block the Weapon channel as well as the player's do.
	AThugCharacter* Shooter = HawkeyeWeaponDamageTest::SpawnShootingThug(TestWorld, 0.0);
	AThugCharacter* Target = Cast<AThugCharacter>(TestWorld.SpawnActor(
		AThugCharacter::StaticClass(), FVector(300.f, 0.f, 0.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Shooter spawned"), Shooter) || !TestNotNull(TEXT("Target spawned"), Target))
	{
		return false;
	}

	UWeaponComponent* Weapon = Shooter->GetWeaponComponent();
	UHealthComponent* TargetHealth = Target->GetHealthComponent();

	const float StartingHealth = TargetHealth->GetCurrentHealth();
	TestTrue(TEXT("The shot goes out"), Weapon->Fire());
	TestEqual(TEXT("The target thug lost exactly one shot of health"),
		TargetHealth->GetCurrentHealth(), StartingHealth - Weapon->Damage);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponHitDelegateFires, "Hawkeye.Weapon.HitDelegateFires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponHitDelegateFires::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;

	AThugCharacter* Thug = HawkeyeWeaponDamageTest::SpawnShootingThug(TestWorld, 0.0);
	AHawkeyeCharacter* Player = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeCharacter::StaticClass(), FVector(300.f, 0.f, 0.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Thug spawned"), Thug) || !TestNotNull(TEXT("Player spawned"), Player))
	{
		return false;
	}

	UWeaponComponent* Weapon = Thug->GetWeaponComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Weapon->OnHit.AddDynamic(Listener, &UHawkeyeTestListener::HandleWeaponHit);

	Weapon->Fire();

	// The HUD's hit marker hangs off this, so a hit on a damageable actor has to report itself.
	TestEqual(TEXT("OnHit fired once"), Listener->WeaponHitCount, 1);
	TestEqual(TEXT("With the player as the hit actor"),
		static_cast<AActor*>(Listener->LastWeaponHitActor.Get()), static_cast<AActor*>(Player));
	TestEqual(TEXT("And the damage that was dealt"), Listener->LastWeaponHitDamage, Weapon->Damage);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponMissDamagesNothing, "Hawkeye.Weapon.MissDamagesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponMissDamagesNothing::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;

	AThugCharacter* Thug = HawkeyeWeaponDamageTest::SpawnShootingThug(TestWorld, 0.0);
	// Behind the thug: the trace runs down +X and the player is at -X.
	AHawkeyeCharacter* Player = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeCharacter::StaticClass(), FVector(-300.f, 0.f, 0.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Thug spawned"), Thug) || !TestNotNull(TEXT("Player spawned"), Player))
	{
		return false;
	}

	UWeaponComponent* Weapon = Thug->GetWeaponComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Weapon->OnHit.AddDynamic(Listener, &UHawkeyeTestListener::HandleWeaponHit);

	const float StartingHealth = Player->GetHealthComponent()->GetCurrentHealth();
	Weapon->Fire();

	TestEqual(TEXT("A player behind the thug takes nothing"),
		Player->GetHealthComponent()->GetCurrentHealth(), StartingHealth);
	TestEqual(TEXT("And no hit marker is asked for"), Listener->WeaponHitCount, 0);

	return true;
}

/**
 * Third person put the camera metres behind the body, so a punch swept from the camera would end
 * before it reached anyone. The swing starts at the pawn's eyes instead; this is that rule.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponPunchReachesFromTheBody, "Hawkeye.Weapon.PunchReachesFromTheBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponPunchReachesFromTheBody::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;

	AHawkeyeCharacter* Player = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(
		AThugCharacter::StaticClass(), FVector(90.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f)));
	UWeaponComponent* Weapon = Player ? Player->GetWeaponComponent() : nullptr;
	if (!TestNotNull(TEXT("Player spawned"), Player) || !TestNotNull(TEXT("Thug spawned in reach"), Thug)
		|| !TestNotNull(TEXT("Player has a weapon component"), Weapon))
	{
		return false;
	}

	TestTrue(TEXT("The player starts on Hands"), Weapon->IsMelee());

	const float StartingHealth = Thug->GetHealthComponent()->GetCurrentHealth();
	Weapon->SetTestTimeSeconds(100.0);
	TestTrue(TEXT("The punch goes out"), Weapon->Fire());
	TestEqual(TEXT("And lands on the thug 90 cm in front"),
		Thug->GetHealthComponent()->GetCurrentHealth(), StartingHealth - Weapon->MeleeDamage);

	return true;
}

#endif
