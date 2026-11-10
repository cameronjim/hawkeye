#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the street crime shots onto the Kate screenshot pass (Saved/Screenshots/Kate/): crime_marker,
 * crime_mugging, crime_results, crime_robbery, crime_ambush and crime_rooftop. See CrimeScreenshotTest.cpp.
 */
void HawkeyeAddCrimeShots(FAutomationTestBase* Test);

#endif
