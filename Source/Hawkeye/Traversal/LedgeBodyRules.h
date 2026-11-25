#pragma once

#include "CoreMinimal.h"
#include "Player/HawkeyeMovementTypes.h"

#include "LedgeBodyRules.generated.h"

/**
 * The body of a ledge move (claude-docs/gameplay-semantics.md, "Ledge body"): the phases a drop to hang, a catch, a climb or
 * a vault goes through, where the hands go on the edge, how the body hangs from them, the swing it arrives with and the
 * numbers the visible body's ledge IK node (Player/LedgeIKNode.h) is handed. Plain maths, no world: UParkourComponent decides
 * the move and the capsule's path (they are unchanged); this decides how the body looks while it runs.
 *
 * Frames. Everything here is in the world, in cm. "Normal" is horizontal and points out of the ledge's face, away from the
 * wall, toward the open air she hangs in (the same Normal as FHawkeyeLedgeLine and a probed obstacle's WallNormal); she
 * hangs facing -Normal. "Along" is her right while she faces the wall. A point on the edge is the top front edge of the ledge.
 */

/** Where a ledge move is in its body: the stage picks which limbs the IK holds and how hard. */
enum class ELedgeStage : uint8
{
	None,
	/** A roof dismount: a hand plants on the parapet's top while she turns to face the wall. */
	Plant,
	/** Lowering over the edge on straightening arms. */
	Lower,
	/** Hanging on both hands, body close to the wall, toes braced on it. */
	Hang,
	/** Just caught: the swing under the hands is settling. */
	Settle,
	/** A catch: the arms reach up for the edge before the hands arrive. */
	Reach,
	/** The hands take the edge and the body swings in under them. */
	Catch,
	/** The hands let go. */
	Release,
	/** A climb or a mantle: pulling the chest to the edge. */
	Pull,
	/** The chest over the edge, the hands pressing on the top. */
	ChestOver,
	/** A knee comes up onto the top. */
	KneeUp,
	/** Standing up on the top: the IK is let go. */
	Stand,
	/** A vault: one hand plants on the top. */
	VaultPlant,
	/** A vault: the legs swing over and the hand lets go. */
	VaultSwing
};

/** The tunables of the ledge body, on UParkourComponent. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeLedgeBodySettings
{
	GENERATED_BODY()

	/** Half the gap between the hands on the edge: the shoulders' width apart, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands", meta = (ClampMin = "5.0"))
	float HandHalfGap = 19.f;

	/** A hand stays this far in from either end of the ledge line, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands", meta = (ClampMin = "0.0"))
	float HandEndMargin = 8.f;

	/** A ledge with less than this between the hands' margins is too short for two hands apart: they close on its middle, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands", meta = (ClampMin = "0.0"))
	float MinHandGap = 10.f;

	/** The wrist (the hand bone) rides this far above the top surface, cm: the palm is on it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands")
	float WristHeight = 2.5f;

	/** The wrist is this far out over the face's edge, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands")
	float WristOut = 1.f;

	/** The fingers point into the wall tipped this far down onto the top, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float FingerDownDegrees = 18.f;

	/** Pushing up over the edge the palms are this far back on the top, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands", meta = (ClampMin = "0.0"))
	float PushBack = 22.f;

	/** The shoulders hang this far out from the wall's face, cm: the chest clear of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Body", meta = (ClampMin = "0.0"))
	float ShoulderOut = 17.f;

	/** How far the arms are stretched when the body hangs from them, 1 straight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Body", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float ArmStraightness = 0.94f;

	/** The most the body is moved to hang from its hands, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Body", meta = (ClampMin = "0.0"))
	float MaxBodyShift = 40.f;

	/** The ball of the foot is held this far out from the wall's face, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Feet", meta = (ClampMin = "0.0"))
	float BallFromWall = 5.f;

	/** The swing's natural frequency, rad/s (a pendulum a metre and a half long is 2.6; this is quicker so it reads). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing", meta = (ClampMin = "0.5"))
	float SwingOmega = 5.2f;

	/** Damping ratio of the swing (under 1: it overshoots and settles). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SwingDamping = 0.22f;

	/** The wall stops the feet at this angle, degrees (feet away from the wall are positive): a small negative is the toes on the brick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing")
	float SwingWallDegrees = -3.f;

	/** How much of the swing's speed comes back off the wall, 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SwingRestitution = 0.25f;

	/** A catch starts the swing at this many degrees, plus CatchKickPerSpeed per cm/s of the fall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing", meta = (ClampMin = "0.0"))
	float CatchKickBase = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing", meta = (ClampMin = "0.0"))
	float CatchKickPerSpeed = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing", meta = (ClampMin = "0.0"))
	float MaxKick = 26.f;

	/** The swing a drop to hang or a transfer ends with, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Swing", meta = (ClampMin = "0.0"))
	float DropKick = 4.f;

	/** After arriving in a hang the stage is Settle for this long (or until the swing is still), s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Timing", meta = (ClampMin = "0.0"))
	float SettleSeconds = 1.4f;

	/** Letting go: the hands' IK fades out over this, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Timing", meta = (ClampMin = "0.02"))
	float ReleaseSeconds = 0.16f;

	/** A roof dismount has turned to face the wall by this much of its time (0 to 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Timing", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float DropTurnFraction = 0.45f;

	/** The IK is held back until the target is within this share of the arm's length (beyond it the clip's hand stays). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands", meta = (ClampMin = "0.5"))
	float ReachStart = 0.98f;

	/** ...and let go entirely beyond this share. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Body|Hands", meta = (ClampMin = "0.5"))
	float ReachEnd = 1.3f;
};

/** One stretch of a move's time, as fractions (0 to 1) of it. */
struct FLedgePhase
{
	ELedgeStage Stage = ELedgeStage::None;
	float Start = 0.f;
	float End = 0.f;
};

