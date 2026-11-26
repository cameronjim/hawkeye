#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Where a raised gunner's pistol points (claude-docs/animation.md, "Props in the hand"): at the chest of whoever he aims at,
 * to within 5 degrees, wherever she stands in front of him (6 to 8 m, 30 degrees to either side, 1.5 m higher or lower).
 * The pistol's own pose without a skeleton (the real hands are checked on the animated body in Hawkeye.Screenshot.Bodies,
 * which asserts the same angle on the real hand and the arms' reach).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGunnerAimsAtTarget, "Hawkeye.Gunner.MuzzlePointsAtTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGunnerAimsAtTarget::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Name;
		float YawDegrees;
		float RangeCm;
		float RiseCm;
	};
	const FCase Cases[] = {
		{TEXT("in front at 7 m"), 0.f, 700.f, 0.f},
		{TEXT("30 degrees left at 6 m"), -30.f, 600.f, 0.f},
		{TEXT("30 degrees right at 8 m"), 30.f, 800.f, 0.f},
		{TEXT("1.5 m above at 7 m"), 0.f, 700.f, 150.f},
		{TEXT("1.5 m below at 7 m"), 0.f, 700.f, -150.f},
	};
	for (const FCase& Case : Cases)
	{
		FHawkeyeTestWorld TestWorld;
		// He faces her, as the AI turns him once he stops to shoot.
		const FVector Face = FRotator(0.f, Case.YawDegrees, 0.f).Vector();
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), Face.Rotation()));
		if (!TestNotNull(TEXT("Gunner spawned"), Thug) || !TestTrue(TEXT("He is a gunner"), Thug->IsGunner()))
		{
			return false;
		}
		const FVector Chest = Thug->GetActorLocation() + Face * Case.RangeCm + FVector(0.f, 0.f, Case.RiseCm);
		Thug->SetWeaponRaised(true, Chest);
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Thug->UpdateGunForTest(1.f / 30.f);
		}
		const UStaticMeshComponent* Pistol = Thug->GetPistolComponent();
		if (!TestNotNull(TEXT("He has a pistol"), Pistol))
		{
			return false;
		}
		if (Thug->HasPistolModel() && Pistol->GetStaticMesh())
		{
			// The imported model's frame is the game's: X the muzzle, Z up with the grip hanging under the slide. (Imported rolled a
			// quarter turn once, the grip along -Y: the gunner's pistol then lay on its side, hidden from the side by his hand.)
			const FBox Bounds = Pistol->GetStaticMesh()->GetBoundingBox();
			TestTrue(FString::Printf(TEXT("The pistol model's grip hangs along -Z (bounds %s to %s)"), *Bounds.Min.ToCompactString(), *Bounds.Max.ToCompactString()),
				Bounds.Min.Z < -10.f && Bounds.GetSize().Y < 6.f && Bounds.GetSize().X > 15.f);
		}
		const FVector Muzzle = Thug->GetGlintLocation();
		const FVector Bore = Pistol->GetComponentTransform().TransformVectorNoScale(FVector::ForwardVector);
		const FVector ToChest = (Chest - Muzzle).GetSafeNormal();
		const double Degrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Bore, ToChest), -1.0, 1.0)));
		TestTrue(FString::Printf(TEXT("%s: the muzzle points at her chest within 5 degrees (%.1f)"), Case.Name, Degrees), Degrees < 5.0);
	}
	return true;
}

#endif
