#include "Player/InventoryComponent.h"

#include "Hawkeye.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowDefinition.h"
#include "Combat/WeaponComponent.h"
#include "Combat/WeaponDefinition.h"
#include "GameFramework/Actor.h"
#include "Mission/MissionDefinition.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	Arrows.SetNum(HawkeyeQuiverSlotCount);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureStandardSlot();
	if (bUseOwnStartingQuiver)
	{
		ApplyOwnStartingQuiver();
	}

	// Whatever the mission granted is already in by now; this makes sure the melee fallback is
	// pointed at Hands and the HUD paints the quiver once.
	ApplyHandsToWeapon();
	OnInventoryChanged.Broadcast();
}

int32 UInventoryComponent::SlotToIndex(int32 Slot)
{
	return (Slot >= 1 && Slot <= HawkeyeQuiverSlotCount) ? Slot - 1 : INDEX_NONE;
}

UWeaponDefinition* UInventoryComponent::GetHandsDefinition()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	if (!HandsDefinition.IsNull())
	{
		if (UWeaponDefinition* Loaded = HandsDefinition.LoadSynchronous())
		{
			return Loaded;
		}
	}

	if (!FallbackHands)
	{
		// Automation worlds have no content; the melee fallback still has to exist.
		FallbackHands = NewObject<UWeaponDefinition>(this, TEXT("FallbackHands"));
		FallbackHands->DisplayName = NSLOCTEXT("Hawkeye", "WeaponHands", "Fists");
		FallbackHands->ShortName = NSLOCTEXT("Hawkeye", "WeaponHandsShort", "Fists");
		FallbackHands->bIsMelee = true;
		FallbackHands->Damage = 15.f;
		FallbackHands->MagazineSize = 0;
		FallbackHands->DefaultReserve = 0;
	}
	return FallbackHands;
}

UArrowDefinition* UInventoryComponent::GetStandardArrowDefinition()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	if (!StandardArrowDefinition.IsNull())
	{
		if (UArrowDefinition* Loaded = StandardArrowDefinition.LoadSynchronous())
		{
			return Loaded;
		}
	}

	if (!FallbackStandardArrow)
	{
		// Same reason as FallbackHands: slot 1 is never empty, content or not.
		FallbackStandardArrow = NewObject<UArrowDefinition>(this, TEXT("FallbackStandardArrow"));
		FallbackStandardArrow->DisplayName = NSLOCTEXT("Hawkeye", "ArrowStandard", "Standard arrow");
		FallbackStandardArrow->ShortName = NSLOCTEXT("Hawkeye", "ArrowStandardShort", "Arrow");
		FallbackStandardArrow->Slot = 1;
		FallbackStandardArrow->Damage = 40.f;
		FallbackStandardArrow->Cap = 30;
		FallbackStandardArrow->bRecoverable = true;
		FallbackStandardArrow->ProjectileClass = AArrowProjectile::StaticClass();
	}
	return FallbackStandardArrow;
}

void UInventoryComponent::EnsureStandardSlot()
{
	if (Arrows.Num() != HawkeyeQuiverSlotCount)
	{
		Arrows.SetNum(HawkeyeQuiverSlotCount);
	}
	if (Arrows[0].Arrow)
	{
		return;
	}
	Arrows[0].Arrow = GetStandardArrowDefinition();
	Arrows[0].Count = 0;
}

void UInventoryComponent::ApplyHandsToWeapon()
{
	const AActor* Owner = GetOwner();
	UWeaponComponent* Weapon = Owner ? Owner->FindComponentByClass<UWeaponComponent>() : nullptr;
	if (Weapon && Weapon->GetActiveDefinition() == nullptr)
	{
		Weapon->SetActiveWeapon(GetHandsDefinition(), 0, 0);
	}
}

// --- Bow --------------------------------------------------------------------------------------------

bool UInventoryComponent::GiveBow(UBowDefinition* Definition)
{
	if (Bow == Definition)
	{
		return false;
	}
	Bow = Definition;
	UE_LOG(LogHawkeye, Log, TEXT("%s now carries bow %s."), *GetNameSafe(GetOwner()), *GetNameSafe(Definition));
	OnInventoryChanged.Broadcast();
	return true;
}

// --- Quiver -----------------------------------------------------------------------------------------

