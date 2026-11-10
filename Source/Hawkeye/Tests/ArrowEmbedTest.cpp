#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/ExplosiveBlast.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Arrows in bodies (claude-docs/gameplay-semantics.md, "Arrows in bodies"): an arrow that meets a capsule
 * ends up on a bone with its tip in the body, not a foot out on the capsule. Like the GASP tests this loads
 * the two real meshes (Kate's UEFN mannequin and the thugs' mannequin), because the point is their shapes.
 */
namespace HawkeyeArrowEmbedTest
{
	static const TCHAR* KateMeshPath = TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin");
	static const TCHAR* ThugMeshPath = TEXT("/Game/Mannequin/Character/Mesh/SK_Mannequin");

	/** Dresses Character in MeshPath on a capsule of Radius (half height 88), with no mesh collision: only the capsule stops arrows. */
	static USkeletalMeshComponent* Dress(ACharacter* Character, const TCHAR* MeshPath, float Radius)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
		USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
		if (!Mesh || !Body || !Mesh->GetPhysicsAsset())
		{
			return nullptr;
		}
		Character->GetCapsuleComponent()->SetCapsuleSize(Radius, 88.f);
		Body->SetSkeletalMeshAsset(Mesh);
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -88.f), FRotator(0.f, -90.f, 0.f));
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->RefreshBoneTransforms();
		Body->UpdateComponentToWorld();
		return Body;
	}

	/** How far Point is outside the nearest of Body's physics shapes (0 inside). */
	static float DistanceToBody(const USkeletalMeshComponent& Body, const FVector& Point)
	{
		float Best = TNumericLimits<float>::Max();
		for (const TObjectPtr<USkeletalBodySetup>& Setup : Body.GetPhysicsAsset()->SkeletalBodySetups)
		{
			const int32 Bone = Setup ? Body.GetBoneIndex(Setup->BoneName) : INDEX_NONE;
			if (Bone != INDEX_NONE)
			{
				const float Distance = Setup->GetShortestDistanceToPoint(Point, Body.GetBoneTransform(Bone));
				if (Distance >= 0.f)
				{
					Best = FMath::Min(Best, Distance);
				}
			}
		}
		return Best;
	}

	static UArrowDefinition* MakeStandard(UObject* Outer)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->Slot = 1;
		Arrow->Cap = 30;
		Arrow->Damage = 10.f;
		Arrow->bRecoverable = true;
		Arrow->ProjectileClass = AArrowProjectile::StaticClass();
		return Arrow;
	}

	/** Flies an arrow from From toward To until it sticks (2 s at most) and returns it. */
	static AArrowProjectile* Shoot(const FHawkeyeTestWorld& TestWorld, const FVector& From, const FVector& To, UArrowDefinition* Definition,
		UBowDefinition* Bow)
	{
		AArrowProjectile* Arrow = Cast<AArrowProjectile>(TestWorld.SpawnActor(AArrowProjectile::StaticClass(), From, FRotator::ZeroRotator));
		if (!Arrow)
		{
			return nullptr;
		}
		Arrow->InitArrow(Definition, Bow, Definition ? Definition->Damage : 10.f, nullptr, nullptr);
		// Straight lines, so each shot goes exactly where it is pointed.
		Arrow->GetProjectileMovement()->ProjectileGravityScale = 0.f;
		Arrow->LaunchWithVelocity((To - From).GetSafeNormal() * 6000.f);
		for (float Elapsed = 0.f; !Arrow->IsStuck() && Elapsed < 2.f; Elapsed += 0.005f)
		{
			Arrow->AdvanceFlight(0.005f);
		}
		return Arrow;
	}

	/** The checks every body shot must pass: stuck in Who, on a bone of Body, tip within 10 cm of its surface. */
	static void CheckEmbedded(FAutomationTestBase& Test, const FString& What, AArrowProjectile* Arrow, AActor* Who,
		USkeletalMeshComponent* Body, float CapsuleRadius, const FVector& Flight)
	{
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: an arrow"), *What), Arrow) || !Test.TestTrue(*FString::Printf(TEXT("%s: it stuck"), *What), Arrow->IsStuck()))
		{
			return;
		}
		Test.TestTrue(*FString::Printf(TEXT("%s: in him"), *What), Arrow->GetStuckInActor() == Who);
		const USceneComponent* Parent = Arrow->GetRootComponent()->GetAttachParent();
		const FName Socket = Arrow->GetRootComponent()->GetAttachSocketName();
		Test.TestTrue(*FString::Printf(TEXT("%s: attached to the mesh, not the capsule"), *What), Parent == Body);
		Test.TestTrue(*FString::Printf(TEXT("%s: on a bone (%s)"), *What, *Socket.ToString()),
			!Socket.IsNone() && Body->GetBoneIndex(Socket) != INDEX_NONE);
		const FVector Tip = Arrow->GetActorLocation();
		const FVector Dir = Flight.GetSafeNormal();
		const float TipOut = DistanceToBody(*Body, Tip);
		const float BackOut = DistanceToBody(*Body, Tip - Dir * 10.f);
		Test.AddInfo(FString::Printf(TEXT("%s: bone %s, tip %.1f cm outside the body, 10 cm back %.1f cm, %.1f cm from the capsule axis."),
			*What, *Socket.ToString(), TipOut, BackOut, FVector::Dist2D(Tip, Who->GetActorLocation())));
		Test.TestTrue(*FString::Printf(TEXT("%s: the tip is within 10 cm of the body's surface (%.1f cm out)"), *What, TipOut), TipOut <= 10.f);
		Test.TestTrue(*FString::Printf(TEXT("%s: well inside the capsule, not on it"), *What),
			FVector::Dist2D(Tip, Who->GetActorLocation()) < CapsuleRadius - 10.f);
		Test.TestTrue(*FString::Printf(TEXT("%s: pointing along its flight"), *What),
			FVector::DotProduct(Arrow->GetActorForwardVector(), Dir) > 0.999f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArrowPartnerFire, "Hawkeye.Arrow.PartnerArrowsNeverHurtThePair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeArrowPartnerFire::RunTest(const FString& Parameters)
{
	using namespace HawkeyeArrowEmbedTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Clint = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator));
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(900.f, 300.f, 0.f), FRotator::ZeroRotator));
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(
		TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator));
	if (!Clint || !Kate || !Thug || !Wall)
	{
		AddError(TEXT("Failed to spawn Clint, Kate, a thug and a wall."));
		return false;
	}
	Wall->SetExtent(FVector(20.f, 300.f, 300.f));
	TestTrue(TEXT("Kate is Clint's partner"), AArrowProjectile::IsPartnerOf(Clint, Kate));
	TestTrue(TEXT("And he hers"), AArrowProjectile::IsPartnerOf(Kate, Clint));
	TestFalse(TEXT("Nobody is his own partner"), AArrowProjectile::IsPartnerOf(Clint, Clint));
	TestFalse(TEXT("A thug is nobody's partner"), AArrowProjectile::IsPartnerOf(Clint, Thug));
	UHealthComponent* KateHealth = Kate->GetHealthComponent();
	const float Before = KateHealth->GetCurrentHealth();

	// Clint's arrow at a thug who is gone, with Kate in the way: it flies on through her into the wall.
	UArrowDefinition* Standard = MakeStandard(GetTransientPackage());
	Standard->Damage = 50.f;
	AArrowProjectile* Arrow = Cast<AArrowProjectile>(TestWorld.SpawnActor(AArrowProjectile::StaticClass(), FVector(60.f, 0.f, 30.f),
		FRotator::ZeroRotator));
	if (!Arrow)
	{
		AddError(TEXT("No arrow."));
		return false;
	}
	Arrow->InitArrow(Standard, nullptr, 50.f, Clint, nullptr);
	Arrow->GetProjectileMovement()->ProjectileGravityScale = 0.f;
	Arrow->LaunchWithVelocity(FVector(6000.f, 0.f, 0.f));
	for (float Elapsed = 0.f; !Arrow->IsStuck() && Elapsed < 1.f; Elapsed += 0.005f)
	{
		Arrow->AdvanceFlight(0.005f);
	}
	TestTrue(TEXT("It stuck"), Arrow->IsStuck());
	TestTrue(TEXT("In the wall behind her, not in her"), Arrow->GetStuckInActor() == Wall);
	TestEqual(TEXT("Kate is unhurt"), KateHealth->GetCurrentHealth(), Before);

	// A hit on her that got through anyway does nothing.
	AArrowProjectile* Direct = Cast<AArrowProjectile>(TestWorld.SpawnActor(AArrowProjectile::StaticClass(), FVector(300.f, 0.f, 30.f),
		FRotator::ZeroRotator));
	Direct->InitArrow(Standard, nullptr, 50.f, Clint, nullptr);
	FHitResult Hit;
	Hit.bBlockingHit = true;
	Hit.ImpactPoint = FVector(466.f, 0.f, 30.f);
	Hit.Location = Hit.ImpactPoint;
	Hit.TraceStart = FVector(300.f, 0.f, 30.f);
	Hit.TraceEnd = FVector(600.f, 0.f, 30.f);
	Hit.HitObjectHandle = FActorInstanceHandle(Kate);
	Direct->HandleImpact(Hit);
	TestEqual(TEXT("A direct hit from her partner does no damage"), KateHealth->GetCurrentHealth(), Before);

	// His explosive near her: half the falloff damage for her, the full falloff for him in his own blast.
	UArrowDefinition* Explosive = MakeStandard(GetTransientPackage());
	Explosive->OnHitEffect = EArrowHitEffect::Explosive;
	Explosive->Damage = 80.f;
	UHealthComponent* ClintHealth = Clint->GetHealthComponent();
	const float ClintBefore = ClintHealth->GetCurrentHealth();
	FHitResult Blast;
	Blast.bBlockingHit = true;
	Blast.ImpactPoint = FVector(300.f, 0.f, 0.f);
	Blast.Location = Blast.ImpactPoint;
	AExplosiveBlast* Effect = Cast<AExplosiveBlast>(TestWorld.SpawnActor(AExplosiveBlast::StaticClass(), Blast.ImpactPoint, FRotator::ZeroRotator));
	if (!Effect)
	{
		AddError(TEXT("No blast."));
		return false;
	}
	Effect->InitEffect(Explosive, Clint, Blast);
	Effect->Activate();
	TestEqual(TEXT("Kate 200 cm from Clint's blast takes half of 40"), KateHealth->GetCurrentHealth(), Before - 20.f, 0.01f);
	TestEqual(TEXT("Clint 300 cm from his own takes all of 20"), ClintHealth->GetCurrentHealth(), ClintBefore - 20.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArrowEmbedsInKate, "Hawkeye.Arrow.EmbedsInKatesBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeArrowEmbedsInKate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeArrowEmbedTest;
	FHawkeyeTestWorld TestWorld;
	ACharacter* Kate = Cast<ACharacter>(TestWorld.SpawnActor(ACharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	USkeletalMeshComponent* Body = Dress(Kate, KateMeshPath, 42.f);
	if (!Body)
	{
		AddError(TEXT("SKM_UEFN_Mannequin (and its physics asset) would not load; run Tools\\create-content.ps1."));
		return false;
	}
	UArrowDefinition* Standard = MakeStandard(GetTransientPackage());
	const FVector Chest = Kate->GetActorLocation() + FVector(0.f, 0.f, 30.f);

	// Level, into her chest from 10 m: the capsule is 42 cm round her.
	CheckEmbedded(*this, TEXT("Level at the chest"), Shoot(TestWorld, Chest - FVector(1000.f, 0.f, 0.f), Chest, Standard, nullptr), Kate,
		Body, 42.f, FVector(1000.f, 0.f, 0.f));
	// From a roof 8 m up and 15 m out: the capsule's top met the old arrows furthest out.
	CheckEmbedded(*this, TEXT("Down from a roof"), Shoot(TestWorld, Chest + FVector(-1500.f, 300.f, 800.f), Chest, Standard, nullptr),
		Kate, Body, 42.f, -FVector(-1500.f, 300.f, 800.f));
	// From the side, at the hip.
	const FVector Hip = Kate->GetActorLocation() + FVector(0.f, 0.f, -5.f);
	CheckEmbedded(*this, TEXT("Side at the hip"), Shoot(TestWorld, Hip + FVector(0.f, -1200.f, 50.f), Hip, Standard, nullptr), Kate, Body,
		42.f, -FVector(0.f, -1200.f, 50.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeArrowEmbedsInThug, "Hawkeye.Arrow.EmbedsInThugsAndKeepsHeadshotsHonest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeArrowEmbedsInThug::RunTest(const FString& Parameters)
{
	using namespace HawkeyeArrowEmbedTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	USkeletalMeshComponent* Body = Dress(Thug, ThugMeshPath, 34.f);
	if (!Body || !Thug->GetHealthComponent())
	{
		AddError(TEXT("SK_Mannequin (and its physics asset) would not load, or the thug has no health."));
		return false;
	}
	UArrowDefinition* Standard = MakeStandard(GetTransientPackage());
	UBowDefinition* Bow = NewObject<UBowDefinition>(GetTransientPackage());
	UHealthComponent* Health = Thug->GetHealthComponent();
	Health->SetMaxHealth(1000.f, /*bResetCurrent=*/true);

	const FVector Chest = Thug->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	AArrowProjectile* InChest = Shoot(TestWorld, Chest - FVector(1500.f, 0.f, 0.f), Chest, Standard, Bow);
	CheckEmbedded(*this, TEXT("Thug chest"), InChest, Thug, Body, 34.f, FVector::ForwardVector);
	TestEqual(TEXT("A body shot does 10"), Health->GetCurrentHealth(), 990.f, 0.01f);

	// Straight at his head: a headshot.
	const FVector Head = Body->GetBoneLocation(FName(TEXT("head")));
	Health->SetMaxHealth(1000.f, true);
	AArrowProjectile* InHead = Shoot(TestWorld, Head - FVector(1500.f, 0.f, 0.f), Head, Standard, Bow);
	CheckEmbedded(*this, TEXT("Thug head"), InHead, Thug, Body, 34.f, FVector::ForwardVector);
	TestEqual(TEXT("Into the head: 3x"), Health->GetCurrentHealth(), 1000.f - 10.f * Bow->HeadshotMultiplier, 0.01f);

	// Through the capsule but past his head (26 cm to the side): stuck in the nearest shape, never a headshot.
	Health->SetMaxHealth(1000.f, true);
	const FVector Beside = Head + FVector(0.f, 26.f, 0.f);
	AArrowProjectile* Past = Shoot(TestWorld, Beside - FVector(1500.f, 0.f, 0.f), Beside, Standard, Bow);
	CheckEmbedded(*this, TEXT("Past his head"), Past, Thug, Body, 34.f, FVector::ForwardVector);
	TestTrue(TEXT("Passing the head is no headshot"), Health->GetCurrentHealth() > 1000.f - 10.f * Bow->HeadshotMultiplier + 0.01f);

	// Pickups still work: Kate within 150 cm of the one in his chest takes it back.
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(-120.f, 0.f, 0.f), FRotator::ZeroRotator));
	if (!Kate || !Kate->GetInventoryComponent() || !InChest)
	{
		AddError(TEXT("Could not spawn Kate for the pickup."));
		return false;
	}
	Kate->GetInventoryComponent()->AddArrows(Standard, 5);
	TestTrue(TEXT("Recovered from the thug within 150 cm"), InChest->TryRecoverBy(Kate));
	TestEqual(TEXT("Back in slot 1"), Kate->GetInventoryComponent()->GetArrowCount(1), 6);
	return true;
}

#endif
