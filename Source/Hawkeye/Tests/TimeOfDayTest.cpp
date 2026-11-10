#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/AmbientSound.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameInstance.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMeshActor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Settings/HawkeyeSettings.h"
#include "Settings/HawkeyeSettingsSave.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/TimeOfDaySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeTimeOfDayTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Never the real slot. */
	static const TCHAR* TestSlot = TEXT("HawkeyeTimeOfDayAutomationTest");

	static void ClearSlot()
	{
		if (UGameplayStatics::DoesSaveGameExist(TestSlot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
		}
	}

	static UHawkeyeSettingsSubsystem* MakeSettings()
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GEngine);
		UHawkeyeSettingsSubsystem* Settings = NewObject<UHawkeyeSettingsSubsystem>(Outer);
		Settings->SlotNameOverride = TestSlot;
		return Settings;
	}

	/** Puts the command line's and the console's overrides back however the test ends. */
	struct FScopedOverrides
	{
		FScopedOverrides()
			: CommandLine(UTimeOfDaySubsystem::SetCommandLineOverrideForTest({}))
			, Console(UTimeOfDaySubsystem::GetConsoleOverride())
		{
			UTimeOfDaySubsystem::SetConsoleOverride({});
		}
		~FScopedOverrides()
		{
			UTimeOfDaySubsystem::SetCommandLineOverrideForTest(CommandLine);
			UTimeOfDaySubsystem::SetConsoleOverride(Console);
		}
		TOptional<EHawkeyeTimeOfDay> CommandLine;
		TOptional<EHawkeyeTimeOfDay> Console;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTimeOfDayRoundTrip, "Hawkeye.TimeOfDay.SettingRoundTripsAndMigrates",
	HawkeyeTimeOfDayTest::Flags)

bool FHawkeyeTimeOfDayRoundTrip::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTimeOfDayTest;
	const FScopedOverrides Overrides;
	ClearSlot();

	TestEqual(TEXT("The default is Night"), FHawkeyeSettings().TimeOfDay, EHawkeyeTimeOfDay::Night);
	// The time of day came in at version 6; later rows bump the version without touching it.
	TestTrue(TEXT("Settings version 6 or later (6 added the time of day)"), FHawkeyeSettings::CurrentVersion >= 6);

	UHawkeyeSettingsSubsystem* Writer = MakeSettings();
	Writer->Load();
	TestEqual(TEXT("A fresh slot is Night"), Writer->GetTimeOfDay(), EHawkeyeTimeOfDay::Night);
	Writer->SetTimeOfDay(EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("Set to Day"), Writer->GetTimeOfDay(), EHawkeyeTimeOfDay::Day);

	UHawkeyeSettingsSubsystem* Reader = MakeSettings();
	Reader->Load();
	TestEqual(TEXT("Day survives the save"), Reader->GetStoredSettings().TimeOfDay, EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("And is in force"), Reader->GetSettings().TimeOfDay, EHawkeyeTimeOfDay::Day);

	// A version 5 save (before the time of day) keeps what the player set and comes up Night.
	ClearSlot();
	UHawkeyeSettingsSave* Old = Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!TestNotNull(TEXT("A save object"), Old))
	{
		return false;
	}
	Old->Settings.LookSensitivity = 0.42f;
	Old->Settings.Difficulty = EHawkeyeDifficulty::Hard;
	Old->Settings.TimeOfDay = EHawkeyeTimeOfDay::Night;
	Old->Settings.Version = 5;
	UGameplayStatics::SaveGameToSlot(Old, TestSlot, 0);
	UHawkeyeSettingsSubsystem* Migrated = MakeSettings();
	Migrated->Load();
	TestEqual(TEXT("Version 5 keeps the sensitivity"), Migrated->GetLookSensitivity(), 0.42f);
	TestEqual(TEXT("And the difficulty"), Migrated->GetStoredSettings().Difficulty, EHawkeyeDifficulty::Hard);
	TestEqual(TEXT("And gets Night"), Migrated->GetStoredSettings().TimeOfDay, EHawkeyeTimeOfDay::Night);
	TestEqual(TEXT("And is stamped the current version"), Migrated->GetStoredSettings().Version, FHawkeyeSettings::CurrentVersion);

	// Version 4 is still too old to trust.
	Old->Settings.Version = 4;
	UGameplayStatics::SaveGameToSlot(Old, TestSlot, 0);
	AddExpectedError(TEXT("is version 4"), EAutomationExpectedErrorFlags::Contains, 1);
	UHawkeyeSettingsSubsystem* Stale = MakeSettings();
	Stale->Load();
	TestEqual(TEXT("Version 4 yields defaults"), Stale->GetLookSensitivity(), 0.2f);

	ClearSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTimeOfDayOverrides, "Hawkeye.TimeOfDay.OverridesDoNotSave", HawkeyeTimeOfDayTest::Flags)

