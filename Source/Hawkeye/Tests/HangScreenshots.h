#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the hang shots onto a screenshot pass (Saved/Screenshots/Kate/; the Kate pass, or on their own as
 * Hawkeye.Screenshot.Hang): hang_shimmy.png, 0.9 s into a shimmy along a tenement parapet toward a real roof corner
 * (the stick injected through IA_Move, the hands staggered), hang_corner.png, part way round that corner,
 * roof_corner_flush.png, that corner's parapet close up from its roof, and roof_corner_outside.png, the same corner
 * from the air past it (both facades flush up through the parapet). The spring arm is measured every frame of it and
 * its largest frame-to-frame jump logged. See ParkourHangDistrictTest.cpp.
 */
void HawkeyeAddHangShots(FAutomationTestBase* Test);

#endif
