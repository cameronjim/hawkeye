#pragma once

#include "CoreMinimal.h"
#include "Combat/StrikePose.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HitContactRules.generated.h"

/** Where on the body a strike is aimed (gameplay-semantics.md, "Contact, reaction and knockout"). */
UENUM(BlueprintType)
enum class EHawkeyeContactRegion : uint8
{
	Torso,
	Head,
	Legs
};

/** Which of the striker's limbs (or what he holds) can be the one that meets the body. */
UENUM(BlueprintType)
enum class EHawkeyeStrikeLimb : uint8
{
	/** Fists and feet: whichever is nearest the body at the hit. */
	Any,
	Hands,
	Feet,
	/** The held weapon (a bat, a sack) and the hand holding it. */
	Weapon
};

/** What landed: a strike, an arrow, a finisher's blow. Arrows throw a body less, along their flight. */
UENUM(BlueprintType)
enum class EHawkeyeImpactSource : uint8
{
	Strike,
	Arrow,
	Finisher
};

/**
 * How a swing meets the body: data on every FHawkeyeMeleeAttack. The swing is warped (a root-motion clip) or slid (an
 * in-place clip) so the striking limb is ContactDistance from the chosen region at the hit, never more than MaxSlide
 * and MaxTurnDegrees of correction; past either it whiffs.
 */
/** What a blow does to a body that is getting up (gameplay-semantics.md, "Authored clips"). */
UENUM(BlueprintType)
enum class EHawkeyeGetUpHit : uint8
{
	/** Not getting up: the usual hit reaction (clip, shove, stagger). */
	React,
	/** Getting up: the damage, flash and sound land, but no clip, shove or stagger: he finishes getting up. */
	Absorb,
	/** A knockdown (a heavy, a bola, a blast) puts him down again: the get-up ends and the knockdown starts over. */
	KnockDownAgain
};

USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeStrikeContact
{
	GENERATED_BODY()

	/** Flat distance from the striker's capsule centre to the region's bone at the hit, cm (times the striker's scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact", meta = (ClampMin = "0.0"))
	float ContactDistance = 80.f;

	/** The region the blow is aimed at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact")
	EHawkeyeContactRegion Region = EHawkeyeContactRegion::Torso;

	/** The most the warp or the slide moves the striker toward the contact point, cm. Further away, the swing whiffs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact", meta = (ClampMin = "0.0"))
	float MaxSlide = 120.f;

	/** The most the striker is turned onto the contact point, degrees. Further round, the swing whiffs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxTurnDegrees = 75.f;

	/** An in-place swing's slide runs over this last stretch of the wind-up, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact", meta = (ClampMin = "0.01"))
	float ApproachSeconds = 0.2f;

	/** Which limbs sweep for the body over the hit window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact")
	EHawkeyeStrikeLimb Limbs = EHawkeyeStrikeLimb::Any;

	/** The sphere swept from each striking limb's last place to its next over the hit window, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact", meta = (ClampMin = "1.0"))
	float StrikeRadius = 15.f;

	/** How hard it is, 0 to 1: hit-stop, camera kick, the struck body's reaction and a knockout's throw scale with it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 0.4f;
};

/** Where a swing will stand and face for its contact, and whether it can. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeContactPlan
{
	GENERATED_BODY()

	/** Inside both caps: the warp or slide goes ahead. False: the swing whiffs honestly. */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	bool bReachable = false;

	/** Flat distance the striker moves to stand ContactDistance off the contact point, cm (0 when already that close). */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	float Slide = 0.f;

	/** Degrees from the striker's facing to the contact point. */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	float TurnDegrees = 0.f;

	/** Where the striker's capsule centre stands at the hit (the striker's own height). */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	FVector StandPoint = FVector::ZeroVector;

	/** The flat way the striker faces at the hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	FVector Facing = FVector::ForwardVector;

	/** Why not, when not: "too far" or "too far round". */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	FName WhiffReason;
};

/** One landed blow, as the struck body needs it: where, which way, which bone, how hard, and from what. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeStrikeImpact
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	FVector Point = FVector::ZeroVector;

	/** Unit, the way the blow travels. */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	FVector Direction = FVector::ForwardVector;

	/** The struck bone on the target's skeleton, or None (the reaction then takes the body nearest Point). */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	FName Bone;

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	float Weight = 0.4f;

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	EHawkeyeImpactSource Source = EHawkeyeImpactSource::Strike;

	/** Set: the blow is real (a default-made impact is not). */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Contact")
	bool bValid = false;
};

