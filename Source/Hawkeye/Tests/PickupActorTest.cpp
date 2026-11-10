#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/Interactable.h"
#include "World/PickupActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyePickupTest
{
	static APickupActor* SpawnPickup(const FHawkeyeTestWorld& TestWorld, EPickupType Type)
	{
		APickupActor* Pickup = Cast<APickupActor>(
			TestWorld.SpawnActor(APickupActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Pickup)
		{
			Pickup->PickupType = Type;
		}
		return Pickup;
	}

	static AHawkeyeCharacter* SpawnPlayer(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePickupBowGivesTheBow, "Hawkeye.Pickup.BowGivesTheBow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePickupBowGivesTheBow::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Player = HawkeyePickupTest::SpawnPlayer(TestWorld);
	APickupActor* Pickup = HawkeyePickupTest::SpawnPickup(TestWorld, EPickupType::Bow);
	UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	if (!Inventory || !Pickup)
	{
		AddError(TEXT("Failed to spawn the player or the pickup."));
		return false;
	}

	TestFalse(TEXT("Kate starts without a bow"), Inventory->HasBow());
	TestFalse(TEXT("A bow pickup with no bow set applies nothing"), Pickup->ApplyTo(Player));

	UBowDefinition* Bow = NewObject<UBowDefinition>(Pickup);
	Pickup->Bow = Bow;
	TestTrue(TEXT("The pickup applies"), Pickup->ApplyTo(Player));
	TestTrue(TEXT("She carries that bow"), Inventory->GetBow() == Bow);
	TestTrue(TEXT("The pickup destroys itself"), Pickup->IsActorBeingDestroyed() || !IsValid(Pickup));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePickupKeycardJoinsTheRing, "Hawkeye.Pickup.KeycardJoinsTheRing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePickupKeycardJoinsTheRing::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Player = HawkeyePickupTest::SpawnPlayer(TestWorld);
	APickupActor* Pickup = HawkeyePickupTest::SpawnPickup(TestWorld, EPickupType::Keycard);
	if (!Player || !Pickup)
	{
		AddError(TEXT("Failed to spawn the player or the pickup."));
		return false;
	}

	const FName RedCard(TEXT("red"));
	Pickup->KeycardId = RedCard;

	TestFalse(TEXT("No keycard to start with"), Player->HasKeycard(RedCard));
	TestTrue(TEXT("The pickup applies"), Pickup->ApplyTo(Player));
	TestTrue(TEXT("The keycard is on the ring"), Player->HasKeycard(RedCard));
	TestFalse(TEXT("Other ids are unaffected"), Player->HasKeycard(FName(TEXT("infirmary"))));
	TestFalse(TEXT("A second grant of the same id changes nothing"), Player->GiveKeycard(RedCard));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePickupArrowsFillTheQuiver, "Hawkeye.Pickup.ArrowsFillTheQuiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePickupArrowsFillTheQuiver::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Player = HawkeyePickupTest::SpawnPlayer(TestWorld);
	APickupActor* Pickup = HawkeyePickupTest::SpawnPickup(TestWorld, EPickupType::Arrows);
	UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	if (!Inventory || !Pickup)
	{
		AddError(TEXT("Failed to spawn the player or the pickup."));
		return false;
	}

	UArrowDefinition* Putty = NewObject<UArrowDefinition>(Pickup);
	Putty->Slot = 3;
	Putty->Cap = 4;
	Putty->OnHitEffect = EArrowHitEffect::Putty;
	Inventory->AddArrows(Putty, 2);

	Pickup->Arrow = Putty;
	Pickup->ArrowCount = 6;
	TestTrue(TEXT("The pickup applies"), Pickup->ApplyTo(Player));
	TestEqual(TEXT("Topped up to the cap, not past it"), Inventory->GetArrowCount(3), 4);

	// A grapple pickup is a no-op on the count: grapple arrows have none.
	UArrowDefinition* Grapple = NewObject<UArrowDefinition>(Player);
	Grapple->Slot = 2;
	Grapple->Cap = 6;
	Grapple->OnHitEffect = EArrowHitEffect::Grapple;
	Inventory->AddArrows(Grapple, 0);
	const FString Before = Inventory->GetSlotCountText(2).ToString();
	APickupActor* GrapplePickup = HawkeyePickupTest::SpawnPickup(TestWorld, EPickupType::Arrows);
	if (GrapplePickup)
	{
		GrapplePickup->Arrow = Grapple;
		GrapplePickup->ArrowCount = 6;
		GrapplePickup->ApplyTo(Player);
	}
	TestEqual(TEXT("A grapple pickup changes nothing"), Inventory->GetSlotCountText(2).ToString(), Before);
	TestEqual(TEXT("Which is the infinity"), Before, FString(UInventoryComponent::UnlimitedGlyph));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePickupIgnoresNonPlayers, "Hawkeye.Pickup.IgnoresNonPlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyePickupIgnoresNonPlayers::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	APickupActor* Pickup = HawkeyePickupTest::SpawnPickup(TestWorld, EPickupType::Keycard);
	AActor* Bystander = TestWorld.SpawnActor(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Pickup || !Bystander)
	{
		AddError(TEXT("Failed to spawn the test actors."));
		return false;
	}

	TestFalse(TEXT("A plain actor cannot interact"), IInteractable::Execute_CanInteract(Pickup, Bystander));
	TestFalse(TEXT("And applying to one does nothing"), Pickup->ApplyTo(Bystander));
	TestTrue(TEXT("The pickup is still there"), IsValid(Pickup) && !Pickup->IsActorBeingDestroyed());

	return true;
}

#endif
