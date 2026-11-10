#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "World/ThugCharacter.h"
#include "World/ThugTypes.h"
#include "World/ThugAim.h"
#include "ThugAIController.generated.h"

class AGrappleAnchor;
class UAIPerceptionComponent;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UStateTreeAIComponent;

/** Where a stimulus came from, so tests can drive the state machine without a perception system. */
UENUM(BlueprintType)
enum class EStimulusKind : uint8
{
	Sight,
	Hearing
};

/**
 * Thug brain. The senses (perception, the archer's own sight check, smoke, the squad alert, the
 * clocks that move Calm, Suspicious and Alerted) run on a 0.25 s think timer. What he does is one
 * EThugMode at a time, chosen by a StateTree (the pawn's ThugStateTree, ST_Thug, built headless by
 * UHawkeyeThugTreeBuilder in the partner's pattern): a state per mode in priority order, each entered
 * on a FHawkeyeThugNeedCondition and running a FHawkeyeThugModeTask that calls RunMode. Without a tree
 * (tests, a thug spawned from C++) Think picks the mode itself with ChooseMode, in the same order.
 *
 * Patrol      Calm: PatrolPoints, waiting PatrolWaitSeconds at each
 * Investigate Suspicious: walk to the last stimulus, wait InvestigateSeconds, back to Calm
 * Attack      Alerted, by weapon. Fists and Bat rush in at RushSpeed; inside MeleeEngageRange (250)
 *             they wind up a swing, close to MeleeCloseDistance, swing again after
 *             MeleeCooldownSeconds, and back off after MeleeSwingsBeforeBackOff swings. The gunner
 *             fires 3-shot bursts after a 0.8 s telegraph and ducks into cover after a burst or when
 *             aimed at. The archer draws for 1.2 s and looses with lead at the chest.
 * Cover       the gunner at a cover point (EQS_CoverPoints, or a C++ ring of candidates): hides,
 *             peeks for a burst, and relocates every RelocateSeconds.
 * Reposition  the gunner backing off to RetreatToDistance when she is inside RetreatTriggerDistance;
 *             the archer zipping to another roof's grapple anchor when she is inside ArcherCloseRange.
 *
 * Alerted drops to Suspicious after LoseTargetSeconds with no perception. Going Alerted tells the
 * squad: after SquadAlertDelay, thugs within SquadAlertRadius with a line of sight to him turn
 * Suspicious toward where he last had her.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AThugAIController : public AAIController
{
	GENERATED_BODY()

public:
	AThugAIController();

	// --- Perception tuning ----------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Sight", meta = (ClampMin = "0.0"))
	float SightRadius = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Sight", meta = (ClampMin = "0.0"))
	float LoseSightRadius = 1800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Sight", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SightHalfAngleDegrees = 35.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Hearing", meta = (ClampMin = "0.0"))
	float HearingRange = 1200.f;

	// --- Behaviour tuning -----------------------------------------------------------------------

	/** Seconds the player must stay visible before Suspicious becomes Alerted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour", meta = (ClampMin = "0.0"))
	float SightConfirmSeconds = 0.6f;

	/** Seconds spent at the stimulus location before giving up and going back to Calm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour", meta = (ClampMin = "0.0"))
	float InvestigateSeconds = 4.f;

	/** Seconds without perceiving the player before Alerted drops back to Suspicious. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour", meta = (ClampMin = "0.0"))
	float LoseTargetSeconds = 5.f;

	/**
	 * The gunner's base scatter cone, degrees (gameplay-semantics.md, "Ranged thug accuracy"; was a flat 4):
	 * her sideways speed and the air widen it, the difficulty scales it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour", meta = (ClampMin = "0.0"))
	float AimSpreadDegrees = HawkeyeThugAim::GunnerBaseConeDegrees;

	/** The archer's base scatter cone, degrees, widened and scaled like the gunner's. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.0"))
	float ArcherAimConeDegrees = HawkeyeThugAim::ArcherBaseConeDegrees;

	/** Inside this the thug shoots instead of closing the distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour", meta = (ClampMin = "0.0"))
	float EngageRange = 1200.f;

	// --- Gunner --------------------------------------------------------------------------------------

	/** The telegraph (0.8 s, pistol raised, glint) and the burst (3 shots 0.25 s apart). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner")
	FHawkeyeBurstClock Burst;

	/** Between the end of one burst and the next telegraph when he stays in the open, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float BurstPauseSeconds = 1.f;

	/** Shots since he last took cover that send him back to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "1"))
	int32 ShotsBeforeCover = 3;

	/** How far he looks for a cover point, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float CoverSearchRadius = 800.f;

	/** At the cover point this long before he peeks, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float CoverHideSeconds = 1.2f;

	/** After this long at one cover point he moves to another, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float RelocateSeconds = 6.f;

	/** She closes inside this and he backs off, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float RetreatTriggerDistance = 300.f;

	/** Where he backs off to, cm from her. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float RetreatToDistance = 600.f;

	/** The longest a retreat lasts, arrived or not, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float RetreatMaxSeconds = 3.f;

	/** Her aim (camera forward while aiming or drawing) within this of his chest counts as aimed at him, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Gunner", meta = (ClampMin = "0.0"))
	float AimedAtToleranceDegrees = 6.f;

	// --- Archer --------------------------------------------------------------------------------------

	/** He only fights when she is this close and in his line of sight, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.0"))
	float ArcherAggroRange = 3000.f;

	/** The near edge of the band he keeps, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.0"))
	float ArcherMinRange = 1500.f;

	/** The far edge of the band, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.0"))
	float ArcherMaxRange = 2500.f;

	/** She closes inside this and he zips to another roof, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.0"))
	float ArcherCloseRange = 800.f;

	/**
	 * Between one arrow and the next draw, seconds. Below 0 (the default) it is drawn each time from the
	 * difficulty's ArcherShotGapMin/MaxSeconds (1.5 to 2.5 s at Normal); 0 or more fixes it (tests).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer")
	float ArcherShotCooldownSeconds = -1.f;

	/** Between one relocation and the next, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.0"))
	float ArcherRelocateCooldownSeconds = 5.f;

	/** The zip's speed, cm/s: the partner's grapple zip speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "1.0"))
	float ArcherZipSpeed = 1800.f;

	/** How far above her capsule centre he aims: the chest, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer")
	float ChestHeight = 30.f;

	/**
	 * Losing his line mid-draw: he holds up to 2.5 s and looses 0.1 s after she shows again inside
	 * ArcherHoldConeDegrees; after every shot, 0.6 s in which he cannot draw.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer")
	FHawkeyeArcherHoldClock ArcherHold;

	/** Half-angle round the held aim inside which her reappearing draws his shot, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float ArcherHoldConeDegrees = 12.f;

	/** How often the hold and the loose window are checked while either runs, seconds. Faster than Think. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Archer", meta = (ClampMin = "0.01", ClampMax = "0.1"))
	float ArcherHoldTickSeconds = 0.05f;

	// --- Squad ---------------------------------------------------------------------------------------

	/** After going Alerted, this long before the squad hears about it, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Squad", meta = (ClampMin = "0.0"))
	float SquadAlertDelay = 1.5f;

	/** Thugs this close, with a line of sight to him, are told, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Squad", meta = (ClampMin = "0.0"))
	float SquadAlertRadius = 1500.f;

	// --- Melee rush (Fists and Bat) --------------------------------------------------------------

	/** Walking speed while Alerted with a melee weapon, cm/s. Calm and Suspicious walk at the pawn's own speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Melee", meta = (ClampMin = "0.0"))
	float RushSpeed = 450.f;

	/** Inside this he commits: the swing's wind-up starts while he is still closing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Melee", meta = (ClampMin = "0.0"))
	float MeleeEngageRange = 250.f;

	/** How close he gets before he stops and lets the swing land, centre to centre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Melee", meta = (ClampMin = "0.0"))
	float MeleeCloseDistance = 120.f;

	/** Seconds after a swing ends before the next can start. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Melee", meta = (ClampMin = "0.0"))
	float MeleeCooldownSeconds = 1.2f;

	/** Swings in a row before he steps back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Melee", meta = (ClampMin = "1"))
	int32 MeleeSwingsBeforeBackOff = 2;

	/** How far he steps back after MeleeSwingsBeforeBackOff swings, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Melee", meta = (ClampMin = "0.0"))
	float MeleeBackOffDistance = 200.f;

	/** The longest a back-off lasts before he comes again, arrived or not. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Melee", meta = (ClampMin = "0.0"))
	float MeleeBackOffMaxSeconds = 1.5f;

	/** True while he is stepping back after a pair of swings. */
	UFUNCTION(BlueprintPure, Category = "Thug|Melee")
	bool IsBackingOff() const { return bBackingOff; }

	/** A downed target is circled at this distance, cm: nothing new is started on her (a swing under way finishes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour", meta = (ClampMin = "0.0"))
	float StandOffDistance = 450.f;

	/** Degrees a second he walks round a downed target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour")
	float StandOffOrbitDegreesPerSecond = 18.f;

	/** True while his target is a downed or dead player: he circles instead of attacking. */
	UFUNCTION(BlueprintPure, Category = "Thug|Behaviour")
	bool IsStandingOff() const { return bStandingOff; }

	/** Swings started in this thug's life (tests). */
	int32 GetSwingsStarted() const { return SwingsStarted; }

	/** True for a Hawkeye character who is downed or dead: no one attacks her. */
	static bool IsTargetDown(const AActor* Target);

	/** Swings since the last back-off. */
	UFUNCTION(BlueprintPure, Category = "Thug|Melee")
	int32 GetSwingsSinceBackOff() const { return SwingsSinceBackOff; }

	/** How often the state machine runs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Behaviour", meta = (ClampMin = "0.01"))
	float ThinkIntervalSeconds = 0.25f;

	/** Loudness at or above which a heard noise counts as a gunshot and alerts immediately. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Hearing", meta = (ClampMin = "0.0"))
	float GunshotLoudnessThreshold = 2.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug")
	TObjectPtr<UAIPerceptionComponent> ThugPerception;

	/**
	 * The one entry point into the state machine. The perception callback funnels into this, and
	 * tests call it directly so the whole thing is exercisable without a perception system.
	 *
	 * Loudness is only meaningful for Hearing; a value at or above GunshotLoudnessThreshold is
	 * treated as a gunshot and alerts the thug on the spot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void ReportStimulus(EStimulusKind Kind, FVector Location, bool bSuccessful, float Loudness = 1.f);

	/** Runs one step of the state machine. Called on a timer; tests call it directly. */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void Think(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Thug")
	AThugCharacter* GetThug() const;

	UFUNCTION(BlueprintPure, Category = "Thug")
	EThugAlertState GetAlertState() const;

	/** Last place a stimulus came from; where a Suspicious thug walks to. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	FVector GetLastStimulusLocation() const { return LastStimulusLocation; }

	/** The pawn the thug is attacking while Alerted, or nullptr. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	AActor* GetTarget() const { return TargetActor; }

	/** Points him at Target, as a sighting would. A hit from the player does this too. */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void SetTarget(AActor* Target) { TargetActor = Target; }

	/**
	 * Off: the think timer stops, he stands where he is, and perception is ignored. For scripted
	 * moments and the screenshot pass. On: the state machine picks up where it left off.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void SetThinkingEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsThinkingEnabled() const { return bThinkingEnabled; }

	/**
	 * A side challenge is running nearby (UChallengeSubsystem): he drops to Calm, lets go of his target,
	 * and neither sees, hears, nor takes a squad alert about anyone until cleared. He keeps patrolling.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void SetPacified(bool bInPacified);

	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsPacified() const { return bPacified; }

	// --- Trick arrow states (claude-docs/gameplay-semantics.md, "trick arrows") ------------------

	/** Putty: while held he does nothing at all, not even think. AHeldEffect sets and clears it. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Trick Arrows")
	void SetHeld(bool bInHeld);

	UFUNCTION(BlueprintPure, Category = "Thug|Trick Arrows")
	bool IsHeld() const { return bHeld; }

	/** EMP: his pistol will not fire for Seconds. A longer jam already running is kept. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Trick Arrows")
	void Jam(float Seconds);

	UFUNCTION(BlueprintPure, Category = "Thug|Trick Arrows")
	bool IsJammed() const { return JamRemaining > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Thug|Trick Arrows")
	float GetJamRemaining() const { return JamRemaining; }

	/**
	 * Smoke: true while he stands in a cloud, his line of sight to the player runs through one, or
	 * the player is crouched inside one. He cannot see, so he neither confirms a sighting nor
	 * attacks; he loses the target after LoseTargetSeconds like any other time he cannot see her.
	 */
	UFUNCTION(BlueprintPure, Category = "Thug|Trick Arrows")
	bool IsBlinded() const { return bBlinded; }

	/** Re-checks IsBlinded against the smoke clouds. Think runs it; public for tests. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Trick Arrows")
	void UpdateBlinded();

	/** True for a character crouched inside a smoke cloud: undetectable by sight or sound. */
	static bool IsHiddenInSmoke(const AActor* Actor);

	// --- Modes (the StateTree asks these) ------------------------------------------------------------

	/** Whether Mode's reason to run exists now. Patrol always does. */
	UFUNCTION(BlueprintPure, Category = "Thug|Modes")
	bool HasNeed(EThugMode Mode) const;

	/** The first mode with a need, in priority order: Stunned, Reposition, Cover, Attack, Investigate, Patrol. */
	UFUNCTION(BlueprintPure, Category = "Thug|Modes")
	EThugMode ChooseMode() const;

	/** One step of Mode, DeltaSeconds long. The StateTree task calls it every tick; so does the fallback. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Modes")
	void RunMode(EThugMode Mode, float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Thug|Modes")
	EThugMode GetMode() const { return CurrentMode; }

	/** True while ST_Thug is running this controller. */
	UFUNCTION(BlueprintPure, Category = "Thug|Modes")
	bool IsUsingStateTree() const;

	/** Any hit that cost him health: a burst or a draw in progress is broken. The thug calls it. */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void NotifyDamaged(AActor* By);

	/** A squad mate went Alerted and can see him: Suspicious toward Location unless he is already Alerted. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Squad")
	void ReceiveSquadAlert(FVector Location, AThugCharacter* From);

	/** Seconds until this thug's squad alert goes out; below 0 when none is pending. */
	UFUNCTION(BlueprintPure, Category = "Thug|Squad")
	float GetSquadAlertRemaining() const { return bSquadAlertPending ? SquadAlertRemaining : -1.f; }

	// --- Gunner state --------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	EGunnerPhase GetGunnerPhase() const { return GunnerPhase; }

	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	bool IsTelegraphing() const { return Burst.IsTelegraphing(); }

	/** Seconds the raised pistol has been up, below 0 when it is not raised. */
	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	float GetTelegraphElapsed() const { return Burst.IsTelegraphing() ? Burst.GetElapsed() : -1.f; }

	/**
	 * The rate an archer's bow counts its draw at so that a bow whose full draw is FullDrawSeconds
	 * reaches it in WantedSeconds (the difficulty's 1.5 / 1.2 / 1.0). Pure.
	 */
	static float ComputeArcherDrawRate(float FullDrawSeconds, float WantedSeconds)
	{
		return FullDrawSeconds > 0.f ? FullDrawSeconds / FMath::Max(WantedSeconds, 0.1f) : 1.f;
	}

	/** A copy of Attack with the difficulty's thug damage multiplier on it. */
	FHawkeyeMeleeAttack ScaleAttackForDifficulty(const FHawkeyeMeleeAttack& Attack) const;

	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	bool IsBursting() const { return Burst.IsActive(); }

	/**
	 * Kate parried the raised pistol: the burst's first shot is deflected (fires nothing). Only while the
	 * telegraph is showing; false otherwise. The rest of the burst comes as usual.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug|Gunner")
	bool DeflectFirstShot();

	/** A parry is waiting to take the burst's first shot. */
	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	bool IsFirstShotDeflected() const { return bDeflectNextShot; }

	/** Shots a parry has deflected. */
	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	int32 GetDeflectedShots() const { return DeflectedShots; }

	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	bool HasCoverPoint() const { return bHasCover; }

	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	FVector GetCoverPoint() const { return CoverPoint; }

	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	bool IsRetreating() const { return bRetreating; }

	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	FVector GetRetreatGoal() const { return RetreatGoal; }

	/** True when the player was aiming at him at the last think. */
	UFUNCTION(BlueprintPure, Category = "Thug|Gunner")
	bool IsAimedAt() const { return bAimedAt; }

	/**
	 * The nearest point within CoverSearchRadius that the line from Threat does not reach, through
	 * the pawn's CoverQuery (EQS) when there is one and the EQS manager exists, otherwise rings of
	 * candidates in C++. Points within 200 cm of Avoid (the cover he is leaving) are skipped.
	 */
	bool FindCoverPoint(const FVector& Threat, FVector& OutPoint, const FVector* Avoid = nullptr) const;

	// --- Ranged aim (gameplay-semantics.md, "Ranged thug accuracy") -----------------------------------

	/** Takes one look at the target for the tracking lag (the fast timer calls it; public for tests). */
	void RecordTargetSample();

	/** What he has seen of the target lately. */
	const FHawkeyeTargetTrack& GetTargetTrack() const { return TargetTrack; }

	/**
	 * Where a shot from From at ProjectileSpeed (0 for the pistol) with a BaseConeDegrees cone goes at the
	 * target now: the lagged lead, the cone, the forced miss after a jink. Logs a forced miss.
	 */
	FHawkeyeRangedAim ComputeRangedAim(const FVector& From, float ProjectileSpeed, float BaseConeDegrees);

	/** Shots sent wide on purpose because she jinked. */
	UFUNCTION(BlueprintPure, Category = "Thug|Ranged")
	int32 GetForcedMisses() const { return ForcedMisses; }

	/** The gap before an archer's next draw, from his last release, s. */
	UFUNCTION(BlueprintPure, Category = "Thug|Archer")
	float GetNextArcherGapSeconds() const { return NextArcherGapSeconds; }

	/** Picks the gap before his next draw (ArcherShotCooldownSeconds, or the difficulty's range). Each release and broken draw calls it. */
	void RollArcherGap();

	/** Replaces the aim scatter stream so a test gets the same shots every run. */
	void SetAimRandomStream(const FRandomStream& InStream) { AimStream = InStream; }

	// --- Archer state --------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Thug|Archer")
	bool IsZipping() const { return bZipping; }

	UFUNCTION(BlueprintPure, Category = "Thug|Archer")
	FVector GetZipEnd() const { return ZipTo; }

	/** Arrows this archer has loosed. */
	UFUNCTION(BlueprintPure, Category = "Thug|Archer")
	int32 GetArrowsLoosed() const;

	/** True while he holds a draw with no line, waiting for her to show. */
	UFUNCTION(BlueprintPure, Category = "Thug|Archer")
	bool IsHoldingDraw() const { return ArcherHold.IsHolding(); }

	/** True in the LooseSeconds after a shot: he cannot draw. */
	UFUNCTION(BlueprintPure, Category = "Thug|Archer")
	bool IsInLooseWindow() const { return ArcherHold.IsLoose(); }

	/** One step of the hold and the loose window. The fast timer calls it; public for tests. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Archer")
	void TickArcherHold(float DeltaSeconds);

	// --- Rules (pure, tested) ------------------------------------------------------------------------

	/** Whether a view from ViewLocation along ViewDirection points within ToleranceDegrees of Point. */
	static bool IsAimedAtPoint(const FVector& ViewLocation, const FVector& ViewDirection, const FVector& Point,
		float ToleranceDegrees);

	/**
	 * Where to aim an arrow of ProjectileSpeed from From so it meets a target at TargetLocation moving
	 * at TargetVelocity: the intercept point, raised by the drop gravity (GravityZ, negative) causes
	 * over the flight. Falls back to the target itself when no intercept exists.
	 */
	static FVector ComputeLeadAimPoint(const FVector& From, const FVector& TargetLocation, const FVector& TargetVelocity,
		float ProjectileSpeed, float GravityZ);

	/** Hold, step back, approach or relocate for an archer Distance (2D) from his target. */
	static EArcherRangeAction ChooseArcherRangeAction(float Distance, float CloseRange, float MinRange, float MaxRange);

	/**
	 * The gunner's retreat: true when Player is inside TriggerDistance (2D) of Thug, with OutGoal
	 * RetreatDistance from Player on the line from her through him.
	 */
	static bool ComputeRetreatGoal(const FVector& Thug, const FVector& Player, float TriggerDistance, float RetreatDistance,
		FVector& OutGoal);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug")
	TObjectPtr<UStateTreeAIComponent> StateTreeComponent;

	/**
	 * Where his eyes are and look, for perception's sight. Alerted: along his aim (the control rotation).
	 * Otherwise along his body, pitched by LookPitch: this controller never ticks, so the control rotation
	 * does not follow a patrolling body, and a cone left on it looked where he spawned facing.
	 */
	virtual void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	/** Degrees his calm gaze is pitched (below 0 is down): a patrol point's pitch while he waits there. */
	float GetLookPitch() const { return LookPitch; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Timer body. */
	void TickThink();

	void TickCalm(float DeltaSeconds);
	void TickSuspicious(float DeltaSeconds);
	void TickAlerted(float DeltaSeconds);

	/** Moves to the current patrol point, advancing the index once it is reached. */
	void AdvancePatrol();

	/**
	 * MoveToActor / MoveToLocation with the failure reported. A goal off the navmesh (she hangs off
	 * a ledge, lands a zip by a parapet, is down against a wall) is walked to its nearest navigable
	 * point within SnapExtent; with none, or no path from where he stands, he holds position and it
	 * is logged at Verbose. Only a world with no navigation data at all warns (once per controller):
	 * that is a missing NavMeshBoundsVolume, not a bad goal. True when a move was started.
	 */
	bool RequestMoveToActor(AActor* Goal, float AcceptanceRadius);
	bool RequestMoveToLocation(const FVector& Goal, float AcceptanceRadius, const FVector& SnapExtent = NarrowNavSnap,
		const TCHAR* GoalName = nullptr);
	void ReportMoveResult(EPathFollowingRequestResult::Type Result, const FString& GoalDescription);
	/** Stops and logs, at Verbose, why the move to Goal was not started. */
	void HoldPosition(const FVector& Goal, const TCHAR* Why, const TCHAR* GoalName);
	/** True when there is a complete navmesh path from the pawn to Point (always true without navigation data). */
	bool IsReachable(const FVector& Point) const;

	/** How far a move goal is snapped onto the navmesh: 2 m for most, 3 m for cover, circling, noises and her. */
	static inline const FVector NarrowNavSnap = FVector(200.f, 200.f, 250.f);
	static inline const FVector WideNavSnap = FVector(300.f, 300.f, 300.f);

	/**
	 * Turns a stopped thug to face ToTarget. Only ever called when he is not moving: while he
	 * is, the movement component orients him along his own velocity so the walk cycle points the
	 * way he is going.
	 */
	static void FaceTarget(APawn* Thug, const FVector& ToTarget);

	/** One shot at TargetActor through the thug's weapon, scattered by AimSpreadDegrees. */
	void FireAtTarget();

	/** The gunner's Attack step: close to EngageRange, then telegraph and burst; to cover after a burst. */
	void TickGunner(float DeltaSeconds, const FVector& ToTarget);

	/** The gunner's Cover step: to the cover point, hide, then peek or relocate. */
	void TickGunnerCover(float DeltaSeconds);

	/** The gunner's Reposition step: back off to RetreatToDistance. */
	void TickGunnerRetreat(float DeltaSeconds);

	/** Picks a cover point away from the target and goes to it. False (and stays out) when there is none. */
	bool BeginCover(const TCHAR* Why, const FVector* Avoid = nullptr);

	/** The nearest spot beside CoverPoint with a line on the target, or CoverPoint itself. */
	FVector FindPeekPoint() const;

	/** Stops a burst in progress: glint off, pistol down. */
	void CancelBurst(const TCHAR* Why);

	/** The archer's Attack step: keep the band, draw, loose with lead. */
	void TickArcher(float DeltaSeconds, const FVector& ToTarget);

	/** The archer's Reposition step: the zip in flight. */
	void TickArcherZip(float DeltaSeconds);

	/** A zip to the anchor whose landing is best for the band. False when there is none. */
	bool StartArcherRelocation(const TCHAR* Why);

	/** Lets the string down without shooting. */
	void CancelArcherDraw(const TCHAR* Why);

	/** She went out of his line mid-draw: hold the draw where it points (ArcherHold). */
	void BeginArcherHold(const FVector& AimPoint);

	/** Looses the draw at Target now (a normal full draw, or a held one she stepped back into). */
	void LooseArcherDraw(const FVector& AimPoint, const TCHAR* Why);

	/** Runs TickArcherHold every ArcherHoldTickSeconds while a hold or a loose window is on. */
	void StartArcherHoldTimer();
	void TickArcherHoldTimer();

	/** The archer's own sight: she is within ArcherAggroRange and nothing is in the way. Think runs it. */
	void UpdateArcherSight();

	/** The gunner's read of whether she is aiming at him. Think runs it. */
	void UpdateAimedAt();

	/** Counts the squad alert down and sends it. Think runs it. */
	void UpdateSquadAlert(float DeltaSeconds);

	/** A clear line from this pawn's eyes to Point, ignoring IgnoreActor. */
	bool HasLineTo(const FVector& Point, const AActor* IgnoreActor) const;

	/** Leaving one mode for another: drop what the old one was holding. */
	void EnterMode(EThugMode NewMode);

	/** The player's pawn, for the archer's sight when no target is set yet. */
	APawn* FindPlayerPawn() const;

	/** Where on Target he aims and looks: ChestHeight above her capsule centre, the centre itself crouched. */
	FVector GetAimPointOn(const AActor* Target) const;

	/** The navmesh is built at BeginPlay; a failed move in this many seconds after start is not reported. */
	static constexpr double NavigationGraceSeconds = 10.0;

	/** World seconds. */
	double GetNowSeconds() const;

	/** The Fists and Bat Alerted step: rush, wind up inside MeleeEngageRange, swing, back off. */
	void TickMeleeRush(float DeltaSeconds, const FVector& ToTarget);

	/** The Alerted step while the target is down: drop the burst and the draw, circle at StandOffDistance. */
	void TickStandOff(float DeltaSeconds, const FVector& ToTarget);

	/** Counts a finished swing, starts the cooldown, and steps back after enough of them. */
	void FinishSwing(const FVector& ToTarget);

	/** RushSpeed while Alerted with a melee weapon, the pawn's own walking speed otherwise. */
	void ApplyMoveSpeed();

	/** The EQS half of FindCoverPoint. False when there is no query or no manager, or nothing passed. */
	bool FindCoverPointEqs(FVector& OutPoint, const FVector* Avoid) const;

	/** The C++ half: three rings of twelve candidates, kept where the line from Threat is blocked. */
	bool FindCoverPointRing(const FVector& Threat, FVector& OutPoint, const FVector* Avoid) const;

	void SetState(EThugAlertState NewState);

	UPROPERTY(Transient)
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(Transient)
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY(Transient)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(Transient)
	FVector LastStimulusLocation = FVector::ZeroVector;

	/** Index into the thug's PatrolPoints. */
	UPROPERTY(Transient)
	int32 PatrolIndex = 0;

private:
	/** True while the player is inside the sight cone right now and nothing blinds him. */
	bool bSeesTarget = false;

	/** What perception last said about the player, before smoke is taken into account. */
	bool bPerceivesTarget = false;

	/** Where the player was last seen (or glimpsed through smoke); the far end of the smoke check. */
	FVector LastSightLocation = FVector::ZeroVector;
	bool bHasSightLocation = false;

	bool bHeld = false;
	bool bBlinded = false;
	bool bPacified = false;
	float JamRemaining = 0.f;

	float SeenSeconds = 0.f;
	float UnseenSeconds = 0.f;
	float InvestigateElapsed = 0.f;
	float PatrolWaitElapsed = 0.f;
	bool bPatrolWaiting = false;

	/** Latches after the first failed move so a broken navmesh logs once, not four times a second. */
	bool bLoggedMoveFailure = false;

	bool bThinkingEnabled = true;

	/** Melee rush state. */
	bool bWasSwinging = false;
	bool bBackingOff = false;
	int32 SwingsSinceBackOff = 0;
	/** Every swing he has started: the heavy alternates bash and bat on it. */
	int32 SwingsStarted = 0;
	float MeleeCooldownRemaining = 0.f;
	float BackOffElapsed = 0.f;

	/** Stand-off state: circling a downed target, where round her, and when to pick the next spot. */
	bool bStandingOff = false;
	float StandOffAngle = 0.f;
	float StandOffRepathRemaining = 0.f;

	/** The pawn's own MaxWalkSpeed, read at possession; the rush speed is laid over it. */
	float BaseWalkSpeed = 300.f;

	EThugMode CurrentMode = EThugMode::Patrol;

	/** Gunner state. */
	EGunnerPhase GunnerPhase = EGunnerPhase::Open;
	int32 ShotsSinceCover = 0;
	float BurstPauseRemaining = 0.f;
	bool bHasCover = false;
	FVector CoverPoint = FVector::ZeroVector;
	FVector PeekPoint = FVector::ZeroVector;
	float CoverElapsed = 0.f;
	float HideElapsed = 0.f;
	bool bAimedAt = false;
	bool bLoggedJamHold = false;
	bool bDeflectNextShot = false;
	int32 DeflectedShots = 0;
	/** A failed cover search is not repeated before this world time. */
	double NextCoverSearchSeconds = -1.0;
	bool bRetreating = false;
	FVector RetreatGoal = FVector::ZeroVector;
	float RetreatElapsed = 0.f;

	/** Ranged aim state. */
	FHawkeyeTargetTrack TargetTrack;
	FRandomStream AimStream;
	FTimerHandle TrackTimerHandle;
	int32 ForcedMisses = 0;
	float NextArcherGapSeconds = 0.f;

	/** Starts (or stops) the fast look at the target a gunner or an archer keeps for his aim. */
	void UpdateTrackTimer();

	/** Archer state. */
	double LastArrowSeconds = -100.0;
	double LastRelocateSeconds = -100.0;
	bool bZipping = false;
	FVector ZipFrom = FVector::ZeroVector;
	FVector ZipTo = FVector::ZeroVector;
	float ZipElapsed = 0.f;
	float ZipSeconds = 0.f;
	bool bLoggedNoAnchor = false;
	/** Where the held draw points: the last aim before she went out of his line. */
	FVector HeldAimPoint = FVector::ZeroVector;
	FTimerHandle ArcherHoldTimerHandle;

	/** A waiting look's pitch (a gunner over a hall looks down at it); 0 walking and once not calm. */
	float LookPitch = 0.f;

	/** Senses back on (thinking, a challenge over): what perception already sees counts now, not at its next change. */
	void ResyncPerception();

	/** Squad alert. */
	bool bSquadAlertPending = false;
	float SquadAlertRemaining = 0.f;

	FTimerHandle ThinkTimerHandle;
};
