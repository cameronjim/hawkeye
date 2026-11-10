#pragma once

#include "CoreMinimal.h"
#include "Combat/StrikePose.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MeleeRules.generated.h"

class AActor;
class AThugCharacter;

/** What a parry would meet on a thug right now. */
UENUM(BlueprintType)
enum class EHawkeyeParryKind : uint8
{
	/** Nothing telegraphed, or nothing a parry answers (an archer's draw). */
	None,
	/** A fists or bat swing (or the heavy's slow bat) in its wind-up. */
	Swing,
	/** The heavy's shield bash in its 0.8 s telegraph. */
	Bash,
	/** A gunner's pistol raised in its 0.8 s telegraph: the first shot is deflected. */
	Burst
};

/**
 * The melee rules that are only geometry and state: where a hit came from, the lean it gives, who
 * the soft lock picks, what a parry meets, and who a finisher can take. Pure or read-only, so the
 * tests check them without a fight.
 */
UCLASS()
class HAWKEYE_API UHawkeyeMeleeRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Which side of Forward a hit from ToAttacker (the one hit to the one hitting) came from: front
	 * and back within 45 degrees of the axis, left or right otherwise.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static EHawkeyeHitDirection ClassifyHitDirection(const FVector& Forward, const FVector& ToAttacker);

	/** The way a body facing Forward leans from a hit on Side: away from it (a hit in front tips it back). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static FVector ComputeLeanDirection(const FVector& Forward, EHawkeyeHitDirection Side);

	/**
	 * The rotation that tips the spine Degrees * Alpha toward LeanDirection (any space, flat): about the
	 * axis Up x LeanDirection, so the bone's up goes toward LeanDirection. Zero with no direction.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static FRotator ComputeHitLeanRotation(const FVector& LeanDirection, float Alpha, float Degrees);

	/**
	 * Index into Candidates of the soft-lock target: the nearest (flat distance) within Range, within
	 * AngleDegrees of Forward and within MaxHeight above or below Origin. INDEX_NONE if none.
	 */
	static int32 SelectSoftLockIndex(const FVector& Origin, const FVector& Forward, const TArray<FVector>& Candidates,
		float Range, float AngleDegrees, float MaxHeight);

	/**
	 * Index into Candidates of the melee assist's target (gameplay-semantics.md, "Melee assist"): of those
	 * within Range (flat), within HalfAngleDegrees of AimDirection and within MaxHeight above or below
	 * Origin, the one with the lowest Distance / Range + Angle / HalfAngleDegrees, so a thug straight down
	 * the stick beats a nearer one off to the side. INDEX_NONE if none.
	 */
	static int32 SelectMeleeAssistIndex(const FVector& Origin, const FVector& AimDirection, const TArray<FVector>& Candidates,
		float Range, float HalfAngleDegrees, float MaxHeight);

	/**
	 * The gap-closing step for a strike at a target DistanceToTarget away (flat, centre to centre): far
	 * enough to stop StandOff short of him, never less than the attack's own MinLunge, never more than
	 * MaxClose. Pure.
	 */
	static float ComputeGapCloseDistance(float DistanceToTarget, float StandOff, float MinLunge, float MaxClose);

	/**
	 * The assist's forgiveness at the hit: a target picked at the press who is still within
	 * Reach * ReachScale of Origin (flat, measured to his capsule's edge, TargetRadius) and within
	 * ForgiveDegrees of SwingDirection is hit even though the sweep missed him. Pure.
	 */
	static bool IsAssistHitForgiven(const FVector& Origin, const FVector& SwingDirection, const FVector& Target, float TargetRadius,
		float Reach, float ReachScale, float ForgiveDegrees);

	/** True when Target is within Range of Origin (flat) and within HalfAngleDegrees of Forward. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static bool IsInFrontWithin(const FVector& Origin, const FVector& Forward, const FVector& Target, float Range,
		float HalfAngleDegrees);

	/**
	 * What a parry would meet on Thug now: a telegraphed swing or bash in its wind-up, a gunner's raised
	 * pistol, or nothing (archers never; anyone staggered, down or dead never).
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static EHawkeyeParryKind ClassifyParry(const AThugCharacter* Thug);

	/**
	 * True when Thug is alive, open to a finisher (knocked down, parry-staggered, or within a second of a
	 * combo ender landing: AThugCharacter::IsFinisherOpen), and within Range of From (flat, 200 cm up or down).
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static bool IsFinisherTarget(const AThugCharacter* Thug, const FVector& From, float Range);

	/**
	 * Seconds Thug has been telegraphing: into his swing's or bash's wind-up, or into a gunner's raised
	 * pistol. Below 0 when he is not telegraphing.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static float GetTelegraphElapsed(const AThugCharacter* Thug);

	/**
	 * Whether a tap TelegraphElapsed seconds into a telegraph parries it, with the difficulty's
	 * WindowDelta: at 0 or above, the whole telegraph counts; below 0 its first -WindowDelta seconds do
	 * not (Hard's -0.1: read it, do not mash). A positive delta widens it before the start instead
	 * (AHawkeyeCharacter's early tap). Not telegraphing (below 0) is never in the window.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	static bool IsInParryWindow(float TelegraphElapsed, float WindowDelta);

	/** Wind-ups at least this long are telegraphs a parry answers (the thugs' 0.6 s and up; not Kate's jab). */
	static constexpr float MinTelegraphSeconds = 0.3f;
};
