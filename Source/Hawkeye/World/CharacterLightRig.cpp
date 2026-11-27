#include "World/CharacterLightRig.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Info.h"
#include "GameFramework/Pawn.h"
#include "Hawkeye.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "World/TimeOfDaySubsystem.h"

static TAutoConsoleVariable<int32> CVarCharLightEnable(TEXT("hawkeye.CharLight.Enable"), 1,
	TEXT("1 (default) lights the characters with the key, fill and rim rig (claude-docs/city-look.md, Character light rig); "
		 "0 turns it off."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCharLightKey(TEXT("hawkeye.CharLight.Key"), 3.0f,
	TEXT("The key light's lux on characters at the night's exposure."), ECVF_Default);

static TAutoConsoleVariable<float> CVarCharLightFill(TEXT("hawkeye.CharLight.Fill"), 0.8f,
	TEXT("The fill light's lux on characters at the night's exposure."), ECVF_Default);

static TAutoConsoleVariable<float> CVarCharLightRim(TEXT("hawkeye.CharLight.Rim"), 5.0f,
	TEXT("The rim light's lux on characters at the night's exposure."), ECVF_Default);

static TAutoConsoleVariable<float> CVarCharLightDayScale(TEXT("hawkeye.CharLight.DayScale"), 0.12f,
	TEXT("The share of the night's on-screen brightness the rig keeps by day (after exposure compensation)."), ECVF_Default);

static TAutoConsoleVariable<int32> CVarCharLightDebug(TEXT("hawkeye.CharLight.Debug"), 0,
	TEXT("1 logs the rig's lux and how many pawns it lit, once a second."), ECVF_Default);

namespace HawkeyeCharLight
{
	static constexpr float ScanSeconds = 0.5f;
}

// --- Rules ---------------------------------------------------------------------------------------

float FCharacterLightRules::ExposureScale(float ExposureBias, const FCharacterLightSettings& Settings)
{
	const float Span = Settings.ReferenceExposureBias - Settings.DayExposureBias;
	const float Blend = FMath::Abs(Span) > KINDA_SMALL_NUMBER
		? FMath::Clamp((Settings.ReferenceExposureBias - ExposureBias) / Span, 0.f, 1.f)
		: 0.f;
	const float Compensation = FMath::Pow(2.f, Settings.ReferenceExposureBias - ExposureBias);
	return FMath::Max(0.f, Compensation * FMath::Lerp(1.f, Settings.DayRelative, Blend));
}

FRotator FCharacterLightRules::LightRotation(const FRotator& Camera, float Yaw, float Pitch)
{
	return (Camera.Quaternion() * FRotator(Pitch, Yaw, 0.f).Quaternion()).Rotator();
}

FCharacterLightSettings FCharacterLightRules::FromConsole()
{
	FCharacterLightSettings Settings;
	Settings.bEnabled = CVarCharLightEnable.GetValueOnGameThread() != 0;
	Settings.KeyLux = FMath::Max(0.f, CVarCharLightKey.GetValueOnGameThread());
	Settings.FillLux = FMath::Max(0.f, CVarCharLightFill.GetValueOnGameThread());
	Settings.RimLux = FMath::Max(0.f, CVarCharLightRim.GetValueOnGameThread());
	Settings.DayRelative = FMath::Max(0.f, CVarCharLightDayScale.GetValueOnGameThread());
	return Settings;
}

// --- Subsystem -----------------------------------------------------------------------------------

UCharacterLightRigSubsystem* UCharacterLightRigSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = (GEngine && WorldContext)
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UCharacterLightRigSubsystem>() : nullptr;
}

bool UCharacterLightRigSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCharacterLightRigSubsystem::Deinitialize()
{
	if (IsValid(Holder))
	{
		Holder->Destroy();
	}
	Holder = nullptr;
	Lights.Reset();
	Super::Deinitialize();
}

TStatId UCharacterLightRigSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCharacterLightRigSubsystem, STATGROUP_Tickables);
}

bool UCharacterLightRigSubsystem::IsTickableWhenPaused() const
{
	// A paused frame keeps its lights where they are; nothing to advance.
	return false;
}

UDirectionalLightComponent* UCharacterLightRigSubsystem::GetLight(int32 Index) const
{
	return Lights.IsValidIndex(Index) ? Lights[Index].Get() : nullptr;
}

void UCharacterLightRigSubsystem::ComputeLux(const FCharacterLightSettings& Settings, float ExposureBias, float OutLux[3])
{
	const float Scale = Settings.bEnabled ? FCharacterLightRules::ExposureScale(ExposureBias, Settings) : 0.f;
	OutLux[0] = Settings.KeyLux * Scale;
	OutLux[1] = Settings.FillLux * Scale;
	OutLux[2] = Settings.RimLux * Scale;
}

int32 UCharacterLightRigSubsystem::ApplyChannels(const APawn* Pawn)
{
	if (!Pawn)
	{
		return 0;
	}
	int32 Changed = 0;
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Pawn);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		// Meshes only: the capsule and the traversal helpers draw nothing, and a light on a hidden driver costs nothing.
		if (!Primitive || !Primitive->IsA<UMeshComponent>() || Primitive->LightingChannels.bChannel1)
		{
			continue;
		}
		Primitive->SetLightingChannels(/*bChannel0=*/true, /*bChannel1=*/true, /*bChannel2=*/Primitive->LightingChannels.bChannel2);
		++Changed;
	}
	return Changed;
}

