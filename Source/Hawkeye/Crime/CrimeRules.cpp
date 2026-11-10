#include "Crime/CrimeRules.h"

float UCrimeRules::RollInterval(float MinSeconds, float MaxSeconds, float Override, FRandomStream& Stream)
{
	if (Override > 0.f)
	{
		return Override;
	}
	const float Low = FMath::Max(0.f, FMath::Min(MinSeconds, MaxSeconds));
	const float High = FMath::Max(Low, FMath::Max(MinSeconds, MaxSeconds));
	return Stream.FRandRange(Low, High);
}

bool UCrimeRules::IsRoaming(bool bCrimeActive, bool bInCombat, bool bInChallenge, bool bInChapterBeat, bool bPlayerOut)
{
	return !bCrimeActive && !bInCombat && !bInChallenge && !bInChapterBeat && !bPlayerOut;
}

bool UCrimeRules::IsSpotEligible(const FVector& Spot, const FVector& Player, bool bHasLast, const FVector& LastSpot,
	float MinDistance, float MaxDistance, float LastApart)
{
	const float Distance = FVector::Dist2D(Spot, Player);
	if (Distance + KINDA_SMALL_NUMBER < MinDistance || Distance - KINDA_SMALL_NUMBER > MaxDistance)
	{
		return false;
	}
	return !bHasLast || FVector::Dist2D(Spot, LastSpot) + KINDA_SMALL_NUMBER >= LastApart;
}

int32 UCrimeRules::PickSpot(const TArray<FCrimeSpotCandidate>& Spots, const FVector& Player, bool bHasLast, const FVector& LastSpot,
	float MinDistance, float MaxDistance, float LastApart, FRandomStream& Stream)
{
	TArray<int32> Hidden;
	TArray<int32> Seen;
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		if (IsSpotEligible(Spots[Index].Location, Player, bHasLast, LastSpot, MinDistance, MaxDistance, LastApart))
		{
			(Spots[Index].bInSight ? Seen : Hidden).Add(Index);
		}
	}
	const TArray<int32>& Pool = Hidden.Num() > 0 ? Hidden : Seen;
	return Pool.Num() > 0 ? Pool[Stream.RandRange(0, Pool.Num() - 1)] : INDEX_NONE;
}

int32 UCrimeRules::VictimHitsAfter(float SecondsOnVictim, float TimeToFailSeconds, int32 HitsToFail)
{
	if (HitsToFail <= 0 || SecondsOnVictim <= 0.f)
	{
		return 0;
	}
	const float Every = FMath::Max(TimeToFailSeconds, KINDA_SMALL_NUMBER) / HitsToFail;
	return FMath::Clamp(FMath::FloorToInt((SecondsOnVictim + KINDA_SMALL_NUMBER) / Every), 0, HitsToFail);
}

bool UCrimeRules::ShouldDespawn(float SecondsSinceEnd, bool bCompleted, float DistanceToPlayer, float CompleteDelay,
	float FailDelay, float FarDistance)
{
	if (!bCompleted)
	{
		return SecondsSinceEnd + KINDA_SMALL_NUMBER >= FailDelay;
	}
	return SecondsSinceEnd + KINDA_SMALL_NUMBER >= CompleteDelay && DistanceToPlayer >= FarDistance;
}

FText UCrimeRules::TypeText(ECrimeType Type)
{
	switch (Type)
	{
	case ECrimeType::Robbery:
		return NSLOCTEXT("Hawkeye", "CrimeTypeRobbery", "robbery");
	case ECrimeType::Ambush:
		return NSLOCTEXT("Hawkeye", "CrimeTypeAmbush", "ambush");
	case ECrimeType::Rooftop:
		return NSLOCTEXT("Hawkeye", "CrimeTypeRooftop", "rooftop");
	case ECrimeType::Mugging:
	default:
		return NSLOCTEXT("Hawkeye", "CrimeTypeMugging", "mugging");
	}
}

FText UCrimeRules::FormatHudLine(const FText& Name, float DistanceCm)
{
	const int32 Metres = FMath::Max(0, FMath::RoundToInt(DistanceCm / 100.f));
	return FText::Format(NSLOCTEXT("Hawkeye", "CrimeHudLine", "{0} {1} m"), Name,
		FText::AsNumber(Metres, &FNumberFormattingOptions::DefaultNoGrouping()));
}

FText UCrimeRules::EndReasonText(ECrimeEndReason Reason)
{
	switch (Reason)
	{
	case ECrimeEndReason::Completed:
		return NSLOCTEXT("Hawkeye", "CrimeEndCompleted", "[Crime stopped]");
	case ECrimeEndReason::VictimLost:
		return NSLOCTEXT("Hawkeye", "CrimeEndVictim", "[Victim lost]");
	case ECrimeEndReason::LootEscaped:
		return NSLOCTEXT("Hawkeye", "CrimeEndEscaped", "[They got away]");
	case ECrimeEndReason::Abandoned:
	default:
		return NSLOCTEXT("Hawkeye", "CrimeEndAbandoned", "[Crime abandoned]");
	}
}

FName UCrimeRules::TypeKey(ECrimeType Type)
{
	switch (Type)
	{
	case ECrimeType::Robbery:
		return TEXT("Robbery");
	case ECrimeType::Ambush:
		return TEXT("Ambush");
	case ECrimeType::Rooftop:
		return TEXT("Rooftop");
	case ECrimeType::Mugging:
	default:
		return TEXT("Mugging");
	}
}

bool UCrimeRules::TypeFromKey(FName Key, ECrimeType& OutType)
{
	for (const ECrimeType Type : { ECrimeType::Mugging, ECrimeType::Robbery, ECrimeType::Ambush, ECrimeType::Rooftop })
	{
		// FName compares without case, so "mugging" from the console finds Mugging.
		if (TypeKey(Type) == Key)
		{
			OutType = Type;
			return true;
		}
	}
	return false;
}

bool UCrimeRules::ParseType(const FString& Text, ECrimeType& OutType)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	return !Trimmed.IsEmpty() && TypeFromKey(FName(*Trimmed), OutType);
}

FVector2D UCrimeRules::AlleyRosterOffset(int32 Index, int32 Count, float Radius, float LateralCm)
{
	if (Count <= 0 || Index < 0)
	{
		return FVector2D::ZeroVector;
	}
	const int32 Pair = Index / 2;
	const float Sign = Index % 2 == 0 ? 1.f : -1.f;
	const float Along = Sign * FMath::Max(Radius, 0.f) * FMath::Max(1.f - 0.4f * Pair, 0.2f);
	const float Across = (Pair % 2 == 0 ? 1.f : -1.f) * FMath::Abs(LateralCm) * Sign;
	return FVector2D(Along, Across);
}
