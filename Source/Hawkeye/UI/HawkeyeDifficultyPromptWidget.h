#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Settings/HawkeyeSettings.h"
#include "HawkeyeDifficultyPromptWidget.generated.h"

class UButton;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyChosenSignature, EHawkeyeDifficulty, Difficulty);

/**
 * Asked once at New Game, after the main menu: Story, Normal or Hard, each with a line saying what it
 * does, Normal focused. The choice goes to the settings (and can be changed there any time); the
 * widget only reports it, and AHawkeyePlayerController carries on with the new game.
 *
 * Name buttons StoryButton, NormalButton and HardButton in a Blueprint layout to have them driven;
 * without one, RebuildWidget builds a plain stack over a dimmer.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeDifficultyPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Difficulty")
	FOnDifficultyChosenSignature OnDifficultyChosen;

	/** Reports Difficulty as the choice, as a click on its button does. */
	UFUNCTION(BlueprintCallable, Category = "Difficulty")
	void Choose(EHawkeyeDifficulty Difficulty);

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleStoryClicked();

	UFUNCTION()
	void HandleNormalClicked();

	UFUNCTION()
	void HandleHardClicked();

	UFUNCTION()
	void HandleButtonHovered();

	/** A button and the line under it that says what the level does. */
	void AddChoice(TObjectPtr<UButton>& Button, const TCHAR* Name, EHawkeyeDifficulty Difficulty);

	UPROPERTY(BlueprintReadOnly, Category = "Difficulty", meta = (BindWidgetOptional))
	TObjectPtr<UButton> StoryButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Difficulty", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NormalButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Difficulty", meta = (BindWidgetOptional))
	TObjectPtr<UButton> HardButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ButtonStack = nullptr;

private:
	bool bBound = false;
};
