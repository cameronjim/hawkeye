#pragma once

#include "CoreMinimal.h"
#include "Settings/HawkeyeSettings.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "TimeOfDaySubsystem.generated.h"

class AActor;
class APostProcessVolume;
class UAudioComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class ULightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class USkyLightComponent;

/**
 * What one time of day asks of the district, relative to the night the generator builds
 * (Tools/Editor/generate_city.py, ensure_lighting_core). One row per EHawkeyeTimeOfDay in
 * UTimeOfDaySubsystem::GetPreset. Night's row is bUseAuthored: the level's own values, untouched.
 *
 * A real cycle later would add rows (dusk, dawn) and blend two of them by the clock; the sun's angle
 * could then come from the engine's SunPosition plugin (USunPositionFunctionLibrary::GetSunPosition at
 * 40.73 N, 73.98 W). Two fixed states do not need it.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FTimeOfDayPreset
{
	GENERATED_BODY()

	/** Night: write back exactly what the level had. Every other field is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	bool bUseAuthored = false;

	/** The directional light's rotation (it shines along its forward vector). Pitch -35 is 35 degrees up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	FRotator SunRotation = FRotator::ZeroRotator;

	/** Directional light illuminance, lux. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float SunLux = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	FLinearColor SunColor = FLinearColor::White;

	/** The sun disc the sky atmosphere draws, as a tint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	FLinearColor SunDiskColorScale = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float SkyLightIntensity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	FLinearColor SkyLightColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float FogDensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	FLinearColor FogInscattering = FLinearColor::Black;

	/** cm from the camera before the fog starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float FogStartDistance = 0.f;

	/**
	 * PP_Global's exposure bias. Exposure is pinned (auto exposure is off project-wide and PP_Global's
	 * min and max brightness are both 1.0 EV100), so this is the only exposure knob: the night's is 2.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float ExposureBias = 0.f;

	/** PP_Global's shadow and highlight gains: the night's purple and cream, or neutral. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	FVector4 GainShadows = FVector4(1.f, 1.f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	FVector4 GainHighlights = FVector4(1.f, 1.f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float Vignette = 0.f;

	/** City_NightSky, the star sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	bool bStars = false;

	/** Street lamps (tag CityLamp): their lights' intensity and their heads' glow, times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float LampScale = 1.f;

	/** The facades' lit windows (M_Facade's WindowGlow), times this, and times GlowScale on top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float WindowGlowScale = 1.f;

	/**
	 * Every glow the player reads the city by, times this: the level's M_Emissive materials (objective
	 * beacons, safehouse doors, the chapter-end arrow), the lit windows, and every glow a class sets through
	 * UTimeOfDaySubsystem::SetGlow (challenge pedestals, target faces, checkpoint rings). The exposure is
	 * pinned, so a lower exposure bias dims emissives exactly as much as it dims the lights; this is what
	 * keeps them reading.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float GlowScale = 1.f;

	/**
	 * How much of City_Ambience's street level goes to the day bed (MS_Amb_StreetDay) rather than the night's
	 * (MS_Amb_Street: the mains drone, a traffic rumble, far horns): 0 at night, 1 by day. The beds crossfade
	 * over HawkeyeAudioMath::BedCrossfadeSeconds when it changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TimeOfDay")
	float DayBedWeight = 0.f;
};

/**
 * Lights the district for the time of day in force (UHawkeyeSettingsSubsystem::GetTimeOfDay): at world
 * begin play, and again whenever the setting or the hawkeye.TimeOfDay console override changes, without
 * a reload. Night is the level exactly as generated; Day is GetPreset(Day) applied on top of it.
 *
 * Finds the scene's pieces by the generator's labels (Moon, SkyLight, HeightFog, PP_Global) in the
 * editor, by class in a cooked build (labels are editor-only), the stars by tag CityNightSky, lamps by
 * tag CityLamp and facades by their material's WindowGlow parameter. Anything missing is skipped.
 * Actors tagged Interior are never touched, and a world without outdoor weather (an interior, a
 * flashback's playable scene) is left alone at begin play; only Apply called directly lights those.
 *
 * Before the first change it records what the level had, so going back to Night restores it exactly.
 *
 * Lamps and the EMP (UArrowEffectsSubsystem) share the lamps without fighting: the time of day owns each
 * lamp's light intensity, head glow and buzz; the EMP owns its lights' visibility. A lamp the EMP has dark
 * keeps its head unlit and its buzz silent whatever the time of day asks, and when the outage ends the EMP
 * hands the lamp back through RefreshLamp, which lights it for the time of day then in force.
 */
