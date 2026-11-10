#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HawkeyeSettingsWidget.generated.h"

class UButton;
class UCheckBox;
class UHorizontalBox;
class USlider;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsBackSignature);

/**
 * The Settings screen, in two columns: controls and audio on the left, difficulty and accessibility on
 * the right, inside a scroll box so nothing is cut off at 1280x720. Every row is a label, a control and
 * (for sliders and choices) a live value, all driven straight into UHawkeyeSettingsSubsystem.
 *
 * Reparent a UMG widget to this class and name the parts after the properties below to have them
 * driven automatically. A subclass with no designer layout works too, because RebuildWidget builds one
 * (the same approach as UHawkeyePauseWidget).
 *
 * The widget owns no settings state. It reads the subsystem on open and writes it on change;
 * AHawkeyePlayerController decides what Back means.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UHawkeyeSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnSettingsBackSignature OnBackRequested;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FText TitleLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FText SensitivityLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FText StickSensitivityLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FText BackLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FText InvertMouseYLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings")
	FText InvertStickYLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings|Audio")
	FText MasterVolumeLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings|Audio")
	FText SfxVolumeLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings|Audio")
	FText AmbientVolumeLabel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Settings|Audio")
	FText MusicVolumeLabel;

	/** Pulls every control and number back in line with the subsystem. Called on construct. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void RefreshFromSettings();

	/** The text a choice row shows now ("Normal", "Large", "Deuteranopia"), for tests and the render check. */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FText GetDifficultyShown() const;

	/** What the time of day row shows now ("Night", "Day"). */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FText GetTimeOfDayShown() const;

	/** What the sprint row shows now ("Hold on keys, toggle on pad", "Hold", "Toggle"). */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FText GetSprintModeShown() const;

	/** The Hints row's value as shown: "On" or "Off". */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FText GetHintsShown() const;

	/** Rows the screen built, both columns together. */
	UFUNCTION(BlueprintPure, Category = "Settings")
	int32 GetRowCount() const { return RowCount; }

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget interface

	UFUNCTION()
	void HandleSensitivityChanged(float Value);

	UFUNCTION()
	void HandleStickSensitivityChanged(float Value);

	UFUNCTION()
	void HandleInvertMouseYChanged(bool bIsChecked);

	UFUNCTION()
	void HandleInvertStickYChanged(bool bIsChecked);

	UFUNCTION()
	void HandleBackClicked();

	UFUNCTION()
	void HandleBackHovered();

	UFUNCTION()
	void HandleMasterVolumeChanged(float Value);

	UFUNCTION()
	void HandleSfxVolumeChanged(float Value);

	UFUNCTION()
	void HandleAmbientVolumeChanged(float Value);

	UFUNCTION()
	void HandleMusicVolumeChanged(float Value);

	UFUNCTION()
	void HandleToggleAimChanged(bool bIsChecked);

	UFUNCTION()
	void HandleToggleCrouchChanged(bool bIsChecked);

	UFUNCTION()
	void HandleReduceShakeChanged(bool bIsChecked);

	UFUNCTION()
	void HandleReduceFlashingChanged(bool bIsChecked);

	UFUNCTION()
	void HandleSubtitleBackgroundChanged(float Value);

	UFUNCTION()
	void HandleHudScaleChanged(float Value);

	/** The choice rows step to the next value on a click, wrapping. */
	UFUNCTION()
	void HandleDifficultyClicked();

	UFUNCTION()
	void HandleSubtitleSizeClicked();

	UFUNCTION()
	void HandlePaletteClicked();

	UFUNCTION()
	void HandleTimeOfDayClicked();

	UFUNCTION()
	void HandleAimAssistClicked();

	UFUNCTION()
	void HandleSprintModeClicked();

	UFUNCTION()
	void HandleHintsClicked();

	/** Writes Value (0..1) into Text as a whole percentage. */
	static void UpdateVolumeText(UTextBlock* Text, float Value);

	/** Fills in any label the designer left empty. Called before the layout is built. */
	void ApplyDefaultLabels();

	/** Writes Value into SensitivityValueText as two decimals. */
	void UpdateValueText(float Value);

	/** Writes Value into StickSensitivityValueText as two decimals. */
	void UpdateStickValueText(float Value);

	/** Adds or removes every control's handler. */
	void BindControls(bool bBind);

	// --- Layout helpers (RebuildWidget only) -----------------------------------------------------

	/** A column heading: larger text, space above it. */
	void AddHeading(UVerticalBox* Column, const FText& Text, const TCHAR* Name);

	/** Label, slider, value on one row. */
	void AddSliderRow(UVerticalBox* Column, const FText& Label, TObjectPtr<USlider>& OutSlider,
		TObjectPtr<UTextBlock>& OutValue, TObjectPtr<UTextBlock>& OutLabel, const TCHAR* BaseName);

	/** A checkbox whose content is its label, so a click on the words toggles it. */
	void AddCheckRow(UVerticalBox* Column, const FText& Label, TObjectPtr<UCheckBox>& OutCheckBox,
		TObjectPtr<UTextBlock>& OutLabel, const TCHAR* BaseName);

	/** Label and a button showing the current choice; a click steps it on. */
	void AddChoiceRow(UVerticalBox* Column, const FText& Label, TObjectPtr<UButton>& OutButton,
		TObjectPtr<UTextBlock>& OutValue, const TCHAR* BaseName);

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<USlider> SensitivitySlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SensitivityValueText = nullptr;

	/** Second slider, "Controller sensitivity", directly under the mouse one. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<USlider> StickSensitivitySlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StickSensitivityValueText = nullptr;

	/** Checkbox under the sliders: "Invert mouse Y". */
	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> InvertMouseYCheckBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InvertMouseYLabelText = nullptr;

	/** Checkbox under the sliders: "Invert controller Y". */
	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> InvertStickYCheckBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> InvertStickYLabelText = nullptr;

	/** "Toggle aim" and "Toggle crouch": press to switch instead of hold. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ToggleAimCheckBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ToggleAimLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ToggleCrouchCheckBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ToggleCrouchLabelText = nullptr;

	/** Three volume rows: master, sound effects, ambience. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> MasterVolumeSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MasterVolumeValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MasterVolumeLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> SfxVolumeSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SfxVolumeValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SfxVolumeLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> AmbientVolumeSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AmbientVolumeValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AmbientVolumeLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<USlider> MusicVolumeSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MusicVolumeValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Audio", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MusicVolumeLabelText = nullptr;

	/** Difficulty: a button reading Story / Normal / Hard, and the line under it saying what that means. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings|Difficulty", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DifficultyButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Difficulty", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DifficultyValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Difficulty", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DifficultyBlurbText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SubtitleSizeButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleSizeValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<USlider> SubtitleBackgroundSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleBackgroundValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleBackgroundLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PaletteButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PaletteValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ReduceShakeCheckBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReduceShakeLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> ReduceFlashingCheckBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReduceFlashingLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<USlider> HudScaleSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HudScaleValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Accessibility", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HudScaleLabelText = nullptr;

	/** World: a button reading Night / Day. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings|World", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TimeOfDayButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|World", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimeOfDayValueText = nullptr;

	/** Controls: a button reading Off / Normal / Strong (the bow's aim assist). */
	UPROPERTY(BlueprintReadOnly, Category = "Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UButton> AimAssistButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AimAssistValueText = nullptr;

	/** Controls: a button reading "Hold on keys, toggle on pad" / Hold / Toggle (the sprint button). */
	UPROPERTY(BlueprintReadOnly, Category = "Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SprintModeButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SprintModeValueText = nullptr;

	/** Controls: a button reading On / Off (the first-time hints). */
	UPROPERTY(BlueprintReadOnly, Category = "Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UButton> HintsButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings|Controls", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HintsValueText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UButton> BackButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SensitivityLabelText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Settings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StickSensitivityLabelText = nullptr;

	/** The stack RebuildWidget builds when the Blueprint has no layout of its own. */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> OptionStack = nullptr;

private:
	bool bBound = false;
	int32 RowCount = 0;
};
