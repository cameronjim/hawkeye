#include "UI/HawkeyeDifficultyPromptWidget.h"

#include "Audio/HawkeyeAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Settings/DifficultySubsystem.h"
#include "UI/HawkeyeMenuLayout.h"

void UHawkeyeDifficultyPromptWidget::AddChoice(TObjectPtr<UButton>& Button, const TCHAR* Name, EHawkeyeDifficulty Difficulty)
{
	HawkeyeMenuLayout::AddButton(WidgetTree, ButtonStack, Button, Name, UDifficultySubsystem::GetDifficultyName(Difficulty));
	UTextBlock* Blurb = HawkeyeMenuLayout::AddText(WidgetTree, ButtonStack, *(FString(Name) + TEXT("Blurb")),
		UDifficultySubsystem::GetDifficultyBlurb(Difficulty), 15, 14.f);
	Blurb->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.85f, 1.f)));
}

TSharedRef<SWidget> UHawkeyeDifficultyPromptWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		ButtonStack = HawkeyeMenuLayout::BuildFrame(WidgetTree, 0.9f);
		HawkeyeMenuLayout::AddText(WidgetTree, ButtonStack, TEXT("TitleText"),
			NSLOCTEXT("Hawkeye", "DifficultyPromptTitle", "Choose a difficulty"), 36, 8.f);
		HawkeyeMenuLayout::AddText(WidgetTree, ButtonStack, TEXT("HintText"),
			NSLOCTEXT("Hawkeye", "DifficultyPromptHint", "You can change it any time in Settings."), 16, 28.f);
		AddChoice(StoryButton, TEXT("StoryButton"), EHawkeyeDifficulty::Story);
		AddChoice(NormalButton, TEXT("NormalButton"), EHawkeyeDifficulty::Normal);
		AddChoice(HardButton, TEXT("HardButton"), EHawkeyeDifficulty::Hard);
	}
	return Super::RebuildWidget();
}

void UHawkeyeDifficultyPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!bBound)
	{
		if (StoryButton)
		{
			StoryButton->OnClicked.AddDynamic(this, &UHawkeyeDifficultyPromptWidget::HandleStoryClicked);
		}
		if (NormalButton)
		{
			NormalButton->OnClicked.AddDynamic(this, &UHawkeyeDifficultyPromptWidget::HandleNormalClicked);
		}
		if (HardButton)
		{
			HardButton->OnClicked.AddDynamic(this, &UHawkeyeDifficultyPromptWidget::HandleHardClicked);
		}
		for (UButton* Button : { StoryButton.Get(), NormalButton.Get(), HardButton.Get() })
		{
			if (Button)
			{
				Button->OnHovered.AddUniqueDynamic(this, &UHawkeyeDifficultyPromptWidget::HandleButtonHovered);
			}
		}
		bBound = true;
	}
	// Normal is the default: a pad's A (or Enter) on arrival takes it.
	if (NormalButton)
	{
		NormalButton->SetKeyboardFocus();
	}
}

void UHawkeyeDifficultyPromptWidget::NativeDestruct()
{
	if (bBound)
	{
		if (StoryButton)
		{
			StoryButton->OnClicked.RemoveDynamic(this, &UHawkeyeDifficultyPromptWidget::HandleStoryClicked);
		}
		if (NormalButton)
		{
			NormalButton->OnClicked.RemoveDynamic(this, &UHawkeyeDifficultyPromptWidget::HandleNormalClicked);
		}
		if (HardButton)
		{
			HardButton->OnClicked.RemoveDynamic(this, &UHawkeyeDifficultyPromptWidget::HandleHardClicked);
		}
		bBound = false;
	}
	Super::NativeDestruct();
}

void UHawkeyeDifficultyPromptWidget::Choose(EHawkeyeDifficulty Difficulty)
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	OnDifficultyChosen.Broadcast(Difficulty);
}

void UHawkeyeDifficultyPromptWidget::HandleStoryClicked()
{
	Choose(EHawkeyeDifficulty::Story);
}

void UHawkeyeDifficultyPromptWidget::HandleNormalClicked()
{
	Choose(EHawkeyeDifficulty::Normal);
}

void UHawkeyeDifficultyPromptWidget::HandleHardClicked()
{
	Choose(EHawkeyeDifficulty::Hard);
}

void UHawkeyeDifficultyPromptWidget::HandleButtonHovered()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Hover);
}
