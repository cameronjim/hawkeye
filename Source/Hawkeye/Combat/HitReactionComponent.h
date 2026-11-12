#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/HitContactRules.h"
#include "HitReactionComponent.generated.h"

class UPhysicalAnimationComponent;
class USkeletalMeshComponent;

/**
 * What a landed blow does to a body (gameplay-semantics.md, "Contact, reaction and knockout"), on Kate, Clint and every
 * thug: the hit-stop that holds both parties a few frames, the physical reaction (an impulse at the struck bone,
 * blended over the animation by physical animation motors and recovered within RecoverSeconds), and a knockout's
 * ragdoll throw and settle. Rules and numbers are UHawkeyeHitContactRules and the tuning structs below (data).
 *
 * The body it moves is the one people see: Kate's look (Sparrow) when she has one, else the character's mesh. A mesh
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

	/** The body a reaction moves: Kate's look, else the owner's mesh. Null when there is none. */
	USkeletalMeshComponent* GetBodyMesh() const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Starts the queued reaction on the body (the hit-stop is over). */
	void StartQueuedReaction();

	/** Ends the reaction: the chain back to kinematic, the motors off. */
	void FinishReaction();

	void EndHitStop();

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

	TWeakObjectPtr<USkeletalMeshComponent> ReactionMesh;
	TWeakObjectPtr<USkeletalMeshComponent> KnockoutMesh;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;
};
