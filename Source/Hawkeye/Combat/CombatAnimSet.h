#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/HitContactRules.h"
#include "Combat/StrikePose.h"
#include "Traversal/TraversalPoseRules.h"
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
	HitHeavy,
	/** The stealth takedown from behind, as a pair: the one doing it and the one it is done to. */
	TakedownAttacker,
	TakedownVictim,
	/**
	 * Hit reactions by where the blow landed (the struck bone's region) and how hard, from a blow in front; each starts on
	 * its damaged pose (the clip's HitPeakTime). Without one the side's clip plays. Appended: assets store the value.
	 */
	HitHeadLight,
	HitHeadHeavy,
	HitGutLight,
	HitGutHeavy,
	HitLegsLight,
	HitLegsHeavy,
	/**
	 * Landings (the Game Animation Sample's M_Neutral_Jump_F_Land_*, root motion taken out; ELandingClip): light or heavy by
	 * impact speed, stand, walk, run or sprint by the speed along the ground. Variants alternate the foot she lands on.
	 */
	LandStandLight,
	LandStandHeavy,
	LandWalkLight,
	LandWalkHeavy,
	LandRunLight,
	LandRunHeavy,
	LandSprintLight,
	LandSprintHeavy,
	/** The roll and the stumble a landing's fall height chose (the landing rules, not the speed). */
	LandRoll,
	LandStumble,
	/** Should loop: the fall after a release. */
	FallLoop,
	/**
	 * The ledge set (Tools/Blender/build_clips.py, "Ledge clips"; UParkourComponent plays them): turning and lowering over a
	 * roof edge to the hang, the catch of a ledge while falling or jumping, the hang itself (loops), the let-go, the climb up
	 * from a hang, and a vault's one-hand plant. Appended: assets store the value.
	 */
	LedgeDropToHang,
	LedgeCatch,
	/** Should loop: a hang on both hands. */
	LedgeHangIdle,
	LedgeHangDrop,
	LedgeClimbUp,
	LedgeVaultPlant
};

