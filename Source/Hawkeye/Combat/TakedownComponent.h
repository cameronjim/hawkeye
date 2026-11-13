#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TakedownComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UCombatAnimSet;

/** How a takedown is played (gameplay-semantics.md, "Takedown"). */
UENUM(BlueprintType)
enum class EHawkeyeTakedownVariant : uint8
{
	/** Today's: she is held still for TakedownSeconds and he drops where he stands. */
	Plain,
	/** Both clips play, side by side on the paired spot: she takes him from behind and lowers him. */
	Paired
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTakedownPerformedSignature, AActor*, Target);

/**
 * Player-side stealth takedowns: finds a nearby actor tagged "Thug" that the player is standing
 * behind, and fires ITakedownable::OnTakedown on it.
 *
 * While a takedown plays the owner is locked out of firing and moving; AHawkeyeCharacter asks
 * IsPerformingTakedown() before handling those inputs.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UTakedownComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTakedownComponent();

	/** Maximum distance from the owner to a valid target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown", meta = (ClampMin = "0.0"))
	float Range = 150.f;

	/** Radius of the sweep used to gather candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown", meta = (ClampMin = "0.0"))
	float TakedownRadius = 120.f;

	/**
	 * Half-angle, in degrees, of the cone behind the target that the attacker must stand in.
	 * 60 means the attacker must be within 60 degrees of directly behind the thug.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxAngleDegrees = 60.f;

	/** Seconds the takedown animation locks the player out of moving and firing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown", meta = (ClampMin = "0.0"))
	float TakedownSeconds = 1.2f;

	/**
	 * Loudness of the body going down, heard like her footsteps (gameplay-semantics.md, "Guard AI states":
	 * takedowns 0.6, so 720 cm at the thugs' 1200 cm hearing range; walls and closed doors stop it).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown", meta = (ClampMin = "0.0"))
	float TakedownNoiseLoudness = 0.6f;

	/** Actor tag a candidate must carry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown")
	FName TargetTag = FName(TEXT("Thug"));

	/**
	 * When false (the default) a thug that vetoes via ITakedownable::CanBeTakenDown - which the
	 * thug does while Alerted - cannot be taken down. Turn on for a forgiving difficulty mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown")
	bool bAlertedThugsAreValid = false;

	/** Object channel swept for candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Pawn;

	/** Draws the sweep for a couple of seconds in non-shipping builds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown|Debug")
	bool bDrawDebug = false;

	UPROPERTY(BlueprintAssignable, Category = "Takedown")
	FOnTakedownPerformedSignature OnTakedownPerformed;

	/** Attempts a takedown. Returns true when one was executed. */
	UFUNCTION(BlueprintCallable, Category = "Takedown")
	bool TryTakedown();

	/** Best current candidate, or nullptr. Useful for a "press E to take down" prompt. */
	UFUNCTION(BlueprintCallable, Category = "Takedown")
	AActor* FindTakedownTarget() const;

	/**
	 * True when Target is tagged, implements ITakedownable, allows the takedown, is within Range
	 * of AttackerLocation and is within MaxAngleDegrees of directly behind. Pure, so the geometry
	 * can be tested without a world or a sweep.
	 */
	UFUNCTION(BlueprintPure, Category = "Takedown")
	bool IsValidTakedownTarget(const AActor* Target, const FVector& AttackerLocation) const;

	/**
	 * The angle half of the check: is AttackerLocation inside the cone of MaxAngleDegrees around
	 * the target's backward direction? Ground-projected, so height never matters.
	 *
	 * Plain static rather than a UFUNCTION: UHT rejects a parameter that shares a name with a
	 * property on the class, and MaxAngleDegrees is both.
	 */
	static bool IsBehindTarget(const FVector& AttackerLocation, const FVector& TargetLocation, const FVector& TargetForward, float MaxAngleDegrees);

	/** True while the takedown animation is locking movement and firing out. */
	UFUNCTION(BlueprintPure, Category = "Takedown")
	bool IsPerformingTakedown() const { return bIsPerformingTakedown; }

	/** Ends the lockout early (animation notify, or the takedown being interrupted). */
	UFUNCTION(BlueprintCallable, Category = "Takedown")
	void EndTakedown();

	/** The owner's combat clips (its TakedownAttacker); null plays every takedown plain. */
	void SetAnimSet(UCombatAnimSet* InAnimSet) { AnimSet = InAnimSet; }

	/** How the last takedown was played. */
	EHawkeyeTakedownVariant GetLastVariant() const { return LastVariant; }

	/** Seconds the takedown under way locks her out for (TakedownSeconds, or the paired clip's length). */
	float GetLockSeconds() const { return LockSeconds; }

	/**
	 * Where the attacker stands for the paired takedown on a victim at VictimLocation facing VictimRotation: Gap cm
	 * straight behind him (flat), facing the way he faces, at AttackerZ. Pure.
	 */
	static FTransform ComputePairedSpot(const FVector& VictimLocation, const FRotator& VictimRotation, float Gap, float AttackerZ);

	/** Paired only when both halves have a clip and the spot is clear (no wall, ledge or gap in the way). Pure. */
	static EHawkeyeTakedownVariant ChooseVariant(bool bAttackerClip, bool bVictimClip, bool bSpotClear);

	/** The paired takedown ends early when its victim is gone or already a ragdoll another way (a blast). Pure. */
	static bool ShouldEndPairedEarly(bool bVictimValid, bool bVictimLimp);

	/** Tests: whether the paired spot for Target is clear from where the owner stands (walls, floor, his fall). */
	bool IsPairedSpotClear(const AActor& Target, const FTransform& Spot, FName* OutWhy = nullptr) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Cm between her capsule and his on the paired spot (the clips are authored at this gap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown|Paired", meta = (ClampMin = "0.0"))
	float PairedGap = 55.f;

	/** Seconds her body eases from where she stood onto the paired spot (the capsule moves at once). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown|Paired", meta = (ClampMin = "0.01"))
	float PairedEaseSeconds = 0.15f;

	/** How far behind him (from his capsule) the floor must be clear for him to be lowered onto it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown|Paired", meta = (ClampMin = "0.0"))
	float PairedFallClearance = 170.f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Takedown")
	bool bIsPerformingTakedown = false;

private:
	/** Starts the paired version on Target; false (nothing changed) when it cannot. */
	bool TryPairedTakedown(AActor* Target, float& OutSeconds);

	FTimerHandle TakedownTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<UCombatAnimSet> AnimSet;

	EHawkeyeTakedownVariant LastVariant = EHawkeyeTakedownVariant::Plain;
	float LockSeconds = 0.f;
	TWeakObjectPtr<AActor> PairedVictim;
	TWeakObjectPtr<UAnimInstance> PairedInstance;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PairedMontage;
	double PairedStartedAt = 0.0;
	bool bVictimHandedOver = false;
	bool bEasing = false;
	float EaseElapsed = 0.f;
	FTransform EaseFrom;
	FTransform EaseTo;
};
