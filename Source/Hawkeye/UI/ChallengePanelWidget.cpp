#include "UI/ChallengePanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeRules.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Challenge/ChallengeTracker.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> UChallengePanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChallengePanel"));
		Panel->SetBrush(FSlateColorBrush(FLinearColor::White));
		Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
		Panel->SetPadding(FMargin(16.f, 10.f));
		WidgetTree->RootWidget = Panel;

		UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChallengeLines"));
		Panel->SetContent(Lines);
		auto AddLine = [this, Lines](const TCHAR* Name, int32 Size, const FLinearColor& Color)
		{
			UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			FSlateFontInfo Font = Line->GetFont();
			Font.Size = Size;
			Line->SetFont(Font);
			Line->SetColorAndOpacity(FSlateColor(Color));
			Line->SetShadowOffset(FVector2D(1.f, 1.f));
			Line->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
			if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Lines->AddChild(Line)))
			{
				LineSlot->SetHorizontalAlignment(HAlign_Right);
				LineSlot->SetPadding(FMargin(0.f, 1.f));
			}
			return Line;
		};
		TitleText = AddLine(TEXT("ChallengeTitle"), 15, TitleColor);
		ClockText = AddLine(TEXT("ChallengeClock"), 30, TextColor);
		ProgressText = AddLine(TEXT("ChallengeProgress"), 18, TextColor);
		BestText = AddLine(TEXT("ChallengeBest"), 13, TextColor);
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Super::RebuildWidget();
}

void UChallengePanelWidget::FormatLines(const UChallengeTracker* Tracker, FText& OutTitle, FText& OutClock, FText& OutProgress,
	FText& OutBest)
{
	const UChallengeDefinition* Definition = Tracker ? Tracker->GetActiveDefinition() : nullptr;
	if (!Definition)
	{
		OutTitle = OutClock = OutProgress = OutBest = FText::GetEmpty();
		return;
	}
	OutTitle = Definition->GetDisplayName();
	OutClock = UChallengeRules::FormatSeconds(Tracker->GetRemainingSeconds());
	const FChallengeRecord Record = Tracker->GetRecord(Definition->Id);
	if (Definition->Type == EChallengeType::Archery)
	{
		OutProgress = FText::Format(NSLOCTEXT("Hawkeye", "ChallengeScoreLine", "Score {0}   Targets {1}/{2}"),
			Tracker->GetScore(), Tracker->GetProgress(), Tracker->GetTotal());
		OutBest = Record.HasCompleted()
			? FText::Format(NSLOCTEXT("Hawkeye", "ChallengeBestScore", "Best {0}"), Record.BestScore)
			: NSLOCTEXT("Hawkeye", "ChallengeNoBest", "Best -");
		return;
	}
	OutProgress = FText::Format(NSLOCTEXT("Hawkeye", "ChallengeCheckpointLine", "Checkpoint {0}/{1}"),
		FMath::Min(Tracker->GetProgress() + 1, Tracker->GetTotal()), Tracker->GetTotal());
	OutBest = Record.HasCompleted()
		? FText::Format(NSLOCTEXT("Hawkeye", "ChallengeBestTime", "Best {0}"), UChallengeRules::FormatSeconds(Record.BestSeconds))
		: NSLOCTEXT("Hawkeye", "ChallengeNoBest", "Best -");
}

void UChallengePanelWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
	Super::NativeTick(MyGeometry, DeltaSeconds);
	const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	const UChallengeTracker* Tracker = Challenges ? Challenges->GetTracker() : nullptr;
	const bool bRunning = Tracker && Tracker->IsRunning();
	if (bRunning != bShown && Panel)
	{
		Panel->SetVisibility(bRunning ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	bShown = bRunning;
	if (!bRunning || !TitleText)
	{
		return;
	}
	FText Title, Clock, Progress, Best;
	FormatLines(Tracker, Title, Clock, Progress, Best);
	TitleText->SetText(Title);
	ClockText->SetText(Clock);
	ClockText->SetColorAndOpacity(FSlateColor(Tracker->GetRemainingSeconds() < 10.f ? HurryColor : TextColor));
	ProgressText->SetText(Progress);
	BestText->SetText(Best);
	ClockShown = Clock;
	ProgressShown = Progress;
}
