#pragma once

#include "CoreMinimal.h"
#include "Dialogue/DialogueLine.h"
#include "Dialogue/DialogueSequence.h"
#include "UObject/Object.h"
#include "DialogueQueue.generated.h"

class UDataTable;

/**
 * The dialogue rules, with no world, no HUD and no audio. UDialogueSubsystem owns one; tests make
 * one with NewObject and hand it lines and sequences.
 *
 * One line speaks at a time. PlayLine queues a line; PlaySequence queues a sequence's lines, each
 * behind its gap. Advance runs the clock: the line speaking counts down, then the next entry's gap,
 * then it starts. While bInCombat, a sequence's next line waits (its gap does not run) until the
 * fight is over; the line already speaking finishes. A sequence is marked played when its last
 * line starts, and a played sequence does not play again unless asked to.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UDialogueQueue : public UObject
{
	GENERATED_BODY()

public:
	/** DT_Dialogue's rows. False (and no lines) for a table of another struct. */
	bool LoadLines(const UDataTable* Table);

	/** DT_DialogueSequences' rows, grouped by Sequence and sorted by Order. */
	bool LoadSequences(const UDataTable* Table);

	void SetLines(const TMap<FName, FHawkeyeDialogueLine>& InLines) { Lines = InLines; }

	/** Groups Rows by Sequence and sorts each group by Order (stable). Tests call this. */
	void SetSequenceRows(const TArray<FHawkeyeDialogueSequenceRow>& Rows);

	/**
	 * Queues one line by row name. False, with a warning, for an unknown row. A reactive line (bReactive: a quip at a
	 * parry, at a trick arrow landing) goes ahead of every story line queued (behind other reactive ones) and is dropped,
	 * never spoken, if it has waited more than ReactiveMaxLateSeconds without starting.
	 */
	bool PlayLine(FName Row, bool bReactive = false);

	/**
	 * Queues every line of Sequence behind its gap. False for an unknown or empty sequence, one
	 * already queued, or one already played (unless bEvenIfPlayed).
	 */
	bool PlaySequence(FName Sequence, bool bEvenIfPlayed = false);

	/**
	 * Moves the clock DeltaSeconds on. Returns the row that started speaking in this step, or None.
	 * bInCombat holds a sequence's next line back.
	 */
	FName Advance(float DeltaSeconds, bool bInCombat);

	/** Overrides how long the line now speaking lasts (the subsystem knows the audio's length). */
	void SetCurrentLineSeconds(float Seconds);

	/** Drops everything queued and the line speaking. Played sequences stay played. */
	void Clear();

	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool IsSpeaking() const { return !CurrentLine.IsNone(); }

	/** Speaking, or something queued. Banter keeps quiet while this is true. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool IsBusy() const { return IsSpeaking() || Pending.Num() > 0; }

	/** True when the next line is a sequence's and the last Advance held it for a fight. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool IsHeldForCombat() const { return bHeldForCombat; }

	UFUNCTION(BlueprintPure, Category = "Dialogue")
	FName GetCurrentLine() const { return CurrentLine; }

	/** The sequence the line speaking belongs to, or None. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	FName GetCurrentSequence() const { return CurrentSequence; }

	UFUNCTION(BlueprintPure, Category = "Dialogue")
	float GetCurrentLineSecondsLeft() const { return CurrentSecondsLeft; }

	UFUNCTION(BlueprintPure, Category = "Dialogue")
	int32 GetPendingCount() const { return Pending.Num(); }

	/** Seconds of gap left before the next queued line, or 0 with nothing queued. */
	float GetNextGapSecondsLeft() const { return Pending.Num() > 0 ? Pending[0].GapSeconds : 0.f; }

	bool HasPlayedSequence(FName Sequence) const { return Played.Contains(Sequence); }
	bool HasSequence(FName Sequence) const { return Sequences.Contains(Sequence); }

	const FHawkeyeDialogueLine* FindLine(FName Row) const { return Lines.Find(Row); }

	/** How long Line speaks: its DurationSeconds, or DefaultLineSeconds. */
	float GetLineSeconds(const FHawkeyeDialogueLine& Line) const;

	/** Sequences played through, for the save. */
	TArray<FName> ExportPlayed() const { return Played.Array(); }

	void ImportPlayed(const TArray<FName>& InPlayed);

	/** A line with no DurationSeconds and no audio stays up this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (ClampMin = "0.5"))
	float DefaultLineSeconds = 4.f;

	/** A reactive line that has not started this long after it was queued is stale: dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (ClampMin = "0.0"))
	float ReactiveMaxLateSeconds = 3.f;

	/** Reactive lines dropped as stale, and the last one's row (tests and the log). */
	int32 GetDroppedCount() const { return DroppedCount; }
	FName GetLastDropped() const { return LastDropped; }

	/** Rows queued, in the order they will start (tests). */
	TArray<FName> GetPendingLines() const;

private:
	struct FEntry
	{
		FName Line;
		FName Sequence;
		float GapSeconds = 0.f;
		bool bLastOfSequence = false;
		bool bReactive = false;
		/** Seconds since it was queued (a reactive line's staleness clock). */
		float WaitedSeconds = 0.f;
	};

	/** Drops every reactive line queued longer than ReactiveMaxLateSeconds; ages the rest by DeltaSeconds first. */
	void DropStale(float DeltaSeconds);

	/** Starts Pending[0]; returns its row. */
	FName StartNext();

	TMap<FName, FHawkeyeDialogueLine> Lines;
	TMap<FName, TArray<FHawkeyeDialogueSequenceRow>> Sequences;
	TArray<FEntry> Pending;
	TSet<FName> Played;

	int32 DroppedCount = 0;
	FName LastDropped;

	FName CurrentLine;
	FName CurrentSequence;
	float CurrentSecondsLeft = 0.f;
	bool bHeldForCombat = false;
};
