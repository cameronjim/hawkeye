#include "Combat/CombatTokenSubsystem.h"

#include "Hawkeye.h"
#include "Combat/HealthComponent.h"
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

int32 UCombatTokenSubsystem::GetGroupBudget(const FBudget& Budget, EHawkeyeAttackKind Kind) const
{
	const double Now = GetNowSeconds();
	int32 Attackers = 0;
	for (const TPair<TWeakObjectPtr<AActor>, double>& Seen : Budget.Roster)
	{
		const AActor* Attacker = Seen.Key.Get();
		const UHealthComponent* Health = Attacker ? Attacker->FindComponentByClass<UHealthComponent>() : nullptr;
		Attackers += Attacker && !(Health && Health->IsDead()) && Now - Seen.Value <= UHawkeyeCombatTokenRules::RosterSeconds ? 1 : 0;
	}
	return UHawkeyeCombatTokenRules::ScaleBudgetForGroup(
		GetBudgetSize(Kind), Attackers, UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::TokenGroupShare));
}

void UCombatTokenSubsystem::NotePressure(FTargetTokens& Tokens, EHawkeyeAttackKind Kind, int32 MeleeHeldBefore)
{
	auto CountHeld = [](const FBudget& Budget)
	{ return Budget.Entries.FilterByPredicate([](const FEntry& Each) { return Each.bHolding; }).Num(); };
	const int32 Melee = CountHeld(Tokens.Budgets[static_cast<int32>(EHawkeyeAttackKind::Melee)]);
	const int32 Ranged = CountHeld(Tokens.Budgets[static_cast<int32>(EHawkeyeAttackKind::Ranged)]);
	FHawkeyeTokenPressure& Pressure = Tokens.Pressure;
	Pressure.PeakMeleeHolders = FMath::Max(Pressure.PeakMeleeHolders, Melee);
	Pressure.PeakRangedHolders = FMath::Max(Pressure.PeakRangedHolders, Ranged);
	Pressure.PeakHolders = FMath::Max(Pressure.PeakHolders, Melee + Ranged);
	if (Kind == EHawkeyeAttackKind::Melee)
	{
		++Pressure.MeleeGrants;
		Pressure.OverlappingMeleeGrants += MeleeHeldBefore > 0 ? 1 : 0;
	}
	else
	{
		++Pressure.RangedGrants;
	}
}

float UCombatTokenSubsystem::ScheduleImpact(const AActor* Attacker, const AActor* Target, float DesiredDelaySeconds)
{
	if (!Attacker || !IsValid(Target))
	{
		return DesiredDelaySeconds;
	}
	FTargetTokens& Tokens = Targets.FindOrAdd(Target);
	const double Now = GetNowSeconds();
	Tokens.Impacts.RemoveAll([Attacker, Now](const FImpact& Impact)
		{ return !Impact.Attacker.IsValid() || Impact.Attacker.Get() == Attacker || Impact.AtSeconds < Now; });
	TArray<double> Others;
	for (const FImpact& Impact : Tokens.Impacts)
	{
		Others.Add(Impact.AtSeconds);
	}
	const float Delay = UHawkeyeCombatTokenRules::StaggerImpactDelay(DesiredDelaySeconds, Now, Others,
		UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ImpactIntervalSeconds));
	if (Delay > DesiredDelaySeconds + UE_KINDA_SMALL_NUMBER)
	{
		++Tokens.Pressure.StaggeredImpacts;
		HawkeyeTokens::LogEvent(FString::Printf(TEXT("%s: his blow on %s waits %.2f s more to land clear of another."),
			*Attacker->GetName(), *Target->GetName(), Delay - DesiredDelaySeconds));
	}
	Tokens.Impacts.Add({Attacker, Now + Delay});
	return Delay;
}

FHawkeyeTokenPressure UCombatTokenSubsystem::GetPressure(const AActor* Target) const
{
	const FTargetTokens* Tokens = IsValid(Target) ? Targets.Find(Target) : nullptr;
	return Tokens ? Tokens->Pressure : FHawkeyeTokenPressure();
}

void UCombatTokenSubsystem::ResetPressure(const AActor* Target)
{
	if (IsValid(Target))
	{
		Targets.FindOrAdd(Target).Pressure = FHawkeyeTokenPressure();
	}
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
	FTargetTokens& Tokens = Targets.FindOrAdd(Target);
	FBudget& Budget = Tokens.Budgets[static_cast<int32>(Kind)];
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
	Budget.Roster.Add(Attacker, Now);
	for (auto It = Budget.Roster.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > UHawkeyeCombatTokenRules::RosterSeconds)
		{
			It.RemoveCurrent();
		}
	}
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
		const int32 GroupBudget = GetGroupBudget(Budget, Kind);
		RankWaiters(Budget, Target, Line);
		// A second melee blow at once only from another side: the first in line who stands far enough round her
		// from every melee holder goes next (gameplay-semantics.md, "Attack tokens").
		int32 Next = Line.Num() > 0 ? Line[0] : INDEX_NONE;
		if (Kind == EHawkeyeAttackKind::Melee && Held > 0)
		{
			const FVector Centre = Target->GetActorLocation();
			TArray<float> HolderBearings;
			for (const FEntry& Each : Budget.Entries)
			{
				if (Each.bHolding && Each.Attacker.IsValid())
				{
					HolderBearings.Add((Each.Attacker->GetActorLocation() - Centre).Rotation().Yaw);
				}
			}
			const float Separation = UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::MeleeSeparationDegrees);
			Next = INDEX_NONE;
			for (const int32 Candidate : Line)
			{
				const AActor* Waiter = Budget.Entries[Candidate].Attacker.Get();
				if (Waiter && UHawkeyeCombatTokenRules::IsSeparatedFromHolders(
								  (Waiter->GetActorLocation() - Centre).Rotation().Yaw, HolderBearings, Separation))
				{
					Next = Candidate;
					break;
				}
			}
		}
		if (UHawkeyeCombatTokenRules::CanGrant(GroupBudget, Held, Now, Budget.LastReleaseSeconds, Gap) && Next == Index)
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
			NotePressure(Tokens, Kind, Kind == EHawkeyeAttackKind::Melee ? Held : 0);
			HawkeyeTokens::LogEvent(FString::Printf(
				TEXT("%s: takes %s's %s token (%d of %d out: the difficulty's %d for %d attackers) %.0f deg off her view, "
					 "after %.1f s in line; telegraph at least %.1f s; %d still waiting."),
				*Attacker->GetName(), *Target->GetName(), HawkeyeTokens::KindName(Kind), Held + 1, GroupBudget,
				GetBudgetSize(Kind), Budget.Entries.Num(), Angle, Now - Entry.WaitSinceSeconds, Entry.MinTelegraphSeconds,
				Line.Num() - 1));
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
		for (TPair<TObjectKey<AActor>, FTargetTokens>& Pair : Targets)
		{
			Pair.Value.Impacts.RemoveAll([Attacker](const FImpact& Impact) { return Impact.Attacker.Get() == Attacker; });
		}
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
	View.Budget = GetGroupBudget(Budget, Kind);
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
