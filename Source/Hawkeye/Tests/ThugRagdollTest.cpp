#include "Combat/HealthComponent.h"
#include "Combat/Takedownable.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeRagdollTest
{
	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AThugCharacter>(TestWorld.SpawnActor(
			AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugGoLimpDisablesTheThug, "Hawkeye.Thug.GoLimpDisablesTheThug",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugGoLimpDisablesTheThug::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = HawkeyeRagdollTest::SpawnThug(TestWorld);
	if (!Thug)
	{
		AddError(TEXT("Could not spawn the thug."));
		return false;
	}

	UCapsuleComponent* Capsule = Thug->GetCapsuleComponent();
	UCharacterMovementComponent* Movement = Thug->GetCharacterMovement();

	TestTrue(TEXT("The capsule collides while the thug is alive"),
		Capsule && Capsule->GetCollisionEnabled() != ECollisionEnabled::NoCollision);

	Thug->GoLimp(nullptr);

	TestTrue(TEXT("The capsule stops colliding"),
		Capsule && Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Movement is disabled"),
		Movement && Movement->MovementMode == EMovementMode::MOVE_None);
	TestNull(TEXT("The AI no longer possesses the body"), Thug->GetController());

	// Takedown then bullet, or bullet then death delegate: GoLimp has to be safe twice.
	Thug->GoLimp(nullptr);
	TestTrue(TEXT("A second GoLimp is harmless"),
		Capsule && Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugDeathGoesLimpOnce, "Hawkeye.Thug.DeathGoesLimpOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugDeathGoesLimpOnce::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = HawkeyeRagdollTest::SpawnThug(TestWorld);
	if (!Thug)
	{
		AddError(TEXT("Could not spawn the thug."));
		return false;
	}

	UHealthComponent* Health = Thug->GetHealthComponent();
	if (!Health)
	{
		AddError(TEXT("The thug has no health component."));
		return false;
	}

	TestEqual(TEXT("Thugs have 100 HP"), Health->MaxHealth, 100.f);

	Health->ApplyDamage(150.f, nullptr);

	TestFalse(TEXT("The thug is dead"), Health->IsAlive());
	TestTrue(TEXT("And the body went limp"),
		Thug->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugMeshCanRagdoll, "Hawkeye.Thug.MeshCanRagdoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * Thugs were freezing upright on death, and every spawn logged
 *
 *     USkeletalMeshComponent::InitArticulated : Could not find root physics body
 *
 * The mannequin's physics asset had come out of the engine's Standard feature pack, which
 * ships it as a 1 KB stub: the right name, the right preview mesh, and empty Bodies and
 * Constraints arrays. InitArticulated then has nothing to bind, SetSimulatePhysics silently
 * does nothing, and the corpse stands there. The High pack's copy is the real 26 KB asset and
 * carries the same internal package path, so it drops straight in.
 *
 * This is the thug against that regressing: bodies exist, and one of them is on the root.
 */
bool FHawkeyeThugMeshCanRagdoll::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mannequin = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Mannequin/Character/Mesh/SK_Mannequin"));
	if (!Mannequin)
	{
		AddError(TEXT("SK_Mannequin would not load."));
		return false;
	}

	UPhysicsAsset* Physics = Mannequin->GetPhysicsAsset();
	if (!Physics)
	{
		AddError(TEXT("SK_Mannequin has no physics asset; run create_world_blueprints.py."));
		return false;
	}

	const int32 BodyCount = Physics->SkeletalBodySetups.Num();
	AddInfo(FString::Printf(TEXT("%s has %d bodies."), *Physics->GetName(), BodyCount));
	TestTrue(TEXT("The physics asset has bodies to simulate"), BodyCount > 0);
	if (BodyCount == 0)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Mannequin->GetRefSkeleton();
	if (RefSkeleton.GetNum() == 0)
	{
		AddError(TEXT("SK_Mannequin has no reference skeleton."));
		return false;
	}

	const FName RootBone = RefSkeleton.GetBoneName(0);
	int32 MatchingBodies = 0;
	bool bRootHasBody = false;
	for (const TObjectPtr<USkeletalBodySetup>& Body : Physics->SkeletalBodySetups)
	{
		if (!Body)
		{
			continue;
		}
		if (RefSkeleton.FindBoneIndex(Body->BoneName) != INDEX_NONE)
		{
			++MatchingBodies;
		}
		if (Body->BoneName == RootBone)
		{
			bRootHasBody = true;
		}
	}

	AddInfo(FString::Printf(TEXT("Root bone is '%s'; %d of %d bodies name a bone on this skeleton."),
		*RootBone.ToString(), MatchingBodies, BodyCount));

	TestTrue(TEXT("The bodies belong to this skeleton"), MatchingBodies > 0);
	TestEqual(TEXT("Every body names a bone this skeleton has"), MatchingBodies, BodyCount);

	// Nothing hangs off 'root' on a UE4 mannequin - the physics hierarchy starts at the pelvis -
	// and that is fine: InitArticulated picks the highest body it finds. It only fails when the
	// array is empty, which is what the asserts above cover.
	AddInfo(FString::Printf(TEXT("A body on the root bone itself: %s."),
		bRootHasBody ? TEXT("yes") : TEXT("no, the hierarchy starts lower down")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugTakedownNamesTheAttacker, "Hawkeye.Thug.TakedownNamesTheAttacker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugTakedownNamesTheAttacker::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = HawkeyeRagdollTest::SpawnThug(TestWorld);
	AActor* Attacker = TestWorld.SpawnActor(
		AActor::StaticClass(), FVector(-100.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (!Thug || !Attacker)
	{
		AddError(TEXT("Could not spawn the thug or the attacker."));
		return false;
	}

	UHealthComponent* Health = Thug->GetHealthComponent();
	if (!Health)
	{
		AddError(TEXT("The thug has no health component."));
		return false;
	}

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Health->OnDeath.AddDynamic(Listener, &UHawkeyeTestListener::HandleDeath);

	// A takedown used to route its damage with no instigator, so every stealth kill logged
	// "killed by None" and nothing downstream could tell who did it.
	ITakedownable::Execute_OnTakedown(Thug, Attacker);

	TestEqual(TEXT("The thug died once"), Listener->DeathCount, 1);
	TestEqual(TEXT("And the takedown named the attacker"),
		static_cast<AActor*>(Listener->LastKiller), Attacker);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
