#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHotbarWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The quiver rules from claude-docs/gameplay-semantics.md, "bow and arrows": standard arrows are
 * always slot 1, counts never pass a cap, number keys and the wheel pick filled slots only, and
 * the mission defines the starting bow and arrows. Definitions are built with NewObject.
 */
namespace HawkeyeInventoryTest
{
	static UArrowDefinition* MakeArrow(UObject* Outer, int32 Slot, int32 Cap, EArrowHitEffect Effect,
		const TCHAR* ShortName)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->Slot = Slot;
		Arrow->Cap = Cap;
		Arrow->OnHitEffect = Effect;
		Arrow->ShortName = FText::FromString(ShortName);
		Arrow->DisplayName = FText::FromString(ShortName);
		return Arrow;
	}

	static UArrowDefinition* MakeStandard(UObject* Outer)
	{
		return MakeArrow(Outer, 1, 30, EArrowHitEffect::None, TEXT("Arrow"));
	}

	static UArrowDefinition* MakeGrapple(UObject* Outer)
	{
		return MakeArrow(Outer, 2, 6, EArrowHitEffect::Grapple, TEXT("Grapple"));
	}

	static FHawkeyeQuiverSlot Grant(UArrowDefinition* Arrow, int32 Count)
	{
		FHawkeyeQuiverSlot Slot;
		Slot.Arrow = Arrow;
		Slot.Count = Count;
		return Slot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryStandardInSlotOne, "Hawkeye.Inventory.StandardAlwaysInSlotOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryStandardInSlotOne::RunTest(const FString& Parameters)
{
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	Inventory->Clear();

	TestEqual(TEXT("Slot 1 is active"), Inventory->GetActiveArrowSlot(), 1);
	TestFalse(TEXT("And holds standard arrows"), Inventory->IsArrowSlotEmpty(1));
	TestEqual(TEXT("Even at zero"), Inventory->GetArrowCount(1), 0);
	TestFalse(TEXT("No bow until one is granted"), Inventory->HasBow());
	for (int32 Slot = 2; Slot <= HawkeyeQuiverSlotCount; ++Slot)
	{
		TestTrue(FString::Printf(TEXT("Slot %d starts empty"), Slot), Inventory->IsArrowSlotEmpty(Slot));
	}
	TestTrue(TEXT("Out of range reads as empty"), Inventory->IsArrowSlotEmpty(7));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryEmptySlotIsNoOp, "Hawkeye.Inventory.EmptySlotIsNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryEmptySlotIsNoOp::RunTest(const FString& Parameters)
{
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	Inventory->Clear();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Inventory->OnActiveArrowSlotChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleActiveSlotChanged);

	TestFalse(TEXT("Key 3 on an empty slot does nothing"), Inventory->SelectArrowSlot(3));
	TestFalse(TEXT("Nor does a key that is no slot"), Inventory->SelectArrowSlot(9));
	TestFalse(TEXT("Nor the slot already active"), Inventory->SelectArrowSlot(1));
	TestEqual(TEXT("Still slot 1"), Inventory->GetActiveArrowSlot(), 1);
	TestEqual(TEXT("And nothing was broadcast"), Listener->ActiveSlotChangedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventorySelectionSkipsEmpty, "Hawkeye.Inventory.SelectionSkipsEmptySlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventorySelectionSkipsEmpty::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInventoryTest;
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	Inventory->Clear();
	Inventory->AddArrows(MakeArrow(Inventory, 4, 3, EArrowHitEffect::Putty, TEXT("Putty")), 3);
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Inventory->OnActiveArrowSlotChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleActiveSlotChanged);

	TestTrue(TEXT("Wheel up moves on"), Inventory->SelectNextArrowSlot());
	TestEqual(TEXT("Straight past the empty slots 2 and 3 to 4"), Inventory->GetActiveArrowSlot(), 4);
	TestEqual(TEXT("Broadcast old slot"), Listener->LastOldArrowSlot, 1);
	TestEqual(TEXT("And new slot"), Listener->LastNewArrowSlot, 4);
	TestTrue(TEXT("Wheel up again wraps"), Inventory->SelectNextArrowSlot());
	TestEqual(TEXT("Back to standard arrows"), Inventory->GetActiveArrowSlot(), 1);
	TestTrue(TEXT("Wheel down goes back"), Inventory->SelectPreviousArrowSlot());
	TestEqual(TEXT("To slot 4, not an empty one"), Inventory->GetActiveArrowSlot(), 4);
	TestTrue(TEXT("Key 1 picks standard arrows"), Inventory->SelectArrowSlot(1));
	TestEqual(TEXT("Four changes, each broadcast once"), Listener->ActiveSlotChangedCount, 4);

	UInventoryComponent* OnlyStandard = NewObject<UInventoryComponent>();
	OnlyStandard->Clear();
	TestFalse(TEXT("With only standard arrows the wheel does nothing"), OnlyStandard->SelectNextArrowSlot());
	TestFalse(TEXT("Either way"), OnlyStandard->SelectPreviousArrowSlot());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryCountsAndCaps, "Hawkeye.Inventory.CountsClampToCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryCountsAndCaps::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInventoryTest;
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	Inventory->Clear();
	UArrowDefinition* Standard = MakeStandard(Inventory);
	UArrowDefinition* Grapple = MakeGrapple(Inventory);

	TestEqual(TEXT("Forty offered, thirty fit"), Inventory->AddArrows(Standard, 40), 30);
	TestEqual(TEXT("Standard at its cap"), Inventory->GetArrowCount(1), 30);
	TestEqual(TEXT("A full slot takes nothing more"), Inventory->AddArrows(Standard, 1), 0);
	TestTrue(TEXT("The grapple has no count"), UInventoryComponent::IsUnlimitedArrow(Grapple));
	TestFalse(TEXT("Standard arrows do"), UInventoryComponent::IsUnlimitedArrow(Standard));
	TestEqual(TEXT("Grapple arrows fill their own slot but add no count"), Inventory->AddArrows(Grapple, 6), 0);
	TestFalse(TEXT("The slot is filled"), Inventory->IsArrowSlotEmpty(2));
	TestEqual(TEXT("Found by effect"), Inventory->FindArrowSlotByEffect(EArrowHitEffect::Grapple), 2);
	TestEqual(TEXT("No putty slot"), Inventory->FindArrowSlotByEffect(EArrowHitEffect::Putty), INDEX_NONE);

	const int32 Held = Inventory->GetArrowCount(2);
	for (int32 Shot = 0; Shot < 50; ++Shot)
	{
		Inventory->ConsumeArrow(2);
	}
	TestTrue(TEXT("Fifty grapple shots all fire"), Inventory->ConsumeArrow(2));
	TestEqual(TEXT("And spend nothing"), Inventory->GetArrowCount(2), Held);
	Inventory->SetArrowCount(2, 0);
	TestTrue(TEXT("It cannot be set to none"), Inventory->ConsumeArrow(2));
	TestEqual(TEXT("A pickup adds nothing"), Inventory->AddArrows(Grapple, 4), 0);

	Inventory->AddArrows(MakeArrow(Inventory, 3, 4, EArrowHitEffect::Putty, TEXT("Putty")), 1);
	TestTrue(TEXT("A trick arrow is spent"), Inventory->ConsumeArrow(3));
	TestEqual(TEXT("And counts down"), Inventory->GetArrowCount(3), 0);
	TestFalse(TEXT("None left, none spent"), Inventory->ConsumeArrow(3));
	TestFalse(TEXT("An empty slot spends nothing"), Inventory->ConsumeArrow(5));
	TestEqual(TEXT("The refill puts no grapple arrows in: 4 putty only"), Inventory->RefillToCaps(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryKeycardsLiveHere, "Hawkeye.Inventory.KeycardsLiveHere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryKeycardsLiveHere::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Kate || !Kate->GetInventoryComponent())
	{
		AddError(TEXT("Could not spawn a character with an inventory."));
		return false;
	}

	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	const FName RedCard(TEXT("red"));

	TestFalse(TEXT("No keycard to start with"), Kate->HasKeycard(RedCard));
	TestTrue(TEXT("The pawn's GiveKeycard forwards to the inventory"), Kate->GiveKeycard(RedCard));
	TestTrue(TEXT("The inventory holds it"), Inventory->HasKeycard(RedCard));
	TestFalse(TEXT("A second grant changes nothing"), Kate->GiveKeycard(RedCard));
	TestEqual(TEXT("One keycard on the ring"), Kate->GetKeycards().Num(), 1);

	Inventory->Clear();
	TestFalse(TEXT("Clearing the inventory takes the keycards with it"), Kate->HasKeycard(RedCard));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryClearResetsToStartingQuiver, "Hawkeye.Inventory.ClearResetsToStartingQuiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryClearResetsToStartingQuiver::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInventoryTest;
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	UBowDefinition* Bow = NewObject<UBowDefinition>(Inventory);
	UArrowDefinition* Standard = MakeStandard(Inventory);
	UArrowDefinition* Grapple = MakeGrapple(Inventory);

	Inventory->ApplyStartingQuiver(Bow, { Grant(Standard, 30), Grant(Grapple, 6) });
	TestTrue(TEXT("The chapter's bow is carried"), Inventory->GetBow() == Bow);

	Inventory->ConsumeArrow(1);
	Inventory->SelectArrowSlot(2);
	Inventory->AddArrows(MakeArrow(Inventory, 3, 3, EArrowHitEffect::Smoke, TEXT("Smoke")), 2);
	Inventory->GiveKeycard(FName(TEXT("red")));
	Inventory->GiveBow(nullptr);

	Inventory->Clear();

	TestTrue(TEXT("The bow comes back"), Inventory->GetBow() == Bow);
	TestEqual(TEXT("Standard arrows back to the grant"), Inventory->GetArrowCount(1), 30);
	TestFalse(TEXT("Grapple still carried"), Inventory->IsArrowSlotEmpty(2));
	TestTrue(TEXT("The smoke arrows found on the way are gone"), Inventory->IsArrowSlotEmpty(3));
	TestEqual(TEXT("Slot 1 active again"), Inventory->GetActiveArrowSlot(), 1);
	TestFalse(TEXT("And the keycards are gone"), Inventory->HasKeycard(FName(TEXT("red"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryMissionStartGrantsQuiver, "Hawkeye.Inventory.MissionStartGrantsQuiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryMissionStartGrantsQuiver::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInventoryTest;
	// CH01's shape: DA_Bow_Kate, 30 standard, 6 grapple. Built in code; tests never load content.
	UMissionDefinition* Chapter = NewObject<UMissionDefinition>();
	UBowDefinition* Bow = NewObject<UBowDefinition>(Chapter);
	Bow->FullDrawSeconds = 0.8f;
	UArrowDefinition* Standard = MakeStandard(Chapter);
	UArrowDefinition* Grapple = MakeGrapple(Chapter);
	Chapter->StartingBow = Bow;
	FHawkeyeArrowGrant StandardGrant;
	StandardGrant.Arrow = Standard;
	StandardGrant.Count = 30;
	FHawkeyeArrowGrant GrappleGrant;
	GrappleGrant.Arrow = Grapple;
	GrappleGrant.Count = 6;
	Chapter->StartingArrows.Add(StandardGrant);
	Chapter->StartingArrows.Add(GrappleGrant);

	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	Inventory->ApplyMissionStart(Chapter);

	TestTrue(TEXT("Kate starts with the chapter's bow"), Inventory->GetBow() == Bow);
	TestTrue(TEXT("Standard arrows in slot 1"), Inventory->GetArrowSlot(1).Arrow == Standard);
	TestEqual(TEXT("Thirty of them"), Inventory->GetArrowCount(1), 30);
	TestTrue(TEXT("Grapple arrows in slot 2"), Inventory->GetArrowSlot(2).Arrow == Grapple);
	TestTrue(TEXT("With no count: the grant's six is ignored"), Inventory->GetSlotCountText(2).ToString() == UInventoryComponent::UnlimitedGlyph);

	UMissionDefinition* Bare = NewObject<UMissionDefinition>();
	Inventory->ApplyMissionStart(Bare);
	TestFalse(TEXT("A chapter that grants nothing leaves her bowless"), Inventory->HasBow());
	TestFalse(TEXT("With slot 1 still standard arrows"), Inventory->IsArrowSlotEmpty(1));
	TestEqual(TEXT("At zero"), Inventory->GetArrowCount(1), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryHandsWithoutBow, "Hawkeye.Inventory.HandsWithoutBow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryHandsWithoutBow::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(
		AHawkeyeCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	UWeaponComponent* Weapon = Kate ? Kate->GetWeaponComponent() : nullptr;
	if (!Weapon || !Kate->GetInventoryComponent())
	{
		AddError(TEXT("Could not spawn a character with fists."));
		return false;
	}

	AActor* Target = TestWorld.SpawnActor(AActor::StaticClass(), FVector(80.f, 0.f, 0.f), FRotator::ZeroRotator);
	UHealthComponent* Health = Target ? NewObject<UHealthComponent>(Target) : nullptr;
	if (!Health)
	{
		AddError(TEXT("Could not spawn the punching bag."));
		return false;
	}
	Health->RegisterComponent();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Health->OnStaggered.AddDynamic(Listener, &UHawkeyeTestListener::HandleStaggered);

	TestFalse(TEXT("No bow"), Kate->GetInventoryComponent()->HasBow());
	TestTrue(TEXT("So left click is Hands"), Weapon->IsMelee());
	TestFalse(TEXT("Which is not a ranged weapon"), Weapon->HasWeapon());

	Weapon->SetTestTimeSeconds(100.0);
	TestTrue(TEXT("The punch goes out"), Weapon->Fire());
	Weapon->SetTestTimeSeconds(100.3);
	TestFalse(TEXT("Half the cooldown later the second punch is refused"), Weapon->Fire());
	Weapon->SetTestTimeSeconds(100.7);
	TestTrue(TEXT("A full cooldown later it lands"), Weapon->Fire());

	Health->ApplyMeleeDamage(Weapon->MeleeDamage, Kate, /*bStagger=*/true);
	TestEqual(TEXT("A punch takes MeleeDamage off"), Health->GetCurrentHealth(), 85.f);
	TestEqual(TEXT("And staggers once"), Listener->StaggeredCount, 1);
	Health->ApplyDamage(10.f, Kate);
	TestEqual(TEXT("A bullet never staggers"), Listener->StaggeredCount, 1);
	Health->Stagger(Kate);
	TestEqual(TEXT("An arrow hit staggers through the same path"), Listener->StaggeredCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryHotbarWidgetReflectsState, "Hawkeye.Inventory.HotbarWidgetReflectsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryHotbarWidgetReflectsState::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInventoryTest;
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	Inventory->ApplyStartingQuiver(NewObject<UBowDefinition>(Inventory),
		{ Grant(MakeStandard(Inventory), 30), Grant(MakeGrapple(Inventory), 6) });
	UHawkeyeHotbarWidget* Hotbar = NewObject<UHawkeyeHotbarWidget>();
	Hotbar->BindToInventory(Inventory);

	TestEqual(TEXT("Slot 1 is the active box"), Hotbar->GetActiveSlot(), 1);
	TestTrue(TEXT("And highlighted"), Hotbar->IsSlotActive(1));
	TestEqual(TEXT("Standard arrows show a bare count"), Hotbar->GetSlotCountText(1).ToString(), FString(TEXT("30")));
	TestEqual(TEXT("Grapple arrows show an infinity, not count and cap"), Hotbar->GetSlotCountText(2).ToString(),
		FString(UInventoryComponent::UnlimitedGlyph));
	Inventory->AddArrows(MakeArrow(Inventory, 4, 4, EArrowHitEffect::Bola, TEXT("Bola")), 2);
	TestEqual(TEXT("A trick arrow shows count and cap"), Hotbar->GetSlotCountText(4).ToString(), FString(TEXT("2/4")));
	TestEqual(TEXT("Under their short name"), Hotbar->GetSlotNameText(2).ToString(), FString(TEXT("Grapple")));
	TestEqual(TEXT("Labels are the number keys"), Hotbar->GetSlotKeyText(6).ToString(), FString(TEXT("6")));
	TestTrue(TEXT("Slot 3 is empty"), Hotbar->IsSlotEmpty(3));
	TestTrue(TEXT("An empty box shows no count"), Hotbar->GetSlotCountText(3).IsEmpty());
	TestEqual(TEXT("Empty boxes are dimmed alike"), Hotbar->GetSlotColor(3), Hotbar->GetSlotColor(6));

	Inventory->SelectArrowSlot(4);
	Inventory->ConsumeArrow(4);
	Inventory->ConsumeArrow(2);
	TestTrue(TEXT("The widget follows the inventory"), Hotbar->IsSlotActive(4) && !Hotbar->IsSlotActive(1));
	TestEqual(TEXT("And the count"), Hotbar->GetSlotCountText(4).ToString(), FString(TEXT("1/4")));
	TestEqual(TEXT("A grapple shot leaves the infinity"), Hotbar->GetSlotCountText(2).ToString(), FString(UInventoryComponent::UnlimitedGlyph));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInventoryOldSaveGrappleCount, "Hawkeye.Inventory.SavedGrappleCountIsIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInventoryOldSaveGrappleCount::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInventoryTest;
	// A save from before 2026-09-29 carries a grapple count (3 of 6 here, or 0 when she had run out); it still
	// loads, the grapple comes back with no count, and the trick arrows keep theirs.
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	UArrowDefinition* Standard = MakeStandard(Inventory);
	UArrowDefinition* Grapple = MakeGrapple(Inventory);
	UArrowDefinition* Smoke = MakeArrow(Inventory, 5, 3, EArrowHitEffect::Smoke, TEXT("Smoke"));
	for (const int32 SavedGrapple : {3, 0})
	{
		TArray<FHawkeyeQuiverSlot> Saved;
		Saved.Add(Grant(Standard, 12));
		Saved.Add(Grant(Grapple, SavedGrapple));
		Saved.Add(Grant(Smoke, 1));
		Inventory->RestoreQuiver(nullptr, Saved, 2);
		TestEqual(TEXT("Standard count restored"), Inventory->GetArrowCount(1), 12);
		TestTrue(TEXT("The grapple slot is back"), Inventory->GetArrowSlot(2).Arrow == Grapple);
		TestEqual(TEXT("With no count"), Inventory->GetSlotCountText(2).ToString(), FString(UInventoryComponent::UnlimitedGlyph));
		TestEqual(TEXT("Still nocked, even from a save that had run out"), Inventory->GetActiveArrowSlot(), 2);
		TestTrue(TEXT("And it fires"), Inventory->ConsumeArrow(2));
		TestEqual(TEXT("Smoke keeps its count"), Inventory->GetArrowCount(5), 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
