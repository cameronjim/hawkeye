#include "World/TimeOfDaySubsystem.h"

#include "Hawkeye.h"
#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Vfx/SnowfallComponent.h"

namespace HawkeyeTimeOfDay
{
	static const FName InteriorTag(TEXT("Interior"));
	static const FName StarsTag(TEXT("CityNightSky"));
	static const FName LampTag(TEXT("CityLamp"));
	/** M_Facade's lit-window strength (Tools/Editor/_materials.py). */
	static const FName WindowGlowParameter(TEXT("WindowGlow"));
	/** A lamp head's glow (M_LampHead's EMISSIVE_INTENSITY_PARAM), the one the EMP also dims. */
	static const FName LampGlowParameter(TEXT("Intensity"));
	/** M_Emissive's strength (EMISSIVE_INTENSITY_PARAM): beacons, safehouse doors, pedestals, targets, rings. */
	static const FName GlowParameter(TEXT("Intensity"));
	/** The base material every glow the city is read by derives from (Tools/Editor/_materials.py, M_EMISSIVE). */
	static const FName EmissiveMaterialName(TEXT("M_Emissive"));

	/**
	 * The day: a clear winter afternoon. The sun 30 degrees up in the south-west (Unreal X is east and Y
	 * south, so it shines toward the north-east, yaw -45), 16 lux of warm white with a little gold. Exposure
	 * stays pinned at 1.0 EV100 as at night; the bias drops from the night's +2 to -0.3, bright enough that
	 * snow reads white and not so bright that sunlit snow clips (none did in day_street.png or day_park.png).
	 * The sky light at 8 (the night's is 3), tinted warm against the blue sky it captures, fills the streets
	 * the low sun leaves in shade: blue there, not black. Thin warm fog. Picked from three rounds of variants
	 * at the same cameras (2026-09-29); the first day (7 lux, sky 6 cool, bias -0.5) read blue and dim.
	 */
	static FTimeOfDayPreset MakeDay()
	{
		// 2026-11-13, the block look (claude-docs/city-look.md): an overcast January afternoon rather than a clear one.
		// Look pass 2: under the cloud deck (City_CloudDeck) the sun drops to 7 lux and the sky light rises to 13, so shadows
		// are faint and the light comes from the whole sky.
		// The sun lower and weaker (12 lux) and nearly white, the sky light stronger and cool so the shade is grey-blue and soft,
		// a denser grey haze (the atmosphere's Mie, set by generate_city, whitens the sky), exposure bias 0 (was -0.3). Was 16 lux warm sun 30
		// degrees up, sky 8 warm, fog 0.0015 warm from 30 m.
		FTimeOfDayPreset Day;
		Day.SunRotation = FRotator(-22.f, -45.f, 0.f);
		Day.SunLux = 7.f;
		Day.SunColor = FLinearColor(1.f, 0.95f, 0.88f);
		Day.SunDiskColorScale = FLinearColor::White;
		Day.SkyLightIntensity = 13.f;
		Day.SkyLightColor = FLinearColor(0.88f, 0.93f, 1.f);
		Day.FogDensity = 0.008f;
		Day.FogInscattering = FLinearColor(0.24f, 0.25f, 0.27f);
		Day.FogStartDistance = 1500.f;
		Day.ExposureBias = 0.f;
		Day.GainShadows = FVector4(1.f, 1.f, 1.f, 1.f);
		Day.GainHighlights = FVector4(1.f, 1.f, 1.f, 1.f);
		Day.Vignette = 0.2f;
		Day.bStars = false;
		Day.LampScale = 0.f;
		Day.WindowGlowScale = 0.25f;
		// 2.3 EV under the night makes every emissive about 5x dimmer; 3x brings the glows back to reading
		// (the windows end at 0.25 x 3 of the night's, a faint warm light behind the glass).
		Day.GlowScale = 3.f;
		Day.DayBedWeight = 1.f;
		return Day;
	}

	/** -TimeOfDay= as parsed at first use, replaceable by tests. */
	static TOptional<EHawkeyeTimeOfDay>& CommandLineOverride()
	{
		static TOptional<EHawkeyeTimeOfDay> Parsed = UTimeOfDaySubsystem::ParseCommandLine(FCommandLine::Get());
		return Parsed;
	}

