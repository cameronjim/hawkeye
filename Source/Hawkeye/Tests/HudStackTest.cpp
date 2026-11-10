#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "Components/OverlaySlot.h"
#include "Settings/HawkeyeSettings.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeHintWidget.h"
#include "UI/HawkeyeHints.h"
#include "UI/HawkeyeHotbarWidget.h"
#include "UI/HawkeyeHudWidget.h"
#include "Widgets/SWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeHudStackTest
{
	/** The widest hint line on a keyboard: the one most likely to be tall (keycaps) and to crowd the subtitle. */
	const FHawkeyeHintRule* LongestRule()
	{
		const FHawkeyeHintRule* Best = nullptr;
		int32 BestLength = -1;
		for (const FHawkeyeHintRule& Rule : UHawkeyeHintRules::GetDefaultRules())
		{
			const int32 Length = UHawkeyeHintRules::ResolveToString(Rule, FHawkeyeHintContext()).Len();
			if (Length > BestLength)
			{
				BestLength = Length;
				Best = &Rule;
			}
		}
		return Best;
	}
}

/**
 * The bottom of the screen at HUD scales 0.8, 1.0 and 1.4 with the largest subtitle size: the real HUD built in
 * Slate (headless, laid out by a prepass), the longest hint on the line and a banter subtitle up. The hint line
 * sits over the hotbar and the subtitle's bottom edge sits over the hint line's top with a gap, at every scale,
 * and the subtitle does not drop back when the hint goes (so it never jumps under a line being read).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHudHintAndSubtitleStack, "Hawkeye.Hud.HintAndSubtitleStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHudHintAndSubtitleStack::RunTest(const FString& Parameters)
{
	const FHawkeyeHintRule* Rule = HawkeyeHudStackTest::LongestRule();
	if (!TestNotNull(TEXT("there are hint rules"), Rule))
	{
		return false;
	}

	for (const float Scale : { 0.8f, 1.0f, 1.4f })
	{
		FHawkeyeTestWorld World;
		UHawkeyeHudWidget* Hud = CreateWidget<UHawkeyeHudWidget>(World.Get(), UHawkeyeHudWidget::StaticClass());
		if (!TestNotNull(TEXT("the HUD is created"), Hud))
		{
			return false;
		}
		const TSharedRef<SWidget> Slate = Hud->TakeWidget();
		FHawkeyeSettings Settings;
		Settings.HudScale = Scale;
		Settings.SubtitleSize = EHawkeyeSubtitleSize::Large;
		Settings.SubtitleBackgroundOpacity = 0.6f;
		Hud->ApplySettings(Settings);

		UHawkeyeHintWidget* Hint = Hud->GetHintLine();
		if (!TestNotNull(TEXT("the HUD has a hint line"), Hint))
		{
			return false;
		}
		Hud->ShowSubtitle(FText::FromString(TEXT("Kate")),
			FText::FromString(TEXT("[Placeholder banter line, long enough to fill the box across the bottom]")), 5.f);

		// A few frames of layout: the prepass measures, the line and the stack place themselves from it.
		auto Frame = [&](const FHawkeyeHintRule* Shown)
		{
			Slate->SlatePrepass(1.f);
			Hint->UpdateLine(Shown, 1.f, FHawkeyeHintContext(), 0.5f);
			Hud->LayOutBottomStack();
		};
		for (int32 Step = 0; Step < 3; ++Step)
		{
			Frame(Rule);
		}

		const float HotbarTop = 24.f + Hud->GetHotbar()->GetDesiredSize().Y * Hud->GetHotbarRenderScale();
		const float HintBottom = Hint->GetBottomPadding();
		const float HintHeight = Hint->GetDesiredSize().Y * Scale;
		const float HintTop = HintBottom + HintHeight;
		const float SubtitleBottom = Hud->GetSubtitleBottom();
		const UOverlaySlot* HintSlot = Cast<UOverlaySlot>(Hint->Slot);
		AddInfo(FString::Printf(TEXT("HUD %.1f: hotbar top %.0f, hint %.0f to %.0f (%.0f px tall, \"%s\"), subtitle from %.0f."),
			Scale, HotbarTop, HintBottom, HintTop, HintHeight, *Hint->GetShownText(), SubtitleBottom));

		TestTrue(FString::Printf(TEXT("HUD %.1f: the hint line was laid out (it has a height)"), Scale), HintHeight > 10.f);
		TestTrue(FString::Printf(TEXT("HUD %.1f: the hint line's slot is where it says"), Scale),
			HintSlot && FMath::IsNearlyEqual(HintSlot->GetPadding().Bottom, HintBottom, 0.5f));
		TestTrue(FString::Printf(TEXT("HUD %.1f: the hint line is over the hotbar"), Scale), HintBottom >= HotbarTop);
		TestTrue(FString::Printf(TEXT("HUD %.1f: the subtitle is over the hint line (%.0f >= %.0f + %.0f)"), Scale,
				SubtitleBottom, HintTop, Hud->SubtitleGapAboveHint),
			SubtitleBottom >= HintTop + Hud->SubtitleGapAboveHint - 0.5f);
		TestTrue(FString::Printf(TEXT("HUD %.1f: and never lower than its own place"), Scale),
			SubtitleBottom >= Hud->SubtitleMinBottom - 0.5f);
		TestTrue(FString::Printf(TEXT("HUD %.1f: the subtitle is up"), Scale), Hud->IsSubtitleVisible());

		// The hint goes: the subtitle keeps its place.
		for (int32 Step = 0; Step < 3; ++Step)
		{
			Frame(nullptr);
		}
		TestTrue(FString::Printf(TEXT("HUD %.1f: the hint has faded"), Scale), Hint->GetShownText().IsEmpty());
		TestEqual(FString::Printf(TEXT("HUD %.1f: the subtitle does not drop when the hint goes"), Scale),
			Hud->GetSubtitleBottom(), SubtitleBottom);
	}
	return true;
}

#endif
