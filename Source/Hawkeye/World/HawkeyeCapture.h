#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

/**
 * The one screenshot queue the game and the tests share: captures run one at a time, each checked on
 * disk once written.
 *
 * FScreenshotRequest holds a single pending request: a second RequestScreenshot before the viewport
 * has drawn the first replaces its file name, and a viewport that does not draw (a minimised or
 * zero-sized window) keeps the request forever. Request() queues instead: the next capture is only
 * issued once the previous file is written. Each capture is taken through
 * UGameViewportClient::OnScreenshotCaptured and its PNG is encoded and written on a worker thread, so
 * the frame pays for the read-back only. OnDone runs on the game thread once the file is checked
 * (exists, not empty, not older than the request) or the capture failed or timed out.
 *
 * Owner is only a tag the counts are kept by (the screenshot tests pass their test, the playtest kit
 * its subsystem). HawkeyeShots (Tests/) is the automation wrapper.
 */
namespace HawkeyeCapture
{
	/** Ok, or why not ("the viewport never drew it in 10 s (...)"). */
	using FOnDone = TFunction<void(bool bOk, const FString& Why)>;

	/** Queues a capture of FullPath (.png, parent folders made). bShowUI false leaves every widget out. */
	HAWKEYE_API void Request(const FString& FullPath, bool bShowUI, const void* Owner, FOnDone OnDone = FOnDone());

	/** Captures Owner has queued that have not been written or failed yet. */
	HAWKEYE_API int32 GetOutstanding(const void* Owner);

	/** Captures that failed for Owner so far. */
	HAWKEYE_API int32 GetFailures(const void* Owner);

	/** Drops Owner's finished captures from the queue. */
	HAWKEYE_API void ForgetFinished(const void* Owner);

	/**
	 * The file check: a capture is good when its file exists, is not empty, and was written no
	 * earlier than SlackSeconds before the request (file times are coarse). Pure.
	 */
	HAWKEYE_API bool IsFreshCapture(bool bExists, int64 FileSize, const FDateTime& FileTimeUtc,
		const FDateTime& RequestedUtc, double SlackSeconds = 2.0);

	/** True on the frame after one whose draw took a capture: that frame's delta carries the read-back. */
	HAWKEYE_API bool DidCaptureLastFrame();

	/** How long one capture may take from being issued to the viewport drawing it, s. */
	constexpr double CaptureTimeoutSeconds = 10.0;
}
