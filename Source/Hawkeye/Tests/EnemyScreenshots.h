#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the enemy shots onto the Kate screenshot pass (Saved/Screenshots/Kate/): squad_alert,
 * gunner_cover, archer_draw, archer_arrow and archer_pickup. See EnemyScreenshotTest.cpp. Leaves every
 * thug frozen, calm and back where it started.
 */
void HawkeyeAddEnemyShots(FAutomationTestBase* Test);

#endif
