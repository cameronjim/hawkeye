#include "UI/HawkeyeSettingsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "UI/HawkeyeMenuLayout.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeAccessibility.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "World/TimeOfDaySubsystem.h"
#include "Combat/AimAssist.h"
#include "Player/HawkeyeSprintToggle.h"

namespace HawkeyeSettingsWidgetLayout
{
	/** Step is 0.01, so the number under the slider never shows a third decimal. */
	static constexpr float SliderStep = 0.01f;

	/** Two columns this wide with a gap between them fit a 1080-unit-high screen (and so 720p). */
	static constexpr float ColumnWidth = 780.f;
	static constexpr float ColumnGap = 80.f;
	static constexpr float LabelWidth = 320.f;
	static constexpr float ValueWidth = 90.f;
	static constexpr float SliderHeight = 28.f;
	static constexpr float RowGap = 18.f;
	static constexpr int32 FontSize = 20;
	static constexpr int32 HeadingSize = 24;
	/** The checkboxes: 24 px, 12 px from their label. */
	static constexpr float CheckBoxSize = 24.f;
	static constexpr float CheckBoxLabelGap = 12.f;
	/** Kate purple, for the column headings. */
	static const FLinearColor HeadingColor(0.78f, 0.55f, 1.f, 1.f);

	/** Grows a text block's default font without needing a font asset of our own. */
	static void SetFontSize(UTextBlock* Text, int32 Size)
	{
		if (!Text)
		{
			return;
		}
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
	}

	/** A fixed-width row box in Column, and the horizontal box inside it. */
	static UHorizontalBox* AddRowBox(UWidgetTree* Tree, UVerticalBox* Column, const FString& Base)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Base + TEXT("RowBox")));
		Box->SetWidthOverride(ColumnWidth);
		if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Box)))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Left);
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
		}
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *(Base + TEXT("Row")));
		if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(Box->AddChild(Row)))
		{
			Inner->SetHorizontalAlignment(HAlign_Fill);
			Inner->SetVerticalAlignment(VAlign_Center);
		}
		return Row;
	}

	/** Text of FontSize in a LabelWidth box, added to Row. */
	static void AddLabel(UWidgetTree* Tree, UHorizontalBox* Row, TObjectPtr<UTextBlock>& OutLabel, const FText& Label,
		const FString& Base)
	{
		if (!OutLabel)
		{
			OutLabel = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(Base + TEXT("LabelText")));
		}
		OutLabel->SetText(Label);
		SetFontSize(OutLabel, FontSize);
		USizeBox* LabelBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Base + TEXT("LabelBox")));
		LabelBox->SetWidthOverride(LabelWidth);
		if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(LabelBox->AddChild(OutLabel)))
		{
			Inner->SetHorizontalAlignment(HAlign_Left);
			Inner->SetVerticalAlignment(VAlign_Center);
		}
		if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(LabelBox)))
		{
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	/** Right-aligned text of FontSize in a ValueWidth box, added to Row. */
	static void AddValue(UWidgetTree* Tree, UHorizontalBox* Row, TObjectPtr<UTextBlock>& OutValue, const FString& Base)
	{
		if (!OutValue)
		{
			OutValue = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(Base + TEXT("ValueText")));
		}
		SetFontSize(OutValue, FontSize);
		USizeBox* ValueBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Base + TEXT("ValueBox")));
		ValueBox->SetWidthOverride(ValueWidth);
		if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(ValueBox->AddChild(OutValue)))
		{
			Inner->SetHorizontalAlignment(HAlign_Right);
			Inner->SetVerticalAlignment(VAlign_Center);
		}
		if (UHorizontalBoxSlot* ValueSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(ValueBox)))
		{
			ValueSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	static void SetRange(USlider* Slider, float Min, float Max, float Step)
	{
		if (Slider)
		{
			Slider->SetMinValue(Min);
			Slider->SetMaxValue(Max);
			Slider->SetStepSize(Step);
		}
	}

	static void SetChecked(UCheckBox* Box, bool bChecked)
	{
		if (Box)
		{
			Box->SetIsChecked(bChecked);
		}
	}

	static void SetText(UTextBlock* Text, const FText& Value)
	{
		if (Text)
		{
			Text->SetText(Value);
		}
	}

	/** The next value of a three- or four-value enum, wrapping. */
	template <typename TEnum>
	static TEnum Next(TEnum Value, int32 Count)
	{
		return static_cast<TEnum>((static_cast<int32>(Value) + 1) % Count);
	}
}

