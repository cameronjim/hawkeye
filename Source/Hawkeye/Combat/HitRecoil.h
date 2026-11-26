#pragma once

#include "CoreMinimal.h"
#include "HitRecoil.generated.h"

/**
 * The procedural recoil a landed blow gives the upper body (gameplay-semantics.md, "Hit recoil"): the spine tips away
 * from the blow and the head snaps further and sooner, each on its own spring, then both settle back. It is additive,
 * on top of whatever the animation plays, so a body with no reaction clip (or a clip that is nearly still) still
 * visibly takes the hit. Tuning is data; the springs are pure and tested.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeHitRecoilTuning
{
	GENERATED_BODY()

	/** The most the spine tips along the blow, degrees, for a light and a heavy hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.0"))
	float LightSpineDegrees = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.0"))
	float HeavySpineDegrees = 18.f;

	/** The most the head snaps along the blow, degrees: more than the spine, and it gets there first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.0"))
	float LightHeadDegrees = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.0"))
	float HeavyHeadDegrees = 30.f;

	/** How much of the head's sideways push turns the face as well (0 keeps it a pure nod), degrees of yaw per degree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float HeadYawShare = 0.6f;

	/** The spine spring's natural frequency, Hz, and its damping ratio (under 1: it overshoots once, then settles). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.5"))
	float SpineHz = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.05", ClampMax = "1.5"))
	float SpineDamping = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.5"))
	float HeadHz = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.05", ClampMax = "1.5"))
	float HeadDamping = 0.4f;

	/** Below this many degrees (and degrees per second) a spring is at rest and the recoil switches off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Recoil", meta = (ClampMin = "0.001"))
	float RestDegrees = 0.15f;
};

/**
 * A damped spring on a flat (X, Y) offset, in degrees: the way the part leans and how far. Stepped with the
 * closed-form solution, so any frame length is stable.
 */
struct HAWKEYE_API FHawkeyeRecoilSpring
{
	/** The offset now and its rate, degrees and degrees per second, in the horizontal plane (world, or the caller's space). */
	FVector2D Offset = FVector2D::ZeroVector;
	FVector2D Rate = FVector2D::ZeroVector;

	/**
	 * Gives the spring a kick along Direction (flat; ignored when it is zero) that carries it to PeakDegrees before it
	 * returns, for a spring of Hz and Damping. Adds to a spring already moving.
	 */
	void Kick(const FVector& Direction, float PeakDegrees, float Hz, float Damping);

	/** Moves it on by Seconds. */
	void Advance(float Seconds, float Hz, float Damping);

	bool IsAtRest(float Degrees) const;

	void Reset()
	{
		Offset = FVector2D::ZeroVector;
		Rate = FVector2D::ZeroVector;
	}

	/** Peak displacement per unit of initial rate, seconds, for an underdamped spring of Hz and Damping. */
	static float PeakPerRate(float Hz, float Damping);
};

/** The spine's and the head's springs for one body, kicked by blows and read each frame. */
struct HAWKEYE_API FHawkeyeHitRecoil
{
	FHawkeyeRecoilSpring Spine;
	FHawkeyeRecoilSpring Head;

	/** A blow along Away (the way it pushes him, flat, world); bHeavy picks the size; Scale is the hawkeye.Combat.RecoilScale CVar. */
	void Kick(const FVector& Away, bool bHeavy, const FHawkeyeHitRecoilTuning& Tuning, float Scale = 1.f);

	void Advance(float Seconds, const FHawkeyeHitRecoilTuning& Tuning);

	bool IsActive(const FHawkeyeHitRecoilTuning& Tuning) const;

	void Reset()
	{
		Spine.Reset();
		Head.Reset();
	}

	/** The spine's offset as a world vector, degrees (X, Y, 0). */
	FVector GetSpineOffset() const { return FVector(Spine.Offset.X, Spine.Offset.Y, 0.f); }

	/** The head's. */
	FVector GetHeadOffset() const { return FVector(Head.Offset.X, Head.Offset.Y, 0.f); }

	/**
	 * The additive rotation (component space) for an offset in that space: a tip of the offset's length toward its
	 * direction (the same axis the hit lean uses). Zero for no offset.
	 */
	static FRotator MakeTipRotation(const FVector& OffsetDegrees);

	/**
	 * The head's rotation: the tip, plus YawShare of the offset's push along Right (the body's right, same space) as yaw, so
	 * a blow from the right turns the face left.
	 */
	static FRotator MakeHeadRotation(const FVector& OffsetDegrees, const FVector& Right, float YawShare);
};
