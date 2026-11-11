#pragma once

#include "CoreMinimal.h"
#include "ThugTypes.generated.h"

/**
 * The top-level states of ST_Thug, in the priority order its root tries them: Stunned, Reposition,
 * Cover, Attack, Investigate, then Patrol as the fallback. The tree's states carry these names.
 */
UENUM(BlueprintType)
enum class EThugMode : uint8
{
	/** Held by putty, staggered, on the floor, dead or switched off: nothing. */
	Stunned,
	/** The gunner backing off to RetreatToDistance, or the archer zipping to another roof. */
	Reposition,
	/** The gunner moving to, or hiding at, a cover point out of the player's line. */
	Cover,
	/** Alerted: the swing, the burst or the draw, by weapon. */
	Attack,
	/** Suspicious: walk to the stimulus (an archer only turns to it), wait, give up. */
	Investigate,
	/** Calm: patrol points, or stand. */
	Patrol
};

/**
 * A fact ST_Thug's conditions ask of the controller (AThugAIController::Senses). Each is read when the
 * condition is tested, so a transition sees what a task changed earlier in the same tick: a token granted,
 * a swing finished, a cover point taken.
 */
UENUM(BlueprintType)
enum class EThugSense : uint8
{
	/** Held by putty, staggered, on the floor, dead, or his thinking is switched off. */
	Stunned,
	/** His alert level is Calm. */
	Calm,
	/** His alert level is Suspicious. */
	Suspicious,
	/** His alert level is Alerted. */
	Alerted,
	/** Alerted, with a target he can see: no smoke in the way. */
	Engaged,
	/** His target is a downed or dead player. */
	TargetDown,
	/** He carries the pistol. */
	Gunner,
	/** He carries the bow. */
	Archer,
	/** His target is beyond EngageRange. */
	OutOfRange,
	/** His target is inside his comfort distance: RetreatTriggerDistance, or ArcherCloseRange for an archer. */
	Crowded,
	/** An archer whose relocation is off cooldown and whose bow is free, with her crowding him or beyond ArcherAggroRange. */
	RoofWanted,
	/** She is aiming at him. */
	AimedAt,
	/** ShotsBeforeCover shots since he last took cover. */
	CoverOwed,
	/** A failed cover search is not still cooling down. */
	CanSearchCover,
	/** He holds a cover point. */
	HasCover,
	/** He has held his cover point for RelocateSeconds. */
	CoverStale,
	/** He holds an attack token on his target. */
	HoldsToken,
	/** MeleeSwingsBeforeBackOff swings since he last stepped back. */
	BackOffOwed
};

/** The names of ST_Thug's states, shared by the builder that writes them and the controller that asks about them. */
namespace HawkeyeThugStates
{
	inline const FName Root(TEXT("Thug"));
	inline const FName Stunned(TEXT("Stunned"));
	inline const FName Reposition(TEXT("Reposition"));
	inline const FName BackAway(TEXT("BackAway"));
	inline const FName ArcherZip(TEXT("ArcherZip"));
	inline const FName Cover(TEXT("Cover"));
	inline const FName Hide(TEXT("Hide"));
	inline const FName Relocate(TEXT("Relocate"));
	inline const FName Peek(TEXT("Peek"));
	inline const FName StepOut(TEXT("StepOut"));
	inline const FName PeekBurst(TEXT("PeekBurst"));
	inline const FName Attack(TEXT("Attack"));
	inline const FName HoldFire(TEXT("HoldFire"));
	inline const FName StandOff(TEXT("StandOff"));
	inline const FName Gunner(TEXT("Gunner"));
	inline const FName Approach(TEXT("Approach"));
	inline const FName TakeCover(TEXT("TakeCover"));
	inline const FName Burst(TEXT("Burst"));
	inline const FName Archer(TEXT("Archer"));
	inline const FName Draw(TEXT("Draw"));
	inline const FName Melee(TEXT("Melee"));
	inline const FName BackOff(TEXT("BackOff"));
	inline const FName Swing(TEXT("Swing"));
	inline const FName HoldRing(TEXT("HoldRing"));
	inline const FName Investigate(TEXT("Investigate"));
	inline const FName Patrol(TEXT("Patrol"));
}

/** Where a gunner is in his fight. */
UENUM(BlueprintType)
enum class EGunnerPhase : uint8
{
	/** Standing where he is and shooting: no cover yet, or none to be had. */
	Open,
	/** Going to or crouched at a cover point. */
	Covering,
	/** Stepped out of cover to a spot with a line on the player, for one burst. */
	Peeking
};

