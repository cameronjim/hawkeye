#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/HitContactRules.h"
#include "HitReactionComponent.generated.h"

class UCurveFloat;
class UPhysicalAnimationComponent;
class USkeletalMeshComponent;

/**
 * What a landed blow does to a body (gameplay-semantics.md, "Contact, reaction and knockout"), on Kate, Clint and every
 * thug: the hit-stop that holds both parties a few frames, the physical reaction (an impulse at the struck bone,
 * blended over the animation by physical animation motors and recovered within RecoverSeconds), and a knockout's
 * ragdoll throw and settle. Rules and numbers are UHawkeyeHitContactRules and the tuning structs below (data).
 *
 * The body it moves is the one people see: a look with a physics asset (Kate's Sparrow fallback) when there is one,
 * else the character's mesh (her built suit and Clint's look have none, so their driver reacts). A mesh
 * with no physics asset (tests, a greybox) keeps every rule's bookkeeping and moves nothing. bPhysicalReaction off (Kate
 * and Clint since 2026-11-12) keeps the bookkeeping and moves nothing too: their hit clips and lean read the blow.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UHitReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHitReactionComponent();

	/**
	 * The blow about to land, set by whoever lands it before the damage goes in, so a death it causes (GoLimp) and the
	 * reaction clip it picks see it. Cleared by ClearPendingImpact once the blow is resolved.
	 */
	void SetPendingImpact(const FHawkeyeStrikeImpact& Impact) { PendingImpact = Impact; }
	void ClearPendingImpact() { PendingImpact = FHawkeyeStrikeImpact(); }
	const FHawkeyeStrikeImpact& GetPendingImpact() const { return PendingImpact; }

	/** The last blow this body reacted to (for the screenshots and the log). */
	const FHawkeyeStrikeImpact& GetLastImpact() const { return LastImpact; }

	/** Notes a hit-stop of Seconds (real) under way, so a reaction waits for it to end. A longer stop wins. */
	void BeginHitStop(float Seconds);

	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsHitStopped() const { return HitStopRemaining > 0.f; }

	/**
	 * The weld's freeze (FHawkeyeWeldTuning): this body holds on its pose for Seconds (the world's clock) on its own clock,
	 * CustomTimeDilation near 0, while the world runs on, and its mesh vibrates along BlowDirection (decaying, by
	 * Weight; bFlipVibration starts it the other way, for the other party). Also notes the stop (BeginHitStop), so a
	 * reaction and a knockout's throw wait for the release. A freeze under way is replaced by the new one.
	 */
	void BeginFreeze(float Seconds, const FVector& BlowDirection, float Weight, bool bFlipVibration);

	/**
	 * Something to do when the freeze lets go (a knockdown: the victim holds the blow's hit pose through the freeze, then goes
	 * over). One at a time. Run at once by RunPostFreezeAction when no freeze came.
	 */
	void SetPostFreezeAction(TFunction<void()> Action) { PostFreezeAction = MoveTemp(Action); }
	bool HasPostFreezeAction() const { return static_cast<bool>(PostFreezeAction); }
	void RunPostFreezeAction();

	/** True from BeginFreeze until the freeze ends: the owner's own clock is stopped. */
	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsFrozen() const { return bFrozen; }

	/** The length of the freeze under way (or the last), real seconds. */
	float GetFreezeSeconds() const { return FreezeDuration; }

	/** How many freezes this body has had (tests and the audit). */
	int32 GetFreezeCount() const { return FreezeCount; }

	/**
	 * Over the freeze under way, turns the owner TurnDegrees about up and moves him by Nudge (world), eased: the victim
	 * facing his attacker and standing where the clip's contact was designed. Ends with the freeze (the rest is applied
	 * at once if it ends early). The caller has checked Nudge against walls and ledges.
	 */
	void BeginAlignment(float TurnDegrees, const FVector& Nudge);

	/**
	 * The reaction's knock-back: one eased displacement of DistanceCm along Direction (flat), over Weld.KnockbackSeconds,
	 * a root motion source on the owner's character movement. Started at the hit, it waits out the freeze by itself (the
	 * movement's time is the owner's, which is stopped) and so starts at the release. A character only.
	 */
	void ApplyKnockback(const FVector& Direction, float DistanceCm);

	/** The physical reaction to Impact; waits for a hit-stop under way to end, so the body freezes, then snaps. */
	void PlayHitReaction(const FHawkeyeStrikeImpact& Impact);

	/** Drops a reaction under way without touching the bodies (a knockdown or death ragdoll takes them over). */
	void CancelReaction();

	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsReacting() const { return bReacting || bReactionQueued; }

	/** The physics blend over the animation now, 0 to the tuning's peak. */
	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	float GetReactionBlend() const { return ReactionBlend; }

	/** The bone the last reaction pushed, on the body mesh. */
	FName GetReactionBone() const { return ReactionBone; }

	/**
	 * A knockout on Ragdoll (already simulating): the pending impact's throw at its struck bone (nothing without one),
	 * damping while it falls, a blend in from the clip when one was playing, then the settle (asleep once still).
	 */
	void BeginKnockout(USkeletalMeshComponent* Ragdoll, bool bBlendFromClip);

	/** What his capsule was doing as he went down (a run, a shove), carried into the knockout's throw. */
	void SetKnockoutCarryVelocity(const FVector& Velocity) { KnockoutCarryVelocity = Velocity; }

	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsKnockoutSettled() const { return bKnockoutSettled; }

	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	bool IsKnockoutFalling() const { return bKnockout && !bKnockoutSettled; }

	/** Seconds since the knockout began (world time). */
	float GetKnockoutElapsed() const { return KnockoutElapsed; }

	/** The velocities the last knockout gave, cm/s (tests and the log). */
	FVector GetKnockoutBoneVelocity() const { return KnockoutBoneVelocity; }
	FVector GetKnockoutBodyVelocity() const { return KnockoutBodyVelocity; }

	int32 GetReactionCount() const { return ReactionCount; }
	int32 GetHitStopCount() const { return HitStopCount; }
	float GetLastHitStopSeconds() const { return LastHitStopSeconds; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction")
	FHawkeyeHitStopTuning HitStop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction")
	FHawkeyeHitReactionTuning Reaction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction")
	FHawkeyeKnockoutTuning Knockout;

	/** The weld: the freeze, the vibration, the alignment, the knock-back's ease, the physics flinch's cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction")
	FHawkeyeWeldTuning Weld;

	/**
	 * Whether a blow's physical reaction runs at all (the hit-stop and the knockout are unaffected). Off on Kate and Clint
	 * (2026-11-12): on Kate's look it carried her body off her (fixed), and then crashed the game now and then inside the
	 * engine's physics blend on that secondary mesh (the melee feel audit; one lap in thirteen). Their hit clips and lean
	 * still read every blow. On by default: every thug's driver reacts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction")
	bool bPhysicalReaction = true;

	/** The bone whose children a reaction moves; the chain from it down to the struck bone simulates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit Reaction")
	FName PelvisBone = FName(TEXT("pelvis"));

	/**
	 * The ragdoll's whole mass, kg, spread over its bodies by BodyMassShares (a body not listed shares what is left). The
	 * UE mannequin's physics asset gives every body 1 kg (19 kg in all), so a falling man had a torso as light as a hand and
	 * flailed like a doll (the melee feel audit, 2026-11-12). 0 keeps the physics asset's own masses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Mass", meta = (ClampMin = "0.0"))
	float RagdollMassKg = 78.f;

	/** Each body's share of RagdollMassKg, by bone (left and right each take their own share). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Mass")
	TMap<FName, float> BodyMassShares;

	/** Sets every body on Mesh to its share of RagdollMassKg (none when it is 0) and caps how fast it is pushed out of the floor. */
	void PrepareRagdollBodies(USkeletalMeshComponent& Mesh) const;

	/** The mass BodyMassShares gives Bone out of TotalKg with Bodies bodies in all, kg. Pure. */
	static float ComputeBodyMass(FName Bone, float TotalKg, const TMap<FName, float>& Shares, const TArray<FName>& Bodies);

	/** The body a reaction moves: Kate's look, else the owner's mesh. Null when there is none. */
	USkeletalMeshComponent* GetBodyMesh() const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Wakes a held knockout and gives it its throw, once the world's time dilation is at the tuning's release level. */
	void ReleaseKnockoutIfRunning();

	/** Starts the queued reaction on the body (the hit-stop is over). */
	void StartQueuedReaction();

	/** Ends the reaction: the chain back to kinematic, the motors off. */
	void FinishReaction();

	void EndHitStop();

	/** One real step of the freeze under way: the vibration's offset, the alignment's share. */
	void AdvanceFreeze(float ElapsedSeconds);

	/** The freeze is over: the clock back, the vibration off, the alignment finished. */
	void EndFreeze();

	/** The mesh the vibration moves: the character's driver (the look is attached to it), or null when it simulates. */
	USkeletalMeshComponent* GetVibratedMesh() const;

	/** The physics state the reaction needs on Mesh (a collision-less look gets physics-only bodies that touch nothing). */
	bool PrepareMesh(USkeletalMeshComponent& Mesh);

	/** Bodies at or under Bone with no body above them on the way to the skeleton's root (the physics asset's topmost). */
	TArray<FName> FindTopBodiesBelow(const USkeletalMeshComponent& Mesh, FName Bone) const;

	/** The knockout's pelvis is within GroundedHeight of the floor (no pelvis or no world: true). */
	bool IsPelvisNearFloor(const USkeletalMeshComponent& Mesh) const;

	/** A knockdown or a death's ragdoll has the body (its pelvis simulates): a reaction leaves it alone. */
	bool IsRagdollOwned(const USkeletalMeshComponent& Mesh) const;

	/** The child of the pelvis the struck bone hangs under (spine_01 for the torso, a thigh for a leg). */
	FName FindChainRoot(const USkeletalMeshComponent& Mesh, FName Bone) const;

	/** The body on Mesh for Impact: its bone when the mesh has a body there, else the body nearest its point. */
	FName FindStruckBody(const USkeletalMeshComponent& Mesh, const FHawkeyeStrikeImpact& Impact) const;

	void UpdateTickEnabled();

	FHawkeyeStrikeImpact PendingImpact;
	FHawkeyeStrikeImpact LastImpact;
	FHawkeyeStrikeImpact QueuedImpact;

	float HitStopRemaining = 0.f;
	float LastHitStopSeconds = 0.f;
	int32 HitStopCount = 0;

	/** The freeze: the owner's own clock stopped, its mesh vibrating along the blow. */
	bool bFrozen = false;
	float FreezeDuration = 0.f;
	float FreezeWeight = 0.f;
	bool bFreezeFlip = false;
	float SavedCustomDilation = 1.f;
	int32 FreezeCount = 0;
	FVector FreezeAxis = FVector::ForwardVector;
	/** The world offset the vibration has put on the mesh now. */
	FVector AppliedVibration = FVector::ZeroVector;

	TFunction<void()> PostFreezeAction;

	/** The alignment over the freeze: totals, and the share of them applied so far (0 to 1). */
	bool bAligning = false;
	float AlignTurnDegrees = 0.f;
	FVector AlignNudge = FVector::ZeroVector;
	float AlignApplied = 0.f;

	/** The knock-back's strength over its time (a linear curve), built once for the ease it was built with. */
	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> KnockbackCurve;

	float KnockbackCurveEase = 0.f;

	bool bReactionQueued = false;
	bool bReacting = false;
	bool bReactionGuarding = false;
	float ReactionElapsed = 0.f;
	float ReactionBlend = 0.f;
	FName ReactionRoot;
	/** Bodies of the chain held on the animation: those with no body above them (the component follows its root body). */
	TArray<FName> KeptKinematic;
	FName ReactionBone;
	int32 ReactionCount = 0;

	bool bKnockout = false;
	bool bKnockoutSettled = false;
	bool bKnockoutBlending = false;
	float KnockoutElapsed = 0.f;
	float KnockoutStillSeconds = 0.f;
	/** KnockoutElapsed when he was first down (the grounded damping starts), -1 until then. */
	float KnockoutGroundedAt = -1.f;
	FVector KnockoutBoneVelocity = FVector::ZeroVector;
	FVector KnockoutBodyVelocity = FVector::ZeroVector;
	FVector KnockoutCarryVelocity = FVector::ZeroVector;
	/** The knockout's bodies are asleep until the world runs at ReleaseMinTimeDilation or faster (a hit-stop is over). */
	bool bKnockoutHeld = false;
	bool bKnockoutHasThrow = false;
	FName KnockoutStruck;

	TWeakObjectPtr<USkeletalMeshComponent> ReactionMesh;
	TWeakObjectPtr<USkeletalMeshComponent> KnockoutMesh;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;
};
