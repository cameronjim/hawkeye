#include "Animation/AnimMontage.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/HealthComponent.h"
#include "Combat/HitContactRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The authored clips' rules (gameplay-semantics.md, "Authored clips"): which get-up a body lying on the floor plays, where
 * the capsule goes so the clip starts on the body, what a blow does to him while he gets up, when he moves again, and
 * which hit clip a blow plays. The clips themselves are content; a test world plays none, so the get-up's timing is run
 * through BeginGetUpForTest.
 */
namespace HawkeyeAuthoredClipsTest
{
	static UAnimMontage* MakeMontage()
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		Montage->SlotAnimTracks.AddDefaulted();
		Montage->SlotAnimTracks[0].SlotName = HawkeyeCombatAnim::FullBodySlot;
		return Montage;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAuthoredGetUpChoice, "Hawkeye.AuthoredClips.GetUpChosenByPelvis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAuthoredGetUpChoice::RunTest(const FString& Parameters)
{
	using namespace HawkeyeAuthoredClipsTest;
	TestTrue(TEXT("Chest facing up: on his back"), UHawkeyeHitContactRules::IsLyingOnBack(FVector(0.1f, 0.f, 0.9f)));
	TestFalse(TEXT("Chest facing the floor: face down"), UHawkeyeHitContactRules::IsLyingOnBack(FVector(0.f, 0.2f, -0.95f)));
	TestFalse(TEXT("On his side counts as face down"), UHawkeyeHitContactRules::IsLyingOnBack(FVector(1.f, 0.f, 0.f)));

	UCombatAnimSet* Set = NewObject<UCombatAnimSet>(GetTransientPackage());
	TestTrue(TEXT("No set: GetUp (the role the fallback reads)"), UCombatAnimSet::GetUpRoleFor(nullptr, false) == ECombatAnimRole::GetUp);
	Set->GetUp = MakeMontage();
	TestTrue(TEXT("On his back: GetUp"), UCombatAnimSet::GetUpRoleFor(Set, true) == ECombatAnimRole::GetUp);
	TestTrue(TEXT("Face down with no face-down clip: GetUp too"), UCombatAnimSet::GetUpRoleFor(Set, false) == ECombatAnimRole::GetUp);
	Set->GetUpFront = MakeMontage();
	TestTrue(TEXT("Face down with one: GetUpFront"), UCombatAnimSet::GetUpRoleFor(Set, false) == ECombatAnimRole::GetUpFront);
	TestTrue(TEXT("On his back still GetUp"), UCombatAnimSet::GetUpRoleFor(Set, true) == ECombatAnimRole::GetUp);
	TestTrue(TEXT("The new roles resolve from their slots"), Set->ResolveMontage(ECombatAnimRole::GetUpFront) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAuthoredGetUpAlignment, "Hawkeye.AuthoredClips.GetUpStartsWhereTheBodyLies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAuthoredGetUpAlignment::RunTest(const FString& Parameters)
{
	FVector Location;
	float Yaw = 0.f;
	// The clip lies with its pelvis 60 cm behind the capsule (-X) and its spine pointing back; the body's pelvis is at
	// (1000, 500), its spine pointing +Y. The capsule turns -90 so the clip's spine (-X) points +Y, and stands 60 cm
	// from the pelvis the other way (-Y).
	UHawkeyeHitContactRules::ComputeGetUpAlignment(FVector(1000.f, 500.f, 12.f), FVector(0.f, 1.f, 0.05f),
		FVector(-60.f, 0.f, 11.f), FVector(-1.f, 0.f, 0.03f), 33.f, Location, Yaw);
	TestEqual(TEXT("Turned so the clip's spine lies along the body's"), Yaw, -90.f, 0.01f);
	TestEqual(TEXT("X: on the pelvis"), static_cast<float>(Location.X), 1000.f, 0.01f);
	TestEqual(TEXT("Y: 60 cm short of it, so the clip's pelvis lands on the body's"), static_cast<float>(Location.Y), 440.f, 0.01f);
	TestEqual(TEXT("Z is the pelvis's (the caller stands it on the floor)"), static_cast<float>(Location.Z), 12.f, 0.01f);
	const FVector Landed = Location + FRotator(0.f, Yaw, 0.f).RotateVector(FVector(-60.f, 0.f, 0.f));
	TestTrue(TEXT("The clip's pelvis lands on the body's"), FVector::Dist2D(Landed, FVector(1000.f, 500.f, 0.f)) < 0.01f);

	// A spine pointing straight up has nothing flat to turn by: the fallback yaw.
	UHawkeyeHitContactRules::ComputeGetUpAlignment(FVector(0.f, 0.f, 50.f), FVector::UpVector, FVector(10.f, 0.f, 0.f),
		FVector(-1.f, 0.f, 0.f), 33.f, Location, Yaw);
	TestEqual(TEXT("Upright body: keeps the capsule's yaw"), Yaw, 33.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAuthoredGetUpControl, "Hawkeye.AuthoredClips.GetUpReturnsControlAndAbsorbsBlows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAuthoredGetUpControl::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Standing: a blow reacts as ever"), UHawkeyeHitContactRules::ResolveHitDuringGetUp(false, false) == EHawkeyeGetUpHit::React);
	TestTrue(TEXT("Getting up: absorbed"), UHawkeyeHitContactRules::ResolveHitDuringGetUp(true, false) == EHawkeyeGetUpHit::Absorb);
	TestTrue(TEXT("Getting up, a knockdown: down again"),
		UHawkeyeHitContactRules::ResolveHitDuringGetUp(true, true) == EHawkeyeGetUpHit::KnockDownAgain);

	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector,
		FRotator::ZeroRotator));
	AThugCharacter* Other = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(300.f, 0.f, 0.f),
		FRotator::ZeroRotator));
	if (!Thug || !Other)
	{
		AddError(TEXT("Failed to spawn the thugs."));
		return false;
	}
	UHealthComponent* Health = Thug->GetHealthComponent();
	Health->SetMaxHealth(500.f, true);

	Thug->BeginGetUpForTest(1.6f);
	TestTrue(TEXT("Getting up"), Thug->IsGettingUp());
	TestTrue(TEXT("So the AI holds off (incapacitated)"), Thug->IsIncapacitated());
	TestTrue(TEXT("He does not move under the clip"), Thug->GetCharacterMovement()->MovementMode == MOVE_None);
	TestEqual(TEXT("For the clip's length"), Thug->GetGetUpSeconds(), 1.6f, 0.001f);

	// A blow while he gets up: it hurts, but no stagger and the get-up runs on.
	Health->ApplyDamage(10.f, Other);
	Thug->StaggerFor(Other, 0.5f);
	TestTrue(TEXT("The damage lands"), Health->GetCurrentHealth() < 500.f);
	TestFalse(TEXT("No stagger while getting up"), Thug->IsStaggered());
	TestTrue(TEXT("Still getting up"), Thug->IsGettingUp());

	Thug->UpdateGetUpForTest(1.55f);
	TestTrue(TEXT("Not up before the clip ends"), Thug->IsGettingUp());
	Thug->UpdateGetUpForTest(0.1f);
	TestFalse(TEXT("Up when it ends"), Thug->IsGettingUp());
	TestFalse(TEXT("No longer incapacitated: the AI runs again"), Thug->IsIncapacitated());
	TestTrue(TEXT("And he walks again"), Thug->GetCharacterMovement()->MovementMode == MOVE_Walking);
	Thug->StaggerFor(Other, 0.5f);
	TestTrue(TEXT("On his feet, a blow staggers him as ever"), Thug->IsStaggered());

	// A knockdown during the get-up puts him down again and ends the get-up.
	Thug->BeginGetUpForTest(1.6f);
	Thug->KnockdownFor(Other, 2.f);
	TestTrue(TEXT("Down again"), Thug->IsKnockedDown());
	TestFalse(TEXT("The get-up is over"), Thug->IsGettingUp());
	Thug->UpdateKnockdown(2.1f);
	TestFalse(TEXT("And he is up after it (no clip or ragdoll here: straight up)"), Thug->IsKnockedDown());
	TestFalse(TEXT("Not left getting up"), Thug->IsGettingUp());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAuthoredHitRole, "Hawkeye.AuthoredClips.HeavyBlowPlaysTheStagger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeAuthoredHitRole::RunTest(const FString& Parameters)
{
	using namespace HawkeyeAuthoredClipsTest;
	UCombatAnimSet* Set = NewObject<UCombatAnimSet>(GetTransientPackage());
	TestTrue(TEXT("No HitHeavy clip: a heavy blow plays its side's clip"),
		UCombatAnimSet::HitRoleForBlow(Set, EHawkeyeHitDirection::Left, 1.f, 0.6f) == ECombatAnimRole::HitLeft);
	Set->HitHeavy = MakeMontage();
	TestTrue(TEXT("A bat (0.65) plays the heavy stagger"),
		UCombatAnimSet::HitRoleForBlow(Set, EHawkeyeHitDirection::Front, 0.65f, 0.6f) == ECombatAnimRole::HitHeavy);
	TestTrue(TEXT("From any side"), UCombatAnimSet::HitRoleForBlow(Set, EHawkeyeHitDirection::Right, 0.8f, 0.6f) == ECombatAnimRole::HitHeavy);
	TestTrue(TEXT("Fists (0.4) play the side's light clip"),
		UCombatAnimSet::HitRoleForBlow(Set, EHawkeyeHitDirection::Right, 0.4f, 0.6f) == ECombatAnimRole::HitRight);
	TestTrue(TEXT("No set: the side's clip"), UCombatAnimSet::HitRoleForBlow(nullptr, EHawkeyeHitDirection::Back, 1.f, 0.6f)
		== ECombatAnimRole::HitBack);
	return true;
}

#endif
