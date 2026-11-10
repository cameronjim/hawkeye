#include "UI/HawkeyeInventoryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HawkeyePlayerController.h"
#include "InputCoreTypes.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"

void UHawkeyeInventoryWidget::ApplyDefaultLabels()
{
	if (TitleLabel.IsEmpty())
	{
		TitleLabel = NSLOCTEXT("Hawkeye", "InventoryTitle", "Inventory");
	}
}

TSharedRef<SWidget> UHawkeyeInventoryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		ApplyDefaultLabels();

		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("InventoryRoot"));
		WidgetTree->RootWidget = Root;

		// A colour brush, not the default one: UBorder ships with a brush that has no image
		// resource, so a plain SetBrushColor draws nothing at all.
		Dimmer = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dimmer"));
		Dimmer->SetBrush(FSlateColorBrush(FLinearColor::White));
		Dimmer->SetBrushColor(DimmerColor);
		if (UOverlaySlot* DimmerSlot = Cast<UOverlaySlot>(Root->AddChild(Dimmer)))
		{
			DimmerSlot->SetHorizontalAlignment(HAlign_Fill);
			DimmerSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Stack"));
		if (UOverlaySlot* StackSlot = Cast<UOverlaySlot>(Root->AddChild(Stack)))
		{
			StackSlot->SetHorizontalAlignment(HAlign_Center);
			StackSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (!TitleText)
		{
			TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
		}
		TitleText->SetText(TitleLabel);
		if (UVerticalBoxSlot* TitleSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(TitleText)))
		{
			TitleSlot->SetHorizontalAlignment(HAlign_Center);
			TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
		}

		if (!RowBox)
		{
			RowBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowBox"));
		}
		if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(RowBox)))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Left);
		}

		if (!HintText)
		{
			HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HintText"));
		}
		if (UVerticalBoxSlot* HintSlot = Cast<UVerticalBoxSlot>(Stack->AddChild(HintText)))
		{
			HintSlot->SetHorizontalAlignment(HAlign_Center);
			HintSlot->SetPadding(FMargin(0.f, 24.f, 0.f, 0.f));
		}

		RefreshRows();
		RefreshHint();
	}

	return Super::RebuildWidget();
}

void UHawkeyeInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ApplyDefaultLabels();
	BindToOwningPawn();
	RefreshRows();
	RefreshHint();
	SetIsFocusable(true);
}

void UHawkeyeInventoryWidget::NativeDestruct()
{
	BindToInventory(nullptr);

	Super::NativeDestruct();
}

void UHawkeyeInventoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bWheelOpenedFromHold && TabHold.IsDown())
	{
		if (const UWorld* World = GetWorld())
		{
			if (TabHold.Tick(World->GetRealTimeSeconds()) == EHawkeyeTapHold::Hold)
			{
				EnterWheelFromHold();
			}
		}
	}

	RefreshHint();
}

FReply UHawkeyeInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// Escape and the pad's B close outright, same press, no second menu underneath.
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		RequestClose();
		return FReply::Handled();
	}

	// Tab (View on a pad, same IA_Inventory binding) is special: Slate treats an unhandled Tab
	// as "focus the next widget," which would swallow the key before the game ever saw it.
	// Claiming it here (tap closes, a hold opens the wheel) is what makes it do anything at all
	// while this screen has focus.
	if ((Key == EKeys::Tab || Key == EKeys::Gamepad_Special_Left) && !InKeyEvent.IsRepeat() && !TabHold.IsDown())
	{
		const UWorld* World = GetWorld();
		TabHold.HoldSeconds = GetQuiverWheelHoldSeconds();
		TabHold.Press(World ? World->GetRealTimeSeconds() : 0.0);
		return FReply::Handled();
	}

	// Everything else is swallowed: the game is paused (or wheel-slowed) under this screen.
	return FReply::Handled();
}

FReply UHawkeyeInventoryWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key != EKeys::Tab && Key != EKeys::Gamepad_Special_Left)
	{
		return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
	}

	const UWorld* World = GetWorld();
	const EHawkeyeTapHold Result = TabHold.Release(World ? World->GetRealTimeSeconds() : 0.0);

	if (bWheelOpenedFromHold)
	{
		// The hold already swapped us into the wheel; this is that same Tab finally coming up.
		FinishWheelFromHold();
	}
	else if (Result == EHawkeyeTapHold::Tap || Result == EHawkeyeTapHold::Hold)
	{
		RequestClose();
	}

	return FReply::Handled();
}

void UHawkeyeInventoryWidget::RequestClose()
{
	if (AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer()))
	{
		PC->SetInventoryOpen(false);
	}
}

