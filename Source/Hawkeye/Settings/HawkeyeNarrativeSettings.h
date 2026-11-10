#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HawkeyeNarrativeSettings.generated.h"

class UDataTable;

/**
 * Where the narrative tables live: Project Settings > Game > Hawkeye Narrative, stored in
 * DefaultGame.ini when changed. The defaults are the paths Tools/Editor/create_narrative.py writes.
 * World subsystems have no Blueprint defaults, so this is how the phone and the dialogue system
 * find their data.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Hawkeye Narrative"))
class HAWKEYE_API UHawkeyeNarrativeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHawkeyeNarrativeSettings();

	/** DT_Messages: rows of FHawkeyePhoneMessage, the phone's texts. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Phone")
	TSoftObjectPtr<UDataTable> MessageTable;

	/** DT_Dialogue: rows of FHawkeyeDialogueLine, every spoken line (banter included). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TSoftObjectPtr<UDataTable> DialogueTable;

	/** DT_DialogueSequences: rows of FHawkeyeDialogueSequenceRow, the walk-and-talk sequences. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TSoftObjectPtr<UDataTable> DialogueSequenceTable;

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	//~ End UDeveloperSettings interface
};
