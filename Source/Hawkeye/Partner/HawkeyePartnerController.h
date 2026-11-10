#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Partner/PartnerTypes.h"
#include "HawkeyePartnerController.generated.h"

class AHawkeyeCharacter;
class UDamageType;
class UHealthComponent;
class UStateTree;
class UStateTreeAIComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPartnerFightEndedSignature, AHawkeyePartnerController*, Partner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPartnerModeChangedSignature, EHawkeyePartnerMode, OldMode,
	EHawkeyePartnerMode, NewMode);

/**
 * The AI Hawkeye (claude-docs/gameplay-semantics.md, "partner and switching"). Possesses whichever
 * of Kate and Clint the player is not playing and backs up the lead: follows at FollowMinDistance
 * to FollowMaxDistance, takes cover when shot at, attacks what the lead hit in the last
 * AttackWindowSeconds (bow at range, bow strikes inside MeleeRange), goes to a point the lead marks,
 * and gets the lead back up once per fight.
 *
 * Which mode runs is a StateTree (PartnerStateTree, ST_Partner, built headless by
 * UHawkeyePartnerTreeBuilder): a state per mode in priority order, each entered on a
 * FHawkeyePartnerNeedCondition and running a FHawkeyePartnerModeTask that calls RunMode. The
 * controller keeps the senses (who the lead hit, who shot at him, the fight clock) on a
 * ThinkIntervalSeconds timer and exposes them as needs. Without a tree (automation tests, a missing
 * asset) the same timer picks the mode itself with ChooseMode, in the same order.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AHawkeyePartnerController : public AAIController
{
	GENERATED_BODY()

public:
	AHawkeyePartnerController();

	// --- Who --------------------------------------------------------------------------------------

	/** The Hawkeye the player controls, whom this one backs up. Rebinds the lead's hit and health events. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void SetLeader(AHawkeyeCharacter* NewLeader);

	UFUNCTION(BlueprintPure, Category = "Partner")
	AHawkeyeCharacter* GetLeader() const { return Leader.Get(); }

	/** The Hawkeye this controller drives. */
	UFUNCTION(BlueprintPure, Category = "Partner")
	AHawkeyeCharacter* GetPartner() const;

	// --- Tuning -----------------------------------------------------------------------------------

	/** Closer than this to the lead he steps back out, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Follow", meta = (ClampMin = "0.0"))
	float FollowMinDistance = 400.f;

	/** Further than this he comes after the lead, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Follow", meta = (ClampMin = "0.0"))
	float FollowMaxDistance = 800.f;

	/** Where he settles when he moves, cm from the lead, on the line from the lead to him. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Follow", meta = (ClampMin = "0.0"))
	float FollowSettleDistance = 600.f;

	/** Beyond this he sprints rather than runs, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Follow", meta = (ClampMin = "0.0"))
	float SprintDistance = 1500.f;

	/** A height difference above this (the lead on a roof, him on the street) can end in a grapple catch-up, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Follow", meta = (ClampMin = "0.0"))
	float CatchUpVerticalGap = 300.f;

	/** Seconds out of the follow band, with that height gap, before the catch-up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Follow", meta = (ClampMin = "0.0"))
	float CatchUpDelaySeconds = 5.f;

	/** How far from the lead a catch-up anchor may be, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Follow", meta = (ClampMin = "0.0"))
	float CatchUpAnchorRadius = 1500.f;

	/** Enemies the lead hit this recently are his targets, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Attack", meta = (ClampMin = "0.0"))
	float AttackWindowSeconds = 3.f;

	/** Inside this he uses bow strikes instead of arrows, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Attack", meta = (ClampMin = "0.0"))
	float MeleeRange = 200.f;

	/** Beyond this he closes in before drawing, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Attack", meta = (ClampMin = "0.0"))
	float BowRange = 3000.f;

	/** Held past full draw before he lets go, seconds, so every shot is a full-power one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Attack", meta = (ClampMin = "0.0"))
	float ReleaseAfterFullDrawSeconds = 0.05f;

	/** Between one arrow and the next draw, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Attack", meta = (ClampMin = "0.0"))
	float ShotCooldownSeconds = 0.6f;

	/** How far above the target's capsule centre he aims: the chest, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Attack")
	float ChestHeight = 40.f;

	/** In cover this long after the last shot at him, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Cover", meta = (ClampMin = "0.0"))
	float CoverSeconds = 4.f;

	/** How far he looks for a spot out of the shooter's line, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Cover", meta = (ClampMin = "0.0"))
	float CoverSearchRadius = 800.f;

	/** Seconds beside the downed lead before she is up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Revive", meta = (ClampMin = "0.0"))
	float ReviveSeconds = 3.f;

	/** Fraction of max health she gets back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Revive", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveHealthFraction = 0.3f;

	/** Close enough to start the revive, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Revive", meta = (ClampMin = "0.0"))
	float ReviveReach = 200.f;

	/**
	 * If he has not reached her in this long he is put beside her, seconds. With ReviveSeconds it has to
	 * fit inside the lead's DownedMaxSeconds (8): 4.5 + 3 lands the revive at 7.5 s at the latest.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Revive", meta = (ClampMin = "0.0"))
	float ReviveTimeoutSeconds = 4.5f;

	/** The lead this far from a held mark releases him back to following, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner|Command", meta = (ClampMin = "0.0"))
	float MarkReleaseDistance = 2000.f;

	/** How often the senses (and, without a tree, the mode) update, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner", meta = (ClampMin = "0.01"))
	float ThinkIntervalSeconds = 0.1f;

	/** Below this fraction of max health he is patched back up to full: immortal for now. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SelfHealBelowFraction = 0.5f;

	/** The fight clock: contact keeps a fight going, and the revive is once per fight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner")
	FHawkeyeFightClock FightClock;

	/** ST_Partner. Without one the controller picks its mode in C++ (same order). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner")
	TObjectPtr<UStateTree> PartnerStateTree;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Partner")
	TObjectPtr<UStateTreeAIComponent> StateTreeComponent;

	// --- Rules (pure, tested) ---------------------------------------------------------------------

	/**
	 * Whether he should move to stay in the band, and where: outside [MinDistance, MaxDistance]
	 * (2D, inclusive) he goes to SettleDistance from the lead on the line from the lead to him.
	 */
	static bool ComputeFollowGoal(const FVector& LeaderLocation, const FVector& PartnerLocation, float MinDistance,
		float MaxDistance, float SettleDistance, FVector& OutGoal);

	/**
	 * Who to attack: of the living actors the lead hit within WindowSeconds of Now, the most recently
	 * hit, the nearest to From on a tie. Null when there is none.
	 */
	static AActor* SelectAttackTarget(const TArray<FHawkeyeLeaderHit>& Hits, double Now, float WindowSeconds,
		const FVector& From);

	/** Grapple catch-up: the height gap is over MinGap and he has been out of range for Delay or more. */
	static bool ShouldGrappleCatchUp(float VerticalGap, float SecondsOutOfRange, float MinGap, float Delay)
	{
		return VerticalGap > MinGap && SecondsOutOfRange >= Delay;
	}

	// --- Events -----------------------------------------------------------------------------------

	/** The lead hit Target (an arrow or a strike). Bound to the lead's bow and melee; tests call it. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void NotifyLeaderHit(AActor* Target);

	/** Someone hurt him: cover for CoverSeconds, away from Shooter. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void NotifyShotAt(AActor* Shooter);

	/**
	 * The lead went down. True when he takes it on (the revive is not yet spent this fight): she stays
	 * down until he has been beside her ReviveSeconds, then is up at ReviveHealthFraction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	bool RequestRevive(AHawkeyeCharacter* Downed);

	/** The lead marked Point (T): go and hold it. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void CommandMoveTo(FVector Point);

	/** Drops a mark order. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void CancelCommand();

	// --- Modes ------------------------------------------------------------------------------------

	/** Whether Mode's reason to run exists now. The StateTree's enter conditions ask this. Follow always does. */
	UFUNCTION(BlueprintPure, Category = "Partner")
	bool HasNeed(EHawkeyePartnerMode Mode) const;

	/** The first mode with a need, in priority order: Revive, GoToMark, Cover, Attack, Follow. */
	UFUNCTION(BlueprintPure, Category = "Partner")
	EHawkeyePartnerMode ChooseMode() const;

	/** One step of Mode, DeltaSeconds long. The StateTree task calls it every tick; so does the fallback. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void RunMode(EHawkeyePartnerMode Mode, float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Partner")
	EHawkeyePartnerMode GetMode() const { return CurrentMode; }

	/** The target Attack would pick right now, or null. */
	UFUNCTION(BlueprintPure, Category = "Partner")
	AActor* GetAttackTarget() const;

	/** "following", "fighting", "covering", "moving to the mark", "holding the mark", "reviving you", "down". */
	UFUNCTION(BlueprintPure, Category = "Partner")
	FText GetStatusText() const;

	UFUNCTION(BlueprintPure, Category = "Partner")
	bool IsInFight() const { return !bBenched && FightClock.IsInFight(GetNowSeconds()); }

	/**
	 * Benched: off stage for a chapter without him (UMissionDefinition::bPartnerPresent off). Thinking off, his
	 * pawn hidden with its collision and movement off; GetPartner returns null, so the HUD, the switch, the mark,
	 * banter, revives and the fight check all see no partner. Off again puts him back where he stands.
	 */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void SetBenched(bool bInBenched);

	UFUNCTION(BlueprintPure, Category = "Partner")
	bool IsBenched() const { return bBenched; }

	/** The senses and, without a StateTree, the mode. The timer calls it; tests call it directly. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void Think(float DeltaSeconds);

	/** Off: he stands still and nothing updates. For the screenshot pass and scripted moments. */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	void SetThinkingEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Partner")
	bool IsThinkingEnabled() const { return bThinkingEnabled; }

	/** True while ST_Partner is running this controller. */
	UFUNCTION(BlueprintPure, Category = "Partner")
	bool IsUsingStateTree() const;

	/** World seconds, or the test clock. */
	double GetNowSeconds() const;

	/** Overrides the clock for automation tests. */
	void SetTestTimeSeconds(double Seconds);

	/** When the fight clock runs out (10 s after the last contact). Banter listens. */
	UPROPERTY(BlueprintAssignable, Category = "Partner")
	FOnPartnerFightEndedSignature OnFightEnded;

	UPROPERTY(BlueprintAssignable, Category = "Partner")
	FOnPartnerModeChangedSignature OnModeChanged;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void TickThink();

	/** Takes the player's pawn as the lead when there is none yet. */
	void EnsureLeader();

	/** Contact from thugs rushing a Hawkeye, the fight-over edge, self heal, catch-up clock. */
	void UpdateSenses(float DeltaSeconds);

	/** Leaving one mode for another: drop the draw, the focus and the move. */
	void EnterMode(EHawkeyePartnerMode NewMode);

	void TickFollow(float DeltaSeconds);
	void TickAttack(float DeltaSeconds);
	void TickCover(float DeltaSeconds);
	void TickGoToMark(float DeltaSeconds);
	void TickRevive(float DeltaSeconds);

	/** Bow or strike at Target; closes in only with bAllowMove. */
	void EngageTarget(AActor* Target, float DeltaSeconds, bool bAllowMove);

	/** Draws on Target's chest, holds past full draw, looses. */
	void UpdateBowShot(AActor* Target, float DeltaSeconds);

	/** Stops a draw in progress without firing and forgets the aim point. */
	void CancelBowShot();

	/** A path move to Goal unless one is already heading there. */
	void MoveTowards(const FVector& Goal, float AcceptanceRadius);

	/** Nearest spot within CoverSearchRadius that the line from Threat does not reach. */
	bool FindCoverPoint(const FVector& Threat, FVector& OutPoint) const;

	/** The grapple catch-up: a zip to the anchor nearest the lead, or a jump straight to it. */
	void GrappleCatchUp();

	bool HasLineOfSight(const AActor* Target) const;

	void BindPartnerEvents(AHawkeyeCharacter* Partner, bool bBind);
	void BindLeaderEvents(AHawkeyeCharacter* InLeader, bool bBind);

	UFUNCTION()
	void HandleLeaderBowHit(AActor* HitActor, float Damage, bool bHeadshot);

	UFUNCTION()
	void HandleLeaderMeleeLanded(AActor* HitActor, float DamageDealt, FName AttackName);

	UFUNCTION()
	void HandleLeaderHealthChanged(UHealthComponent* Health, float NewHealth, float Delta, AActor* DamageInstigator);

	UFUNCTION()
	void HandlePartnerHealthChanged(UHealthComponent* Health, float NewHealth, float Delta, AActor* DamageInstigator);

	UFUNCTION()
	void HandlePartnerBowHit(AActor* HitActor, float Damage, bool bHeadshot);

	UFUNCTION()
	void HandlePartnerMeleeLanded(AActor* HitActor, float DamageDealt, FName AttackName);

	UPROPERTY(Transient)
	TWeakObjectPtr<AHawkeyeCharacter> Leader;

	/** The lead's recent hits, one entry per target, latest time. */
	UPROPERTY(Transient)
	TArray<FHawkeyeLeaderHit> LeaderHits;

	UPROPERTY(Transient)
	TWeakObjectPtr<AHawkeyeCharacter> ReviveTarget;

	UPROPERTY(Transient)
	EHawkeyePartnerMode CurrentMode = EHawkeyePartnerMode::Follow;

private:
	double UnderFireUntil = -1.0;
	FVector ThreatLocation = FVector::ZeroVector;
	bool bHasCoverPoint = false;
	FVector CoverPoint = FVector::ZeroVector;

	bool bHasMark = false;
	FVector MarkPoint = FVector::ZeroVector;
	bool bAtMark = false;

	float ReviveElapsed = 0.f;
	float ReviveWaitElapsed = 0.f;

	float OutOfRangeSeconds = 0.f;
	bool bWasInFight = false;

	/** When the last arrow left, for ShotCooldownSeconds. */
	double LastShotSeconds = -100.0;

	/** The last goal handed to path following, so a held goal is not re-requested every tick. */
	bool bHasMoveGoal = false;
	FVector LastMoveGoal = FVector::ZeroVector;
	bool bLoggedMoveFailure = false;

	bool bThinkingEnabled = true;
	bool bBenched = false;
	bool bEverPossessed = false;
	bool bUseTestTime = false;
	double TestTimeSeconds = 0.0;

	FTimerHandle ThinkTimerHandle;
};
