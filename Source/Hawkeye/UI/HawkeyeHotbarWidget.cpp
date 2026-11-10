#include "UI/HawkeyeHotbarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Combat/ArrowDefinition.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Player/InventoryComponent.h"

TSharedRef<SWidget> UHawkeyeHotbarWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		SlotRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotRow"));
		WidgetTree->RootWidget = SlotRow;

		SlotBoxes.Reset();
		SlotKeyTexts.Reset();
		SlotNameTexts.Reset();
		SlotCountTexts.Reset();

		for (int32 QuiverSlot = 1; QuiverSlot <= HawkeyeQuiverSlotCount; ++QuiverSlot)
		{
			const FString Suffix = FString::FromInt(QuiverSlot);

			UBorder* Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *(TEXT("SlotBox") + Suffix));
			// UBorder's default brush carries no image resource, so SetBrushColor on its own
			// paints nothing. A colour brush always draws, which is what a slot box needs.
			Box->SetBrush(FSlateColorBrush(FLinearColor::White));
			Box->SetPadding(FMargin(8.f, 5.f));

			// A size box so every slot is the same width whatever is written in it.
			USizeBox* Sizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(TEXT("SlotSizer") + Suffix));
			Sizer->SetMinDesiredWidth(SlotWidthPixels);
			if (UBorderSlot* BoxSlot = Cast<UBorderSlot>(Box->AddChild(Sizer)))
			{
				BoxSlot->SetHorizontalAlignment(HAlign_Fill);
				BoxSlot->SetVerticalAlignment(VAlign_Center);
			}

			UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *(TEXT("SlotStack") + Suffix));
			if (USizeBoxSlot* StackSlot = Cast<USizeBoxSlot>(Sizer->AddChild(Stack)))
			{
				StackSlot->SetHorizontalAlignment(HAlign_Center);
				StackSlot->SetVerticalAlignment(VAlign_Center);
			}

			auto AddLine = [this, Stack](TArray<TObjectPtr<UTextBlock>>& Into, const FString& Name, int32 FontSize)
			{
				UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *Name);
				FSlateFontInfo Font = Text->GetFont();
				Font.Size = FontSize;
				Text->SetFont(Font);
				if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(Text)))
				{
					LineSlot->SetHorizontalAlignment(HAlign_Center);
				}
				Into.Add(Text);
			};

			AddLine(SlotKeyTexts, TEXT("SlotKey") + Suffix, 10);
			AddLine(SlotNameTexts, TEXT("SlotName") + Suffix, 12);
			AddLine(SlotCountTexts, TEXT("SlotCount") + Suffix, 14);

			if (UHorizontalBoxSlot* RowSlot = Cast<UHorizontalBoxSlot>(SlotRow->AddChild(Box)))
			{
				RowSlot->SetPadding(FMargin(4.f, 0.f));
				RowSlot->SetVerticalAlignment(VAlign_Bottom);
			}

			SlotBoxes.Add(Box);
		}

		RefreshSlots();
	}

	return Super::RebuildWidget();
}

void UHawkeyeHotbarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BindToOwningPawn();
	RefreshSlots();
}

void UHawkeyeHotbarWidget::NativeDestruct()
{
	BindToInventory(nullptr);

	Super::NativeDestruct();
}

void UHawkeyeHotbarWidget::BindToOwningPawn()
{
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	BindToInventory(Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr);
}

void UHawkeyeHotbarWidget::BindToInventory(UInventoryComponent* Inventory)
{
	if (BoundInventory == Inventory)
	{
		return;
	}

	if (BoundInventory)
	{
		BoundInventory->OnInventoryChanged.RemoveDynamic(this, &UHawkeyeHotbarWidget::HandleInventoryChanged);
		BoundInventory->OnActiveArrowSlotChanged.RemoveDynamic(this, &UHawkeyeHotbarWidget::HandleActiveSlotChanged);
	}

	BoundInventory = Inventory;

	if (BoundInventory)
	{
		BoundInventory->OnInventoryChanged.AddDynamic(this, &UHawkeyeHotbarWidget::HandleInventoryChanged);
		BoundInventory->OnActiveArrowSlotChanged.AddDynamic(this, &UHawkeyeHotbarWidget::HandleActiveSlotChanged);
	}

	RefreshSlots();
}

