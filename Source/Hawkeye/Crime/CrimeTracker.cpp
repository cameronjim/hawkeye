#include "Crime/CrimeTracker.h"

#include "Hawkeye.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeRules.h"

bool UCrimeTracker::StartCrime(UCrimeDefinition* Definition, const FVector& InSpotLocation, int32 InThugsTotal)
{
	if (!Definition)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: StartCrime without a definition."), *GetName());
		return false;
	}
	if (Active)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s refused, %s is still on."), *GetName(), *Definition->Id.ToString(),
			*Active->Id.ToString());
		return false;
	}
	Active = Definition;
	SpotLocation = InSpotLocation;
	ElapsedSeconds = 0.f;
	SecondsOnVictim = 0.f;
	VictimHits = 0;
	ThugsDown = 0;
	ThugsTotal = FMath::Max(InThugsTotal, 0);
	return true;
}

bool UCrimeTracker::Advance(float DeltaSeconds, const FCrimeTickState& State)
{
	if (!Active)
	{
		return false;
	}
	const float Step = FMath::Max(DeltaSeconds, 0.f);
	ElapsedSeconds += Step;
	ThugsDown = FMath::Clamp(State.ThugsDown, 0, ThugsTotal);

	if ((ThugsTotal > 0 && ThugsDown >= ThugsTotal) || State.bLootRecovered)
	{
		EndCrime(ECrimeEndReason::Completed);
		return true;
	}
	if (Active->Type == ECrimeType::Mugging && State.ThugsOnVictim > 0)
	{
		SecondsOnVictim += Step;
		VictimHits = UCrimeRules::VictimHitsAfter(SecondsOnVictim, Active->TimeToFailSeconds, Active->VictimHitsToFail);
		if (VictimHits >= Active->VictimHitsToFail)
		{
			EndCrime(ECrimeEndReason::VictimLost);
			return true;
		}
	}
	if (State.bLootEscaped)
	{
		EndCrime(ECrimeEndReason::LootEscaped);
		return true;
	}
	if (State.bPlayerOut || State.PlayerDistance > Active->AbandonDistance)
	{
		EndCrime(ECrimeEndReason::Abandoned);
		return true;
	}
	return false;
}

void UCrimeTracker::AbortCrime()
{
	if (Active)
	{
		EndCrime(ECrimeEndReason::Abandoned);
	}
}

void UCrimeTracker::EndCrime(ECrimeEndReason Reason)
{
	UCrimeDefinition* Definition = Active;
	FCrimeResult Result;
	Result.CrimeId = Definition->Id;
	Result.Type = Definition->Type;
	Result.Name = Definition->GetDisplayName();
	Result.Reason = Reason;
	Result.Seconds = ElapsedSeconds;
	Result.ThugsDown = ThugsDown;
	Result.ThugsTotal = ThugsTotal;
	Result.VictimHits = VictimHits;
	if (Reason == ECrimeEndReason::Completed)
	{
		++Completions.FindOrAdd(Definition->Type);
	}
	Result.Completions = GetCompletions(Definition->Type);

	Active = nullptr;
	LastResult = Result;
	bHasLastResult = true;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s ended %s after %.1f s: %d of %d thugs down, victim hit %d time(s), %d stopped so far."),
		*GetName(), *Result.CrimeId.ToString(), *UEnum::GetValueAsString(Reason), Result.Seconds, Result.ThugsDown, Result.ThugsTotal,
		Result.VictimHits, Result.Completions);
	OnCrimeEnded.Broadcast(Result);
}

int32 UCrimeTracker::GetCompletions(ECrimeType Type) const
{
	const int32* Count = Completions.Find(Type);
	return Count ? *Count : 0;
}

void UCrimeTracker::ExportCompletions(TArray<FName>& OutTypes, TArray<int32>& OutCounts) const
{
	OutTypes.Reset();
	OutCounts.Reset();
	for (const ECrimeType Type : { ECrimeType::Mugging, ECrimeType::Robbery, ECrimeType::Ambush, ECrimeType::Rooftop })
	{
		const int32 Count = GetCompletions(Type);
		if (Count > 0)
		{
			OutTypes.Add(UCrimeRules::TypeKey(Type));
			OutCounts.Add(Count);
		}
	}
}

void UCrimeTracker::ImportCompletions(const TArray<FName>& Types, const TArray<int32>& Counts)
{
	Completions.Reset();
	if (Types.Num() != Counts.Num())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: the save has %d crime types and %d counts; the extra rows are dropped."), *GetName(),
			Types.Num(), Counts.Num());
	}
	const int32 Rows = FMath::Min(Types.Num(), Counts.Num());
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		ECrimeType Type;
		if (!UCrimeRules::TypeFromKey(Types[Row], Type))
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: unknown crime type %s in the save; dropped."), *GetName(), *Types[Row].ToString());
			continue;
		}
		if (Counts[Row] > 0)
		{
			Completions.Add(Type, Counts[Row]);
		}
	}
}

void UCrimeTracker::Reset()
{
	Active = nullptr;
	ElapsedSeconds = 0.f;
	SecondsOnVictim = 0.f;
	VictimHits = 0;
	ThugsDown = 0;
	ThugsTotal = 0;
	Completions.Reset();
	LastResult = FCrimeResult();
	bHasLastResult = false;
}