	static TOptional<EHawkeyeTimeOfDay> ConsoleOverride;

	static void RefreshAllWorlds()
	{
		if (!GEngine)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				if (UTimeOfDaySubsystem* Subsystem = World->GetSubsystem<UTimeOfDaySubsystem>())
				{
					Subsystem->Refresh();
				}
			}
		}
	}

	static UWorld* FirstGameWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	static FAutoConsoleCommand TimeOfDayCommand(
		TEXT("hawkeye.TimeOfDay"),
		TEXT("hawkeye.TimeOfDay Day|Night relights the district now without saving (it wins over the setting and ")
		TEXT("-TimeOfDay= until the Settings row is changed). hawkeye.TimeOfDay Saved drops it. No argument prints ")
		TEXT("the time of day in force."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				const EHawkeyeTimeOfDay InForce = UHawkeyeSettingsSubsystem::GetCurrentSettings(FirstGameWorld()).TimeOfDay;
				UE_LOG(LogHawkeye, Display, TEXT("hawkeye.TimeOfDay: %s%s."),
					*UTimeOfDaySubsystem::GetTimeOfDayName(InForce).ToString(),
					ConsoleOverride.IsSet() ? TEXT(" (console override)") : TEXT(""));
				return;
			}
			if (Args[0].Equals(TEXT("Saved"), ESearchCase::IgnoreCase))
			{
				UTimeOfDaySubsystem::SetConsoleOverride({});
				return;
			}
			EHawkeyeTimeOfDay State = EHawkeyeTimeOfDay::Night;
			if (!UTimeOfDaySubsystem::ParseTimeOfDay(Args[0], State))
			{
				UE_LOG(LogHawkeye, Warning, TEXT("hawkeye.TimeOfDay: '%s' is not Day, Night or Saved."), *Args[0]);
				return;
			}
			UTimeOfDaySubsystem::SetConsoleOverride(State);
		}));
}

UTimeOfDaySubsystem* UTimeOfDaySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UTimeOfDaySubsystem>() : nullptr;
}

bool UTimeOfDaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTimeOfDaySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Interiors and flashback scenes have no outdoor weather and their own light; they stay as built.
	if (!USnowfallComponent::ShouldSnowIn(&InWorld))
	{
		return;
	}

	if (UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(&InWorld))
	{
		Settings->OnSettingsChanged.AddUniqueDynamic(this, &UTimeOfDaySubsystem::HandleSettingsChanged);
		bListening = true;
	}

	const EHawkeyeTimeOfDay InForce = UHawkeyeSettingsSubsystem::GetCurrentSettings(&InWorld).TimeOfDay;
	if (InForce != EHawkeyeTimeOfDay::Night)
	{
		Apply(InForce);
	}
}

void UTimeOfDaySubsystem::Deinitialize()
{
	if (bListening)
	{
		if (UHawkeyeSettingsSubsystem* Settings = UHawkeyeSettingsSubsystem::Get(GetWorld()))
		{
			Settings->OnSettingsChanged.RemoveDynamic(this, &UTimeOfDaySubsystem::HandleSettingsChanged);
		}
		bListening = false;
	}
	Super::Deinitialize();
}

void UTimeOfDaySubsystem::HandleSettingsChanged(FHawkeyeSettings NewSettings)
{
	if (NewSettings.TimeOfDay != Applied)
	{
		Apply(NewSettings.TimeOfDay);
	}
}

void UTimeOfDaySubsystem::Refresh()
{
	const UWorld* World = GetWorld();
	if (!World || !USnowfallComponent::ShouldSnowIn(World))
	{
		return;
	}
	const EHawkeyeTimeOfDay InForce = UHawkeyeSettingsSubsystem::GetCurrentSettings(World).TimeOfDay;
	if (InForce != Applied)
	{
		Apply(InForce);
	}
}