int32 UInventoryComponent::AddArrows(UArrowDefinition* Definition, int32 Count)
{
	const int32 Index = Definition ? SlotToIndex(Definition->Slot) : INDEX_NONE;
	if (Index == INDEX_NONE || Count < 0)
	{
		if (Definition)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: %s wants quiver slot %d, which does not exist."),
				*GetNameSafe(GetOwner()), *Definition->GetName(), Definition->Slot);
		}
		return 0;
	}

	EnsureStandardSlot();
	FHawkeyeQuiverSlot& Entry = Arrows[Index];
	if (Entry.Arrow != Definition)
	{
		if (Entry.Arrow && Entry.Arrow->Slot == Definition->Slot && Index != 0)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: %s replaces %s in quiver slot %d."), *GetNameSafe(GetOwner()),
				*Definition->GetName(), *Entry.Arrow->GetName(), Definition->Slot);
		}
		Entry.Arrow = Definition;
		Entry.Count = 0;
	}

	if (IsUnlimitedArrow(Definition))
	{
		// No count to add to: the slot is filled (held at its cap) and a pickup or a reward puts none in.
		Entry.Count = FMath::Max(GetCap(Definition), 1);
		OnInventoryChanged.Broadcast();
		return 0;
	}
	const int32 Before = Entry.Count;
	Entry.Count = FMath::Clamp(Entry.Count + Count, 0, FMath::Max(GetCap(Definition), 0));
	OnInventoryChanged.Broadcast();
	return Entry.Count - Before;
}

bool UInventoryComponent::ConsumeArrow(int32 Slot)
{
	const int32 Index = SlotToIndex(Slot);
	if (Index == INDEX_NONE || !Arrows.IsValidIndex(Index) || Arrows[Index].IsEmpty() || Arrows[Index].Count <= 0)
	{
		return false;
	}
	if (IsUnlimitedArrow(Arrows[Index].Arrow))
	{
		return true;
	}
	--Arrows[Index].Count;
	OnInventoryChanged.Broadcast();
	return true;
}

void UInventoryComponent::SetArrowCount(int32 Slot, int32 Count)
{
	const int32 Index = SlotToIndex(Slot);
	if (Index == INDEX_NONE || !Arrows.IsValidIndex(Index) || Arrows[Index].IsEmpty())
	{
		return;
	}
	Arrows[Index].Count = ClampCount(Arrows[Index].Arrow, Count);
	OnInventoryChanged.Broadcast();
}

FHawkeyeQuiverSlot UInventoryComponent::GetArrowSlot(int32 Slot) const
{
	const int32 Index = SlotToIndex(Slot);
	return Arrows.IsValidIndex(Index) ? Arrows[Index] : FHawkeyeQuiverSlot();
}

UArrowDefinition* UInventoryComponent::GetActiveArrow() const
{
	return GetArrowSlot(ActiveArrowSlot).Arrow;
}

int32 UInventoryComponent::FindArrowSlotByEffect(EArrowHitEffect Effect) const
{
	for (int32 Index = 0; Index < Arrows.Num(); ++Index)
	{
		if (Arrows[Index].Arrow && Arrows[Index].Arrow->OnHitEffect == Effect)
		{
			return Index + 1;
		}
	}
	return INDEX_NONE;
}

bool UInventoryComponent::SelectArrowSlot(int32 Slot)
{
	EnsureStandardSlot();
	if (SlotToIndex(Slot) == INDEX_NONE || IsArrowSlotEmpty(Slot))
	{
		// An empty slot holds nothing to nock; its key does nothing at all.
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: quiver slot %d is empty."), *GetNameSafe(GetOwner()), Slot);
		return false;
	}
	if (Slot == ActiveArrowSlot)
	{
		return false;
	}

	const int32 OldSlot = ActiveArrowSlot;
	ActiveArrowSlot = Slot;
	OnActiveArrowSlotChanged.Broadcast(OldSlot, ActiveArrowSlot);
	OnInventoryChanged.Broadcast();
	return true;
}

