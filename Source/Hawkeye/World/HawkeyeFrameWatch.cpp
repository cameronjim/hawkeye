#include "World/HawkeyeFrameWatch.h"

bool FHawkeyeFrameWatch::AddFrame(double FrameSeconds, double SinceStartSeconds, bool bScreenshotFrame)
{
	const double Seconds = FMath::Max(FrameSeconds, 0.0);
	if (Seconds > WorstSeconds || Frames == 0)
	{
		WorstSeconds = Seconds;
		WorstAtSeconds = SinceStartSeconds;
		WorstFrame = Frames;
		bWorstWasScreenshot = bScreenshotFrame;
	}
	if (!bScreenshotFrame && Seconds > WorstGameSeconds)
	{
		WorstGameSeconds = Seconds;
		WorstGameAtSeconds = SinceStartSeconds;
	}
	++Frames;
	const bool bHitch = Seconds >= ThresholdSeconds;
	if (bHitch)
	{
		++HitchFrames;
		ScreenshotHitchFrames += bScreenshotFrame ? 1 : 0;
	}
	return bHitch;
}

void FHawkeyePerfWindow::AddFrame(float FrameMilliseconds, float GameMs, float RenderMs, float GpuMs, bool bScreenshotFrame)
{
	if (bScreenshotFrame)
	{
		++CaptureFrames;
		return;
	}
	FrameMs.Add(FMath::Max(FrameMilliseconds, 0.f));
	GameMsSum += GameMs;
	RenderMsSum += RenderMs;
	GpuMsSum += GpuMs;
	FramesOver += FrameMilliseconds >= OverMs ? 1 : 0;
}

float FHawkeyePerfWindow::AverageMs() const
{
	double Sum = 0.0;
	for (const float Ms : FrameMs)
	{
		Sum += Ms;
	}
	return FrameMs.Num() ? float(Sum / FrameMs.Num()) : 0.f;
}

float FHawkeyePerfWindow::PercentileMs(float Percent) const
{
	if (FrameMs.Num() == 0)
	{
		return 0.f;
	}
	TArray<float> Sorted = FrameMs;
	Sorted.Sort();
	const int32 Rank = FMath::Clamp(FMath::CeilToInt(Percent / 100.f * Sorted.Num()) - 1, 0, Sorted.Num() - 1);
	return Sorted[Rank];
}

float FHawkeyePerfWindow::WorstMs() const
{
	float Worst = 0.f;
	for (const float Ms : FrameMs)
	{
		Worst = FMath::Max(Worst, Ms);
	}
	return Worst;
}

FString FHawkeyePerfWindow::Describe() const
{
	return FString::Printf(TEXT("%d frames, avg %.1f ms, p95 %.1f, worst %.1f, game %.1f, draw %.1f, gpu %.1f, %d over %.0f ms, %d captures"),
		Num(), AverageMs(), PercentileMs(95.f), WorstMs(), AverageGameMs(), AverageRenderMs(), AverageGpuMs(),
		FramesOver, OverMs, CaptureFrames);
}