void UTimeOfDaySubsystem::Apply(EHawkeyeTimeOfDay State)
{
	if (!bCaptured)
	{
		Capture();
	}

	const FTimeOfDayPreset Preset = GetPreset(State);
	if (Preset.bUseAuthored)
	{
		RestoreAuthored();
	}
	else
	{
		ApplyPreset(Preset);
	}
	Applied = State;
	AppliedPreset = Preset;
	// Night's row carries 1 for every scale: these put back what the level had, bar what the EMP has dark.
	ApplyLamps(Preset.LampScale);
	ApplyRegisteredGlows(Preset.GlowScale);

	UE_LOG(LogHawkeye, Log,
		TEXT("%s: %s (sun %d, sky light %d, fog %d, post process %d, stars %d, %d lamp lights, %d lamp heads, %d lamp buzzes, ")
		TEXT("%d facade materials, %d emissive materials, %d registered glows at x%.2f)."),
		*GetNameSafe(GetWorld()), *GetTimeOfDayName(State).ToString(), Sun.IsValid() ? 1 : 0, SkyLight.IsValid() ? 1 : 0,
		Fog.IsValid() ? 1 : 0, PostProcess.IsValid() ? 1 : 0, Stars.IsValid() ? 1 : 0, GetLampLightCount(), GetLampGlowCount(),
		GetLampSoundCount(), WindowMaterials.Num(), EmissiveMaterials.Num(), RegisteredGlows.Num(), Preset.GlowScale);
}

FTimeOfDayPreset UTimeOfDaySubsystem::GetPresetInForce(const UObject* WorldContextObject)
{
	const UTimeOfDaySubsystem* Subsystem = Get(WorldContextObject);
	return Subsystem ? Subsystem->AppliedPreset : GetPreset(EHawkeyeTimeOfDay::Night);
}

float UTimeOfDaySubsystem::GetGlowScale(const UObject* WorldContextObject)
{
	const UTimeOfDaySubsystem* Subsystem = Get(WorldContextObject);
	return Subsystem ? Subsystem->AppliedPreset.GlowScale : 1.f;
}

void UTimeOfDaySubsystem::SetGlow(const UObject* WorldContextObject, UMaterialInstanceDynamic* Material, float NightValue,
	FName Parameter)
{
	if (!Material)
	{
		return;
	}
	UTimeOfDaySubsystem* Subsystem = Get(WorldContextObject);
	Material->SetScalarParameterValue(Parameter, NightValue * (Subsystem ? Subsystem->AppliedPreset.GlowScale : 1.f));
	if (Subsystem)
	{
		// A ring or target that has gone (a challenge ended) leaves its entry behind; drop those here rather than
		// only at the next Apply, so the list stays the size of what is alive.
		for (auto It = Subsystem->RegisteredGlows.CreateIterator(); It; ++It)
		{
			if (!It.Value().Material.IsValid())
			{
				It.RemoveCurrent();
			}
		}
		FRegisteredGlow& Entry = Subsystem->RegisteredGlows.FindOrAdd(MakeTuple(TObjectKey<UMaterialInstanceDynamic>(Material), Parameter));
		Entry.Material = Material;
		Entry.Parameter = Parameter;
		Entry.NightValue = NightValue;
	}
}

void UTimeOfDaySubsystem::ApplyRegisteredGlows(float Scale)
{
	for (auto It = RegisteredGlows.CreateIterator(); It; ++It)
	{
		UMaterialInstanceDynamic* Material = It.Value().Material.Get();
		if (!Material)
		{
			It.RemoveCurrent();
			continue;
		}
		Material->SetScalarParameterValue(It.Value().Parameter, It.Value().NightValue * Scale);
	}
}

int32 UTimeOfDaySubsystem::GetLampLightCount() const
{
	int32 Count = 0;
	for (const FLamp& Lamp : Lamps)
	{
		Count += Lamp.Lights.Num();
	}
	return Count;
}

int32 UTimeOfDaySubsystem::GetLampGlowCount() const
{
	int32 Count = 0;
	for (const FLamp& Lamp : Lamps)
	{
		Count += Lamp.Glows.Num();
	}
	return Count;
}

int32 UTimeOfDaySubsystem::GetLampSoundCount() const
{
	int32 Count = 0;
	for (const FLamp& Lamp : Lamps)
	{
		Count += Lamp.Sounds.Num();
	}
	return Count;
}