/** The stages a move goes through, in order, covering 0 to 1 with no gap. */
struct FLedgeSequence
{
	TArray<FLedgePhase> Phases;

	bool IsEmpty() const { return Phases.Num() == 0; }
};

/** What each limb group is held by, 0 to 1, at a point in a stage. */
struct FLedgeStageWeights
{
	float Hands = 0.f;
	float Feet = 0.f;
	float Body = 0.f;
	float Grip = 0.f;
};

/** The ledge edge a body is on: the point on the edge under her, its directions, and how far the line runs either way. */
struct FLedgeEdge
{
	FVector Point = FVector::ZeroVector;
	/** Horizontal, her right as she faces the wall. */
	FVector Along = FVector::RightVector;
	/** Horizontal, out of the face. */
	FVector Normal = -FVector::ForwardVector;
	/** Cm from Point along Along to the line's ends: Min is zero or less, Max zero or more. */
	float MinAlong = -200.f;
	float MaxAlong = 200.f;
};

/** Where the two wrists go on an edge (index 0 the left hand, 1 the right). */
struct FLedgeHandPlan
{
	FVector Hand[2] = {FVector::ZeroVector, FVector::ZeroVector};
	/** Cm along the edge from Point to each hand. */
	float AlongOffset[2] = {0.f, 0.f};
	/** The gap between them, cm. */
	float Spacing = 0.f;
	/** The ledge is too short for two hands apart: they are closed up on its middle. */
	bool bCramped = false;
};

/** Everything the visible body's ledge IK is given for one frame (world space, cm). */
struct FLedgeBodyPose
{
	ELedgeStage Stage = ELedgeStage::None;
	/** 0 to 1 through the stage. */
	float StageAlpha = 0.f;
	/** The IK's overall weight: the most any limb group is held by (0 means the node does nothing). */
	float Weight = 0.f;
	float HandAlpha[2] = {0.f, 0.f};
	float Grip[2] = {0.f, 0.f};
	FVector Hand[2] = {FVector::ZeroVector, FVector::ZeroVector};
	float FootAlpha = 0.f;
	float BodyAlpha = 0.f;
	/** How far the body hangs out from the wall, rotated about the hands' line: degrees, feet away from the wall positive. */
	float SwingDegrees = 0.f;
	/** The wall's face: out of it (horizontal), her right along it, and the plane N . X = PlaneD through the edge. */
	FVector Normal = -FVector::ForwardVector;
	FVector Along = FVector::RightVector;
	float PlaneD = 0.f;
	/** The ledge's height the hands are at. */
	float EdgeZ = 0.f;

	bool IsActive() const { return Weight > 0.001f; }
};

/** A damped pendulum for the hanging body's swing: the angle and its rate, degrees and degrees per second. */
struct FLedgePendulum
{
	float Angle = 0.f;
	float Velocity = 0.f;

	void Kick(float Degrees)
	{
		Angle = Degrees;
		Velocity = 0.f;
	}

	void Reset()
	{
		Angle = 0.f;
		Velocity = 0.f;
	}

	/** Moved on by DeltaSeconds in substeps of at most 1/120 s; the wall stops the angle at WallDegrees and gives Restitution of the speed back. */
	void Step(float DeltaSeconds, float Omega, float Damping, float WallDegrees, float Restitution);

