#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTokenSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * What every fight in a lap cost Kate and how hard it pressed her (claude-docs/testing.md, "Fight pressure"). Sampled
 * once a frame from the lap's own update: a fight opens when any attacker asks for a token on her and closes after
 * QuietSeconds with nobody holding, waiting or cooling on her budgets. Per fight: seconds (first ask to last), hits
 * from someone else that took health and the health they took, dodges started and parries landed (Kate's own counts,
 * not the lap bot's taps), the most melee holders at once and the most holders of both kinds, the seconds two melee
 * attackers held at once, and how many attackers took part. A reload (a new pawn or a new world) carries the counts on.
 */
struct FHawkeyeLapFightPressure
{
	struct FFight
	{
		double StartSeconds = 0.0;
		double LastActiveSeconds = 0.0;
		int32 Hits = 0;
		float HealthLost = 0.f;
		int32 Dodges = 0;
		int32 Parries = 0;
		int32 PeakMeleeHolders = 0;
		int32 PeakHolders = 0;
		float TwoMeleeSeconds = 0.f;
		TSet<FString> Attackers;
		/** Those of them seen on her melee budget. */
		TSet<FString> MeleeAttackers;

		float GetSeconds() const { return static_cast<float>(LastActiveSeconds - StartSeconds); }
		int32 GetDefences() const { return Dodges + Parries; }
		/**
		 * Three or more melee attackers: a group fight, where the pressure is asserted. Fewer share one melee token
		 * (gameplay-semantics.md, "Attack tokens"), so two at once only starts at three.
		 */
		bool IsGroup() const { return MeleeAttackers.Num() >= 3; }
	};

	/** Nobody on her budgets this long closes the fight, seconds of world time. */
	double QuietSeconds = 4.0;

	TArray<FFight> Fights;

	/** One frame of the lap: World's first player's pawn, if it is Kate. */
	void Sample(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		const UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(World);
		if (!Kate || !Tokens)
		{
			return;
		}
		const double Now = World->GetTimeSeconds();
		// Her counters, rebased when the pawn changes (a reload's new Kate starts at her own zero).
		const int32 HitsNow = Kate->GetHitsTakenCount();
		const float DamageNow = Kate->GetDamageTakenTotal();
		const int32 DodgesNow = Kate->GetDodgeCount();
		const int32 ParriesNow = Kate->GetParryCount();
		const bool bSamePawn = LastKate.Get() == Kate && LastWorld.Get() == World;
		const int32 NewHits = HitsNow - (bSamePawn ? LastHits : 0);
		const float NewDamage = DamageNow - (bSamePawn ? LastDamage : 0.f);
		const int32 NewDodges = DodgesNow - (bSamePawn ? LastDodges : 0);
		const int32 NewParries = ParriesNow - (bSamePawn ? LastParries : 0);
		const double Step = bSamePawn && LastSeconds >= 0.0 ? FMath::Max(Now - LastSeconds, 0.0) : 0.0;
		LastKate = Kate;
		LastWorld = World;
		LastHits = HitsNow;
		LastDamage = DamageNow;
		LastDodges = DodgesNow;
		LastParries = ParriesNow;
		LastSeconds = Now;

		const FHawkeyeTokenBudgetView Melee = Tokens->GetBudgetView(Kate, EHawkeyeAttackKind::Melee);
		const FHawkeyeTokenBudgetView Ranged = Tokens->GetBudgetView(Kate, EHawkeyeAttackKind::Ranged);
		// Who is in it: anyone on her budgets still standing (a body left on a list until someone asks again is out).
		TArray<const AActor*> InFight;
		TArray<const AActor*> InMelee;
		for (const FHawkeyeTokenBudgetView* View : {&Melee, &Ranged})
		{
			for (const TArray<TObjectPtr<AActor>>* List : {&View->Holders, &View->Waiters, &View->Idle})
			{
				for (const AActor* Attacker : *List)
				{
					const AThugCharacter* Thug = Cast<AThugCharacter>(Attacker);
					if (Attacker && !(Thug && Thug->IsLimp()))
					{
						InFight.AddUnique(Attacker);
						if (View == &Melee)
						{
							InMelee.AddUnique(Attacker);
						}
					}
				}
			}
		}
		const int32 Present = InFight.Num();
		// A new world (an interior, a reload) has its own clock: whatever was open there is over.
		if (LastWorldSeen.Get() != World)
		{
			bOpen = false;
		}
		LastWorldSeen = World;
		FFight* Open = bOpen ? &Fights.Last() : nullptr;
		if (Open && Present == 0 && Now - Open->LastActiveSeconds > QuietSeconds)
		{
			bOpen = false;
			Open = nullptr;
		}
		if (!Open && Present > 0)
		{
			Open = &Fights.AddDefaulted_GetRef();
			Open->StartSeconds = Now;
			bOpen = true;
		}
		if (!Open)
		{
			return;
		}
		if (Present > 0)
		{
			Open->LastActiveSeconds = Now;
		}
		Open->Hits += FMath::Max(NewHits, 0);
		Open->HealthLost += FMath::Max(NewDamage, 0.f);
		Open->Dodges += FMath::Max(NewDodges, 0);
		Open->Parries += FMath::Max(NewParries, 0);
		Open->PeakMeleeHolders = FMath::Max(Open->PeakMeleeHolders, Melee.Holders.Num());
		Open->PeakHolders = FMath::Max(Open->PeakHolders, Melee.Holders.Num() + Ranged.Holders.Num());
		Open->TwoMeleeSeconds += Melee.Holders.Num() >= 2 ? static_cast<float>(Step) : 0.f;
		for (const AActor* Attacker : InFight)
		{
			Open->Attackers.Add(GetNameSafe(Attacker));
		}
		for (const AActor* Attacker : InMelee)
		{
			Open->MeleeAttackers.Add(GetNameSafe(Attacker));
		}
	}

	/** The fights as a JSON array, for the lap's own file. */
	FString ToJson(const FString& Indent = TEXT("  ")) const
	{
		TArray<FString> Rows;
		for (const FFight& Fight : Fights)
		{
			Rows.Add(FString::Printf(TEXT("%s  {\"start\": %.1f, \"seconds\": %.1f, \"attackers\": %d, \"melee_attackers\": %d, \"group\": %s, \"hits\": %d, "
										  "\"health_lost\": %.0f, \"dodges\": %d, \"parries\": %d, \"peak_melee_holders\": %d, "
										  "\"peak_holders\": %d, \"two_melee_seconds\": %.1f}"),
				*Indent, Fight.StartSeconds, Fight.GetSeconds(), Fight.Attackers.Num(), Fight.MeleeAttackers.Num(), Fight.IsGroup() ? TEXT("true") : TEXT("false"),
				Fight.Hits, Fight.HealthLost, Fight.Dodges, Fight.Parries, Fight.PeakMeleeHolders, Fight.PeakHolders,
				Fight.TwoMeleeSeconds));
		}
		return Rows.Num() > 0 ? FString::Printf(TEXT("[\n%s\n%s]"), *FString::Join(Rows, TEXT(",\n")), *Indent) : TEXT("[]");
	}

	/** One line a fight in the test's log. */
	void Report(FAutomationTestBase* Test, const FString& Lap) const
	{
		for (int32 Index = 0; Index < Fights.Num(); ++Index)
		{
			const FFight& Fight = Fights[Index];
			Test->AddInfo(FString::Printf(TEXT("%s fight %d at %.1f s: %.1f s, %d attackers (%d melee), %d hits (%.0f health), %d dodges, "
											   "%d parries, %d melee at once (%.1f s of two), %d attacking at once."),
				*Lap, Index + 1, Fight.StartSeconds, Fight.GetSeconds(), Fight.Attackers.Num(), Fight.MeleeAttackers.Num(), Fight.Hits,
				Fight.HealthLost,
				Fight.Dodges, Fight.Parries, Fight.PeakMeleeHolders, Fight.TwoMeleeSeconds, Fight.PeakHolders));
		}
	}

	/**
	 * Every group fight cost something: at least one hit, or at least MinDefences dodges and parries (the lap bot too
	 * good to be hit is still made to defend). The pressure is measured, never faked: nothing here slows the bot.
	 */
	void CheckGroupFightsCost(FAutomationTestBase* Test, const FString& Lap, int32 MinDefences = 2) const
	{
		for (int32 Index = 0; Index < Fights.Num(); ++Index)
		{
			const FFight& Fight = Fights[Index];
			if (Fight.IsGroup())
			{
				Test->TestTrue(FString::Printf(TEXT("%s fight %d (%d melee attackers) cost her a hit or %d dodges and parries (%d hits, %d)"),
								   *Lap, Index + 1, Fight.MeleeAttackers.Num(), MinDefences, Fight.Hits, Fight.GetDefences()),
					Fight.Hits >= 1 || Fight.GetDefences() >= MinDefences);
			}
		}
	}

	/** The most melee attackers that held at once in any fight. */
	int32 GetPeakMeleeHolders() const
	{
		int32 Peak = 0;
		for (const FFight& Fight : Fights)
		{
			Peak = FMath::Max(Peak, Fight.PeakMeleeHolders);
		}
		return Peak;
	}

private:
	bool bOpen = false;
	TWeakObjectPtr<AHawkeyeCharacter> LastKate;
	TWeakObjectPtr<UWorld> LastWorld;
	TWeakObjectPtr<UWorld> LastWorldSeen;
	int32 LastHits = 0;
	float LastDamage = 0.f;
	int32 LastDodges = 0;
	int32 LastParries = 0;
	double LastSeconds = -1.0;
};

/**
 * The running lap's recorder: Start in the lap's RunTest samples every frame of whatever game world is up (on the core
 * ticker, so a level change or a reload never misses a frame of the lap's own update); Finish, where the lap writes its
 * JSON, stops it, puts one line a fight in the test's log, asserts every group fight cost something, and returns the
 * JSON with a "fights" array added.
 */
namespace HawkeyeLapPressure
{
	inline FHawkeyeLapFightPressure& Recorder()
	{
		static FHawkeyeLapFightPressure Instance;
		return Instance;
	}

	inline FTSTicker::FDelegateHandle& TickerHandle()
	{
		static FTSTicker::FDelegateHandle Handle;
		return Handle;
	}

	inline UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	inline void Stop()
	{
		if (TickerHandle().IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle());
			TickerHandle().Reset();
		}
	}

	inline void Start()
	{
		Stop();
		Recorder() = FHawkeyeLapFightPressure();
		TickerHandle() = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				Recorder().Sample(FindGameWorld());
				return true;
			}));
	}

	inline FString Finish(FAutomationTestBase* Test, const FString& Lap, const FString& Json, int32 MinDefences = 2)
	{
		Stop();
		const FHawkeyeLapFightPressure& Pressure = Recorder();
		Pressure.Report(Test, Lap);
		Pressure.CheckGroupFightsCost(Test, Lap, MinDefences);
		int32 Close = INDEX_NONE;
		if (!Json.FindLastChar(TEXT('}'), Close))
		{
			return Json;
		}
		const FString Head = Json.Left(Close).TrimEnd();
		return FString::Printf(TEXT("%s,\n  \"fights\": %s\n%s"), *Head, *Pressure.ToJson(), *Json.Mid(Close));
	}
}

#endif
