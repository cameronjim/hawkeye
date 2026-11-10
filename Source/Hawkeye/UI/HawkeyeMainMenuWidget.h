#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HawkeyeMainMenuWidget.generated.h"

class UButton;
class UHawkeyeSaveSubsystem;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMainMenuChoiceSignature);

/**
 * The main menu, drawn over the paused district on first boot and after "Quit to menu":
 * Continue (only when a save exists), New Game, Settings, Quit.
 *
 * Like the pause menu it only reports which button was pressed; AHawkeyePlayerController decides
 * what each means. Name buttons ContinueButton, NewGameButton, SettingsButton and QuitButton in a
 * Blueprint layout to have them driven; without one, RebuildWidget builds a plain stack.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Main menu")
	FOnMainMenuChoiceSignature OnContinueClicked;

	UPROPERTY(BlueprintAssignable, Category = "Main menu")
	FOnMainMenuChoiceSignature OnNewGameClicked;

	UPROPERTY(BlueprintAssignable, Category = "Main menu")
	FOnMainMenuChoiceSignature OnSettingsClicked;

	UPROPERTY(BlueprintAssignable, Category = "Main menu")
	FOnMainMenuChoiceSignature OnQuitClicked;

	/** Enables or greys out Continue. */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void SetContinueEnabled(bool bEnabled);

	/** Continue is enabled exactly when the save subsystem has a save in its slot. */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void RefreshFromSave(const UHawkeyeSaveSubsystem* Save);

	UFUNCTION(BlueprintPure, Category = "Main menu")
	bool IsContinueEnabled() const { return bContinueEnabled; }

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleContinueClicked();

	UFUNCTION()
	void HandleNewGameClicked();

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleQuitClicked();

	/** Any button under the cursor or pad focus: the hover blip. */
	UFUNCTION()
	void HandleButtonHovered();

	/** Any button pressed: the click. */
	UFUNCTION()
	void HandleButtonPressed();

	UPROPERTY(BlueprintReadOnly, Category = "Main menu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ContinueButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Main menu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NewGameButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Main menu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Main menu", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ButtonStack = nullptr;

private:
	bool bContinueEnabled = false;
	bool bBound = false;
};