void UHawkeyeHotbarWidget::HandleInventoryChanged()
{
	RefreshSlots();
}

void UHawkeyeHotbarWidget::HandleActiveSlotChanged(int32 /*OldSlot*/, int32 /*NewSlot*/)
{
	RefreshSlots();
}

int32 UHawkeyeHotbarWidget::GetActiveSlot() const
{
	return BoundInventory ? BoundInventory->GetActiveArrowSlot() : 1;
}

bool UHawkeyeHotbarWidget::IsSlotActive(int32 QuiverSlot) const
{
	return BoundInventory != nullptr && BoundInventory->GetActiveArrowSlot() == QuiverSlot;
}

bool UHawkeyeHotbarWidget::IsSlotEmpty(int32 QuiverSlot) const
{
	return !BoundInventory || BoundInventory->IsArrowSlotEmpty(QuiverSlot);
}

FText UHawkeyeHotbarWidget::GetSlotKeyText(int32 QuiverSlot)
{
	// Past the number keys (explosive, slot 7) the way in is holding Tab for the wheel.
	return QuiverSlot <= HawkeyeQuiverKeyedSlotCount ? FText::AsNumber(QuiverSlot)
		: NSLOCTEXT("Hawkeye", "HotbarWheelKey", "Tab");
}

FText UHawkeyeHotbarWidget::GetSlotNameText(int32 QuiverSlot) const
{
	if (!BoundInventory)
	{
		return FText::GetEmpty();
	}
	const FHawkeyeQuiverSlot Entry = BoundInventory->GetArrowSlot(QuiverSlot);
	return Entry.IsEmpty() ? NSLOCTEXT("Hawkeye", "HotbarEmptySlot", "--") : Entry.Arrow->GetShortNameOrDisplayName();
}

FText UHawkeyeHotbarWidget::GetSlotCountText(int32 QuiverSlot) const
{
	if (!BoundInventory)
	{
		return FText::GetEmpty();
	}
	// "30" for slot 1, "count/cap" for a trick arrow, the infinity for the grapple.
	return BoundInventory->GetSlotCountText(QuiverSlot);
}

void UHawkeyeHotbarWidget::SetAccentColor(const FLinearColor& Purple, bool bDesign)
{
	if (!bDesignColorKept)
	{
		DesignActiveSlotColor = ActiveSlotColor;
		bDesignColorKept = true;
	}
	ActiveSlotColor = bDesign ? DesignActiveSlotColor : FLinearColor(Purple.R * 0.55f, Purple.G * 0.55f, Purple.B * 0.55f, 0.9f);
	RefreshSlots();
}

FLinearColor UHawkeyeHotbarWidget::GetSlotColor(int32 QuiverSlot) const
{
	if (IsSlotEmpty(QuiverSlot))
	{
		return EmptySlotColor;
	}
	return IsSlotActive(QuiverSlot) ? ActiveSlotColor : FilledSlotColor;
}

void UHawkeyeHotbarWidget::RefreshSlots()
{
	for (int32 Index = 0; Index < HawkeyeQuiverSlotCount; ++Index)
	{
		const int32 QuiverSlot = Index + 1;
		if (SlotBoxes.IsValidIndex(Index) && SlotBoxes[Index])
		{
			SlotBoxes[Index]->SetBrushColor(GetSlotColor(QuiverSlot));
		}
		if (SlotKeyTexts.IsValidIndex(Index) && SlotKeyTexts[Index])
		{
			SlotKeyTexts[Index]->SetText(GetSlotKeyText(QuiverSlot));
		}
		if (SlotNameTexts.IsValidIndex(Index) && SlotNameTexts[Index])
		{
			SlotNameTexts[Index]->SetText(GetSlotNameText(QuiverSlot));
		}
		if (SlotCountTexts.IsValidIndex(Index) && SlotCountTexts[Index])
		{
			SlotCountTexts[Index]->SetText(GetSlotCountText(QuiverSlot));
		}
	}
}
