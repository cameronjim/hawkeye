#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;
class UWorld;

/**
 * Queues the effect shots onto a screenshot pass (Saved/Screenshots/Kate/; the Kate pass, or on their own
 * as Hawkeye.Screenshot.Vfx): vfx_snowfall, vfx_footstep, vfx_explosion, vfx_emp, arrow_hit_spark (and
 * _later), vfx_smoke, muzzle_flash (and _later) and anchor_spark (and _later), 30 m down the street from
 * the PlayerStart so the lamps an EMP puts out and the scorch it leaves stay out of the other shots.
 * See VfxScreenshotTest.cpp. vfx_zipline.png is taken on the Kate pass's own grapple zip.
 */
void HawkeyeAddVfxShots(FAutomationTestBase* Test);

/**
 * Every live component of the Niagara system named System (NS_HitSpark) in World, as "System (active 1):
 * Emitter count, ...". OutCount gets the live particles of the emitter named Emitter (every emitter's when
 * null). "no live System" when there are none.
 */
FString HawkeyeDescribeLiveVfx(const UWorld* World, const TCHAR* System, const TCHAR* Emitter, int32& OutCount);

#endif
