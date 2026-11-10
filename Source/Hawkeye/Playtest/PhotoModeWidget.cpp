#include "Playtest/PhotoModeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UPhotoModeWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PhotoRoot"));
		WidgetTree->RootWidget = Root;

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PhotoCard"));
		Card->SetBrush(FSlateColorBrush(FLinearColor::White));
		Card->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Card->SetPadding(FMargin(10.f, 6.f));
		if (UOverlaySlot* CardSlot = Cast<UOverlaySlot>(Root->AddChild(Card)))
		{
			CardSlot->SetHorizontalAlignment(HAlign_Left);
			CardSlot->SetVerticalAlignment(VAlign_Top);
			CardSlot->SetPadding(FMargin(16.f));
		}

		UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PhotoLines"));
		Card->SetContent(Lines);

		TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PhotoTitle"));
		TitleText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13));
		TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.93f, 0.8f)));
		Lines->AddChild(TitleText);

		ControlsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PhotoControls"));
		ControlsText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10));
		ControlsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)));
		if (UVerticalBoxSlot* ControlsSlot = Cast<UVerticalBoxSlot>(Lines->AddChild(ControlsText)))
		{
			ControlsSlot->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
		}
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	return Super::RebuildWidget();
}

FString UPhotoModeWidget::GetControlsText(bool bGamepad)
{
	return bGamepad
		? FString(TEXT("L stick move, R stick look, RT up, LT down, L3 fast\nLB / RB field of view, Y reset, A photo, B back"))
		: FString(TEXT("WASD move, mouse look, E up, Q down, Shift fast\nWheel field of view, R reset, F12 photo, Esc back"));
}

void UPhotoModeWidget::SetState(float Fov, bool bGamepad, const FString& Status)
{
	const int32 Rounded = FMath::RoundToInt(Fov);
	const int32 Device = bGamepad ? 1 : 0;
	if (Rounded == ShownFov && Device == ShownDevice && Status == ShownStatus)
	{
		return;
	}
	ShownFov = Rounded;
	ShownDevice = Device;
	ShownStatus = Status;
	if (TitleText)
	{
		FString Title = FString::Printf(TEXT("Photo mode    FOV %d"), Rounded);
		if (!Status.IsEmpty())
		{
			Title += TEXT("    ") + Status;
		}
		TitleText->SetText(FText::FromString(Title));
	}
	if (ControlsText)
	{
		ControlsText->SetText(FText::FromString(GetControlsText(bGamepad)));
	}
}

FString UPhotoModeWidget::GetShownText() const
{
	return (TitleText ? TitleText->GetText().ToString() : FString()) + TEXT("\n")
		+ (ControlsText ? ControlsText->GetText().ToString() : FString());
}
