#pragma once

#include "CoreMinimal.h"
#include "PartnerTypes.generated.h"

/**
 * What the AI Hawkeye is doing. The partner StateTree (ST_Partner) has one state per mode, tried
 * in priority order: Revive, GoToMark, Cover, Attack, then Follow as the fallback.
 */
UENUM(BlueprintType)
enum class EHawkeyePartnerMode : uint8
{
	/** Keep 400 to 800 cm from the lead. */
	Follow,
	/** Bow at range, bow strikes inside MeleeRange, on whoever the lead hit last. */
	Attack,
	/** Shot at: to the nearest spot out of the shooter's line, until it goes quiet. */
	Cover,
	/** The lead marked a point (T): go there and hold it. */
	GoToMark,
	/** The lead is down: run over and get her up. */
	Revive
};

/** One enemy the lead has hit, and when. The partner attacks the most recent inside the window. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeLeaderHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Partner")
	TWeakObjectPtr<AActor> Target;

	/** World seconds of the hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Partner")
	double Seconds = 0.0;
};

/**
 * The fight clock (claude-docs/gameplay-semantics.md, "partner and switching"). Any contact (a
 * hit either way between a Hawkeye and a thug) keeps a fight going; it ends FightEndSeconds after
 * the last one. The partner's revive is spent once per fight and comes back with the next fight.
 * Pure: every call takes the time, so the rules are testable without a world.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeFightClock
{
	GENERATED_BODY()

	/** Seconds after the last contact that the fight is over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Partner", meta = (ClampMin = "0.0"))
	float FightEndSeconds = 10.f;

	/** A hit either way. Starting a new fight gives the revive back. */
	void NotifyContact(double Now)
	{
		if (!IsInFight(Now))
		{
			bReviveUsed = false;
		}
		bHasContact = true;
		LastContactSeconds = Now;
	}

	bool IsInFight(double Now) const { return bHasContact && Now - LastContactSeconds < FightEndSeconds; }

	/** True unless the revive was already spent in the fight still going on. */
	bool CanRevive(double Now) const { return !bReviveUsed || !IsInFight(Now); }

	/** Spends the revive. False (nothing spent) when it is already gone this fight. */
	bool ConsumeRevive(double Now)
	{
		if (!CanRevive(Now))
		{
			return false;
		}
		// Going down is contact: the fight she went down in is the one the revive belongs to.
		NotifyContact(Now);
		bReviveUsed = true;
		return true;
	}

	bool IsReviveUsed() const { return bReviveUsed; }
	double GetLastContactSeconds() const { return LastContactSeconds; }

private:
	bool bHasContact = false;
	bool bReviveUsed = false;
	double LastContactSeconds = 0.0;
};
