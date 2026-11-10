#pragma once

#include "CoreMinimal.h"
#include "StrikePose.generated.h"

/** Which arms a strike throws. There are no attack clips; the bow-IK graph reaches the hands out. */
UENUM(BlueprintType)
enum class EHawkeyeStrikePose : uint8
{
	None,
	/** The right hand punches straight out. */
	Light,
	/** Both hands drive out together. */
	Heavy,
	/** The bow hand sweeps across from the left: a finisher with the bow up. */
	BowSweep
};

/** Where a hit came from, relative to the one it hit. */
UENUM(BlueprintType)
enum class EHawkeyeHitDirection : uint8
{
	Front,
	Back,
	Left,
	Right
};

/** One frame of a strike pose: per hand, how far on (0..1) and where, in the striker's actor frame (cm). */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeStrikePoseSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Pose")
	float RightAlpha = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Pose")
	FVector RightHand = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Pose")
	FVector RightElbow = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Pose")
	float LeftAlpha = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Pose")
	FVector LeftHand = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Melee|Pose")
	FVector LeftElbow = FVector::ZeroVector;

	bool IsActive() const { return RightAlpha > 0.f || LeftAlpha > 0.f; }
};

/**
 * Where the hands go on a strike, in the striker's actor frame (X forward, Y right, Z up from the
 * capsule centre, cm), and how long they take.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeStrikePoseSettings
{
	GENERATED_BODY()

	/** The light's fist: 70 cm out at chest height, a touch right of centre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose")
	FVector LightHand = FVector(70.f, 8.f, 45.f);

	/** The heavy's fists, both out together. Y is mirrored for the left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose")
	FVector HeavyHand = FVector(65.f, 12.f, 40.f);

	/** The bow hand's sweep, from out left to across the front. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose")
	FVector SweepFrom = FVector(35.f, -50.f, 45.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose")
	FVector SweepTo = FVector(60.f, 35.f, 35.f);

	/** Where a striking elbow points (right arm; Y mirrored for the left). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose")
	FVector ElbowHint = FVector(20.f, 45.f, -25.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose", meta = (ClampMin = "0.01"))
	float LightExtendSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose", meta = (ClampMin = "0.01"))
	float LightReturnSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose", meta = (ClampMin = "0.01"))
	float HeavyExtendSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose", meta = (ClampMin = "0.01"))
	float HeavyReturnSeconds = 0.25f;

	/** Seconds the extended pose is held at the hit before it returns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose", meta = (ClampMin = "0.0"))
	float HoldSeconds = 0.f;
};

/**
 * A strike pose running: the hands go out over the extend time so they arrive as the hit lands
 * (HitAtSeconds after Start), then come back over the return time. Pure; the owner advances it and
 * hands each Sample to its bow-IK anim instance.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeStrikePoseClock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Pose")
	FHawkeyeStrikePoseSettings Settings;

	/** Starts Pose; the hands arrive HitAtSeconds from now (at once when the extend is longer). */
	void Start(EHawkeyeStrikePose InPose, float HitAtSeconds)
	{
		Pose = InPose;
		Elapsed = 0.f;
		ExtendDelay = FMath::Max(0.f, HitAtSeconds - GetExtendSeconds());
	}

	void Stop() { Pose = EHawkeyeStrikePose::None; }

	void Advance(float DeltaSeconds)
	{
		if (Pose == EHawkeyeStrikePose::None)
		{
			return;
		}
		Elapsed += FMath::Max(DeltaSeconds, 0.f);
		if (Elapsed >= ExtendDelay + GetExtendSeconds() + Settings.HoldSeconds + GetReturnSeconds())
		{
			Pose = EHawkeyeStrikePose::None;
		}
	}

	EHawkeyeStrikePose GetPose() const { return Pose; }
	bool IsActive() const { return Pose != EHawkeyeStrikePose::None; }

	/** 0 before the extend, rising to 1 as the hit lands, held, falling back to 0. */
	float GetAlpha() const
	{
		if (Pose == EHawkeyeStrikePose::None)
		{
			return 0.f;
		}
		const float Out = Elapsed - ExtendDelay;
		if (Out <= 0.f)
		{
			return 0.f;
		}
		const float Extend = GetExtendSeconds();
		if (Out < Extend)
		{
			return FMath::Clamp(Out / Extend, 0.f, 1.f);
		}
		const float Back = Out - Extend - Settings.HoldSeconds;
		return Back <= 0.f ? 1.f : FMath::Clamp(1.f - Back / GetReturnSeconds(), 0.f, 1.f);
	}

	/** This frame's hands. */
	FHawkeyeStrikePoseSample Sample() const
	{
		FHawkeyeStrikePoseSample Out;
		const float Alpha = GetAlpha();
		const FVector Mirror(1.f, -1.f, 1.f);
		switch (Pose)
		{
		case EHawkeyeStrikePose::Light:
			Out.RightAlpha = Alpha;
			Out.RightHand = Settings.LightHand;
			Out.RightElbow = Settings.ElbowHint;
			break;
		case EHawkeyeStrikePose::Heavy:
			Out.RightAlpha = Alpha;
			Out.RightHand = Settings.HeavyHand;
			Out.RightElbow = Settings.ElbowHint;
			Out.LeftAlpha = Alpha;
			Out.LeftHand = Settings.HeavyHand * Mirror;
			Out.LeftElbow = Settings.ElbowHint * Mirror;
			break;
		case EHawkeyeStrikePose::BowSweep:
			// The hand travels the arc as the pose comes on, and stays at the end of it going back.
			Out.LeftAlpha = Alpha;
			Out.LeftHand = FMath::Lerp(Settings.SweepFrom, Settings.SweepTo, GetSweepFraction());
			Out.LeftElbow = Settings.ElbowHint * Mirror;
			break;
		default:
			break;
		}
		return Out;
	}

