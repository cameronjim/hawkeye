#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "HawkeyeSpringArmComponent.generated.h"

struct FCollisionQueryParams;

/**
 * The player's camera arm (gameplay-semantics.md, "Camera rules"). The engine's spring arm with the parts a third-person
 * action camera needs and the engine's does not have:
 *
 * - The lag history can be reset (ResetLag), and is reset on its own when the arm is teleported or the pivot jumps
 *   further than SnapDistance in a frame, or the control rotation snaps round, so each is a cut and not a long swoop.
 * - The lagged pivot trails her by at most LagMaxHorizontal / LagMaxVertical, and the lag runs on the clock the player
 *   sees (hit stop and slow motion do not stretch it). The lift (TargetOffset) is added after the lag, so it is exact.
 * - A vertical dead zone while she is in the air, so a jump's arc does not bob the view.
 * - A three-stage sweep: up from her centre to the lift, then out to the shoulder, then from the shoulder to the lens,
 *   so a wall beside her shortens the shoulder offset instead of pulling the lens into her body. Two side feelers ease
 *   the arm in before the main sweep touches anything. The arm snaps in at once and eases back out.
 * - A floor rule: the lens is never lower than FloorClearance above the floor under it or under her, whatever the
 *   pitch or the lag; the correction eases.
 * - A shake that is added after the sweep, itself swept, so a hit's shake cannot put the lens in a wall.
 *
 * AHawkeyeCharacter owns one (CameraBoom) and feeds it TargetArmLength, SocketOffset and TargetOffset every tick.
 * The pure maths is HawkeyeCameraRules (CameraRules.h).
 */
UCLASS(ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UHawkeyeSpringArmComponent : public USpringArmComponent
{
	GENERATED_BODY()

public:
	UHawkeyeSpringArmComponent();

	/**
	 * Snaps the lag history to where the arm wants to be at its next update: a camera cut. Call it after any teleport or
	 * snap of the character (AHawkeyeCharacter::NotifyTeleported does).
	 */
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ResetLag();

	/** How many times the arm has cut (ResetLag, a teleport, a pivot jump past SnapDistance, a control rotation that snapped). The camera audit watches it. */
	int32 GetCutCount() const { return CutCount; }

	/** Places the lens now, with the arm's current settings, as its own tick would. For tests, and for a cut that must show this frame. */
	void UpdateImmediately(float DeltaTime) { UpdateDesiredArmLocation(bDoCollisionTest, bEnableCameraLag, bEnableCameraRotationLag, DeltaTime); }

	/** A shake for this frame, added after the sweep: camera-local Y (right) and Z (up), cm. Set it every tick; zero for none. */
	void SetPostSweepShake(const FVector2D& LocalOffset) { PostSweepShake = LocalOffset; }

	/** True while she is in the air under her own power: the pivot's height holds inside VerticalDeadZone. Set every tick. */
	void SetVerticalHold(bool bInHold) { bVerticalHold = bInHold; }

	/** The fraction of the lens's reach the shoulder keeps (1 in the open, less beside a wall). */
	float GetShoulderScale() const { return ShoulderScale; }

	/** The fraction of the arm's reach left after the sweep and the feelers (1 in the open). */
	float GetArmFraction() const { return ArmFraction; }

	/** How far the floor rule has raised the lens this frame, cm. */
	float GetFloorCorrection() const { return FloorCorrection; }

	/** The lens as the sweeps left it, before the shake. For the debug draw and the audit. */
	FVector GetResolvedShoulder() const { return ResolvedShoulder; }

	/** The longest the lagged pivot may trail her sideways, cm (0 = no limit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Lag", meta = (ClampMin = "0.0"))
	float LagMaxHorizontal = 75.f;

	/** The longest it may trail her up or down, cm (0 = no limit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Lag", meta = (ClampMin = "0.0"))
	float LagMaxVertical = 40.f;

	/** A pivot that moves further than this in one update is a cut, cm. Zip speed is 90 per frame at 20 fps; a teleport is metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Lag", meta = (ClampMin = "0.0"))
	float SnapDistance = 400.f;

	/** While in the air the pivot's height does not follow her inside this much above or below where she took off, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Lag", meta = (ClampMin = "0.0"))
	float VerticalDeadZone = 60.f;

	/** Never closer than this to the floor under the lens or under her, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Collision", meta = (ClampMin = "0.0"))
	float FloorClearance = 35.f;

	/** How far below her centre, and below the lens, the floor is looked for, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Collision", meta = (ClampMin = "0.0"))
	float FloorSearchDistance = 600.f;

	/** Half the angle between the arm and each side feeler, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Collision", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float FeelerAngle = 16.f;

	/** The side feelers' sphere radius, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Collision", meta = (ClampMin = "0.0"))
	float FeelerRadius = 8.f;

	/** How fast the arm and the shoulder ease back out once what pulled them in is gone (per second; they snap in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Collision", meta = (ClampMin = "0.0"))
	float ReleaseSpeed = 6.f;

protected:
	// USpringArmComponent
	virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime) override;

	// USceneComponent: a teleport of the parent arrives here.
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;

private:
	/** The rotation after the (substepped) rotation lag, like the engine's. */
	FRotator LagRotation(const FRotator& DesiredRot, bool bDoRotationLag, bool bCut, float Delta);

	/** The lagged pivot, in the same substepped way the engine lags it, clamped; the lift is not in it. */
	FVector LagPivot(const FVector& Target, bool bDoLocationLag, bool bCut, float Delta);

	/** The reference height the dead zone measures from, eased to her height unless she is in the air. */
	float UpdateHoldReference(float ActualZ, bool bCut, float Delta);

	/** Stage 0 and 1: the lift up from her centre, then the shoulder out from there, each stopped by what is in the way. */
	void ResolveShoulder(const FVector& Base, const FVector& Right, const FCollisionQueryParams& Query, float Delta, bool bCut,
		FVector& OutPivotOffset, float& OutShoulderScale);

	/** Stage 2: from the shoulder to the lens with the side feelers; the lens's fraction of the way. */
	float SweepArm(const FVector& Shoulder, const FVector& Lens, const FCollisionQueryParams& Query, float Delta, bool bCut);

	/** The floor rule: raises Lens until it clears the floor under it and under her; stopped by a ceiling. */
	FVector ApplyFloorRule(const FVector& Lens, const FVector& Base, const FCollisionQueryParams& Query, float Delta, bool bCut);

	/** The highest floor under Origin within FloorSearchDistance on the camera and visibility channels, or NoFloor. */
	float FindFloor(const FVector& Origin, const FCollisionQueryParams& Query) const;

	void DrawDebug(const FVector& Pivot, const FVector& Shoulder, const FVector& Lens, const FVector& Direction) const;

	/** The pivot's un-lagged position at the last update (for the pivot-jump cut), and whether there was one. */
	FVector LastBase = FVector::ZeroVector;
	FRotator LastTargetRotation = FRotator::ZeroRotator;
	bool bHasHistory = false;
	bool bPendingReset = false;
	bool bVerticalHold = false;
	int32 CutCount = 0;

	float HoldReferenceZ = 0.f;
	bool bTeleportSeen = false;
	float LiftScale = 1.f;
	float ShoulderScale = 1.f;
	float ArmFraction = 1.f;
	float FeelerFraction = 1.f;
	float FloorCorrection = 0.f;
	FVector ResolvedShoulder = FVector::ZeroVector;
	FVector2D PostSweepShake = FVector2D::ZeroVector;
};
