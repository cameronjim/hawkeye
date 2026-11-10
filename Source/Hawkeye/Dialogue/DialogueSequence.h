#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DialogueSequence.generated.h"

/**
 * One step of a walk-and-talk sequence: a row of DT_DialogueSequences. Every row with the same
 * Sequence belongs to it, played in Order, each after GapSeconds of quiet. Row names follow the
 * sequence: seq_ch01_open_01, seq_ch01_open_02.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeDialogueSequenceRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The sequence this step belongs to (UDialogueSubsystem::PlaySequence takes this name). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Sequence;

	/** Position in the sequence, lowest first. Ties keep the table's row order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	int32 Order = 0;

	/** The DT_Dialogue row spoken at this step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Line;

	/** Seconds of quiet before this line (after the previous one ends, or after the sequence starts). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (ClampMin = "0.0"))
	float GapSeconds = 1.f;
};
