#pragma once

#include "CoreMinimal.h"
#include "Boss/BossTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BossRules.generated.h"

/**
 * The boss's combo and its winded window, as a clock with no world. UBossFightSubsystem keeps one per fight and
 * feeds it the boss controller's swing count; Hawkeye.Boss.Rules.* drive it directly.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossComboClock
{
	GENERATED_BODY()

	/** Swings started (in total) when the current combo began. */
	int32 ComboStart = 0;

	float WindedRemaining = 0.f;

	/** Times he has been winded. */
	int32 WindedCount = 0;

	bool IsWinded() const { return WindedRemaining > 0.f; }

	/** Starts counting a combo from SwingsStarted (a phase start, a restore). Not winded. */
	void Reset(int32 SwingsStarted);

	/**
	 * One frame: the winded clock runs down; then, not winded, with WindedAfterSwings or more swings started since the
	 * combo began and the last of them finished (bAttacking false), he goes winded for WindedSeconds and the next
	 * combo starts counting from now. True on the frame he goes winded.
	 */
	bool Step(float DeltaSeconds, int32 SwingsStarted, bool bAttacking, const FBossMoveSet& Moves);

	/**
	 * Winded now for Seconds, whatever the combo (his guard broken, the hazard caught him): the next combo counts from
	 * SwingsStarted. True when he was not winded already (then it counts); already winded, nothing changes.
	 */
	bool ForceWinded(int32 SwingsStarted, float Seconds);
};

/**
 * A boss fight's rules, pure and world-free (claude-docs/gameplay-semantics.md, "Bosses"): which phase health and
 * the crew put him in, when he takes damage, the winded window's damage, the finisher on him, the checkpoint a load
 * restarts at, the hazard's cycle and its hits, and the lines' once-a-fight rule. UBossPhaseComponent and
 * UBossFightSubsystem apply them; Hawkeye.Boss.Rules.* tests call them directly.
 */
