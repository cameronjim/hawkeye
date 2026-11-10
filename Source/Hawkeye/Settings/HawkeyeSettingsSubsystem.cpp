#include "Settings/HawkeyeSettingsSubsystem.h"

#include "Hawkeye.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettingsSave.h"
#include "World/TimeOfDaySubsystem.h"

const TCHAR* UHawkeyeSettingsSubsystem::DefaultSlotName = TEXT("HawkeyeSettings");

namespace HawkeyeSettingsSubsystem
{
	/** What GetCurrentSettings answers in a world with no game instance, while a test holds one. */
	static const FHawkeyeSettings* TestOverride = nullptr;
}

UHawkeyeSettingsSubsystem* UHawkeyeSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UHawkeyeSettingsSubsystem>() : nullptr;
}

void UHawkeyeSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Load();
}

float UHawkeyeSettingsSubsystem::ClampLookSensitivity(float Value)
{
	return FMath::Clamp(Value, MinLookSensitivity, MaxLookSensitivity);
}

float UHawkeyeSettingsSubsystem::ClampStickSensitivity(float Value)
{
	return FMath::Clamp(Value, MinStickSensitivity, MaxStickSensitivity);
}

FString UHawkeyeSettingsSubsystem::GetSlotName() const
{
	return SlotNameOverride.IsEmpty() ? FString(DefaultSlotName) : SlotNameOverride;
}

void UHawkeyeSettingsSubsystem::Commit()
{
	Save();
	OnSettingsChanged.Broadcast(GetSettings());
}

