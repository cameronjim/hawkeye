#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FlashbackSequencer.generated.h"

class UFlashbackDefinition;

/**
 * The clock behind a flashback slideshow, with no dependency on UMG or a world.
 *
 * Slide i occupies HoldSeconds (fully visible) followed by CrossfadeSeconds (blending into
 * slide i+1, or into black for the last slide), so the total run time is
 * sum(HoldSeconds + CrossfadeSeconds) over every slide.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UFlashbackSequencer : public UObject
{
	GENERATED_BODY()

public:
	/** Seconds after the start during which Skip is ignored, so a held key can't skip the flashback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashback|Timing", meta = (ClampMin = "0.0"))
	float SkipLockoutSeconds = 0.5f;

	/**
	 * Loads the slide timings. A null definition or one with no slides leaves the sequencer
	 * finished, which is how Play() knows to end immediately.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	void Initialize(const UFlashbackDefinition* Definition);

	/** Advances the clock. Returns true once the whole flashback is over. */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	bool Advance(float DeltaSeconds);

	/** Ends playback immediately if the skip lockout has elapsed. Returns true when it skipped. */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	bool TrySkip();

	/** True once the lockout has passed and playback has not finished. */
	UFUNCTION(BlueprintPure, Category = "Flashback")
	bool CanSkip() const;

	/** Slide currently on screen; the last slide once finished, or INDEX_NONE with no slides. */
	UFUNCTION(BlueprintPure, Category = "Flashback")
	int32 GetCurrentSlideIndex() const { return CurrentSlideIndex; }

	/** 0 while the current slide holds, rising to 1 across its crossfade into the next slide. */
	UFUNCTION(BlueprintPure, Category = "Flashback")
	float GetBlendAlpha() const { return BlendAlpha; }

	UFUNCTION(BlueprintPure, Category = "Flashback")
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category = "Flashback")
	float GetElapsedSeconds() const { return ElapsedSeconds; }

	/** Sum of every slide's HoldSeconds + CrossfadeSeconds. */
	UFUNCTION(BlueprintPure, Category = "Flashback")
	float GetTotalDuration() const { return TotalDuration; }

	UFUNCTION(BlueprintPure, Category = "Flashback")
	int32 GetSlideCount() const { return SlideDurations.Num(); }

private:
	/** Recomputes CurrentSlideIndex and BlendAlpha from ElapsedSeconds. */
	void RefreshState();

	/** Per-slide hold length. */
	TArray<float> HoldSeconds;

	/** Per-slide crossfade length; the last one is the fade to black. */
	TArray<float> CrossfadeSeconds;

	/** HoldSeconds + CrossfadeSeconds per slide, cached so RefreshState is a simple walk. */
	TArray<float> SlideDurations;

	float TotalDuration = 0.f;
	float ElapsedSeconds = 0.f;
	float BlendAlpha = 0.f;
	int32 CurrentSlideIndex = INDEX_NONE;
	bool bFinished = true;
};
