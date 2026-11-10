#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the narrative pass onto the end of the Kate screenshot pass (Saved/Screenshots/Kate/):
 * phone_open, chapter_title, dialogue_subtitle, chapter_end_closeup, then the rest of chapter 1's
 * end played for real (end card, the placeholder flashback, its playable scene) with
 * scene_placeholder inside the room, and the way back to the district's return point with the
 * chapter-complete toast (chapter_complete). Last: it completes the chapter and travels twice.
 * See NarrativeScreenshotTest.cpp.
 */
void HawkeyeAddNarrativeShots(FAutomationTestBase* Test);

#endif
