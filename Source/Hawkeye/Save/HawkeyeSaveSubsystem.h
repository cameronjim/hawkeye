#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Save/HawkeyeAutosaveClock.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"

#include "Save/HawkeyeSceneReturn.h"
#include "HawkeyeSaveSubsystem.generated.h"

class UFlashbackDefinition;
class USpudSubsystem;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHawkeyeCampaignSavedSignature, const FString&, Reason, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHawkeyeCampaignLoadedSignature, bool, bSuccess);

/**
 * The campaign save: one slot, "HawkeyeCampaign", written by SPUD (Plugins/SPUD, sinbad/SPUD).
 *
 * SPUD stores every ISpudObject actor in the level (Kate and Clint: transform, health, quiver;
 * thugs: position, dead or alive) and the global UHawkeyeCampaignState (chapter, objectives,
 * safehouses). This subsystem decides when to save (objective complete, safehouse, 60 s of calm
 * roaming), whether a save may be loaded (version, stale chapter), and hands the campaign state
 * to the mission once the reloaded map has started it.
 *
 * SPUD also carries level state across any map travel by default; this game only wants explicit
 * saves, so every travel that is not a SPUD load starts SPUD afresh (see HandlePreLoadMap).
 * Rules: claude-docs/gameplay-semantics.md, "Save data".
 */