bool UTimeOfDaySubsystem::RefreshLamp(AActor* Lamp)
{
	if (!Lamp || !bCaptured)
	{
		return false;
	}
	FLamp* Entry = Lamps.FindByPredicate([Lamp](const FLamp& Candidate) { return Candidate.Actor.Get() == Lamp; });
	if (!Entry)
	{
		return false;
	}
	ApplyLamp(*Entry, AppliedPreset.LampScale);
	return true;
}

bool UTimeOfDaySubsystem::IsLampLit(const AActor* Lamp) const
{
	const FLamp* Entry = Lamp ? Lamps.FindByPredicate([Lamp](const FLamp& Candidate) { return Candidate.Actor.Get() == Lamp; }) : nullptr;
	return Entry && Entry->bLit;
}

void UTimeOfDaySubsystem::ApplyLamps(float Scale)
{
	for (FLamp& Lamp : Lamps)
	{
		ApplyLamp(Lamp, Scale);
	}
}

void UTimeOfDaySubsystem::ApplyLamp(FLamp& Lamp, float Scale)
{
	// The EMP's outage wins over the time of day: its lights stay hidden (the EMP's own switch), and the
	// head and the buzz stay off until it hands the lamp back.
	const UArrowEffectsSubsystem* Effects = UArrowEffectsSubsystem::Get(this);
	const bool bEmpDark = Effects && Effects->IsLampDisabled(Lamp.Actor.Get());
	Lamp.bLit = !bEmpDark && Scale > 0.f;
	for (const TPair<TWeakObjectPtr<ULightComponent>, float>& Light : Lamp.Lights)
	{
		if (ULightComponent* Live = Light.Key.Get())
		{
			Live->SetIntensity(Light.Value * Scale);
		}
	}
	for (const FLampGlow& Glow : Lamp.Glows)
	{
		if (UMaterialInstanceDynamic* Material = Glow.Material.Get())
		{
			Material->SetScalarParameterValue(HawkeyeTimeOfDay::LampGlowParameter, bEmpDark ? 0.f : Glow.Intensity * Scale);
		}
	}
	for (const TWeakObjectPtr<UAudioComponent>& Sound : Lamp.Sounds)
	{
		UAudioComponent* Live = Sound.Get();
		if (!Live)
		{
			continue;
		}
		if (Lamp.bLit && !Live->IsPlaying())
		{
			Live->Play();
		}
		else if (!Lamp.bLit && Live->IsPlaying())
		{
			Live->Stop();
		}
	}
}

AActor* UTimeOfDaySubsystem::FindScenePiece(const TCHAR* Label, UClass* Class) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	AActor* FirstOfClass = nullptr;
	for (TActorIterator<AActor> It(World, Class); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->Tags.Contains(HawkeyeTimeOfDay::InteriorTag))
		{
			continue;
		}
#if WITH_EDITOR
		if (Actor->GetActorLabel(false) == Label)
		{
			return Actor;
		}
#endif
		if (Actor->GetFName() == FName(Label))
		{
			return Actor;
		}
		if (!FirstOfClass)
		{
			FirstOfClass = Actor;
		}
	}
	return FirstOfClass;
}

