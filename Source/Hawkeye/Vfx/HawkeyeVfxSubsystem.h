#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HawkeyeVfxSubsystem.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;

/** Fired for every effect a game event asks for, set or not: the test listener's hook. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnHawkeyeVfxRequested, FName /*Event*/, const FVector& /*Location*/);

/**
 * Where every particle effect is spawned from (claude-docs/gameplay-semantics.md, "Effects"). The
 * same shape as UHawkeyeAudioSubsystem: components hold their Niagara systems as soft pointers, set
 * on the Blueprints and data assets by create_blueprints.py; these helpers load them synchronously
 * the first time (the standalone game has nothing resident, testing.md 2b), keep them for the level,
 * and log every request at Verbose on LogHawkeye:
 *
 *   -LogCmds="LogHawkeye Verbose"   then grep the log for "Vfx:"
 *
 * Every request is counted by event name, even when the system is unset or the world has no
 * renderer, so tests assert that the call was made (testing.md: assert on calls, not effects).
 * The systems themselves are built by Tools/Editor/create_vfx.py.
 */
UCLASS()
class HAWKEYE_API UHawkeyeVfxSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A one-shot at Location, facing Rotation (the system's X), scaled. Pooled; returns the component or null. */
	static UNiagaraComponent* SpawnAt(const UObject* WorldContext, const TSoftObjectPtr<UNiagaraSystem>& System,
		const FVector& Location, const FRotator& Rotation, FName Event, float Scale = 1.f);

	/**
	 * Attached to AttachTo and moving with it (trails). Not pooled: the caller may keep it, deactivate
	 * it to let it finish, or destroy it. Returns the component or null.
	 */
	static UNiagaraComponent* SpawnAttached(const TSoftObjectPtr<UNiagaraSystem>& System, USceneComponent* AttachTo,
		FName Event, const FVector& Offset = FVector::ZeroVector);

	/** Not attached, not pooled, not auto-destroyed: a component the caller drives and destroys (the zip line, snow). */
	static UNiagaraComponent* SpawnKept(const UObject* WorldContext, const TSoftObjectPtr<UNiagaraSystem>& System,
		const FVector& Location, FName Event);

	/**
	 * A decal of Material at Location, projecting along Rotation's X, Radius across and DepthCm deep,
	 * fading out over its last FadeSeconds of LifeSeconds.
	 */
	static UDecalComponent* SpawnDecal(const UObject* WorldContext, const TSoftObjectPtr<UMaterialInterface>& Material,
		const FVector& Location, const FRotator& Rotation, float Radius, float LifeSeconds, float FadeSeconds,
		FName Event, float DepthCm = 80.f);

	/** Lets a kept effect finish (no new particles) and forgets it. Safe on null. */
	static void Release(TObjectPtr<UNiagaraComponent>& Component);

	/** Removes a kept effect at once and forgets it. Safe on null. */
	static void Kill(TObjectPtr<UNiagaraComponent>& Component);

	/** Loads System (synchronously) and keeps it for the level. Null when unset or missing. */
	UNiagaraSystem* Resolve(const TSoftObjectPtr<UNiagaraSystem>& System);

	/**
	 * Streams every Niagara system under Path (/Game/VFX) in the background and keeps each for the
	 * level once it lands, so the first grapple or chimney does not load one mid-play (a 100 to
	 * 300 ms frame in the standalone game). The game mode starts it at the end of its BeginPlay.
	 */
	void PreloadFolderAsync(const FString& Path);

	/** Requests for Event in this world, whether or not anything was drawn. */
	int32 GetRequestCount(FName Event) const { return RequestCounts.FindRef(Event); }

	/** Requests of every kind in this world. */
	int32 GetTotalRequests() const { return TotalRequests; }

	/** Effects actually spawned (a system was set and loaded). */
	int32 GetSpawnCount() const { return SpawnCount; }

	/** Every request, as it happens. */
	FOnHawkeyeVfxRequested OnRequested;

	/** The subsystem of WorldContext's world, or null. */
	static UHawkeyeVfxSubsystem* Find(const UObject* WorldContext);

	// Event names, one per game event that asks for an effect.
	static const FName SmokeCloudEvent;
	static const FName EmpPulseEvent;
	static const FName ExplosionEvent;
	static const FName ScorchEvent;
	static const FName PuttySplatEvent;
	static const FName TrickEffectEvent;
	static const FName ArrowTrailEvent;
	static const FName ArrowImpactEvent;
	static const FName HitSparkEvent;
	static const FName MeleeSparkEvent;
	static const FName KnockdownDustEvent;
	static const FName ParryEvent;
	static const FName BowReleaseEvent;
	static const FName ZipLineEvent;
	static const FName AnchorSparksEvent;
	static const FName MuzzleFlashEvent;
	static const FName TracerEvent;
	static const FName FootstepEvent;
	static const FName LandingEvent;
	static const FName SnowfallEvent;
	static const FName ChimneyEvent;
	static const FName ScreenPulseEvent;

	/** Counts and logs a request that draws nothing of its own (the screen pulse). */
	static void NoteRequest(const UObject* WorldContext, FName Event, const FVector& Location);

private:
	void Note(FName Event, const UObject* Asset, const UObject* Context, const FVector& Location, bool bSpawned);

	/** The background preload in flight, if any. */
	TSharedPtr<struct FStreamableHandle> PreloadHandle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Resident;

	TMap<FName, int32> RequestCounts;
	int32 TotalRequests = 0;
	int32 SpawnCount = 0;
};
