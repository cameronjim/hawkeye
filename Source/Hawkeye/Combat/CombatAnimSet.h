#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/StrikePose.h"
#include "CombatAnimSet.generated.h"

class UAnimMontage;

/** What a combat clip is for. A character's UCombatAnimSet has one montage slot per role. */
UENUM(BlueprintType)
enum class ECombatAnimRole : uint8
{
	None,
	/** The light chain: first, second, third (the ender). */
	Light1,
	Light2,
	Light3,
	/** Kate's held strike; a thug's bat, bash and slow swing. */
	Heavy,
	/** Stands in for Light3 when the set has no Light3 clip. */
	Kick,
	/** Kate's parry strike. */
	Parry,
	DodgeForward,
	DodgeBack,
	DodgeLeft,
	DodgeRight,
	/** Hit reactions, by the side the blow came from. */
	HitFront,
	HitBack,
	HitLeft,
	HitRight,
	/** Goes down and holds the last frame; GetUp stands him back up. */
	Knockdown,
	GetUp,
	FinisherAttacker,
	FinisherVictim,
	/** The finisher with the bow up: the sweep. */
	FinisherBow,
	/** Upper body: the draw, the held aim (loops), the release, the next arrow onto the string. */
	BowDraw,
	BowAimIdle,
	BowFire,
	BowNock,
	/** Getting up from lying face down (GetUp is from lying on the back). Appended: assets store the value. */
	GetUpFront,
	/** A heavy blow's stagger (two steps back), whichever side it came from. */
	HitHeavy
};

