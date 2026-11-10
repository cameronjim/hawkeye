#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the save pass onto the end of the Kate screenshot pass (Saved/Screenshots/Kate/):
 * safehouse_door, safehouse_menu, main_menu and death_fade. The death is real and last: it fades
 * out and loads the automation slot's last save. See SaveRoundTripTest.cpp.
 */
void HawkeyeAddSaveShots(FAutomationTestBase* Test);

#endif