	/** Still enough to stop caring: the angle and its rate both small. */
	bool IsSettled(float AngleTolerance = 0.5f, float VelocityTolerance = 4.f) const
	{
		return FMath::Abs(Angle) <= AngleTolerance && FMath::Abs(Velocity) <= VelocityTolerance;
	}
};

/** An arm or a leg solved to a target. */
struct FTwoBoneSolution
{
	FVector Joint = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	/** The target was out of reach (or too close) and the end was held on the nearest point the limb can take. */
	bool bClamped = false;
};

namespace HawkeyeLedge
{
	/** The stages of Move over its time; empty for a move with no ledge body. bFalling: a ledge grab that is a catch of a fall, the quick one. */
	HAWKEYE_API FLedgeSequence GetSequence(EHawkeyeParkourMove Move, bool bFalling = false);

	/** The stage at Alpha (0 to 1) of Sequence and how far (0 to 1) through that stage it is; None for an empty sequence. */
	HAWKEYE_API ELedgeStage StageAt(const FLedgeSequence& Sequence, float Alpha, float& OutStageAlpha);

	/** The fraction of Move's time by which she has turned to face the wall (1 for every move that turns over all of it). */
	HAWKEYE_API float TurnFraction(EHawkeyeParkourMove Move, const FHawkeyeLedgeBodySettings& Settings);

	/** What a stage holds, and how hard, StageAlpha of the way through it. */
	HAWKEYE_API FLedgeStageWeights StageWeights(ELedgeStage Stage, float StageAlpha);

	/** The wrist for a hand OffsetAlong cm along the edge from its Point (clamped to the line's ends by the margin), PushBack cm back onto the top. */
	HAWKEYE_API FVector WristAt(const FLedgeEdge& Edge, float OffsetAlong, float PushBackCm, const FHawkeyeLedgeBodySettings& Settings);

	/**
	 * Both wrists on the edge: HandHalfGap either side of Point, slid along so both stay HandEndMargin inside the line's
	 * ends, the gap narrowed when the ledge is shorter than it, closed up on the middle (bCramped) when less than MinHandGap
	 * is left. PushBackCm: palms back on the top (a climb's second half).
	 */
	HAWKEYE_API FLedgeHandPlan PlanHands(const FLedgeEdge& Edge, float PushBackCm, const FHawkeyeLedgeBodySettings& Settings);

	/** The pendulum's start for a catch coming down at DownSpeed cm/s, degrees (feet away from the wall). */
	HAWKEYE_API float CatchKick(float DownSpeed, const FHawkeyeLedgeBodySettings& Settings);

	/**
	 * The two-bone limb from Root with the given segment lengths, its end on Target (or the nearest point it can reach), its
	 * middle bent toward Pole (a point: the elbow's or the knee's side).
	 */
	HAWKEYE_API FTwoBoneSolution SolveTwoBone(const FVector& Root, float Upper, float Lower, const FVector& Target, const FVector& Pole);

	/** 1 while Distance is within ReachStart of Length, falling smoothly to 0 at ReachEnd of it: a target too far away is left to the clip. */
	HAWKEYE_API float ReachWeight(float Distance, float Length, const FHawkeyeLedgeBodySettings& Settings);

	/**
	 * Where the shoulders' midpoint hangs from hands at HandMid: ShoulderOut out from the wall (along Normal), as far below
	 * the hands as ArmLength * ArmStraightness leaves, directly under them along the edge.
	 */
	HAWKEYE_API FVector ShoulderTarget(const FVector& HandMid, const FVector& Normal, float ArmLength, const FHawkeyeLedgeBodySettings& Settings);

	/** The turn about the hands' line that swings the body Degrees (feet away from the wall positive): about Normal x Up. */
	HAWKEYE_API FQuat SwingTurn(const FVector& Normal, float Degrees);

	/**
	 * The rotation that carries FromA onto ToA and then turns about ToA so FromB lies in the plane of ToA and ToB on the same
	 * side as ToB: a frame taken onto another by two directions.
	 */
	HAWKEYE_API FQuat AlignFrames(const FVector& FromA, const FVector& FromB, const FVector& ToA, const FVector& ToB);

	/** The hand's wanted fingers and palm directions: fingers into the wall tipped down, the palm down on the top. */
	HAWKEYE_API void HandAxes(const FVector& Normal, float FingerDownDegrees, FVector& OutFingers, FVector& OutPalm);

	/** A foot's ball moved along Normal so it is BallFromWall off the plane N . X = PlaneD. */
	HAWKEYE_API FVector BallOnPlane(const FVector& Ball, const FVector& Normal, float PlaneD, float BallFromWall);
}