bool FHawkeyeTimeOfDayOverrides::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTimeOfDayTest;
	const FScopedOverrides Overrides;
	ClearSlot();

	// The command line parse.
	TestTrue(TEXT("-TimeOfDay=Day"), UTimeOfDaySubsystem::ParseCommandLine(TEXT("Hawkeye.uproject -game -TimeOfDay=Day -log"))
		== TOptional<EHawkeyeTimeOfDay>(EHawkeyeTimeOfDay::Day));
	TestTrue(TEXT("-TimeOfDay=night, any case"), UTimeOfDaySubsystem::ParseCommandLine(TEXT("-timeofday=NIGHT"))
		== TOptional<EHawkeyeTimeOfDay>(EHawkeyeTimeOfDay::Night));
	TestFalse(TEXT("-TimeOfDay=Dusk names nothing"), UTimeOfDaySubsystem::ParseCommandLine(TEXT("-TimeOfDay=Dusk")).IsSet());
	TestFalse(TEXT("No argument, no override"), UTimeOfDaySubsystem::ParseCommandLine(TEXT("-game -log")).IsSet());

	UHawkeyeSettingsSubsystem* Settings = MakeSettings();
	Settings->Load();
	Settings->SetLookSensitivity(0.3f);

	// -TimeOfDay=Day: in force, but the slot still says Night.
	UTimeOfDaySubsystem::SetCommandLineOverrideForTest(EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("The command line's Day is in force"), Settings->GetTimeOfDay(), EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("GetSettings says Day"), Settings->GetSettings().TimeOfDay, EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("The stored choice is still Night"), Settings->GetStoredSettings().TimeOfDay, EHawkeyeTimeOfDay::Night);
	Settings->SetLookSensitivity(0.35f);
	UHawkeyeSettingsSubsystem* Reloaded = MakeSettings();
	Reloaded->Load();
	TestEqual(TEXT("A save made under the override writes Night"), Reloaded->GetStoredSettings().TimeOfDay,
		EHawkeyeTimeOfDay::Night);
	TestEqual(TEXT("With no game instance the override is in force too"),
		UHawkeyeSettingsSubsystem::GetCurrentSettings(nullptr).TimeOfDay, EHawkeyeTimeOfDay::Day);

	// hawkeye.TimeOfDay wins over the command line, and does not save either.
	UTimeOfDaySubsystem::SetConsoleOverride(EHawkeyeTimeOfDay::Night);
	TestEqual(TEXT("The console's Night beats the command line's Day"), Settings->GetTimeOfDay(), EHawkeyeTimeOfDay::Night);
	UTimeOfDaySubsystem::SetConsoleOverride({});
	TestEqual(TEXT("Dropped, the command line is back"), Settings->GetTimeOfDay(), EHawkeyeTimeOfDay::Day);

	UTimeOfDaySubsystem::SetCommandLineOverrideForTest({});
	TestEqual(TEXT("No overrides: the stored Night"), Settings->GetTimeOfDay(), EHawkeyeTimeOfDay::Night);

	ClearSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTimeOfDayRestoresNight, "Hawkeye.TimeOfDay.DayThenNightRestoresTheLevel",
	HawkeyeTimeOfDayTest::Flags)