void UTimeOfDaySubsystem::Capture()
{
	bCaptured = true;

	if (const ADirectionalLight* Moon = Cast<ADirectionalLight>(FindScenePiece(TEXT("Moon"), ADirectionalLight::StaticClass())))
	{
		Sun = Cast<UDirectionalLightComponent>(Moon->GetLightComponent());
	}
	if (UDirectionalLightComponent* Light = Sun.Get())
	{
		Authored.SunRotation = Light->GetComponentRotation();
		Authored.SunLux = Light->Intensity;
		Authored.SunColor = Light->LightColor;
		Authored.SunDiskColorScale = Light->GetAtmosphereSunDiskColorScale();
		Authored.bSunUseTemperature = Light->bUseTemperature;
	}

	if (const ASkyLight* Sky = Cast<ASkyLight>(FindScenePiece(TEXT("SkyLight"), ASkyLight::StaticClass())))
	{
		SkyLight = Sky->GetLightComponent();
	}
	if (const USkyLightComponent* Light = SkyLight.Get())
	{
		Authored.SkyLightIntensity = Light->Intensity;
		Authored.SkyLightColor = Light->LightColor;
	}

	if (const AExponentialHeightFog* HeightFog =
			Cast<AExponentialHeightFog>(FindScenePiece(TEXT("HeightFog"), AExponentialHeightFog::StaticClass())))
	{
		Fog = HeightFog->GetComponent();
	}
	if (const UExponentialHeightFogComponent* Haze = Fog.Get())
	{
		Authored.FogDensity = Haze->FogDensity;
		Authored.FogInscattering = Haze->FogInscatteringLuminance;
		Authored.FogStartDistance = Haze->StartDistance;
	}

	// PP_Global by label; otherwise the first unbound volume (a bounded one is a room's, not the sky's).
	APostProcessVolume* Global = nullptr;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (It->Tags.Contains(HawkeyeTimeOfDay::InteriorTag))
			{
				continue;
			}
#if WITH_EDITOR
			if (It->GetActorLabel(false) == TEXT("PP_Global"))
			{
				Global = *It;
				break;
			}
#endif
			if (!Global && It->bUnbound)
			{
				Global = *It;
			}
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(HawkeyeTimeOfDay::StarsTag) && !It->Tags.Contains(HawkeyeTimeOfDay::InteriorTag))
			{
				Stars = *It;
				break;
			}
		}
	}
	PostProcess = Global;
	if (const APostProcessVolume* Volume = PostProcess.Get())
	{
		const FPostProcessSettings& Settings = Volume->Settings;
		Authored.bOverrideExposureBias = Settings.bOverride_AutoExposureBias;
		Authored.ExposureBias = Settings.AutoExposureBias;
		Authored.bOverrideGainShadows = Settings.bOverride_ColorGainShadows;
		Authored.GainShadows = Settings.ColorGainShadows;
		Authored.bOverrideGainHighlights = Settings.bOverride_ColorGainHighlights;
		Authored.GainHighlights = Settings.ColorGainHighlights;
		Authored.bOverrideVignette = Settings.bOverride_VignetteIntensity;
		Authored.Vignette = Settings.VignetteIntensity;
	}
	if (const AActor* Sphere = Stars.Get())
	{
		Authored.bStarsHidden = Sphere->IsHidden();
	}

	CaptureLamps();
	CaptureMaterials();
}

void UTimeOfDaySubsystem::CaptureLamps()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->Tags.Contains(HawkeyeTimeOfDay::LampTag) || It->Tags.Contains(HawkeyeTimeOfDay::InteriorTag))
		{
			continue;
		}
		FLamp Lamp;
		Lamp.Actor = *It;
		TArray<ULightComponent*> Lights;
		It->GetComponents<ULightComponent>(Lights);
		for (ULightComponent* Light : Lights)
		{
			if (Light)
			{
				Lamp.Lights.Emplace(Light, Light->Intensity);
			}
		}
		TArray<UMeshComponent*> Meshes;
		It->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			// The glow as authored: an EMP may already have this head dark, through its own instance of it.
			UMaterialInterface* Material = Mesh ? Mesh->GetMaterial(0) : nullptr;
			if (const UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Material))
			{
				Material = Existing->Parent;
			}
			float Glow = 0.f;
			if (!Material || !Material->GetScalarParameterValue(FHashedMaterialParameterInfo(HawkeyeTimeOfDay::LampGlowParameter), Glow)
				|| Glow <= 0.f)
			{
				continue;
			}
			// The same dynamic instance the EMP dims (CreateDynamicMaterialInstance reuses one already there).
			if (UMaterialInstanceDynamic* Dynamic = Mesh->CreateDynamicMaterialInstance(0))
			{
				FLampGlow Entry;
				Entry.Material = Dynamic;
				Entry.Intensity = Glow;
				Lamp.Glows.Add(Entry);
			}
		}
		// The buzz on the lamps round the park (City_LampBuzz_<n>, AAmbientSound, tag CityLamp).
		TArray<UAudioComponent*> Sounds;
		It->GetComponents<UAudioComponent>(Sounds);
		for (UAudioComponent* Sound : Sounds)
		{
			if (Sound)
			{
				Lamp.Sounds.Add(Sound);
			}
		}
		if (Lamp.Lights.Num() + Lamp.Glows.Num() + Lamp.Sounds.Num() > 0)
		{
			Lamps.Add(MoveTemp(Lamp));
		}
	}
}

