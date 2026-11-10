#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/HawkeyeMovementTypes.h"
#include "HawkeyeMovementComponent.generated.h"

/** The zip's sweep hit something: Hit, whether a chain's turn was under way, whether the step reached the end. */
DECLARE_DELEGATE_RetVal_ThreeParams(EHawkeyeZipHitResponse, FHawkeyeZipHitDelegate, const FHitResult&, bool, bool);
DECLARE_DELEGATE_OneParam(FHawkeyeZipEndedDelegate, EHawkeyeZipEnd);

/**
 * The character's movement, with Hawkeye's custom modes on top of the engine's
 * (claude-docs/architecture.md, Player and framework).
 *
 * Zip (claude-docs/gameplay-semantics.md, "Zip movement"): StartZip takes the line the grapple chose
 * and moves the capsule along it in PhysZip, swept, with Velocity the real displacement over each
 * sub-step. The speed along the line starts at her velocity along it (ZipMinStartSpeed to ZipSpeed)
 * and accelerates to ZipSpeed; a chain started mid-line keeps the speed and turns onto the new line
 * over RedirectBlendSeconds. The grapple owns the rules about what the capsule may pass through, so a
 * hit is put to OnZipHit; OnZipStep and OnZipEnded tell it where the zip got to and how it finished.
 */
UCLASS(ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UHawkeyeMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** Starts a zip along Line, or, mid-line, redirects the current one onto it keeping the speed. */
	void StartZip(const FHawkeyeZipLine& Line);

	/** Lets go of the zip: Falling with its velocity times ReleaseVelocityScale, plus ReleaseJumpBoost up for a jump. */
	void ReleaseZip(bool bJump);

	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	bool IsZipping() const;

	/** In the hop up to the line's start. */
	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	bool IsZipHopping() const { return IsZipping() && bZipHopping; }

	/** A chain is still turning the direction of travel onto the new line. */
	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	bool IsZipRedirecting() const { return IsZipping() && bZipRedirecting; }

	/** 0 at the start of the line, 1 at the landing point; 0 while not zipping. */
	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	float GetZipProgress() const;

	/** Length of the current line, cm; 0 while not zipping. */
	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	float GetZipLength() const { return IsZipping() ? ZipLength : 0.f; }

	/** Speed along the line, cm/s (the hop's own velocity is separate). */
	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	float GetZipLineSpeed() const { return ZipLineSpeed; }

	/** Unit direction the zip moved in on its last step; zero while not zipping. */
	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	FVector GetZipDirection() const { return IsZipping() ? ZipDirection : FVector::ZeroVector; }

	/** Where the current line starts. */
	UFUNCTION(BlueprintPure, Category = "Movement|Zip")
	FVector GetZipLaunch() const { return ZipLaunch; }

	virtual float GetMaxSpeed() const override;
	virtual FString GetMovementName() const override;

	/** Asked whenever the zip's sweep hits something; unbound, every hit stops the zip. */
	FHawkeyeZipHitDelegate OnZipHit;

	/** After every sub-step the zip moved. */
	FSimpleDelegate OnZipStep;

	/** Once, when the zip ends for any reason, after the new movement mode is set. */
	FHawkeyeZipEndedDelegate OnZipEnded;

	/** Top speed along a zip's line, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "1.0"))
	float ZipSpeed = 1800.f;

	/** The slowest a zip starts along its line, however she was moving, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "1.0"))
	float ZipMinStartSpeed = 600.f;

	/** How fast the speed along the line rises to ZipSpeed, cm/s². 0 keeps the start speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "0.0"))
	float ZipAcceleration = 6000.f;

	/** The hop from the ground up to the line's start, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "0.0"))
	float ZipHopSeconds = 0.15f;

	/** A chain turns the direction of travel from the old line onto the new one over this long, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "0.0"))
	float RedirectBlendSeconds = 0.1f;

	/** Arriving, she moves on along the line's horizontal direction at her speed up to this, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "0.0"))
	float ArrivalCarrySpeed = 600.f;

	/** Letting go keeps the zip's velocity times this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ReleaseVelocityScale = 1.f;

	/** Letting go with jump adds this much upward velocity, cm/s; crouch adds none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Zip", meta = (ClampMin = "0.0"))
	float ReleaseJumpBoost = 350.f;

protected:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	/** Sub-steps the zip the CharacterMovementComponent way (GetSimulationTimeStep, MaxSimulationIterations). */
	void PhysZip(float DeltaTime, int32 Iterations);

	/** Eases the capsule up to the launch point, unswept as the hop always was; returns the time left after it. */
	float StepZipHop(float DeltaTime);

	/** Moves the capsule DeltaTime along the line, swept. False when the zip ended in the step. */
	bool StepZipLine(float DeltaTime);

	/** Speeds up toward ZipSpeed over DeltaTime and returns the distance covered, integrated exactly. */
	float AdvanceZipSpeed(float DeltaTime);

	/** Leaves the zip with Velocity and the mode that goes with Reason. */
	void EndZip(EHawkeyeZipEnd Reason, const FVector& NewVelocity);

private:
	FVector ZipLaunch = FVector::ZeroVector;
	FVector ZipEnd = FVector::ZeroVector;
	FVector ZipHopStart = FVector::ZeroVector;
	FVector ZipDirection = FVector::ZeroVector;
	FVector RedirectFromDirection = FVector::ZeroVector;
	float ZipLength = 0.f;
	float ZipTravelled = 0.f;
	float ZipLineSpeed = 0.f;
	float HopElapsed = 0.f;
	float RedirectElapsed = 0.f;
	bool bZipHopping = false;
	bool bZipRedirecting = false;

	/** What OnZipEnded reports for the next mode change out of the zip; anything not ours is an interruption. */
	EHawkeyeZipEnd PendingZipEnd = EHawkeyeZipEnd::Interrupted;
};
