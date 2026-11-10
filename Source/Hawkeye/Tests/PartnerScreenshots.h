#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;
class UWorld;

/**
 * Queues the partner shots onto the Kate screenshot pass (Saved/Screenshots/Kate/): partner_follow,
 * partner_shoot, banter and switch_clint. See PartnerScreenshotTest.cpp.
 */
void HawkeyeAddPartnerShots(FAutomationTestBase* Test);

/** Stops the partner thinking and the banter talking, so the rest of the Kate pass is unchanged. */
void HawkeyeFreezePartner(UWorld* World);

#endif
