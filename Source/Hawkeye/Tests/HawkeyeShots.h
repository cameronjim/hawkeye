#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Screenshot capture for the -game screenshot passes, one at a time and checked on disk. A thin
 * wrapper over HawkeyeCapture (World/HawkeyeCapture.h), the queue the game's playtest kit also uses,
 * so a note taken during a test run can never steal a test's capture slot.
 *
 * FScreenshotRequest holds a single pending request: a second RequestScreenshot before the viewport
 * has drawn the first replaces its file name, and a viewport that does not draw (a minimised or
 * zero-sized window) keeps the request forever. Either way the PNG never appears and the test used
 * to pass. Request() queues instead: the next capture is only issued once the previous file is
 * written. Each capture is taken through UGameViewportClient::OnScreenshotCaptured and its PNG is
 * encoded and written on a worker thread, so the frame pays for the read-back only. Every test that
 * requests shots ends with FHawkeyeWaitForShots, which waits for its queue to drain and fails the
 * test for every file that is missing, empty, or older than its request.
 */
namespace HawkeyeShots
{
	/** Queues a capture of FullPath (.png, parent folders made) for Test and logs "Requested <path>". */
	HAWKEYE_API void Request(FAutomationTestBase* Test, const FString& FullPath, bool bShowUI = true);

	/** Captures Test has queued that have not been written or failed yet. */
	HAWKEYE_API int32 GetOutstanding(const FAutomationTestBase* Test);

	/** Captures that failed for Test so far (each one is also an error on the test). */
	HAWKEYE_API int32 GetFailures(const FAutomationTestBase* Test);

	/** Drops Test's finished captures from the queue; FHawkeyeWaitForShots calls it once it has reported. */
	HAWKEYE_API void ForgetFinished(const FAutomationTestBase* Test);

	/**
	 * The file check: a capture is good when its file exists, is not empty, and was written no
	 * earlier than SlackSeconds before the request (file times are coarse). Pure.
	 */
	HAWKEYE_API bool IsFreshCapture(bool bExists, int64 FileSize, const FDateTime& FileTimeUtc,
		const FDateTime& RequestedUtc, double SlackSeconds = 2.0);

	/**
	 * True on the frame after one whose draw took a capture: that frame's delta carries the read-back.
	 * Frame meters skip it.
	 */
	HAWKEYE_API bool DidCaptureLastFrame();

	/** How long one capture may take from being issued to the viewport drawing it, s. */
	constexpr double CaptureTimeoutSeconds = 10.0;
}

/**
 * Waits until every capture Test queued is written or has failed, then reports: an error per
 * failed capture (added as it failed), an error if any are still outstanding at TimeoutSeconds,
 * and one summary line.
 */
class HAWKEYE_API FHawkeyeWaitForShots : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeWaitForShots(FAutomationTestBase* InTest, float InTimeoutSeconds = 60.f)
		: Test(InTest), TimeoutSeconds(InTimeoutSeconds) {}

	virtual bool Update() override;

private:
	FAutomationTestBase* Test = nullptr;
	float TimeoutSeconds = 60.f;
	double StartSeconds = -1.0;
};

#endif
