#include "UI/PhoneWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Phone/PhoneSubsystem.h"

namespace HawkeyePhoneStyle
{
	static const FLinearColor Body(0.02f, 0.02f, 0.03f, 0.94f);
	static const FLinearColor Purple(0.78f, 0.55f, 1.f, 1.f);
	static const FLinearColor Cream(1.f, 0.93f, 0.78f, 1.f);
	static const FLinearColor Dim(0.6f, 0.6f, 0.65f, 1.f);
	static const FLinearColor Selected(0.19f, 0.07f, 0.3f, 1.f);
	static const FLinearColor Bubble(0.1f, 0.1f, 0.12f, 1.f);
}

TSharedRef<SWidget> UPhoneWidget::RebuildWidget()
{
	using namespace HawkeyePhoneStyle;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PhoneRoot"));
		WidgetTree->RootWidget = Root;

		Panel = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PhonePanel"));
		Panel->SetWidthOverride(PanelWidth);
		if (UOverlaySlot* PanelSlot = Cast<UOverlaySlot>(Root->AddChild(Panel)))
		{
			PanelSlot->SetHorizontalAlignment(HAlign_Right);
			PanelSlot->SetVerticalAlignment(VAlign_Fill);
			PanelSlot->SetPadding(FMargin(0.f, 90.f, 48.f, 110.f));
		}

		UBorder* Shell = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PhoneBody"));
		Shell->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 18.f, FLinearColor(0.3f, 0.12f, 0.45f), 2.f));
		Shell->SetBrushColor(HawkeyePhoneStyle::Body);
		Shell->SetPadding(FMargin(20.f, 18.f));
		Panel->AddChild(Shell);

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PhoneStack"));
		Shell->SetContent(Stack);

		auto AddToStack = [Stack](UWidget* Child, float Bottom, bool bFill)
		{
			if (UVerticalBoxSlot* Entry = Cast<UVerticalBoxSlot>(Stack->AddChild(Child)))
			{
				Entry->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
				Entry->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
			}
		};
		AddToStack(MakeText(NSLOCTEXT("Hawkeye", "PhoneHeader", "[Messages]"), 22, Purple), 12.f, false);
		ContactList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContactList"));
		AddToStack(ContactList, 12.f, false);

		UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PhoneRule"));
		Rule->SetBrushColor(FLinearColor(0.3f, 0.12f, 0.45f));
		Rule->SetPadding(FMargin(0.f, 1.f));
		AddToStack(Rule, 12.f, false);

		ThreadScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ThreadScroll"));
		ThreadList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ThreadList"));
		ThreadScroll->AddChild(ThreadList);
		AddToStack(ThreadScroll, 12.f, true);

		HintText = MakeText(NSLOCTEXT("Hawkeye", "PhoneHint", "[P] [Close]"), 13, Dim);
		AddToStack(HintText, 0.f, false);
	}
	return Super::RebuildWidget();
}

void UPhoneWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	RefreshHint();
}

UTextBlock* UPhoneWidget::MakeText(const FText& Text, int32 Size, const FLinearColor& Color) const
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = Size;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetText(Text);
	Block->SetAutoWrapText(true);
	return Block;
}

FString UPhoneWidget::GetSelectedContact() const
{
	return Contacts.IsValidIndex(SelectedIndex) ? Contacts[SelectedIndex] : FString();
}

void UPhoneWidget::Open()
{
	bOpen = true;
	SetVisibility(ESlateVisibility::Visible);
	SelectedIndex = 0;
	Refresh();
	SelectContact(0);
	SetKeyboardFocus();
}

void UPhoneWidget::Close()
{
	bOpen = false;
}

void UPhoneWidget::Refresh()
{
	const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(this);
	const UPhoneInbox* Inbox = Phone ? Phone->GetInbox() : nullptr;
	const FString Keep = GetSelectedContact();
	Contacts = Inbox ? Inbox->GetContacts() : TArray<FString>();
	const int32 Kept = Contacts.IndexOfByKey(Keep);
	SelectedIndex = Contacts.IsEmpty() ? INDEX_NONE : (Kept != INDEX_NONE ? Kept : FMath::Clamp(SelectedIndex, 0, Contacts.Num() - 1));

	if (ContactList)
	{
		ContactList->ClearChildren();
		ContactRows.Reset();
		for (int32 Index = 0; Index < Contacts.Num(); ++Index)
		{
			AddContactRow(Index);
		}
		if (Contacts.IsEmpty())
		{
			ContactList->AddChild(MakeText(NSLOCTEXT("Hawkeye", "PhoneEmpty", "[No messages]"), 16, HawkeyePhoneStyle::Dim));
		}
	}
	RebuildThread();
}