void UHawkeyeSettingsWidget::ApplyDefaultLabels()
{
	if (TitleLabel.IsEmpty())
	{
		TitleLabel = NSLOCTEXT("Hawkeye", "SettingsTitle", "Settings");
	}
	if (SensitivityLabel.IsEmpty())
	{
		SensitivityLabel = NSLOCTEXT("Hawkeye", "SettingsSensitivity", "Mouse sensitivity");
	}
	if (StickSensitivityLabel.IsEmpty())
	{
		StickSensitivityLabel = NSLOCTEXT("Hawkeye", "SettingsStickSensitivity", "Controller sensitivity");
	}
	if (BackLabel.IsEmpty())
	{
		BackLabel = NSLOCTEXT("Hawkeye", "SettingsBack", "Back");
	}
	if (InvertMouseYLabel.IsEmpty())
	{
		InvertMouseYLabel = NSLOCTEXT("Hawkeye", "SettingsInvertMouseY", "Invert mouse Y");
	}
	if (InvertStickYLabel.IsEmpty())
	{
		InvertStickYLabel = NSLOCTEXT("Hawkeye", "SettingsInvertStickY", "Invert controller Y");
	}
	if (MasterVolumeLabel.IsEmpty())
	{
		MasterVolumeLabel = NSLOCTEXT("Hawkeye", "SettingsMasterVolume", "Master volume");
	}
	if (SfxVolumeLabel.IsEmpty())
	{
		SfxVolumeLabel = NSLOCTEXT("Hawkeye", "SettingsSfxVolume", "Sound effects");
	}
	if (AmbientVolumeLabel.IsEmpty())
	{
		AmbientVolumeLabel = NSLOCTEXT("Hawkeye", "SettingsAmbientVolume", "Ambience");
	}
	if (MusicVolumeLabel.IsEmpty())
	{
		MusicVolumeLabel = NSLOCTEXT("Hawkeye", "SettingsMusicVolume", "Music");
	}
}

void UHawkeyeSettingsWidget::AddHeading(UVerticalBox* Column, const FText& Text, const TCHAR* Name)
{
	using namespace HawkeyeSettingsWidgetLayout;
	UTextBlock* Heading = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Heading->SetText(Text);
	SetFontSize(Heading, HeadingSize);
	Heading->SetColorAndOpacity(FSlateColor(HeadingColor));
	if (UVerticalBoxSlot* HeadingSlot = Cast<UVerticalBoxSlot>(Column->AddChild(Heading)))
	{
		HeadingSlot->SetHorizontalAlignment(HAlign_Left);
		HeadingSlot->SetPadding(FMargin(0.f, Column->GetChildrenCount() > 1 ? 12.f : 0.f, 0.f, RowGap));
	}
}

void UHawkeyeSettingsWidget::AddSliderRow(UVerticalBox* Column, const FText& Label, TObjectPtr<USlider>& OutSlider,
	TObjectPtr<UTextBlock>& OutValue, TObjectPtr<UTextBlock>& OutLabel, const TCHAR* BaseName)
{
	using namespace HawkeyeSettingsWidgetLayout;
	const FString Base(BaseName);
	UHorizontalBox* Row = AddRowBox(WidgetTree, Column, Base);
	AddLabel(WidgetTree, Row, OutLabel, Label, Base);

	if (!OutSlider)
	{
		OutSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), *(Base + TEXT("Slider")));
	}
	USizeBox* SliderBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Base + TEXT("SliderBox")));
	SliderBox->SetHeightOverride(SliderHeight);
	if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(SliderBox->AddChild(OutSlider)))
	{
		Inner->SetHorizontalAlignment(HAlign_Fill);
		Inner->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* SliderSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(SliderBox)))
	{
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
		SliderSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
	}
	AddValue(WidgetTree, Row, OutValue, Base);
	++RowCount;
}

