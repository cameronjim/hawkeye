#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FinisherComponent.generated.h"

class AThugCharacter;
class USoundBase;

/** How a finisher lands: a two-handed blow, or (bow up) a sweep of the bow that throws him sideways. */
UENUM(BlueprintType)
enum class EHawkeyeFinisherStyle : uint8
{
	Strike,
	Bow
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFinisherStartedSignature, AActor*, Target, EHawkeyeFinisherStyle, Style);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFinisherStruckSignature, AActor*, Target);

/**
 * The finisher: F on a staggered or knocked-down thug within Range. A DurationSeconds move in which
 * the owner lunges to StandOffDistance from him, time slows to SlowDilation for SlowRealSeconds of
 * real time from SlowAtSeconds, he takes lethal damage at StrikeAtSeconds and his ragdoll is thrown,
 * and the camera pushes in CameraPushDistance for the length of it. The owner is invulnerable and
 * (through AHawkeyeCharacter::IsLockedOutByTakedown) takes no input until it ends.
 *
 * The stealth takedown (from behind, unaware) is tried first on the same key; this is the loud one.
 * Timing runs from TickComponent; AdvanceFinisher is public so a test can step it.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UFinisherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFinisherComponent();

	/** The nearest thug a finisher can take from where the owner stands, or null. */
	UFUNCTION(BlueprintPure, Category = "Finisher")
	AThugCharacter* FindTarget() const;

	/** Finds a target and starts on it: the bow sweep when bBowOut. False with none, or one running. */
	UFUNCTION(BlueprintCallable, Category = "Finisher")
	bool TryFinisher(bool bBowOut);

	/** Starts on Target if it is a finisher target (see UHawkeyeMeleeRules::IsFinisherTarget). */
	UFUNCTION(BlueprintCallable, Category = "Finisher")
	bool StartFinisher(AThugCharacter* Target, EHawkeyeFinisherStyle InStyle);

	/** Moves the finisher on by DeltaSeconds of game time. */
	UFUNCTION(BlueprintCallable, Category = "Finisher")
	void AdvanceFinisher(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Finisher")
	bool IsPerformingFinisher() const { return bActive; }

	/** True while the slow motion is on. */
	UFUNCTION(BlueprintPure, Category = "Finisher")
	bool IsSlowing() const { return bSlowing; }

	/** 0..1, how far the camera has pushed in; the character takes CameraPushDistance times this off its arm. */
	UFUNCTION(BlueprintPure, Category = "Finisher")
	float GetCameraPushAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Finisher")
	EHawkeyeFinisherStyle GetStyle() const { return Style; }

	UFUNCTION(BlueprintPure, Category = "Finisher")
	float GetElapsed() const { return bActive ? Elapsed : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Finisher")
	int32 GetFinisherCount() const { return FinisherCount; }

	/** The throw on his ragdoll: along Forward (Strike) or the striker's right (Bow), plus Lift up. Pure. */
	static FVector ComputeImpulse(const FVector& Forward, EHawkeyeFinisherStyle InStyle, float Speed, float Lift);

	/** Reach from the owner's capsule to the thug, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float Range = 200.f;

	/** The whole move, game seconds; input is locked and the owner invulnerable for all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.1"))
	float DurationSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.01"))
	float LungeSeconds = 0.25f;

	/** The lunge stops this far from him, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float StandOffDistance = 85.f;

	/** When the blow lands, game seconds in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float StrikeAtSeconds = 0.35f;

	/** When time slows, game seconds in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float SlowAtSeconds = 0.25f;

	/** How long time stays slow, real seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float SlowRealSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SlowDilation = 0.5f;

	/** How far in the camera pushes, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float CameraPushDistance = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.01"))
	float CameraInSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.01"))
	float CameraOutSeconds = 0.3f;

	/** The ragdoll's throw, cm/s, and its lift. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float ImpulseSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher", meta = (ClampMin = "0.0"))
	float ImpulseLift = 450.f;

	/** The blow landing (MS_Melee_Heavy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Finisher|Audio")
	TSoftObjectPtr<USoundBase> StrikeSound;

	UPROPERTY(BlueprintAssignable, Category = "Finisher")
	FOnFinisherStartedSignature OnFinisherStarted;

	UPROPERTY(BlueprintAssignable, Category = "Finisher")
	FOnFinisherStruckSignature OnFinisherStruck;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The lunge toward the target, as a root motion force on a character owner. */
	void Lunge(const AActor& Target) const;

	/** The blow: lethal damage and the ragdoll's throw. */
	void Strike();

	void BeginSlow();
	void EndSlow();

	/** Puts time, invulnerability and input back. */
	void EndFinisher();

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AThugCharacter> Victim;

	EHawkeyeFinisherStyle Style = EHawkeyeFinisherStyle::Strike;
	bool bActive = false;
	bool bStruck = false;
	bool bSlowing = false;
	bool bSlowDone = false;
	bool bOwnsInvulnerability = false;
	float Elapsed = 0.f;
	float SlowRealRemaining = 0.f;
	int32 FinisherCount = 0;
};