/**
 * Hit-stop on a landed blow Kate (or Clint) is a party to: the attacker and the victim hold on the hit pose, each on its
 * own clock (CustomTimeDilation; "The weld", FHawkeyeWeldTuning), for these seconds. The world keeps running: only the
 * parry's, the finisher's and the quiver wheel's slow motion are global, because those are deliberate effects.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeHitStopTuning
{
	GENERATED_BODY()

	/** At weight 0, real seconds (2026-11-12: 0.02, which left her light at 0.035 s, two frames that did not read). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|HitStop", meta = (ClampMin = "0.0"))
	float MinSeconds = 0.05f;

	/** At weight 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|HitStop", meta = (ClampMin = "0.0"))
	float MaxSeconds = 0.09f;

	/**
	 * A blow landing within this long (real seconds) of the last stop's start is part of a chain: it freezes the parties
	 * for ChainScale of its own length instead of the whole (it used to get none, which left every hit of a chain after the
	 * first soft; 2026-11-23). Two blows at once (two thugs) never stack into one long freeze.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|HitStop", meta = (ClampMin = "0.0"))
	float MinGapSeconds = 0.15f;

	/** The share of its own hit-stop a blow inside MinGapSeconds of the last gets: 40%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|HitStop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChainScale = 0.4f;
};

/**
 * The weld (gameplay-semantics.md, "The weld"): a landed blow and its reaction are one event. In this order on the hit
 * frame: the attacker and the victim freeze on the hit pose (per actor, CustomTimeDilation near 0, the world runs on); the
 * victim has snapped to the hit pose of his reaction clip; both vibrate along the blow while frozen; on release the
 * knock-back is one eased displacement; the physics flinch starts at release and only adds to the clip.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeWeldTuning
{
	GENERATED_BODY()

	/** An actor's CustomTimeDilation while frozen. Not exactly 0: movement and animation code divide by the step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0001", ClampMax = "0.5"))
	float FreezeDilation = 0.001f;

	/** The mesh's offset along the blow at the first frame of the freeze, cm, for a light (weight at or under VibrationLightWeight)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0"))
	float VibrationLightCm = 2.5f;

	/** ...and for a heavy (weight 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0"))
	float VibrationHeavyCm = 4.f;

	/** The weight that gets VibrationLightCm (Kate's light is 0.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VibrationLightWeight = 0.3f;

	/** How many times it flips side over the freeze: 2 for a light, 4 for a heavy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "1"))
	int32 VibrationMinFrames = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "1"))
	int32 VibrationMaxFrames = 4;

	/** The most a victim is turned toward the attacker over the freeze, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxTurnDegrees = 25.f;

	/** The most he is moved along the line to the attacker so the spacing matches the clip's designed contact distance, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0"))
	float MaxNudgeCm = 15.f;

	/**
	 * The knock-back's total distance for a light and a heavy reaction, cm. A light blow is a short, clear stagger (18 cm,
	 * about a step's worth, over KnockbackLightSeconds) and a heavy one a step and a stumble (52 cm over KnockbackSeconds);
	 * both were 10 and 28 cm (what LaunchCharacter's shove slid a thug) and read as nothing on screen (2026-11-25). These win
	 * over a clip's own baked KnockbackCm unless bClipKnockbackWins; the clips' feet are authored with the same numbers
	 * (LIGHT_SHOVE and HEAVY_SHOVE in Tools/Blender/build_clips.py) so the feet do not skate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0"))
	float KnockbackLightCm = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0"))
	float KnockbackHeavyCm = 52.f;

	/** How long a heavy's eased displacement takes, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.05"))
	float KnockbackSeconds = 0.3f;

	/** How long a light's takes, s: a snap, not a slide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.05"))
	float KnockbackLightSeconds = 0.15f;

	/** True: a reaction clip's baked KnockbackCm replaces the tuned distance (the old behaviour). False: the tuning decides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld")
	bool bClipKnockbackWins = false;

	/** Air kept between the victim's capsule and a wall the knock-back would run into, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0"))
	float KnockbackSkinCm = 3.f;

	/** The speed's fall-off: (1 - t) to this power, so it starts fast and settles slowly (2: a hard start, a soft end). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float KnockbackEase = 2.f;

	/** The most the physics flinch shows over the clip, 0 to 1 (it used to be 0.8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicalBlendCap = 0.5f;

	/** A reaction clip with no authored HitPeakTime blends in over this instead of snapping, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weld", meta = (ClampMin = "0.0"))
	float UnauthoredBlendInSeconds = 0.05f;
};

/** Where a reaction clip starts and how it blends in (ComputeReactionStart). */
struct HAWKEYE_API FHawkeyeReactionStart
{
	/** Seconds into the montage it starts at. */
	float StartAtSeconds = 0.f;

