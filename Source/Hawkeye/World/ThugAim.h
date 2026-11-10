#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * How archers and gunners aim (gameplay-semantics.md, "Ranged thug accuracy"): they see Kate a lag late,
 * lead from what they saw, scatter into a cone that widens with her sideways speed and in the air, and
 * miss on purpose after she jinks. Pure, so the tests measure it without a fight; AThugAIController
 * feeds it from the world.
 */
namespace HawkeyeThugAim
{
	/** Samples are taken this often, s. */
	inline constexpr float SampleSeconds = 0.05f;
	/** And kept this long, s. */
	inline constexpr float HistorySeconds = 1.5f;
	/** Her velocity "then" is the average over this long before then, s. */
	inline constexpr float VelocityWindowSeconds = 0.15f;
	/** The cone grows by the angle her sideways speed sweeps across his view in this long, s. */
	inline constexpr float LateralSweepSeconds = 0.2f;
	/** Extra cone while she is in the air, degrees. */
	inline constexpr float AirborneConeDegrees = 3.f;
	/** A turn sharper than this... */
	inline constexpr float ErraticTurnDegrees = 90.f;
	/** ...within this long, or a dodge within it, is erratic, s. */
	inline constexpr float ErraticWindowSeconds = 0.5f;
	/** Only velocities faster than this count toward a turn, cm/s. */
	inline constexpr float ErraticMinSpeed = 150.f;
	/** Closer than this, erratic movement does not save her, cm. */
	inline constexpr float ErraticSafeDistance = 600.f;
	/** A forced miss goes her capsule radius plus this wide, cm. */
	inline constexpr float MissMargin = 50.f;
	/** The archer's and the gunner's base cones, degrees. */
	inline constexpr float ArcherBaseConeDegrees = 0.75f;
	inline constexpr float GunnerBaseConeDegrees = 6.f;
}

/** One look at the target. */
struct HAWKEYE_API FHawkeyeTargetSample
{
	double Time = 0.0;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	bool bAirborne = false;
	bool bDodging = false;
};

/** What a thug has seen of his target lately: a sample every SampleSeconds, HistorySeconds of them. */
struct HAWKEYE_API FHawkeyeTargetTrack
{
	/** Records a look at Now; one closer than SampleSeconds to the last is dropped. */
	void AddSample(double Now, const FVector& Location, const FVector& Velocity, bool bAirborne, bool bDodging);

	/** Where the target was LagSeconds before Now (interpolated) and its velocity then; false with no samples. */
	bool GetLagged(double Now, float LagSeconds, FVector& OutLocation, FVector& OutVelocity) const;

	/**
	 * True when, within ErraticWindowSeconds before Now, the flat velocity turned more than ErraticTurnDegrees
	 * (both samples faster than ErraticMinSpeed), or a dodge was seen.
	 */
	bool IsErratic(double Now) const;

	/** The newest sample, or null. */
	const FHawkeyeTargetSample* GetLatest() const { return Samples.Num() > 0 ? &Samples.Last() : nullptr; }

	void Reset() { Samples.Reset(); }

	int32 Num() const { return Samples.Num(); }

	TArray<FHawkeyeTargetSample> Samples;
};

/** One shot's inputs. */
struct HAWKEYE_API FHawkeyeRangedShot
{
	/** Where the shot leaves from. */
	FVector From = FVector::ZeroVector;
	/** Her chest now (for the distance and the forced miss) and her velocity now. */
	FVector ChestNow = FVector::ZeroVector;
	FVector VelocityNow = FVector::ZeroVector;
	/** Her chest and velocity as he saw them TrackingLag ago. */
	FVector ChestSeen = FVector::ZeroVector;
	FVector VelocitySeen = FVector::ZeroVector;
	float TrackingLagSeconds = 0.f;
	/** cm/s; 0 for a hitscan pistol. */
	float ProjectileSpeed = 0.f;
	float GravityZ = -980.f;
	float BaseConeDegrees = 0.f;
	/** The difficulty's cone scale. */
	float ConeScale = 1.f;
	bool bAirborne = false;
	bool bErratic = false;
	/** Her capsule radius, for the forced miss. */
	float TargetRadius = 34.f;
};

/** Where one shot goes and why. */
struct HAWKEYE_API FHawkeyeRangedAim
{
	/** The point to shoot at: for an arrow, the lead-and-drop point; for the pistol, along the scattered line. */
	FVector AimPoint = FVector::ZeroVector;
	/** The cone it was scattered in, degrees (0 for a forced miss). */
	float ConeDegrees = 0.f;
	/** She jinked and was far enough: this one goes wide on purpose. */
	bool bForcedMiss = false;
};

namespace HawkeyeThugAim
{
	/** The scatter cone for a shot, degrees: base, plus the sideways sweep, plus the air, times the scale. Pure. */
	HAWKEYE_API float ComputeConeDegrees(const FVector& From, const FVector& ChestSeen, const FVector& VelocitySeen,
		float BaseConeDegrees, float ConeScale, bool bAirborne);

	/** Where to shoot (gameplay-semantics.md, "Ranged thug accuracy"). Stream scatters it. */
	HAWKEYE_API FHawkeyeRangedAim ComputeShot(const FHawkeyeRangedShot& Shot, FRandomStream& Stream);

	/**
	 * A shot at Now from what Track has seen: her chest and velocity TrackingLagSeconds ago, whether she is in the
	 * air (the newest look) and whether she has jinked. ChestNow and VelocityNow are where she really is.
	 */
	HAWKEYE_API FHawkeyeRangedShot BuildShot(const FHawkeyeTargetTrack& Track, double Now, const FVector& From, const FVector& ChestNow,
		const FVector& VelocityNow, float TrackingLagSeconds, float ProjectileSpeed, float GravityZ, float BaseConeDegrees,
		float ConeScale, float TargetRadius);
}
