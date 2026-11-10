#include "UI/HawkeyeSafehouseWidget.h"

#include "Audio/HawkeyeAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/FlashbackReplayEntryWidget.h"
#include "UI/HawkeyeMenuLayout.h"

namespace HawkeyeSafehouseMenu
{
	static UVerticalBox* AddBox(UWidgetTree* Tree, UVerticalBox* Stack, const TCHAR* Name)
	{
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), Name);
		if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(Box)))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Box;
	}
}

TSharedRef<SWidget> UHawkeyeSafehouseWidget::RebuildWidget()
{
	using namespace HawkeyeSafehouseMenu;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Stack = HawkeyeMenuLayout::BuildFrame(WidgetTree, 0.6f);
		TitleText = HawkeyeMenuLayout::AddText(WidgetTree, Stack, TEXT("TitleText"), SafehouseName, 28, 2.f);
		SubtitleText = HawkeyeMenuLayout::AddText(WidgetTree, Stack, TEXT("SubtitleText"), SubtitleLine, 13, 8.f);
		SubtitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.72f, 0.8f, 1.f)));
		StatusText = HawkeyeMenuLayout::AddText(WidgetTree, Stack, TEXT("StatusText"), StatusLine, 14, 24.f);

		MainBox = AddBox(WidgetTree, Stack, TEXT("MainBox"));
		HawkeyeMenuLayout::AddButton(WidgetTree, MainBox, RefillButton, TEXT("RefillButton"),
			NSLOCTEXT("Hawkeye", "SafehouseRefill", "Refill arrows"));
		HawkeyeMenuLayout::AddButton(WidgetTree, MainBox, SaveButton, TEXT("SaveButton"),
			NSLOCTEXT("Hawkeye", "SafehouseSave", "Save"));
		HawkeyeMenuLayout::AddButton(WidgetTree, MainBox, FastTravelButton, TEXT("FastTravelButton"),
			NSLOCTEXT("Hawkeye", "SafehouseFastTravel", "Fast travel"));
		HawkeyeMenuLayout::AddButton(WidgetTree, MainBox, ChapterSelectButton, TEXT("ChapterSelectButton"),
			NSLOCTEXT("Hawkeye", "SafehouseChapterSelect", "Chapter select"));
		HawkeyeMenuLayout::AddButton(WidgetTree, MainBox, LeaveButton, TEXT("LeaveButton"),
			NSLOCTEXT("Hawkeye", "SafehouseLeave", "Leave"));

		ListFrame = AddBox(WidgetTree, Stack, TEXT("ListFrame"));
		ListBox = AddBox(WidgetTree, ListFrame, TEXT("ListBox"));
		HawkeyeMenuLayout::AddButton(WidgetTree, ListFrame, BackButton, TEXT("BackButton"),
			NSLOCTEXT("Hawkeye", "SafehouseBack", "Back"));
		RebuildList();
	}
	return Super::RebuildWidget();
}

void UHawkeyeSafehouseWidget::SetSafehouseName(const FText& Name)
{
	SafehouseName = FText::Format(NSLOCTEXT("Hawkeye", "SafehouseTitle", "Safehouse: {0}"), Name);
	if (TitleText)
	{
		TitleText->SetText(SafehouseName);
	}
}

void UHawkeyeSafehouseWidget::SetSubtitle(const FText& Subtitle)
{
	SubtitleLine = Subtitle;
	if (SubtitleText)
	{
		SubtitleText->SetText(SubtitleLine);
	}
}

void UHawkeyeSafehouseWidget::SetStatus(const FText& Status)
{
	StatusLine = Status;
	if (StatusText)
	{
		StatusText->SetText(StatusLine);
	}
}

void UHawkeyeSafehouseWidget::ShowList(const FText& Heading, const TArray<FHawkeyeMenuListRow>& Rows)
{
	ListHeading = Heading;
	ListRows = Rows;
	bListShown = true;
	RebuildList();
}

void UHawkeyeSafehouseWidget::ShowMain()
{
	bListShown = false;
	RebuildList();
}