void UHawkeyeSettingsSubsystem::SetLookSensitivity(float NewSensitivity)
{
	const float Clamped = ClampLookSensitivity(NewSensitivity);

	// A slider fires on every pixel of drag; a value that changes nothing must not write the
	// file or wake every listener.
	if (FMath::IsNearlyEqual(Clamped, Settings.LookSensitivity, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	Settings.LookSensitivity = Clamped;
	Commit();
}

void UHawkeyeSettingsSubsystem::SetStickSensitivity(float NewSensitivity)
{
	const float Clamped = ClampStickSensitivity(NewSensitivity);

	if (FMath::IsNearlyEqual(Clamped, Settings.StickSensitivity, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	Settings.StickSensitivity = Clamped;
	Commit();
}

void UHawkeyeSettingsSubsystem::SetInvertMouseY(bool bInvert)
{
	SetField(Settings.bInvertMouseY, bInvert);
}

void UHawkeyeSettingsSubsystem::SetInvertStickY(bool bInvert)
{
	SetField(Settings.bInvertStickY, bInvert);
}

void UHawkeyeSettingsSubsystem::SetVolume(float& Field, float Value)
{
	const float Clamped = ClampVolume(Value);
	if (FMath::IsNearlyEqual(Clamped, Field, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}
	Field = Clamped;
	Commit();
}

void UHawkeyeSettingsSubsystem::SetMasterVolume(float NewVolume)
{
	SetVolume(Settings.MasterVolume, NewVolume);
}

void UHawkeyeSettingsSubsystem::SetSfxVolume(float NewVolume)
{
	SetVolume(Settings.SfxVolume, NewVolume);
}

void UHawkeyeSettingsSubsystem::SetAmbientVolume(float NewVolume)
{
	SetVolume(Settings.AmbientVolume, NewVolume);
}

void UHawkeyeSettingsSubsystem::SetMusicVolume(float NewVolume)
{
	SetVolume(Settings.MusicVolume, NewVolume);
}

EHawkeyeDifficulty UHawkeyeSettingsSubsystem::GetDifficulty() const
{
	return UDifficultySubsystem::ResolveDifficulty(Settings.Difficulty);
}

void UHawkeyeSettingsSubsystem::SetDifficulty(EHawkeyeDifficulty NewDifficulty)
{
	SetField(Settings.Difficulty, NewDifficulty);
}

void UHawkeyeSettingsSubsystem::SetSubtitleSize(EHawkeyeSubtitleSize NewSize)
{
	SetField(Settings.SubtitleSize, NewSize);
}

void UHawkeyeSettingsSubsystem::SetSubtitleBackgroundOpacity(float NewOpacity)
{
	SetVolume(Settings.SubtitleBackgroundOpacity, NewOpacity);
}

void UHawkeyeSettingsSubsystem::SetToggleAim(bool bToggle)
{
	SetField(Settings.bToggleAim, bToggle);
}

void UHawkeyeSettingsSubsystem::SetToggleCrouch(bool bToggle)
{
	SetField(Settings.bToggleCrouch, bToggle);
}

void UHawkeyeSettingsSubsystem::SetColorPalette(EHawkeyeColorPalette NewPalette)
{
	SetField(Settings.ColorPalette, NewPalette);
}

void UHawkeyeSettingsSubsystem::SetReduceCameraShake(bool bReduce)
{
	SetField(Settings.bReduceCameraShake, bReduce);
}

void UHawkeyeSettingsSubsystem::SetReduceFlashing(bool bReduce)
{
	SetField(Settings.bReduceFlashing, bReduce);
}

void UHawkeyeSettingsSubsystem::SetAimAssist(EHawkeyeAimAssist NewAimAssist)
{
	SetField(Settings.AimAssist, NewAimAssist);
}

void UHawkeyeSettingsSubsystem::SetSprintMode(EHawkeyeSprintMode NewSprintMode)
{
	SetField(Settings.SprintMode, NewSprintMode);
}

void UHawkeyeSettingsSubsystem::SetShowHints(bool bShow)
{
	SetField(Settings.bShowHints, bShow);
}

void UHawkeyeSettingsSubsystem::SetHudScale(float NewScale)
{
	const float Clamped = ClampHudScale(NewScale);
	if (FMath::IsNearlyEqual(Clamped, Settings.HudScale, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}
	Settings.HudScale = Clamped;
	Commit();
}

EHawkeyeTimeOfDay UHawkeyeSettingsSubsystem::GetTimeOfDay() const
{
	return UTimeOfDaySubsystem::ResolveTimeOfDay(Settings.TimeOfDay);
}

void UHawkeyeSettingsSubsystem::SetTimeOfDay(EHawkeyeTimeOfDay NewTimeOfDay)
{
	SetField(Settings.TimeOfDay, NewTimeOfDay);
}

void UHawkeyeSettingsSubsystem::MarkFlashbackSeen(const FSoftObjectPath& Flashback)
{
	if (Flashback.IsNull() || Settings.SeenFlashbacks.Contains(Flashback))
	{
		return;
	}
	Settings.SeenFlashbacks.Add(Flashback);
	Commit();
}

void UHawkeyeSettingsSubsystem::SetSettings(const FHawkeyeSettings& NewSettings)
{
	Settings = ClampSettings(NewSettings);
	Commit();
}

FHawkeyeSettings UHawkeyeSettingsSubsystem::GetSettings() const
{
	FHawkeyeSettings InForce = Settings;
	InForce.Difficulty = GetDifficulty();
	InForce.TimeOfDay = GetTimeOfDay();
	return InForce;
}

FHawkeyeSettings UHawkeyeSettingsSubsystem::GetCurrentSettings(const UObject* WorldContextObject)
{
	if (const UHawkeyeSettingsSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->GetSettings();
	}
	FHawkeyeSettings Fallback = HawkeyeSettingsSubsystem::TestOverride ? *HawkeyeSettingsSubsystem::TestOverride
		: FHawkeyeSettings();
	Fallback.Difficulty = UDifficultySubsystem::ResolveDifficulty(Fallback.Difficulty);
	Fallback.TimeOfDay = UTimeOfDaySubsystem::ResolveTimeOfDay(Fallback.TimeOfDay);
	return Fallback;
}

void UHawkeyeSettingsSubsystem::SetTestSettingsOverride(const FHawkeyeSettings* Override)
{
	HawkeyeSettingsSubsystem::TestOverride = Override;
}

FHawkeyeSettings UHawkeyeSettingsSubsystem::ClampSettings(const FHawkeyeSettings& InSettings)
{
	FHawkeyeSettings Out = InSettings;
	Out.LookSensitivity = ClampLookSensitivity(Out.LookSensitivity);
	Out.StickSensitivity = ClampStickSensitivity(Out.StickSensitivity);
	Out.MasterVolume = ClampVolume(Out.MasterVolume);
	Out.SfxVolume = ClampVolume(Out.SfxVolume);
	Out.AmbientVolume = ClampVolume(Out.AmbientVolume);
	Out.MusicVolume = ClampVolume(Out.MusicVolume);
	Out.SubtitleBackgroundOpacity = ClampVolume(Out.SubtitleBackgroundOpacity);
	Out.HudScale = ClampHudScale(Out.HudScale);
	Out.Version = FHawkeyeSettings::CurrentVersion;
	return Out;
}

void UHawkeyeSettingsSubsystem::Load()
{
	const FString Slot = GetSlotName();

	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		Settings = FHawkeyeSettings();
		return;
	}

	const UHawkeyeSettingsSave* Loaded = Cast<UHawkeyeSettingsSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Loaded)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("Settings slot %s could not be read; using defaults."), *Slot);
		Settings = FHawkeyeSettings();
		return;
	}

	const int32 LoadedVersion = Loaded->Settings.Version;
	if (LoadedVersion >= FHawkeyeSettings::OldestMigratedVersion && LoadedVersion < FHawkeyeSettings::CurrentVersion)
	{
		// Every field an older version lacks deserialises as its default (6: TimeOfDay, Night; 7: AimAssist, Normal;
		// 8: SprintMode, Default; 9: MusicVolume, 0.6; 10: bShowHints, on), so the rest of what the player set
		// carries over. ClampSettings stamps the current version.
		UE_LOG(LogHawkeye, Log, TEXT("Settings slot %s is version %d; migrated to %d."), *Slot, LoadedVersion,
			FHawkeyeSettings::CurrentVersion);
		Settings = ClampSettings(Loaded->Settings);
		return;
	}

	if (LoadedVersion != FHawkeyeSettings::CurrentVersion)
	{
		UE_LOG(LogHawkeye, Warning,
			TEXT("Settings slot %s is version %d, this build writes %d; using defaults."),
			*Slot, Loaded->Settings.Version, FHawkeyeSettings::CurrentVersion);
		Settings = FHawkeyeSettings();
		return;
	}

	Settings = ClampSettings(Loaded->Settings);
}

bool UHawkeyeSettingsSubsystem::Save() const
{
	UHawkeyeSettingsSave* SaveObject =
		Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!SaveObject)
	{
		UE_LOG(LogHawkeye, Error, TEXT("Could not create the settings save object."));
		return false;
	}

	SaveObject->Settings = Settings;
	SaveObject->Settings.Version = FHawkeyeSettings::CurrentVersion;

	const FString Slot = GetSlotName();
	if (!UGameplayStatics::SaveGameToSlot(SaveObject, Slot, 0))
	{
		UE_LOG(LogHawkeye, Error, TEXT("Could not write the settings slot %s."), *Slot);
		return false;
	}

	return true;
}

FHawkeyeScopedSettingsOverride::FHawkeyeScopedSettingsOverride(const FHawkeyeSettings& Override)
	: Held(Override)
{
	UHawkeyeSettingsSubsystem::SetTestSettingsOverride(&Held);
}

FHawkeyeScopedSettingsOverride::~FHawkeyeScopedSettingsOverride()
{
	UHawkeyeSettingsSubsystem::SetTestSettingsOverride(nullptr);
}
