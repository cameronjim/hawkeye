#pragma once

#include "CoreMinimal.h"
#include "GrappleRopeRules.generated.h"

/** The grapple rope's life (claude-docs/gameplay-semantics.md, "Grapple rope"). */
UENUM(BlueprintType)
enum class EGrappleRopeState : uint8
{
	/** Nothing drawn. */
	Hidden,
	/** The arrow is in the air: the rope pays out behind it, slack and sagging. */
	Flying,
	/** The arrow is in the anchor: the rope snaps straight, shivers, and holds while she is pulled. */
	Taut,
	/** She let go (or landed): the rope goes slack, whips and is pulled back to the anchor, then is gone. */
	Released
};

/** Every number of the rope. A property of UGrappleComponent (BP_HawkeyeCharacter), so it tunes without a build. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeGrappleRopeSettings
{
	GENERATED_BODY()

	/** Braided nylon cord, about the width of a thumb nail on screen, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.2"))
	float ThicknessCm = 1.4f;

	/** Straight pieces the rope is drawn in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "2", ClampMax = "64"))
	int32 SegmentCount = 18;

	/** How far a fully slack rope sags at its middle, as a share of the span between its ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.0"))
	float SagFraction = 0.10f;

	/** The sag never exceeds this, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.0"))
	float MaxSagCm = 260.f;

	/** The ripple that runs down a rope in flight: amplitude cm and frequency Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.0"))
	float FlyWaveCm = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.1"))
	float FlyWaveHz = 6.f;

	/** The sag goes out of the rope in this long once the arrow bites, s (a snap: eased out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.01"))
	float SnapSeconds = 0.10f;

	/** The shiver as it goes taut: amplitude at the middle (cm), frequency (Hz) and the time it takes to die away (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.0"))
	float VibrationCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.1"))
	float VibrationHz = 11.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.01"))
	float VibrationDecaySeconds = 0.30f;

	/** After a release the rope whips and is drawn back to the anchor over this long, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.01"))
	float ReleaseSeconds = 0.20f;

	/** The whip's amplitude (cm) and frequency (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.0"))
	float WhipCm = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple Rope", meta = (ClampMin = "0.1"))
	float WhipHz = 5.f;
};

/**
 * One rope's state machine and its shape. Pure: Step takes the frame, BuildPoints takes where the ends are now, so the
 * shape follows her hand and the arrow exactly. Flying -> Taut on Arrive, Flying or Taut -> Released on Release, and
 * Released -> Hidden when ReleaseSeconds are up (tested in Hawkeye.Grapple.Rope.*).
 */
struct HAWKEYE_API FGrappleRope
{
	/** The arrow has left the bow: the rope pays out, fully slack. */
	void Begin();

	/** The arrow bit the anchor: snap taut. Ignored unless Flying. */
	void Arrive();

	/** Let go. HandAtRelease is where the near end was: it comes away from her hand from here. Ignored when Hidden or Released. */
	void Release(const FVector& HandAtRelease);

	void Reset();

	/** Advances the clock by DeltaSeconds and eases the slack; Released ends in Hidden. */
	void Step(float DeltaSeconds, const FHawkeyeGrappleRopeSettings& Settings);

	EGrappleRopeState GetState() const
	{
		return State;
	}
	bool IsVisible() const
	{
		return State != EGrappleRopeState::Hidden;
	}

	/** 1 fully slack, 0 straight. */
	float GetSlack() const
	{
		return Slack;
	}

	/** Seconds in the current state. */
	float GetStateSeconds() const
	{
		return StateSeconds;
	}

	/**
	 * SegmentCount + 1 points from the near end to End. Near end is Hand, or once Released the point that left her hand and is
	 * being drawn in toward End. Straight to within a shiver when Taut, sagging toward the ground (and sideways when the span
	 * is steep) when slack.
	 */
	void BuildPoints(const FVector& Hand, const FVector& End, const FHawkeyeGrappleRopeSettings& Settings,
		TArray<FVector>& OutPoints) const;

	/** The most any point is off the straight line from the near end to End, cm: the sag plus the shiver. For tests and the HUD-less debug. */
	static float MaxDeviation(const TArray<FVector>& Points);

private:
	EGrappleRopeState State = EGrappleRopeState::Hidden;
	float StateSeconds = 0.f;
	float Slack = 0.f;
	FVector FrozenHand = FVector::ZeroVector;
};
