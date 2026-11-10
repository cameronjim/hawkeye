#include "Settings/HawkeyeNarrativeSettings.h"

#include "Engine/DataTable.h"

UHawkeyeNarrativeSettings::UHawkeyeNarrativeSettings()
{
	MessageTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Messages.DT_Messages")));
	DialogueTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Dialogue.DT_Dialogue")));
	DialogueSequenceTable =
		TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_DialogueSequences.DT_DialogueSequences")));
}