void UHawkeyeSettingsWidget::AddCheckRow(UVerticalBox* Column, const FText& Label, TObjectPtr<UCheckBox>& OutCheckBox,
	TObjectPtr<UTextBlock>& OutLabel, const TCHAR* BaseName)
{
	using namespace HawkeyeSettingsWidgetLayout;
	const FString Base(BaseName);
	UHorizontalBox* Row = AddRowBox(WidgetTree, Column, Base);
	if (!OutLabel)
	{
		OutLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(Base + TEXT("LabelText")));
	}
	OutLabel->SetText(Label);
	SetFontSize(OutLabel, FontSize);

	// The label is the checkbox's own content, so a click anywhere on the words toggles it, and the box is
	// drawn at CheckBoxSize px beside them instead of the engine's 16.
	if (!OutCheckBox)
	{
		OutCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), *(Base + TEXT("CheckBox")));
	}
	OutCheckBox->SetWidgetStyle(HawkeyeMenuLayout::MakeCheckBoxStyle(OutCheckBox->GetWidgetStyle(), CheckBoxSize,
		CheckBoxLabelGap));
	USizeBox* LabelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Base + TEXT("LabelBox")));
	LabelBox->SetWidthOverride(ColumnWidth - CheckBoxSize - CheckBoxLabelGap);
	LabelBox->SetMinDesiredHeight(CheckBoxSize);
	if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(LabelBox->AddChild(OutLabel)))
	{
		Inner->SetHorizontalAlignment(HAlign_Left);
		Inner->SetVerticalAlignment(VAlign_Center);
	}
	OutCheckBox->SetContent(LabelBox);
	if (UHorizontalBoxSlot* CheckSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(OutCheckBox)))
	{
		CheckSlot->SetVerticalAlignment(VAlign_Center);
	}
	++RowCount;
}

void UHawkeyeSettingsWidget::AddChoiceRow(UVerticalBox* Column, const FText& Label, TObjectPtr<UButton>& OutButton,
	TObjectPtr<UTextBlock>& OutValue, const TCHAR* BaseName)
{
	using namespace HawkeyeSettingsWidgetLayout;
	const FString Base(BaseName);
	UHorizontalBox* Row = AddRowBox(WidgetTree, Column, Base);
	TObjectPtr<UTextBlock> LabelText = nullptr;
	AddLabel(WidgetTree, Row, LabelText, Label, Base);

	if (!OutButton)
	{
		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *(Base + TEXT("Button")));
	}
	if (!OutValue)
	{
		OutValue = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(Base + TEXT("ValueText")));
	}
	SetFontSize(OutValue, FontSize);
	OutValue->SetColorAndOpacity(FSlateColor(FLinearColor(0.08f, 0.08f, 0.1f, 1.f)));
	if (UButtonSlot* ValueSlot = Cast<UButtonSlot>(OutButton->AddChild(OutValue)))
	{
		ValueSlot->SetHorizontalAlignment(HAlign_Center);
		ValueSlot->SetPadding(FMargin(24.f, 4.f));
	}
	if (UHorizontalBoxSlot* ButtonSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(OutButton)))
	{
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}
	++RowCount;
}

