#include "Combat/CombatTokenSubsystem.h"

#include "Hawkeye.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarHawkeyeDebugTokens(TEXT("hawkeye.DebugTokens"), 0,
	TEXT("1 draws a red line from each attack token holder to his target and a grey one from each waiter, with his ")
		TEXT("place in line, and logs every grant and return at Log."),
	ECVF_Default);

namespace HawkeyeTokens
{
	static const TCHAR* KindName(EHawkeyeAttackKind Kind)
	{
		return Kind == EHawkeyeAttackKind::Melee ? TEXT("melee") : TEXT("ranged");
	}

	/** Grants and returns are the story of a fight: Log while hawkeye.DebugTokens is on, Verbose otherwise. */
	static void LogEvent(const FString& Line)
	{
		if (UCombatTokenSubsystem::IsDebugEnabled())
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s"), *Line);
		}
		else
		{
			UE_LOG(LogHawkeye, Verbose, TEXT("%s"), *Line);
		}
	}
}

UCombatTokenSubsystem* UCombatTokenSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UCombatTokenSubsystem>() : nullptr;
}

bool UCombatTokenSubsystem::IsDebugEnabled()
{
	return CVarHawkeyeDebugTokens.GetValueOnGameThread() != 0;
}

double UCombatTokenSubsystem::GetNowSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

int32 UCombatTokenSubsystem::GetBudgetSize(EHawkeyeAttackKind Kind) const
{
	return FMath::RoundToInt(UDifficultySubsystem::GetScalarFor(this, UHawkeyeCombatTokenRules::GetBudgetStat(Kind)));
}

FHawkeyeTokenGrant UCombatTokenSubsystem::RequestToken(
	AActor* Attacker, AActor* Target, EHawkeyeAttackKind Kind, bool bReady)
{
	if (!IsValid(Attacker) || !IsValid(Target))
	{
		return {};
	}
	for (auto It = Targets.CreateIterator(); It; ++It)
	{
		if (!It.Key().ResolveObjectPtr())
		{
			It.RemoveCurrent();
		}
	}
	FBudget& Budget = Targets.FindOrAdd(Target).Budgets[static_cast<int32>(Kind)];
	Withdraw(Attacker, &Budget);
	Prune(Budget, Target, Kind);

	const double Now = GetNowSeconds();
	int32 Index =
		Budget.Entries.IndexOfByPredicate([Attacker](const FEntry& Entry) { return Entry.Attacker.Get() == Attacker; });
	if (Index == INDEX_NONE)
	{
		Index = Budget.Entries.AddDefaulted();
		Budget.Entries[Index].Attacker = Attacker;
	}
	FEntry& Entry = Budget.Entries[Index];
	Entry.LastAskSeconds = Now;
	if (!bReady && !Entry.bHolding)
	{
		Entry.WaitSinceSeconds = -1.0;
	}
	else if (Entry.WaitSinceSeconds < 0.0)
	{
		Entry.WaitSinceSeconds = Now;
	}

	TArray<int32> Line;
	if (!Entry.bHolding && bReady)
	{
		const int32 Held = Budget.Entries.FilterByPredicate([](const FEntry& Each) { return Each.bHolding; }).Num();
		const float Gap = UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::TokenGapSeconds);
		RankWaiters(Budget, Target, Line);
		if (UHawkeyeCombatTokenRules::CanGrant(GetBudgetSize(Kind), Held, Now, Budget.LastReleaseSeconds, Gap) &&
			Line.Num() > 0 && Line[0] == Index)
		{
			FVector ViewLocation;
			FVector ViewForward;
			GetViewOf(Target, ViewLocation, ViewForward);
			const float Angle =
				UHawkeyeCombatTokenRules::ComputeViewAngle(ViewLocation, ViewForward, Attacker->GetActorLocation());
			Entry.bHolding = true;
			Entry.GrantedSeconds = Now;
			Entry.MinTelegraphSeconds = UHawkeyeCombatTokenRules::GetMinTelegraphSeconds(
				Angle, UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::OffScreenTelegraphSeconds));
			Budget.LastGrantSeconds.Add(Attacker, Now);
			HawkeyeTokens::LogEvent(FString::Printf(
				TEXT("%s: takes %s's %s token (%d of %d out) %.0f deg off her view, "
					 "after %.1f s in line; telegraph at least %.1f s; %d still waiting."),
				*Attacker->GetName(), *Target->GetName(), HawkeyeTokens::KindName(Kind), Held + 1, GetBudgetSize(Kind),
				Angle, Now - Entry.WaitSinceSeconds, Entry.MinTelegraphSeconds, Line.Num() - 1));
		}
	}
	if (IsDebugEnabled())
	{
		DrawDebug(Entry, Target, Line.IndexOfByKey(Index));
	}
	return {Entry.bHolding, Entry.bHolding ? Entry.MinTelegraphSeconds : 0.f};
}

