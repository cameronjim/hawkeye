#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Settings/HawkeyeSettings.h"
#include "HawkeyeSettingsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHawkeyeSettingsChangedSignature, FHawkeyeSettings, Settings);

/**
 * Owns the player's options and persists them. A game instance subsystem because settings
 * outlive a level: the pause menu, the pawn and anything else read the same live values
 * across travel.
 *
 * Deliberately free of UI. UHawkeyeSettingsWidget drives it; AHawkeyeCharacter listens to
 * OnSettingsChanged so a slider takes effect while the player is still holding it.
 */
UCLASS()
class HAWKEYE_API UHawkeyeSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem for the world this object lives in, or nullptr outside a game instance. */
	static UHawkeyeSettingsSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem interface

	/** Fires after every accepted change, with the whole struct as the game uses it (GetSettings). */
	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnHawkeyeSettingsChangedSignature OnSettingsChanged;

	/** Slowest and fastest mouse the screen will offer. */
	static constexpr float MinLookSensitivity = 0.02f;
	static constexpr float MaxLookSensitivity = 1.0f;

	/** Slowest and fastest gamepad stick look the screen will offer. */
	static constexpr float MinStickSensitivity = 0.2f;
	static constexpr float MaxStickSensitivity = 3.0f;

	/** Smallest and largest HUD the screen will offer. */
	static constexpr float MinHudScale = 0.8f;
	static constexpr float MaxHudScale = 1.4f;

	/** The slot settings live in, unless SlotNameOverride is set. */
	static const TCHAR* DefaultSlotName;

	UFUNCTION(BlueprintPure, Category = "Settings")
	float GetLookSensitivity() const { return Settings.LookSensitivity; }

	/** Clamps, stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetLookSensitivity(float NewSensitivity);

	UFUNCTION(BlueprintPure, Category = "Settings")
	float GetStickSensitivity() const { return Settings.StickSensitivity; }

	/** Clamps, stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetStickSensitivity(float NewSensitivity);

	UFUNCTION(BlueprintPure, Category = "Settings")
	bool GetInvertMouseY() const { return Settings.bInvertMouseY; }

	/** Stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetInvertMouseY(bool bInvert);

	UFUNCTION(BlueprintPure, Category = "Settings")
	bool GetInvertStickY() const { return Settings.bInvertStickY; }

	/** Stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetInvertStickY(bool bInvert);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetMasterVolume() const { return Settings.MasterVolume; }

	/** Clamps to 0..1, stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMasterVolume(float NewVolume);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetSfxVolume() const { return Settings.SfxVolume; }

	/** Clamps to 0..1, stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetSfxVolume(float NewVolume);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetAmbientVolume() const { return Settings.AmbientVolume; }

	/** Clamps to 0..1, stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetAmbientVolume(float NewVolume);

	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	float GetMusicVolume() const { return Settings.MusicVolume; }

	/** Clamps to 0..1, stores, saves and broadcasts. A value that changes nothing does nothing. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Audio")
	void SetMusicVolume(float NewVolume);

	// --- Difficulty and accessibility ----------------------------------------------------------

	/** The difficulty in force: a -Difficulty= override (automation) wins over the stored choice. */
	UFUNCTION(BlueprintPure, Category = "Settings|Difficulty")
	EHawkeyeDifficulty GetDifficulty() const;

	/** Stores, saves and broadcasts. A value that changes nothing does nothing (the rest likewise). */
	UFUNCTION(BlueprintCallable, Category = "Settings|Difficulty")
	void SetDifficulty(EHawkeyeDifficulty NewDifficulty);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetSubtitleSize(EHawkeyeSubtitleSize NewSize);

	/** Clamps to 0..1, stores, saves and broadcasts. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetSubtitleBackgroundOpacity(float NewOpacity);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetToggleAim(bool bToggle);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetToggleCrouch(bool bToggle);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetColorPalette(EHawkeyeColorPalette NewPalette);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetReduceCameraShake(bool bReduce);

	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetReduceFlashing(bool bReduce);

	/** The bow's aim assist: Off, Normal or Strong. Stores, saves and broadcasts. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetAimAssist(EHawkeyeAimAssist NewAimAssist);

	/** The sprint button: Default (hold on keys, toggle on a pad), Hold or Toggle. Stores, saves and broadcasts. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetSprintMode(EHawkeyeSprintMode NewSprintMode);

	/** The first-time hints on or off. Stores, saves and broadcasts. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Controls")
	void SetShowHints(bool bShow);

	/** Clamps to [MinHudScale, MaxHudScale], stores, saves and broadcasts. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Accessibility")
	void SetHudScale(float NewScale);

	UFUNCTION(BlueprintPure, Category = "Settings|Accessibility")
	static float ClampHudScale(float Value) { return FMath::Clamp(Value, MinHudScale, MaxHudScale); }

	// --- World ---------------------------------------------------------------------------------------

	/**
	 * The time of day in force: the hawkeye.TimeOfDay console override, else -TimeOfDay= on the command
	 * line, else the stored choice (UTimeOfDaySubsystem::ResolveTimeOfDay).
	 */
	UFUNCTION(BlueprintPure, Category = "Settings|World")
	EHawkeyeTimeOfDay GetTimeOfDay() const;

	/** Stores, saves and broadcasts; UTimeOfDaySubsystem relights the district on the broadcast. */
	UFUNCTION(BlueprintCallable, Category = "Settings|World")
	void SetTimeOfDay(EHawkeyeTimeOfDay NewTimeOfDay);

	/** Adds Flashback to the seen list (once), saves and broadcasts. */
	UFUNCTION(BlueprintCallable, Category = "Settings|Flashbacks")
	void MarkFlashbackSeen(const FSoftObjectPath& Flashback);

	/**
	 * Replaces every option at once (clamped), saves and broadcasts. Tests use it to put the player's
	 * settings back after a pass that changed them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void SetSettings(const FHawkeyeSettings& NewSettings);

	/** The options as the game uses them: the stored ones, with the difficulty and time of day in force. */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FHawkeyeSettings GetSettings() const;

	/** The options exactly as stored (the difficulty the player chose, whatever overrides it). */
	UFUNCTION(BlueprintPure, Category = "Settings")
	FHawkeyeSettings GetStoredSettings() const { return Settings; }

	/**
	 * The options in force for WorldContextObject's game: the subsystem's, or with no game instance (an
	 * automation world) the test override if one is set, else the defaults.
	 */
	static FHawkeyeSettings GetCurrentSettings(const UObject* WorldContextObject);

	/** Tests only: what GetCurrentSettings answers with no subsystem. Null clears it. */
	static void SetTestSettingsOverride(const FHawkeyeSettings* Override);

	/** Every range clamped into place, and the version this build writes. */
	static FHawkeyeSettings ClampSettings(const FHawkeyeSettings& InSettings);

	/**
	 * Reads the slot. A missing file or a version mismatch leaves defaults in place; a save from
	 * FHawkeyeSettings::OldestMigratedVersion up is migrated (the fields it lacks take their defaults).
	 */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void Load();

	/** Writes the slot. Returns false when the platform refused the write. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	bool Save() const;

	UFUNCTION(BlueprintPure, Category = "Settings")
	static float ClampLookSensitivity(float Value);

	UFUNCTION(BlueprintPure, Category = "Settings")
	static float ClampStickSensitivity(float Value);

	/** Every volume slider is 0..1. */
	UFUNCTION(BlueprintPure, Category = "Settings|Audio")
	static float ClampVolume(float Value) { return FMath::Clamp(Value, 0.f, 1.f); }

	/**
	 * Set by tests so the real player's settings are never touched. Empty in game.
	 */
	UPROPERTY(Transient)
	FString SlotNameOverride;

	UFUNCTION(BlueprintPure, Category = "Settings")
	FString GetSlotName() const;

private:
	/** Clamps Value into Field (0..1) and saves and broadcasts if that changed it. */
	void SetVolume(float& Field, float Value);

	/** Writes Value into Field and saves and broadcasts if that changed it. */
	template <typename T>
	void SetField(T& Field, const T& Value)
	{
		if (Field == Value)
		{
			return;
		}
		Field = Value;
		Commit();
	}

	/** Saves and broadcasts the options in force. */
	void Commit();

	UPROPERTY(Transient)
	FHawkeyeSettings Settings;
};

/** Tests only: GetCurrentSettings answers with Override in worlds that have no settings subsystem. */
struct HAWKEYE_API FHawkeyeScopedSettingsOverride
{
	explicit FHawkeyeScopedSettingsOverride(const FHawkeyeSettings& Override);
	~FHawkeyeScopedSettingsOverride();

	FHawkeyeScopedSettingsOverride(const FHawkeyeScopedSettingsOverride&) = delete;
	FHawkeyeScopedSettingsOverride& operator=(const FHawkeyeScopedSettingsOverride&) = delete;

private:
	FHawkeyeSettings Held;
};
