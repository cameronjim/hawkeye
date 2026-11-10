#include "Combat/WeaponComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeWeaponTest
{
	/** A pistol with the default stats and a test clock, so the fire rate is deterministic. */
	static UWeaponComponent* MakeWeapon()
	{
		UWeaponComponent* Weapon = NewObject<UWeaponComponent>();
		Weapon->SetTestTimeSeconds(0.0);
		return Weapon;
	}

	/** Moves the test clock far enough forward that the next shot is always allowed. */
	static void AdvanceClock(UWeaponComponent* Weapon, double& Now, double Seconds)
	{
		Now += Seconds;
		Weapon->SetTestTimeSeconds(Now);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponDefaultStats, "Hawkeye.Weapon.DefaultStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponDefaultStats::RunTest(const FString& Parameters)
{
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();

	TestEqual(TEXT("Magazine size"), Weapon->MagazineSize, 12);
	TestEqual(TEXT("Starting magazine"), Weapon->CurrentAmmo, 12);
	TestEqual(TEXT("Reserve"), Weapon->ReserveAmmo, 24);
	TestEqual(TEXT("Body damage"), Weapon->Damage, 34.f);

	// Three body shots or one headshot kill a 100 HP guard.
	TestTrue(TEXT("Three body shots kill a 100 HP guard"), Weapon->ComputeDamageForHit(FName(TEXT("spine_01"))) * 3.f >= 100.f);
	TestTrue(TEXT("Two body shots do not"), Weapon->ComputeDamageForHit(FName(TEXT("spine_01"))) * 2.f < 100.f);
	TestTrue(TEXT("One headshot kills"), Weapon->ComputeDamageForHit(FName(TEXT("head"))) >= 100.f);
	TestEqual(TEXT("neck_01 counts as a head"), Weapon->ComputeDamageForHit(FName(TEXT("neck_01"))), 34.f * 3.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponFireDecrementsMagazine, "Hawkeye.Weapon.FireDecrementsMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponFireDecrementsMagazine::RunTest(const FString& Parameters)
{
	double Now = 0.0;
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Weapon->OnAmmoChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleAmmoChanged);

	TestTrue(TEXT("First shot goes out"), Weapon->Fire());
	TestEqual(TEXT("Magazine decremented"), Weapon->CurrentAmmo, 11);
	TestEqual(TEXT("OnAmmoChanged fired once"), Listener->AmmoChangedCount, 1);
	TestEqual(TEXT("Magazine reported"), Listener->LastMagazine, 11);
	TestEqual(TEXT("Reserve untouched"), Listener->LastReserve, 24);

	HawkeyeWeaponTest::AdvanceClock(Weapon, Now, 1.0);
	TestTrue(TEXT("Second shot goes out"), Weapon->Fire());
	TestEqual(TEXT("Magazine decremented again"), Weapon->CurrentAmmo, 10);
	TestEqual(TEXT("OnAmmoChanged counted twice"), Listener->AmmoChangedCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponFireRateBlocksRapidFire, "Hawkeye.Weapon.FireRateBlocksRapidFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponFireRateBlocksRapidFire::RunTest(const FString& Parameters)
{
	double Now = 0.0;
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();
	Weapon->FireRate = 600.f; // 0.1 s between shots.

	TestTrue(TEXT("First shot goes out"), Weapon->Fire());
	TestFalse(TEXT("Same instant is blocked"), Weapon->Fire());
	TestEqual(TEXT("No ammo spent on the blocked shot"), Weapon->CurrentAmmo, 11);

	HawkeyeWeaponTest::AdvanceClock(Weapon, Now, 0.05);
	TestFalse(TEXT("Half the interval is still blocked"), Weapon->Fire());

	HawkeyeWeaponTest::AdvanceClock(Weapon, Now, 0.05);
	TestTrue(TEXT("A full interval allows the next shot"), Weapon->Fire());
	TestEqual(TEXT("Two rounds spent"), Weapon->CurrentAmmo, 10);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponFireAtZeroAmmoClicks, "Hawkeye.Weapon.FireAtZeroAmmoClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponFireAtZeroAmmoClicks::RunTest(const FString& Parameters)
{
	double Now = 0.0;
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();
	Weapon->CurrentAmmo = 0;

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Weapon->OnAmmoChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleAmmoChanged);
	Weapon->OnEmptyClick.AddDynamic(Listener, &UHawkeyeTestListener::HandleEmptyClick);

	HawkeyeWeaponTest::AdvanceClock(Weapon, Now, 10.0);
	TestFalse(TEXT("Fire refused"), Weapon->Fire());
	TestEqual(TEXT("Magazine stays at zero"), Weapon->CurrentAmmo, 0);
	TestEqual(TEXT("No ammo change broadcast"), Listener->AmmoChangedCount, 0);
	TestEqual(TEXT("OnEmptyClick fired"), Listener->EmptyClickCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponReloadMath, "Hawkeye.Weapon.ReloadMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponReloadMath::RunTest(const FString& Parameters)
{
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();
	Weapon->CurrentAmmo = 4;
	Weapon->ReserveAmmo = 24;

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Weapon->OnAmmoChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleAmmoChanged);

	TestTrue(TEXT("Reload starts"), Weapon->Reload());
	TestTrue(TEXT("Weapon is reloading"), Weapon->IsReloading());
	TestFalse(TEXT("Firing during a reload is blocked"), Weapon->Fire());

	Weapon->CompleteReloadNow();

	TestFalse(TEXT("Reload finished"), Weapon->IsReloading());
	TestEqual(TEXT("Magazine full"), Weapon->CurrentAmmo, 12);
	TestEqual(TEXT("Reserve reduced by the 8 rounds moved"), Weapon->ReserveAmmo, 16);
	TestEqual(TEXT("One OnAmmoChanged for the reload"), Listener->AmmoChangedCount, 1);

	// Partial reserve: only what is left moves.
	Weapon->CurrentAmmo = 0;
	Weapon->ReserveAmmo = 5;
	Weapon->Reload();
	Weapon->CompleteReloadNow();

	TestEqual(TEXT("Only the remaining reserve is loaded"), Weapon->CurrentAmmo, 5);
	TestEqual(TEXT("Reserve empty"), Weapon->ReserveAmmo, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponReloadRefused, "Hawkeye.Weapon.ReloadRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponReloadRefused::RunTest(const FString& Parameters)
{
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Weapon->OnAmmoChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleAmmoChanged);

	TestFalse(TEXT("Reload with a full magazine is a no-op"), Weapon->Reload());

	Weapon->CurrentAmmo = 2;
	Weapon->ReserveAmmo = 0;
	TestFalse(TEXT("Reload with an empty reserve is a no-op"), Weapon->Reload());

	Weapon->ReserveAmmo = 10;
	TestTrue(TEXT("Reload starts"), Weapon->Reload());
	TestFalse(TEXT("Reload while already reloading is a no-op"), Weapon->Reload());

	TestEqual(TEXT("No ammo moved by any refused reload"), Listener->AmmoChangedCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponCancelReloadKeepsAmmo, "Hawkeye.Weapon.CancelReloadKeepsAmmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponCancelReloadKeepsAmmo::RunTest(const FString& Parameters)
{
	double Now = 0.0;
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();
	Weapon->CurrentAmmo = 3;

	Weapon->Reload();
	Weapon->CancelReload();

	TestFalse(TEXT("No longer reloading"), Weapon->IsReloading());
	TestEqual(TEXT("Magazine unchanged"), Weapon->CurrentAmmo, 3);
	TestEqual(TEXT("Reserve unchanged"), Weapon->ReserveAmmo, 24);

	HawkeyeWeaponTest::AdvanceClock(Weapon, Now, 1.0);
	TestTrue(TEXT("Firing works again after cancelling"), Weapon->Fire());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponSpreadByAimState, "Hawkeye.Weapon.SpreadByAimState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponSpreadByAimState::RunTest(const FString& Parameters)
{
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();

	TestEqual(TEXT("Hip spread default"), Weapon->HipSpreadDegrees, 2.5f);
	TestEqual(TEXT("Aim spread default"), Weapon->AimSpreadDegrees, 0.5f);

	TestFalse(TEXT("Starts hip-firing"), Weapon->IsAiming());
	TestEqual(TEXT("Hip-firing uses the wide cone"), Weapon->GetCurrentSpreadDegrees(), Weapon->HipSpreadDegrees);

	Weapon->SetAiming(true);
	TestTrue(TEXT("Aiming with a weapon takes"), Weapon->IsAiming());
	TestEqual(TEXT("Aiming uses the tight cone"), Weapon->GetCurrentSpreadDegrees(), Weapon->AimSpreadDegrees);

	Weapon->SetAiming(false);
	TestFalse(TEXT("Lowering the sights clears the flag"), Weapon->IsAiming());
	TestEqual(TEXT("And the wide cone is back"), Weapon->GetCurrentSpreadDegrees(), Weapon->HipSpreadDegrees);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponUnarmedAimIsNoOp, "Hawkeye.Weapon.UnarmedAimIsNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponUnarmedAimIsNoOp::RunTest(const FString& Parameters)
{
	UWeaponComponent* Weapon = HawkeyeWeaponTest::MakeWeapon();
	Weapon->bHasWeapon = false;

	Weapon->SetAiming(true);
	TestFalse(TEXT("Aiming empty-handed does nothing"), Weapon->IsAiming());
	TestEqual(TEXT("Spread stays at the hip value"), Weapon->GetCurrentSpreadDegrees(), Weapon->HipSpreadDegrees);

	// Picking a pistol up mid-aim must not inherit a stale aimed state.
	Weapon->GiveWeapon(12, 24);
	TestFalse(TEXT("Still not aiming after being armed"), Weapon->IsAiming());

	Weapon->SetAiming(true);
	TestTrue(TEXT("Aiming works once armed"), Weapon->IsAiming());

	Weapon->RemoveWeapon();
	TestFalse(TEXT("Being disarmed drops the aim"), Weapon->IsAiming());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeWeaponConeSpreadStaysInsideTheCone, "Hawkeye.Weapon.ConeSpreadStaysInsideTheCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeWeaponConeSpreadStaysInsideTheCone::RunTest(const FString& Parameters)
{
	const FVector Forward(1.f, 0.f, 0.f);

	// Zero spread is the identity, so a weapon tuned to be perfectly accurate traces dead ahead.
	FRandomStream Stream(1234);
	TestTrue(TEXT("Zero spread returns the direction untouched"),
		UWeaponComponent::ApplyConeSpread(Forward, 0.f, Stream).Equals(Forward, KINDA_SMALL_NUMBER));

	const float SpreadDegrees = 2.5f;
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(SpreadDegrees)) - KINDA_SMALL_NUMBER;

	float WidestAngle = 0.f;
	for (int32 Shot = 0; Shot < 256; ++Shot)
	{
		const FVector Direction = UWeaponComponent::ApplyConeSpread(Forward, SpreadDegrees, Stream);
		TestTrue(TEXT("The spread direction is normalised"), FMath::IsNearlyEqual(Direction.Size(), 1.f, KINDA_SMALL_NUMBER));

		const float Dot = FVector::DotProduct(Direction, Forward);
		TestTrue(TEXT("Every shot lands inside the cone"), Dot >= CosLimit);
		WidestAngle = FMath::Max(WidestAngle, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f))));
	}

	TestTrue(TEXT("And the cone is actually used, not collapsed to the centre"), WidestAngle > SpreadDegrees * 0.5f);

	// Same seed, same shots: the trace is reproducible for a test that asserts on a hit.
	FRandomStream First(99);
	FRandomStream Second(99);
	TestTrue(TEXT("The same seed gives the same shot"),
		UWeaponComponent::ApplyConeSpread(Forward, SpreadDegrees, First)
			.Equals(UWeaponComponent::ApplyConeSpread(Forward, SpreadDegrees, Second), KINDA_SMALL_NUMBER));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