/** The last role: loops over every slot run to here. */
constexpr ECombatAnimRole LastCombatAnimRole = ECombatAnimRole::LedgeVaultPlant;

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
 * What one clip says about itself that its montage cannot: baked by the content scripts from the clip's own frames
 * (Tools/Data/Anims/manifest.json, or the authored clip's measures) into UCombatAnimSet::ClipData, looked up by montage
 * (claude-docs/gameplay-semantics.md, "The weld"). -1 in a field: unknown, the rule's fallback applies.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCombatClipData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	TSoftObjectPtr<UAnimMontage> Montage;

	/**
	 * A reaction clip: seconds into the montage of its damaged pose, where it starts (with no blend) when the blow lands, so
	 * the victim pops onto the hit pose and holds it through the freeze. -1: starts at 0 and blends in a little.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	float HitPeakTime = -1.f;

	/**
	 * A strike clip: the flat distance from the striker's capsule centre to the struck region's bone at the clip's hit
	 * frame, cm, in the game's units (the rig's scale is in it): the distance the warp stands him at. -1: the attack's own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	float ContactDistance = -1.f;

	/** A strike clip: the striking limb's height over the floor at the hit frame, cm. Picks the region the blow is aimed at. -1: the attack's own region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	float ContactHeight = -1.f;

	/** A reaction clip: how far its capsule is knocked back, cm: the shove its feet were authored to step back onto. -1: the weld tuning's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	float KnockbackCm = -1.f;

	/** A strike clip that sweeps in from the side (a hook, a roundhouse): the variant choice prefers it for a target off to the side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim")
	bool bWideSwing = false;

	bool HasHitPeak() const { return HitPeakTime >= 0.f; }
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

	/** The data baked for Montage (a role's clip, or one of its variants), or null when there is none. */
	const FCombatClipData* FindClipData(const UAnimMontage* Montage) const;

	/** The same by the montage's path, so a variant that is not loaded can be looked up. */
	const FCombatClipData* FindClipDataByPath(const FSoftObjectPath& Path) const;

	/** The hit reaction for a blow from Side. */
	static ECombatAnimRole HitRoleFor(EHawkeyeHitDirection Side);

	/**
	 * The hit reaction for a blow from Side that struck Region at Weight: from the front, the head, gut or legs clip of the
	 * blow's weight (heavy at HeavyWeight or more) when Set has it; otherwise HitRoleForBlow (the side's clip, or the heavy
	 * stagger). A blow from the side or behind always plays the side's clip: the zone clips are authored facing the blow.
	 */
	static ECombatAnimRole HitRoleForImpact(const UCombatAnimSet* Set, EHawkeyeHitDirection Side, EHawkeyeContactRegion Region,
		float Weight, float HeavyWeight);

	/** The zone role for Region at the given weight class. */
	static ECombatAnimRole ZoneHitRole(EHawkeyeContactRegion Region, bool bHeavy);

	/** True for a hit reaction that is a heavy blow's: the heavy stagger, or a zone clip of the heavy class. */
	static bool IsHeavyHitRole(ECombatAnimRole Role);

	/**
	 * The hit reaction for a blow from Side at Weight (0 to 1): HitHeavy at HeavyWeight or more when Set has a HitHeavy
	 * clip, else HitRoleFor(Side) (gameplay-semantics.md, "Authored clips").
	 */
	static ECombatAnimRole HitRoleForBlow(const UCombatAnimSet* Set, EHawkeyeHitDirection Side, float Weight, float HeavyWeight);

	/** The get-up for a body on its back (GetUp) or face down (GetUpFront, or GetUp when the set has none). */
	static ECombatAnimRole GetUpRoleFor(const UCombatAnimSet* Set, bool bOnBack);

	/** The role of a landing clip (None for ELandingClip::None). */
	static ECombatAnimRole LandingRoleFor(ELandingClip Clip);

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

	/** The stealth takedown's two halves, authored together; both must play or neither does. */
	/** Hit reactions by zone and weight, from a blow in front (the head snaps, the gut folds, the legs buckle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitHeadLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitHeadHeavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitGutLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitGutHeavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitLegsLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Hit")
	TSoftObjectPtr<UAnimMontage> HitLegsHeavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Takedown")
	TSoftObjectPtr<UAnimMontage> TakedownAttacker;

	/** Should hold its last frame (he is handed to the ragdoll from it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Takedown")
	TSoftObjectPtr<UAnimMontage> TakedownVictim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandStandLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandStandHeavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandWalkLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandWalkHeavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandRunLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandRunHeavy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandSprintLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandSprintHeavy;

	/** Plays in place of the scripted 360 degree spin; the capsule shrink and the 200 cm of travel stay the roll's own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandRoll;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> LandStumble;

	/** Should loop (the montage's one section loops). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Landing")
	TSoftObjectPtr<UAnimMontage> FallLoop;

	/** Turning round at a roof edge and lowering over it to the hang (it takes the drop to hang's time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Ledge")
	TSoftObjectPtr<UAnimMontage> LedgeDropToHang;

	/** Reaching up, the hands taking the edge, the body swinging in under them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Ledge")
	TSoftObjectPtr<UAnimMontage> LedgeCatch;

	/** Should loop (the import script makes its one section loop): hanging on both hands, a small sway. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Ledge")
	TSoftObjectPtr<UAnimMontage> LedgeHangIdle;

	/** Letting go: a push off the wall, the legs ready for the landing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Ledge")
	TSoftObjectPtr<UAnimMontage> LedgeHangDrop;

	/** From the hang to standing on top: pull, chest over, knee up, stand (it takes the climb's time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Ledge")
	TSoftObjectPtr<UAnimMontage> LedgeClimbUp;

	/** A vault with one hand planted on the top and the legs swung to the side. Not wired to a move by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Ledge")
	TSoftObjectPtr<UAnimMontage> LedgeVaultPlant;

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

	/** What each clip says about itself (its hit pose, its contact distance and height, its knock-back); filled by create_combat_anims.py. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat Anim|Clip Data")
	TArray<FCombatClipData> ClipData;

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
