#pragma once

#include "CoreMinimal.h"

/**
 * How fair a fight felt, measured (the scripted fights in LapTest.cpp write it out). Pure: the caller
 * feeds it frames, telegraphs and damage.
 *
 * Annoyance: the share of the fight Kate spent staggered or knocked down (not in control of her own
 * character). Fairness: every hit she takes must come from an attacker who showed a telegraph (a
 * wind-up glow, a raised pistol, a drawn bow) no more than TelegraphWindowSeconds before it landed.
 * An attacker's telegraph is noted every frame it is showing, so the window runs from its last frame:
 * a gunner's third shot, 0.5 s after the glint went off, still counts as warned.
 */
struct HAWKEYE_API FHawkeyeFightMetrics
{
	/** How long before a hit its telegraph may have ended and still count, seconds. */
	float TelegraphWindowSeconds = 1.f;

	/** One frame of the fight. bKateDisabled: staggered, knocked back or down this frame. */
	void Tick(float DeltaSeconds, bool bKateDisabled)
	{
		const float Step = FMath::Max(DeltaSeconds, 0.f);
		FightSeconds += Step;
		DisabledSeconds += bKateDisabled ? Step : 0.f;
	}

	/** Attacker is showing a telegraph at Time (world seconds). */
	void NoteTelegraph(const FString& Attacker, double Time)
	{
		LastTelegraph.Add(Attacker, Time);
	}

	/** A hit on Kate from Attacker at Time. True when it was telegraphed in time. */
	bool NoteDamage(const FString& Attacker, double Time)
	{
		++Hits;
		const double* Seen = LastTelegraph.Find(Attacker);
		const bool bWarned = Seen && Time >= *Seen - KINDA_SMALL_NUMBER
			&& Time - *Seen <= TelegraphWindowSeconds + KINDA_SMALL_NUMBER;
		if (!bWarned)
		{
			++Untelegraphed;
			UntelegraphedNotes.Add(FString::Printf(TEXT("%s at %.2f s (last telegraph %s)"), *Attacker, Time,
				Seen ? *FString::Printf(TEXT("%.2f s"), *Seen) : TEXT("never")));
		}
		return bWarned;
	}

	/** Seconds Kate was staggered or down over seconds fought, 0..1. */
	float GetAnnoyanceFraction() const { return FightSeconds > 0.f ? DisabledSeconds / FightSeconds : 0.f; }

	float GetFightSeconds() const { return FightSeconds; }
	float GetDisabledSeconds() const { return DisabledSeconds; }
	int32 GetHits() const { return Hits; }
	int32 GetUntelegraphedHits() const { return Untelegraphed; }
	const TArray<FString>& GetUntelegraphedNotes() const { return UntelegraphedNotes; }

private:
	float FightSeconds = 0.f;
	float DisabledSeconds = 0.f;
	int32 Hits = 0;
	int32 Untelegraphed = 0;
	TMap<FString, double> LastTelegraph;
	TArray<FString> UntelegraphedNotes;
};
