#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the first-time hint shots onto the Kate screenshot pass (Saved/Screenshots/Kate/), each hint brought up
 * by its own trigger with hawkeye.Hints 2 and every other hint counted as learned:
 *   hint_sprint.png   3.5 s of walking along the open street from the PlayerStart: "Hold [Shift] to sprint"
 *   hint_grapple.png  on the street looking at the nearest anchor a press zips to: the green diamond and "[Q] to zip"
 *   hint_parry.png    a bat thug 150 cm in front of her 0.3 s into his swing: the telegraph "!" and the parry hint
 * Each checks that the line is up, on the lower half of the screen, above the hotbar and clear of the crime panel.
 * The campaign's hint counts are put back afterwards. See HintScreenshotTest.cpp.
 */
void HawkeyeAddHintShots(FAutomationTestBase* Test);

#endif
