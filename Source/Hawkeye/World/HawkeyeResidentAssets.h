#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HawkeyeResidentAssets.generated.h"

/**
 * Keeps the heavy content every map shares loaded across level changes: the character classes in
 * play (Kate, Clint, the thugs, with their animation Blueprints, meshes and the Game Animation
 * Sample's chooser tables behind them), the motion-matching databases with their animations, and
 * the effects and sounds the world subsystems stream in.
 *
 * Without it every OpenLevel (a death loading the last save, the flashback's playable scene and
 * back) collected all of it with the old world and loaded it again from disk: 12 to 14 s of the
 * same ~1,300 animations and ~160 databases each time. The game instance outlives the worlds, so
 * a reference held here outlives the travel. Nothing is loaded here; it only keeps what a world
 * already loaded.
 */
UCLASS()
class HAWKEYE_API UHawkeyeResidentAssets : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem of WorldContext's game instance, or null (a bare test world has none). */
	static UHawkeyeResidentAssets* Get(const UObject* WorldContext);

	/** Keeps Object loaded until the game instance ends. Null and repeats are ignored. */
	void Keep(UObject* Object);

	/**
	 * Keeps what World has in play: every pawn's class, and every PoseSearch database loaded.
	 * The game mode calls it at the end of its BeginPlay. Returns how many objects were added.
	 */
	int32 KeepWorldContent(const UWorld* World);

	int32 GetKeptCount() const { return Kept.Num(); }

	/** Keep's rule on its own, for a test: whether Object goes in (not null, not already kept). */
	static bool ShouldKeep(const UObject* Object, const TSet<const UObject*>& AlreadyKept);

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Kept;

	TSet<const UObject*> KeptSet;
};
