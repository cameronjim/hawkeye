#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Settings/HawkeyeSettings.h"
#include "HawkeyeSettingsSave.generated.h"

/**
 * The on-disk form of FHawkeyeSettings, in its own slot so that wiping a campaign save never
 * costs the player their sensitivity. Saved/SaveGames/HawkeyeSettings.sav.
 */
UCLASS()
class HAWKEYE_API UHawkeyeSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FHawkeyeSettings Settings;
};
