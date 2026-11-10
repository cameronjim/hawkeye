#include "Dialogue/DialogueQueue.h"

#include "Hawkeye.h"
#include "Engine/DataTable.h"

bool UDialogueQueue::LoadLines(const UDataTable* Table)
{
	Lines.Reset();
	if (!Table || Table->GetRowStruct() != FHawkeyeDialogueLine::StaticStruct())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s is not a table of FHawkeyeDialogueLine."), *GetNameSafe(this),
			*GetNameSafe(Table));
		return false;
	}
	Table->ForeachRow<FHawkeyeDialogueLine>(TEXT("UDialogueQueue"),
		[this](const FName& Key, const FHawkeyeDialogueLine& Row) { Lines.Add(Key, Row); });
	return true;
}

bool UDialogueQueue::LoadSequences(const UDataTable* Table)
{
	if (!Table || Table->GetRowStruct() != FHawkeyeDialogueSequenceRow::StaticStruct())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s is not a table of FHawkeyeDialogueSequenceRow."), *GetNameSafe(this),
			*GetNameSafe(Table));
		Sequences.Reset();
		return false;
	}
	TArray<FHawkeyeDialogueSequenceRow> Rows;
	Table->ForeachRow<FHawkeyeDialogueSequenceRow>(TEXT("UDialogueQueue"),
		[&Rows](const FName& /*Key*/, const FHawkeyeDialogueSequenceRow& Row) { Rows.Add(Row); });
	SetSequenceRows(Rows);
	return true;
}

void UDialogueQueue::SetSequenceRows(const TArray<FHawkeyeDialogueSequenceRow>& Rows)
{
	Sequences.Reset();
	for (const FHawkeyeDialogueSequenceRow& Row : Rows)
	{
		if (!Row.Sequence.IsNone())
		{
			Sequences.FindOrAdd(Row.Sequence).Add(Row);
		}
	}
	for (TPair<FName, TArray<FHawkeyeDialogueSequenceRow>>& Pair : Sequences)
	{
		Pair.Value.StableSort([](const FHawkeyeDialogueSequenceRow& A, const FHawkeyeDialogueSequenceRow& B)
			{ return A.Order < B.Order; });
	}
}

float UDialogueQueue::GetLineSeconds(const FHawkeyeDialogueLine& Line) const
{
	return Line.DurationSeconds > 0.f ? Line.DurationSeconds : DefaultLineSeconds;
}

bool UDialogueQueue::PlayLine(FName Row)
{
	if (!Lines.Contains(Row))
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no dialogue line '%s'."), *GetNameSafe(this), *Row.ToString());
		return false;
	}
	Pending.Add({ Row, NAME_None, 0.f, false });
	return true;
}

bool UDialogueQueue::PlaySequence(FName Sequence, bool bEvenIfPlayed)
{
	const TArray<FHawkeyeDialogueSequenceRow>* Rows = Sequences.Find(Sequence);
	if (!Rows || Rows->IsEmpty())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no dialogue sequence '%s'."), *GetNameSafe(this), *Sequence.ToString());
		return false;
	}
	const bool bQueued = CurrentSequence == Sequence
		|| Pending.ContainsByPredicate([Sequence](const FEntry& Entry) { return Entry.Sequence == Sequence; });
	if (bQueued || (!bEvenIfPlayed && Played.Contains(Sequence)))
	{
		return false;
	}
	int32 Added = 0;
	for (const FHawkeyeDialogueSequenceRow& Row : *Rows)
	{
		if (!Lines.Contains(Row.Line))
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: sequence '%s' names a missing line '%s'; skipped."), *GetNameSafe(this),
				*Sequence.ToString(), *Row.Line.ToString());
			continue;
		}
		Pending.Add({ Row.Line, Sequence, FMath::Max(Row.GapSeconds, 0.f), false });
		++Added;
	}
	if (Added == 0)
	{
		return false;
	}
	Pending.Last().bLastOfSequence = true;
	return true;
}

FName UDialogueQueue::StartNext()
{
	const FEntry Entry = Pending[0];
	Pending.RemoveAt(0);
	CurrentLine = Entry.Line;
	CurrentSequence = Entry.Sequence;
	const FHawkeyeDialogueLine* Line = Lines.Find(Entry.Line);
	CurrentSecondsLeft = Line ? GetLineSeconds(*Line) : DefaultLineSeconds;
	if (Entry.bLastOfSequence)
	{
		Played.Add(Entry.Sequence);
	}
	return Entry.Line;
}

FName UDialogueQueue::Advance(float DeltaSeconds, bool bInCombat)
{
	float Remaining = FMath::Max(DeltaSeconds, 0.f);
	bHeldForCombat = false;
	if (IsSpeaking())
	{
		const float Used = FMath::Min(Remaining, CurrentSecondsLeft);
		CurrentSecondsLeft -= Used;
		Remaining -= Used;
		// A hair of tolerance, so 0.9 s and 0.1 s of frames add up to a 1 s line.
		if (CurrentSecondsLeft > KINDA_SMALL_NUMBER)
		{
			return NAME_None;
		}
		CurrentLine = NAME_None;
		CurrentSequence = NAME_None;
		CurrentSecondsLeft = 0.f;
	}
	if (Pending.IsEmpty())
	{
		return NAME_None;
	}
	if (bInCombat && !Pending[0].Sequence.IsNone())
	{
		// Walk and talk waits out the fight: the gap is frozen where it was.
		bHeldForCombat = true;
		return NAME_None;
	}
	Pending[0].GapSeconds -= Remaining;
	return Pending[0].GapSeconds <= KINDA_SMALL_NUMBER ? StartNext() : NAME_None;
}

void UDialogueQueue::SetCurrentLineSeconds(float Seconds)
{
	if (IsSpeaking())
	{
		CurrentSecondsLeft = FMath::Max(Seconds, 0.1f);
	}
}

void UDialogueQueue::Clear()
{
	Pending.Reset();
	CurrentLine = NAME_None;
	CurrentSequence = NAME_None;
	CurrentSecondsLeft = 0.f;
	bHeldForCombat = false;
}

void UDialogueQueue::ImportPlayed(const TArray<FName>& InPlayed)
{
	Played.Reset();
	Played.Append(InPlayed);
}
