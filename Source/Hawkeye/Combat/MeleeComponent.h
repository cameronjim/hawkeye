#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/HitContactRules.h"
#include "MeleeComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequenceBase;
class UMotionWarpingComponent;
class USkeletalMeshComponent;
class USoundBase;

/** One swing: how hard, how slow, how far. Kate's light and heavy and each thug weapon are one of these. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeMeleeAttack
{
	GENERATED_BODY()

	/** For the log and for listeners: "light", "heavy", "fists", "bat". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	FName Name = FName(TEXT("light"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.0"))
	float Damage = 15.f;

	/** Seconds between starting the swing and the hit landing. The telegraph runs this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.0"))
	float WindupSeconds = 0.1f;

	/** Seconds after the hit before another swing can start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.0"))
	float RecoverSeconds = 0.2f;

	/** How far in front of the owner's capsule centre the sweep reaches, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.0"))
	float Range = 120.f;

	/** Radius of the swept sphere, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "1.0"))
	float Radius = 35.f;

	/** Passes the hit through ApplyMeleeDamage with stagger on (above the target's StaggerThreshold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	bool bStagger = true;

	/** Puts a surviving thug on the floor (AThugCharacter::Knockdown) instead of staggering him. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	bool bKnockdown = false;

	/** Shoves whoever it lands on this far away from the swinger, cm (the heavy's bash: 250). 0 for none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.0"))
	float KnockbackDistance = 0.f;

	/** Seconds the knockback takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.01"))
	float KnockbackSeconds = 0.35f;

	/**
	 * Which clip of the owner's UCombatAnimSet plays this swing. None, an empty slot, or a clip that
	 * will not play keeps the procedural swing (the strike pose and the lunge).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	ECombatAnimRole AnimRole = ECombatAnimRole::None;

	/** Procedural step forward at the start of the swing, cm. Skipped when a clip with root motion moves the owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.0"))
	float LungeDistance = 0.f;

	/** Seconds the lunge takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee", meta = (ClampMin = "0.01"))
	float LungeSeconds = 0.1f;

	/**
	 * No parry answers it (UHawkeyeMeleeRules::ClassifyParry says None): it has its own telegraph (the "!!" and the
	 * dodge key over him) and must be dodged. A boss's heavy swing; every thug's swing leaves it off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	bool bUnblockable = false;

	/**
	 * How it meets the body: the contact distance and region the warp or slide aims for, their caps (past them it
	 * whiffs), the limbs that sweep for the body over the hit window, and its weight (gameplay-semantics.md, "Contact,
	 * reaction and knockout").
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Contact")
	FHawkeyeStrikeContact Contact;

	/** Seconds from the start of the swing until another can start. */
	float GetTotalSeconds() const { return WindupSeconds + RecoverSeconds; }
};

/**
 * How hard a swing is pulled onto its target and what stops the pull (gameplay-semantics.md, "Melee feel"): data on the
 * melee component, Kate's bigger than a thug's.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeStrikeMagnetism
{
	GENERATED_BODY()

	/** The fastest the warp, lunge or slide closes on him, cm/s, averaged over the time left to the hit. 0: no cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Magnetism", meta = (ClampMin = "0.0"))
	float MaxClosingSpeed = 1100.f;

	/** A rise in the floor on the way higher than this stops the pull, cm (a wall, a crate, a step up onto a roof). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Magnetism", meta = (ClampMin = "0.0"))
	float MaxStep = 45.f;

	/** A drop deeper than this on the way stops it, cm (a gap between roofs, a ledge, the edge of a dock). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Magnetism", meta = (ClampMin = "0.0"))
	float MaxDrop = 60.f;

	/** The floor is sampled this far apart along the way, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Magnetism", meta = (ClampMin = "5.0"))
	float FloorSpacing = 25.f;

	/** The sight line chest to chest is swept with this radius; anything solid on it (a wall, a fence) stops the pull, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Magnetism", meta = (ClampMin = "0.0"))
	float SightRadius = 8.f;

	/** The sight line's height over each capsule's centre, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Magnetism")
	float SightHeight = 40.f;
};

UENUM(BlueprintType)
enum class EMeleePhase : uint8
{
	Idle,
	/** Telegraphing: the hit has not landed yet. */
	Windup,
	/** The hit has been swept; waiting out RecoverSeconds. */
	Recover
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMeleeWindupSignature, FName, AttackName, float, WindupSeconds);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMeleeLandedSignature, AActor*, HitActor, float, DamageDealt, FName, AttackName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMeleeMissedSignature, FName, AttackName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMeleeComboWindowSignature, bool, bOpen);

