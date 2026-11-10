#include "Combat/CombatTokenRules.h"

EDifficultyStat UHawkeyeCombatTokenRules::GetBudgetStat(EHawkeyeAttackKind Kind)
{
	return Kind == EHawkeyeAttackKind::Melee ? EDifficultyStat::MeleeTokens : EDifficultyStat::RangedTokens;
}

bool UHawkeyeCombatTokenRules::CanGrant(
	int32 Budget, int32 Held, double NowSeconds, double LastReleaseSeconds, float GapSeconds)
{
	// The clock is a double and the gap a float: 0.6 s after 10.0 must count as 0.6.
	return Held < Budget && NowSeconds - LastReleaseSeconds + UE_KINDA_SMALL_NUMBER >= GapSeconds;
}

float UHawkeyeCombatTokenRules::ComputeViewAngle(
	const FVector& ViewLocation, const FVector& ViewForward, const FVector& Point)
{
	const FVector To = (Point - ViewLocation).GetSafeNormal();
	const FVector Forward = ViewForward.GetSafeNormal();
	if (To.IsNearlyZero() || Forward.IsNearlyZero())
	{
		return 0.f;
	}
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(To, Forward), -1.f, 1.f)));
}

float UHawkeyeCombatTokenRules::GetMinTelegraphSeconds(float ViewAngleDegrees, float OffScreenSeconds)
{
	return IsOnScreen(ViewAngleDegrees) ? 0.f : OffScreenSeconds;
}

void UHawkeyeCombatTokenRules::RankRequests(const TArray<FHawkeyeTokenRequest>& Requests, TArray<int32>& OutOrder)
{
	double LastGrant = -1.0;
	for (const FHawkeyeTokenRequest& Request : Requests)
	{
		LastGrant = FMath::Max(LastGrant, Request.LastGrantSeconds);
	}
	struct FRankKey
	{
		bool bOverdue;
		bool bJustAttacked;
		bool bOnScreen;
		int32 DistanceSteps;
		float WaitSeconds;
	};
	TArray<FRankKey> Keys;
	Keys.Reserve(Requests.Num());
	for (const FHawkeyeTokenRequest& Request : Requests)
	{
		Keys.Add({Request.WaitSeconds >= OverdueSeconds, LastGrant >= 0.0 && Request.LastGrantSeconds == LastGrant,
			IsOnScreen(Request.ViewAngleDegrees), FMath::FloorToInt(Request.Distance / DistanceStep),
			Request.WaitSeconds});
	}
	OutOrder.Reset(Requests.Num());
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		OutOrder.Add(Index);
	}
	OutOrder.StableSort(
		[&Keys](int32 A, int32 B)
		{
			const FRankKey& L = Keys[A];
			const FRankKey& R = Keys[B];
			if (L.bOverdue != R.bOverdue)
			{
				return L.bOverdue;
			}
			if (L.bOverdue)
			{
				return L.WaitSeconds > R.WaitSeconds;
			}
			if (L.bJustAttacked != R.bJustAttacked)
			{
				return R.bJustAttacked;
			}
			if (L.bOnScreen != R.bOnScreen)
			{
				return L.bOnScreen;
			}
			if (L.DistanceSteps != R.DistanceSteps)
			{
				return L.DistanceSteps < R.DistanceSteps;
			}
			return L.WaitSeconds > R.WaitSeconds;
		});
}

void UHawkeyeCombatTokenRules::SpreadRingSlots(
	const TArray<float>& BearingsDegrees, float MinSeparationDegrees, TArray<float>& OutSlotsDegrees)
{
	const int32 Count = BearingsDegrees.Num();
	OutSlotsDegrees = BearingsDegrees;
	if (Count < 2)
	{
		return;
	}
	const double Separation = FMath::Min(static_cast<double>(MinSeparationDegrees), 360.0 / Count);

	// Round the ring in bearing order, cut at the widest gap: the spacing then never has to wrap past it.
	TArray<double> Wrapped;
	TArray<int32> Order;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Wrapped.Add(FRotator::ClampAxis(BearingsDegrees[Index]));
		Order.Add(Index);
	}
	Order.StableSort([&Wrapped](int32 A, int32 B) { return Wrapped[A] < Wrapped[B]; });
	int32 Cut = 0;
	double WidestGap = -1.0;
	for (int32 Step = 0; Step < Count; ++Step)
	{
		const int32 Next = (Step + 1) % Count;
		const double Gap = Wrapped[Order[Next]] - Wrapped[Order[Step]] + (Next == 0 ? 360.0 : 0.0);
		if (Gap > WidestGap)
		{
			WidestGap = Gap;
			Cut = Next;
		}
	}

	// Slots x_k with x_(k+1) - x_k >= Separation, nearest the bearings a_k: y_k = x_k - k * Separation must not
	// decrease, so y is the least-squares non-decreasing fit to a_k - k * Separation (pool adjacent violators).
	TArray<double> Bearing;
	TArray<double> BlockMean;
	TArray<int32> BlockSize;
	for (int32 Step = 0; Step < Count; ++Step)
	{
		double Unwrapped = Wrapped[Order[(Cut + Step) % Count]];
		if (Unwrapped < Wrapped[Order[Cut]])
		{
			Unwrapped += 360.0;
		}
		Bearing.Add(Unwrapped);
		BlockMean.Add(Unwrapped - Step * Separation);
		BlockSize.Add(1);
		while (BlockMean.Num() > 1 && BlockMean.Last(1) > BlockMean.Last())
		{
			const int32 Merged = BlockSize.Last(1) + BlockSize.Last();
			BlockMean.Last(1) = (BlockMean.Last(1) * BlockSize.Last(1) + BlockMean.Last() * BlockSize.Last()) / Merged;
			BlockSize.Last(1) = Merged;
			BlockMean.Pop();
			BlockSize.Pop();
		}
	}
	TArray<double> Slots;
	for (int32 Block = 0; Block < BlockMean.Num(); ++Block)
	{
		for (int32 Member = 0; Member < BlockSize[Block]; ++Member)
		{
			Slots.Add(BlockMean[Block] + Slots.Num() * Separation);
		}
	}

	// Spread far enough to meet itself round the back: space them evenly instead, turned to fit the bearings.
	if (Slots.Last() - Slots[0] > 360.0 - Separation)
	{
		const double Even = 360.0 / Count;
		double Offset = 0.0;
		for (int32 Step = 0; Step < Count; ++Step)
		{
			Offset += (Bearing[Step] - Step * Even) / Count;
		}
		for (int32 Step = 0; Step < Count; ++Step)
		{
			Slots[Step] = Offset + Step * Even;
		}
	}
	for (int32 Step = 0; Step < Count; ++Step)
	{
		OutSlotsDegrees[Order[(Cut + Step) % Count]] = FRotator::NormalizeAxis(static_cast<float>(Slots[Step]));
	}
}