void UHawkeyeInventoryWidget::EnterWheelFromHold()
{
	bWheelOpenedFromHold = true;

	// Hide this screen's own content; the wheel it is about to draw lives on the HUD beneath it.
	// The widget itself stays mounted and focused so the matching key-up still reaches us.
	if (Dimmer)
	{
		Dimmer->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (TitleText)
	{
		TitleText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (RowBox)
	{
		RowBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (HintText)
	{
		HintText->SetVisibility(ESlateVisibility::Collapsed);
	}

	AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer());
	if (PC)
	{
		// The wheel runs on time dilation, not a full pause.
		PC->SetPause(false);
	}
	if (AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr)
	{
		Kate->OpenQuiverWheel();
	}
}

void UHawkeyeInventoryWidget::FinishWheelFromHold()
{
	bWheelOpenedFromHold = false;

	AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer());
	if (AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr)
	{
		Kate->CloseQuiverWheel(/*bSelect=*/true);
	}
	if (PC)
	{
		PC->SetInventoryOpen(false);
	}
}

float UHawkeyeInventoryWidget::GetQuiverWheelHoldSeconds() const
{
	const APlayerController* PC = GetOwningPlayer();
	const AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	return Kate ? Kate->QuiverWheelHoldSeconds : 0.25f;
}

void UHawkeyeInventoryWidget::RefreshHint()
{
	if (!HintText)
	{
		return;
	}
	const AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer());
	HintText->SetText(PC && PC->IsUsingGamepad()
		? NSLOCTEXT("Hawkeye", "InventoryHintGamepad", "[View] Close   [B] Close")
		: NSLOCTEXT("Hawkeye", "InventoryHint", "[Tab] Close   [Esc] Close"));
}

void UHawkeyeInventoryWidget::BindToOwningPawn()
{
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	BindToInventory(Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr);
}

void UHawkeyeInventoryWidget::BindToInventory(UInventoryComponent* Inventory)
{
	if (BoundInventory == Inventory)
	{
		return;
	}

	if (BoundInventory)
	{
		BoundInventory->OnInventoryChanged.RemoveDynamic(this, &UHawkeyeInventoryWidget::HandleInventoryChanged);
	}

	BoundInventory = Inventory;

	if (BoundInventory)
	{
		BoundInventory->OnInventoryChanged.AddDynamic(this, &UHawkeyeInventoryWidget::HandleInventoryChanged);
	}

	RefreshRows();
}

void UHawkeyeInventoryWidget::HandleInventoryChanged()
{
	RefreshRows();
}

TArray<FText> UHawkeyeInventoryWidget::BuildInventoryLines() const
{
	TArray<FText> Lines;
	for (const FHawkeyeInventoryRow& Row : BuildInventoryRows())
	{
		Lines.Add(Row.Text);
	}
	return Lines;
}

TArray<FHawkeyeInventoryRow> UHawkeyeInventoryWidget::BuildInventoryRows() const
{
	TArray<FHawkeyeInventoryRow> Lines;
	if (!BoundInventory)
	{
		return Lines;
	}

	auto AddRow = [&Lines](const FText& Text, bool bHeading)
	{
		FHawkeyeInventoryRow Row;
		Row.Text = Text;
		Row.bHeading = bHeading;
		Lines.Add(Row);
	};

	AddRow(NSLOCTEXT("Hawkeye", "InventoryBow", "Bow"), true);

	const UBowDefinition* Bow = BoundInventory->GetBow();
	AddRow(Bow
		? (Bow->DisplayName.IsEmpty() ? FText::FromName(Bow->GetFName()) : Bow->DisplayName)
		: NSLOCTEXT("Hawkeye", "InventoryNoBow", "none (fists)"), false);

	AddRow(NSLOCTEXT("Hawkeye", "InventoryQuiver", "Quiver"), true);

	for (int32 QuiverSlot = 1; QuiverSlot <= HawkeyeQuiverSlotCount; ++QuiverSlot)
	{
		const FHawkeyeQuiverSlot Entry = BoundInventory->GetArrowSlot(QuiverSlot);
		if (Entry.IsEmpty())
		{
			continue;
		}
		AddRow(FText::FromString(UInventoryComponent::IsUnlimitedArrow(Entry.Arrow)
			? FString::Printf(TEXT("%d. %s  %s"), QuiverSlot, *Entry.Arrow->GetDisplayNameOrAssetName().ToString(),
				UInventoryComponent::UnlimitedGlyph)
			: FString::Printf(TEXT("%d. %s  %d / %d"), QuiverSlot, *Entry.Arrow->GetDisplayNameOrAssetName().ToString(),
				Entry.Count, BoundInventory->GetCap(Entry.Arrow))), false);
	}

	AddRow(NSLOCTEXT("Hawkeye", "InventoryKeycards", "Keycards"), true);

	TArray<FName> Keycards = BoundInventory->GetKeycards().Array();
	Keycards.Sort(FNameLexicalLess());
	if (Keycards.Num() == 0)
	{
		AddRow(NSLOCTEXT("Hawkeye", "InventoryNoKeycards", "none"), false);
	}
	for (const FName& Keycard : Keycards)
	{
		AddRow(FText::FromName(Keycard), false);
	}

	return Lines;
}

void UHawkeyeInventoryWidget::RefreshRows()
{
	if (!RowBox || !WidgetTree)
	{
		return;
	}

	RowBox->ClearChildren();

	int32 RowIndex = 0;
	for (const FHawkeyeInventoryRow& Row : BuildInventoryRows())
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("Row%d"), RowIndex++));
		Text->SetText(Row.Text);

		// Headings sit flush left in the HUD green; the things you are carrying are indented
		// white underneath them, so the screen reads as three lists rather than one block.
		if (Row.bHeading)
		{
			Text->SetColorAndOpacity(FSlateColor(HeadingColor));
		}

		if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(RowBox->AddChild(Text)))
		{
			LineSlot->SetHorizontalAlignment(HAlign_Left);
			LineSlot->SetPadding(Row.bHeading
				? FMargin(0.f, RowIndex > 1 ? 16.f : 0.f, 0.f, 4.f)
				: FMargin(20.f, 2.f, 0.f, 2.f));
		}
	}
}
