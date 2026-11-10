#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class FAutomationTestBase;

/**
 * Queues the narrative pass onto the end of the Kate screenshot pass (Saved/Screenshots/Kate/), on Chapter 1
 * "Night Shift": chapter_title, dialogue_subtitle (its opening line), then its end played for real once its three
 * objectives are completed (the catch is the campaign lap's): chapter_end_card with Kate's last line, back to
 * roaming with the toast (chapter_complete), and phone_open once its seven texts have arrived. Night Shift has no
 * close-up, flashback or scene; the chapter-end interactable and the scene system are tested headless
 * (Hawkeye.MissionFlow.*, Hawkeye.Narrative.*). Last: it completes the chapter.
 * See NarrativeScreenshotTest.cpp.
 */
void HawkeyeAddNarrativeShots(FAutomationTestBase* Test);

#endif
