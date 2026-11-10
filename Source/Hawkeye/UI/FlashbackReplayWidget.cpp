#include "UI/FlashbackReplayWidget.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Flashback/FlashbackDefinition.h"
#include "UI/FlashbackReplayEntryWidget.h"
#include "UI/HawkeyeMenuLayout.h"

TSharedRef<SWidget> UFlashbackReplayWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Stack = HawkeyeMenuLayout::BuildFrame(WidgetTree, 0.75f);
		HawkeyeMenuLayout::AddText(WidgetTree, Stack, TEXT("TitleText"),
			NSLOCTEXT("Hawkeye", "ReplayTitle", "Replay flashbacks"), 32, 24.f);
		ListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ListBox"));
		if (UVerticalBoxSlot* ListSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(ListBox)))
		{
			ListSlot->SetHorizontalAlignment(HAlign_Fill);
			ListSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
		}
		EmptyText = HawkeyeMenuLayout::AddText(WidgetTree, ListBox, TEXT("EmptyText"),
			NSLOCTEXT("Hawkeye", "ReplayEmpty", "No flashbacks seen yet."), 18, 12.f);
		HawkeyeMenuLayout::AddButton(WidgetTree, Stack, BackButton, TEXT("BackButton"), NSLOCTEXT("Hawkeye", "ReplayBack", "Back"));
	}
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	RebuildRows();
	return Built;
}

void UFlashbackReplayWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (BackButton && !bBound)
	{
		BackButton->OnClicked.AddDynamic(this, &UFlashbackReplayWidget::HandleBackClicked);
		bBound = true;
	}
}

void UFlashbackReplayWidget::NativeDestruct()
{
	if (BackButton && bBound)
	{
		BackButton->OnClicked.RemoveDynamic(this, &UFlashbackReplayWidget::HandleBackClicked);
		bBound = false;
	}
	Super::NativeDestruct();
}

void UFlashbackReplayWidget::SetFlashbacks(const TArray<FSoftObjectPath>& Seen)
{
	Flashbacks.Reset();
	for (const FSoftObjectPath& Path : Seen)
	{
		// A handful of small data assets, loaded when the list opens.
		if (UFlashbackDefinition* Flashback = Cast<UFlashbackDefinition>(Path.TryLoad()))
		{
			Flashbacks.Add(Flashback);
		}
		else
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: seen flashback %s did not load; not listed."), *GetName(), *Path.ToString());
		}
	}
	RebuildRows();
}

void UFlashbackReplayWidget::RebuildRows()
{
	if (!ListBox || !WidgetTree)
	{
		return;
	}
	for (UFlashbackReplayEntryWidget* Row : Rows)
	{
		if (Row)
		{
			Row->OnPicked.RemoveDynamic(this, &UFlashbackReplayWidget::HandleEntryPicked);
			Row->RemoveFromParent();
		}
	}
	Rows.Reset();
	if (EmptyText)
	{
		EmptyText->SetVisibility(Flashbacks.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	for (int32 Index = 0; Index < Flashbacks.Num(); ++Index)
	{
		UFlashbackReplayEntryWidget* Row = CreateWidget<UFlashbackReplayEntryWidget>(this, UFlashbackReplayEntryWidget::StaticClass());
		if (!Row)
		{
			continue;
		}
		Row->Setup(Index, MakeEntryLabel(Flashbacks[Index]));
		Row->OnPicked.AddDynamic(this, &UFlashbackReplayWidget::HandleEntryPicked);
		if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(ListBox->AddChild(Row)))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
			RowSlot->SetPadding(FMargin(0.f, 6.f));
		}
		Rows.Add(Row);
	}
}

FText UFlashbackReplayWidget::MakeEntryLabel(const UFlashbackDefinition* Flashback)
{
	if (!Flashback)
	{
		return FText::GetEmpty();
	}
	return Flashback->Title.IsEmpty() ? FText::FromString(Flashback->GetName()) : Flashback->Title;
}

FText UFlashbackReplayWidget::GetEntryLabel(int32 Index) const
{
	return Flashbacks.IsValidIndex(Index) ? MakeEntryLabel(Flashbacks[Index]) : FText::GetEmpty();
}

void UFlashbackReplayWidget::PickEntry(int32 Index)
{
	if (Flashbacks.IsValidIndex(Index) && Flashbacks[Index])
	{
		OnFlashbackPicked.Broadcast(Flashbacks[Index]);
	}
}

void UFlashbackReplayWidget::HandleEntryPicked(int32 Index)
{
	PickEntry(Index);
}

void UFlashbackReplayWidget::HandleBackClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	OnBackRequested.Broadcast();
}