	/** Blend-in, s. 0: the victim is on the hit pose from the first frame. */
	float BlendInSeconds = 0.f;
};

/** The struck body's physical reaction: an impulse at the struck bone, blended over the animation and recovered. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeHitReactionTuning
{
	GENERATED_BODY()

	/** Velocity given the struck bone at weight 1, cm/s, along the blow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.0"))
	float ImpulseSpeed = 420.f;

	/** At weight 0 the impulse is this share of ImpulseSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinShare = 0.35f;

	/** The most the physics shows over the animation, 0 to 1 (at weight 1). 0.5 since the weld (it was 0.8): the clip carries the reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PeakBlend = 0.5f;

	/** Seconds to reach the peak. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.0"))
	float RiseSeconds = 0.04f;

	/** Seconds from the hit until the body is all animation again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.05"))
	float RecoverSeconds = 0.4f;

	/** The reaction while his guard is up (a boss guarding: incoming damage scaled under 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GuardScale = 0.35f;

	/** Physical animation motor strengths pulling the bodies back to the animated pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.0"))
	float OrientationStrength = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Reaction", meta = (ClampMin = "0.0"))
	float AngularVelocityStrength = 80.f;
};

/** A knockout: the ragdoll's throw at the struck bone, and how it settles. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeKnockoutTuning
{
	GENERATED_BODY()

	/** The struck bone's velocity along the blow at weight 1, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float BoneSpeed = 260.f;

	/** The whole body's velocity along the blow at weight 1, cm/s (a light blow: a share of it, MinShare). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float BodySpeed = 200.f;

	/** Upward on the whole body at weight 1, cm/s: enough to lift him off his feet, not to throw him. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float Lift = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinShare = 0.4f;

	/**
	 * A finisher's struck-bone throw and lift against a strike's: its bone is low on the back (spine_01), so he folds down
	 * and back instead of being flipped head over heels (the audit's ko_finisher frames: legs high over backward).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FinisherBoneScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FinisherLiftScale = 0.25f;

	/**
	 * A knockout's ragdoll is held asleep, and thrown, only once the world's time dilation is this or more: under the hit-stop
	 * (0.1) its joints' first corrections, over a tenth of a step, became 4 to 9 m/s. The finisher's slow motion (0.25) runs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReleaseMinTimeDilation = 0.2f;

	/** The fastest a ragdoll's body is pushed out of whatever it starts inside (the floor), cm/s. 0: the project's (none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float MaxDepenetrationSpeed = 120.f;

	/** An arrow's throw against a strike's, along the arrow (and no lift). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArrowScale = 0.45f;

	/** Never faster than these, cm/s, whatever the weight: a body falls, it does not fly across the street. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float MaxBoneSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float MaxBodySpeed = 300.f;

	/** A ragdoll that starts while a clip plays (a finisher's victim, a knockdown) blends in from the clip over this, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float BlendInSeconds = 0.2f;

	/** The body is put to sleep once its pelvis has moved slower than SettleSpeed for SettleHoldSeconds... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float SettleSpeed = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float SettleHoldSeconds = 0.3f;

	/** ...or at the latest this long after it went down, s (4 before the audit; a body still crawling at 3 s is a defect). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.5"))
	float SettleMaxSeconds = 3.f;

	/** Linear and angular damping on every body while it falls (the physics asset's own are near zero). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float LinearDamping = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float AngularDamping = 1.5f;

	/**
	 * Once he is down (at least this long after the knockout, his pelvis within GroundedHeight of the floor), the damping
	 * rises to the grounded values over GroundedBlendSeconds: a body lands with weight and stops, it does not roll and slide
	 * on (the audit: still 150 to 290 cm/s 1.5 s in).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float GroundedAfterSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float GroundedHeight = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.01"))
	float GroundedBlendSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float GroundedLinearDamping = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Knockout", meta = (ClampMin = "0.0"))
	float GroundedAngularDamping = 8.f;
};

/**
 * The contact rules (gameplay-semantics.md, "Contact, reaction and knockout"): where a swing stands to land, whether it
 * whiffs, which bone it meets, which way the struck body reacts, how long the hit-stop holds and how a knockout throws.
 * All pure, so the tests check them without a fight.
 */