void UCharacterLightRigSubsystem::EnsureLights()
{
	if ((IsValid(Holder) && Lights.Num() == 3) || bSpawnFailed)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Holder)
	{
		bSpawnFailed = true;
		UE_LOG(LogHawkeye, Warning, TEXT("Character light rig: the holder actor could not be spawned; no rig."));
		return;
	}
#if WITH_EDITOR
	Holder->SetActorLabel(TEXT("CharacterLightRig"));
#endif
	USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
	Holder->SetRootComponent(Root);
	Root->RegisterComponent();

	Lights.Reset();
	static const TCHAR* Names[3] = { TEXT("Key"), TEXT("Fill"), TEXT("Rim") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(Holder, Names[Index]);
		Light->SetupAttachment(Root);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetAtmosphereSunLight(false);
		Light->SetCastShadows(false);
		Light->SetIndirectLightingIntensity(0.f);
		Light->SetVolumetricScatteringIntensity(0.f);
		Light->bUseTemperature = false;
		// Characters only: channel 1, and not the default channel 0 the street is in.
		Light->SetLightingChannels(/*bChannel0=*/false, /*bChannel1=*/true, /*bChannel2=*/false);
		Light->RegisterComponent();
		Light->SetVisibility(false);
		Lights.Add(Light);
	}
	bLightsVisible = false;
	// Forward shading, translucency, water and volumetric fog take one directional light: the level's sun/moon must win over
	// the rig's three (priority 0), or the engine prints "Multiple directional lights are competing..." across the screen.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It == Holder)
		{
			continue;
		}
		TInlineComponentArray<UDirectionalLightComponent*> Suns(*It);
		for (UDirectionalLightComponent* Sun : Suns)
		{
			if (Sun->ForwardShadingPriority < 1)
			{
				Sun->SetForwardShadingPriority(1);
			}
		}
	}
	bLightsVisible = false;
	UE_LOG(LogHawkeye, Log, TEXT("Character light rig: three lights in lighting channel 1."));
}

void UCharacterLightRigSubsystem::ScanPawns()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	PawnsLit = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		ApplyChannels(*It);
		++PawnsLit;
	}
}

void UCharacterLightRigSubsystem::UpdateLights(const FCharacterLightSettings& Settings)
{
	UWorld* World = GetWorld();
	const APlayerCameraManager* Camera = World ? UGameplayStatics::GetPlayerCameraManager(World, 0) : nullptr;
	if (Lights.Num() != 3 || !Camera)
	{
		return;
	}
	const FRotator View = Camera->GetCameraRotation();
	const float Bias = UTimeOfDaySubsystem::GetExposureBiasInForce(World);
	float Lux[3];
	ComputeLux(Settings, Bias, Lux);

	const FLinearColor Colours[3] = { Settings.KeyColor, Settings.FillColor, Settings.RimColor };
	const FRotator Turns[3] = {
		FCharacterLightRules::LightRotation(View, Settings.KeyYaw, Settings.KeyPitch),
		FCharacterLightRules::LightRotation(View, Settings.FillYaw, Settings.FillPitch),
		FCharacterLightRules::LightRotation(View, Settings.RimYaw, Settings.RimPitch),
	};
	const bool bAny = Settings.bEnabled;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UDirectionalLightComponent* Light = Lights[Index];
		if (!Light)
		{
			continue;
		}
		const bool bOn = bAny && Lux[Index] > KINDA_SMALL_NUMBER;
		if (Light->IsVisible() != bOn)
		{
			Light->SetVisibility(bOn);
		}
		if (!bOn)
		{
			continue;
		}
		Light->SetWorldRotation(Turns[Index]);
		Light->SetLightColor(Colours[Index], /*bSRGB=*/false);
		Light->SetIntensity(Lux[Index]);
	}
	bLightsVisible = bAny;

	if (CVarCharLightDebug.GetValueOnGameThread() != 0)
	{
		static double LastLog = 0.0;
		const double Now = World->GetTimeSeconds();
		if (Now - LastLog >= 1.0)
		{
			LastLog = Now;
			UE_LOG(LogHawkeye, Log, TEXT("Character light rig: bias %.2f, lux key %.2f fill %.2f rim %.2f, %d pawns in channel 1."),
				Bias, Lux[0], Lux[1], Lux[2], PawnsLit);
		}
	}
}

void UCharacterLightRigSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	const FCharacterLightSettings Settings = FCharacterLightRules::FromConsole();
	if (!Settings.bEnabled && !bLightsVisible)
	{
		return;
	}
	EnsureLights();
	ScanAccumulator += DeltaTime;
	if (ScanAccumulator >= HawkeyeCharLight::ScanSeconds)
	{
		ScanAccumulator = 0.f;
		ScanPawns();
	}
	UpdateLights(Settings);
}