void UCombatTokenSubsystem::ReleaseToken(const AActor* Attacker)
{
	if (Attacker)
	{
		Withdraw(Attacker, nullptr);
	}
}

void UCombatTokenSubsystem::Withdraw(const AActor* Attacker, const FBudget* Keep)
{
	for (TPair<TObjectKey<AActor>, FTargetTokens>& Pair : Targets)
	{
		for (int32 Kind = 0; Kind < 2; ++Kind)
		{
			FBudget& Budget = Pair.Value.Budgets[Kind];
			if (&Budget == Keep)
			{
				continue;
			}
			const int32 Index = Budget.Entries.IndexOfByPredicate(
				[Attacker](const FEntry& Entry) { return Entry.Attacker.Get() == Attacker; });
			if (Index == INDEX_NONE)
			{
				continue;
			}
			if (Budget.Entries[Index].bHolding)
			{
				EndHold(Budget, Budget.Entries[Index], Pair.Key.ResolveObjectPtr(),
					static_cast<EHawkeyeAttackKind>(Kind), TEXT("gives back"));
			}
			Budget.Entries.RemoveAt(Index);
		}
	}
}

void UCombatTokenSubsystem::Prune(FBudget& Budget, const AActor* Target, EHawkeyeAttackKind Kind)
{
	const double Now = GetNowSeconds();
	for (int32 Index = Budget.Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Budget.Entries[Index];
		const bool bSilent = Now - Entry.LastAskSeconds > UHawkeyeCombatTokenRules::LeaseSeconds;
		const bool bOverheld = Entry.bHolding && Now - Entry.GrantedSeconds > UHawkeyeCombatTokenRules::MaxHoldSeconds;
		if (Entry.Attacker.IsValid() && !bSilent && !bOverheld)
		{
			continue;
		}
		if (Entry.bHolding)
		{
			const TCHAR* Why = TEXT("held too long; his token lapses");
			if (!Entry.Attacker.IsValid())
			{
				Why = TEXT("is gone; his token lapses");
			}
			else if (bSilent)
			{
				Why = TEXT("stopped asking; his token lapses");
			}
			EndHold(Budget, Entry, Target, Kind, Why);
		}
		Budget.Entries.RemoveAt(Index);
	}
	for (auto It = Budget.LastGrantSeconds.CreateIterator(); It; ++It)
	{
		if (!It.Key().ResolveObjectPtr())
		{
			It.RemoveCurrent();
		}
	}
}

void UCombatTokenSubsystem::EndHold(
	FBudget& Budget, FEntry& Entry, const AActor* Target, EHawkeyeAttackKind Kind, const TCHAR* Why)
{
	const double Now = GetNowSeconds();
	Entry.bHolding = false;
	Budget.LastReleaseSeconds = Now;
	HawkeyeTokens::LogEvent(
		FString::Printf(TEXT("%s: %s %s's %s token after %.1f s."), *GetNameSafe(Entry.Attacker.Get()), Why,
			*GetNameSafe(Target), HawkeyeTokens::KindName(Kind), Now - Entry.GrantedSeconds));
}

void UCombatTokenSubsystem::GetViewOf(const AActor* Target, FVector& OutLocation, FVector& OutForward)
{
	const APawn* Pawn = Cast<APawn>(Target);
	const APlayerController* Player = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (Player && Player->PlayerCameraManager)
	{
		OutLocation = Player->PlayerCameraManager->GetCameraLocation();
		OutForward = Player->PlayerCameraManager->GetCameraRotation().Vector();
		return;
	}
	FRotator Rotation;
	Target->GetActorEyesViewPoint(OutLocation, Rotation);
	OutForward = Rotation.Vector();
}

void UCombatTokenSubsystem::RankWaiters(const FBudget& Budget, const AActor* Target, TArray<int32>& OutEntries) const
{
	OutEntries.Reset();
	if (!IsValid(Target))
	{
		return;
	}
	FVector ViewLocation;
	FVector ViewForward;
	GetViewOf(Target, ViewLocation, ViewForward);
	const double Now = GetNowSeconds();
	TArray<int32> Waiting;
	TArray<FHawkeyeTokenRequest> Requests;
	for (int32 Index = 0; Index < Budget.Entries.Num(); ++Index)
	{
		const FEntry& Entry = Budget.Entries[Index];
		const AActor* Attacker = Entry.Attacker.Get();
		if (!Attacker || Entry.bHolding || Entry.WaitSinceSeconds < 0.0)
		{
			continue;
		}
		FHawkeyeTokenRequest& Request = Requests.AddDefaulted_GetRef();
		Request.Distance = FVector::Dist2D(Attacker->GetActorLocation(), Target->GetActorLocation());
		Request.ViewAngleDegrees =
			UHawkeyeCombatTokenRules::ComputeViewAngle(ViewLocation, ViewForward, Attacker->GetActorLocation());
		Request.WaitSeconds = static_cast<float>(Now - Entry.WaitSinceSeconds);
		const double* LastGrant = Budget.LastGrantSeconds.Find(Attacker);
		Request.LastGrantSeconds = LastGrant ? *LastGrant : -1.0;
		Waiting.Add(Index);
	}
	TArray<int32> Order;
	UHawkeyeCombatTokenRules::RankRequests(Requests, Order);
	for (const int32 Place : Order)
	{
		OutEntries.Add(Waiting[Place]);
	}
}

