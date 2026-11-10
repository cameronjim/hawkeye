#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HawkeyeVolumeSubsystem.generated.h"

/**
 * Keeps the settings mix and its sound classes loaded for as long as the game runs. The audio device
 * holds raw pointers to every class a pushed mix overrides, and it outlives any one world; a class
 * collected under it is read after free (the engine logs "Sound class '...' does not exist" with a
 * garbage name). AHawkeyePlayerController hands its mix and classes here before it pushes them.
 */
UCLASS()
class HAWKEYE_API UHawkeyeVolumeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem for WorldContext's game instance, or null (tests, no game instance). */
	static UHawkeyeVolumeSubsystem* Get(const UObject* WorldContext);

	/** Keeps Object alive until the game instance shuts down. Null is ignored. */
	void Hold(UObject* Object);

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Held;
};