/**
 * A swing: wind-up, one sphere sweep in front of the owner at the end of it, recovery. Shared by
 * Kate (light, heavy) and the thugs (fists, bat); the attack itself is data, an FHawkeyeMeleeAttack.
 *
 * The sweep looks for pawns only (object type Pawn), so the hidden traversal ledges on the Weapon
 * channel along every roof edge never eat a punch. Actors carrying IgnoreTag are never hit, which
 * is how a thug's bat misses the thug beside him.
 *
 * Timing runs from TickComponent; AdvanceAttack is public so a test can step it without a world tick.
 *
 * With a clip (claude-docs/gameplay-semantics.md, "Combat animation clips"): when the attack's
 * AnimRole resolves in the anim set and the montage plays, the swing is timed by the montage's
 * ANS_HitWindow and ANS_ComboWindow instead of the attack's timers (a window it lacks keeps its
 * timer), a root-motion clip is warped toward the target given by SetNextAttackTarget, and the
 * lunge only runs when the clip does not move the owner.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UMeleeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMeleeComponent();

	/**
	 * Starts Attack. Refused (false) while another swing is winding up or recovering, except inside a
	 * clip's combo window, where the new swing cuts the old one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool StartAttack(const FHawkeyeMeleeAttack& Attack);

	/** True when StartAttack would take a swing now: idle, or inside the current clip's combo window. */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool CanStartAttack() const;

	/** The clips the owner's swings use. Null, or an empty role, keeps the procedural swing. */
	UFUNCTION(BlueprintCallable, Category = "Melee|Animation")
	void SetAnimSet(UCombatAnimSet* InAnimSet) { AnimSet = InAnimSet; }

	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	UCombatAnimSet* GetAnimSet() const { return AnimSet; }

	/** The next StartAttack is aimed at Target: a root-motion clip warps to him. Null clears it. */
	UFUNCTION(BlueprintCallable, Category = "Melee|Animation")
	void SetNextAttackTarget(AActor* Target) { PendingTarget = Target; }

	/** The montage the current (or last) swing plays, or null on the procedural path. */
	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	UAnimMontage* GetCurrentMontage() const { return CurrentMontage; }

	/**
	 * The clip the current (or last) swing picked from its role's variants, whether or not it played;
	 * null when the role has none. Each swing of a role takes the next variant in order
	 * (UCombatAnimSet::PickNextVariant), so with two or more no clip plays twice running.
	 */
	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	UAnimMontage* GetPickedMontage() const { return PickedMontage; }

	/** Which variant of its role the current (or last) swing picked (0 is the role's slot); -1 for none. */
	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	int32 GetPickedVariantIndex() const { return PickedVariantIndex; }

	/** True when the current swing's hit comes from ANS_HitWindow rather than WindupSeconds. */
	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	bool IsHitFromNotify() const { return bHitFromNotify; }

	/** True when the current swing's chain and recovery come from ANS_ComboWindow rather than the timers. */
	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	bool IsComboFromNotify() const { return bComboFromNotify; }

	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	bool IsHitWindowOpen() const { return bHitWindowOpen; }

	UFUNCTION(BlueprintPure, Category = "Melee|Animation")
	bool IsComboWindowOpen() const { return bComboWindowOpen; }

	/** ANS_HitWindow opened on Animation: the swing lands (or starts sweeping). Ignored for another montage. */
	void NotifyHitWindowBegin(const UAnimSequenceBase* Animation);

	/** ANS_HitWindow closed: a swing that found nobody missed. */
	void NotifyHitWindowEnd(const UAnimSequenceBase* Animation);

	/** ANS_ComboWindow opened: the next swing may start. */
	void NotifyComboWindowBegin(const UAnimSequenceBase* Animation);

	/** ANS_ComboWindow closed. */
	void NotifyComboWindowEnd(const UAnimSequenceBase* Animation);

	/** The current swing's montage ended (or was cut): the swing is over. */
	void NotifyMontageEnded(bool bInterrupted);

	/**
	 * Tests: the next StartAttack is timed by the notify calls as if its montage carried both windows,
	 * without playing anything (a test world has no anim instance).
	 */
	void ForceNotifyTimingForTest() { bForceNotifyTimingForTest = true; }

	/**
	 * The next StartAttack lunges and sweeps along Direction (flattened) instead of the owner's forward,
	 * for the whole swing: the soft lock turns the body over 0.1 s, but the blow goes at the target at once.
	 * Zero clears it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void SetNextAttackDirection(FVector Direction) { PendingDirection = Direction.GetSafeNormal2D(); }

	/** The way the current swing goes: the direction it was given, else the owner's forward. */
	UFUNCTION(BlueprintPure, Category = "Melee")
	FVector GetSwingDirection() const;

	/** Drops the current swing without a hit: a stagger or a knockdown interrupts the wind-up. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void CancelAttack();

	/** Moves the current swing on by DeltaSeconds; the sweep runs the moment the wind-up ends. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void AdvanceAttack(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsAttacking() const { return Phase != EMeleePhase::Idle; }

	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsWindingUp() const { return Phase == EMeleePhase::Windup; }

	UFUNCTION(BlueprintPure, Category = "Melee")
	EMeleePhase GetPhase() const { return Phase; }

	/** The swing in progress (or the last one). */
	UFUNCTION(BlueprintPure, Category = "Melee")
	const FHawkeyeMeleeAttack& GetCurrentAttack() const { return CurrentAttack; }

	/** Seconds left in the current phase. */
	UFUNCTION(BlueprintPure, Category = "Melee")
	float GetPhaseRemaining() const { return PhaseRemaining; }

	/**
	 * The first living pawn with a health component, other than the owner and anything tagged
	 * IgnoreTag, in a sphere of Attack.Radius swept Attack.Range forward from the owner. Null if none.
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	AActor* FindTarget(const FHawkeyeMeleeAttack& Attack) const;

	/**
	 * The knockback's force (cm/s, flat) on a target at To hit from From: Distance over Seconds, pointing away
	 * from From. Pure.
	 */
	static FVector ComputeKnockbackForce(const FVector& From, const FVector& To, float Distance, float Seconds);

	/** Pushes Target (a character) Attack.KnockbackDistance away from the owner as a root motion force. */
	void ApplyKnockback(AActor* Target, const FHawkeyeMeleeAttack& Attack) const;

	/** Swings the target's shield took. */
	UFUNCTION(BlueprintPure, Category = "Melee")
	int32 GetBlockedCount() const { return BlockedCount; }

	/** Actors with this tag are never hit. Thugs set "Thug". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	FName IgnoreTag;

	/**
	 * The telegraph: played as a wind-up of TelegraphMinWindup or longer starts (the thugs' swings and
	 * the heavy's bash), so the tell is heard as well as seen (MS_Thug_Telegraph).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Audio")
	TSoftObjectPtr<USoundBase> WindupSound;

	/** Wind-ups shorter than this are not telegraphed (Kate's 0.1 s jab). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Audio", meta = (ClampMin = "0.0"))
	float TelegraphMinWindup = 0.3f;

	/** The swing through the air, hit or miss (MS_Thug_BatSwing on the thugs). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Audio")
	TSoftObjectPtr<USoundBase> SwingSound;

	/** A strike that lands (MS_Melee_Punch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Audio")
	TSoftObjectPtr<USoundBase> HitSound;

	/** A knockdown strike that lands (Kate's heavy, MS_Melee_Heavy). Falls back to HitSound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Audio")
	TSoftObjectPtr<USoundBase> HeavyHitSound;

	/** Fired as a swing starts: the telegraph. */
	UPROPERTY(BlueprintAssignable, Category = "Melee")
	FOnMeleeWindupSignature OnAttackWindup;

	/** Fired when the sweep at the end of the wind-up found something and hurt it. */
	UPROPERTY(BlueprintAssignable, Category = "Melee")
	FOnMeleeLandedSignature OnAttackLanded;

	/** Fired when the sweep found nobody, or a shield took it. */
	UPROPERTY(BlueprintAssignable, Category = "Melee")
	FOnMeleeMissedSignature OnAttackMissed;

	/** A clip's combo window opened (true) or closed (false; also when its swing ends or is cancelled). */
	UPROPERTY(BlueprintAssignable, Category = "Melee|Animation")
	FOnMeleeComboWindowSignature OnComboWindowChanged;

	/**
	 * The melee assist (gameplay-semantics.md, "Melee assist"; Kate only): the swing closes the gap to the
	 * target given by SetNextAttackTarget (a longer lunge, or the warp for a root-motion clip), keeps the
	 * warp target on him through the wind-up, and a sweep that misses him still lands on him when he was
	 * that target and is within AssistReachScale of the attack's reach and AssistForgiveDegrees of the swing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Assist")
	bool bAssistSwings = false;

	/** A target picked at the press is still hit this far past the attack's reach (Range + Radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Assist", meta = (ClampMin = "1.0"))
	float AssistReachScale = 1.3f;

	/** And this far off the swing's direction, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Assist", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float AssistForgiveDegrees = 30.f;

	/** The target the current (or last) swing was aimed at, or null. */
	UFUNCTION(BlueprintPure, Category = "Melee|Assist")
	AActor* GetSwingTarget() const { return SwingTarget.Get(); }

	/** How far the current (or last) swing's procedural step asked to go, cm (0 for a warped clip). */
	UFUNCTION(BlueprintPure, Category = "Melee|Assist")
	float GetLastLungeDistance() const { return LastLungeDistance; }

	/** Swings whose hit the assist's forgiveness gave (the sweep alone would have missed). */
	UFUNCTION(BlueprintPure, Category = "Melee|Assist")
	int32 GetForgivenHitCount() const { return ForgivenHitCount; }

	/** The current (or last) swing's contact plan: where it stands to land and whether it can. */
	const FHawkeyeContactPlan& GetContactPlan() const { return ContactPlan; }

	/** The current (or last) swing was out of its warp or slide caps: it whiffs, no forgiveness. */
	UFUNCTION(BlueprintPure, Category = "Melee|Contact")
	bool IsContactWhiff() const { return bContactWhiff; }

	/** Swings that whiffed for being out of their caps. */
	UFUNCTION(BlueprintPure, Category = "Melee|Contact")
	int32 GetContactWhiffCount() const { return ContactWhiffCount; }

	/** The last blow that landed: where, which way, how hard. */
	const FHawkeyeStrikeImpact& GetLastImpact() const { return LastImpact; }

	/** True when the current swing's hit is found by its limbs' sweep (a clip times it), not the sweep from the capsule. */
	UFUNCTION(BlueprintPure, Category = "Melee|Contact")
	bool IsLimbSweep() const { return bLimbSweep; }

	/**
	 * The point on Target a swing of Contact aims at: the region's bone (head, spine, thigh) on his mesh, or his capsule
	 * centre when he is down or has no such bone.
	 */
	FVector ComputeContactPoint(const AActor& Target, const FHawkeyeStrikeContact& InContact) const;

	/**
	 * The plan for the current swing on its target from where the owner is now, scaled by the owner's size: past the slide
	 * cap (the attack's MaxSlide, or what Magnetism's closing speed covers over the wind-up, or over the slide's own
	 * ApproachSeconds for an in-place swing), through a wall or
	 * over a gap or a ledge it is not reachable and the swing whiffs.
	 */
	FHawkeyeContactPlan PlanCurrentContact() const;

	/** The pull: its speed cap and what stops it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Magnetism")
	FHawkeyeStrikeMagnetism Magnetism;

	/**
	 * Whether a swing may pull the owner from where he stands to StandPoint, on Target: nothing solid on the sight line
	 * between them, and a floor all the way he can walk (Magnetism's MaxStep and MaxDrop). OutWhy says which failed.
	 */
	bool IsStrikePathClear(const AActor& Target, const FVector& StandPoint, FName* OutWhy = nullptr) const;

	/** Nothing solid on the sight line from the owner's chest to Target's. */
	bool HasStrikeSight(const AActor& Target) const;

	/** The motion warping target a strike clip is warped to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Animation")
	FName WarpTargetName = FName(TEXT("CombatTarget"));

	/**
	 * Fit each clip so its ANS_HitWindow opens at the attack's WindupSeconds, so the telegraph keeps its
	 * length (thugs): at a rate between ClipFitMinRate and ClipFitMaxRate, starting later in a wind-up that
	 * is too long or holding the first frame of one that is too short (HawkeyeCombatAnim::FitHitToWindup).
	 * Off: clips play at rate 1 (Kate).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Animation")
	bool bFitClipToWindup = false;

	/** With bFitClipToWindup, the slowest a clip plays; a wind-up still too short holds its first frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Animation", meta = (ClampMin = "0.05"))
	float ClipFitMinRate = 0.8f;

	/** With bFitClipToWindup, the fastest a clip plays; a wind-up still too long starts part way in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Animation", meta = (ClampMin = "0.05"))
	float ClipFitMaxRate = 1.3f;

	/** A clip-timed swing ends on its own this long after its montage should have, s (notifies that never came). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Animation", meta = (ClampMin = "0.0"))
	float ClipTimeoutPadding = 0.5f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The end of the wind-up: sweep, damage, stagger or knockdown, or a miss. */
	void Strike();

	/**
	 * One sweep and its consequences. False when nobody was in reach (nothing broadcast); true when
	 * the swing is settled: it landed, a shield took it, or the target could not be hurt.
	 */
	bool ResolveSweep();

	/** Plays the attack's clip when it has one that will play, and takes the swing's timing from it. */
	bool TryPlaySwingClip(const FHawkeyeMeleeAttack& Attack, float& OutHitDelaySeconds);

	/** Picks the next variant of Attack's role and logs it; null when the role has none. */
	UAnimMontage* PickSwingVariant(const FHawkeyeMeleeAttack& Attack);

	/** Points the owner's motion warping at the swing's target (or clears it) for a root-motion clip. */
	void UpdateWarpTarget();

	/** The owner's motion warping component, made on first use when it has none. */
	UMotionWarpingComponent* FindOrAddMotionWarping() const;

	/** Steps a swing whose current phase a clip times; true when it did, so the timers are skipped. */
	bool AdvanceClipSwing(float DeltaSeconds);

	/** Ends a clip-timed swing: a hit left open is a miss, an open combo window closes. */
	void EndClipSwing(bool bInterrupted);

	/** Closes the combo window if it is open, telling listeners. */
	void CloseComboWindow();

	/** Forgets the current clip and its windows, quietly (a new swing is taking over). */
	void ResetClipState();

	/** True when Animation is the current swing's montage, or there is none to compare with (tests). */
	bool IsCurrentClip(const UAnimSequenceBase* Animation) const;

	/**
	 * The procedural step into the swing, as a root motion force on a character owner; toward an assist target
	 * it grows to close the gap, finishing a little before HitDelaySeconds.
	 */
	void ApplyLunge(const FHawkeyeMeleeAttack& Attack, float HitDelaySeconds);

	/** The assist target when the sweep missed him but the forgiveness still reaches him, else null. */
	AActor* FindForgivenTarget() const;

	/** An in-place swing's slide to its contact distance over the end of the wind-up (thugs: no lunge, no warp). */
	void UpdateApproach();

	/** Marks the swing a whiff (out of its caps), once, with the plan's reason in the log. */
	void MarkContactWhiff(const TCHAR* Where);

	/** Where the striking limbs (and a held weapon's length) are now. False when the owner has no such bones. */
	bool GatherStrikePoints(TArray<FVector>& OutPoints) const;

	/**
	 * The first living pawn the limbs' spheres meet between their last places and now; OutPoint where, OutVelocity the
	 * striking limb's motion. Updates the last places.
	 */
	AActor* FindTargetByLimbs(FVector& OutPoint, FVector& OutVelocity);

	/** A candidate the swing may hurt: alive, with health, not the owner, not IgnoreTag. */
	bool IsStrikeCandidate(const AActor* Candidate) const;

	/** What the landed blow is: the contact point, its direction, its weight. */
	FHawkeyeStrikeImpact BuildImpact(const AActor& Target, const FVector* LimbPoint, const FVector& LimbVelocity) const;

	/** The hit-stop on both parties and the struck body's reaction, after the damage. */
	void DeliverImpact(AActor& Target, const FHawkeyeStrikeImpact& Impact, bool bTargetAlive);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Melee")
	EMeleePhase Phase = EMeleePhase::Idle;

	UPROPERTY(Transient)
	FHawkeyeMeleeAttack CurrentAttack;

	UPROPERTY(Transient)
	float PhaseRemaining = 0.f;

	int32 BlockedCount = 0;

	float LastLungeDistance = 0.f;
	int32 ForgivenHitCount = 0;

	/** SetNextAttackDirection's value, waiting for the next swing, and the one the current swing took. */
	FVector PendingDirection = FVector::ZeroVector;
	FVector SwingDirection = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UCombatAnimSet> AnimSet;

	/** The current swing's clip and the instance it plays on. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CurrentMontage;

	TWeakObjectPtr<UAnimInstance> MontageInstance;

	/** The variant the current (or last) swing picked, and its index. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PickedMontage;

	int32 PickedVariantIndex = -1;

	/** Per role, the variant its next swing takes. */
	TMap<ECombatAnimRole, int32> VariantCursors;

	/** SetNextAttackTarget's value, and the one the current swing took. */
	TWeakObjectPtr<AActor> PendingTarget;
	TWeakObjectPtr<AActor> SwingTarget;

	bool bHitFromNotify = false;
	bool bComboFromNotify = false;
	bool bHitWindowOpen = false;
	bool bComboWindowOpen = false;

	/** The hit window found someone, or ended without: the swing's hit is settled. */
	bool bSwingResolved = false;

	/** The clip moves the owner (root motion on the main instance), so no lunge. */
	bool bClipMovesOwner = false;

	bool bForceNotifyTimingForTest = false;

	/** Seconds until a clip-timed swing gives up on its notifies. */
	float ClipTimeRemaining = 0.f;

	/** Seconds the fitted clip still holds its first frame before it plays (a wind-up shorter than the telegraph). */
	float ClipHoldRemaining = 0.f;

	/** The fitted clip's play rate, for resuming it after the hold. */
	float ClipRate = 1.f;

	/** Game time the current swing started, for the log line when its hit window opens. */
	double SwingStartTime = 0.0;

	/** Game time the current swing's hit is due (its clip's hit window, or the wind-up), for the closing speed cap. */
	double SwingHitDueTime = 0.0;

	/** Counts swings, so a cut montage's end event is not taken for the swing that cut it. */
	int32 SwingSerial = 0;

	FHawkeyeContactPlan ContactPlan;
	bool bContactWhiff = false;
	bool bApproachDone = false;
	int32 ContactWhiffCount = 0;
	bool bLimbSweep = false;
	FHawkeyeStrikeImpact LastImpact;

	/** The striking limbs' places at the last tick, for the sweep across the hit window. */
	TArray<FVector> LastLimbPoints;
	double LastLimbTime = 0.0;
};
