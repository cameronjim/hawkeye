#include "Flashback/FlashbackDefinition.h"
#include "Flashback/FlashbackSequencer.h"
#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeFlashbackTest
{
	static UFlashbackDefinition* MakeFlashback(int32 SlideCount, float Hold, float Crossfade)
	{
		UFlashbackDefinition* Flashback = NewObject<UFlashbackDefinition>();
		for (int32 Index = 0; Index < SlideCount; ++Index)
		{
			FFlashbackSlide Slide;
			Slide.HoldSeconds = Hold;
			Slide.CrossfadeSeconds = Crossfade;
			Flashback->Slides.Add(Slide);
		}
		return Flashback;
	}

	/** Ticks the sequencer at a fixed step until it finishes or the budget runs out. */
	static float RunToEnd(UFlashbackSequencer* Sequencer, float Step, float Budget)
	{
		float Elapsed = 0.f;
		while (!Sequencer->IsFinished() && Elapsed < Budget)
		{
			Sequencer->Advance(Step);
			Elapsed += Step;
		}
		return Elapsed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlashbackEmptyFinishesImmediately, "Hawkeye.Flashback.EmptyFinishesImmediately",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlashbackEmptyFinishesImmediately::RunTest(const FString& Parameters)
{
	UFlashbackSequencer* Sequencer = NewObject<UFlashbackSequencer>();

	Sequencer->Initialize(nullptr);
	TestTrue(TEXT("A null definition is finished right away"), Sequencer->IsFinished());
	TestEqual(TEXT("No slides"), Sequencer->GetSlideCount(), 0);
	TestEqual(TEXT("No duration"), Sequencer->GetTotalDuration(), 0.f);

	Sequencer->Initialize(NewObject<UFlashbackDefinition>());
	TestTrue(TEXT("A definition with zero slides is finished right away"), Sequencer->IsFinished());
	TestTrue(TEXT("Advancing keeps it finished"), Sequencer->Advance(1.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlashbackTotalDuration, "Hawkeye.Flashback.TotalDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlashbackTotalDuration::RunTest(const FString& Parameters)
{
	UFlashbackSequencer* Sequencer = NewObject<UFlashbackSequencer>();
	Sequencer->Initialize(HawkeyeFlashbackTest::MakeFlashback(3, /*Hold=*/4.f, /*Crossfade=*/1.f));

	// Three slides of 4 s hold + 1 s crossfade; the last crossfade is the fade to black.
	TestEqual(TEXT("Total duration is the sum of holds and fades"), Sequencer->GetTotalDuration(), 15.f);
	TestFalse(TEXT("Not finished at the start"), Sequencer->IsFinished());

	const float Ticked = HawkeyeFlashbackTest::RunToEnd(Sequencer, 0.25f, /*Budget=*/30.f);
	TestTrue(TEXT("Finished at the total duration"), Sequencer->IsFinished());
	TestEqual(TEXT("It took the whole duration to get there"), Ticked, 15.f, 0.26f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlashbackSlideProgression, "Hawkeye.Flashback.SlideProgression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlashbackSlideProgression::RunTest(const FString& Parameters)
{
	UFlashbackSequencer* Sequencer = NewObject<UFlashbackSequencer>();
	Sequencer->Initialize(HawkeyeFlashbackTest::MakeFlashback(2, /*Hold=*/4.f, /*Crossfade=*/1.f));

	TestEqual(TEXT("Starts on the first slide"), Sequencer->GetCurrentSlideIndex(), 0);
	TestEqual(TEXT("Fully opaque while holding"), Sequencer->GetBlendAlpha(), 0.f);

	Sequencer->Advance(4.5f);
	TestEqual(TEXT("Still the first slide during its crossfade"), Sequencer->GetCurrentSlideIndex(), 0);
	TestEqual(TEXT("Half way through the crossfade"), Sequencer->GetBlendAlpha(), 0.5f, 0.001f);

	Sequencer->Advance(1.f);
	TestEqual(TEXT("Second slide once the crossfade ends"), Sequencer->GetCurrentSlideIndex(), 1);
	TestEqual(TEXT("Holding again"), Sequencer->GetBlendAlpha(), 0.f);

	Sequencer->Advance(5.f);
	TestTrue(TEXT("Finished after the fade to black"), Sequencer->IsFinished());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlashbackSkipIgnoredInFirstHalfSecond, "Hawkeye.Flashback.SkipIgnoredInFirstHalfSecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlashbackSkipIgnoredInFirstHalfSecond::RunTest(const FString& Parameters)
{
	UFlashbackSequencer* Sequencer = NewObject<UFlashbackSequencer>();
	Sequencer->Initialize(HawkeyeFlashbackTest::MakeFlashback(3, /*Hold=*/4.f, /*Crossfade=*/1.f));

	TestFalse(TEXT("Cannot skip at time zero"), Sequencer->CanSkip());
	TestFalse(TEXT("TrySkip does nothing"), Sequencer->TrySkip());

	Sequencer->Advance(0.4f);
	TestFalse(TEXT("Still inside the lockout"), Sequencer->TrySkip());
	TestFalse(TEXT("Playback continues"), Sequencer->IsFinished());

	Sequencer->Advance(0.2f);
	TestTrue(TEXT("Skippable once the lockout passes"), Sequencer->CanSkip());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFlashbackSkipFinishesWholeThing, "Hawkeye.Flashback.SkipFinishesWholeThing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFlashbackSkipFinishesWholeThing::RunTest(const FString& Parameters)
{
	UFlashbackSequencer* Sequencer = NewObject<UFlashbackSequencer>();
	Sequencer->Initialize(HawkeyeFlashbackTest::MakeFlashback(4, /*Hold=*/4.f, /*Crossfade=*/1.f));

	Sequencer->Advance(1.f);
	TestEqual(TEXT("On the first slide"), Sequencer->GetCurrentSlideIndex(), 0);

	TestTrue(TEXT("Skip accepted"), Sequencer->TrySkip());
	TestTrue(TEXT("The whole flashback is over, not just the slide"), Sequencer->IsFinished());
	TestEqual(TEXT("Elapsed jumped to the end"), Sequencer->GetElapsedSeconds(), Sequencer->GetTotalDuration());
	TestFalse(TEXT("A second skip does nothing"), Sequencer->TrySkip());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
