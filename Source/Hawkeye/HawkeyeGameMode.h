#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "World/HawkeyeFrameWatch.h"
#include "HawkeyeGameMode.generated.h"

class UInventoryComponent;
class UMissionDefinition;
class UMissionObjective;

/**
 * Default game mode. Create a Blueprint child per level, set StartingMission, and pick the
 * BP_HawkeyeCharacter pawn.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AHawkeyeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHawkeyeGameMode();

	/** Mission handed to the UMissionSubsystem when play begins. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<UMissionDefinition> StartingMission;

	/** Seconds between the player dying and the level reloading. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission", meta = (ClampMin = "0.0"))
	float RestartDelaySeconds = 2.f;

	/**
	 * Reopens the current level, which restarts the mission from the top (there are no
	 * checkpoints until stage 3). Repeat calls are ignored.
	 *
	 * Delay is the wait in seconds before the level reloads. A negative Delay (the default)
	 * uses RestartDelaySeconds, which is what dying does so the death screen has time to read;
	 * the pause menu passes 0 because the player already chose to start over.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	void RestartMission(float Delay = -1.f);

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsRestartPending() const { return bRestartPending; }

	/**
	 * Death and the pause menu's restart: fades to black over FadeSeconds (negative uses
	 * DeathFadeSeconds), then loads the last autosave through UHawkeyeSaveSubsystem. With no save
	 * to load it falls back to RestartMission's level reload. Repeat calls are ignored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	void ReloadLastCheckpoint(float FadeSeconds = -1.f);

	/** Seconds of fade to black between dying and the checkpoint loading. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save", meta = (ClampMin = "0.0"))
	float DeathFadeSeconds = 1.5f;

	/** How often the roaming autosave clock is advanced, seconds of unpaused play. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save", meta = (ClampMin = "0.1"))
	float AutosaveTickSeconds = 1.f;

	/**
	 * True while any living thug is alerted or the partner's fight clock is running. The roaming
	 * autosave waits for this to clear.
	 */
	UFUNCTION(BlueprintPure, Category = "Save")
	bool IsInCombat() const { return IsWorldInCombat(GetWorld()); }

	/** IsInCombat for any world; static so a test can ask it of a bare test world. */
	static bool IsWorldInCombat(const UWorld* World);

	/** The roaming autosave's gate: a living, standing player on the ground, and no fight on. */
	UFUNCTION(BlueprintPure, Category = "Save")
	bool CanAutosaveNow() const;

	/**
	 * Builds the navmesh once at BeginPlay.
	 *
	 * The maps are generated headlessly and nobody ever pressed Build Paths, so the navmesh
	 * they ship with is empty: nav data spawns, every MoveTo fails with "off the navmesh" and
	 * the guards stand still. Rebuilding at start costs a fraction of a second on maps this
	 * size. TODO(stage3): build and save navigation with the level instead.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Navigation")
	bool bBuildNavigationAtStart = true;

	/**
	 * Outdoor weather: snow falls round the camera only in a map whose game mode says so. An interior
	 * (a flashback's playable scene) turns it off.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World")
	bool bOutdoorWeather = true;

	/** Whether the game mode running World wants outdoor weather. A world without a Hawkeye game mode has none. */
	static bool WantsOutdoorWeather(const UWorld* World);

	/** The same rule for a given game mode (null, or not a Hawkeye one: no weather). */
	static bool WantsOutdoorWeather(const AGameModeBase* GameMode);

	/**
	 * An interior map (L_Int_*): the player's hip camera arm is capped at the character's
	 * IndoorArmLength (250 cm) so it stays inside the room. Off in the district.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World")
	bool bInteriorCamera = false;

	/** Whether the game mode running World wants the indoor camera. No Hawkeye game mode: no. */
	static bool WantsInteriorCamera(const UWorld* World);

	/**
	 * Uncooked builds only (-game from the editor binaries): index every loaded PoseSearch database
	 * during the load instead of on the first frame. The motion-matching databases wait for their
	 * animations to finish loading and then compose their DDC keys on the game thread, all ~240 of
	 * them in the first tick after the map loads, which made that frame take 1.4 s. Doing it in
	 * BeginPlay moves the same work before the playable mark. Cooked builds carry the index.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loading")
	bool bIndexMotionMatchingAtLoad = true;

	/** Frames longer than this after the first playable frame are logged as hitches, ms. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Diagnostics", meta = (ClampMin = "1.0"))
	float HitchLogMs = 100.f;

	/** How long after the first playable frame the frame summary covers, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Diagnostics", meta = (ClampMin = "1.0"))
	float FrameSummarySeconds = 20.f;

	/** Seconds from process start (or from the level change that loaded this map) to the first playable frame; -1 before it. */
	UFUNCTION(BlueprintPure, Category = "Diagnostics")
	float GetPlayableSeconds() const { return PlayableSeconds; }

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * -HawkeyePerfLog: names the part of a scripted run that follows ("map", "fight"); the perf lines say
	 * "<test>/<phase>". Empty clears it. Static so it survives the level change a phase may cause.
	 */
	static void SetPerfPhase(const FString& Phase);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The first tick with a possessed pawn: logs "Playable after X s" and starts the frame watch. */
	void NotePlayable();

	/** One real frame of the watch after the playable mark. */
	void WatchFrame(double Now);

	/** -HawkeyePerfLog: adds one frame to the perf window and logs the window every PerfLogSeconds. */
	void WatchPerf(double Now, double FrameSeconds, bool bScreenshot);

	/** Logs the perf window (and the process memory) under the current label and starts a new one. */
	void FlushPerfWindow(double Now);

	/** Timer body: actually reopens the level. */
	void ReopenCurrentLevel();

	/** See bIndexMotionMatchingAtLoad. */
	void IndexMotionMatchingDatabases();

	/** Rebuilds navigation when the level shipped without any. See bBuildNavigationAtStart. */
	void BuildNavigationIfEmpty();

	/** Loads every effect and sound the district can play, so none loads mid-play. See PreloadEffectsAndSounds. */
	void PreloadEffectsAndSounds();
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleMissionComplete(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	/** The local player's inventory, or null before a pawn exists. */
	UInventoryComponent* FindPlayerInventory() const;

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	/** Timer body: one step of the roaming autosave clock. */
	void TickAutosave();

	/** Timer body: the fade is black; load the checkpoint. */
	void LoadCheckpointNow();

	/** Timer body: the first save of a new game, once the world has settled. */
	void SaveNewGame();

	/** Blueprint hook for end-of-mission scoring, stats screens, achievements. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Mission")
	void OnMissionCompleted(UMissionDefinition* Mission);

	/** Blueprint hook for a death screen shown during RestartDelaySeconds. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Mission")
	void OnMissionRestarting();

private:
	/** Marks the frame the engine processed a screenshot, so the frame watch can say so. */
	void HandleScreenshotProcessed();

	double BeginPlayWallSeconds = 0.0;

	double PlayableWallSeconds = 0.0;
	double LastFrameWallSeconds = 0.0;
	float PlayableSeconds = -1.f;
	FHawkeyeFrameWatch FrameWatch;
	bool bFrameSummaryLogged = false;
	bool bScreenshotThisFrame = false;
	FDelegateHandle ScreenshotProcessedHandle;
	/** -HawkeyeQuitAfterPlayable=<s>: the load measurement quits this long after the playable mark. */
	float QuitAfterPlayableSeconds = -1.f;
	/** -HawkeyeReloadAfterPlayable=<s>: the load measurement reopens the level once, this long after the mark. */
	float ReloadAfterPlayableSeconds = -1.f;
	bool bQuitRequested = false;

	/** -HawkeyePerfLog=<s>: log frame time, the stat unit split and memory every this many seconds; negative: off. */
	float PerfLogSeconds = -1.f;
	/** -HawkeyeStatDump=<s>: run "stat dumpave" this often after the playable mark; negative: off. */
	float StatDumpSeconds = -1.f;
	FHawkeyePerfWindow PerfWindow;
	double PerfWindowStartSeconds = 0.0;
	double LastStatDumpSeconds = 0.0;
	/** The running automation test when the window began (the window closes when it changes). */
	FString PerfLabel;

	bool bRestartPending = false;
	FTimerHandle RestartTimerHandle;
	FTimerHandle AutosaveTimerHandle;
	FTimerHandle NewGameSaveTimerHandle;
};
