#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/HawkeyeAudioTypes.h"
#include "HawkeyeAudioSubsystem.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundBase;

/**
 * Where every game sound is played from (claude-docs/gameplay-semantics.md, "Audio"). Components
 * hold their sounds as soft pointers, set on the Blueprints by create_blueprints.py; these helpers
 * load them synchronously the first time (the standalone game has nothing resident, testing.md 2b),
 * keep them resident for the level so a second shot does not hitch, and log every trigger at
 * Verbose on LogHawkeye so a run can be audited:
 *
 *   -LogCmds="LogHawkeye Verbose"   then grep the log for "Sound:"
 *
 * An unset sound, a world with no audio device (tests, -nullrhi) or no world at all plays nothing
 * and is not an error. The MetaSounds themselves are built by Tools/Editor/create_audio.py.
 */
UCLASS()
class HAWKEYE_API UHawkeyeAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A one-shot at Location with the sound's own attenuation. Returns the component or null. */
	static UAudioComponent* PlayAt(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound,
		const FVector& Location, const TCHAR* Event, float VolumeMultiplier = 1.f);

	/** Attached to AttachTo (and stopped with it): loops, and sounds that travel with an actor. */
	static UAudioComponent* PlayAttached(const TSoftObjectPtr<USoundBase>& Sound, USceneComponent* AttachTo,
		const TCHAR* Event, float VolumeMultiplier = 1.f);

	/** Non-spatial, and heard through pause (UGameplayStatics::SpawnSound2D marks it a UI sound). */
	static UAudioComponent* Play2D(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound,
		const TCHAR* Event, float VolumeMultiplier = 1.f);

	/** An interface sound, through the local AHawkeyePlayerController's defaults. */
	static void PlayUI(const UObject* WorldContext, EHawkeyeUISound Sound);

	/** Stops a loop started by PlayAttached and clears the handle. Safe on null. */
	static void StopLoop(TObjectPtr<UAudioComponent>& Loop, const TCHAR* Event);

	/** Loads Sound (synchronously) and keeps it for the level. Null when unset or missing. */
	USoundBase* Resolve(const TSoftObjectPtr<USoundBase>& Sound);

	/**
	 * Streams every sound under Path (/Game/Audio) in the background and keeps each for the level
	 * once it lands, so the first footstep, zip or thug bark does not load a MetaSound mid-play.
	 * The game mode starts it at the end of its BeginPlay.
	 */
	void PreloadFolderAsync(const FString& Path);

	/** How many sounds have been triggered in this world, for tests and the log. */
	int32 GetTriggerCount() const { return TriggerCount; }

private:
	static UHawkeyeAudioSubsystem* Find(const UObject* WorldContext);

	void NoteTrigger(const TCHAR* Event, const USoundBase* Sound, const UObject* Context, const FVector& Location);

	/** The background preload in flight, if any. */
	TSharedPtr<struct FStreamableHandle> PreloadHandle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> Resident;

	int32 TriggerCount = 0;
};