private:
	float GetExtendSeconds() const
	{
		return Pose == EHawkeyeStrikePose::Light ? Settings.LightExtendSeconds : Settings.HeavyExtendSeconds;
	}

	float GetReturnSeconds() const
	{
		return Pose == EHawkeyeStrikePose::Light ? Settings.LightReturnSeconds : Settings.HeavyReturnSeconds;
	}

	float GetSweepFraction() const
	{
		return FMath::Clamp((Elapsed - ExtendDelay) / GetExtendSeconds(), 0.f, 1.f);
	}

	EHawkeyeStrikePose Pose = EHawkeyeStrikePose::None;
	float Elapsed = 0.f;
	float ExtendDelay = 0.f;
};

/**
 * A short lean away from a hit: the upper body tips LeanDegrees away from where it came from and
 * back, over Seconds (a sine, so it peaks half way). Pure; the owner starts it on a hit and hands the
 * direction and alpha to its bow-IK anim instance.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeHitLeanClock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Hit Lean", meta = (ClampMin = "0.01"))
	float Seconds = 0.2f;

	/** Starts a lean toward Away (world, flattened): the way from the hitter through the one hit. */
	void Start(const FVector& Away)
	{
		const FVector Flat = Away.GetSafeNormal2D();
		if (Flat.IsNearlyZero())
		{
			return;
		}
		Direction = Flat;
		Elapsed = 0.f;
		bActive = true;
	}

	void Advance(float DeltaSeconds)
	{
		if (!bActive)
		{
			return;
		}
		Elapsed += FMath::Max(DeltaSeconds, 0.f);
		bActive = Elapsed < Seconds;
	}

	bool IsActive() const { return bActive; }

	/** 0..1..0 over Seconds. */
	float GetAlpha() const { return bActive ? FMath::Sin(PI * FMath::Clamp(Elapsed / Seconds, 0.f, 1.f)) : 0.f; }

	/** The way the body leans, world, flat. */
	const FVector& GetDirection() const { return Direction; }

private:
	FVector Direction = FVector::ZeroVector;
	float Elapsed = 0.f;
	bool bActive = false;
};
