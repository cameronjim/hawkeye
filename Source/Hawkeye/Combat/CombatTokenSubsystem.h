#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "Combat/CombatTokenRules.h"
#include "CombatTokenSubsystem.generated.h"

/** Who holds and who waits on one of a target's token budgets: for hawkeye.DebugTokens and the HUD. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeTokenBudgetView
{
	GENERATED_BODY()

	/** Tokens the budget has at the difficulty in force. */
	UPROPERTY(BlueprintReadOnly, Category = "Tokens")
	int32 Budget = 0;

	/** Attackers holding a token now. */
	UPROPERTY(BlueprintReadOnly, Category = "Tokens")
	TArray<TObjectPtr<AActor>> Holders;

	/** Attackers ready and waiting, first in line first. */
	UPROPERTY(BlueprintReadOnly, Category = "Tokens")
	TArray<TObjectPtr<AActor>> Waiters;

	/** Attackers in the fight but not ready (a melee thug in his cooldown): on the ring, not in line. */
	UPROPERTY(BlueprintReadOnly, Category = "Tokens")
	TArray<TObjectPtr<AActor>> Idle;
};

/**
 * The attack tokens (gameplay-semantics.md, "Attack tokens"): for each target, a melee and a ranged budget of
 * how many of her attackers may be attacking at once, who holds them, and who waits. Attackers ask from their
 * own think (no tick here, no sweep over actors) and the rules in UHawkeyeCombatTokenRules decide. Everyone is
 * held by weak reference and a token not renewed lapses, so a destroyed or silent holder never keeps one.
 */
UCLASS()
class HAWKEYE_API UCombatTokenSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem for WorldContext's world, or null. */
	static UCombatTokenSubsystem* Get(const UObject* WorldContext);

	/**
	 * Attacker asks Target's Kind budget for a token. Called every think while he is in the fight: a holder's
	 * call renews his token; bReady false (a melee thug in his cooldown) keeps his place on the ring but is
	 * never granted. Asking about another target or kind first withdraws him from the old one.
	 */
	FHawkeyeTokenGrant RequestToken(AActor* Attacker, AActor* Target, EHawkeyeAttackKind Kind, bool bReady);

	/** His attack is over or broken, or he is out of the fight: gives back his token and leaves every queue. */
	void ReleaseToken(const AActor* Attacker);

	/** True while Attacker holds a token on any budget. */
	UFUNCTION(BlueprintPure, Category = "Tokens")
	bool HoldsToken(const AActor* Attacker) const;

	/**
	 * The yaw from Target at which Attacker waits on her melee ring: the attackers in her melee budget who hold no
	 * token, spread MinSeparationDegrees apart (UHawkeyeCombatTokenRules::SpreadRingSlots). His own bearing when
	 * he is not among them.
	 */
	float GetRingSlotYaw(const AActor* Attacker, const AActor* Target, float MinSeparationDegrees) const;

	/** Who holds and who waits on Target's Kind budget now. */
	UFUNCTION(BlueprintCallable, Category = "Tokens")
	FHawkeyeTokenBudgetView GetBudgetView(const AActor* Target, EHawkeyeAttackKind Kind) const;

	/** hawkeye.DebugTokens: lines from holders and waiters to their target, and every grant and return at Log. */
	static bool IsDebugEnabled();

private:
	/** One attacker on one budget. */
	struct FEntry
	{
		TWeakObjectPtr<AActor> Attacker;
		double LastAskSeconds = 0.0;
		/** When he became ready and began to wait; below 0 while not ready. */
		double WaitSinceSeconds = -1.0;
		bool bHolding = false;
		double GrantedSeconds = 0.0;
		float MinTelegraphSeconds = 0.f;
	};

	struct FBudget
	{
		TArray<FEntry> Entries;
		double LastReleaseSeconds = -1.0e9;
		/** When each attacker was last granted here: who just attacked goes to the back. Kept past his entry. */
		TMap<TObjectKey<AActor>, double> LastGrantSeconds;
	};

	struct FTargetTokens
	{
		FBudget Budgets[2];
	};

	double GetNowSeconds() const;

	/** Kind's budget at the difficulty in force. */
	int32 GetBudgetSize(EHawkeyeAttackKind Kind) const;

	/** Where Target looks from and along: her camera when a player has her, else her eyes. */
	static void GetViewOf(const AActor* Target, FVector& OutLocation, FVector& OutForward);

	/** Drops dead and silent entries; a holder who lapsed gives his token back. */
	void Prune(FBudget& Budget, const AActor* Target, EHawkeyeAttackKind Kind);

	/** Indices into Budget.Entries of the ready waiters, first in line first. */
	void RankWaiters(const FBudget& Budget, const AActor* Target, TArray<int32>& OutEntries) const;

	/** Takes Attacker off every budget but Keep (if any), giving back a token he held there. */
	void Withdraw(const AActor* Attacker, const FBudget* Keep);

	/** Ends Entry's hold: the budget's gap starts now. */
	void EndHold(FBudget& Budget, FEntry& Entry, const AActor* Target, EHawkeyeAttackKind Kind, const TCHAR* Why);

	void DrawDebug(const FEntry& Entry, const AActor* Target, int32 PlaceInLine) const;

	TMap<TObjectKey<AActor>, FTargetTokens> Targets;
};
