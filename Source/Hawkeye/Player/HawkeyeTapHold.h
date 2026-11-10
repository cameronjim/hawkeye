#pragma once

#include "CoreMinimal.h"

/** What a button did: nothing yet, a tap (released before the hold), or a hold. */
enum class EHawkeyeTapHold : uint8
{
	None,
	Tap,
	Hold,
};

/**
 * One button that means one thing when tapped and another when held (D-pad down: a tap is quiver
 * slot 2, a hold of HoldSeconds opens the phone). The tap only fires on release, and only if the
 * hold never did, so starting a hold never also selects the slot. Times are real seconds, so a
 * paused game still counts the thumb. No world, no input system: the controller feeds it presses,
 * releases and ticks, and the tests feed it numbers.
 */
struct FHawkeyeTapHold
{
	float HoldSeconds = 0.4f;

	void Press(double NowSeconds)
	{
		PressedSeconds = NowSeconds;
		bDown = true;
		bHoldFired = false;
	}

	/** Hold once, on the first tick at least HoldSeconds after the press; None otherwise. */
	EHawkeyeTapHold Tick(double NowSeconds)
	{
		// A microsecond of slack: 20.4 - 20.0 is a hair under 0.4 in doubles.
		if (bDown && !bHoldFired && NowSeconds - PressedSeconds + 1e-6 >= HoldSeconds)
		{
			bHoldFired = true;
			return EHawkeyeTapHold::Hold;
		}
		return EHawkeyeTapHold::None;
	}

	/**
	 * Tap when released before the hold; Hold when the hold time passed but no tick saw it yet;
	 * None when the hold already fired or the button was never pressed.
	 */
	EHawkeyeTapHold Release(double NowSeconds)
	{
		if (!bDown)
		{
			return EHawkeyeTapHold::None;
		}
		const EHawkeyeTapHold Late = Tick(NowSeconds);
		const bool bWasTap = !bHoldFired;
		bDown = false;
		if (Late == EHawkeyeTapHold::Hold)
		{
			return EHawkeyeTapHold::Hold;
		}
		return bWasTap ? EHawkeyeTapHold::Tap : EHawkeyeTapHold::None;
	}

	bool IsDown() const { return bDown; }

private:
	double PressedSeconds = 0.0;
	bool bDown = false;
	bool bHoldFired = false;
};
