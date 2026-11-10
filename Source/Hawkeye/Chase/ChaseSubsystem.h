#pragma once

#include "CoreMinimal.h"
#include "Chase/ChaseTypes.h"
#include "Player/HawkeyeMovementTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChaseSubsystem.generated.h"

class AHawkeyeCharacter;
class AThugCharacter;
class UChaseRouteDefinition;
class UMissionDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChaseStateChangedSignature, EChaseState, State);

/**
 * Runs a chapter's chase (claude-docs/gameplay-semantics.md, "Chases"): spawns the thief and moves him along
 * the route with the rubber band, follows Kate's progress, completes the route's objectives where it says,
 * plays the dumpster beat, notices the catch and the loss, and keeps its state in the campaign save. The
 * rules are UChaseRules; the route is a UChaseRouteDefinition the chapter's mission names. Ticks only while
 * the game is not paused.
 */
UCLASS()
class HAWKEYE_API UChaseSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Chase", meta = (WorldContext = "WorldContextObject"))
	static UChaseSubsystem* Get(const UObject* WorldContextObject);

	/** On the thief: the chase's runner. */
	static const FName ThiefTag;

	/** On the dumpster and the props: the chase's furniture. */
	static const FName FurnitureTag;

	/** The chapter's opening: puts Kate on the route's start and sets the thief running. False without a chase. */
	bool BeginChapter(const UMissionDefinition* Mission);

	/** Starts Route. bPlaceKate puts her on its start at a sprint (the opening); otherwise she stays where she is. */
	UFUNCTION(BlueprintCallable, Category = "Chase")
	bool StartChase(UChaseRouteDefinition* Route, bool bPlaceKate);

	/** Ends a chase with nothing completed or failed: the thief goes. */
	UFUNCTION(BlueprintCallable, Category = "Chase")
	void StopChase();

	/** Running, in the beat, cornered, or caught with its objectives still to complete. */
	UFUNCTION(BlueprintPure, Category = "Chase")
	bool IsChaseRunning() const;

	UFUNCTION(BlueprintPure, Category = "Chase")
	EChaseState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	UChaseRouteDefinition* GetRoute() const { return Route; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	AThugCharacter* GetThief() const { return Thief.Get(); }

	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetLead() const { return Lead; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetThiefProgress() const { return ThiefAlong; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetKateProgress() const { return KateAlong; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetRouteLength() const { return Distances.Num() > 0 ? Distances.Last() : 0.f; }

	/** The route distance of waypoint Index, or -1. */
	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetWaypointProgress(int32 Index) const { return Distances.IsValidIndex(Index) ? Distances[Index] : -1.f; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetLoseClock() const { return LoseClock; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetThiefSpeed() const { return ThiefSpeed; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	bool IsKateOnRoute() const { return bKateOnRoute; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	bool IsBeatDone() const { return bBeatDone; }

	/** The beat played through (not passed or skipped). */
	UFUNCTION(BlueprintPure, Category = "Chase")
	bool DidBeatPlay() const { return bBeatPlayed; }

	/** 0 none, 1 the line letting go, 2 falling, 3 in the dumpster, 4 climbing out. */
	UFUNCTION(BlueprintPure, Category = "Chase")
	int32 GetBeatPhase() const { return BeatPhase; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetBeatSeconds() const { return BeatSeconds; }

	UFUNCTION(BlueprintPure, Category = "Chase")
	int32 GetReachedWaypoints() const { return ReachedWaypoints; }

	/** Seconds the current chase has run (unpaused). */
	UFUNCTION(BlueprintPure, Category = "Chase")
	float GetChaseSeconds() const { return ChaseSeconds; }

	/** The smallest and largest lead since it started (the beat excluded), cm. */
	float GetMinLead() const { return MinLead; }
	float GetMaxLead() const { return MaxLead; }

	/** Writes the running chase into the campaign state (the save subsystem calls this as it saves). */
	void MirrorToCampaign() const;

	/** Picks a saved chase back up after a load. */
	void RestoreFromCampaign();

	/** When set (tests), this pawn is Kate instead of the first player's. */
	TWeakObjectPtr<APawn> PlayerOverride;

	/** When set (tests), a catch completes without waiting for the line. */
	bool bSkipCaughtHold = false;

	UPROPERTY(BlueprintAssignable, Category = "Chase")
	FOnChaseStateChangedSignature OnChaseStateChanged;

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

	/** The pawn the chase follows: PlayerOverride, else the first player's. */
	APawn* FindKate() const;

	void SetState(EChaseState NewState);

	/** Reads the route into Points and Distances. False for a route that cannot run. */
	bool LoadRoute(UChaseRouteDefinition* InRoute);

	/** Spawns the dumpster and the vault props for Route, once per world. */
	void EnsureFurniture(const UChaseRouteDefinition* InRoute);

	/** Spawns the thief at ThiefAlong. */
	AThugCharacter* SpawnThief();

	/** Puts Kate on the route's start, facing along it, at her sprint speed. */
	void PlaceKate(APawn* Kate) const;

	/** Puts ThiefSegment and LegElapsed where ThiefAlong says. */
	void SyncSegment();

	void UpdateKate(APawn* Kate);
	void UpdateThief(float DeltaSeconds);
	void MoveThief(const FVector& Feet, float Yaw, float DeltaSeconds);
	void UpdateWaypoints();
	void UpdateLose(float DeltaSeconds, const APawn* Kate);
	void UpdateCatch(float DeltaSeconds);
	void UpdateMarker();
	void ClearMarker();

	/** Plays a dialogue sequence by name, if it has one. */
	void Say(FName Sequence) const;

	// --- The beat ---------------------------------------------------------------------------------------------
	void UpdateBeatTrigger(AHawkeyeCharacter* Kate);
	void BeginBeat(AHawkeyeCharacter* Kate);
	void UpdateBeat(float DeltaSeconds, AHawkeyeCharacter* Kate);
	void EndBeat(AHawkeyeCharacter* Kate, bool bForced);
	void StartBeatMove(AHawkeyeCharacter* Kate, const TArray<FVector>& Feet, float Seconds) const;
	bool IsReplay() const;

	/** Turns her view along the alley and down into the bin as the line lets go. */
	void FaceAlongAlley(AHawkeyeCharacter* Kate) const;

	void Lose();
	void CompleteCatch();

	UFUNCTION()
	void HandleParkourStarted(EHawkeyeParkourMove Move, EHawkeyeParkourRoute ParkourRoute);

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	void BindKate(APawn* Kate);
	void UnbindKate();

	UPROPERTY(Transient)
	TObjectPtr<UChaseRouteDefinition> Route = nullptr;

	TArray<FVector> Points;
	TArray<float> Distances;
	TWeakObjectPtr<AThugCharacter> Thief;
	TWeakObjectPtr<APawn> BoundKate;
	TWeakObjectPtr<AActor> Dumpster;
	TArray<TWeakObjectPtr<AActor>> Props;
	TWeakObjectPtr<const UChaseRouteDefinition> FurnishedFor;

	EChaseState State = EChaseState::Idle;
	float ThiefAlong = 0.f;
	float ThiefSpeed = 0.f;
	int32 ThiefSegment = 0;
	float LegElapsed = 0.f;
	FVector LastThiefFeet = FVector::ZeroVector;
	FVector LastKateFeet = FVector::ZeroVector;
	bool bHaveKateFeet = false;
	float KateAlong = 0.f;
	bool bKateOnRoute = true;
	float Lead = 0.f;
	float LoseClock = 0.f;
	int32 ReachedWaypoints = 0;
	bool bBeatDone = false;
	bool bBeatPlayed = false;
	bool bPulledAhead = false;
	bool bVaulted = false;
	bool bWasZipping = false;
	float ChaseSeconds = 0.f;
	float MinLead = 0.f;
	float MaxLead = 0.f;
	float CaughtSeconds = 0.f;

	int32 BeatPhase = 0;
	float BeatSeconds = 0.f;
	float BeatPhaseSeconds = 0.f;
	bool bBeatInputIgnored = false;
	bool bBeatReplay = false;
};
