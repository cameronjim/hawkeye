#include "UI/HawkeyeQuiverWheelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Combat/ArrowDefinition.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Player/InventoryComponent.h"
#include "UI/QuiverWheelMath.h"

namespace HawkeyeQuiverWheel
{
	static void CentreInCanvas(UCanvasPanelSlot* Slot, const FVector2D& Offset, const FVector2D& Size)
	{
		if (!Slot)
		{
			return;
		}
		Slot->SetAnchors(FAnchors(0.5f, 0.5f));
		Slot->SetAlignment(FVector2D(0.5f, 0.5f));
		Slot->SetAutoSize(Size.IsZero());
		Slot->SetPosition(Offset);
		if (!Size.IsZero())
		{
			Slot->SetSize(Size);
		}
	}

	static UTextBlock* MakeText(UWidgetTree* Tree, const FString& Name, int32 FontSize)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		Text->SetJustification(ETextJustify::Center);
		return Text;
	}
}

TSharedRef<SWidget> UHawkeyeQuiverWheelWidget::RebuildWidget()
{
	using namespace HawkeyeQuiverWheel;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WheelCanvas"));
		WidgetTree->RootWidget = Canvas;

		// A dark disc behind the ring so the labels read over a bright street, and the ring itself.
		const float Disc = (SegmentRadius + SegmentWidth * 0.75f) * 2.f;
		FSlateBrush DiscBrush;
		DiscBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		DiscBrush.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.35f));
		DiscBrush.OutlineSettings =
			FSlateBrushOutlineSettings(0.f, FSlateColor(FLinearColor(0.78f, 0.55f, 1.f, 0.9f)), 2.f);
		DiscBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Ring = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("WheelRing"));
		Ring->SetBrush(DiscBrush);
		CentreInCanvas(Cast<UCanvasPanelSlot>(Canvas->AddChild(Ring)), FVector2D::ZeroVector, FVector2D(Disc, Disc));

		CentreText = MakeText(WidgetTree, TEXT("WheelCentre"), 16);
		CentreInCanvas(
			Cast<UCanvasPanelSlot>(Canvas->AddChild(CentreText)), FVector2D::ZeroVector, FVector2D::ZeroVector);

		SegmentBoxes.Reset();
		SegmentNames.Reset();
		SegmentCounts.Reset();
		for (int32 QuiverSlot = 1; QuiverSlot <= HawkeyeQuiverSlotCount; ++QuiverSlot)
		{
			const FString Suffix = FString::FromInt(QuiverSlot);
			UBorder* Box =
				WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *(TEXT("WheelSegment") + Suffix));
			// A colour brush: UBorder's default brush has no resource and SetBrushColor alone draws nothing.
			Box->SetBrush(FSlateColorBrush(FLinearColor::White));
			Box->SetPadding(FMargin(6.f, 6.f));
			UVerticalBox* Stack =
				WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *(TEXT("WheelStack") + Suffix));
			if (UBorderSlot* StackSlot = Cast<UBorderSlot>(Box->AddChild(Stack)))
			{
				StackSlot->SetHorizontalAlignment(HAlign_Center);
				StackSlot->SetVerticalAlignment(VAlign_Center);
			}
			UTextBlock* Name = MakeText(WidgetTree, TEXT("WheelName") + Suffix, 13);
			UTextBlock* Count = MakeText(WidgetTree, TEXT("WheelCount") + Suffix, 15);
			for (UTextBlock* Line : {Name, Count})
			{
				if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(Line)))
				{
					LineSlot->SetHorizontalAlignment(HAlign_Center);
				}
			}
			const FVector2D Offset =
				UQuiverWheelMath::GetSegmentLabelOffset(QuiverSlot, HawkeyeQuiverSlotCount, SegmentRadius);
			CentreInCanvas(Cast<UCanvasPanelSlot>(Canvas->AddChild(Box)), Offset, FVector2D(SegmentWidth, 56.f));
			SegmentBoxes.Add(Box);
			SegmentNames.Add(Name);
			SegmentCounts.Add(Count);
		}
		SetVisibility(ESlateVisibility::Collapsed);
		RefreshSegments();
	}
	return Super::RebuildWidget();
}