bool FHawkeyeTimeOfDayRestoresNight::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTimeOfDayTest;
	const FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UTimeOfDaySubsystem* TimeOfDay = World ? World->GetSubsystem<UTimeOfDaySubsystem>() : nullptr;
	if (!TestNotNull(TEXT("The world has a time of day subsystem"), TimeOfDay))
	{
		return false;
	}

	// The generator's night, in miniature (generate_city.py, ensure_lighting_core).
	ADirectionalLight* Moon = Cast<ADirectionalLight>(TestWorld.SpawnActor(ADirectionalLight::StaticClass(),
		FVector(0.f, 0.f, 3000.f), FRotator(-30.f, -30.f, 0.f)));
	ASkyLight* Sky = Cast<ASkyLight>(TestWorld.SpawnActor(ASkyLight::StaticClass(), FVector(0.f, 0.f, 2000.f), FRotator::ZeroRotator));
	AExponentialHeightFog* Fog = Cast<AExponentialHeightFog>(TestWorld.SpawnActor(AExponentialHeightFog::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator));
	APostProcessVolume* Post = Cast<APostProcessVolume>(TestWorld.SpawnActor(APostProcessVolume::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator));
	AStaticMeshActor* Stars = Cast<AStaticMeshActor>(TestWorld.SpawnActor(AStaticMeshActor::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator));
	ASpotLight* Lamp = Cast<ASpotLight>(TestWorld.SpawnActor(ASpotLight::StaticClass(), FVector(500.f, 0.f, 700.f),
		FRotator(-90.f, 0.f, 0.f)));
	// An interior's own fog, which must never be touched.
	AExponentialHeightFog* InteriorFog = Cast<AExponentialHeightFog>(TestWorld.SpawnActor(AExponentialHeightFog::StaticClass(),
		FVector(0.f, 0.f, -500.f), FRotator::ZeroRotator));
	if (!Moon || !Sky || !Fog || !Post || !Stars || !Lamp || !InteriorFog)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	InteriorFog->Tags.Add(TEXT("Interior"));
	InteriorFog->GetComponent()->SetFogDensity(0.05f);

	UDirectionalLightComponent* MoonLight = Cast<UDirectionalLightComponent>(Moon->GetLightComponent());
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->SetIntensity(0.3f);
	MoonLight->SetLightFColor(FColor(140, 165, 255));
	MoonLight->SetAtmosphereSunDiskColorScale(FLinearColor(0.9f, 0.93f, 1.f));
	USkyLightComponent* SkyLight = Sky->GetLightComponent();
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetIntensity(3.f);
	SkyLight->SetLightColor(FLinearColor(FColor(250, 248, 255)));
	UExponentialHeightFogComponent* Haze = Fog->GetComponent();
	Haze->SetFogDensity(0.008f);
	Haze->SetFogInscatteringColor(FLinearColor(0.035f, 0.035f, 0.06f));
	Haze->SetStartDistance(0.f);
	Post->bUnbound = true;
	Post->Settings.bOverride_AutoExposureBias = true;
	Post->Settings.AutoExposureBias = 2.f;
	Post->Settings.bOverride_ColorGainShadows = true;
	Post->Settings.ColorGainShadows = FVector4(0.92f, 0.86f, 1.10f, 1.f);
	Post->Settings.bOverride_ColorGainHighlights = true;
	Post->Settings.ColorGainHighlights = FVector4(1.05f, 1.f, 0.88f, 1.f);
	Post->Settings.bOverride_VignetteIntensity = true;
	Post->Settings.VignetteIntensity = 0.3f;
	Stars->Tags.Add(TEXT("CityNightSky"));
	Lamp->Tags.Add(TEXT("CityLamp"));
	Lamp->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Lamp->GetLightComponent()->SetIntensity(1100.f);

	const FRotator NightRotation = MoonLight->GetComponentRotation();
	const FColor NightColor = MoonLight->LightColor;
	const FColor NightSkyColor = SkyLight->LightColor;
	const FLinearColor NightFogColor = Haze->FogInscatteringLuminance;
	const FVector4 NightShadows = Post->Settings.ColorGainShadows;

	TestEqual(TEXT("Nothing is applied before the first call"), TimeOfDay->GetApplied(), EHawkeyeTimeOfDay::Night);

	// Day.
	TimeOfDay->Apply(EHawkeyeTimeOfDay::Day);
	const FTimeOfDayPreset Day = UTimeOfDaySubsystem::GetPreset(EHawkeyeTimeOfDay::Day);
	TestFalse(TEXT("Day is a real row, not the authored night"), Day.bUseAuthored);
	TestEqual(TEXT("Day applied"), TimeOfDay->GetApplied(), EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("The sun at Day's lux"), MoonLight->Intensity, Day.SunLux);
	TestTrue(TEXT("Brighter than the moon"), MoonLight->Intensity > 1.f);
	TestTrue(TEXT("The sun at Day's angle"), MoonLight->GetComponentRotation().Equals(Day.SunRotation, 0.01f));
	TestEqual(TEXT("The sky light at Day's intensity"), SkyLight->Intensity, Day.SkyLightIntensity);
	TestEqual(TEXT("Lighter fog"), Haze->FogDensity, Day.FogDensity);
	TestEqual(TEXT("Day's exposure"), Post->Settings.AutoExposureBias, Day.ExposureBias);
	TestTrue(TEXT("No purple in the shadows"), Post->Settings.ColorGainShadows.Equals(FVector4(1.f, 1.f, 1.f, 1.f), 0.001f));
	TestTrue(TEXT("No stars by day"), Stars->IsHidden());
	TestEqual(TEXT("Lamps off"), Lamp->GetLightComponent()->Intensity, 1100.f * Day.LampScale);
	TestEqual(TEXT("The interior's fog is untouched"), InteriorFog->GetComponent()->FogDensity, 0.05f);
	TestEqual(TEXT("One lamp light found"), TimeOfDay->GetLampLightCount(), 1);

	// And Night again: exactly what the level had.
	TimeOfDay->Apply(EHawkeyeTimeOfDay::Night);
	TestEqual(TEXT("Night applied"), TimeOfDay->GetApplied(), EHawkeyeTimeOfDay::Night);
	TestEqual(TEXT("Moon lux restored"), MoonLight->Intensity, 0.3f);
	TestTrue(TEXT("Moon angle restored"), MoonLight->GetComponentRotation().Equals(NightRotation, 0.001f));
	TestTrue(TEXT("Moon colour restored, byte for byte"), MoonLight->LightColor == NightColor);
	TestTrue(TEXT("Moon disc restored"), MoonLight->GetAtmosphereSunDiskColorScale().Equals(FLinearColor(0.9f, 0.93f, 1.f)));
	TestEqual(TEXT("Sky light restored"), SkyLight->Intensity, 3.f);
	TestTrue(TEXT("Sky light colour restored, byte for byte"), SkyLight->LightColor == NightSkyColor);
	TestEqual(TEXT("Fog density restored"), Haze->FogDensity, 0.008f);
	TestTrue(TEXT("Fog colour restored"), Haze->FogInscatteringLuminance.Equals(NightFogColor));
	TestEqual(TEXT("Fog start restored"), Haze->StartDistance, 0.f);
	TestEqual(TEXT("Exposure restored"), Post->Settings.AutoExposureBias, 2.f);
	TestTrue(TEXT("Purple shadows restored"), Post->Settings.ColorGainShadows.Equals(NightShadows, 0.0001f));
	TestTrue(TEXT("Cream highlights restored"),
		Post->Settings.ColorGainHighlights.Equals(FVector4(1.05f, 1.f, 0.88f, 1.f), 0.0001f));
	TestEqual(TEXT("Vignette restored"), Post->Settings.VignetteIntensity, 0.3f);
	TestFalse(TEXT("Stars back"), Stars->IsHidden());
	TestEqual(TEXT("Lamp back to 1100 lm"), Lamp->GetLightComponent()->Intensity, 1100.f);

	// A second round trip lands in the same place (the night is recorded once, not re-read from a day).
	TimeOfDay->Apply(EHawkeyeTimeOfDay::Day);
	TimeOfDay->Apply(EHawkeyeTimeOfDay::Night);
	TestEqual(TEXT("Twice round: moon lux"), MoonLight->Intensity, 0.3f);
	TestEqual(TEXT("Twice round: lamp"), Lamp->GetLightComponent()->Intensity, 1100.f);
	return true;
}