TSharedRef<SWidget> UHawkeyeSettingsWidget::RebuildWidget()
{
	using namespace HawkeyeSettingsWidgetLayout;

	// WBP_Settings is generated by a script and has no designer layout, so build one here.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		ApplyDefaultLabels();
		RowCount = 0;

		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SettingsRoot"));
		WidgetTree->RootWidget = Root;

		// Darker than the pause menu's dimmer: the settings screen replaces it rather than sitting on
		// top, so the frozen game should read as further away still. The brush is set explicitly because
		// UBorder's default Background draws nothing however it is tinted.
		UBorder* Dimmer = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dimmer"));
		Dimmer->SetBrush(FSlateColorBrush(FLinearColor::White));
		Dimmer->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		if (UOverlaySlot* DimmerSlot = Cast<UOverlaySlot>(Root->AddChild(Dimmer)))
		{
			DimmerSlot->SetHorizontalAlignment(HAlign_Fill);
			DimmerSlot->SetVerticalAlignment(VAlign_Fill);
		}

		// A scroll box, so a smaller screen scrolls rather than cutting the bottom rows off.
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SettingsScroll"));
		if (UOverlaySlot* ScrollSlot = Cast<UOverlaySlot>(Root->AddChild(Scroll)))
		{
			ScrollSlot->SetHorizontalAlignment(HAlign_Center);
			ScrollSlot->SetVerticalAlignment(VAlign_Center);
			ScrollSlot->SetPadding(FMargin(0.f, 40.f));
		}

		OptionStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OptionStack"));
		if (UScrollBoxSlot* StackSlot = Cast<UScrollBoxSlot>(Scroll->AddChild(OptionStack)))
		{
			StackSlot->SetHorizontalAlignment(HAlign_Center);
		}

		if (!TitleText)
		{
			TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
		}
		TitleText->SetText(TitleLabel);
		SetFontSize(TitleText, 36);
		if (UVerticalBoxSlot* TitleSlot = Cast<UVerticalBoxSlot>(OptionStack->AddChild(TitleText)))
		{
			TitleSlot->SetHorizontalAlignment(HAlign_Center);
			TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 32.f));
		}

		UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Columns"));
		if (UVerticalBoxSlot* ColumnsSlot = Cast<UVerticalBoxSlot>(OptionStack->AddChild(Columns)))
		{
			ColumnsSlot->SetHorizontalAlignment(HAlign_Center);
			ColumnsSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
		}
		UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LeftColumn"));
		UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RightColumn"));
		if (UHorizontalBoxSlot* LeftSlot = Cast<UHorizontalBoxSlot>(Columns->AddChild(Left)))
		{
			LeftSlot->SetPadding(FMargin(0.f, 0.f, ColumnGap, 0.f));
		}
		Columns->AddChild(Right);

		// --- Left: controls and audio -----------------------------------------------------------------
		AddHeading(Left, NSLOCTEXT("Hawkeye", "SettingsControls", "Controls"), TEXT("ControlsHeading"));
		AddSliderRow(Left, SensitivityLabel, SensitivitySlider, SensitivityValueText, SensitivityLabelText, TEXT("Sensitivity"));
		AddSliderRow(Left, StickSensitivityLabel, StickSensitivitySlider, StickSensitivityValueText, StickSensitivityLabelText,
			TEXT("StickSensitivity"));
		AddCheckRow(Left, InvertMouseYLabel, InvertMouseYCheckBox, InvertMouseYLabelText, TEXT("InvertMouseY"));
		AddCheckRow(Left, InvertStickYLabel, InvertStickYCheckBox, InvertStickYLabelText, TEXT("InvertStickY"));
		AddCheckRow(Left, NSLOCTEXT("Hawkeye", "SettingsToggleAim", "Toggle aim (press, not hold)"), ToggleAimCheckBox,
			ToggleAimLabelText, TEXT("ToggleAim"));
		AddCheckRow(Left, NSLOCTEXT("Hawkeye", "SettingsToggleCrouch", "Toggle crouch (press, not hold)"), ToggleCrouchCheckBox,
			ToggleCrouchLabelText, TEXT("ToggleCrouch"));
		AddChoiceRow(Left, NSLOCTEXT("Hawkeye", "SettingsAimAssist", "Aim assist (bow)"), AimAssistButton, AimAssistValueText,
			TEXT("AimAssist"));
		AddChoiceRow(Left, NSLOCTEXT("Hawkeye", "SettingsSprintMode", "Sprint"), SprintModeButton, SprintModeValueText,
			TEXT("SprintMode"));
		AddChoiceRow(Left, NSLOCTEXT("Hawkeye", "SettingsHints", "Hints"), HintsButton, HintsValueText, TEXT("Hints"));
		AddHeading(Left, NSLOCTEXT("Hawkeye", "SettingsAudio", "Audio"), TEXT("AudioHeading"));
		AddSliderRow(Left, MasterVolumeLabel, MasterVolumeSlider, MasterVolumeValueText, MasterVolumeLabelText, TEXT("MasterVolume"));
		AddSliderRow(Left, SfxVolumeLabel, SfxVolumeSlider, SfxVolumeValueText, SfxVolumeLabelText, TEXT("SfxVolume"));
		AddSliderRow(Left, AmbientVolumeLabel, AmbientVolumeSlider, AmbientVolumeValueText, AmbientVolumeLabelText,
			TEXT("AmbientVolume"));
		AddSliderRow(Left, MusicVolumeLabel, MusicVolumeSlider, MusicVolumeValueText, MusicVolumeLabelText, TEXT("MusicVolume"));

		// --- Right: difficulty and accessibility --------------------------------------------------------
		AddHeading(Right, NSLOCTEXT("Hawkeye", "SettingsDifficultyHeading", "Difficulty"), TEXT("DifficultyHeading"));
		AddChoiceRow(Right, NSLOCTEXT("Hawkeye", "SettingsDifficulty", "Difficulty"), DifficultyButton, DifficultyValueText,
			TEXT("Difficulty"));
		if (!DifficultyBlurbText)
		{
			DifficultyBlurbText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DifficultyBlurbText"));
		}
		SetFontSize(DifficultyBlurbText, 16);
		DifficultyBlurbText->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.85f, 1.f)));
		DifficultyBlurbText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* BlurbSlot = Cast<UVerticalBoxSlot>(Right->AddChild(DifficultyBlurbText)))
		{
			BlurbSlot->SetPadding(FMargin(0.f, 0.f, 0.f, RowGap));
		}
		AddHeading(Right, NSLOCTEXT("Hawkeye", "SettingsAccessibility", "Accessibility"), TEXT("AccessibilityHeading"));
		AddChoiceRow(Right, NSLOCTEXT("Hawkeye", "SettingsSubtitleSize", "Subtitle size"), SubtitleSizeButton,
			SubtitleSizeValueText, TEXT("SubtitleSize"));
		AddSliderRow(Right, NSLOCTEXT("Hawkeye", "SettingsSubtitleBack", "Subtitle background"), SubtitleBackgroundSlider,
			SubtitleBackgroundValueText, SubtitleBackgroundLabelText, TEXT("SubtitleBackground"));
		AddChoiceRow(Right, NSLOCTEXT("Hawkeye", "SettingsPalette", "Colour palette"), PaletteButton, PaletteValueText,
			TEXT("Palette"));
		AddCheckRow(Right, NSLOCTEXT("Hawkeye", "SettingsReduceShake", "Reduce camera shake"), ReduceShakeCheckBox,
			ReduceShakeLabelText, TEXT("ReduceShake"));
		AddCheckRow(Right, NSLOCTEXT("Hawkeye", "SettingsReduceFlashing", "Reduce flashing"), ReduceFlashingCheckBox,
			ReduceFlashingLabelText, TEXT("ReduceFlashing"));
		AddSliderRow(Right, NSLOCTEXT("Hawkeye", "SettingsHudScale", "HUD scale"), HudScaleSlider, HudScaleValueText,
			HudScaleLabelText, TEXT("HudScale"));
		AddHeading(Right, NSLOCTEXT("Hawkeye", "SettingsWorld", "World"), TEXT("WorldHeading"));
		AddChoiceRow(Right, NSLOCTEXT("Hawkeye", "SettingsTimeOfDay", "Time of day"), TimeOfDayButton, TimeOfDayValueText,
			TEXT("TimeOfDay"));

		// --- Back ---------------------------------------------------------------------------------------
		if (!BackButton)
		{
			BackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BackButton"));
		}
		UTextBlock* BackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BackButtonLabel"));
		BackText->SetText(BackLabel);
		SetFontSize(BackText, FontSize);
		if (UButtonSlot* BackTextSlot = Cast<UButtonSlot>(BackButton->AddChild(BackText)))
		{
			BackTextSlot->SetHorizontalAlignment(HAlign_Center);
			BackTextSlot->SetPadding(FMargin(48.f, 12.f));
		}
		if (UVerticalBoxSlot* BackSlot = Cast<UVerticalBoxSlot>(OptionStack->AddChild(BackButton)))
		{
			BackSlot->SetHorizontalAlignment(HAlign_Center);
		}
	}

	return Super::RebuildWidget();
}

