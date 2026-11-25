#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CharacterLightRig.generated.h"

class AActor;
class APawn;
class UDirectionalLightComponent;
class UPrimitiveComponent;

/** What the character light rig asks of the scene. The numbers are right at ReferenceExposureBias (the night's). */
struct HAWKEYE_API FCharacterLightSettings
{
	bool bEnabled = true;

	/**
	 * Lux on a character, as the camera's exposure leaves the night. Exposure is pinned (EV100 1, bias +2 at night), so
	 * 3 lux is about 0.3 of display white on a 10 % grey: her costume (linear albedo 0.01 to 0.05) reads as dark cloth, not
	 * black, and the street (moon 0.6 lux, lamps) stays night.
	 */
	float KeyLux = 3.0f;
	float FillLux = 0.8f;
	float RimLux = 5.0f;

	/** The exposure bias the lux above are for, the day's bias, and the share of the night's on-screen brightness left by day. */
	float ReferenceExposureBias = 2.f;
	float DayExposureBias = 0.f;
	float DayRelative = 0.12f;

	FLinearColor KeyColor = FLinearColor(1.f, 0.93f, 0.84f);
	FLinearColor FillColor = FLinearColor(0.62f, 0.72f, 1.f);
	FLinearColor RimColor = FLinearColor(0.5f, 0.68f, 1.f);

	/**
	 * Where each light travels, relative to the camera's own rotation: yaw (a positive yaw sends it to the camera's right,
	 * so it comes from the left) and pitch (negative is down).
	 */
	float KeyYaw = 30.f;
	float KeyPitch = -35.f;
	float FillYaw = -45.f;
	float FillPitch = -5.f;
	float RimYaw = 210.f;
	float RimPitch = -25.f;
};

/** The rig's pure rules (tested in Hawkeye.CharLight.*). */
struct HAWKEYE_API FCharacterLightRules
{
	/**
	 * How much to scale the rig's lux at ExposureBias so a character looks the same through the pinned exposure: 2^(Reference - Bias)
	 * (a lower bias darkens the picture, so the lights rise to match), times the day's share of the night's brightness, blended in
	 * as the bias goes from the reference to the day's. 1 at the reference; never negative.
	 */
	static float ExposureScale(float ExposureBias, const FCharacterLightSettings& Settings);

	/** A light's rotation: the camera's turned by Yaw and Pitch (degrees). */
	static FRotator LightRotation(const FRotator& Camera, float Yaw, float Pitch);

	/** The settings from the hawkeye.CharLight.* variables. */
	static FCharacterLightSettings FromConsole();
};

/**
 * Keeps Kate, Clint, Lucky and the thugs readable at night (claude-docs/city-look.md, "Character light rig"): three
 * unshadowed directional lights (a warm key from the upper left of the view, a cool fill from the right, a cool rim from behind) that
 * turn with the camera and light ONLY pawns, through lighting channel 1: every pawn's meshes are put in channels 0 and 1, the lights
 * in channel 1 alone, so the street, walls and sky do not feel them. Their lux follows the exposure in force (FCharacterLightRules)
 * so the day does not blow out. The lights give nothing to Lumen's indirect light or the fog.
 *
 * hawkeye.CharLight.Enable, .Key, .Fill, .Rim, .DayScale tune it live; hawkeye.CharLight.Debug 1 logs what it did.
 */
UCLASS()
class HAWKEYE_API UCharacterLightRigSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UCharacterLightRigSubsystem* Get(const UObject* WorldContext);

	//~ Begin UWorldSubsystem / FTickableGameObject
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override;
	//~ End UWorldSubsystem / FTickableGameObject

	/** Puts every mesh of Pawn in lighting channels 0 and 1. Returns how many components changed. Runs for every pawn twice a second. */
	static int32 ApplyChannels(const APawn* Pawn);

	/** The lights' lux and colour at the rig's settings and ExposureBias, in the order key, fill, rim. Pure on its inputs. */
	static void ComputeLux(const FCharacterLightSettings& Settings, float ExposureBias, float OutLux[3]);

	UDirectionalLightComponent* GetLight(int32 Index) const;
	int32 GetPawnCount() const
	{
		return PawnsLit;
	}

private:
	void EnsureLights();
	void UpdateLights(const FCharacterLightSettings& Settings);
	void ScanPawns();

	UPROPERTY(Transient)
	TObjectPtr<AActor> Holder = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDirectionalLightComponent>> Lights;

	float ScanAccumulator = 1.f;
	int32 PawnsLit = 0;
	bool bLightsVisible = false;
	/** The holder could not be spawned: do not try again every frame. */
	bool bSpawnFailed = false;
};