UCLASS()
class HAWKEYE_API UHawkeyeHitContactRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Where a striker at Origin facing Forward stands and faces to land ContactDistance (flat) from ContactPoint: the
	 * slide toward it (0 when already that close; never backward) and the turn onto it. Past MaxSlide or MaxTurnDegrees
	 * the plan is not reachable and names why.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Contact")
	static FHawkeyeContactPlan PlanContact(const FVector& Origin, const FVector& Forward, const FVector& ContactPoint,
		float ContactDistance, float MaxSlide, float MaxTurnDegrees);

	/** The region a blow aims at: a body on the floor (knocked down, limp) is hit in the torso whatever the attack says. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Contact")
	static EHawkeyeContactRegion SelectContactRegion(EHawkeyeContactRegion Preferred, bool bTargetDown);

	/** The bones that stand for a region, best first (UE4, UE5 and Paragon skeletons share these names). */
	static TArray<FName> RegionBones(EHawkeyeContactRegion Region);

	/** The first of the region's bones the skeleton has (HasBone), or None when it has none of them. */
	static FName SelectRegionBone(EHawkeyeContactRegion Region, TFunctionRef<bool(FName)> HasBone);

	/** The bones a striker's limbs sweep with (the weapon's points are added by whoever holds it). */
	static TArray<FName> LimbBones(EHawkeyeStrikeLimb Limbs);

	/** Index of the limb point nearest Target, INDEX_NONE for none. */
	static int32 PickStrikeLimb(const TArray<FVector>& LimbPoints, const FVector& Target);

	/**
	 * The way a blow travels: the striking limb's flat motion when it moves at MinSpeed or more and goes toward the
	 * target (a hook comes from the side), else the line from striker to target.
	 */
	static FVector ComputeStrikeDirection(const FVector& LimbVelocity, const FVector& StrikerToTarget, float MinSpeed);

	/** The reaction a body facing Forward plays for a blow travelling StrikeDirection: the side it came from. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Contact")
	static EHawkeyeHitDirection ClassifyReaction(const FVector& Forward, const FVector& StrikeDirection);

	/** Hit-stop length for a blow of Weight, s. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Contact")
	static float ComputeHitStopSeconds(float Weight, const FHawkeyeHitStopTuning& Tuning);

	/**
	 * The same for a blow landing SecondsSinceLastStop (real) after the last stop began (negative: there was none): inside
	 * the tuning's MinGapSeconds, ChainScale (40%) of the blow's own, so a chain freezes on every hit but never stacks.
	 */
	static float ComputeHitStopSecondsAfter(float Weight, const FHawkeyeHitStopTuning& Tuning, float SecondsSinceLastStop);

	/** The vibration's offset along the blow at its first frame for a blow of Weight, cm: Light at or under the light weight, Heavy at 1. */
	static float ComputeVibrationAmplitude(float Weight, const FHawkeyeWeldTuning& Tuning);

	/** How many sides the vibration flips over a freeze of a blow of Weight: Min (a light) to Max (a heavy), rounded. */
	static int32 ComputeVibrationFrames(float Weight, const FHawkeyeWeldTuning& Tuning);

	/**
	 * The mesh's offset along the blow ElapsedSeconds into a freeze of DurationSeconds, cm, signed (positive: along the
	 * blow; bFlip starts it the other way, for the other party): the freeze is cut into ComputeVibrationFrames equal
	 * steps, each the opposite side of the last and smaller, down to 1 / Frames of the first at the last. 0 outside the freeze.
	 */
	static float ComputeVibrationOffset(float ElapsedSeconds, float DurationSeconds, float Weight, bool bFlip,
		const FHawkeyeWeldTuning& Tuning);

	/** The turn that points a body facing CurrentYaw at YawToAttacker, clamped to MaxDegrees, signed degrees (the short way round). */
	static float ComputeVictimTurn(float CurrentYawDegrees, float YawToAttackerDegrees, float MaxDegrees);

	/**
	 * How far to move a victim along the line from the attacker (positive: away from him) so their flat distance goes from
	 * CurrentFlatCm to DesiredFlatCm, never more than MaxCm either way. Zero for a desired distance of 0 or less (no data).
	 */
	static float ComputeVictimNudge(float CurrentFlatCm, float DesiredFlatCm, float MaxCm);

	/**
	 * The knock-back's speed profile: Samples strengths (0..1, the first 1) of (1 - t) ^ Ease at t = 0..1 evenly, and the
	 * peak speed (cm/s) that makes the displacement over SecondsLong exactly DistanceCm when the strengths are joined by
	 * straight lines (what a linear curve does). OutPeakSpeed is 0 for no distance or time.
	 */
	static void ComputeKnockbackProfile(float DistanceCm, float SecondsLong, float Ease, int32 Samples, float& OutPeakSpeed,
		TArray<float>& OutStrengths);

	/**
	 * Where a reaction clip starts: at its HitPeakTime with no blend (the victim is popped onto the hit pose) when the clip's
	 * data has one (PeakSeconds at or over 0, under PlayLength), else from its start blending in over the tuning's
	 * UnauthoredBlendInSeconds.
	 */
	static FHawkeyeReactionStart ComputeReactionStart(float PeakSeconds, float PlayLength, const FHawkeyeWeldTuning& Tuning);

	/**
	 * Whether a blow turns and nudges its victim over the freeze: he is alive, not winding up a swing (a boss keeps his
	 * swing's pose), on his feet (not down, not limp) and not the player (her facing and place are hers).
	 */
	static bool ShouldAlignVictim(bool bAlive, bool bWindingUp, bool bDown, bool bPlayerControlled);

	/** The physics flinch's blend over the clip, capped at the tuning's PhysicalBlendCap (the clip carries the reaction). */
	static float ClampPhysicalBlend(float Blend, const FHawkeyeWeldTuning& Tuning);

	/** The region of the body a struck bone is in: head and neck, legs (pelvis, thighs, calves, feet), else the torso. */
	static EHawkeyeContactRegion ClassifyBoneRegion(FName Bone);

	/**
	 * The region nearest ContactHeightCm among the head, torso and legs heights (all over the floor, cm); the attack's own
	 * Preferred region when its contact height is unknown (negative).
	 */
	static EHawkeyeContactRegion SelectRegionForHeight(float ContactHeightCm, float HeadCm, float TorsoCm, float LegsCm,
		EHawkeyeContactRegion Preferred);

	/**
	 * The height (over his floor, cm) a blow's zone is chosen by: the clip's baked contact height when it has one (not
	 * negative), else where the striking limb really met him (LimbHeightCm), else -1 (the attack's own region applies).
	 * A chest-height punch plays the gut reaction, not the head snap its attack data names.
	 */
	static float PickStrikeHeight(float ClipHeightCm, float LimbHeightCm);

	/**
	 * The knock-back distance to apply so the victim's net displacement from where he stood when hit is TunedCm: the
	 * victim-align nudge (done over the freeze, before the knock-back) already moved him Nudge; the part of it along the
	 * knock-back (Direction) is taken off, the part against it (a nudge toward the attacker) is added back. Never negative.
	 */
	static float ComputeNetKnockbackCm(float TunedCm, const FVector& Nudge, const FVector& Direction);

	/**
	 * The knock-back distance for a reaction, cm: the clip's baked ClipCm when the tuning says clips win (and it has one, 0
	 * or more), else the tuned light or heavy distance; times Scale (the Hawkeye.Combat.KnockbackScale CVar). Never negative.
	 */
	static float ResolveKnockbackCm(float ClipCm, bool bHeavy, const FHawkeyeWeldTuning& Tuning, float Scale = 1.f);

	/** The time the knock-back takes: KnockbackLightSeconds for a light, KnockbackSeconds for a heavy. */
	static float ResolveKnockbackSeconds(bool bHeavy, const FHawkeyeWeldTuning& Tuning);

	/**
	 * The way a blow shoves its victim: flat, from the attacker through the victim. With no attacker (or standing on him),
	 * Fallback flattened (the victim's back), else straight back along X.
	 */
	static FVector ComputeKnockbackDirection(const FVector* AttackerLocation, const FVector& VictimLocation, const FVector& Fallback);

	/**
	 * What of DesiredCm may be moved given a wall ClearanceCm ahead of the victim (the sweep's free distance; negative for
	 * no hit): the clearance less SkinCm, never more than desired and never negative. Nothing is moved through a wall.
	 */
	static float ClampKnockbackToClearance(float DesiredCm, float ClearanceCm, float SkinCm);

	/**
	 * Whether a party of a landed blow plays its reaction after the hit-stop (alive, not winding up a swing: a boss
	 * swinging through a light keeps his swing's pose).
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Contact")
	static bool ShouldHitStop(bool bAlive, bool bWindingUp);

	/** The struck bone's velocity change for a blow travelling Direction at Weight; GuardScale applies while guarding. */
	static FVector ComputeReactionImpulse(const FVector& Direction, float Weight, bool bGuarding, const FHawkeyeHitReactionTuning& Tuning);

	/** The physics blend over the animation Elapsed s after a hit of Weight: up over RiseSeconds, eased back to 0. */
	static float ComputeReactionBlend(float Elapsed, float Weight, bool bGuarding, const FHawkeyeHitReactionTuning& Tuning);

	/**
	 * A knockout's throw: the struck bone's velocity (OutBone) and the whole body's (OutBody), along the blow (flat for an
	 * arrow, plus lift for a strike or finisher), scaled by Weight and capped.
	 */
	static void ComputeKnockoutImpulse(const FVector& Direction, float Weight, EHawkeyeImpactSource Source,
		const FHawkeyeKnockoutTuning& Tuning, FVector& OutBone, FVector& OutBody);

	/**
	 * A knocked-out body's damping SecondsGrounded after it was first down (negative: still falling, or not yet
	 * GroundedAfterSeconds in): the falling values, then up to the grounded ones over GroundedBlendSeconds (smoothstepped),
	 * so a body thrown off a roof still falls at full speed. X linear, Y angular.
	 */
	static FVector2D ComputeKnockoutDamping(float SecondsGrounded, const FHawkeyeKnockoutTuning& Tuning);

	/** The camera kick for a landed blow of Weight: Light cm at 0, Heavy cm at 1. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Contact")
	static float ComputeCameraKick(float Weight, float LightDistance, float HeavyDistance);

	/**
	 * Whether a body lying with its chest facing BodyFront (world) is on its back: the chest faces up (z above 0). A body
	 * on its side counts as face down (the face-down get-up rolls onto the hands first).
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Contact")
	static bool IsLyingOnBack(const FVector& BodyFront);

	/**
	 * Where the capsule stands and which way it faces for a get-up clip to start on a body lying on the floor
	 * (gameplay-semantics.md, "Authored clips"): the clip's first frame has its pelvis at ClipPelvis and its spine
	 * pointing ClipBodyUp, both in the capsule's space (forward +X); the body lies with its pelvis at WorldPelvis and its
	 * spine pointing WorldBodyUp. The yaw turns the clip's spine onto the body's (flat), and the location puts the clip's
	 * pelvis on the body's (flat; Z is WorldPelvis's, the caller stands it on the floor). A spine pointing straight up or
	 * down (nothing flat to turn by) keeps FallbackYaw.
	 */
	static void ComputeGetUpAlignment(const FVector& WorldPelvis, const FVector& WorldBodyUp, const FVector& ClipPelvis,
		const FVector& ClipBodyUp, float FallbackYaw, FVector& OutLocation, float& OutYaw);

	/** What a blow does to a body getting up off the floor (the get-up rule). */
	static EHawkeyeGetUpHit ResolveHitDuringGetUp(bool bGettingUp, bool bKnocksDown);
};
