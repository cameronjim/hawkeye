#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Challenge/ChallengeTypes.h"
#include "ChallengeSubsystem.generated.h"

class AChallengeCheckpoint;
class AChallengeStart;
class AChallengeTarget;
class APawn;
class AThugAIController;
class UChallengeDefinition;
class UChallengeTracker;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChallengeStartedSignature, UChallengeDefinition*, Challenge);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChallengeEndedSignature, const FChallengeResult&, Result);

/**
 * The side challenges in a running world (claude-docs/gameplay-semantics.md, "Side challenges"). An
 * AChallengeStart's Interact starts a run here: the targets or checkpoint rings are spawned, thugs
 * near the start go calm, the next checkpoint or the targets still up are marked through the
 * mission's secondary markers, and each frame the tracker gets the player's position. When the run
 * ends the spawned actors go, the thugs come back, a completed run pays its reward and is saved, and
 * the results card opens on the player controller.
 *
 * The rules (clock, score, medals, failure, bests) are in UChallengeTracker; the bests are mirrored
 * into UHawkeyeCampaignState for the save. Ticks only while the game is not paused.
 */
UCLASS()
class HAWKEYE_API UChallengeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Challenge", meta = (WorldContext = "WorldContextObject"))
	static UChallengeSubsystem* Get(const UObject* WorldContextObject);

	/** The secondary marker source the challenges set. */
	static const FName MarkerSource;

	/** The secondary marker source "Mark nearest challenge" sets on a pedestal. */
	static const FName StartMarkerSource;

	/** How high over a pedestal's foot its markers point, cm (the floating icon). */
	static constexpr float StartMarkerUp = 175.f;

	/** Every pedestal in the world. */
	TArray<AChallengeStart*> GetStarts() const;

	/** The pedestal nearest From, or null. */
	AChallengeStart* FindNearestStart(const FVector& From) const;

	/**
	 * The nearest pedestal within Radius of From whose challenge id is not in Noticed, or null: the
	 * "[Challenge nearby]" toast's first-time check (AHawkeyePlayerController).
	 */
	AChallengeStart* FindUnnoticedStart(const FVector& From, float Radius, const TArray<FName>& Noticed) const;

	/** How close a pedestal has to be for its "[Challenge nearby]" toast, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Challenge|Marker", meta = (ClampMin = "0.0"))
	float NoticeRadius = 4000.f;

	/** The pause menu's "Mark nearest challenge": a secondary marker on the nearest pedestal. Null when there is none. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	AChallengeStart* MarkNearestChallenge(const FVector& From);

	/** A secondary marker on Start (the world map picks it). False for null or a pedestal with no definition. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	bool MarkChallenge(AChallengeStart* Start);

	/** Takes the pedestal marker down (starting any challenge does too). */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	void ClearChallengeMarker();

	UFUNCTION(BlueprintPure, Category = "Challenge")
	AChallengeStart* GetMarkedStart() const { return MarkedStart.Get(); }

	/**
	 * Starts Start's challenge for Player. Refused while one runs, without a definition, or for a player
	 * who is down or dead. True when the run began.
	 */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	bool StartChallenge(AChallengeStart* Start, APawn* Player);

	/** Ends the running challenge as abandoned. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	void AbortChallenge();

	/** The results card's Retry: back to the last challenge's pedestal and straight into another run. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	bool RetryLastChallenge();

	/** An arrow hit a target of the running challenge RadialCm from its centre. Only the challenger's arrows score. */
	void ReportTargetHit(AChallengeTarget* Target, float RadialCm, AActor* Shooter);

	UFUNCTION(BlueprintPure, Category = "Challenge")
	bool IsRunning() const;

	UFUNCTION(BlueprintPure, Category = "Challenge")
	UChallengeTracker* GetTracker() const { return Tracker; }

	UFUNCTION(BlueprintPure, Category = "Challenge")
	UChallengeDefinition* GetActiveDefinition() const;

	/** The pedestal of the running challenge, or of the last one when none runs. */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	AChallengeStart* GetLastStart() const { return LastStart.Get(); }

	/** The running challenge's targets, by index (archery). */
	const TArray<TWeakObjectPtr<AChallengeTarget>>& GetTargets() const { return Targets; }

	/** The running challenge's rings, in order (traversal). */
	const TArray<TWeakObjectPtr<AChallengeCheckpoint>>& GetCheckpoints() const { return Checkpoints; }

	/** Thugs calmed for the running challenge. */
	int32 GetCalmedThugCount() const { return CalmedThugs.Num(); }

	/** The best record for ChallengeId (empty when never completed). */
	UFUNCTION(BlueprintPure, Category = "Challenge")
	FChallengeRecord GetRecord(FName ChallengeId) const;

	/** Writes every best into the campaign state. The save calls this before writing. */
	void MirrorToCampaign() const;

	/** Reads the bests back from the campaign state. */
	void RestoreFromCampaign();

	/** False keeps the results card shut (scripted runs read the result instead). */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Challenge")
	bool bShowResultsCard = true;

	UPROPERTY(BlueprintAssignable, Category = "Challenge")
	FOnChallengeStartedSignature OnChallengeStarted;

	UPROPERTY(BlueprintAssignable, Category = "Challenge")
	FOnChallengeEndedSignature OnChallengeEnded;

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

	/** The run's targets or rings, where the definition puts them. */
	void SpawnCourse(const UChallengeDefinition* Definition);

	/** Removes everything SpawnCourse made. */
	void ClearCourse();

	/** Calms every thug within the definition's CalmThugRadius of its start. */
	void CalmThugs(const UChallengeDefinition* Definition);

	/** Hands the calmed thugs their senses back. */
	void ReleaseThugs();

	/** The rings' looks and the secondary markers for where the run is now. */
	void RefreshCourse();

	/** The tracker's end of a run: tidy up, reward, save, results. */
	void HandleRunEnded(const FChallengeResult& Result);

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	/** The player is down, dead, or gone. */
	bool IsPlayerOut() const;

	UPROPERTY(Transient)
	TObjectPtr<UChallengeTracker> Tracker = nullptr;

	TWeakObjectPtr<AChallengeStart> LastStart;
	TWeakObjectPtr<AChallengeStart> MarkedStart;
	TWeakObjectPtr<APawn> Challenger;
	TArray<TWeakObjectPtr<AChallengeTarget>> Targets;
	TArray<TWeakObjectPtr<AChallengeCheckpoint>> Checkpoints;
	TArray<TWeakObjectPtr<AThugAIController>> CalmedThugs;

	/** The progress the rings were last drawn for. */
	int32 ShownProgress = INDEX_NONE;
};