/** What an archer should do about the distance to his target. */
UENUM(BlueprintType)
enum class EArcherRangeAction : uint8
{
	/** In the band: stand and shoot. */
	Hold,
	/** Inside the band but not close: a few steps back on his roof. */
	StepBack,
	/** Beyond the band: zip to a roof nearer the target, or shoot from here if there is none. */
	Approach,
	/** She is on top of him: zip to another roof. */
	Relocate
};

/** What a waiting melee thug's fidget started this step (FHawkeyeRingFidget). */
enum class EHawkeyeRingFidgetMove : uint8
{
	None,
	/** To one side of his place on the ring. */
	Shift,
	/** In toward her and back out: a step that looks like a swing coming, with no swing. */
	Feint
};

/**
 * A melee thug waiting on the ring without a token keeps moving (gameplay-semantics.md, "Attack tokens"): every
 * MinIntervalSeconds to MaxIntervalSeconds he either shifts ShiftDegrees to one side of his place (back to the other
 * side next time, never drifting off it) or, FeintChance of the time, feints: FeintDepth cm in toward her for the first
 * half of FeintSeconds and back out for the second. Pure: the caller hands it the rolls (0..1).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeRingFidget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fidget", meta = (ClampMin = "0.1"))
	float MinIntervalSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fidget", meta = (ClampMin = "0.1"))
	float MaxIntervalSeconds = 2.2f;

	/** How far to one side of his place a shift goes, degrees round her. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fidget", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float ShiftDegrees = 25.f;

	/** How far in from the ring a feint steps, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fidget", meta = (ClampMin = "0.0"))
	float FeintDepth = 120.f;

	/** In and back out, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fidget", meta = (ClampMin = "0.1"))
	float FeintSeconds = 0.9f;

	/** The share of moves that are feints, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fidget", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FeintChance = 0.35f;

	/**
	 * Runs the clock on by DeltaSeconds. When the wait runs out a move starts: a feint when RollMove is under
	 * FeintChance, else a shift to the side RollSide picks (under 0.5 left), and the next wait is RollInterval of the
	 * way from MinIntervalSeconds to MaxIntervalSeconds. Returns the move started this step, or None.
	 */
	EHawkeyeRingFidgetMove Advance(float DeltaSeconds, float RollMove, float RollInterval, float RollSide)
	{
		const float Step = FMath::Max(DeltaSeconds, 0.f);
		if (FeintRemaining > 0.f)
		{
			const bool bWasIn = IsFeintingIn();
			FeintRemaining = FMath::Max(0.f, FeintRemaining - Step);
			bSpotChanged |= bWasIn != IsFeintingIn();
		}
		WaitRemaining -= Step;
		if (WaitRemaining > 0.f || FeintRemaining > 0.f)
		{
			return EHawkeyeRingFidgetMove::None;
		}
		WaitRemaining = FMath::Lerp(MinIntervalSeconds, FMath::Max(MinIntervalSeconds, MaxIntervalSeconds),
			FMath::Clamp(RollInterval, 0.f, 1.f));
		bSpotChanged = true;
		if (RollMove < FeintChance)
		{
			FeintRemaining = FeintSeconds;
			return EHawkeyeRingFidgetMove::Feint;
		}
		SlotOffsetDegrees = RollSide < 0.5f ? -ShiftDegrees : ShiftDegrees;
		return EHawkeyeRingFidgetMove::Shift;
	}

	/** Back to his own place, waiting a full MinIntervalSeconds before the next move (his turn came, or a new fight). */
	void Reset()
	{
		SlotOffsetDegrees = 0.f;
		FeintRemaining = 0.f;
		WaitRemaining = MinIntervalSeconds;
		bSpotChanged = true;
	}

	/** Degrees to add to his place on the ring. */
	float GetSlotOffsetDegrees() const { return SlotOffsetDegrees; }

	/** In the first half of a feint: he stands FeintDepth inside the ring. */
	bool IsFeintingIn() const { return FeintRemaining > FeintSeconds * 0.5f; }

	bool IsFeinting() const { return FeintRemaining > 0.f; }

	/** Cm to take off the ring's radius this moment. */
	float GetRadiusInset() const { return IsFeintingIn() ? FeintDepth : 0.f; }

	/** True once after the spot he walks to changed (a move began, a feint turned back): re-path now. */
	bool ConsumeSpotChanged()
	{
		const bool bChanged = bSpotChanged;
		bSpotChanged = false;
		return bChanged;
	}