UCLASS()
class HAWKEYE_API UTimeOfDaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UTimeOfDaySubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem / UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	//~ End USubsystem / UWorldSubsystem interface

	/** Lights the world for State now. Records the authored night first if it has not yet. */
	UFUNCTION(BlueprintCallable, Category = "TimeOfDay")
	void Apply(EHawkeyeTimeOfDay State);

	/** Applies the time of day in force, if it is not the one already applied. */
	UFUNCTION(BlueprintCallable, Category = "TimeOfDay")
	void Refresh();

	/** What Apply last put in place (Night before the first call). */
	UFUNCTION(BlueprintPure, Category = "TimeOfDay")
	EHawkeyeTimeOfDay GetApplied() const { return Applied; }

	/** The row Apply last put in place (Night's before the first call). */
	const FTimeOfDayPreset& GetAppliedPreset() const { return AppliedPreset; }

	/** WorldContext's world's applied row, or Night's when there is no time of day there (the editor, no world). */
	static FTimeOfDayPreset GetPresetInForce(const UObject* WorldContext);

	/** GetPresetInForce(WorldContext).GlowScale. */
	static float GetGlowScale(const UObject* WorldContext);

	/**
	 * Sets Parameter on Material to NightValue times the glow scale in force, and keeps it there: every later
	 * Apply rescales it. Calling it again for the same material and parameter replaces the night value. Without
	 * a time of day subsystem (the editor, a test with no world) it just sets NightValue.
	 */
	static void SetGlow(const UObject* WorldContext, UMaterialInstanceDynamic* Material, float NightValue,
		FName Parameter = FName(TEXT("Intensity")));

	/**
	 * Lights Lamp (an actor tagged CityLamp) for the time of day in force, unless the EMP still has it dark.
	 * False when this subsystem has not recorded the lamp (no Apply yet, or not a lamp), and the caller keeps
	 * its own record. UArrowEffectsSubsystem calls it when an outage ends.
	 */
	bool RefreshLamp(AActor* Lamp);

	/** Whether the time of day has Lamp's head glowing and its buzz playing (false for an unrecorded lamp). */
	bool IsLampLit(const AActor* Lamp) const;

	/** How many lamp lights, lamp heads, lamp buzzes and facade materials the last Apply changed (tests, the log). */
	int32 GetLampLightCount() const;
	int32 GetLampGlowCount() const;
	int32 GetLampSoundCount() const;
	int32 GetWindowMaterialCount() const { return WindowMaterials.Num(); }

	/** How many of the level's M_Emissive materials, and how many SetGlow instances, it scales. */
	int32 GetEmissiveMaterialCount() const { return EmissiveMaterials.Num(); }
	int32 GetRegisteredGlowCount() const { return RegisteredGlows.Num(); }

	/** The table: what State asks for. Night's row says "the level's own values". */
	UFUNCTION(BlueprintPure, Category = "TimeOfDay")
	static FTimeOfDayPreset GetPreset(EHawkeyeTimeOfDay State);

	/** The console override, else the command line's -TimeOfDay=, else Stored. */
	static EHawkeyeTimeOfDay ResolveTimeOfDay(EHawkeyeTimeOfDay Stored);

	/** "Night" or "Day" (case does not matter) into OutState. False for anything else. */
	static bool ParseTimeOfDay(const FString& Text, EHawkeyeTimeOfDay& OutState);

	/** The player-facing name: "Night", "Day". */
	UFUNCTION(BlueprintPure, Category = "TimeOfDay")
	static FText GetTimeOfDayName(EHawkeyeTimeOfDay State);

	/**
	 * hawkeye.TimeOfDay's override (not saved), or unset to fall back to the command line and the
	 * setting. Relights every game world. The Settings row clears it when the player picks a value.
	 */
	static void SetConsoleOverride(TOptional<EHawkeyeTimeOfDay> Override);
	static TOptional<EHawkeyeTimeOfDay> GetConsoleOverride();

	/** Tests only: replaces what -TimeOfDay= parsed to, until the returned previous value is put back. */
	static TOptional<EHawkeyeTimeOfDay> SetCommandLineOverrideForTest(TOptional<EHawkeyeTimeOfDay> Override);

	/** What -TimeOfDay= in CommandLine names, if anything. Pure; the cached command line uses it. */
	static TOptional<EHawkeyeTimeOfDay> ParseCommandLine(const TCHAR* CommandLine);

