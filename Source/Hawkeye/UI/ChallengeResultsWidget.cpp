#include "UI/ChallengeResultsWidget.h"

#include "Audio/HawkeyeAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Challenge/ChallengeRules.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "InputCoreTypes.h"
#include "UI/HawkeyeMenuLayout.h"

namespace HawkeyeChallengeResults
{
	/** Font size and space under each line, top to bottom. */
	static const int32 Sizes[] = { 28, 16, 20, 20, 24, 16 };
	static const float Gaps[] = { 4.f, 18.f, 4.f, 4.f, 4.f, 24.f };
	static constexpr int32 LineCount = 6;
}

TSharedRef<SWidget> UChallengeResultsWidget::RebuildWidget()
{
	using namespace HawkeyeChallengeResults;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Stack = HawkeyeMenuLayout::BuildFrame(WidgetTree, 0.55f);
		LineTexts.Reset();
		for (int32 Index = 0; Index < LineCount; ++Index)
		{
			LineTexts.Add(HawkeyeMenuLayout::AddText(WidgetTree, Stack, *FString::Printf(TEXT("ResultLine%d"), Index),
				Lines.IsValidIndex(Index) ? Lines[Index] : FText::GetEmpty(), Sizes[Index], Gaps[Index]));
		}
		HawkeyeMenuLayout::AddButton(WidgetTree, Stack, RetryButton, TEXT("RetryButton"), NSLOCTEXT("Hawkeye", "ChallengeRetry", "[Retry]"));
		HawkeyeMenuLayout::AddButton(WidgetTree, Stack, LeaveButton, TEXT("LeaveButton"), NSLOCTEXT("Hawkeye", "ChallengeLeave", "[Leave]"));
	}
	return Super::RebuildWidget();
}

TArray<FText> UChallengeResultsWidget::FormatLines(const FChallengeResult& Result)
{
	TArray<FText> Out;
	Out.Add(Result.Name);
	Out.Add(UChallengeRules::EndReasonText(Result.Reason).ToUpper());
	Out.Add(FText::Format(NSLOCTEXT("Hawkeye", "ResultTime", "Time {0}"), UChallengeRules::FormatSeconds(Result.Seconds)));
	Out.Add(Result.Type == EChallengeType::Archery
		? FText::Format(NSLOCTEXT("Hawkeye", "ResultScore", "Score {0}   ({1}/{2} targets)"), Result.Score, Result.Progress, Result.Total)
		: FText::Format(NSLOCTEXT("Hawkeye", "ResultCheckpoints", "Checkpoints {0}/{1}"), Result.Progress, Result.Total));
	Out.Add(UChallengeRules::MedalText(Result.Medal));
	FText Best = NSLOCTEXT("Hawkeye", "ResultNoBest", "Best -");
	if (Result.Record.HasCompleted())
	{
		const FText Value = Result.Type == EChallengeType::Archery ? FText::AsNumber(Result.Record.BestScore)
			: UChallengeRules::FormatSeconds(Result.Record.BestSeconds);
		Best = FText::Format(Result.bNewBest ? NSLOCTEXT("Hawkeye", "ResultNewBest", "New best {0} ({1})")
			: NSLOCTEXT("Hawkeye", "ResultBest", "Best {0} ({1})"), Value, UChallengeRules::MedalText(Result.Record.BestMedal));
	}
	Out.Add(Best);
	return Out;
}

void UChallengeResultsWidget::SetResult(const FChallengeResult& Result)
{
	Lines = FormatLines(Result);
	ApplyLines();
}

void UChallengeResultsWidget::ApplyLines()
{
	for (int32 Index = 0; Index < LineTexts.Num(); ++Index)
	{
		if (LineTexts[Index])
		{
			LineTexts[Index]->SetText(Lines.IsValidIndex(Index) ? Lines[Index] : FText::GetEmpty());
		}
	}
}

void UChallengeResultsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	ApplyLines();
	if (bBound)
	{
		return;
	}
	if (RetryButton)
	{
		RetryButton->OnClicked.AddDynamic(this, &UChallengeResultsWidget::HandleRetryClicked);
	}
	if (LeaveButton)
	{
		LeaveButton->OnClicked.AddDynamic(this, &UChallengeResultsWidget::HandleLeaveClicked);
	}
	for (UButton* Button : { RetryButton.Get(), LeaveButton.Get() })
	{
		if (Button)
		{
			Button->OnHovered.AddUniqueDynamic(this, &UChallengeResultsWidget::HandleButtonHovered);
			Button->OnPressed.AddUniqueDynamic(this, &UChallengeResultsWidget::HandleButtonPressed);
		}
	}
	bBound = true;
}

void UChallengeResultsWidget::NativeDestruct()
{
	if (bBound)
	{
		if (RetryButton)
		{
			RetryButton->OnClicked.RemoveDynamic(this, &UChallengeResultsWidget::HandleRetryClicked);
		}
		if (LeaveButton)
		{
			LeaveButton->OnClicked.RemoveDynamic(this, &UChallengeResultsWidget::HandleLeaveClicked);
		}
		bBound = false;
	}
	Super::NativeDestruct();
}

void UChallengeResultsWidget::HandleRetryClicked()
{
	OnRetryClicked.Broadcast();
}

void UChallengeResultsWidget::HandleLeaveClicked()
{
	OnLeaveClicked.Broadcast();
}

void UChallengeResultsWidget::HandleButtonHovered()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Hover);
}

void UChallengeResultsWidget::HandleButtonPressed()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
}

FReply UChallengeResultsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// Enter (or the pad's A) repeats the default button: Retry.
	if (Key == EKeys::Enter || Key == EKeys::Virtual_Gamepad_Accept.GetVirtualKey() || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		HandleRetryClicked();
		return FReply::Handled();
	}

	// Escape (or the pad's B) leaves, same as clicking Leave.
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		HandleLeaveClicked();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
