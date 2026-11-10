#include "UI/HawkeyeHintWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "UI/HawkeyeHintSubsystem.h"

namespace HawkeyeHintWidget
{
	static const FLinearColor TextColor(1.f, 1.f, 1.f, 1.f);
	/** The HUD's cream, so a key reads as the same thing as the marker's key hints. */
	static const FLinearColor KeyColor(0.96f, 0.92f, 0.82f, 1.f);
	static const FLinearColor KeyTextColor(0.06f, 0.05f, 0.08f, 1.f);
	static const FLinearColor BackingColor(0.f, 0.f, 0.f, 0.55f);
}

TSharedRef<SWidget> UHawkeyeHintWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Backing = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HintBacking"));
		Backing->SetBrush(FSlateColorBrush(FLinearColor::White));
		Backing->SetBrushColor(HawkeyeHintWidget::BackingColor);
		Backing->SetPadding(FMargin(0.f));
		WidgetTree->RootWidget = Backing;
		Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HintRow"));
		Backing->SetContent(Row);
		// Never collapsed: a collapsed widget does not tick, and the line is driven from its own tick. With
		// nothing up it is an empty box at opacity 0.
		SetVisibility(ESlateVisibility::HitTestInvisible);
		SetRenderOpacity(0.f);
	}
	return Super::RebuildWidget();
}

void UHawkeyeHintWidget::SetHudScale(float Scale)
{
	HudScale = FMath::Max(Scale, 0.1f);
	SetRenderTransformPivot(FVector2D(0.5f, 1.f));
	SetRenderScale(FVector2D(HudScale, HudScale));
	LastPadding = -1.f;
}

void UHawkeyeHintWidget::Rebuild(const TArray<FHawkeyeHintSegment>& Segments)
{
	using namespace HawkeyeHintWidget;
	if (!Row || !WidgetTree)
	{
		return;
	}
	Row->ClearChildren();
	for (const FHawkeyeHintSegment& Segment : Segments)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(FText::FromString(Segment.Text));
		if (Segment.bKey)
		{
			Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FontSize - 2));
			Text->SetColorAndOpacity(FSlateColor(KeyTextColor));
			UBorder* Cap = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			Cap->SetBrush(FSlateColorBrush(FLinearColor::White));
			Cap->SetBrushColor(KeyColor);
			Cap->SetPadding(FMargin(7.f, 1.f));
			Cap->SetContent(Text);
			if (UHorizontalBoxSlot* CapSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Cap)))
			{
				CapSlot->SetVerticalAlignment(VAlign_Center);
				CapSlot->SetPadding(FMargin(2.f, 0.f));
			}
		}
		else
		{
			Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FontSize));
			Text->SetColorAndOpacity(FSlateColor(TextColor));
			Text->SetShadowOffset(FVector2D(1.f, 1.f));
			Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f));
			if (UHorizontalBoxSlot* TextSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Text)))
			{
				TextSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
	}
}

float UHawkeyeHintWidget::GetBottomPadding() const
{
	float HotbarHeight = 64.f;
	float HotbarScale = HudScale;
	if (Hotbar)
	{
		const float Desired = Hotbar->GetDesiredSize().Y;
		HotbarHeight = Desired > 1.f ? Desired : HotbarHeight;
		HotbarScale = Hotbar->GetRenderTransform().Scale.Y;
	}
	// The hotbar grows up from its bottom edge by its scale; the line sits GapAboveHotbar over its top.
	return FMath::RoundToFloat(HotbarBottom + HotbarHeight * HotbarScale + GapAboveHotbar);
}

float UHawkeyeHintWidget::GetStackTop() const
{
	// The line is scaled about its bottom centre, so it grows up from its padding by the HUD scale.
	return GetBottomPadding() + FMath::Max(ReservedLineHeight, MeasuredLineHeight) * HudScale;
}

void UHawkeyeHintWidget::PlaceAboveHotbar()
{
	if (bBackingOpen)
	{
		// Last frame's layout of the line up now (two keycaps can make it taller than plain text).
		MeasuredLineHeight = FMath::Max(MeasuredLineHeight, static_cast<float>(GetDesiredSize().Y));
	}
	UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot);
	if (!OverlaySlot)
	{
		return;
	}
	const float BottomPadding = GetBottomPadding();
	if (!FMath::IsNearlyEqual(BottomPadding, LastPadding))
	{
		LastPadding = BottomPadding;
		OverlaySlot->SetHorizontalAlignment(HAlign_Center);
		OverlaySlot->SetVerticalAlignment(VAlign_Bottom);
		OverlaySlot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPadding));
	}
}

void UHawkeyeHintWidget::UpdateLine(const FHawkeyeHintRule* Rule, float Age, const FHawkeyeHintContext& Context, float DeltaSeconds)
{
	const float Fade = FMath::Max(FadeSeconds, 0.01f);
	if (Rule)
	{
		const TArray<FHawkeyeHintSegment> Segments = UHawkeyeHintRules::Resolve(*Rule, Context);
		FString Flat;
		for (const FHawkeyeHintSegment& Segment : Segments)
		{
			Flat += Segment.bKey ? FString::Printf(TEXT("[%s]"), *Segment.Text) : Segment.Text;
		}
		// A new hint, or the same one read for another device (a pad picked up mid-hint).
		if (Rule->Id != ShownId || Flat != ShownText)
		{
			Rebuild(Segments);
			ShownId = Rule->Id;
			ShownText = Flat;
		}
		Opacity = FMath::Clamp(Age / Fade, 0.f, 1.f);
	}
	else if (!ShownId.IsNone())
	{
		// Gone: fade the last line out, then drop it.
		Opacity = FMath::Max(Opacity - FMath::Max(DeltaSeconds, 0.f) / Fade, 0.f);
		if (Opacity <= 0.f)
		{
			ShownId = NAME_None;
			ShownText.Reset();
		}
	}
	SetRenderOpacity(ShownId.IsNone() ? 0.f : Opacity);
	// The backing only has size with a line in it.
	const bool bHasLine = !ShownId.IsNone();
	if (bHasLine != bBackingOpen)
	{
		bBackingOpen = bHasLine;
		if (!bHasLine && Row)
		{
			Row->ClearChildren();
		}
		if (Backing)
		{
			Backing->SetPadding(bHasLine ? FMargin(14.f, 5.f) : FMargin(0.f));
		}
	}
	PlaceAboveHotbar();
}

void UHawkeyeHintWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
	Super::NativeTick(MyGeometry, DeltaSeconds);
	const UHawkeyeHintSubsystem* Hints = UHawkeyeHintSubsystem::Get(this);
	const FHawkeyeHintRule* Rule = Hints ? Hints->GetShown() : nullptr;
	// A widget ticks on dilated time; the fade-out wants real time like the subsystem's clocks.
	const float RealDelta = FMath::Min(static_cast<float>(FApp::GetDeltaTime()), 0.1f);
	UpdateLine(Rule, Hints ? Hints->GetShownAge() : 0.f, Rule ? Hints->GetContext() : FHawkeyeHintContext(), RealDelta);
}
