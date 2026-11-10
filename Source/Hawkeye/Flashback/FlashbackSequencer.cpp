#include "Flashback/FlashbackSequencer.h"

#include "Flashback/FlashbackDefinition.h"

void UFlashbackSequencer::Initialize(const UFlashbackDefinition* Definition)
{
	HoldSeconds.Reset();
	CrossfadeSeconds.Reset();
	SlideDurations.Reset();

	TotalDuration = 0.f;
	ElapsedSeconds = 0.f;
	BlendAlpha = 0.f;
	CurrentSlideIndex = INDEX_NONE;

	if (!Definition || Definition->Slides.Num() == 0)
	{
		bFinished = true;
		return;
	}

	for (const FFlashbackSlide& Slide : Definition->Slides)
	{
		const float Hold = FMath::Max(Slide.HoldSeconds, 0.f);
		const float Crossfade = FMath::Max(Slide.CrossfadeSeconds, 0.f);

		HoldSeconds.Add(Hold);
		CrossfadeSeconds.Add(Crossfade);
		SlideDurations.Add(Hold + Crossfade);
		TotalDuration += Hold + Crossfade;
	}

	bFinished = TotalDuration <= 0.f;
	CurrentSlideIndex = 0;
	RefreshState();
}

bool UFlashbackSequencer::Advance(float DeltaSeconds)
{
	if (bFinished)
	{
		return true;
	}

	ElapsedSeconds += FMath::Max(DeltaSeconds, 0.f);
	RefreshState();

	return bFinished;
}

void UFlashbackSequencer::RefreshState()
{
	if (SlideDurations.Num() == 0)
	{
		bFinished = true;
		BlendAlpha = 1.f;
		return;
	}

	if (ElapsedSeconds >= TotalDuration)
	{
		bFinished = true;
		CurrentSlideIndex = SlideDurations.Num() - 1;
		BlendAlpha = 1.f;
		return;
	}

	float SlideStart = 0.f;
	for (int32 Index = 0; Index < SlideDurations.Num(); ++Index)
	{
		const float SlideEnd = SlideStart + SlideDurations[Index];
		if (ElapsedSeconds < SlideEnd)
		{
			CurrentSlideIndex = Index;

			const float IntoSlide = ElapsedSeconds - SlideStart;
			const float Crossfade = CrossfadeSeconds[Index];
			BlendAlpha = (Crossfade > 0.f && IntoSlide > HoldSeconds[Index])
				? FMath::Clamp((IntoSlide - HoldSeconds[Index]) / Crossfade, 0.f, 1.f)
				: 0.f;
			return;
		}

		SlideStart = SlideEnd;
	}

	// Floating point left us past the last boundary; treat it as finished.
	bFinished = true;
	CurrentSlideIndex = SlideDurations.Num() - 1;
	BlendAlpha = 1.f;
}

bool UFlashbackSequencer::CanSkip() const
{
	return !bFinished && ElapsedSeconds >= SkipLockoutSeconds;
}

bool UFlashbackSequencer::TrySkip()
{
	if (!CanSkip())
	{
		return false;
	}

	ElapsedSeconds = TotalDuration;
	RefreshState();
	return true;
}
