#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the bow hands shots onto a screenshot pass (Saved/Screenshots/Kate/): bow_hold,
 * bow_aim_half, bow_aim_full, bow_aim_full_view, clint_draw and archer_draw_close. See
 * BowIKScreenshotTest.cpp. Expects the placed thugs and the partner frozen.
 */
void HawkeyeAddBowIKShots(FAutomationTestBase* Test);

#endif