void UPhoneWidget::AddContactRow(int32 Index)
{
	using namespace HawkeyePhoneStyle;
	const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(this);
	const int32 Unread = Phone && Phone->GetInbox() ? Phone->GetInbox()->GetUnreadCountFrom(Contacts[Index]) : 0;
	const FText Label = Unread > 0
		? FText::Format(NSLOCTEXT("Hawkeye", "PhoneContactUnread", "{0}  ({1})"), FText::FromString(Contacts[Index]), FText::AsNumber(Unread))
		: FText::FromString(Contacts[Index]);

	UBorder* Row = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Row->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 8.f));
	Row->SetBrushColor(Index == SelectedIndex ? Selected : FLinearColor::Transparent);
	Row->SetPadding(FMargin(10.f, 6.f));
	Row->SetContent(MakeText(Label, 18, Unread > 0 ? Cream : FLinearColor::White));
	if (UVerticalBoxSlot* Entry = Cast<UVerticalBoxSlot>(ContactList->AddChild(Row)))
	{
		Entry->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}
	ContactRows.Add(Row);
}

void UPhoneWidget::RebuildThread()
{
	ThreadLength = 0;
	if (!ThreadList)
	{
		return;
	}
	ThreadList->ClearChildren();
	const UPhoneSubsystem* Phone = UPhoneSubsystem::Get(this);
	if (!Phone || !Phone->GetInbox() || !Contacts.IsValidIndex(SelectedIndex))
	{
		return;
	}
	for (const FHawkeyeReceivedMessage& Message : Phone->GetInbox()->GetThread(Contacts[SelectedIndex]))
	{
		UBorder* Bubble = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Bubble->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 12.f));
		Bubble->SetBrushColor(HawkeyePhoneStyle::Bubble);
		Bubble->SetPadding(FMargin(12.f, 8.f));
		Bubble->SetContent(MakeText(Message.Text, 17, FLinearColor::White));
		if (UVerticalBoxSlot* Entry = Cast<UVerticalBoxSlot>(ThreadList->AddChild(Bubble)))
		{
			Entry->SetPadding(FMargin(0.f, 0.f, 40.f, 8.f));
			Entry->SetHorizontalAlignment(HAlign_Left);
		}
		++ThreadLength;
	}
	ThreadScroll->ScrollToEnd();
}

void UPhoneWidget::SelectContact(int32 Index)
{
	if (Contacts.IsEmpty())
	{
		SelectedIndex = INDEX_NONE;
		RebuildThread();
		return;
	}
	SelectedIndex = FMath::Clamp(Index, 0, Contacts.Num() - 1);
	// Showing the thread is reading it.
	if (UPhoneSubsystem* Phone = UPhoneSubsystem::Get(this))
	{
		Phone->ReadThread(Contacts[SelectedIndex]);
	}
	Refresh();
}

void UPhoneWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float Target = bOpen ? 1.f : 0.f;
	if (SlideAlpha != Target)
	{
		const float Step = SlideSeconds > 0.f ? InDeltaTime / SlideSeconds : 1.f;
		SlideAlpha = bOpen ? FMath::Min(SlideAlpha + Step, 1.f) : FMath::Max(SlideAlpha - Step, 0.f);
	}
	if (Panel)
	{
		// Ease out: quick to arrive, soft to settle.
		const float Eased = 1.f - FMath::Square(1.f - SlideAlpha);
		Panel->SetRenderTranslation(FVector2D((1.f - Eased) * (PanelWidth + 60.f), 0.f));
	}
	if (!bOpen && SlideAlpha <= 0.f && IsInViewport())
	{
		RemoveFromParent();
	}
	RefreshHint();
}

void UPhoneWidget::RefreshHint()
{
	if (!HintText)
	{
		return;
	}
	const AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer());
	HintText->SetText(PC && PC->IsUsingGamepad()
		? NSLOCTEXT("Hawkeye", "PhoneHintGamepad", "[Menu] Close   [B] Close")
		: NSLOCTEXT("Hawkeye", "PhoneHint", "[P] [Close]"));
}

FReply UPhoneWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (!bOpen)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}
	if (Key == EKeys::P || Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::Gamepad_FaceButton_Right
		|| Key == EKeys::Gamepad_Special_Right)
	{
		RequestClose();
		return FReply::Handled();
	}
	if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)
	{
		SelectContact(SelectedIndex - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)
	{
		SelectContact(SelectedIndex + 1);
		return FReply::Handled();
	}
	// Everything else is swallowed: the game is paused under the phone.
	return FReply::Handled();
}

FReply UPhoneWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	for (int32 Index = 0; Index < ContactRows.Num(); ++Index)
	{
		if (ContactRows[Index] && ContactRows[Index]->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
		{
			SelectContact(Index);
			return FReply::Handled();
		}
	}
	return FReply::Handled();
}

FReply UPhoneWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!Contacts.IsEmpty())
	{
		SelectContact(SelectedIndex + (InMouseEvent.GetWheelDelta() > 0.f ? -1 : 1));
	}
	return FReply::Handled();
}

void UPhoneWidget::RequestClose()
{
	if (AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer()))
	{
		PC->SetPhoneOpen(false);
		return;
	}
	Close();
}
