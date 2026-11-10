#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the interior shot of the Kate screenshot pass (Saved/Screenshots/Kate/): interior_entrance.png,
 * the "[Auction house]" door on its district building. The inside shots are Hawkeye.Screenshot.Interior
 * (InteriorMapTest.cpp).
 */
void HawkeyeAddInteriorShots(FAutomationTestBase* Test);

#endif
