#include "UI/HawkeyeMainMenuWidget.h"

#include "Audio/HawkeyeAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "UI/HawkeyeMenuLayout.h"

TSharedRef<SWidget> UHawkeyeMainMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// Nearly opaque: the district shows through just enough to say the game is already loaded.
		ButtonStack = HawkeyeMenuLayout::BuildFrame(WidgetTree, 0.85f);
		HawkeyeMenuLayout::AddText(WidgetTree, ButtonStack, TEXT("TitleText"),
			NSLOCTEXT("Hawkeye", "MainMenuTitle", "HAWKEYE"), 48, 4.f);
		HawkeyeMenuLayout::AddText(WidgetTree, ButtonStack, TEXT("SubtitleText"),
			NSLOCTEXT("Hawkeye", "MainMenuSubtitle", "Chapter 1: East Village"), 16, 36.f);
		HawkeyeMenuLayout::AddButton(WidgetTree, ButtonStack, ContinueButton, TEXT("ContinueButton"),
			NSLOCTEXT("Hawkeye", "MainMenuContinue", "Continue"));
		HawkeyeMenuLayout::AddButton(WidgetTree, ButtonStack, NewGameButton, TEXT("NewGameButton"),
			NSLOCTEXT("Hawkeye", "MainMenuNewGame", "New Game"));
		HawkeyeMenuLayout::AddButton(WidgetTree, ButtonStack, SettingsButton, TEXT("SettingsButton"),
			NSLOCTEXT("Hawkeye", "MainMenuSettings", "Settings"));
		HawkeyeMenuLayout::AddButton(WidgetTree, ButtonStack, QuitButton, TEXT("QuitButton"),
			NSLOCTEXT("Hawkeye", "MainMenuQuit", "Quit"));
	}
	TSharedRef<SWidget> Built = Super::RebuildWidget();
	SetContinueEnabled(bContinueEnabled);
	return Built;
}

void UHawkeyeMainMenuWidget::SetContinueEnabled(bool bEnabled)
{
	bContinueEnabled = bEnabled;
	if (ContinueButton)
	{
		ContinueButton->SetIsEnabled(bEnabled);
	}
}

void UHawkeyeMainMenuWidget::RefreshFromSave(const UHawkeyeSaveSubsystem* Save)
{
	SetContinueEnabled(Save && Save->HasSave());
}

void UHawkeyeMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (bBound)
	{
		return;
	}
	if (ContinueButton)
	{
		ContinueButton->OnClicked.AddDynamic(this, &UHawkeyeMainMenuWidget::HandleContinueClicked);
	}
	if (NewGameButton)
	{
		NewGameButton->OnClicked.AddDynamic(this, &UHawkeyeMainMenuWidget::HandleNewGameClicked);
	}
	if (SettingsButton)
	{
		SettingsButton->OnClicked.AddDynamic(this, &UHawkeyeMainMenuWidget::HandleSettingsClicked);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &UHawkeyeMainMenuWidget::HandleQuitClicked);
	}
	for (UButton* Button : { ContinueButton.Get(), NewGameButton.Get(), SettingsButton.Get(), QuitButton.Get() })
	{
		if (Button)
		{
			Button->OnHovered.AddUniqueDynamic(this, &UHawkeyeMainMenuWidget::HandleButtonHovered);
			Button->OnPressed.AddUniqueDynamic(this, &UHawkeyeMainMenuWidget::HandleButtonPressed);
		}
	}
	bBound = true;
}

void UHawkeyeMainMenuWidget::NativeDestruct()
{
	if (bBound)
	{
		if (ContinueButton)
		{
			ContinueButton->OnClicked.RemoveDynamic(this, &UHawkeyeMainMenuWidget::HandleContinueClicked);
		}
		if (NewGameButton)
		{
			NewGameButton->OnClicked.RemoveDynamic(this, &UHawkeyeMainMenuWidget::HandleNewGameClicked);
		}
		if (SettingsButton)
		{
			SettingsButton->OnClicked.RemoveDynamic(this, &UHawkeyeMainMenuWidget::HandleSettingsClicked);
		}
		if (QuitButton)
		{
			QuitButton->OnClicked.RemoveDynamic(this, &UHawkeyeMainMenuWidget::HandleQuitClicked);
		}
		bBound = false;
	}
	Super::NativeDestruct();
}

void UHawkeyeMainMenuWidget::HandleButtonHovered()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Hover);
}

void UHawkeyeMainMenuWidget::HandleButtonPressed()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
}

void UHawkeyeMainMenuWidget::HandleContinueClicked()
{
	if (bContinueEnabled)
	{
		OnContinueClicked.Broadcast();
	}
}

void UHawkeyeMainMenuWidget::HandleNewGameClicked()
{
	OnNewGameClicked.Broadcast();
}

void UHawkeyeMainMenuWidget::HandleSettingsClicked()
{
	OnSettingsClicked.Broadcast();
}

void UHawkeyeMainMenuWidget::HandleQuitClicked()
{
	OnQuitClicked.Broadcast();
}
