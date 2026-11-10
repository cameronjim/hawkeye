#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the side-challenge shots onto the Kate screenshot pass (Saved/Screenshots/Kate/):
 * challenge_pedestal, archery_targets, challenge_results and traversal_checkpoint. See
 * ChallengeScreenshotTest.cpp.
 */
void HawkeyeAddChallengeShots(FAutomationTestBase* Test);

/** Just challenge_pedestal's framing, written as FileName (the time of day pass's day_pedestal.png). */
void HawkeyeAddChallengePedestalShot(FAutomationTestBase* Test, const FString& FileName);

#endif
