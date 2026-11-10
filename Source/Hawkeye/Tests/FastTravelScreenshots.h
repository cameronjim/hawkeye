#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the fast-travel shots onto the Kate screenshot pass (Saved/Screenshots/Kate/):
 * safehouse2_door, fasttravel_list and compass_safehouses. See FastTravelLapTest.cpp.
 */
void HawkeyeAddFastTravelShots(FAutomationTestBase* Test);

#endif