namespace HawkeyeTimeOfDayTest
{
	static UMaterialInterface* LoadMaterial(const TCHAR* Path)
	{
		return LoadObject<UMaterialInterface>(nullptr, Path);
	}

	/** A cube wearing Material, tagged Tag (none if empty), movable so it takes materials at runtime. */
	static AStaticMeshActor* SpawnGlowMesh(const FHawkeyeTestWorld& TestWorld, const FVector& Location, UMaterialInterface* Material,
		FName Tag)
	{
		AStaticMeshActor* Actor = Cast<AStaticMeshActor>(TestWorld.SpawnActor(AStaticMeshActor::StaticClass(), Location, FRotator::ZeroRotator));
		if (!Actor)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetMaterial(0, Material);
		if (!Tag.IsNone())
		{
			Actor->Tags.Add(Tag);
		}
		return Actor;
	}

	static float Scalar(const UMaterialInterface* Material, FName Parameter)
	{
		float Value = -1.f;
		if (Material)
		{
			Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Parameter), Value);
		}
		return Value;
	}

	static float HeadGlow(const AStaticMeshActor* Head)
	{
		return Scalar(Head->GetStaticMeshComponent()->GetMaterial(0), TEXT("Intensity"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTimeOfDayEmpLamps, "Hawkeye.TimeOfDay.EmpAndLampsDoNotFight", HawkeyeTimeOfDayTest::Flags)

bool FHawkeyeTimeOfDayEmpLamps::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTimeOfDayTest;
	const FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UTimeOfDaySubsystem* TimeOfDay = World ? World->GetSubsystem<UTimeOfDaySubsystem>() : nullptr;
	UArrowEffectsSubsystem* Effects = World ? World->GetSubsystem<UArrowEffectsSubsystem>() : nullptr;
	UMaterialInterface* LampHead = LoadMaterial(TEXT("/Game/Materials/MI_StreetLamp.MI_StreetLamp"));
	if (!TestNotNull(TEXT("Time of day"), TimeOfDay) || !TestNotNull(TEXT("Arrow effects"), Effects)
		|| !TestNotNull(TEXT("MI_StreetLamp"), LampHead))
	{
		return false;
	}
	const float AuthoredGlow = Scalar(LampHead, TEXT("Intensity"));
	TestTrue(TEXT("The lamp head glows as authored"), AuthoredGlow > 0.f);

	// One lamp as the generator builds it: the light, the head and the buzz, three actors tagged CityLamp.
	ASpotLight* Light = Cast<ASpotLight>(TestWorld.SpawnActor(ASpotLight::StaticClass(), FVector(300.f, 0.f, 690.f),
		FRotator(-90.f, 0.f, 0.f)));
	AStaticMeshActor* Head = SpawnGlowMesh(TestWorld, FVector(300.f, 0.f, 700.f), LampHead, UArrowEffectsSubsystem::LampTag);
	AAmbientSound* Buzz = Cast<AAmbientSound>(TestWorld.SpawnActor(AAmbientSound::StaticClass(), FVector(300.f, 0.f, 300.f),
		FRotator::ZeroRotator));
	if (!Light || !Head || !Buzz)
	{
		AddError(TEXT("Could not spawn the lamp."));
		return false;
	}
	Light->Tags.Add(UArrowEffectsSubsystem::LampTag);
	Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Light->GetLightComponent()->SetIntensity(1100.f);
	Buzz->Tags.Add(UArrowEffectsSubsystem::LampTag);
	ULightComponent* Lamp = Light->GetLightComponent();

	// 1. Dark by EMP at night, before the time of day has recorded anything; Day picked while it is out.
	TestEqual(TEXT("The EMP reaches all three"), Effects->DisableLampsInRadius(FVector::ZeroVector, 600.f, 20.f), 3);
	TestEqual(TEXT("EMP: head dark"), HeadGlow(Head), 0.f);
	TimeOfDay->Apply(EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("The head is recorded at its authored glow, not the EMP's 0"), TimeOfDay->GetLampGlowCount(), 1);
	TestEqual(TEXT("The buzz is recorded"), TimeOfDay->GetLampSoundCount(), 1);
	TestEqual(TEXT("Day while dark: head still dark"), HeadGlow(Head), 0.f);
	TestFalse(TEXT("Day while dark: light still hidden"), Lamp->IsVisible());
	Effects->Tick(20.1f);
	TestFalse(TEXT("The outage is over"), Effects->IsLampDisabled(Head));
	TestTrue(TEXT("The EMP gives the light its visibility back"), Lamp->IsVisible());
	TestEqual(TEXT("But by day it is off"), Lamp->Intensity, 0.f);
	TestEqual(TEXT("And the head stays unlit, not back at the night's glow"), HeadGlow(Head), 0.f);
	TestFalse(TEXT("No buzz by day"), TimeOfDay->IsLampLit(Buzz));

	TimeOfDay->Apply(EHawkeyeTimeOfDay::Night);
	TestEqual(TEXT("Night: the light is back"), Lamp->Intensity, 1100.f);
	TestEqual(TEXT("Night: the head glows as authored"), HeadGlow(Head), AuthoredGlow);
	TestTrue(TEXT("Night: the buzz plays"), TimeOfDay->IsLampLit(Buzz));

	// 2. Dark by EMP by day; Night picked while it is out.
	TimeOfDay->Apply(EHawkeyeTimeOfDay::Day);
	Effects->DisableLampsInRadius(FVector::ZeroVector, 600.f, 20.f);
	TimeOfDay->Apply(EHawkeyeTimeOfDay::Night);
	TestFalse(TEXT("Night while dark: the light stays hidden"), Lamp->IsVisible());
	TestEqual(TEXT("Night while dark: the head stays dark"), HeadGlow(Head), 0.f);
	TestFalse(TEXT("Night while dark: no buzz"), TimeOfDay->IsLampLit(Buzz));
	TestFalse(TEXT("Night while dark: the head is not lit"), TimeOfDay->IsLampLit(Head));
	Effects->Tick(20.1f);
	TestTrue(TEXT("Outage over at night: the light shows"), Lamp->IsVisible());
	TestEqual(TEXT("At full"), Lamp->Intensity, 1100.f);
	TestEqual(TEXT("The head glows again"), HeadGlow(Head), AuthoredGlow);
	TestTrue(TEXT("And buzzes"), TimeOfDay->IsLampLit(Buzz));

	// 3. A plain night EMP once the time of day owns the lamp.
	Effects->DisableLampsInRadius(FVector::ZeroVector, 600.f, 20.f);
	TestEqual(TEXT("Night EMP: head dark"), HeadGlow(Head), 0.f);
	Effects->Tick(20.1f);
	TestEqual(TEXT("Night EMP over: head back"), HeadGlow(Head), AuthoredGlow);
	TestTrue(TEXT("Night EMP over: light back"), Lamp->IsVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTimeOfDayGlow, "Hawkeye.TimeOfDay.GlowScalesByDay", HawkeyeTimeOfDayTest::Flags)

bool FHawkeyeTimeOfDayGlow::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTimeOfDayTest;
	const FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	UTimeOfDaySubsystem* TimeOfDay = World ? World->GetSubsystem<UTimeOfDaySubsystem>() : nullptr;
	UMaterialInterface* Beacon = LoadMaterial(TEXT("/Game/Materials/MI_ObjectiveBeacon.MI_ObjectiveBeacon"));
	UMaterialInterface* Facade = LoadMaterial(TEXT("/Game/Materials/MI_Facade_BrickRed.MI_Facade_BrickRed"));
	UMaterialInterface* Emissive = LoadMaterial(TEXT("/Game/Materials/M_Emissive.M_Emissive"));
	if (!TestNotNull(TEXT("Time of day"), TimeOfDay) || !TestNotNull(TEXT("MI_ObjectiveBeacon"), Beacon)
		|| !TestNotNull(TEXT("MI_Facade_BrickRed"), Facade) || !TestNotNull(TEXT("M_Emissive"), Emissive))
	{
		return false;
	}
	const FTimeOfDayPreset Day = UTimeOfDaySubsystem::GetPreset(EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("Night's glow scale is 1"), UTimeOfDaySubsystem::GetPreset(EHawkeyeTimeOfDay::Night).GlowScale, 1.f);
	TestTrue(TEXT("Day's is about 3"), FMath::IsNearlyEqual(Day.GlowScale, 3.f, 0.5f));

	const float BeaconGlow = Scalar(Beacon, TEXT("Intensity"));
	const float WindowGlow = Scalar(Facade, TEXT("WindowGlow"));
	AStaticMeshActor* BeaconTop = SpawnGlowMesh(TestWorld, FVector(0.f, 0.f, 1000.f), Beacon, TEXT("CityBeacon"));
	AStaticMeshActor* Building = SpawnGlowMesh(TestWorld, FVector(1000.f, 0.f, 0.f), Facade, TEXT("CityBuilding"));
	AStaticMeshActor* InteriorSign = SpawnGlowMesh(TestWorld, FVector(0.f, 1000.f, 0.f), Beacon, TEXT("Interior"));
	// A class's own glow (a pedestal cap, a target face, a checkpoint ring), set through the subsystem.
	AStaticMeshActor* Cap = SpawnGlowMesh(TestWorld, FVector(-1000.f, 0.f, 0.f), Emissive, NAME_None);
	if (!BeaconTop || !Building || !InteriorSign || !Cap)
	{
		AddError(TEXT("Could not spawn the glow scene."));
		return false;
	}
	UMaterialInstanceDynamic* CapGlow = Cap->GetStaticMeshComponent()->CreateDynamicMaterialInstance(0);
	UTimeOfDaySubsystem::SetGlow(Cap, CapGlow, 0.35f);
	TestEqual(TEXT("Night: a set glow is its night value"), Scalar(CapGlow, TEXT("Intensity")), 0.35f);
	TestEqual(TEXT("And the subsystem keeps it"), TimeOfDay->GetRegisteredGlowCount(), 1);

	TimeOfDay->Apply(EHawkeyeTimeOfDay::Day);
	TestEqual(TEXT("One beacon material found"), TimeOfDay->GetEmissiveMaterialCount(), 1);
	TestEqual(TEXT("One facade material found"), TimeOfDay->GetWindowMaterialCount(), 1);
	UMaterialInterface* DayBeacon = BeaconTop->GetStaticMeshComponent()->GetMaterial(0);
	TestTrue(TEXT("Day: the beacon wears a copy"), DayBeacon != Beacon);
	TestTrue(TEXT("Day: the beacon glows GlowScale times brighter"),
		FMath::IsNearlyEqual(Scalar(DayBeacon, TEXT("Intensity")), BeaconGlow * Day.GlowScale, 1e-3f));
	TestTrue(TEXT("Day: the windows at WindowGlowScale times GlowScale"),
		FMath::IsNearlyEqual(Scalar(Building->GetStaticMeshComponent()->GetMaterial(0), TEXT("WindowGlow")),
			WindowGlow * Day.WindowGlowScale * Day.GlowScale, 1e-4f));
	TestTrue(TEXT("Day: the set glow is rescaled"), FMath::IsNearlyEqual(Scalar(CapGlow, TEXT("Intensity")), 0.35f * Day.GlowScale, 1e-4f));
	TestTrue(TEXT("Day: an interior's glow is untouched"), InteriorSign->GetStaticMeshComponent()->GetMaterial(0) == Beacon);

	// A glow set by day comes up scaled at once, and a second set replaces the first's night value.
	UTimeOfDaySubsystem::SetGlow(Cap, CapGlow, 0.5f);
	TestTrue(TEXT("Set by day: scaled at once"), FMath::IsNearlyEqual(Scalar(CapGlow, TEXT("Intensity")), 0.5f * Day.GlowScale, 1e-4f));
	TestEqual(TEXT("Still one registered glow"), TimeOfDay->GetRegisteredGlowCount(), 1);
	TestTrue(TEXT("GetGlowScale says Day's"), FMath::IsNearlyEqual(UTimeOfDaySubsystem::GetGlowScale(Cap), Day.GlowScale));

	TimeOfDay->Apply(EHawkeyeTimeOfDay::Night);
	TestTrue(TEXT("Night: the beacon's own material is back"), BeaconTop->GetStaticMeshComponent()->GetMaterial(0) == Beacon);
	TestTrue(TEXT("Night: the facade's own material is back"), Building->GetStaticMeshComponent()->GetMaterial(0) == Facade);
	TestEqual(TEXT("Night: the set glow at its latest night value"), Scalar(CapGlow, TEXT("Intensity")), 0.5f);
	TestEqual(TEXT("GetGlowScale says 1"), UTimeOfDaySubsystem::GetGlowScale(Cap), 1.f);

	// A glow whose material has gone (its ring destroyed) is dropped when the next one registers.
	{
		UMaterialInstanceDynamic* Gone = UMaterialInstanceDynamic::Create(Emissive, GetTransientPackage());
		UTimeOfDaySubsystem::SetGlow(Cap, Gone, 0.2f);
		TestEqual(TEXT("Two registered glows"), TimeOfDay->GetRegisteredGlowCount(), 2);
		Gone->MarkAsGarbage();
		UTimeOfDaySubsystem::SetGlow(Cap, CapGlow, 0.5f);
		TestEqual(TEXT("The gone one is dropped on the next register"), TimeOfDay->GetRegisteredGlowCount(), 1);
	}
	return true;
}

#endif
