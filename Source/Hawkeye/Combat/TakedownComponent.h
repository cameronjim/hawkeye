#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TakedownComponent.generated.h"

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

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Takedown")
	bool bIsPerformingTakedown = false;

private:
	FTimerHandle TakedownTimerHandle;
};