void UHawkeyeSettingsWidget::NativeConstruct()
{
	using namespace HawkeyeSettingsWidgetLayout;
	Super::NativeConstruct();

	ApplyDefaultLabels();

	// The ranges are the subsystem's, not the designer's: a slider that can ask for a value the subsystem
	// clamps away would snap back under the player's thumb.
	SetRange(SensitivitySlider, UHawkeyeSettingsSubsystem::MinLookSensitivity, UHawkeyeSettingsSubsystem::MaxLookSensitivity,
		SliderStep);
	SetRange(StickSensitivitySlider, UHawkeyeSettingsSubsystem::MinStickSensitivity,
		UHawkeyeSettingsSubsystem::MaxStickSensitivity, SliderStep);
	for (USlider* Volume : { MasterVolumeSlider.Get(), SfxVolumeSlider.Get(), AmbientVolumeSlider.Get(), MusicVolumeSlider.Get() })
	{
		SetRange(Volume, 0.f, 1.f, SliderStep);
	}
	SetRange(SubtitleBackgroundSlider, 0.f, 1.f, 0.05f);
	SetRange(HudScaleSlider, UHawkeyeSettingsSubsystem::MinHudScale, UHawkeyeSettingsSubsystem::MaxHudScale, 0.05f);

	if (!bBound)
	{
		BindControls(true);
		bBound = true;
	}

	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::NativeDestruct()
{
	if (bBound)
	{
		BindControls(false);
		bBound = false;
	}

	Super::NativeDestruct();
}

void UHawkeyeSettingsWidget::BindControls(bool bBind)
{
	// One list for both directions, so a new control cannot be bound and never unbound.
#define HAWKEYE_BIND(Widget, Event, Handler) \
	if (Widget) \
	{ \
		if (bBind) \
		{ \
			Widget->Event.AddUniqueDynamic(this, &UHawkeyeSettingsWidget::Handler); \
		} \
		else \
		{ \
			Widget->Event.RemoveDynamic(this, &UHawkeyeSettingsWidget::Handler); \
		} \
	}

	HAWKEYE_BIND(SensitivitySlider, OnValueChanged, HandleSensitivityChanged)
	HAWKEYE_BIND(StickSensitivitySlider, OnValueChanged, HandleStickSensitivityChanged)
	HAWKEYE_BIND(InvertMouseYCheckBox, OnCheckStateChanged, HandleInvertMouseYChanged)
	HAWKEYE_BIND(InvertStickYCheckBox, OnCheckStateChanged, HandleInvertStickYChanged)
	HAWKEYE_BIND(ToggleAimCheckBox, OnCheckStateChanged, HandleToggleAimChanged)
	HAWKEYE_BIND(ToggleCrouchCheckBox, OnCheckStateChanged, HandleToggleCrouchChanged)
	HAWKEYE_BIND(MasterVolumeSlider, OnValueChanged, HandleMasterVolumeChanged)
	HAWKEYE_BIND(SfxVolumeSlider, OnValueChanged, HandleSfxVolumeChanged)
	HAWKEYE_BIND(AmbientVolumeSlider, OnValueChanged, HandleAmbientVolumeChanged)
	HAWKEYE_BIND(MusicVolumeSlider, OnValueChanged, HandleMusicVolumeChanged)
	HAWKEYE_BIND(DifficultyButton, OnClicked, HandleDifficultyClicked)
	HAWKEYE_BIND(SubtitleSizeButton, OnClicked, HandleSubtitleSizeClicked)
	HAWKEYE_BIND(SubtitleBackgroundSlider, OnValueChanged, HandleSubtitleBackgroundChanged)
	HAWKEYE_BIND(PaletteButton, OnClicked, HandlePaletteClicked)
	HAWKEYE_BIND(TimeOfDayButton, OnClicked, HandleTimeOfDayClicked)
	HAWKEYE_BIND(AimAssistButton, OnClicked, HandleAimAssistClicked)
	HAWKEYE_BIND(SprintModeButton, OnClicked, HandleSprintModeClicked)
	HAWKEYE_BIND(HintsButton, OnClicked, HandleHintsClicked)
	HAWKEYE_BIND(ReduceShakeCheckBox, OnCheckStateChanged, HandleReduceShakeChanged)
	HAWKEYE_BIND(ReduceFlashingCheckBox, OnCheckStateChanged, HandleReduceFlashingChanged)
	HAWKEYE_BIND(HudScaleSlider, OnValueChanged, HandleHudScaleChanged)
	HAWKEYE_BIND(BackButton, OnClicked, HandleBackClicked)
	HAWKEYE_BIND(BackButton, OnHovered, HandleBackHovered)
#undef HAWKEYE_BIND
}

void UHawkeyeSettingsWidget::RefreshFromSettings()
{
	using namespace HawkeyeSettingsWidgetLayout;
	const FHawkeyeSettings Current = UHawkeyeSettingsSubsystem::GetCurrentSettings(this);

	if (SensitivitySlider)
	{
		SensitivitySlider->SetValue(Current.LookSensitivity);
	}
	UpdateValueText(Current.LookSensitivity);
	if (StickSensitivitySlider)
	{
		StickSensitivitySlider->SetValue(Current.StickSensitivity);
	}
	UpdateStickValueText(Current.StickSensitivity);

	SetChecked(InvertMouseYCheckBox, Current.bInvertMouseY);
	SetChecked(InvertStickYCheckBox, Current.bInvertStickY);
	SetChecked(ToggleAimCheckBox, Current.bToggleAim);
	SetChecked(ToggleCrouchCheckBox, Current.bToggleCrouch);
	SetChecked(ReduceShakeCheckBox, Current.bReduceCameraShake);
	SetChecked(ReduceFlashingCheckBox, Current.bReduceFlashing);

	const TTuple<USlider*, UTextBlock*, float> Percentages[] = {
		{ MasterVolumeSlider.Get(), MasterVolumeValueText.Get(), Current.MasterVolume },
		{ SfxVolumeSlider.Get(), SfxVolumeValueText.Get(), Current.SfxVolume },
		{ AmbientVolumeSlider.Get(), AmbientVolumeValueText.Get(), Current.AmbientVolume },
		{ MusicVolumeSlider.Get(), MusicVolumeValueText.Get(), Current.MusicVolume },
		{ SubtitleBackgroundSlider.Get(), SubtitleBackgroundValueText.Get(), Current.SubtitleBackgroundOpacity },
	};
	for (const TTuple<USlider*, UTextBlock*, float>& Row : Percentages)
	{
		if (Row.Get<0>())
		{
			Row.Get<0>()->SetValue(Row.Get<2>());
		}
		UpdateVolumeText(Row.Get<1>(), Row.Get<2>());
	}
	if (HudScaleSlider)
	{
		HudScaleSlider->SetValue(Current.HudScale);
	}
	UpdateVolumeText(HudScaleValueText, Current.HudScale);

	SetText(DifficultyValueText, UDifficultySubsystem::GetDifficultyName(Current.Difficulty));
	SetText(DifficultyBlurbText, UDifficultySubsystem::GetDifficultyBlurb(Current.Difficulty));
	SetText(SubtitleSizeValueText, FText::Format(NSLOCTEXT("Hawkeye", "SettingsSubtitleSizeValue", "{0} ({1} px)"),
		UHawkeyeAccessibility::GetSubtitleSizeName(Current.SubtitleSize),
		FText::AsNumber(UHawkeyeAccessibility::GetSubtitleFontSize(Current.SubtitleSize))));
	SetText(PaletteValueText, UHawkeyeAccessibility::GetPaletteName(Current.ColorPalette));
	SetText(TimeOfDayValueText, UTimeOfDaySubsystem::GetTimeOfDayName(Current.TimeOfDay));
	SetText(AimAssistValueText, UHawkeyeAimAssist::GetLevelName(Current.AimAssist));
	SetText(SprintModeValueText, FHawkeyeSprintToggle::GetModeName(Current.SprintMode));
	SetText(HintsValueText, Current.bShowHints ? NSLOCTEXT("Hawkeye", "SettingsHintsOn", "On")
		: NSLOCTEXT("Hawkeye", "SettingsHintsOff", "Off"));
}

FText UHawkeyeSettingsWidget::GetDifficultyShown() const
{
	return DifficultyValueText ? DifficultyValueText->GetText() : FText::GetEmpty();
}

FText UHawkeyeSettingsWidget::GetTimeOfDayShown() const
{
	return TimeOfDayValueText ? TimeOfDayValueText->GetText() : FText::GetEmpty();
}

FText UHawkeyeSettingsWidget::GetSprintModeShown() const
{
	return SprintModeValueText ? SprintModeValueText->GetText() : FText::GetEmpty();
}

FText UHawkeyeSettingsWidget::GetHintsShown() const
{
	return HintsValueText ? HintsValueText->GetText() : FText::GetEmpty();
}

void UHawkeyeSettingsWidget::UpdateVolumeText(UTextBlock* Text, float Value)
{
	if (Text)
	{
		Text->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.f))));
	}
}