UCLASS()
class HAWKEYE_API UBossRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The phase after a change: from Current, on through every next phase whose entry is met (HealthBelow: health
	 * strictly below its threshold; CrewDown: bCrewDown, which only opens the phase right after Current). A
	 * CrewDown phase that is not met stops the walk, so overkill never skips a crew. Dead: Current (the killing
	 * blow never advances a phase).
	 */
	static int32 ResolvePhase(const TArray<FBossPhase>& Phases, int32 Current, float HealthFraction, bool bCrewDown, bool bDead);

	/** Phases in the order the component will run them: thresholds never rising. */
	static bool IsPhaseOrderValid(const TArray<FBossPhase>& Phases);

	/** He takes damage: not during a phase that holds him out of it, and not during a protected transition. */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool IsBossDamageable(bool bPhaseInvulnerable, bool bTransitioning, bool bTransitionInvulnerable);

	/** The extra damage a hit of Damage (positive) does while he is winded: Damage * (Scale - 1), 0 otherwise. */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static float WindedBonusDamage(float Damage, bool bWinded, float Scale);

	/**
	 * A finisher on him: FinisherFraction of his max health, or all he has left when that is less (then it kills).
	 * A fraction of 0 is a thug's finisher: everything he has.
	 */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static float FinisherDamage(float CurrentHealth, float MaxHealth, float FinisherFraction);

	/**
	 * The checkpoint a load restarts the fight at, from the objectives the save has: -1 before the meet objective (he
	 * waits on the roof), else the first phase whose objective is not complete (a phase without one counts as
	 * complete once a later one is, and as open otherwise). Phases.Num() when every one is complete.
	 */
	static int32 CheckpointPhase(FName MeetObjective, const TArray<FName>& PhaseObjectives, const TArray<FName>& Completed);

	/**
	 * His health at a phase's checkpoint, as a fraction: full for the first phase and for any a crew opens, the
	 * phase's threshold for one health opens (on it, not under: the next hit carries on from there).
	 */
	static float CheckpointHealthFraction(const TArray<FBossPhase>& Phases, int32 PhaseIndex);

	/**
	 * The hazard's cycle Seconds after it came loose: FirstDelay waiting, then telegraph, sweep, rest, again, each
	 * sweep going back the way the last came.
	 */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static FBossHazardState HazardStateAt(float Seconds, const FBossHazardTuning& Tuning);

	/**
	 * The hazard hits: only while it sweeps, only within the lane (DistanceAcross under HalfWidth, DistanceAlong under
	 * HalfLength of its body), and not again on the same victim within HitCooldownSeconds.
	 */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool HazardHits(EBossHazardPhase Phase, float DistanceAcross, float DistanceAlong, float SecondsSinceLastHit,
		const FBossHazardTuning& Tuning);

	/** A bark may speak: never twice when bOnce, else not within CooldownSeconds of the last time. */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool CanBark(bool bOnce, int32 TimesSpoken, float SecondsSinceLast, float CooldownSeconds);

	/** The intro is over: its lines are done (after a second), it ran IntroMaxSeconds, or she hit someone. */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool IsIntroOver(float IntroSeconds, bool bDialogueBusy, bool bSomeoneHit, float IntroMaxSeconds);

	/** Point-in-polygon on the arena's outline, and her feet no more than 60 cm under the roof or 250 over it. */
	static bool IsOnRoof(const TArray<FVector2D>& Outline, float RoofZ, const FVector& Feet);

	// --- The solves (second pass, 2026-11-10) ------------------------------------------------------------------

	/**
	 * A finisher can take him: with bNeedsGrantedWindow (a boss) only inside a granted window (his winded window); a
	 * thug also when knocked down, parry-staggered or inside a combo ender's window.
	 */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool IsFinisherOpen(bool bKnockedDown, bool bParryStaggered, float ComboEnderWindow, float GrantedWindow, bool bNeedsGrantedWindow);

	/** He guards: the phase gives him a guard (a fraction under 1), he is fighting, and he is not winded. */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool IsGuarding(float GuardDamageFraction, bool bFighting, bool bWinded);

	/** What every hit on him is multiplied by: 0 in a beat, the guard's fraction while he guards, else 1. */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static float BossDamageScale(bool bInBeat, bool bGuarding, float GuardDamageFraction);

	/** An arrow's effect breaks his guard: he guards, the effect is one of Sources and it landed within Radius of him. */
	static bool BreaksGuard(EArrowHitEffect Effect, float Distance, const TArray<EArrowHitEffect>& Sources, float Radius, bool bGuarding);

	/**
	 * A finisher on him ends something: its blow (FinisherDamage times DamageScale, his winded scale) kills him, or puts
	 * his health fraction strictly under NextThreshold (the next phase's, when health opens it; below 0: none).
	 */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool IsDecisiveFinisher(float CurrentHealth, float MaxHealth, float FinisherFraction, float DamageScale, float NextThreshold);

	/**
	 * A beat's camera: Beat.CameraOffset in the frame of the flat line from Kate to the boss (X along it from their
	 * middle, Y to its right, Z up from the middle's height), looking at the point Beat.LookAtBoss of the way from her
	 * to him (both at chest height, 40 cm over the actor), then pushed back along its view (up to BeatMaxPushBack) until
	 * both of them are inside BeatFrameFraction of half its field of view. Kate on top of him: the frame faces +X.
	 */
	static void BeatCamera(const FVector& Kate, const FVector& Boss, const FBossBeat& Beat, FVector& OutLocation, FRotator& OutRotation);

	/** A beat is over: it ran its Seconds, or the skip key was pressed after its first BeatMinSkipSeconds. */
	UFUNCTION(BlueprintPure, Category = "Boss|Rules")
	static bool IsBeatOver(float Elapsed, float Seconds, bool bSkipPressed);

	/** The lines of Lines not yet in Heard, in order: a line already played is never played again. */
	static TArray<FName> UnheardLines(const TArray<FName>& Lines, const TArray<FName>& Heard);

	/**
	 * A putty arrow pins the hazard: it is Putty, the tuning pins (PinSeconds over 0), and it landed within PinRadius of
	 * the body or of the tether (the lane's start).
	 */
	static bool PinsHazard(EArrowHitEffect Effect, float DistanceToBody, float DistanceToTether, const FBossHazardTuning& Tuning);

	/**
	 * Where a prop the hazard pushes ends up along its lane, cm from the lane's start: a prop within HalfWidth plus its
	 * Radius across, and from half a body behind to HalfLength ahead of the body (Direction +1 going toward the end, -1
	 * back), goes HalfLength ahead of the body; any other stays at PropAlong. Never more than HalfLength past the lane's
	 * ends (where the body rests it has passed the prop, so the sweep back leaves it).
	 */
	static float PushedAlong(float PropAlong, float PropAcross, float PropRadius, float BodyAlong, float Direction, float LaneLength,
		const FBossHazardTuning& Tuning);

	static constexpr float RoofBelowTolerance = 60.f;
	static constexpr float RoofAboveTolerance = 250.f;
	static constexpr float IntroMinSeconds = 1.f;
	/** A beat cannot be skipped in its first this, s (so a held attack does not skip it). */
	static constexpr float BeatMinSkipSeconds = 0.4f;
	/** A beat's camera keeps both of them inside this fraction of half its field of view, backing off up to BeatMaxPushBack cm. */
	static constexpr float BeatFrameFraction = 0.75f;
	static constexpr float BeatMaxPushBack = 3000.f;
};
