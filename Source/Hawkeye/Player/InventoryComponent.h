#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/ArrowDefinition.h"
#include "InventoryComponent.generated.h"

class UBowDefinition;
class UMissionDefinition;
class UWeaponComponent;
class UWeaponDefinition;

/** One quiver slot: which arrow type it holds and how many are left. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeQuiverSlot
{
	GENERATED_BODY()

	/** The arrow type in this slot, or null while the slot is empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UArrowDefinition> Arrow = nullptr;

	/**
	 * Arrows left, 0..Arrow->Cap. An unlimited type (the grapple, UInventoryComponent::IsUnlimitedArrow) has no
	 * count: it is held at its cap while the slot is filled so every "any left?" check passes, and never spent.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 Count = 0;

	bool IsEmpty() const { return Arrow == nullptr; }
};

/** Fired whenever the bow, an arrow count or the keycards change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChangedSignature);

/** Fired when the active quiver slot changes. Slots are 1..7. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActiveArrowSlotChangedSignature, int32, OldSlot, int32, NewSlot);

/**
 * What Kate carries: a bow (or none), a seven-slot quiver, and a keycard ring.
 *
 * Rules (claude-docs/gameplay-semantics.md, "bow and arrows"): standard arrows are always in
 * slot 1, present even at zero; counts never exceed an arrow's Cap; the grapple arrow has no count
 * at all (IsUnlimitedArrow: never spent, never added to, shown as an infinity); number keys pick a slot and
 * the wheel steps through the filled ones, skipping empty slots. Without a bow, left click is
 * Hands, the melee fallback on the owner's UWeaponComponent. Clear() goes back to what the mission
 * granted (StartingBow and StartingArrows); nothing carries between missions.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	/** DA_Weapon_Hands: the punch used while no bow is owned. A transient stand-in when unset (tests). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TSoftObjectPtr<UWeaponDefinition> HandsDefinition;

	/** DA_Arrow_Standard: what fills slot 1 when nothing else has. A transient stand-in when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TSoftObjectPtr<UArrowDefinition> StandardArrowDefinition;

	/** The bow Clear() hands back. Set from the mission by ApplyStartingQuiver. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UBowDefinition> StartingBow = nullptr;

	/** The arrows Clear() hands back, one entry per type. Set from the mission by ApplyStartingQuiver. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FHawkeyeQuiverSlot> StartingArrows;

	/**
	 * A character with a quiver of their own (Clint): when set, BeginPlay grants OwnStartingBow and
	 * OwnStartingArrows and the chapter's grant (which is Kate's) is ignored by ApplyMissionStart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	bool bUseOwnStartingQuiver = false;

	/** The bow granted at BeginPlay when bUseOwnStartingQuiver is set (DA_Bow_Clint on BP_Clint). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TSoftObjectPtr<UBowDefinition> OwnStartingBow;

	/** The arrows granted at BeginPlay when bUseOwnStartingQuiver is set, one entry per type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FHawkeyeArrowGrant> OwnStartingArrows;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryChangedSignature OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnActiveArrowSlotChangedSignature OnActiveArrowSlotChanged;

	// --- Bow --------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Inventory|Bow")
	UBowDefinition* GetBow() const { return Bow; }

	UFUNCTION(BlueprintPure, Category = "Inventory|Bow")
	bool HasBow() const { return Bow != nullptr; }

	/** Kate now carries Definition (null takes the bow away). Returns true when it changed. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Bow")
	bool GiveBow(UBowDefinition* Definition);

	// --- Quiver -----------------------------------------------------------------------------------

	/**
	 * Adds Count arrows of Definition to its slot, filling the slot if it was empty (and replacing
	 * a different type that was there). Clamped to the arrow's Cap. Returns how many went in. For an
	 * unlimited type it only fills the slot and always returns 0 (a grapple pickup is a no-op).
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	int32 AddArrows(UArrowDefinition* Definition, int32 Count);

	/**
	 * Takes one arrow from Slot. False (and nothing changes) when the slot is empty or at zero. An unlimited
	 * slot always gives one and keeps its count.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	bool ConsumeArrow(int32 Slot);

	/** Makes Slot (1..7) active. Does nothing for an empty or out-of-range slot or the active one. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	bool SelectArrowSlot(int32 Slot);

	/** Next filled slot, wrapping. Mouse wheel up. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	bool SelectNextArrowSlot();

	/** Previous filled slot, wrapping. Mouse wheel down. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	bool SelectPreviousArrowSlot();

	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	int32 GetActiveArrowSlot() const { return ActiveArrowSlot; }

	/** The arrow type in the active slot. Standard arrows unless another slot was picked. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	UArrowDefinition* GetActiveArrow() const;

	/** Slot's contents (1..7). Out of range returns an empty slot. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	FHawkeyeQuiverSlot GetArrowSlot(int32 Slot) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	bool IsArrowSlotEmpty(int32 Slot) const { return GetArrowSlot(Slot).IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	int32 GetArrowCount(int32 Slot) const { return GetArrowSlot(Slot).Count; }

	/** The first slot holding an arrow with Effect, or INDEX_NONE. The grapple uses this. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	int32 FindArrowSlotByEffect(EArrowHitEffect Effect) const;

	/**
	 * The most of Arrow the quiver carries: its Cap, plus TrickArrowCapBonus for a trick arrow (Story's
	 * +2). Every clamp and every "n/cap" on screen reads this.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	int32 GetCap(const UArrowDefinition* Arrow) const;

	/** A trick arrow: one that does something on landing beyond damage, other than the grapple. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	static bool IsTrickArrow(const UArrowDefinition* Arrow);

	/**
	 * An arrow type with no count (revised 2026-09-29, "grapples should be infinite"): the grapple, whose
	 * arrow is traversal rather than ammunition. Its slot shows an infinity, it is never spent, pickups
	 * and refills add none, Story's extra cap does not apply and the save's count for it is ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	static bool IsUnlimitedArrow(const UArrowDefinition* Arrow);

	/** The count text for Slot: "30" for slot 1, "count/cap" for a trick arrow, the infinity glyph for an unlimited one, empty for an empty slot. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	FText GetSlotCountText(int32 Slot) const;

	/** The infinity glyph an unlimited slot shows in place of a count. */
	static const TCHAR* UnlimitedGlyph;

	/** Sets the difficulty's extra trick arrows; counts over the new caps come down to them. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	void SetTrickArrowCapBonus(int32 Bonus);

	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	int32 GetTrickArrowCapBonus() const { return TrickArrowCapBonus; }

	/** Sets Slot's count directly, clamped to its Cap. Debug and tests. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	void SetArrowCount(int32 Slot, int32 Count);

	/**
	 * Every filled slot back up to its arrow's Cap: the safehouse restock. Empty slots stay empty
	 * (a trick arrow has to be found before it can be refilled). Returns how many arrows went in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Quiver")
	int32 RefillToCaps();

	/** Arrows added to every trick arrow's cap (the difficulty). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Quiver")
	int32 TrickArrowCapBonus = 0;

	/** All seven slots, index 0 being slot 1. The save reads this. */
	const TArray<FHawkeyeQuiverSlot>& GetArrowSlots() const { return Arrows; }

	/**
	 * Loading a save: replaces the bow, every slot and the active slot wholesale. Counts clamp to
	 * caps; standard arrows are put back in slot 1 if the save had none; an empty active slot falls
	 * back to slot 1.
	 */
	void RestoreQuiver(UBowDefinition* InBow, const TArray<FHawkeyeQuiverSlot>& InArrows, int32 InActiveSlot);

	// --- Mission ----------------------------------------------------------------------------------

	/** Drops everything and rebuilds StartingBow and StartingArrows, slot 1 active. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void Clear();

	/** Replaces the starting bow and arrows and rebuilds from them. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ApplyStartingQuiver(UBowDefinition* InBow, const TArray<FHawkeyeQuiverSlot>& Arrows);

	/**
	 * Loads Mission's StartingBow and StartingArrows and applies them. AHawkeyeGameMode calls this on
	 * mission start. Does nothing with bUseOwnStartingQuiver: that character keeps their own grant.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ApplyMissionStart(const UMissionDefinition* Mission);

	/** Loads Bow and Grants (soft references) and applies them as the starting quiver. */
	void ApplyGrant(const TSoftObjectPtr<UBowDefinition>& InBow, const TArray<FHawkeyeArrowGrant>& Grants);

	/** Grants OwnStartingBow and OwnStartingArrows. BeginPlay calls it with bUseOwnStartingQuiver; public for tests. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ApplyOwnStartingQuiver();

	/**
	 * True the first time Arrow is picked up this session, false after: the "Trickshot's arrow" toast
	 * shows once.
	 */
	bool NoteFirstPickup(const UArrowDefinition* Arrow);

	/** Whether Arrow has been picked up this session. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Quiver")
	bool HasPickedUp(const UArrowDefinition* Arrow) const { return Arrow && PickedUpTypes.Contains(Arrow->GetFName()); }

	// --- Keycards ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Inventory|Keycards")
	bool HasKeycard(FName KeycardId) const;

	/** Adds a keycard to the ring. Returns false when she already had it. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Keycards")
	bool GiveKeycard(FName KeycardId);

	UFUNCTION(BlueprintPure, Category = "Inventory|Keycards")
	TSet<FName> GetKeycards() const { return Keycards; }

	// --- Hands ------------------------------------------------------------------------------------

	/** Hands, loaded or made on demand. Never null outside the class default object. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	UWeaponDefinition* GetHandsDefinition();

	/** Standard arrows, loaded or made on demand. Never null outside the class default object. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	UArrowDefinition* GetStandardArrowDefinition();

protected:
	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	//~ End UActorComponent interface

	/** Puts standard arrows in slot 1 (at zero) if nothing is there. Safe to call repeatedly. */
	void EnsureStandardSlot();

	/** Points the owner's weapon component at Hands, the melee fallback. */
	void ApplyHandsToWeapon();

	/** The next filled slot Step away from the active one, or INDEX_NONE. Slots are 1-based. */
	int32 FindAdjacentArrowSlot(int32 Step) const;

	/** Slot 1..7 to an array index, or INDEX_NONE. */
	static int32 SlotToIndex(int32 Slot);

	/** Count clamped to Arrow's cap; an unlimited type is held at its cap (at least 1) whatever Count says. */
	int32 ClampCount(const UArrowDefinition* Arrow, int32 Count) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Bow")
	TObjectPtr<UBowDefinition> Bow = nullptr;

	/** Always HawkeyeQuiverSlotCount long; index 0 is slot 1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Quiver")
	TArray<FHawkeyeQuiverSlot> Arrows;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Quiver")
	int32 ActiveArrowSlot = 1;

	/** Keycard ids collected so far. Doors check this by id. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Keycards")
	TSet<FName> Keycards;

	/** Arrow types picked up from the world this session, by asset name. */
	UPROPERTY(Transient)
	TSet<FName> PickedUpTypes;

	UPROPERTY(Transient)
	TObjectPtr<UWeaponDefinition> FallbackHands = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UArrowDefinition> FallbackStandardArrow = nullptr;
};