void UHawkeyeSettingsWidget::HandleMasterVolumeChanged(float Value)
{
	UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this);
	if (SettingsSubsystem)
	{
		SettingsSubsystem->SetMasterVolume(Value);
	}
	UpdateVolumeText(MasterVolumeValueText,
		SettingsSubsystem ? SettingsSubsystem->GetMasterVolume() : UHawkeyeSettingsSubsystem::ClampVolume(Value));
}

void UHawkeyeSettingsWidget::HandleSfxVolumeChanged(float Value)
{
	UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this);
	if (SettingsSubsystem)
	{
		SettingsSubsystem->SetSfxVolume(Value);
	}
	UpdateVolumeText(SfxVolumeValueText,
		SettingsSubsystem ? SettingsSubsystem->GetSfxVolume() : UHawkeyeSettingsSubsystem::ClampVolume(Value));
}

void UHawkeyeSettingsWidget::HandleAmbientVolumeChanged(float Value)
{
	UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this);
	if (SettingsSubsystem)
	{
		SettingsSubsystem->SetAmbientVolume(Value);
	}
	UpdateVolumeText(AmbientVolumeValueText,
		SettingsSubsystem ? SettingsSubsystem->GetAmbientVolume() : UHawkeyeSettingsSubsystem::ClampVolume(Value));
}

