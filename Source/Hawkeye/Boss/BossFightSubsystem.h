#pragma once

#include "CoreMinimal.h"
#include "Boss/BossRules.h"
#include "Boss/BossTypes.h"
#include "Combat/FinisherComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "BossFightSubsystem.generated.h"

class ABossArenaHazard;
class AThugCharacter;
class UBossDefinition;
struct FBossPhaseDefinition;
class UBossPhaseComponent;
class UHealthComponent;
class UMissionDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBossFightStateChangedSignature, EBossFightState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBossFightPhaseStartedSignature, int32, PhaseIndex);

/**
 * Runs a chapter's boss fight (claude-docs/gameplay-semantics.md, "Bosses"): dresses the arena's roof, puts the boss and
 * the first phase's crew on it, completes the meet objective when Kate arrives, plays the pre-fight lines, then runs the
 * phases the boss's definition lays out (crews, the boss's combo and winded window, the hazard, the lines and the
 * arrows a phase grants), completes each phase's objective as the next one opens and the last once he is down and the
 * after lines are over. A load restarts the fight at the checkpoint the save's objectives give. The rules are
 * UBossRules; the boss is a UBossDefinition the chapter's mission names (UMissionDefinition::Boss). Ticks only while
 * the game is not paused.
 */
UCLASS()
class HAWKEYE_API UBossFightSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Boss", meta = (WorldContext = "WorldContextObject"))
	static UBossFightSubsystem* Get(const UObject* WorldContextObject);

	/** On the boss. */
	static const FName BossTag;
	/** On every crew member. */
	static const FName CrewTag;
	/** On the props, lights and the hazard. */
	static const FName FurnitureTag;

	/** The chapter's opening: puts Kate on the arena's street stand, facing the roof. False without a boss. */
	bool BeginChapter(const UMissionDefinition* Mission);

	/** Dresses the arena and puts the boss and the first crew on the roof, calm, waiting for Kate (Approach). */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	bool SetUpFight(UBossDefinition* Definition);

	/** Ends the fight with nothing completed: the boss, the crew and the furniture go. */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	void StopFight();

	/**
	 * The checkpoint: the boss back at the start of PhaseIndex with its checkpoint health, that phase's crew fresh, the
	 * hazard as the phase has it, fighting at once (no pre-fight lines). -1 is the approach; Phases.Num() is done.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	void RestartAtPhase(int32 PhaseIndex);

	/** Picks the fight back up after a load at UBossRules::CheckpointPhase of the save's objectives. */
	void RestoreFromCampaign();

	UFUNCTION(BlueprintPure, Category = "Boss")
	EBossFightState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Boss")
	UBossDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "Boss")
	AThugCharacter* GetBoss() const { return Boss.Get(); }

	UFUNCTION(BlueprintPure, Category = "Boss")
	UBossPhaseComponent* GetPhaseComponent() const;

	/** The current phase's crew, the living and the down. */
	TArray<AThugCharacter*> GetCrew() const;

	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetPhaseIndex() const { return PhaseIndex; }

	UFUNCTION(BlueprintPure, Category = "Boss")
	ABossArenaHazard* GetHazard() const { return Hazard.Get(); }

	UFUNCTION(BlueprintPure, Category = "Boss")
	bool IsWinded() const { return Combo.IsWinded(); }

	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetWindedCount() const { return Combo.WindedCount; }

	/** Seconds since the fight (the first phase) started, unpaused. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	float GetFightSeconds() const { return FightSeconds; }

	/** The fight clock at the start of each phase reached (in order), for the reports. */
	const TArray<float>& GetPhaseStartSeconds() const { return PhaseStarts; }

	/** Every bark spoken this fight, as "<phase>:<event>". */
	const TArray<FString>& GetBarksSpoken() const { return BarksSpoken; }

	/** Every DT_Dialogue row this fight asked to speak, in order. */
	const TArray<FName>& GetLinesSpoken() const { return LinesSpoken; }

	// --- The solves and the beats (gameplay-semantics.md, "Bosses") -------------------------------------------

	/** He guards now: the phase gives him a guard, he fights, he is not winded. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	bool IsGuarding() const;

	/** Times a trick arrow broke his guard (he went winded for it). */
	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetGuardBreakCount() const { return GuardBreaks; }

	/** Times the hazard's sweep caught him. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetHazardBossHits() const { return HazardBossHits; }

	/** Kate's dodges started while he wound up an unblockable swing within 450 cm. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetUnblockableDodges() const { return UnblockableDodges; }

	/** Finishers on him, and those that started while he was not winded (the rules make that 0). */
	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetBossFinishers() const { return BossFinishers; }

	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetBossFinishersOutsideWindow() const { return BossFinishersOutsideWindow; }

	/** Finishers on him that ended a phase or the fight (the slow, close camera). */
	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetDecisiveFinishers() const { return DecisiveFinishers; }

	/** A scripted beat holds the fight (or one waits for her finisher to end). */
	UFUNCTION(BlueprintPure, Category = "Boss")
	bool IsInBeat() const { return bInBeat; }

	UFUNCTION(BlueprintPure, Category = "Boss")
	bool IsBeatPending() const { return bBeatPending; }

	UFUNCTION(BlueprintPure, Category = "Boss")
	float GetBeatElapsed() const { return bInBeat ? BeatElapsed : 0.f; }

	/** Beats played and how many of them were skipped. */
	int32 GetBeatsPlayed() const { return BeatsPlayed; }
	int32 GetBeatsSkipped() const { return BeatsSkipped; }

	/** Where the running beat's camera stands and looks (zero when none). */
	FVector GetBeatCameraLocation() const { return BeatCameraLocation; }
	FRotator GetBeatCameraRotation() const { return BeatCameraRotation; }

	/** Skips the running beat (its skip key does the same once BeatMinSkipSeconds are in). */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	void SkipBeat() { bSkipRequested = true; }

	/** What the fight has spoken, as "line:<row>" and "bark:<phase>:<event>": kept across a load so nothing repeats. */
	TArray<FString> ExportHeard() const;
	void ImportHeard(const TArray<FString>& Heard);

	/** Writes ExportHeard into the campaign (UHawkeyeSaveSubsystem::CaptureCampaign) and the save's session memory. */
	void MirrorToCampaign() const;

	/** The fight is set up or running: the roof is the chapter's arena. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	bool IsFightActive() const { return State == EBossFightState::Approach || State == EBossFightState::Intro
		|| State == EBossFightState::Fight || State == EBossFightState::Defeated; }

	/** When set (tests), this pawn is Kate instead of the first player's. */
	TWeakObjectPtr<APawn> PlayerOverride;

	/** Tests: the fight starts the moment she arrives, and ends the moment he is down. */
	bool bSkipLines = false;

	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FOnBossFightStateChangedSignature OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FOnBossFightPhaseStartedSignature OnPhaseStarted;

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem interface

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	APawn* FindKate() const;
	void SetState(EBossFightState NewState);

	/** The props, lights and the tethered hazard, once per fight. */
	void EnsureFurniture();
	void ClearFurniture();
	void ClearCrew();
	void ClearBoss();

	AThugCharacter* SpawnThug(UClass* Class, const FVector& Feet, float Yaw, EThugWeapon Weapon, float Scale) const;
	AThugCharacter* SpawnBoss();
	void SpawnCrew(int32 Index);

	/** He thinks and fights Kate, or stands still, calm. */
	void SetFighting(AThugCharacter* Thug, bool bFight, APawn* Kate) const;
	void FaceKate(AThugCharacter* Thug, const APawn* Kate) const;

	void BeginIntro();
	void StartFight();
	/** The phase's crew, the boss's part, the hazard, the arrows and its start lines. */
	void StartPhase(int32 Index, bool bRestore);
	void UpdateApproach(APawn* Kate);
	void UpdateIntro(float DeltaSeconds, APawn* Kate);
	void UpdateFight(float DeltaSeconds, APawn* Kate);
	void UpdateDefeated(float DeltaSeconds);
	void UpdateBarks(APawn* Kate);
	void UpdateMarker();
	void Finish();

	/** Speaks Lines, every one; a story line (bStory) already heard this run of the fight is left out. */
	void Say(const TArray<FName>& Lines, bool bStory = false);
	void Bark(EBossBarkEvent Event);

	/** He goes winded now (a broken guard, the hazard): the finisher window, the bark. Why, for the log. */
	bool ForceWinded(const TCHAR* Why, AActor* By);
	/** New trick-arrow effects: a guard break on him, a pin on the hazard. */
	void CheckArrowEffects();
	void UpdateGuard(AThugCharacter* Him, const FBossPhaseDefinition& Phase);
	void UpdateHazardOnBoss(float DeltaSeconds, AThugCharacter* Him, const FBossPhaseDefinition& Phase);

	/** Starts Beat now, or once her finisher is over; with nothing to play, a defeat goes straight to the after lines. */
	void RequestBeat(const FBossBeat& Beat, bool bDefeat);
	void StartBeat(APawn* Kate);
	void UpdateBeat(float DeltaSeconds, APawn* Kate);
	/** Control back. bCancel (a restart, a stop): no after lines follow a defeat beat. */
	void EndBeat(bool bSkipped, bool bCancel = false);
	/** Kate held or let go: input, damage, the lock. */
	void HoldKate(APawn* Kate, bool bHold) const;
	/** The after sequence (once he is down and the defeat beat, if any, is over). */
	void BeginAfter();
	/** Puts Kate's finisher camera back after a boss finisher widened it. */
	void RestoreFinisherCamera();
	void AddRimLight(AThugCharacter* Him) const;
	void RememberHeard(const FString& Key) const;
	bool CompleteObjective(FName ObjectiveId) const;
	bool IsDown(const AThugCharacter* Thug) const;

	UFUNCTION()
	void HandlePhaseChanged(int32 OldPhaseIndex, int32 NewPhaseIndex, FBossPhase Phase);

	UFUNCTION()
	void HandleBossHealthChanged(UHealthComponent* Health, float NewHealth, float Delta, AActor* DamageInstigator);

	UFUNCTION()
	void HandleBossDeath(UHealthComponent* Health, AActor* Killer);

	UFUNCTION()
	void HandleKateHealthChanged(UHealthComponent* Health, float NewHealth, float Delta, AActor* DamageInstigator);

	UFUNCTION()
	void HandleFinisherStruck(AActor* Target);

	UFUNCTION()
	void HandleFinisherStarted(AActor* Target, EHawkeyeFinisherStyle Style);

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	void BindKate(APawn* Kate);
	void UnbindKate();

	UPROPERTY(Transient)
	TObjectPtr<UBossDefinition> Definition = nullptr;

	TWeakObjectPtr<AThugCharacter> Boss;
	TArray<TWeakObjectPtr<AThugCharacter>> Crew;
	TArray<bool> CrewWasDown;
	TArray<bool> CrewWasParried;
	TArray<bool> CrewWasWindingUp;
	TWeakObjectPtr<ABossArenaHazard> Hazard;
	TArray<TWeakObjectPtr<AActor>> Furniture;
	TWeakObjectPtr<APawn> BoundKate;

	EBossFightState State = EBossFightState::Idle;
	int32 PhaseIndex = INDEX_NONE;
	float IntroSeconds = 0.f;
	float FightSeconds = 0.f;
	float PhaseSeconds = 0.f;
	float DefeatedSeconds = 0.f;
	TArray<float> PhaseStarts;
	FBossComboClock Combo;
	bool bApplyingBonus = false;
	bool bBossWasParried = false;
	bool bKateWasDodging = false;
	/** The granted arrows' slot and count, to hear the first one used. */
	int32 GrantSlot = 0;
	int32 GrantCount = 0;

	/** Times each bark (by phase and event) spoke this fight, and when it last did. */
	TMap<FString, int32> BarkCounts;
	TMap<FString, float> BarkLastSeconds;
	TArray<FString> BarksSpoken;
	TArray<FName> LinesSpoken;
	/** Story lines (start, end, beat) heard this run of the fight, a load included. */
	TArray<FName> HeardLines;

	// The solves.
	int32 GuardBreaks = 0;
	int32 HazardBossHits = 0;
	int32 UnblockableDodges = 0;
	int32 BossFinishers = 0;
	int32 BossFinishersOutsideWindow = 0;
	int32 DecisiveFinishers = 0;
	float SinceHazardHitBoss = 1000.f;
	TArray<TWeakObjectPtr<AActor>> SeenEffects;

	// The beat.
	bool bInBeat = false;
	bool bBeatPending = false;
	bool bBeatIsDefeat = false;
	bool bSkipRequested = false;
	bool bAfterStarted = false;
	float BeatElapsed = 0.f;
	FBossBeat ActiveBeat;
	int32 BeatsPlayed = 0;
	int32 BeatsSkipped = 0;
	FVector BeatCameraLocation = FVector::ZeroVector;
	FRotator BeatCameraRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<AActor> BeatCamera;

	// A boss finisher's wider camera, and Kate's own numbers to put back.
	TWeakObjectPtr<UFinisherComponent> WidenedFinisher;
	float SavedSlowDilation = 0.5f;
	float SavedSlowRealSeconds = 0.4f;
	float SavedCameraPush = 40.f;
};