private:
	float WaitRemaining = 1.0f;
	float FeintRemaining = 0.f;
	float SlotOffsetDegrees = 0.f;
	bool bSpotChanged = false;
};

/**
 * The gunner's burst (claude-docs/gameplay-semantics.md, "gunner"): a TelegraphSeconds wind-up (the
 * pistol raised, the glint on), then ShotsPerBurst shots ShotIntervalSeconds apart. Pure: Advance
 * returns how many shots came due, so the timing is testable without a world or a weapon.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeBurstClock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.0"))
	float TelegraphSeconds = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "1"))
	int32 ShotsPerBurst = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Burst", meta = (ClampMin = "0.01"))
	float ShotIntervalSeconds = 0.25f;

	/**
	 * Begins the telegraph, lasting TelegraphSeconds or MinTelegraphSeconds if longer (a burst granted from off
	 * her screen). Ignored while a burst is running.
	 */
	void Start(float MinTelegraphSeconds = 0.f)
	{
		if (bActive)
		{
			return;
		}
		bActive = true;
		Elapsed = 0.f;
		ShotsFired = 0;
		ActiveTelegraphSeconds = FMath::Max(TelegraphSeconds, MinTelegraphSeconds);
	}

	/** Drops the burst where it is (a hit, a duck into cover). */
	void Cancel()
	{
		bActive = false;
		Elapsed = 0.f;
		ShotsFired = 0;
	}

	/** Runs the clock on; the number of shots that came due in this step. The burst ends after its last. */
	int32 Advance(float DeltaSeconds)
	{
		if (!bActive)
		{
			return 0;
		}
		Elapsed += FMath::Max(DeltaSeconds, 0.f);
		int32 Due = 0;
		if (Elapsed >= ActiveTelegraphSeconds)
		{
			Due = FMath::Min(ShotsPerBurst, 1 + FMath::FloorToInt((Elapsed - ActiveTelegraphSeconds) / ShotIntervalSeconds));
		}
		const int32 New = FMath::Max(Due - ShotsFired, 0);
		ShotsFired = Due;
		if (ShotsFired >= ShotsPerBurst)
		{
			bActive = false;
			++BurstsCompleted;
		}
		return New;
	}

	bool IsActive() const { return bActive; }
	bool IsTelegraphing() const { return bActive && Elapsed < ActiveTelegraphSeconds; }
	bool IsFiring() const { return bActive && Elapsed >= ActiveTelegraphSeconds; }
	/** The running burst's telegraph, seconds (TelegraphSeconds unless Start was given a longer one). */
	float GetTelegraphSeconds() const { return bActive ? ActiveTelegraphSeconds : TelegraphSeconds; }
	int32 GetShotsFired() const { return ShotsFired; }
	int32 GetBurstsCompleted() const { return BurstsCompleted; }
	float GetElapsed() const { return Elapsed; }

private:
	bool bActive = false;
	float Elapsed = 0.f;
	float ActiveTelegraphSeconds = 0.f;
	int32 ShotsFired = 0;
	int32 BurstsCompleted = 0;
};

/** What the archer's hold does this step. */
UENUM(BlueprintType)
enum class EArcherHoldStep : uint8
{
	/** Not holding. */
	None,
	/** Still holding the draw, waiting for her. */
	Hold,
	/** She came back into his aim cone at full draw: loose now. */
	Fire,
	/** Held too long: let the string down. */
	Relax
};