void UHawkeyeSettingsWidget::HandleMusicVolumeChanged(float Value)
{
	UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this);
	if (SettingsSubsystem)
	{
		SettingsSubsystem->SetMusicVolume(Value);
	}
	UpdateVolumeText(MusicVolumeValueText,
		SettingsSubsystem ? SettingsSubsystem->GetMusicVolume() : UHawkeyeSettingsSubsystem::ClampVolume(Value));
}

void UHawkeyeSettingsWidget::UpdateValueText(float Value)
{
	if (SensitivityValueText)
	{
		SensitivityValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Value)));
	}
}

void UHawkeyeSettingsWidget::UpdateStickValueText(float Value)
{
	if (StickSensitivityValueText)
	{
		StickSensitivityValueText->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), Value)));
	}
}

void UHawkeyeSettingsWidget::HandleSensitivityChanged(float Value)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetLookSensitivity(Value);
		UpdateValueText(SettingsSubsystem->GetLookSensitivity());
		return;
	}

	UpdateValueText(UHawkeyeSettingsSubsystem::ClampLookSensitivity(Value));
}

void UHawkeyeSettingsWidget::HandleStickSensitivityChanged(float Value)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetStickSensitivity(Value);
		UpdateStickValueText(SettingsSubsystem->GetStickSensitivity());
		return;
	}

	UpdateStickValueText(UHawkeyeSettingsSubsystem::ClampStickSensitivity(Value));
}