int32 UInventoryComponent::FindAdjacentArrowSlot(int32 Step) const
{
	const int32 Count = Arrows.Num();
	int32 Index = SlotToIndex(ActiveArrowSlot);
	if (Count <= 0 || Index == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	for (int32 Tried = 0; Tried < Count - 1; ++Tried)
	{
		Index = (Index + Step + Count) % Count;
		if (!Arrows[Index].IsEmpty())
		{
			return Index + 1;
		}
	}
	return INDEX_NONE;
}

bool UInventoryComponent::SelectNextArrowSlot()
{
	const int32 Slot = FindAdjacentArrowSlot(1);
	return Slot != INDEX_NONE && SelectArrowSlot(Slot);
}

bool UInventoryComponent::SelectPreviousArrowSlot()
{
	const int32 Slot = FindAdjacentArrowSlot(-1);
	return Slot != INDEX_NONE && SelectArrowSlot(Slot);
}

int32 UInventoryComponent::GetCap(const UArrowDefinition* Arrow) const
{
	if (!Arrow)
	{
		return 0;
	}
	return Arrow->Cap + (IsTrickArrow(Arrow) ? FMath::Max(TrickArrowCapBonus, 0) : 0);
}

bool UInventoryComponent::IsTrickArrow(const UArrowDefinition* Arrow)
{
	return Arrow && Arrow->OnHitEffect != EArrowHitEffect::None && Arrow->OnHitEffect != EArrowHitEffect::Grapple;
}

bool UInventoryComponent::IsUnlimitedArrow(const UArrowDefinition* Arrow)
{
	return Arrow && Arrow->OnHitEffect == EArrowHitEffect::Grapple;
}

// U+221E, the infinity sign: Roboto (the HUD's font) has it.
const TCHAR* UInventoryComponent::UnlimitedGlyph = TEXT("∞");

int32 UInventoryComponent::ClampCount(const UArrowDefinition* Arrow, int32 Count) const
{
	const int32 Cap = FMath::Max(GetCap(Arrow), 0);
	return IsUnlimitedArrow(Arrow) ? FMath::Max(Cap, 1) : FMath::Clamp(Count, 0, Cap);
}

FText UInventoryComponent::GetSlotCountText(int32 Slot) const
{
	const FHawkeyeQuiverSlot Entry = GetArrowSlot(Slot);
	if (Entry.IsEmpty())
	{
		return FText::GetEmpty();
	}
	if (IsUnlimitedArrow(Entry.Arrow))
	{
		return FText::FromString(UnlimitedGlyph);
	}
	// Standard arrows are the plentiful ones; a trick arrow's cap is small enough to be worth showing.
	return Slot == 1 ? FText::AsNumber(Entry.Count)
					 : FText::FromString(FString::Printf(TEXT("%d/%d"), Entry.Count, GetCap(Entry.Arrow)));
}

void UInventoryComponent::SetTrickArrowCapBonus(int32 Bonus)
{
	const int32 Clamped = FMath::Max(Bonus, 0);
	if (Clamped == TrickArrowCapBonus)
	{
		return;
	}
	TrickArrowCapBonus = Clamped;
	for (FHawkeyeQuiverSlot& Entry : Arrows)
	{
		if (Entry.Arrow)
		{
			Entry.Count = FMath::Min(Entry.Count, GetCap(Entry.Arrow));
		}
	}
	OnInventoryChanged.Broadcast();
}

int32 UInventoryComponent::RefillToCaps()
{
	int32 Added = 0;
	for (FHawkeyeQuiverSlot& Entry : Arrows)
	{
		const int32 Cap = Entry.Arrow ? GetCap(Entry.Arrow) : 0;
		if (Entry.Arrow && Entry.Count < Cap)
		{
			Added += Cap - Entry.Count;
			Entry.Count = Cap;
		}
	}
	if (Added > 0)
	{
		OnInventoryChanged.Broadcast();
	}
	return Added;
}

void UInventoryComponent::RestoreQuiver(UBowDefinition* InBow, const TArray<FHawkeyeQuiverSlot>& InArrows, int32 InActiveSlot)
{
	const int32 OldSlot = ActiveArrowSlot;
	Arrows.Reset();
	Arrows.SetNum(HawkeyeQuiverSlotCount);
	Bow = InBow;
	for (const FHawkeyeQuiverSlot& Saved : InArrows)
	{
		const int32 Index = Saved.Arrow ? SlotToIndex(Saved.Arrow->Slot) : INDEX_NONE;
		if (Index != INDEX_NONE)
		{
			// An unlimited slot's saved count (a save from before 2026-09-29 has one) is ignored.
			Arrows[Index].Arrow = Saved.Arrow;
			Arrows[Index].Count = ClampCount(Saved.Arrow, Saved.Count);
		}
	}
	EnsureStandardSlot();
	ApplyHandsToWeapon();
	const int32 ActiveIndex = SlotToIndex(InActiveSlot);
	ActiveArrowSlot = ActiveIndex != INDEX_NONE && !Arrows[ActiveIndex].IsEmpty() ? InActiveSlot : 1;
	if (OldSlot != ActiveArrowSlot)
	{
		OnActiveArrowSlotChanged.Broadcast(OldSlot, ActiveArrowSlot);
	}
	OnInventoryChanged.Broadcast();
}

// --- Mission ----------------------------------------------------------------------------------------

void UInventoryComponent::Clear()
{
	const int32 OldSlot = ActiveArrowSlot;

	Arrows.Reset();
	Arrows.SetNum(HawkeyeQuiverSlotCount);
	Keycards.Reset();
	Bow = StartingBow;
	ActiveArrowSlot = 1;

	for (const FHawkeyeQuiverSlot& Grant : StartingArrows)
	{
		const int32 Index = Grant.Arrow ? SlotToIndex(Grant.Arrow->Slot) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			continue;
		}
		Arrows[Index].Arrow = Grant.Arrow;
		Arrows[Index].Count = ClampCount(Grant.Arrow, Grant.Count);
	}
	EnsureStandardSlot();
	ApplyHandsToWeapon();

	if (OldSlot != ActiveArrowSlot)
	{
		OnActiveArrowSlotChanged.Broadcast(OldSlot, ActiveArrowSlot);
	}
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::ApplyStartingQuiver(UBowDefinition* InBow, const TArray<FHawkeyeQuiverSlot>& InArrows)
{
	StartingBow = InBow;
	StartingArrows.Reset();
	for (const FHawkeyeQuiverSlot& Grant : InArrows)
	{
		if (Grant.Arrow)
		{
			StartingArrows.Add(Grant);
		}
	}
	Clear();
}

void UInventoryComponent::ApplyMissionStart(const UMissionDefinition* Mission)
{
	if (!Mission)
	{
		return;
	}
	if (bUseOwnStartingQuiver)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: keeps its own quiver; mission %s's grant is for the lead."), *GetNameSafe(GetOwner()),
			*GetNameSafe(Mission));
		return;
	}

	// Soft references so a mission asset does not drag every arrow into memory until it starts.
	ApplyGrant(Mission->StartingBow, Mission->StartingArrows);
	UE_LOG(LogHawkeye, Log, TEXT("%s: mission %s grants bow %s and %d arrow type(s)."), *GetNameSafe(GetOwner()),
		*GetNameSafe(Mission), *GetNameSafe(Bow), StartingArrows.Num());
}

