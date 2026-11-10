#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The melee assist (claude-docs/gameplay-semantics.md, "Melee assist"): the pick down the stick, the gap
 * close, the forgiveness at the hit, and keeping her eyes on him through the wind-up.
 */
namespace HawkeyeMeleeAssistTest
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeAssistRules, "Hawkeye.Melee.AssistRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeAssistRules::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	const FVector Stick = FVector::ForwardVector;
	TArray<FVector> Candidates = {
		FVector(300.f, 0.f, 0.f),                      // straight down the stick, 300
		FVector(150.f * 0.643f, 150.f * 0.766f, 0.f),  // 50 degrees off, 150
		FVector(-100.f, 0.f, 0.f),                     // behind
		FVector(360.f, 0.f, 0.f),                      // past 350
		FVector(100.f, 0.f, 300.f),                    // on the roof above
	};
	// 300/350 + 0 = 0.86 beats 150/350 + 50/60 = 1.26.
	TestEqual(TEXT("The one down the stick beats a nearer one off to the side"),
		UHawkeyeMeleeRules::SelectMeleeAssistIndex(Origin, Stick, Candidates, 350.f, 60.f, 200.f), 0);
	Candidates[0] = FVector(-300.f, 0.f, 0.f);
	TestEqual(TEXT("Without him, the one 50 degrees off"),
		UHawkeyeMeleeRules::SelectMeleeAssistIndex(Origin, Stick, Candidates, 350.f, 60.f, 200.f), 1);
	TestEqual(TEXT("Nobody inside 45 degrees"),
		UHawkeyeMeleeRules::SelectMeleeAssistIndex(Origin, Stick, Candidates, 350.f, 45.f, 200.f), INDEX_NONE);
	TestEqual(TEXT("A thug at 360 is out of reach"),
		UHawkeyeMeleeRules::SelectMeleeAssistIndex(Origin, Stick, { FVector(360.f, 0.f, 0.f) }, 350.f, 60.f, 200.f), INDEX_NONE);

	TestEqual(TEXT("Gap close: 300 away stops 90 short"), UHawkeyeMeleeRules::ComputeGapCloseDistance(300.f, 90.f, 20.f, 260.f), 210.f);
	TestEqual(TEXT("Close in: never less than the attack's own lunge"),
		UHawkeyeMeleeRules::ComputeGapCloseDistance(100.f, 90.f, 20.f, 260.f), 20.f);
	TestEqual(TEXT("Never more than 260"), UHawkeyeMeleeRules::ComputeGapCloseDistance(500.f, 90.f, 20.f, 260.f), 260.f);

	// Reach 180 (120 + 60), 1.3x is 234 to his capsule's edge, 30 degrees.
	TestTrue(TEXT("Forgiven at 260 (edge 226)"),
		UHawkeyeMeleeRules::IsAssistHitForgiven(Origin, Stick, FVector(260.f, 0.f, 0.f), 34.f, 180.f, 1.3f, 30.f));
	TestFalse(TEXT("Not at 280 (edge 246)"),
		UHawkeyeMeleeRules::IsAssistHitForgiven(Origin, Stick, FVector(280.f, 0.f, 0.f), 34.f, 180.f, 1.3f, 30.f));
	const FVector At25 = FVector(FMath::Cos(FMath::DegreesToRadians(25.f)), FMath::Sin(FMath::DegreesToRadians(25.f)), 0.f) * 200.f;
	const FVector At35 = FVector(FMath::Cos(FMath::DegreesToRadians(35.f)), FMath::Sin(FMath::DegreesToRadians(35.f)), 0.f) * 200.f;
	TestTrue(TEXT("Forgiven 25 degrees off"), UHawkeyeMeleeRules::IsAssistHitForgiven(Origin, Stick, At25, 34.f, 180.f, 1.3f, 30.f));
	TestFalse(TEXT("Not 35 degrees off"), UHawkeyeMeleeRules::IsAssistHitForgiven(Origin, Stick, At35, 34.f, 180.f, 1.3f, 30.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeAssistPick, "Hawkeye.Melee.AssistPicksWhereSheMeans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeAssistPick::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeAssistTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Ahead = SpawnThug(TestWorld, FVector(250.f, 0.f, 0.f));
	AThugCharacter* Left = SpawnThug(TestWorld, FVector(0.f, -250.f, 0.f));
	if (!Kate || !Ahead || !Left)
	{
		AddError(TEXT("Failed to spawn Kate and the thugs."));
		return false;
	}
	TestTrue(TEXT("No stick: the camera's forward picks the one ahead"), Kate->FindMeleeAssistTarget() == Ahead);
	Kate->SetMoveInputDirectionForTest(-FVector::RightVector);
	TestTrue(TEXT("The stick to the left picks the one on her left"), Kate->FindMeleeAssistTarget() == Left);
	TestTrue(TEXT("The light starts"), Kate->StartLightAttack());
	TestTrue(TEXT("Aimed at him"), Kate->GetMeleeComponent()->GetSwingTarget() == Left);
	TestTrue(TEXT("The blow goes at him"),
		FVector::DotProduct(Kate->GetMeleeComponent()->GetSwingDirection(), -FVector::RightVector) > 0.99f);
	Kate->GetMeleeComponent()->AdvanceAttack(1.f);

	// Out of reach, or behind: nobody.
	Left->SetActorLocation(FVector(0.f, -400.f, 0.f));
	TestNull(TEXT("At 400 he is past the 350"), Kate->FindMeleeAssistTarget());
	Left->SetActorLocation(FVector(0.f, 250.f, 0.f));
	TestNull(TEXT("Behind the stick, nobody"), Kate->FindMeleeAssistTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeAssistCloses, "Hawkeye.Melee.AssistClosesTheGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeAssistCloses::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeAssistTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(300.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	TestTrue(TEXT("Her swings are assisted"), Melee->bAssistSwings);
	TestTrue(TEXT("The light starts"), Kate->StartLightAttack());
	TestEqual(TEXT("Her sweep is 60 cm"), Melee->GetCurrentAttack().Radius, 60.f);
	TestEqual(TEXT("The step closes to 90 cm short of him (210)"), Melee->GetLastLungeDistance(), 210.f, 0.5f);
	Melee->AdvanceAttack(1.f);

	// Close in, the lunge is the attack's own.
	Thug->SetActorLocation(FVector(100.f, 0.f, 0.f));
	TestTrue(TEXT("Another light"), Kate->StartLightAttack());
	TestEqual(TEXT("At 100 cm she steps only her own 20 (or the chain's)"), Melee->GetLastLungeDistance(),
		Melee->GetCurrentAttack().LungeDistance, 0.5f);
	Melee->AdvanceAttack(1.f);

	// Thugs get no assist.
	TestFalse(TEXT("A thug's swing is not assisted"), Thug->GetMeleeComponent()->bAssistSwings);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeAssistForgives, "Hawkeye.Melee.AssistForgivesAStepBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeAssistForgives::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeAssistTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(150.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	UMeleeComponent* Melee = Kate->GetMeleeComponent();
	UHealthComponent* Health = Thug->GetHealthComponent();
	const float Radius = Thug->GetCapsuleComponent()->GetScaledCapsuleRadius();

	// Valid at the press, a step back mid-swing: the sweep (reach + his radius) misses, the assist lands it.
	TestTrue(TEXT("The light starts"), Kate->StartLightAttack());
	const float Reach = Melee->GetCurrentAttack().Range + Melee->GetCurrentAttack().Radius;
	TestEqual(TEXT("Reach 180"), Reach, 180.f);
	Thug->SetActorLocation(FVector(Reach * 1.3f + Radius - 10.f, 0.f, 0.f));
	Melee->AdvanceAttack(Melee->GetCurrentAttack().WindupSeconds + 0.01f);
	TestEqual(TEXT("It lands"), Health->GetCurrentHealth(), 85.f);
	TestEqual(TEXT("On the forgiveness"), Melee->GetForgivenHitCount(), 1);
	Melee->AdvanceAttack(1.f);
	Thug->UpdateKnockdown(5.f);

	// Past 1.3x the reach at the hit: a miss.
	Thug->SetActorLocation(FVector(150.f, 0.f, 0.f));
	TestTrue(TEXT("Another light"), Kate->StartLightAttack());
	Thug->SetActorLocation(FVector(Reach * 1.3f + Radius + 20.f, 0.f, 0.f));
	Melee->AdvanceAttack(Melee->GetCurrentAttack().WindupSeconds + 0.01f);
	TestEqual(TEXT("Too far: no damage"), Health->GetCurrentHealth(), 85.f);
	Melee->AdvanceAttack(1.f);

	// Out to the side more than 30 degrees: a miss.
	Thug->SetActorLocation(FVector(150.f, 0.f, 0.f));
	TestTrue(TEXT("A third light"), Kate->StartLightAttack());
	const FVector Aside = FVector(FMath::Cos(FMath::DegreesToRadians(40.f)), FMath::Sin(FMath::DegreesToRadians(40.f)), 0.f) * 220.f;
	Thug->SetActorLocation(Aside);
	Melee->AdvanceAttack(Melee->GetCurrentAttack().WindupSeconds + 0.01f);
	TestEqual(TEXT("40 degrees off: no damage"), Health->GetCurrentHealth(), 85.f);
	Melee->AdvanceAttack(1.f);

	// Never a valid pick (behind her at the press): the forgiveness does not reach round for him.
	Thug->SetActorLocation(FVector(-150.f, 0.f, 0.f));
	TestTrue(TEXT("A light at the air"), Kate->StartLightAttack());
	TestNull(TEXT("Nobody picked"), Melee->GetSwingTarget());
	Thug->SetActorLocation(FVector(Reach * 1.3f + Radius - 10.f, 0.f, 0.f));
	Melee->AdvanceAttack(Melee->GetCurrentAttack().WindupSeconds + 0.01f);
	TestEqual(TEXT("Not his: no damage"), Health->GetCurrentHealth(), 85.f);
	TestEqual(TEXT("Still one forgiven"), Melee->GetForgivenHitCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMeleeAssistTracks, "Hawkeye.Melee.AssistKeepsFacingHim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMeleeAssistTracks::RunTest(const FString& Parameters)
{
	using namespace HawkeyeMeleeAssistTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(200.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	TestTrue(TEXT("The heavy starts (0.6 s wind-up)"), Kate->StartHeavyAttack());
	Kate->AdvanceMeleeFlow(0.1f);
	TestEqual(TEXT("Facing him after the soft turn"), static_cast<float>(Kate->GetActorRotation().Yaw), 0.f, 0.5f);
	Thug->SetActorLocation(FVector(200.f, 100.f, 0.f));
	Kate->AdvanceMeleeFlow(0.1f);
	TestEqual(TEXT("She follows him as he moves in the wind-up"), static_cast<float>(Kate->GetActorRotation().Yaw),
		FMath::RadiansToDegrees(FMath::Atan2(100.f, 200.f)), 0.5f);
	return true;
}

#endif