UCLASS()
class HAWKEYE_API UHawkeyeSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UHawkeyeSaveSubsystem();

	/** The subsystem for the world this object lives in, or nullptr outside a game instance. */
	static UHawkeyeSaveSubsystem* Get(const UObject* WorldContextObject);

	/** The one campaign slot, unless SlotNameOverride is set. */
	static const TCHAR* DefaultSlotName;

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/** Set by tests so the player's campaign is never touched. Empty in game; automation runs use "HawkeyeCampaignAutomation". */
	UPROPERTY(Transient)
	FString SlotNameOverride;

	UFUNCTION(BlueprintPure, Category = "Save")
	FString GetSlotName() const;

	/** True when the slot has a save file (whether or not it will load). Drives the menu's Continue. */
	UFUNCTION(BlueprintPure, Category = "Save")
	bool HasSave() const;

	/**
	 * Writes the campaign now. Refused (false, logged) outside a game world, while SPUD is busy or
	 * a load is pending, with no mission running, or while the player is dead or down.
	 */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool SaveCampaign(const FString& Reason);

	/**
	 * Continue: loads the slot if DecideLoad says so, otherwise starts a new game. Asynchronous:
	 * the map reloads and OnCampaignLoaded fires once it is restored. True when a load started.
	 */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool LoadCampaign();

	/** Forgets the campaign state and reopens the district fresh. The old save stays until overwritten. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void StartNewGame();

	/**
	 * EvaluateLoad plus the log line: a stale chapter is an error (the save points at content that
	 * no longer exists), a version with no migration a warning.
	 */
	EHawkeyeLoadDecision DecideLoad(bool bSaveExists, int32 SavedVersion, const FString& InMissionPath) const;

	/** Reads the slot's header (save version and mission path) without loading it. False when unreadable. */
	bool ReadSaveHeader(int32& OutVersion, FString& OutMissionPath) const;

	/** True from LoadCampaign until the restored world is ready. */
	UFUNCTION(BlueprintPure, Category = "Save")
	bool IsLoading() const;

	/**
	 * AHawkeyeGameMode calls this once the reloaded map has started its mission: completes the
	 * saved objectives silently. Does nothing unless a load is being restored.
	 */
	void ApplyPendingRestore(UWorld* World);

	/** True between LoadCampaign or StartNewGame and the next world starting: a travel is coming. */
	bool IsTravelPending() const { return bLoadInFlight || bNewGameAutosavePending; }

	/** True once after StartNewGame, so the new game's first autosave can be written. */
	bool ConsumeNewGameAutosave();

	/**
	 * What the boss fight has spoken since its chapter started, kept in memory across a death's reload (which reads the
	 * slot back, older than the lines spoken since). UBossFightSubsystem writes and reads it; a chapter's opening and a
	 * new game clear it.
	 */
	TArray<FString> SessionBossHeard;

	// --- Safehouses -------------------------------------------------------------------------------

	/** Records a safehouse as found. True the first time. */
	UFUNCTION(BlueprintCallable, Category = "Save|Safehouse")
	bool DiscoverSafehouse(FName SafehouseId);

	UFUNCTION(BlueprintPure, Category = "Save|Safehouse")
	bool IsSafehouseDiscovered(FName SafehouseId) const;

	UFUNCTION(BlueprintPure, Category = "Save|Safehouse")
	TArray<FName> GetDiscoveredSafehouses() const;

	/** Records the safehouse last used (menu opened or travelled to); saved with the next save. */
	UFUNCTION(BlueprintCallable, Category = "Save|Safehouse")
	void SetLastSafehouse(FName SafehouseId);

	UFUNCTION(BlueprintPure, Category = "Save|Safehouse")
	FName GetLastSafehouse() const;

	/** Records that the "nearby" toast announced PlaceId (a safehouse or pedestal). True the first time. */
	bool NotePlace(FName PlaceId);

	/** The ids NotePlace has recorded. */
	TArray<FName> GetNoticedPlaces() const;

	// --- First-time hints -------------------------------------------------------------------------

	/** Times hint HintId has shown this campaign (its MaxShows once learned); 0 before any. */
	int32 GetHintShowCount(FName HintId) const;

	/** Records hint HintId's count; saved with the next save. */
	void SetHintShowCount(FName HintId, int32 Count);

	// --- Autosave ---------------------------------------------------------------------------------

	/**
	 * Called by the game mode about once a second of unpaused play. bCanSave is false in a fight,
	 * while down, dead or off the ground; the roaming autosave fires after IntervalSeconds of true.
	 */
	void AdvanceAutosave(float DeltaSeconds, bool bCanSave);

	UFUNCTION(BlueprintPure, Category = "Save")
	float GetAutosaveRoamingSeconds() const { return AutosaveClock.GetRoamingSeconds(); }

	/** Seconds of calm roaming between autosaves. */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void SetAutosaveInterval(float Seconds) { AutosaveClock.IntervalSeconds = FMath::Max(1.f, Seconds); }

	// --- Playable scenes ---------------------------------------------------------------------------

	/**
	 * A flashback's playable scene: saves the district (the reason is "playable scene"), remembers
	 * the flashback's ReturnPointLabel and opens its PlayableScene. False when it has none.
	 */
	UFUNCTION(BlueprintCallable, Category = "Save|Scene")
	bool EnterPlayableScene(UFlashbackDefinition* Flashback);

	/** Between EnterPlayableScene and the scene's mission completing. Saves are refused. */
	UFUNCTION(BlueprintPure, Category = "Save|Scene")
	bool IsInPlayableScene() const { return SceneReturn.bInScene; }

	/**
	 * The scene's mission is done: loads the save made on the way in (or, if it cannot be used,
	 * opens the district fresh) and puts the player at the return point once it is back.
	 */
	UFUNCTION(BlueprintCallable, Category = "Save|Scene")
	void ReturnFromPlayableScene();

	/**
	 * Puts the player at the pending return point and tells the controller the scene is over.
	 * Runs after a load restores the district, or from the game mode after a fresh open.
	 */
	void ApplyPendingSceneReturn(UWorld* World);

	// --- Interiors ---------------------------------------------------------------------------------

	/**
	 * Goes into an interior map through its door (AInteriorEntrance): the same plumbing as a playable
	 * scene. Saves the district (reason "interior"), remembers ReturnPointLabel and opens Interior.
	 * Saves are refused inside. False when Interior is unset or there is no game world.
	 */
	bool EnterInterior(const TSoftObjectPtr<UWorld>& Interior, FName ReturnPointLabel);

	/** True between EnterInterior and leaving through an AInteriorExit. */
	UFUNCTION(BlueprintPure, Category = "Save|Scene")
	bool IsInInterior() const { return SceneReturn.bInScene && SceneReturn.bInterior; }

	/**
	 * Leaves the interior: loads the save made on the way in (or opens the district fresh) and puts the
	 * player at ReturnPointOverride, or at the entrance's return point when that is None. The chapter's
	 * flow is left alone. False when no interior is running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Save|Scene")
	bool ReturnFromInterior(FName ReturnPointOverride);

	/**
	 * An interior map opened directly (a test, -game into it) has no entry to return to: records one
	 * from District's ReturnPointLabel with no save, so its exits still lead somewhere.
	 */
	void BeginInteriorWithoutEntry(const FString& District, FName ReturnPointLabel);

	/** True once, on an interior's first playable frame: the screen should come back from black. */
	bool ConsumeInteriorArrivalFade();

	/**
	 * The quiver goes with her through an interior's door, both ways: EnterInterior and ReturnFromInterior
	 * copy the player's bow, arrows and active slot, and the next world's player gets them back (the
	 * interior on its first playable frame, the district once its save has loaded). False, and nothing
	 * changes, when nothing is being carried or there is no player.
	 */
	bool RestoreCarriedQuiver(UWorld* World);

	const FHawkeyeSceneReturn& GetSceneReturn() const { return SceneReturn; }

	// --- Main menu --------------------------------------------------------------------------------

	/**
	 * True exactly once per game instance, for the first world that boots: the main menu shows over
	 * it. False for automation runs (-ExecCmds with RunTests, or -NoMainMenu) and after a load.
	 */
	bool ConsumeBootMenu();

	/** Test hook: the next world to boot shows the main menu again, as the first one did. */
	void RearmBootMenu() { bBootMenuConsumed = false; }

	UFUNCTION(BlueprintPure, Category = "Save")
	UHawkeyeCampaignState* GetCampaignState() const { return Campaign; }

	/** Seconds the screen takes to come back from black after a load. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save", meta = (ClampMin = "0.0"))
	float FadeInSeconds = 1.f;

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FOnHawkeyeCampaignSavedSignature OnCampaignSaved;

	UPROPERTY(BlueprintAssignable, Category = "Save")
	FOnHawkeyeCampaignLoadedSignature OnCampaignLoaded;

	/** Saves written this session; tests and the log use it. */
	UFUNCTION(BlueprintPure, Category = "Save")
	int32 GetSaveCount() const { return SaveCount; }

