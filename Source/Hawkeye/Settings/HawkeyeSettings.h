#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "HawkeyeSettings.generated.h"

/** How hard the fights are. One setting, three positions; the numbers live in UDifficultySubsystem's table. */
UENUM(BlueprintType)
enum class EHawkeyeDifficulty : uint8
{
	Story,
	Normal,
	Hard
};

/** Subtitle text size: 20, 26 or 34 px (UHawkeyeAccessibility::GetSubtitleFontSize). */
UENUM(BlueprintType)
enum class EHawkeyeSubtitleSize : uint8
{
	Small,
	Medium,
	Large
};

/** Which colours the HUD's green, purple and cream accents, the reticle and the health bars use. */
UENUM(BlueprintType)
enum class EHawkeyeColorPalette : uint8
{
	Default,
	Deuteranopia,
	Protanopia,
	Tritanopia
};

/**
 * The district's light: the moonlit night the chapters are set in, or a plain winter day for seeing the
 * block. UTimeOfDaySubsystem holds what each one sets. New states (dusk, dawn) go on the end so saved
 * values keep their meaning.
 */
UENUM(BlueprintType)
enum class EHawkeyeTimeOfDay : uint8
{
	Night,
	Day
};

/** How much the bow's aim assist helps (UHawkeyeAimAssist::GetTuning). Normal is the default. */
UENUM(BlueprintType)
enum class EHawkeyeAimAssist : uint8
{
	Off,
	Normal,
	Strong
};

/**
 * How the sprint button works. Default holds on a keyboard (Shift) and toggles on a pad (L3); Hold or Toggle set
 * explicitly apply to both. New values go on the end so saved values keep their meaning.
 */
UENUM(BlueprintType)
enum class EHawkeyeSprintMode : uint8
{
	Default,
	Hold,
	Toggle
};

/**
 * Every player-facing option, in one struct, so the save file is one blob and the Settings
 * screen has one thing to read. Adding an option means a new field here and a Version bump;
 * an old save then fails the version check and the player gets defaults rather than garbage.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeSettings
{
	GENERATED_BODY()

	/** Multiplier on raw mouse look input. Clamped to [0.02, 1.0] by the subsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float LookSensitivity = 0.2f;

	/**
	 * Multiplier on the gamepad look stick's degrees-per-second rate. Clamped to [0.2, 3.0] by
	 * the subsystem. Unlike LookSensitivity this scales an already delta-time-scaled rate rather
	 * than a raw per-frame pixel delta, so 1.0 reads as "the tuned default", not "off".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float StickSensitivity = 1.0f;

	/** Flips mouse look pitch: on, pushing the mouse forward looks down instead of up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bInvertMouseY = false;

	/** Flips gamepad look pitch: on, pushing the stick up looks down instead of up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bInvertStickY = false;

	/** Everything the game plays, 0..1. The sliders are squared into gains (HawkeyeAudioMath). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Audio")
	float MasterVolume = 1.0f;

	/** Bow, arrows, fights, footsteps and the menus, 0..1, under the master. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Audio")
	float SfxVolume = 1.0f;

	/** The city: wind, street hum, lamp buzz, 0..1, under the master. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Audio")
	float AmbientVolume = 0.8f;

	/** The combat music and its stings, 0..1, under the master. Low by default: it sits under the effects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Audio")
	float MusicVolume = 0.6f;

	/** Story, Normal or Hard. Asked once at New Game, changeable in Settings at any time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Difficulty")
	EHawkeyeDifficulty Difficulty = EHawkeyeDifficulty::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	EHawkeyeSubtitleSize SubtitleSize = EHawkeyeSubtitleSize::Medium;

	/** Opacity of the black box behind a subtitle, 0 (none) to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	float SubtitleBackgroundOpacity = 0.f;

	/** On, one press of aim raises the bow and the next lowers it; off, aim is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	bool bToggleAim = false;

	/** On, one press crouches and the next stands; off, crouch is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	bool bToggleCrouch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	EHawkeyeColorPalette ColorPalette = EHawkeyeColorPalette::Default;

	/** Camera shakes at 30%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	bool bReduceCameraShake = false;

	/** The EMP's chromatic split, the parry ring and the explosion's flash at 30%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	bool bReduceFlashing = false;

	/** The HUD, hotbar, markers and compass, 0.8 to 1.4. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Accessibility")
	float HudScale = 1.f;

	/**
	 * Flashbacks the player has seen, oldest first, across every save: the pause menu's "Replay
	 * flashbacks" lists them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Flashbacks")
	TArray<FSoftObjectPath> SeenFlashbacks;

	/** Night (the story's) or Day. Applied live by UTimeOfDaySubsystem; -TimeOfDay= overrides it unsaved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|World")
	EHawkeyeTimeOfDay TimeOfDay = EHawkeyeTimeOfDay::Night;

	/** The bow's aim assist: Off, Normal (default) or Strong; the difficulty scales it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Controls")
	EHawkeyeAimAssist AimAssist = EHawkeyeAimAssist::Normal;

	/** The sprint button: hold on keys and toggle on a pad (Default), or Hold or Toggle on both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Controls")
	EHawkeyeSprintMode SprintMode = EHawkeyeSprintMode::Default;

	/** The first-time control hints above the hotbar (UHawkeyeHintSubsystem). On by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Controls")
	bool bShowHints = true;

	/** Bumped whenever the meaning of a field changes. A mismatch on load yields defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	int32 Version = 10;

	/** The version this build writes and accepts. */
	static constexpr int32 CurrentVersion = 10;

	/**
	 * The oldest save this build migrates rather than dropping: version 5 lacks TimeOfDay, AimAssist,
	 * SprintMode, MusicVolume and bShowHints, 6 lacks AimAssist onward, 7 lacks SprintMode onward, 8 lacks
	 * MusicVolume and bShowHints, 9 lacks bShowHints; each loads as its default (Night, Normal, Default, 0.6,
	 * hints on), so everything else the player set is kept. Older yields defaults.
	 */
	static constexpr int32 OldestMigratedVersion = 5;
};
