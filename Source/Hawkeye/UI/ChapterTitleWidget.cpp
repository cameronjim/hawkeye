#include "UI/ChapterTitleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Hawkeye.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Mission/MissionDefinition.h"

TSharedRef<SWidget> UChapterTitleWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TitleRoot"));
		WidgetTree->RootWidget = Root;

		if (!Backdrop)
		{
			Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		}
		Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, BackdropOpacity));
		if (UOverlaySlot* BackdropSlot = Cast<UOverlaySlot>(Root->AddChild(Backdrop)))
		{
			BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
			BackdropSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TitleStack"));
		if (UOverlaySlot* StackSlot = Cast<UOverlaySlot>(Root->AddChild(Stack)))
		{
			StackSlot->SetHorizontalAlignment(HAlign_Center);
			StackSlot->SetVerticalAlignment(VAlign_Center);
		}
		auto AddText = [this, Stack](TObjectPtr<UTextBlock>& TextSlot, const TCHAR* Name, int32 FontSize,
			const FLinearColor& Color, float Bottom)
		{
			if (!TextSlot)
			{
				TextSlot = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			}
			FSlateFontInfo Font = TextSlot->GetFont();
			Font.Size = FontSize;
			TextSlot->SetFont(Font);
			TextSlot->SetColorAndOpacity(FSlateColor(Color));
			TextSlot->SetJustification(ETextJustify::Center);
			if (UVerticalBoxSlot* Entry = Cast<UVerticalBoxSlot>(Stack->AddChild(TextSlot)))
			{
				Entry->SetHorizontalAlignment(HAlign_Center);
				Entry->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
			}
		};
		// Kate purple over the title, cream under it: the HUD's two accents.
		AddText(ChapterNumberText, TEXT("ChapterNumberText"), 20, FLinearColor(0.78f, 0.55f, 1.f), 12.f);
		AddText(TitleText, TEXT("TitleText"), 60, FLinearColor::White, 16.f);
		AddText(SubtitleText, TEXT("SubtitleText"), 22, FLinearColor(1.f, 0.93f, 0.78f), 0.f);
	}
	return Super::RebuildWidget();
}

FText UChapterTitleWidget::GetChapterNumberText() const
{
	const FText Format = ChapterNumberFormat.IsEmpty()
		? NSLOCTEXT("Hawkeye", "ChapterNumberFormat", "[Chapter {0}]") : ChapterNumberFormat;
	return FText::Format(Format, FText::AsNumber(CurrentMission ? CurrentMission->MissionNumber : 0));
}

FText UChapterTitleWidget::GetTitleShown() const
{
	return CurrentMission ? CurrentMission->OpeningTitle : FText::GetEmpty();
}

void UChapterTitleWidget::Play(UMissionDefinition* Mission)
{
	CurrentMission = Mission;
	if (ChapterNumberText)
	{
		ChapterNumberText->SetText(GetChapterNumberText());
	}
	if (TitleText)
	{
		TitleText->SetText(GetTitleShown());
	}
	if (SubtitleText)
	{
		SubtitleText->SetText(Mission ? Mission->OpeningSubtitle : FText::GetEmpty());
	}
	// Never in the way of play: no hit testing, no focus.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Clock.Start();
	SetRenderOpacity(Clock.GetAlpha());
	UE_LOG(LogHawkeye, Log, TEXT("%s: chapter title for %s (%.1f s)."), *GetName(), *GetNameSafe(Mission), Clock.GetTotalSeconds());
}

bool UChapterTitleWidget::Skip()
{
	const bool bSkipped = Clock.TrySkip();
	if (bSkipped)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: chapter title skipped at %.2f s."), *GetName(), Clock.GetElapsed());
	}
	return bSkipped;
}

void UChapterTitleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Clock.IsRunning())
	{
		return;
	}
	if (Clock.Advance(InDeltaTime))
	{
		Finish();
		return;
	}
	SetRenderOpacity(Clock.GetAlpha());
}

void UChapterTitleWidget::Finish()
{
	SetRenderOpacity(0.f);
	RemoveFromParent();
	UMissionDefinition* Finished = CurrentMission;
	OnTitleFinished.Broadcast(Finished);
}
