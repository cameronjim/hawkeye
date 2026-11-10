#pragma once

#include "CoreMinimal.h"

/**
 * Real frame times after the first playable frame: the worst one, when it came, and how many ran
 * over a threshold, with the frames the engine spent writing a screenshot counted apart (a capture
 * is the test's cost, not the game's). No world, no engine: AHawkeyeGameMode feeds it wall-clock
 * deltas and the tests feed it numbers.
 */
struct HAWKEYE_API FHawkeyeFrameWatch
{
	/** Frames at or over this count as hitches, s. */
	double ThresholdSeconds = 0.1;

	int32 Frames = 0;
	double WorstSeconds = 0.0;
	double WorstAtSeconds = 0.0;
	int32 WorstFrame = INDEX_NONE;
	bool bWorstWasScreenshot = false;
	int32 HitchFrames = 0;
	int32 ScreenshotHitchFrames = 0;

	/** Worst frame that was not a screenshot capture. */
	double WorstGameSeconds = 0.0;
	double WorstGameAtSeconds = 0.0;

	/** One frame of FrameSeconds ending SinceStartSeconds after the watch began. True when it is a hitch. */
	bool AddFrame(double FrameSeconds, double SinceStartSeconds, bool bScreenshotFrame);

	/** Hitches that were not screenshot captures. */
	int32 GetGameHitchFrames() const { return HitchFrames - ScreenshotHitchFrames; }
};

/**
 * A window of frames for the -HawkeyePerfLog line: frame time (average, 95th percentile, worst) and
 * the stat unit split (game thread, render thread, GPU), screenshot captures left out. Also plain
 * numbers only; the game mode feeds it and logs Describe().
 */
struct HAWKEYE_API FHawkeyePerfWindow
{
	/** Frame times, ms, in arrival order (captures excluded). */
	TArray<float> FrameMs;
	double GameMsSum = 0.0;
	double RenderMsSum = 0.0;
	double GpuMsSum = 0.0;
	int32 CaptureFrames = 0;
	/** Frames at or over OverMs (captures excluded). */
	int32 FramesOver = 0;
	float OverMs = 50.f;

	void AddFrame(float FrameMilliseconds, float GameMs, float RenderMs, float GpuMs, bool bScreenshotFrame);

	int32 Num() const { return FrameMs.Num(); }
	float AverageMs() const;
	/** The Percent'th percentile frame (nearest rank), ms; 0 with no frames. */
	float PercentileMs(float Percent) const;
	float WorstMs() const;
	float AverageGameMs() const { return FrameMs.Num() ? float(GameMsSum / FrameMs.Num()) : 0.f; }
	float AverageRenderMs() const { return FrameMs.Num() ? float(RenderMsSum / FrameMs.Num()) : 0.f; }
	float AverageGpuMs() const { return FrameMs.Num() ? float(GpuMsSum / FrameMs.Num()) : 0.f; }

	/** "120 frames, avg 16.7 ms, p95 18.0, worst 22.1, game 6.1, draw 5.0, gpu 12.3, 0 over 50 ms, 0 captures". */
	FString Describe() const;
};