private:
	USpudSubsystem* GetSpud() const;

	/** The game world this instance is playing, or null. */
	UWorld* GetGameWorld() const;

	/** Why the player cannot be saved right now, or empty. */
	FString GetSaveRefusal(UWorld* World) const;

	/** Copies the mission's state and who is being played into Campaign. */
	void CaptureCampaign(UWorld* World);

	void HandlePreLoadMap(const FString& MapName);

	UFUNCTION()
	void HandleSpudPostLoad(const FString& SlotName, bool bSuccess);

	UFUNCTION()
	void HandleSpudPostSave(const FString& SlotName, bool bSuccess);

	/** The second half of leaving a scene or interior: load the entry save, else open the district fresh. */
	void TravelBackFromScene();

	/** Copies the player's quiver in World for RestoreCarriedQuiver. */
	void CarryQuiver(UWorld* World);

	/** After a load: hand control to whoever was being played, and bring the picture back. */
	void FinishRestoredWorld(UWorld* World);

	UPROPERTY(Transient)
	TObjectPtr<UHawkeyeCampaignState> Campaign;

	UPROPERTY(Transient)
	FHawkeyeAutosaveClock AutosaveClock;

	UPROPERTY(Transient)
	FHawkeyeSceneReturn SceneReturn;

	/** The quiver on its way through an interior's door (see RestoreCarriedQuiver). */
	UPROPERTY(Transient)
	TObjectPtr<UBowDefinition> CarriedBow;

	UPROPERTY(Transient)
	TArray<FHawkeyeQuiverSlot> CarriedArrows;

	int32 CarriedActiveSlot = 0;
	bool bCarryingQuiver = false;

	FDelegateHandle PreLoadMapHandle;
	bool bRestorePending = false;
	bool bLoadInFlight = false;
	bool bBootMenuConsumed = false;
	bool bNewGameAutosavePending = false;
	bool bLastSaveSucceeded = false;
	int32 SaveCount = 0;
};