void UTimeOfDaySubsystem::CaptureMaterials()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// One entry per authored material, however many meshes wear it: facades by their WindowGlow parameter,
	// glows by their base material, M_Emissive. Dynamic instances belong to the classes that made them (they
	// go through SetGlow), and lamps are CaptureLamps'.
	TMap<UMaterialInterface*, TPair<bool, int32>> Seen;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->Tags.Contains(HawkeyeTimeOfDay::InteriorTag) || It->Tags.Contains(HawkeyeTimeOfDay::LampTag))
		{
			continue;
		}
		TArray<UMeshComponent*> Meshes;
		It->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			const int32 Count = Mesh ? Mesh->GetNumMaterials() : 0;
			for (int32 Slot = 0; Slot < Count; ++Slot)
			{
				UMaterialInterface* Material = Mesh->GetMaterial(Slot);
				if (!Material || Material->IsA<UMaterialInstanceDynamic>())
				{
					continue;
				}
				TPair<bool, int32>* Known = Seen.Find(Material);
				if (!Known)
				{
					TArray<FSwappedMaterial>* List = nullptr;
					float Glow = 0.f;
					if (Material->GetScalarParameterValue(FHashedMaterialParameterInfo(HawkeyeTimeOfDay::WindowGlowParameter), Glow))
					{
						List = &WindowMaterials;
					}
					else if (const UMaterial* Base = Material->GetBaseMaterial(); Base && Base->GetFName() == HawkeyeTimeOfDay::EmissiveMaterialName
						&& Material->GetScalarParameterValue(FHashedMaterialParameterInfo(HawkeyeTimeOfDay::GlowParameter), Glow))
					{
						List = &EmissiveMaterials;
					}
					if (!List)
					{
						Seen.Add(Material, MakeTuple(false, INDEX_NONE));
						continue;
					}
					FSwappedMaterial Entry;
					Entry.Authored = Material;
					Entry.Glow = Glow;
					Known = &Seen.Add(Material, MakeTuple(List == &WindowMaterials, List->Add(Entry)));
				}
				if (Known->Value == INDEX_NONE)
				{
					continue;
				}
				TArray<FSwappedMaterial>& List = Known->Key ? WindowMaterials : EmissiveMaterials;
				List[Known->Value].Slots.Emplace(Mesh, Slot);
			}
		}
	}
}

void UTimeOfDaySubsystem::RestoreAuthored()
{
	if (UDirectionalLightComponent* Light = Sun.Get())
	{
		if (AActor* Owner = Light->GetOwner(); Owner && Owner->GetRootComponent() == Light)
		{
			Owner->SetActorRotation(Authored.SunRotation);
		}
		else
		{
			Light->SetWorldRotation(Authored.SunRotation);
		}
		Light->SetIntensity(Authored.SunLux);
		Light->bUseTemperature = Authored.bSunUseTemperature;
		Light->SetLightFColor(Authored.SunColor);
		Light->SetAtmosphereSunDiskColorScale(Authored.SunDiskColorScale);
	}
	if (USkyLightComponent* Light = SkyLight.Get())
	{
		Light->SetIntensity(Authored.SkyLightIntensity);
		// FColor -> linear -> FColor through the sRGB tables is exact, so this is the authored byte triple.
		Light->SetLightColor(FLinearColor(Authored.SkyLightColor));
	}
	if (UExponentialHeightFogComponent* Haze = Fog.Get())
	{
		Haze->SetFogDensity(Authored.FogDensity);
		Haze->SetFogInscatteringColor(Authored.FogInscattering);
		Haze->SetStartDistance(Authored.FogStartDistance);
	}
	if (APostProcessVolume* Volume = PostProcess.Get())
	{
		FPostProcessSettings& Settings = Volume->Settings;
		Settings.bOverride_AutoExposureBias = Authored.bOverrideExposureBias;
		Settings.AutoExposureBias = Authored.ExposureBias;
		Settings.bOverride_ColorGainShadows = Authored.bOverrideGainShadows;
		Settings.ColorGainShadows = Authored.GainShadows;
		Settings.bOverride_ColorGainHighlights = Authored.bOverrideGainHighlights;
		Settings.ColorGainHighlights = Authored.GainHighlights;
		Settings.bOverride_VignetteIntensity = Authored.bOverrideVignette;
		Settings.VignetteIntensity = Authored.Vignette;
	}
	if (AActor* Sphere = Stars.Get())
	{
		Sphere->SetActorHiddenInGame(Authored.bStarsHidden);
	}
	// The facades and glows get their own materials back, not a copy at the night's value. The lamps are
	// Apply's (ApplyLamps at 1).
	RestoreSwapped(WindowMaterials);
	RestoreSwapped(EmissiveMaterials);
}

