#include "UI/HawkeyeMenuLayout.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

UVerticalBox* HawkeyeMenuLayout::BuildFrame(UWidgetTree* WidgetTree, float DimAlpha)
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MenuRoot"));
	WidgetTree->RootWidget = Root;

	// A colour brush: UBorder's default brush has no resource and SetBrushColor alone paints nothing.
	UBorder* Dimmer = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dimmer"));
	Dimmer->SetBrush(FSlateColorBrush(FLinearColor::White));
	Dimmer->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, DimAlpha));
	if (UOverlaySlot* DimmerSlot = Cast<UOverlaySlot>(Root->AddChild(Dimmer)))
	{
		DimmerSlot->SetHorizontalAlignment(HAlign_Fill);
		DimmerSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ButtonStack"));
	if (UOverlaySlot* StackSlot = Cast<UOverlaySlot>(Root->AddChild(Stack)))
	{
		StackSlot->SetHorizontalAlignment(HAlign_Center);
		StackSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Stack;
}

UTextBlock* HawkeyeMenuLayout::AddText(UWidgetTree* WidgetTree, UVerticalBox* Stack, const TCHAR* Name, const FText& Text,
	int32 FontSize, float BottomPadding)
{
	UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Line->SetText(Text);
	FSlateFontInfo Font = Line->GetFont();
	Font.Size = FontSize;
	Line->SetFont(Font);
	Line->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(Line)))
	{
		LineSlot->SetHorizontalAlignment(HAlign_Center);
		LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
	}
	return Line;
}

void HawkeyeMenuLayout::AddButton(UWidgetTree* WidgetTree, UVerticalBox* Stack, TObjectPtr<UButton>& Button, const TCHAR* Name,
	const FText& Label)
{
	if (!Button)
	{
		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	}
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(FString(Name) + TEXT("Label")));
	Text->SetText(Label);
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetPadding(FMargin(48.f, 12.f));
	}
	if (UVerticalBoxSlot* Entry = Cast<UVerticalBoxSlot>(Stack->AddChild(Button)))
	{
		Entry->SetHorizontalAlignment(HAlign_Fill);
		Entry->SetPadding(FMargin(0.f, 6.f));
	}
}

FCheckBoxStyle HawkeyeMenuLayout::MakeCheckBoxStyle(const FCheckBoxStyle& Base, float BoxSize, float LabelGap)
{
	FCheckBoxStyle Style = Base;
	const FVector2D Size(BoxSize, BoxSize);
	for (FSlateBrush* Brush : { &Style.UncheckedImage, &Style.UncheckedHoveredImage, &Style.UncheckedPressedImage,
			&Style.CheckedImage, &Style.CheckedHoveredImage, &Style.CheckedPressedImage,
			&Style.UndeterminedImage, &Style.UndeterminedHoveredImage, &Style.UndeterminedPressedImage })
	{
		Brush->ImageSize = Size;
	}
	Style.Padding = FMargin(LabelGap, 0.f, 0.f, 0.f);
	return Style;
}