void UHawkeyeQuiverWheelWidget::NativeDestruct()
{
	BindToInventory(nullptr);
	Super::NativeDestruct();
}

void UHawkeyeQuiverWheelWidget::BindToOwningPawn()
{
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	BindToInventory(Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr);
}

void UHawkeyeQuiverWheelWidget::BindToInventory(UInventoryComponent* Inventory)
{
	if (BoundInventory == Inventory)
	{
		return;
	}
	if (BoundInventory)
	{
		BoundInventory->OnInventoryChanged.RemoveDynamic(this, &UHawkeyeQuiverWheelWidget::HandleInventoryChanged);
	}
	BoundInventory = Inventory;
	if (BoundInventory)
	{
		BoundInventory->OnInventoryChanged.AddDynamic(this, &UHawkeyeQuiverWheelWidget::HandleInventoryChanged);
	}
	RefreshSegments();
}

void UHawkeyeQuiverWheelWidget::HandleInventoryChanged()
{
	RefreshSegments();
}

void UHawkeyeQuiverWheelWidget::SetWheelState(bool bOpen, int32 QuiverSlot)
{
	if (bOpen && !BoundInventory)
	{
		BindToOwningPawn();
	}
	bWheelOpen = bOpen;
	HighlightedSlot = bOpen ? QuiverSlot : 0;
	SetVisibility(bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	RefreshSegments();
}

FText UHawkeyeQuiverWheelWidget::GetSegmentNameText(int32 QuiverSlot) const
{
	const FHawkeyeQuiverSlot Entry = BoundInventory ? BoundInventory->GetArrowSlot(QuiverSlot) : FHawkeyeQuiverSlot();
	return Entry.IsEmpty() ? NSLOCTEXT("Hawkeye", "WheelEmptySlot", "--") : Entry.Arrow->GetShortNameOrDisplayName();
}

FText UHawkeyeQuiverWheelWidget::GetSegmentCountText(int32 QuiverSlot) const
{
	return BoundInventory ? BoundInventory->GetSlotCountText(QuiverSlot) : FText::GetEmpty();
}

FLinearColor UHawkeyeQuiverWheelWidget::GetSegmentColor(int32 QuiverSlot) const
{
	if (!BoundInventory || BoundInventory->IsArrowSlotEmpty(QuiverSlot))
	{
		return QuiverSlot == HighlightedSlot ? FLinearColor(0.25f, 0.1f, 0.4f, 0.5f) : EmptyColor;
	}
	if (QuiverSlot == HighlightedSlot)
	{
		return HighlightColor;
	}
	return BoundInventory->GetActiveArrowSlot() == QuiverSlot ? NockedColor : FilledColor;
}

FText UHawkeyeQuiverWheelWidget::GetCentreText() const
{
	if (!BoundInventory)
	{
		return FText::GetEmpty();
	}
	const int32 Shown = HighlightedSlot > 0 ? HighlightedSlot : BoundInventory->GetActiveArrowSlot();
	const FHawkeyeQuiverSlot Entry = BoundInventory->GetArrowSlot(Shown);
	return Entry.IsEmpty() ? NSLOCTEXT("Hawkeye", "WheelNoArrows", "None carried")
						   : Entry.Arrow->GetDisplayNameOrAssetName();
}

void UHawkeyeQuiverWheelWidget::RefreshSegments()
{
	for (int32 Index = 0; Index < SegmentBoxes.Num(); ++Index)
	{
		const int32 QuiverSlot = Index + 1;
		if (SegmentBoxes[Index])
		{
			SegmentBoxes[Index]->SetBrushColor(GetSegmentColor(QuiverSlot));
		}
		if (SegmentNames.IsValidIndex(Index) && SegmentNames[Index])
		{
			SegmentNames[Index]->SetText(GetSegmentNameText(QuiverSlot));
		}
		if (SegmentCounts.IsValidIndex(Index) && SegmentCounts[Index])
		{
			SegmentCounts[Index]->SetText(GetSegmentCountText(QuiverSlot));
		}
	}
	if (CentreText)
	{
		CentreText->SetText(GetCentreText());
	}
}
