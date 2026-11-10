#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BossPhaseComponent.generated.h"

class UHealthComponent;

/** One stage of a boss fight. Authoring order does not matter; BeginPlay sorts by threshold. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FBossPhase
{
	GENERATED_BODY()

	/** Designer-facing name ("Warden_Gunplay", "Warden_Melee"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	FName PhaseName;

	/** Phase begins once health drops strictly below this fraction of MaxHealth (1.0 = full health). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealthThresholdPercent = 1.f;

	/** Ignore damage while transitioning into this phase (scripted animation, teleport, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	bool bInvulnerableDuringTransition = true;

	/** Length of the transition before normal combat resumes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase", meta = (ClampMin = "0.0"))
	float TransitionSeconds = 2.f;

	/**
	 * Behaviour selector read by the AI. Set the same name as a blackboard value and branch on it in
	 * the behaviour tree (e.g. "Gun" -> ranged sub-tree, "Melee" -> chainsaw sub-tree).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phase")
	FName BehaviorTag;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBossPhaseChangedSignature, int32, OldPhaseIndex, int32, NewPhaseIndex, FBossPhase, Phase);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBossPhaseTransitionFinishedSignature, int32, PhaseIndex);

/**
 * Drives multi-phase boss fights off a UHealthComponent.
 *
 * BeginPlay binds to the owner's health component; tests (and anything without a world) call
 * Bind() directly. A boss with N phases has N-1 transitions: phase 0 is active from the start and
 * is not broadcast.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UBossPhaseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBossPhaseComponent();

	/** Phases, highest threshold first. Sorted into that order by Bind(). Index 0 is active at start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss")
	TArray<FBossPhase> Phases;

	/** Broadcast the moment a new phase is entered; hook this up to swap the AI behaviour. */
	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FOnBossPhaseChangedSignature OnPhaseChanged;

	/** Broadcast when the transition window ends and invulnerability is restored. */
	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FOnBossPhaseTransitionFinishedSignature OnPhaseTransitionFinished;

	/**
	 * Sorts the phases, listens to InHealthComponent and makes phase 0 active without broadcasting.
	 * BeginPlay calls this with the owner's health component; call it directly in tests.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	void Bind(UHealthComponent* InHealthComponent);

	/** Sorts Phases by descending HealthThresholdPercent. Bind() calls this. */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	void SortPhases();

	UFUNCTION(BlueprintPure, Category = "Boss")
	int32 GetCurrentPhaseIndex() const { return CurrentPhaseIndex; }

	/** The active phase, or a default-constructed phase when none is active. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	FBossPhase GetCurrentPhase() const;

	UFUNCTION(BlueprintPure, Category = "Boss")
	FName GetCurrentBehaviorTag() const;

	UFUNCTION(BlueprintPure, Category = "Boss")
	bool IsTransitioning() const { return bTransitioning; }

	/** Forces a phase regardless of health (debug / scripted beats). Broadcasts OnPhaseChanged once. */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	void EnterPhase(int32 PhaseIndex);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleHealthChanged(UHealthComponent* InHealthComponent, float NewHealth, float Delta, AActor* DamageInstigator);

	/** Raises the transition flag and, if the phase asks for it, invulnerability. */
	void BeginTransition(const FBossPhase& Phase);

	/** Starts the transition timer, or ends the transition immediately when there is no world. */
	void ScheduleTransitionEnd(const FBossPhase& Phase);

	void FinishTransition();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<UHealthComponent> HealthComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss")
	int32 CurrentPhaseIndex = INDEX_NONE;

private:
	bool bTransitioning = false;

	/** Invulnerability state captured when a transition starts, restored when it ends. */
	bool bInvulnerableBeforeTransition = false;

	FTimerHandle TransitionTimerHandle;
};