private:
	/** Every piece the time of day moves, as the level had it. */
	struct FAuthored
	{
		FRotator SunRotation = FRotator::ZeroRotator;
		float SunLux = 0.f;
		FColor SunColor = FColor::White;
		FLinearColor SunDiskColorScale = FLinearColor::White;
		bool bSunUseTemperature = false;
		float SkyLightIntensity = 1.f;
		FColor SkyLightColor = FColor::White;
		float FogDensity = 0.f;
		FLinearColor FogInscattering = FLinearColor::Black;
		float FogStartDistance = 0.f;
		bool bOverrideExposureBias = false;
		float ExposureBias = 0.f;
		bool bOverrideGainShadows = false;
		FVector4 GainShadows = FVector4(1.f, 1.f, 1.f, 1.f);
		bool bOverrideGainHighlights = false;
		FVector4 GainHighlights = FVector4(1.f, 1.f, 1.f, 1.f);
		bool bOverrideVignette = false;
		float Vignette = 0.f;
		bool bStarsHidden = false;
	};

	/** A lamp head's glowing material slot and its glow as authored. */
	struct FLampGlow
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		float Intensity = 0.f;
	};

	/** One lamp actor: its lights, its glowing head and its buzz, as authored. */
	struct FLamp
	{
		TWeakObjectPtr<AActor> Actor;
		TArray<TPair<TWeakObjectPtr<ULightComponent>, float>> Lights;
		TArray<FLampGlow> Glows;
		TArray<TWeakObjectPtr<UAudioComponent>> Sounds;
		bool bLit = true;
	};

	/**
	 * One authored material the day rescales (a facade's WindowGlow, an M_Emissive's Intensity): its value
	 * as authored, and every component slot that wears it.
	 */
	struct FSwappedMaterial
	{
		TWeakObjectPtr<UMaterialInterface> Authored;
		float Glow = 0.f;
		TArray<TPair<TWeakObjectPtr<UMeshComponent>, int32>> Slots;
	};

	/** A glow a class set through SetGlow. */
	struct FRegisteredGlow
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Material;
		FName Parameter;
		float NightValue = 0.f;
	};

	/** Finds the pieces and records them as authored. Once per world. */
	void Capture();
	void CaptureLamps();
	void CaptureMaterials();

	/** Writes the night back exactly. */
	void RestoreAuthored();

	void ApplyPreset(const FTimeOfDayPreset& Preset);

	/** Every lamp at Scale (0 off, 1 as authored), except that an EMP-dark lamp's head and buzz stay off. */
	void ApplyLamps(float Scale);
	void ApplyLamp(FLamp& Lamp, float Scale);

	/** Every registered glow at its night value times Scale. */
	void ApplyRegisteredGlows(float Scale);

	/** Swaps a copy of each of Materials into its slots, with Parameter at the authored value times Scale. */
	void ApplySwapped(const TArray<FSwappedMaterial>& Materials, TArray<TObjectPtr<UMaterialInstanceDynamic>>& Copies,
		FName Parameter, float Scale);

	/** Puts each of Materials back in its slots. */
	static void RestoreSwapped(const TArray<FSwappedMaterial>& Materials);

	UFUNCTION()
	void HandleSettingsChanged(FHawkeyeSettings NewSettings);

	/** The actor labelled Label (editor), else the first of Class not tagged Interior. */
	AActor* FindScenePiece(const TCHAR* Label, UClass* Class) const;

	bool bCaptured = false;
	EHawkeyeTimeOfDay Applied = EHawkeyeTimeOfDay::Night;
	FAuthored Authored;

	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	TWeakObjectPtr<USkyLightComponent> SkyLight;
	TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
	TWeakObjectPtr<APostProcessVolume> PostProcess;
	TWeakObjectPtr<AActor> Stars;

	FTimeOfDayPreset AppliedPreset;

	TArray<FLamp> Lamps;
	TArray<FSwappedMaterial> WindowMaterials;
	TArray<FSwappedMaterial> EmissiveMaterials;
	TMap<TPair<TObjectKey<UMaterialInstanceDynamic>, FName>, FRegisteredGlow> RegisteredGlows;

	/** The day's facade and emissive copies, one per authored material; kept so they are not collected while in use. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WindowDimmers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> EmissiveCopies;

	bool bListening = false;
};
