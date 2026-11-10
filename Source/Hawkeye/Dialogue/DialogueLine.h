#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UObject/SoftObjectPtr.h"
#include "DialogueLine.generated.h"

class USoundBase;

/** When a banter line can play. Row names in DT_Dialogue carry the snake_case form: kate_after_fight_03. */
UENUM(BlueprintType)
enum class EHawkeyeBanterSituation : uint8
{
	/** Roaming the district, every 45 to 90 s. */
	IdleRoam,
	/** The fight clock ran out. */
	AfterFight,
	/** The current objective is close. */
	ObjectiveNear,
	/** The lead dropped under a third of her health. */
	LowHealth,
	/** Never picked by banter: played by name (UDialogueSubsystem::PlayLine) or from a sequence. */
	Scripted
};

/**
 * One spoken line: a row of DT_Dialogue. The HUD shows it as a subtitle with the speaker's name;
 * when Audio is set it plays too. Row names are stable: kate_after_fight_03, seq_ch01_open_01.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeDialogueLine : public FTableRowBase
{
	GENERATED_BODY()

	/** Kate or Clint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EHawkeyeBanterSituation Situation = EHawkeyeBanterSituation::IdleRoam;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (MultiLine = "true"))
	FText Text;

	/** The recorded line (S_VO_<row name>). Optional: without it the line is a subtitle only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TSoftObjectPtr<USoundBase> Audio;

	/**
	 * Seconds the subtitle stays up. 0 uses the audio's length when there is audio, else the
	 * dialogue system's default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (ClampMin = "0.0"))
	float DurationSeconds = 0.f;
};
