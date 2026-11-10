#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the fair-fight shots onto a screenshot pass (Saved/Screenshots/Kate/): heavy_block,
 * heavy_bash, archer_hold, thug_alert_glyph, aim_view_clear and arrow_in_kate. See CombatScreenshotTest.cpp. Leaves
 * every thug frozen, calm and back where it started, and Kate as she was.
 */
void HawkeyeAddCombatShots(FAutomationTestBase* Test);

#endif