bool UCombatTokenSubsystem::HoldsToken(const AActor* Attacker) const
{
	for (const TPair<TObjectKey<AActor>, FTargetTokens>& Pair : Targets)
	{
		for (const FBudget& Budget : Pair.Value.Budgets)
		{
			if (Budget.Entries.ContainsByPredicate(
					[Attacker](const FEntry& Entry) { return Entry.bHolding && Entry.Attacker.Get() == Attacker; }))
			{
				return true;
			}
		}
	}
	return false;
}

float UCombatTokenSubsystem::GetRingSlotYaw(
	const AActor* Attacker, const AActor* Target, float MinSeparationDegrees) const
{
	if (!IsValid(Attacker) || !IsValid(Target))
	{
		return 0.f;
	}
	const FVector Centre = Target->GetActorLocation();
	const float OwnYaw = (Attacker->GetActorLocation() - Centre).Rotation().Yaw;
	const FTargetTokens* Tokens = Targets.Find(Target);
	if (!Tokens)
	{
		return OwnYaw;
	}
	TArray<float> Bearings;
	int32 Mine = INDEX_NONE;
	for (const FEntry& Entry : Tokens->Budgets[static_cast<int32>(EHawkeyeAttackKind::Melee)].Entries)
	{
		const AActor* Waiter = Entry.Attacker.Get();
		if (!Waiter || Entry.bHolding)
		{
			continue;
		}
		Mine = Waiter == Attacker ? Bearings.Num() : Mine;
		Bearings.Add((Waiter->GetActorLocation() - Centre).Rotation().Yaw);
	}
	if (Mine == INDEX_NONE)
	{
		return OwnYaw;
	}
	TArray<float> Slots;
	UHawkeyeCombatTokenRules::SpreadRingSlots(Bearings, MinSeparationDegrees, Slots);
	return Slots[Mine];
}

FHawkeyeTokenBudgetView UCombatTokenSubsystem::GetBudgetView(const AActor* Target, EHawkeyeAttackKind Kind) const
{
	FHawkeyeTokenBudgetView View;
	View.Budget = GetBudgetSize(Kind);
	const FTargetTokens* Tokens = IsValid(Target) ? Targets.Find(Target) : nullptr;
	if (!Tokens)
	{
		return View;
	}
	const FBudget& Budget = Tokens->Budgets[static_cast<int32>(Kind)];
	TArray<int32> Line;
	RankWaiters(Budget, Target, Line);
	for (const int32 Index : Line)
	{
		View.Waiters.Add(Budget.Entries[Index].Attacker.Get());
	}
	for (const FEntry& Entry : Budget.Entries)
	{
		AActor* Attacker = Entry.Attacker.Get();
		if (!Attacker)
		{
			continue;
		}
		if (Entry.bHolding)
		{
			View.Holders.Add(Attacker);
		}
		else if (Entry.WaitSinceSeconds < 0.0)
		{
			View.Idle.Add(Attacker);
		}
	}
	return View;
}

void UCombatTokenSubsystem::DrawDebug(const FEntry& Entry, const AActor* Target, int32 PlaceInLine) const
{
	const AActor* Attacker = Entry.Attacker.Get();
	UWorld* World = GetWorld();
	if (!Attacker || !World)
	{
		return;
	}
	const FVector Head = Attacker->GetActorLocation() + FVector(0.f, 0.f, 110.f);
	const FColor Colour = Entry.bHolding ? FColor::Red : FColor(150, 150, 150);
	DrawDebugLine(World, Head, Target->GetActorLocation(), Colour, false, -1.f, 0, Entry.bHolding ? 3.f : 1.f);
	FString Label = TEXT("cooling");
	if (Entry.bHolding)
	{
		Label = TEXT("TOKEN");
	}
	else if (PlaceInLine != INDEX_NONE)
	{
		Label = FString::Printf(TEXT("waits #%d"), PlaceInLine + 1);
	}
	DrawDebugString(World, Head + FVector(0.f, 0.f, 30.f), Label, nullptr, Colour, -1.f, true);
}