/**
 * The archer's hold and loose window (claude-docs/gameplay-semantics.md, "archer"). Losing his line
 * mid-draw starts a hold of up to MaxHoldSeconds; while holding, the draw keeps building, and once she
 * has been back inside his aim cone for ReactionSeconds at full draw he looses. Past MaxHoldSeconds he
 * relaxes. After every shot comes LooseSeconds in which he cannot draw at all: the window to hit him.
 * Pure: the controller feeds it what it sees, so the timing is testable without a world.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeArcherHoldClock
{
	GENERATED_BODY()

	/** The longest he holds a draw with no line, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archer", meta = (ClampMin = "0.0"))
	float MaxHoldSeconds = 2.5f;

	/** How long she must be back in the cone before he looses, seconds. Kept under 0.2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archer", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float ReactionSeconds = 0.1f;

	/** After a shot, how long he cannot draw, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archer", meta = (ClampMin = "0.0"))
	float LooseSeconds = 0.6f;

	/** Starts a hold (he just lost his line mid-draw). Ignored while already holding. */
	void BeginHold()
	{
		if (bHolding)
		{
			return;
		}
		bHolding = true;
		HoldElapsed = 0.f;
		SeenElapsed = 0.f;
	}

	/** Ends a hold without a shot (a hit, a stun, leaving the fight). */
	void EndHold()
	{
		bHolding = false;
		HoldElapsed = 0.f;
		SeenElapsed = 0.f;
	}

	/** One step of the hold. bInCone: she is in his line inside the aim cone. bFullDraw: the draw is complete. */
	EArcherHoldStep Advance(float DeltaSeconds, bool bInCone, bool bFullDraw)
	{
		if (!bHolding)
		{
			return EArcherHoldStep::None;
		}
		const float Step = FMath::Max(DeltaSeconds, 0.f);
		HoldElapsed += Step;
		SeenElapsed = bInCone ? SeenElapsed + Step : 0.f;
		if (bInCone && bFullDraw && SeenElapsed + KINDA_SMALL_NUMBER >= ReactionSeconds)
		{
			EndHold();
			return EArcherHoldStep::Fire;
		}
		if (HoldElapsed + KINDA_SMALL_NUMBER >= MaxHoldSeconds)
		{
			EndHold();
			return EArcherHoldStep::Relax;
		}
		return EArcherHoldStep::Hold;
	}

	/** A shot just left the bow: the loose window opens. */
	void BeginLoose() { LooseRemaining = LooseSeconds; }

	void AdvanceLoose(float DeltaSeconds) { LooseRemaining = FMath::Max(0.f, LooseRemaining - FMath::Max(DeltaSeconds, 0.f)); }

	bool IsHolding() const { return bHolding; }
	bool IsLoose() const { return LooseRemaining > 0.f; }
	float GetHoldElapsed() const { return HoldElapsed; }
	float GetLooseRemaining() const { return LooseRemaining; }

	/** A new draw is allowed: not holding one and not in the loose window. */
	bool CanStartDraw() const { return !bHolding && !IsLoose(); }

private:
	bool bHolding = false;
	float HoldElapsed = 0.f;
	float SeenElapsed = 0.f;
	float LooseRemaining = 0.f;
};

/**
 * The glyph over a thug's head (claude-docs/gameplay-semantics.md, "thug readability"): a "!" for
 * GlyphSeconds when he goes Alerted, a "?" when he goes Suspicious, nothing going back to Calm. Pure.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeAlertGlyph
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readability", meta = (ClampMin = "0.0"))
	float GlyphSeconds = 0.6f;

	/** He changed state: show the glyph for it. Calm shows nothing (and clears what was up). */
	void Trigger(bool bAlerted, bool bSuspicious)
	{
		Glyph = bAlerted ? TCHAR('!') : (bSuspicious ? TCHAR('?') : TCHAR(0));
		Remaining = Glyph ? GlyphSeconds : 0.f;
	}

	void Advance(float DeltaSeconds)
	{
		Remaining = FMath::Max(0.f, Remaining - FMath::Max(DeltaSeconds, 0.f));
		if (Remaining <= 0.f)
		{
			Glyph = 0;
		}
	}

	/** '!', '?' or 0 when nothing shows. */
	TCHAR GetGlyph() const { return Remaining > 0.f ? Glyph : TCHAR(0); }
	float GetRemaining() const { return Remaining; }

	/**
	 * The thin health bar's opacity: 0 at full health or beyond MaxDistance, 1 for HoldSeconds after
	 * the last damage, then fading linearly to 0 over FadeSeconds.
	 */
	static float ComputeHealthBarAlpha(float HealthFraction, float Distance, float SecondsSinceDamage,
		float MaxDistance = 1500.f, float HoldSeconds = 3.f, float FadeSeconds = 0.5f)
	{
		if (HealthFraction >= 1.f || HealthFraction <= 0.f || Distance > MaxDistance || SecondsSinceDamage < 0.f)
		{
			return 0.f;
		}
		if (SecondsSinceDamage <= HoldSeconds)
		{
			return 1.f;
		}
		return FadeSeconds > 0.f ? FMath::Clamp(1.f - (SecondsSinceDamage - HoldSeconds) / FadeSeconds, 0.f, 1.f) : 0.f;
	}

private:
	TCHAR Glyph = 0;
	float Remaining = 0.f;
};
