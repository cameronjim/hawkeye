#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/BodyLook.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The body look (gameplay-semantics.md, "Kate's body look"): a soft mesh and retarget AnimBP drawn over a
 * hidden driver mesh that keeps every system. Content-free except the one case that needs a real skeletal
 * mesh, which uses the committed UE4 mannequin and is skipped (with a note) when it is not there.
 */
namespace HawkeyeBodyLookTest
{
	static const TCHAR* LookMeshPath = TEXT("/Game/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin");

	static AHawkeyeCharacter* Spawn(const FHawkeyeTestWorld& TestWorld, const FHawkeyeBodyLook& Look)
	{
		UWorld* World = TestWorld.Get();
		if (!World)
		{
			return nullptr;
		}
		const FTransform Where(FRotator::ZeroRotator, FVector(0.f, 0.f, 100.f));
		AHawkeyeCharacter* Kate = World->SpawnActorDeferred<AHawkeyeCharacter>(
			AHawkeyeCharacter::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Kate)
		{
			Kate->BodyLook = Look;
			Kate->FinishSpawning(Where);
		}
		return Kate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBodyLookNoneDrawsDriver, "Hawkeye.BodyLook.NoLookDrawsTheDriver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBodyLookNoneDrawsDriver::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = HawkeyeBodyLookTest::Spawn(TestWorld, FHawkeyeBodyLook());
	if (!TestNotNull(TEXT("Spawned"), Kate) || !TestNotNull(TEXT("Has a mesh"), Kate->GetMesh()))
	{
		return false;
	}
	TestFalse(TEXT("No look"), Kate->HasBodyLook());
	TestTrue(TEXT("The body people see is the driver"), Kate->GetBodyMesh() == Kate->GetMesh());
	TestTrue(TEXT("The driver is drawn"), Kate->GetMesh()->IsVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBodyLookMissingKeepsDriver, "Hawkeye.BodyLook.MissingContentKeepsTheDriver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBodyLookMissingKeepsDriver::RunTest(const FString& Parameters)
{
	// A clone without Paragon: the paths are written on BP_Kate, the packages are not on disk.
	FHawkeyeBodyLook Look;
	Look.Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/NoSuchLook/SK_Missing.SK_Missing")));
	Look.AnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/NoSuchLook/ABP_Missing.ABP_Missing_C")));
	USkeletalMesh* Mesh = nullptr;
	UClass* AnimClass = nullptr;
	TestFalse(TEXT("Does not resolve"), HawkeyeBodyLook::Resolve(Look, Mesh, AnimClass));
	TestTrue(TEXT("Resolves to nothing"), Mesh == nullptr && AnimClass == nullptr);

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = HawkeyeBodyLookTest::Spawn(TestWorld, Look);
	if (!TestNotNull(TEXT("Spawned"), Kate) || !TestNotNull(TEXT("Has a mesh"), Kate->GetMesh()))
	{
		return false;
	}
	TestFalse(TEXT("No look put on"), Kate->HasBodyLook());
	TestTrue(TEXT("The body people see is the driver"), Kate->GetBodyMesh() == Kate->GetMesh());
	TestTrue(TEXT("The driver is still drawn"), Kate->GetMesh()->IsVisible());
	TArray<USkeletalMeshComponent*> Meshes;
	Kate->GetComponents(Meshes);
	TestEqual(TEXT("No look component"), Meshes.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeBodyLookHidesDriver, "Hawkeye.BodyLook.LookIsDrawnOverAHiddenDriver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeBodyLookHidesDriver::RunTest(const FString& Parameters)
{
	USkeletalMesh* LookMesh =
		LoadObject<USkeletalMesh>(nullptr, HawkeyeBodyLookTest::LookMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!LookMesh)
	{
		AddInfo(TEXT("No UE4 mannequin in this clone; the look's attachment is not checked."));
		return true;
	}
	FHawkeyeBodyLook Look;
	Look.Mesh = LookMesh;
	Look.AnimClass = UAnimInstance::StaticClass();
	Look.HiddenBones = {FName(TEXT("hand_l")), FName(TEXT("no_such_bone"))};

	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = HawkeyeBodyLookTest::Spawn(TestWorld, Look);
	if (!TestNotNull(TEXT("Spawned"), Kate) || !TestNotNull(TEXT("Has a mesh"), Kate->GetMesh()))
	{
		return false;
	}
	USkeletalMeshComponent* Driver = Kate->GetMesh();
	USkeletalMeshComponent* Seen = Kate->GetBodyMesh();
	if (!TestTrue(TEXT("The look is on"), Kate->HasBodyLook()) || !TestNotNull(TEXT("The body people see"), Seen))
	{
		return false;
	}
	TestTrue(TEXT("It is not the driver"), Seen != Driver);
	TestTrue(TEXT("It wears the look's mesh"), Seen->GetSkeletalMeshAsset() == LookMesh);
	TestTrue(TEXT("It is under the driver"), Seen->GetAttachParent() == Driver);
	TestTrue(TEXT("At the driver's own transform"), Seen->GetRelativeTransform().Equals(FTransform::Identity, 0.01));
	TestTrue(TEXT("It runs the look's AnimBP"),
		Seen->GetAnimationMode() == EAnimationMode::AnimationBlueprint &&
			Seen->AnimClass == UAnimInstance::StaticClass());
	TestTrue(TEXT("It is drawn"), Seen->IsVisible());
	TestTrue(TEXT("It casts the shadow"), Seen->CastShadow);
	TestTrue(TEXT("It has no collision of its own"), Seen->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Its hidden bone is hidden"), Seen->IsBoneHiddenByName(TEXT("hand_l")));
	TestFalse(TEXT("The driver is not drawn"), Driver->IsVisible());
	TestTrue(TEXT("The driver still poses unseen"),
		Driver->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
	TestTrue(TEXT("The driver still collides"), Driver->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Applying again changes nothing"), Kate->ApplyBodyLook() == Seen);
	return true;
}

#endif
