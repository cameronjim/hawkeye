#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "World/HawkeyeCapture.h"

namespace HawkeyeShots
{
	void Request(FAutomationTestBase* Test, const FString& FullPath, bool bShowUI)
	{
		if (Test)
		{
			Test->AddInfo(FString::Printf(TEXT("Requested %s"), *FullPath));
		}
		HawkeyeCapture::Request(FullPath, bShowUI, Test, [Test](bool bOk, const FString& Why)
		{
			if (!bOk && Test)
			{
				Test->AddError(Why);
			}
		});
	}

	int32 GetOutstanding(const FAutomationTestBase* Test)
	{
		return HawkeyeCapture::GetOutstanding(Test);
	}

	int32 GetFailures(const FAutomationTestBase* Test)
	{
		return HawkeyeCapture::GetFailures(Test);
	}

	void ForgetFinished(const FAutomationTestBase* Test)
	{
		HawkeyeCapture::ForgetFinished(Test);
	}

	bool DidCaptureLastFrame()
	{
		return HawkeyeCapture::DidCaptureLastFrame();
	}

	bool IsFreshCapture(bool bExists, int64 FileSize, const FDateTime& FileTimeUtc, const FDateTime& RequestedUtc,
		double SlackSeconds)
	{
		return HawkeyeCapture::IsFreshCapture(bExists, FileSize, FileTimeUtc, RequestedUtc, SlackSeconds);
	}
}

bool FHawkeyeWaitForShots::Update()
{
	const double Now = FPlatformTime::Seconds();
	if (StartSeconds < 0.0)
	{
		StartSeconds = Now;
	}
	const int32 Outstanding = HawkeyeShots::GetOutstanding(Test);
	if (Outstanding > 0 && Now - StartSeconds < TimeoutSeconds)
	{
		return false;
	}
	const int32 Failures = HawkeyeShots::GetFailures(Test);
	if (Test)
	{
		if (Outstanding > 0)
		{
			Test->AddError(FString::Printf(TEXT("%d screenshot(s) still not written after %.0f s."), Outstanding,
				TimeoutSeconds));
		}
		Test->AddInfo(FString::Printf(TEXT("Screenshots: %d failed, %d outstanding."), Failures, Outstanding));
	}
	HawkeyeShots::ForgetFinished(Test);
	return true;
}

#endif
