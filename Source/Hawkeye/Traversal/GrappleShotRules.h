#pragma once

#include "CoreMinimal.h"
#include "GrappleShotRules.generated.h"

/** Where a grapple shot is, from the press to the arrow biting the anchor (claude-docs/gameplay-semantics.md, "Grapple shot"). */
UENUM(BlueprintType)
enum class EGrappleShotPhase : uint8
{
	None,
	/** The bow arm comes up on the anchor line. */
	Raise,
	/** The string hand comes back to the cheek; the arrow is released at the end of it. */
	Draw,
	/** The arrow is in the air, the rope paying out behind it. */
	Flight,
	/** The arrow is in the anchor, the rope taut: the pull. */
	Stuck
};

/**
 * Every number of the grapple shot. A property of UGrappleComponent (BP_HawkeyeCharacter), so it tunes without a build.
 * Off by default in C++ (the unticked test worlds fire and fly the arrow in one call); create_grapple_rope.py turns it on.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeGrappleShotSettings
{
	GENERATED_BODY()

	/**
	 * The press plays the raise, draw and release before the arrow leaves, the rope (not the Niagara beam) is drawn, and the
	 * arrow lingers after a let-go. Off: the arrow leaves at the press and the old beam is drawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot")
	bool bEnabled = false;

	/** The bow arm comes up on the line this long after the press, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.0"))
	float RaiseSeconds = 0.08f;

	/** The string hand goes to the cheek, then the release; s. Raise plus this is the snap draw, about 0.18 s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.0"))
	float DrawSeconds = 0.10f;

	/**
	 * The release is never later than this share of the press-to-pull time, so a short line still sees its arrow fly:
	 * raise and draw squeeze to fit (a 2 m line, 0.33 s of arrow, releases at 0.2 s at most).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.1", ClampMax = "0.95"))
	float ReleaseMaxShare = 0.6f;

	/** The earliest release, s: below this a shot reads as the arrow appearing in the air. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.0"))
	float MinReleaseSeconds = 0.03f;

	/**
	 * Press to pull of a mid-air chain, s. ChainArrowSpeed 0 means the redirect used to start at the press; this is the
	 * compressed shot's own time (the arrow still flies it). 0 (the default since 2026-11-25) keeps the redirect at the press, as
	 * before the shot existed: 0.10 delayed it by 180 cm of travel at 1800 cm/s and the East Village lap's roof-to-roof chain hit a
	 * wall (Hawkeye.Lap.EastVillage).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.0"))
	float ChainTotalSeconds = 0.f;

	/** Added to press-to-pull on top of the arrow's own flight, s. 0: the shot costs no latency (the default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.0"))
	float ExtraLatencySeconds = 0.f;

	/** After a zip ends without a landing (let go, blocked) the arrow stays in the anchor this long, then is taken out, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.0"))
	float ArrowLingerSeconds = 0.25f;

	/** The bow stays raised and on the line while the rope is taut, and this long after it lets go, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Shot", meta = (ClampMin = "0.0"))
	float BowHoldAfterZipSeconds = 0.15f;
};

/** One shot's clock, from the press. Seconds. */
struct HAWKEYE_API FGrappleShotSchedule
{
	/** The arm is up and the string at the cheek at RaiseSeconds + DrawSeconds (the release); they are squeezed when the line is short. */
	float RaiseSeconds = 0.f;
	float DrawSeconds = 0.f;
	/** When the arrow leaves, from the press. */
	float ReleaseSeconds = 0.f;
	/** From the release to the arrow in the anchor. */
	float FlightSeconds = 0.f;
	/** From the press to the pull starting. */
	float TotalSeconds = 0.f;
	/** What the arrow flies at so it arrives at TotalSeconds, cm/s. */
	float ArrowSpeed = 0.f;
	/** How far the snap draw was squeezed: 1 is uncut. */
	float Compression = 1.f;

	/** The arrow leaves (the release) at or before this elapsed time. */
	bool HasReleased(float Elapsed) const
	{
		return Elapsed >= ReleaseSeconds;
	}
};

/** The grapple shot's pure maths (tested in Hawkeye.Grapple.Shot.*). */
struct HAWKEYE_API FGrappleShotRules
{
	/**
	 * The shot's clock for an anchor Distance cm from the bow hand with an arrow of ArrowSpeed cm/s (a chain's speed when
	 * bChain; 0 is "arrives at once"). Press to pull is the arrow's own flight time as before (Distance over speed) plus
	 * ExtraLatencySeconds, so the traversal timing does not move; the release takes RaiseSeconds + DrawSeconds out of it,
	 * never more than ReleaseMaxShare of it nor less than MinReleaseSeconds, and the arrow then flies the rest.
	 * A chain at speed 0 takes ChainTotalSeconds. Disabled settings or a bad speed give the old shot: release at once, the
	 * arrow flying the whole distance.
	 */
	static FGrappleShotSchedule MakeSchedule(
		float Distance, float ArrowSpeed, bool bChain, const FHawkeyeGrappleShotSettings& Settings);

	/** 0..1 how far the draw has come at Elapsed since the press: 0 through the raise's first moments, 1 at the release (eased). */
	static float DrawFractionAt(float Elapsed, const FGrappleShotSchedule& Schedule);

	/** 0..1 how far the bow arm is up at Elapsed: the raise, then held. */
	static float RaiseFractionAt(float Elapsed, const FGrappleShotSchedule& Schedule);

	/** The phase at Elapsed: Raise, Draw, then Flight until the arrow arrives (bArrived: Stuck). */
	static EGrappleShotPhase PhaseAt(float Elapsed, const FGrappleShotSchedule& Schedule, bool bArrived);

	/** Amount of motion blur while traversing: Base toward Traversal by Weight (0..1). */
	static float BlurAmount(float Base, float Traversal, float Weight);

	/** Weight of the traversal blur: steps toward bTraversing (1) or not (0), over InSeconds going in and OutSeconds going out. */
	static float StepBlurWeight(float Weight, bool bTraversing, float DeltaSeconds, float InSeconds, float OutSeconds);
};
