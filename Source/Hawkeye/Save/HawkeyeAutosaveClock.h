#pragma once

#include "CoreMinimal.h"
#include "HawkeyeAutosaveClock.generated.h"

/**
 * The roaming autosave clock: one save for every IntervalSeconds of roaming. Time only counts
 * while the player could be saved safely (alive, not downed, on the ground, no fight on), so a
 * fight pauses the clock instead of resetting it and a save that falls due mid-fight waits for
 * the first calm second after it. Pure: the caller passes the time and the state.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeAutosaveClock
{
	GENERATED_BODY()

	/** Seconds of calm roaming between autosaves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Save", meta = (ClampMin = "1.0"))
	float IntervalSeconds = 60.f;

	/**
	 * Adds DeltaSeconds of play. Returns true when a save is due now, and starts the next interval.
	 * bCanSave false (a fight, down, mid-air) adds nothing and never returns true.
	 */
	bool Advance(float DeltaSeconds, bool bCanSave)
	{
		if (!bCanSave || DeltaSeconds <= 0.f)
		{
			return false;
		}
		RoamingSeconds += DeltaSeconds;
		if (RoamingSeconds < IntervalSeconds)
		{
			return false;
		}
		RoamingSeconds = 0.f;
		return true;
	}

	/** Any other save (an objective, a safehouse) starts the interval again. */
	void Reset() { RoamingSeconds = 0.f; }

	float GetRoamingSeconds() const { return RoamingSeconds; }

private:
	UPROPERTY(Transient)
	float RoamingSeconds = 0.f;
};
