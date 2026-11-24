#pragma once

#include "CoreMinimal.h"

/** What her swing in progress is, to the hold that may turn it into the heavy. */
enum class EHawkeyeSwingPhase : uint8
{
	/** Nothing of hers is swinging. */
	None,
	/** Winding up: the hit window has not opened yet. */
	BeforeHit,
	/** The hit window has opened (the blow landed or missed) and the swing is recovering. */
	AfterHit
};

/** What the melee button's hold does when it reaches the heavy threshold. */
enum class EHawkeyeHoldOutcome : uint8
{
	None,
	/** The heavy starts now: nothing of hers is swinging. */
	StartHeavy,
	/** The light the press started has not reached its hit window: it is cancelled and the heavy takes its place. */
	ConvertLight,
	/** A blow of hers already hit (or is recovering): the heavy is armed as the next blow, and goes the moment she can swing. */
	ArmHeavy
};

/**
 * The melee button (gameplay-semantics.md, "Melee feel"): a light on the PRESS, a heavy on the hold. With
 * bLightOnPress (the default, from bLightAttackOnPress) the press starts the light at once, 70 to 120 ms sooner than on the
 * release; if the button is still down at HeavyHoldSeconds the hold turns into the heavy: the light converts into it when it
 * has not reached its hit window, or the heavy is armed as the next blow when it has (the usual case: a light's hit window
 * is 0.25 s in and the threshold 0.4 s). With it off, as before: the light on the release, the heavy at the threshold.
 *
 * Pure: the character feeds it presses, releases, ticks and the swing's phase; the tests feed it numbers.
 */
struct FHawkeyeMeleePress
{
	/** Seconds the button must be held to be the heavy. */
	float HeavyHoldSeconds = 0.4f;

	/** The press starts the light (true) or the release does (false). */
	bool bLightOnPress = true;

	/** Seconds an armed heavy waits after the button is let go (the light buffer's length), and so can still go. */
	float ArmedGraceSeconds = 0.3f;

	/** What a hold reaching the threshold does, for a swing in Phase, given that the press started a light of its own. */
	static EHawkeyeHoldOutcome ResolveHold(bool bLightStartedByPress, EHawkeyeSwingPhase Phase)
	{
		if (Phase == EHawkeyeSwingPhase::None)
		{
			return EHawkeyeHoldOutcome::StartHeavy;
		}
		if (Phase == EHawkeyeSwingPhase::BeforeHit && bLightStartedByPress)
		{
			return EHawkeyeHoldOutcome::ConvertLight;
		}
		return EHawkeyeHoldOutcome::ArmHeavy;
	}

	/** The button went down. bLightStarted: the press itself started a light. */
	void Press(bool bLightStarted)
	{
		bDown = true;
		bLightFired = bLightStarted;
		bHoldFired = false;
		HeldSeconds = 0.f;
	}

	/**
	 * A step with the button down or up. Down: the first step past the threshold returns the hold's outcome once. Up: counts
	 * an armed heavy's grace down and drops it when it is spent.
	 */
	EHawkeyeHoldOutcome Tick(float DeltaSeconds, EHawkeyeSwingPhase Phase)
	{
		const float Step = FMath::Max(DeltaSeconds, 0.f);
		if (!bDown)
		{
			if (bArmed)
			{
				ArmedLeft -= Step;
				bArmed = ArmedLeft > 0.f;
			}
			return EHawkeyeHoldOutcome::None;
		}
		HeldSeconds += Step;
		if (bHoldFired || HeldSeconds + 1e-5f < HeavyHoldSeconds)
		{
			return EHawkeyeHoldOutcome::None;
		}
		bHoldFired = true;
		const EHawkeyeHoldOutcome Outcome = ResolveHold(bLightFired, Phase);
		if (Outcome == EHawkeyeHoldOutcome::ArmHeavy)
		{
			Arm();
		}
		return Outcome;
	}

	/**
	 * The button went up. True when the release itself starts the light (bLightOnPress off, and the hold did not become the
	 * heavy). An armed heavy starts its grace.
	 */
	bool Release()
	{
		if (!bDown)
		{
			return false;
		}
		bDown = false;
		const bool bTap = !bLightOnPress && !bHoldFired;
		if (bArmed)
		{
			ArmedLeft = ArmedGraceSeconds;
		}
		return bTap;
	}

	/** An armed heavy is waiting for her to be able to swing. */
	bool IsArmed() const { return bArmed; }

	/** The armed heavy goes: true once, and it is spent. */
	bool TakeArmed()
	{
		const bool bWas = bArmed;
		bArmed = false;
		return bWas;
	}

	/** Drops everything (a stagger, a dodge, a downed Kate: the press is no longer hers). */
	void Cancel()
	{
		bDown = false;
		bArmed = false;
		bHoldFired = false;
		bLightFired = false;
		HeldSeconds = 0.f;
	}

	bool IsDown() const { return bDown; }
	bool HasHoldFired() const { return bHoldFired; }
	float GetHeldSeconds() const { return HeldSeconds; }

private:
	void Arm()
	{
		bArmed = true;
		ArmedLeft = ArmedGraceSeconds;
	}

	bool bDown = false;
	bool bLightFired = false;
	bool bHoldFired = false;
	bool bArmed = false;
	float HeldSeconds = 0.f;
	float ArmedLeft = 0.f;
};