void UInventoryComponent::ApplyOwnStartingQuiver()
{
	ApplyGrant(OwnStartingBow, OwnStartingArrows);
	UE_LOG(LogHawkeye, Log, TEXT("%s: own quiver, bow %s and %d arrow type(s)."), *GetNameSafe(GetOwner()),
		*GetNameSafe(Bow), StartingArrows.Num());
}

void UInventoryComponent::ApplyGrant(const TSoftObjectPtr<UBowDefinition>& InBow, const TArray<FHawkeyeArrowGrant>& Grants)
{
	UBowDefinition* StartBow = InBow.IsNull() ? nullptr : InBow.LoadSynchronous();
	TArray<FHawkeyeQuiverSlot> Slots;
	for (const FHawkeyeArrowGrant& Grant : Grants)
	{
		if (UArrowDefinition* Arrow = Grant.Arrow.IsNull() ? nullptr : Grant.Arrow.LoadSynchronous())
		{
			FHawkeyeQuiverSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Arrow = Arrow;
			Slot.Count = Grant.Count;
		}
	}
	ApplyStartingQuiver(StartBow, Slots);
}

// --- Keycards ---------------------------------------------------------------------------------------

bool UInventoryComponent::HasKeycard(FName KeycardId) const
{
	return !KeycardId.IsNone() && Keycards.Contains(KeycardId);
}

bool UInventoryComponent::GiveKeycard(FName KeycardId)
{
	if (KeycardId.IsNone() || Keycards.Contains(KeycardId))
	{
		return false;
	}

	Keycards.Add(KeycardId);
	UE_LOG(LogHawkeye, Log, TEXT("%s picked up keycard '%s'."), *GetNameSafe(GetOwner()), *KeycardId.ToString());

	OnInventoryChanged.Broadcast();
	return true;
}

bool UInventoryComponent::NoteFirstPickup(const UArrowDefinition* Arrow)
{
	if (!Arrow || PickedUpTypes.Contains(Arrow->GetFName()))
	{
		return false;
	}
	PickedUpTypes.Add(Arrow->GetFName());
	return true;
}