/**
 * More clips for one role, after the role's own slot: the slot is variant 0, these are 1, 2, ...
 * (claude-docs/gameplay-semantics.md, "Combat animation clips": strikes cycle through them per swing).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCombatAnimVariants
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	ECombatAnimRole Role = ECombatAnimRole::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	TArray<TSoftObjectPtr<UAnimMontage>> Montages;
};

/**
 * One character's combat clips (claude-docs/gameplay-semantics.md, "Combat animation clips").
 * Filled by Tools/Editor/create_combat_anims.py from the AM_<Role>_<Variant> montages the import
 * script made; every slot may be empty, and an empty slot means the procedural fallback.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UCombatAnimSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The soft montage for Role, with the one data fallback (Light3 uses Kick when it has none). */
	TSoftObjectPtr<UAnimMontage> GetMontage(ECombatAnimRole Role) const;

	/** GetMontage(Role), loaded. Null for an empty slot or one that does not load. */
	UFUNCTION(BlueprintCallable, Category = "Combat Anim")
	UAnimMontage* ResolveMontage(ECombatAnimRole Role) const;

	/** Set->ResolveMontage(Role), or null with no set. */
	static UAnimMontage* Resolve(const UCombatAnimSet* Set, ECombatAnimRole Role);

	/** True when Role (after its fallback) names a montage, loaded or not. */
	UFUNCTION(BlueprintPure, Category = "Combat Anim")
	bool HasMontage(ECombatAnimRole Role) const { return !GetMontage(Role).IsNull(); }

	/** Roles that name a montage, not counting fallbacks. */
	UFUNCTION(BlueprintPure, Category = "Combat Anim")
	int32 CountAssigned() const;

	/**
	 * How many clips Role has: its slot (after the Light3 fallback) plus its entry in MoreVariants.
	 * 0 for an empty role. A role whose slot is empty has no variants either.
	 */
	UFUNCTION(BlueprintPure, Category = "Combat Anim")
	int32 GetVariantCount(ECombatAnimRole Role) const;

	/** Variant Index of Role (0 is GetMontage(Role)); null past the end. */
	TSoftObjectPtr<UAnimMontage> GetVariant(ECombatAnimRole Role, int32 Index) const;

	/** GetVariant, loaded. Null for an index past the end or a clip that does not load. */
	UFUNCTION(BlueprintCallable, Category = "Combat Anim")
	UAnimMontage* ResolveVariant(ECombatAnimRole Role, int32 Index) const;

	/** Set->ResolveVariant(Role, Index), or null with no set (the static form of it). */
	static UAnimMontage* ResolveVariantIn(const UCombatAnimSet* Set, ECombatAnimRole Role, int32 Index);

	/**
	 * The variant to play next of Count, from Cursor (a per-role counter its user keeps), and moves the
	 * cursor on: 0, 1, ..., Count - 1, 0, ... in order, so no clip plays twice running when there are two
	 * or more. 0 for Count of 1 or less.
	 */
	static int32 PickNextVariant(int32& Cursor, int32 Count);

	/** Loads every assigned montage now and keeps it, so the first strike does not hitch. */
	void Preload();

	/** The hit reaction for a blow from Side. */
	static ECombatAnimRole HitRoleFor(EHawkeyeHitDirection Side);

	/**
	 * The hit reaction for a blow from Side at Weight (0 to 1): HitHeavy at HeavyWeight or more when Set has a HitHeavy
	 * clip, else HitRoleFor(Side) (gameplay-semantics.md, "Authored clips").
	 */
	static ECombatAnimRole HitRoleForBlow(const UCombatAnimSet* Set, EHawkeyeHitDirection Side, float Weight, float HeavyWeight);

	/** The get-up for a body on its back (GetUp) or face down (GetUpFront, or GetUp when the set has none). */
	static ECombatAnimRole GetUpRoleFor(const UCombatAnimSet* Set, bool bOnBack);

	/** The dodge for a dash toward Side of the one dodging (Front is a dash forward). */
	static ECombatAnimRole DodgeRoleFor(EHawkeyeHitDirection Side);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Strikes")
	TSoftObjectPtr<UAnimMontage> Light1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Strikes")
	TSoftObjectPtr<UAnimMontage> Light2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Strikes")
	TSoftObjectPtr<UAnimMontage> Light3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Strikes")
	TSoftObjectPtr<UAnimMontage> Heavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Strikes")
	TSoftObjectPtr<UAnimMontage> Kick;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Strikes")
	TSoftObjectPtr<UAnimMontage> Parry;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Dodge")
	TSoftObjectPtr<UAnimMontage> DodgeForward;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Dodge")
	TSoftObjectPtr<UAnimMontage> DodgeBack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Dodge")
	TSoftObjectPtr<UAnimMontage> DodgeLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Dodge")
	TSoftObjectPtr<UAnimMontage> DodgeRight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitFront;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitBack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitRight;

	/** Should hold its last frame (the import script turns auto blend-out off for it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> Knockdown;

	/** From lying on his back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> GetUp;

	/** From lying face down. Empty: GetUp is used for both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> GetUpFront;

	/** A heavy blow (impact weight at or over the reaction's heavy threshold). Empty: the side's Hit clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitHeavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Finisher")
	TSoftObjectPtr<UAnimMontage> FinisherAttacker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Finisher")
	TSoftObjectPtr<UAnimMontage> FinisherVictim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Finisher")
	TSoftObjectPtr<UAnimMontage> FinisherBow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Bow")
	TSoftObjectPtr<UAnimMontage> BowDraw;

	/** Should loop (the import script makes its one section loop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Bow")
	TSoftObjectPtr<UAnimMontage> BowAimIdle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Bow")
	TSoftObjectPtr<UAnimMontage> BowFire;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Bow")
	TSoftObjectPtr<UAnimMontage> BowNock;

	/**
	 * Further clips per role, in the order they cycle after the role's slot. Filled by
	 * create_combat_anims.py from every AM_<Role>_* the character wears, in manifest order.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Variants")
	TArray<FCombatAnimVariants> MoreVariants;

private:
	/** The slot for Role, no fallback. Null for None. */
	const TSoftObjectPtr<UAnimMontage>* FindSlot(ECombatAnimRole Role) const;

	/** The role whose clips Role plays: Role, or Kick for a Light3 with no slot of its own. */
	ECombatAnimRole SourceRole(ECombatAnimRole Role) const;

	/** Role's MoreVariants entry (no fallback), or null. */
	const FCombatAnimVariants* FindVariants(ECombatAnimRole Role) const;

	/** What Preload loaded, held so the soft references stay resident. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> Preloaded;
};
