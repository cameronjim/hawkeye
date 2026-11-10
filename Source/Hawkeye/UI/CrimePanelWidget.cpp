#include "UI/CrimePanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeLoot.h"
#include "Crime/CrimeRules.h"
#include "Crime/CrimeSubsystem.h"
#include "Crime/CrimeTracker.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

TSharedRef<SWidget> UCrimePanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CrimePanel"));
		Panel->SetBrush(FSlateColorBrush(FLinearColor::White));
		Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
		Panel->SetPadding(FMargin(16.f, 8.f));
		WidgetTree->RootWidget = Panel;

		UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CrimeLines"));
		Panel->SetContent(Lines);
		auto AddLine = [this, Lines](const TCHAR* Name, int32 Size, const FLinearColor& Color)
		{
			UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			FSlateFontInfo Font = Line->GetFont();
			Font.Size = Size;
			Line->SetFont(Font);
			Line->SetColorAndOpacity(FSlateColor(Color));
			Line->SetShadowOffset(FVector2D(1.f, 1.f));
			Line->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
			if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Lines->AddChild(Line)))
			{
				LineSlot->SetHorizontalAlignment(HAlign_Right);
				LineSlot->SetPadding(FMargin(0.f, 1.f));
			}
			return Line;
		};
		TitleText = AddLine(TEXT("CrimeTitle"), 18, TitleColor);
		StatusText = AddLine(TEXT("CrimeStatus"), 13, TextColor);
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Super::RebuildWidget();
}

void UCrimePanelWidget::FormatLines(const UCrimeSubsystem* Crimes, const FVector& PlayerLocation, FText& OutTitle, FText& OutStatus)
{
	const UCrimeTracker* Tracker = Crimes ? Crimes->GetTracker() : nullptr;
	const UCrimeDefinition* Definition = Tracker ? Tracker->GetActiveDefinition() : nullptr;
	if (!Definition)
	{
		OutTitle = OutStatus = FText::GetEmpty();
		return;
	}
	OutTitle = UCrimeRules::FormatHudLine(Definition->GetDisplayName(), FVector::Dist(PlayerLocation, Crimes->GetCrimePoint()));
	const FText Down = FText::Format(NSLOCTEXT("Hawkeye", "CrimeDownLine", "Down {0}/{1}"), Crimes->CountThugsDown(),
		Tracker->GetThugsTotal());
	if (Definition->Type == ECrimeType::Mugging)
	{
		OutStatus = FText::Format(NSLOCTEXT("Hawkeye", "CrimeVictimLine", "{0}   Victim hit {1}/{2}"), Down, Tracker->GetVictimHits(),
			Definition->VictimHitsToFail);
		return;
	}
	const ACrimeLoot* Loot = Crimes->GetLoot();
	if (Definition->Type == ECrimeType::Robbery && Loot && Loot->IsCarried())
	{
		const int32 Metres = FMath::RoundToInt(FVector::Dist2D(Loot->GetActorLocation(), Crimes->GetEscapeLocation()) / 100.f);
		OutStatus = FText::Format(NSLOCTEXT("Hawkeye", "CrimeLootLine", "{0}   Loot {1} m from escape"), Down, Metres);
		return;
	}
	if (Definition->Type == ECrimeType::Robbery && Loot)
	{
		OutStatus = FText::Format(NSLOCTEXT("Hawkeye", "CrimeLootDropped", "{0}   Loot dropped"), Down);
		return;
	}
	OutStatus = Down;
}

void UCrimePanelWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
	Super::NativeTick(MyGeometry, DeltaSeconds);
	const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(this);
	const bool bOn = Crimes && Crimes->IsCrimeActive() && !Crimes->IsCrimePaused();
	if (bOn != bShown && Panel)
	{
		Panel->SetVisibility(bOn ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	bShown = bOn;
	if (!bOn || !TitleText)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	FText Title, Status;
	FormatLines(Crimes, Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector, Title, Status);
	TitleText->SetText(Title);
	StatusText->SetText(Status);
	TitleShown = Title;
	StatusShown = Status;
}
