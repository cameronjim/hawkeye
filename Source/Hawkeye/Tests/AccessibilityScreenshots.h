#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the difficulty and accessibility shots onto the Kate screenshot pass (Saved/Screenshots/Kate/):
 * settings_full, difficulty_prompt, subtitles_large, palette_deuteranopia and pedestal_dimmer. The
 * player's settings are put back afterwards. See AccessibilityScreenshotTest.cpp.
 */
void HawkeyeAddAccessibilityShots(FAutomationTestBase* Test);

#endif
