#pragma once

#include "CoreMinimal.h"
#include "Boss/BossRules.h"
#include "Boss/BossTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "BossFightSubsystem.generated.h"

class ABossArenaHazard;
class AThugCharacter;
class UBossDefinition;
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

	void Say(const TArray<FName>& Lines);
	void Bark(EBossBarkEvent Event);
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
};