void UHawkeyeSettingsWidget::HandleInvertMouseYChanged(bool bIsChecked)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetInvertMouseY(bIsChecked);
	}
}

void UHawkeyeSettingsWidget::HandleInvertStickYChanged(bool bIsChecked)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetInvertStickY(bIsChecked);
	}
}

void UHawkeyeSettingsWidget::HandleToggleAimChanged(bool bIsChecked)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetToggleAim(bIsChecked);
	}
}

void UHawkeyeSettingsWidget::HandleToggleCrouchChanged(bool bIsChecked)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetToggleCrouch(bIsChecked);
	}
}

void UHawkeyeSettingsWidget::HandleReduceShakeChanged(bool bIsChecked)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetReduceCameraShake(bIsChecked);
	}
}

void UHawkeyeSettingsWidget::HandleReduceFlashingChanged(bool bIsChecked)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetReduceFlashing(bIsChecked);
	}
}

void UHawkeyeSettingsWidget::HandleSubtitleBackgroundChanged(float Value)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetSubtitleBackgroundOpacity(Value);
	}
	UpdateVolumeText(SubtitleBackgroundValueText, UHawkeyeSettingsSubsystem::ClampVolume(Value));
}

void UHawkeyeSettingsWidget::HandleHudScaleChanged(float Value)
{
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetHudScale(Value);
	}
	UpdateVolumeText(HudScaleValueText, UHawkeyeSettingsSubsystem::ClampHudScale(Value));
}

void UHawkeyeSettingsWidget::HandleDifficultyClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		// Steps the stored choice; a -Difficulty= override still wins while it is on the command line.
		SettingsSubsystem->SetDifficulty(HawkeyeSettingsWidgetLayout::Next(SettingsSubsystem->GetStoredSettings().Difficulty, 3));
	}
	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::HandleSubtitleSizeClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetSubtitleSize(HawkeyeSettingsWidgetLayout::Next(SettingsSubsystem->GetSettings().SubtitleSize, 3));
	}
	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::HandlePaletteClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetColorPalette(HawkeyeSettingsWidgetLayout::Next(SettingsSubsystem->GetSettings().ColorPalette, 4));
	}
	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::HandleAimAssistClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetAimAssist(HawkeyeSettingsWidgetLayout::Next(SettingsSubsystem->GetSettings().AimAssist, 3));
	}
	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::HandleSprintModeClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetSprintMode(HawkeyeSettingsWidgetLayout::Next(SettingsSubsystem->GetSettings().SprintMode, 3));
	}
	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::HandleHintsClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		SettingsSubsystem->SetShowHints(!SettingsSubsystem->GetSettings().bShowHints);
	}
	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::HandleTimeOfDayClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	if (UHawkeyeSettingsSubsystem* SettingsSubsystem = UHawkeyeSettingsSubsystem::Get(this))
	{
		// Steps from what the player sees, and drops a hawkeye.TimeOfDay console override so the pick shows
		// at once; a -TimeOfDay= on the command line still wins while it is there.
		const EHawkeyeTimeOfDay Next = HawkeyeSettingsWidgetLayout::Next(SettingsSubsystem->GetTimeOfDay(), 2);
		UTimeOfDaySubsystem::SetConsoleOverride({});
		SettingsSubsystem->SetTimeOfDay(Next);
	}
	RefreshFromSettings();
}

void UHawkeyeSettingsWidget::HandleBackClicked()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Click);
	OnBackRequested.Broadcast();
}

void UHawkeyeSettingsWidget::HandleBackHovered()
{
	UHawkeyeAudioSubsystem::PlayUI(this, EHawkeyeUISound::Hover);
}
