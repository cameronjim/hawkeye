#pragma once

#include "CoreMinimal.h"
#include "HawkeyeAudioTypes.generated.h"

/** The interface sounds AHawkeyePlayerController owns, played 2D and through pause. */
UENUM(BlueprintType)
enum class EHawkeyeUISound : uint8
{
	Hover,
	Click,
	ObjectiveComplete,
	NewObjective,
	Toast,
};

/** What an arrow sounds like when it lands in something. */
UENUM(BlueprintType)
enum class EHawkeyeArrowSurface : uint8
{
	Stone,
	Wood,
	Flesh,
};

/**
 * What the floor under a footstep is. Outdoors everything is snow; an interior floor says what it is
 * with an actor tag (SurfaceWood, SurfaceCarpet) that Tools/Editor/generate_interior.py writes.
 */
UENUM(BlueprintType)
enum class EHawkeyeFootstepSurface : uint8
{
	Snow,
	Wood,
	Carpet,
};

/**
 * The gain each sound class gets from the settings sliders. Every value already includes the
 * master slider: classes are overridden one by one with the product, never relying on the class
 * tree to multiply (see HawkeyeAudioMath::ComputeClassVolumes).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeClassVolumes
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float Master = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float Sfx = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float Ambient = 1.f;

	/** Menus and toasts follow the SFX slider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float UI = 1.f;

	/** The score and the win sting (SCL_Music). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float Music = 1.f;
};

/**
 * Footsteps by distance, not by animation: every WalkStrideCm walked (SprintStrideCm sprinting) on
 * the ground is one step. Pure, so the rule is tested without a world.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeFootstepTracker
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "1.0"))
	float WalkStrideCm = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "1.0"))
	float SprintStrideCm = 55.f;

	/** A frame that moves further than this is a teleport or a zip, not walking: no steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "1.0"))
	float MaxStepPerFrameCm = 300.f;

	/** Ground covered since the last step. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Audio")
	float Accumulated = 0.f;

	/**
	 * Adds DistanceCm of travel. Returns true when a step is due (at most one a call; the rest
	 * carries to the next). Off the ground the count starts again, so the first step after a
	 * landing is a full stride later; the landing makes its own sound.
	 */
	bool Advance(float DistanceCm, bool bSprinting, bool bGrounded);

	void Reset() { Accumulated = 0.f; }
};
