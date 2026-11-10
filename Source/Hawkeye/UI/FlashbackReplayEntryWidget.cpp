#include "UI/FlashbackReplayEntryWidget.h"

#include "Audio/HawkeyeAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"

void UFlashbackReplayEntryWidget::Setup(int32 InIndex, const FText& InLabel)
{
	Index = InIndex;
	Label = InLabel;
	if (LabelText)
	{
		LabelText->SetText(Label);
	}
}

void UFlashbackReplayEntryWidget::SetEntryEnabled(bool bEnabled)
{
	bEntryEnabled = bEnabled;
	if (Button)
	{
		Button->SetIsEnabled(bEnabled);
	}
	if (LabelText)
	{
		LabelText->SetColorAndOpacity(FSlateColor(bEnabled ? FLinearColor::White : FLinearColor(0.45f, 0.45f, 0.45f, 1.f)));
	}
}

TSharedRef<SWidget> UFlashbackReplayEntryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("EntryButton"));
		WidgetTree->RootWidget = Button;
		LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EntryLabel"));
		LabelText->SetText(Label);
		if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Button->AddChild(LabelText)))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetPadding(FMargin(48.f, 12.f));
		}
		SetEntryEnabled(bEntryEnabled);
	}
	return Super::RebuildWidget();
}

void UFlashbackReplayEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Button && !bBound)
	{
		Button->OnClicked.AddDynamic(this, &UFlashbackReplayEntryWidget::HandleClicked);
		bBound = true;
	}
}

void UFlashbackReplayEntryWidget::NativeDestruct()
{
	if (Button && bBound)
	{
		Button->OnClicked.RemoveDynamic(this, &UFlashbackReplayEntryWidget::HandleClicked);
		bBound = false;
	}
	Super::NativeDestruct();
}

void UFlashbackReplayEntryWidget::HandleClicked()
{
	if (!bEntryEnabled)
	{
		return;
	}
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	OnPicked.Broadcast(Index);
}