void UTimeOfDaySubsystem::RestoreSwapped(const TArray<FSwappedMaterial>& Materials)
{
	for (const FSwappedMaterial& Swapped : Materials)
	{
		UMaterialInterface* Material = Swapped.Authored.Get();
		for (const TPair<TWeakObjectPtr<UMeshComponent>, int32>& Slot : Swapped.Slots)
		{
			UMeshComponent* Mesh = Slot.Key.Get();
			if (Mesh && Material && Mesh->GetMaterial(Slot.Value) != Material)
			{
				Mesh->SetMaterial(Slot.Value, Material);
			}
		}
	}
}

void UTimeOfDaySubsystem::ApplySwapped(const TArray<FSwappedMaterial>& Materials,
	TArray<TObjectPtr<UMaterialInstanceDynamic>>& Copies, FName Parameter, float Scale)
{
	// One copy per authored material, swapped into every slot that wore the original.
	Copies.SetNum(Materials.Num());
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		const FSwappedMaterial& Swapped = Materials[Index];
		UMaterialInterface* Material = Swapped.Authored.Get();
		if (!Material)
		{
			continue;
		}
		if (!Copies[Index])
		{
			Copies[Index] = UMaterialInstanceDynamic::Create(Material, this);
		}
		UMaterialInstanceDynamic* Copy = Copies[Index];
		Copy->SetScalarParameterValue(Parameter, Swapped.Glow * Scale);
		for (const TPair<TWeakObjectPtr<UMeshComponent>, int32>& Slot : Swapped.Slots)
		{
			UMeshComponent* Mesh = Slot.Key.Get();
			if (Mesh && Mesh->GetMaterial(Slot.Value) != Copy)
			{
				Mesh->SetMaterial(Slot.Value, Copy);
			}
		}
	}
}

void UTimeOfDaySubsystem::ApplyPreset(const FTimeOfDayPreset& Preset)
{
	if (UDirectionalLightComponent* Light = Sun.Get())
	{
		if (AActor* Owner = Light->GetOwner(); Owner && Owner->GetRootComponent() == Light)
		{
			Owner->SetActorRotation(Preset.SunRotation);
		}
		else
		{
			Light->SetWorldRotation(Preset.SunRotation);
		}
		Light->SetIntensity(Preset.SunLux);
		Light->bUseTemperature = false;
		Light->SetLightColor(Preset.SunColor);
		Light->SetAtmosphereSunDiskColorScale(Preset.SunDiskColorScale);
	}
	if (USkyLightComponent* Light = SkyLight.Get())
	{
		Light->SetIntensity(Preset.SkyLightIntensity);
		Light->SetLightColor(Preset.SkyLightColor);
	}
	if (UExponentialHeightFogComponent* Haze = Fog.Get())
	{
		Haze->SetFogDensity(Preset.FogDensity);
		Haze->SetFogInscatteringColor(Preset.FogInscattering);
		Haze->SetStartDistance(Preset.FogStartDistance);
	}
	if (APostProcessVolume* Volume = PostProcess.Get())
	{
		FPostProcessSettings& Settings = Volume->Settings;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = Preset.ExposureBias;
		Settings.bOverride_ColorGainShadows = true;
		Settings.ColorGainShadows = Preset.GainShadows;
		Settings.bOverride_ColorGainHighlights = true;
		Settings.ColorGainHighlights = Preset.GainHighlights;
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = Preset.Vignette;
	}
	if (AActor* Sphere = Stars.Get())
	{
		Sphere->SetActorHiddenInGame(!Preset.bStars);
	}
	ApplySwapped(WindowMaterials, WindowDimmers, HawkeyeTimeOfDay::WindowGlowParameter, Preset.WindowGlowScale * Preset.GlowScale);
	ApplySwapped(EmissiveMaterials, EmissiveCopies, HawkeyeTimeOfDay::GlowParameter, Preset.GlowScale);
}

