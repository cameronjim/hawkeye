#include "UI/ChapterTitleClock.h"

void FHawkeyeTitleCardClock::Start()
{
	Elapsed = 0.f;
	FadeOutStart = FadeInSeconds + HoldSeconds;
	FadeOutFromAlpha = 1.f;
	bRunning = true;
	bSkipped = false;
}

bool FHawkeyeTitleCardClock::Advance(float DeltaSeconds)
{
	if (!bRunning)
	{
		return true;
	}
	Elapsed += FMath::Max(DeltaSeconds, 0.f);
	if (Elapsed >= GetTotalSeconds())
	{
		bRunning = false;
		return true;
	}
	return false;
}

bool FHawkeyeTitleCardClock::TrySkip()
{
	if (!bRunning || Elapsed < SkipLockoutSeconds || IsFadingOut())
	{
		return false;
	}
	FadeOutFromAlpha = GetAlpha();
	FadeOutStart = Elapsed;
	bSkipped = true;
	return true;
}

float FHawkeyeTitleCardClock::GetAlpha() const
{
	if (!bRunning)
	{
		return 0.f;
	}
	if (Elapsed >= FadeOutStart)
	{
		const float Out = FadeOutSeconds > 0.f ? (Elapsed - FadeOutStart) / FadeOutSeconds : 1.f;
		return FadeOutFromAlpha * (1.f - FMath::Clamp(Out, 0.f, 1.f));
	}
	return FadeInSeconds > 0.f ? FMath::Clamp(Elapsed / FadeInSeconds, 0.f, 1.f) : 1.f;
}