void UHawkeyeSafehouseWidget::RebuildList()
{
	for (UFlashbackReplayEntryWidget* Row : RowWidgets)
	{
		if (Row)
		{
			Row->OnPicked.RemoveDynamic(this, &UHawkeyeSafehouseWidget::HandleRowPicked);
		}
	}
	RowWidgets.Reset();
	if (MainBox)
	{
		MainBox->SetVisibility(bListShown ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (ListFrame)
	{
		ListFrame->SetVisibility(bListShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!ListBox || !WidgetTree)
	{
		return;
	}
	ListBox->ClearChildren();
	if (!bListShown)
	{
		return;
	}
	HawkeyeMenuLayout::AddText(WidgetTree, ListBox, TEXT("ListHeading"), ListHeading, 18, 10.f);
	for (int32 Index = 0; Index < ListRows.Num(); ++Index)
	{
		UFlashbackReplayEntryWidget* Row = CreateWidget<UFlashbackReplayEntryWidget>(this, UFlashbackReplayEntryWidget::StaticClass());
		if (!Row)
		{
			continue;
		}
		Row->Setup(Index, ListRows[Index].Label);
		Row->SetEntryEnabled(ListRows[Index].bEnabled);
		Row->OnPicked.AddDynamic(this, &UHawkeyeSafehouseWidget::HandleRowPicked);
		if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(ListBox->AddChild(Row)))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
			RowSlot->SetPadding(FMargin(0.f, 6.f));
		}
		RowWidgets.Add(Row);
	}
}

bool UHawkeyeSafehouseWidget::PickListRow(int32 Index)
{
	if (!bListShown || !ListRows.IsValidIndex(Index) || !ListRows[Index].bEnabled)
	{
		return false;
	}
	OnListRowPicked.Broadcast(Index);
	return true;
}

void UHawkeyeSafehouseWidget::HandleRowPicked(int32 Index)
{
	PickListRow(Index);
}

void UHawkeyeSafehouseWidget::HandleBackClicked()
{
	ShowMain();
	OnListBackClicked.Broadcast();
}

void UHawkeyeSafehouseWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (bBound)
	{
		return;
	}
	if (RefillButton)
	{
		RefillButton->OnClicked.AddDynamic(this, &UHawkeyeSafehouseWidget::HandleRefillClicked);
	}
	if (SaveButton)
	{
		SaveButton->OnClicked.AddDynamic(this, &UHawkeyeSafehouseWidget::HandleSaveClicked);
	}
	if (FastTravelButton)
	{
		FastTravelButton->OnClicked.AddDynamic(this, &UHawkeyeSafehouseWidget::HandleFastTravelClicked);
	}
	if (ChapterSelectButton)
	{
		ChapterSelectButton->OnClicked.AddDynamic(this, &UHawkeyeSafehouseWidget::HandleChapterSelectClicked);
	}
	if (LeaveButton)
	{
		LeaveButton->OnClicked.AddDynamic(this, &UHawkeyeSafehouseWidget::HandleLeaveClicked);
	}
	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &UHawkeyeSafehouseWidget::HandleBackClicked);
	}
	for (UButton* Button : { RefillButton.Get(), SaveButton.Get(), FastTravelButton.Get(), ChapterSelectButton.Get(),
			 LeaveButton.Get(), BackButton.Get() })
	{
		if (Button)
		{
			Button->OnHovered.AddUniqueDynamic(this, &UHawkeyeSafehouseWidget::HandleButtonHovered);
			Button->OnPressed.AddUniqueDynamic(this, &UHawkeyeSafehouseWidget::HandleButtonPressed);
		}
	}
	bBound = true;
}

void UHawkeyeSafehouseWidget::HandleButtonHovered()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Hover);
}

void UHawkeyeSafehouseWidget::HandleButtonPressed()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
}

void UHawkeyeSafehouseWidget::NativeDestruct()
{
	if (bBound)
	{
		if (RefillButton)
		{
			RefillButton->OnClicked.RemoveDynamic(this, &UHawkeyeSafehouseWidget::HandleRefillClicked);
		}
		if (SaveButton)
		{
			SaveButton->OnClicked.RemoveDynamic(this, &UHawkeyeSafehouseWidget::HandleSaveClicked);
		}
		if (FastTravelButton)
		{
			FastTravelButton->OnClicked.RemoveDynamic(this, &UHawkeyeSafehouseWidget::HandleFastTravelClicked);
		}
		if (ChapterSelectButton)
		{
			ChapterSelectButton->OnClicked.RemoveDynamic(this, &UHawkeyeSafehouseWidget::HandleChapterSelectClicked);
		}
		if (LeaveButton)
		{
			LeaveButton->OnClicked.RemoveDynamic(this, &UHawkeyeSafehouseWidget::HandleLeaveClicked);
		}
		if (BackButton)
		{
			BackButton->OnClicked.RemoveDynamic(this, &UHawkeyeSafehouseWidget::HandleBackClicked);
		}
		bBound = false;
	}
	Super::NativeDestruct();
}

void UHawkeyeSafehouseWidget::HandleRefillClicked()
{
	OnRefillClicked.Broadcast();
}

void UHawkeyeSafehouseWidget::HandleSaveClicked()
{
	OnSaveClicked.Broadcast();
}

void UHawkeyeSafehouseWidget::HandleFastTravelClicked()
{
	OnFastTravelClicked.Broadcast();
}

void UHawkeyeSafehouseWidget::HandleChapterSelectClicked()
{
	OnChapterSelectClicked.Broadcast();
}

void UHawkeyeSafehouseWidget::HandleLeaveClicked()
{
	OnLeaveClicked.Broadcast();
}
