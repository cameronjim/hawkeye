#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HawkeyePauseWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPauseMenuChoiceSignature);

/**
 * The Escape menu: Resume, Settings, Replay flashbacks, Mark nearest safehouse, Mark nearest challenge, Restart mission
 * (the last save), Quit to menu, Quit to desktop.
 *
 * Reparent a UMG widget to this class and name the buttons ResumeButton, SettingsButton,
 * RestartMissionButton, QuitToMenuButton and QuitToDesktopButton to have them driven automatically. A subclass
 * with no designer layout works too, because RebuildWidget builds a vertical stack itself
 * (the same approach as UHawkeyeHudWidget and UFlashbackWidget).
 *
 * The widget only reports which button was pressed; AHawkeyePlayerController decides what each
 * one means, so the menu has no opinion about pausing, travel or quitting.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyePauseWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnResumeClicked;

	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnSettingsClicked;

	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnReplayFlashbacksClicked;

	/** "Mark nearest safehouse": a marker on the nearest safehouse, found or not. */
	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnMarkSafehouseClicked;

	/** "Mark nearest challenge": a marker on the nearest challenge pedestal. */
	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnMarkChallengeClicked;

	/** "Photo mode": the free camera (the pad's way in; F11 on keys). */
	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnPhotoModeClicked;

	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnRestartMissionClicked;

	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnQuitToMenuClicked;

	UPROPERTY(BlueprintAssignable, Category = "Pause")
	FOnPauseMenuChoiceSignature OnQuitToDesktopClicked;

	/** Label text, so the three buttons can be renamed without touching the layout code. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText TitleLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText ResumeLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText SettingsLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText ReplayFlashbacksLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText MarkSafehouseLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText MarkChallengeLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText PhotoModeLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText RestartMissionLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText QuitToMenuLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	FText QuitToDesktopLabel;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleResumeClicked();

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleReplayFlashbacksClicked();

	UFUNCTION()
	void HandleMarkSafehouseClicked();

	UFUNCTION()
	void HandleMarkChallengeClicked();

	UFUNCTION()
	void HandlePhotoModeClicked();

	UFUNCTION()
	void HandleRestartMissionClicked();

	UFUNCTION()
	void HandleQuitToMenuClicked();

	UFUNCTION()
	void HandleQuitToDesktopClicked();

	/** Any button under the cursor or pad focus: the hover blip. */
	UFUNCTION()
	void HandleButtonHovered();

	/** Any button pressed: the click. */
	UFUNCTION()
	void HandleButtonPressed();

	/** Fills in any label the designer left empty. Called before the layout is built. */
	void ApplyDefaultLabels();

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResumeButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ReplayFlashbacksButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> MarkSafehouseButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> MarkChallengeButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PhotoModeButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> RestartMissionButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitToMenuButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitToDesktopButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Pause", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText = nullptr;

	/** The stack RebuildWidget builds when the Blueprint has no layout of its own. */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ButtonStack = nullptr;

private:
	bool bBound = false;
};
