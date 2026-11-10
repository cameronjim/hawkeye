#pragma once

#include "CoreMinimal.h"
#include "ChapterTitleClock.generated.h"

/**
 * The chapter title card's timing, with no UMG: fade in over FadeInSeconds, hold for HoldSeconds,
 * fade out over FadeOutSeconds. A skip after SkipLockoutSeconds starts the fade out from wherever
 * the card is (so it never pops); a skip inside the lockout, or during the fade out, is ignored.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeTitleCardClock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title", meta = (ClampMin = "0.0"))
	float FadeInSeconds = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title", meta = (ClampMin = "0.0"))
	float HoldSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 0.75f;

	/** A key held over from play cannot skip the card this early. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Title", meta = (ClampMin = "0.0"))
	float SkipLockoutSeconds = 0.5f;

	/** Back to the start, running. */
	void Start();

	/** Moves the clock on. Returns true once the card is over (and on every call after). */
	bool Advance(float DeltaSeconds);

	/** Starts the fade out now. False inside the lockout, while fading out, or when not running. */
	bool TrySkip();

	/** 0 (clear) to 1 (full card) right now. */
	float GetAlpha() const;

	bool IsRunning() const { return bRunning; }
	bool IsFadingOut() const { return bRunning && Elapsed >= FadeOutStart; }
	bool WasSkipped() const { return bSkipped; }
	float GetElapsed() const { return Elapsed; }

	/** When the card is gone, from its start: FadeIn + Hold + FadeOut unless skipped. */
	float GetTotalSeconds() const { return FadeOutStart + FadeOutSeconds; }

private:
	float Elapsed = 0.f;
	float FadeOutStart = 0.f;
	float FadeOutFromAlpha = 1.f;
	bool bRunning = false;
	bool bSkipped = false;
};