FTimeOfDayPreset UTimeOfDaySubsystem::GetPreset(EHawkeyeTimeOfDay State)
{
	switch (State)
	{
	case EHawkeyeTimeOfDay::Day:
		return HawkeyeTimeOfDay::MakeDay();
	default:
	{
		FTimeOfDayPreset Night;
		Night.bUseAuthored = true;
		Night.bStars = true;
		return Night;
	}
	}
}

EHawkeyeTimeOfDay UTimeOfDaySubsystem::ResolveTimeOfDay(EHawkeyeTimeOfDay Stored)
{
	if (HawkeyeTimeOfDay::ConsoleOverride.IsSet())
	{
		return HawkeyeTimeOfDay::ConsoleOverride.GetValue();
	}
	const TOptional<EHawkeyeTimeOfDay>& CommandLine = HawkeyeTimeOfDay::CommandLineOverride();
	return CommandLine.IsSet() ? CommandLine.GetValue() : Stored;
}

bool UTimeOfDaySubsystem::ParseTimeOfDay(const FString& Text, EHawkeyeTimeOfDay& OutState)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.Equals(TEXT("Night"), ESearchCase::IgnoreCase))
	{
		OutState = EHawkeyeTimeOfDay::Night;
		return true;
	}
	if (Trimmed.Equals(TEXT("Day"), ESearchCase::IgnoreCase))
	{
		OutState = EHawkeyeTimeOfDay::Day;
		return true;
	}
	return false;
}

FText UTimeOfDaySubsystem::GetTimeOfDayName(EHawkeyeTimeOfDay State)
{
	switch (State)
	{
	case EHawkeyeTimeOfDay::Day:
		return NSLOCTEXT("Hawkeye", "TimeOfDayDay", "Day");
	default:
		return NSLOCTEXT("Hawkeye", "TimeOfDayNight", "Night");
	}
}

void UTimeOfDaySubsystem::SetConsoleOverride(TOptional<EHawkeyeTimeOfDay> Override)
{
	if (HawkeyeTimeOfDay::ConsoleOverride == Override)
	{
		return;
	}
	HawkeyeTimeOfDay::ConsoleOverride = Override;
	const FString What = Override.IsSet()
		? FString::Printf(TEXT("%s (not saved)"), *GetTimeOfDayName(Override.GetValue()).ToString())
		: FString(TEXT("back to the setting"));
	UE_LOG(LogHawkeye, Log, TEXT("hawkeye.TimeOfDay: %s."), *What);
	HawkeyeTimeOfDay::RefreshAllWorlds();
}

TOptional<EHawkeyeTimeOfDay> UTimeOfDaySubsystem::GetConsoleOverride()
{
	return HawkeyeTimeOfDay::ConsoleOverride;
}

TOptional<EHawkeyeTimeOfDay> UTimeOfDaySubsystem::SetCommandLineOverrideForTest(TOptional<EHawkeyeTimeOfDay> Override)
{
	TOptional<EHawkeyeTimeOfDay> Previous = HawkeyeTimeOfDay::CommandLineOverride();
	HawkeyeTimeOfDay::CommandLineOverride() = Override;
	return Previous;
}

TOptional<EHawkeyeTimeOfDay> UTimeOfDaySubsystem::ParseCommandLine(const TCHAR* CommandLine)
{
	FString Value;
	EHawkeyeTimeOfDay State = EHawkeyeTimeOfDay::Night;
	if (CommandLine && FParse::Value(CommandLine, TEXT("TimeOfDay="), Value) && ParseTimeOfDay(Value, State))
	{
		return State;
	}
	return {};
}
