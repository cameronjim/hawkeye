#pragma once

#include "CoreMinimal.h"
#include "TraversalPoseRules.generated.h"

/**
 * Which part of a grapple trip the body is in (claude-docs/gameplay-semantics.md, "Traversal body pose"). Chosen every
 * frame from the movement's own state; nothing here moves the capsule.
 */
UENUM(BlueprintType)
enum class ETraversalPhase : uint8
{
	None,
	/** The hop from the ground up to the line's start: the crouch-load and the reach. */
	Launch,
	/** Hauled along the line by the rope hand. */
	Pull,
	/** A chain's mid-air turn onto a new line: the body swings through. */
	Redirect,
	/** Let go of the line: the tuck, for the first ReleaseSeconds. */
	Release,
	/** In the air after a release (or an arrival with no floor), reacting to vertical speed. */
	Fall,
	/** On the landing point, in the step-down, two-step or slide-out the zip's length and surface ask for. */
	Arrive,
	/** The arrow is in flight and she is on the ground: the crouch-load, before the hop. Visual only. */
	Load,
	/** The last BraceSeconds of the pull: the legs swing forward and up to meet the surface and the chest comes upright. */
	Brace
};

/** How a zip's arrival is taken: by the zip's length, the line's slope and whether she holds forward (never by line speed,
 * which is always about the top speed by the end). */
UENUM(BlueprintType)
enum class EArrivalKind : uint8
{
	None,
	/** A zip shorter than ShortZipCm: a soft step down. */
	StepDown,
	/** The default: a braking two-step with the knees bent. */
	Brake,
	/** The line is about level with the surface and forward is held: a slide out. */
	SlideOut
};

/**
 * The landing clip family (the Game Animation Sample's M_Neutral_Jump_F_Land_*): impact speed picks light or heavy, the
 * speed along the ground picks stand, walk, run or sprint, and a roll or a stumble keeps the outcome the landing rules
 * chose from the fall height. The anim set roles of the same names (ECombatAnimRole::LandStandLight ...).
 */
UENUM(BlueprintType)
enum class ELandingClip : uint8
{
	None,
	StandLight,
	StandHeavy,
	WalkLight,
	WalkHeavy,
	RunLight,
	RunHeavy,
	SprintLight,
	SprintHeavy,
	Roll,
	Stumble
};

/** Every number of the traversal body pose. Properties on AHawkeyeCharacter (BP_Kate), so they tune without a build. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeTraversalPoseSettings
{
	GENERATED_BODY()

	/** Gravity the acceleration lean is measured against, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean", meta = (ClampMin = "1.0"))
	float Gravity = 980.f;

	/** The line speed at which the legs trail fully, cm/s (the zip's top speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean", meta = (ClampMin = "1.0"))
	float ReferenceSpeed = 1800.f;

	/** atan2(horizontal acceleration, gravity) is scaled by this (0.3 to 0.7 reads as weight; Insomniac-style). */
	UPROPERTY(
		EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AccelLeanStrength = 0.5f;

	/** Accelerations above this are clamped before they lean the body (the hop's end hands over 600 cm/s in a frame),
	 * cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean", meta = (ClampMin = "0.0"))
	float MaxAcceleration = 8000.f;

	/** How far the body's axis tips toward the rope: this fraction of the rope's angle from vertical. */
	UPROPERTY(
		EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RopeTiltWeight = 0.5f;

	/** The most the whole body tips (rope tilt plus acceleration lean), degrees. */
	UPROPERTY(
		EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float MaxTiltDegrees = 55.f;

	/** Share of the tip taken by the pelvis; the rest is spread over the spine by SpineWeights. */
	UPROPERTY(
		EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PelvisTiltShare = 0.6f;

	/** spine_01, spine_02, spine_03 shares of the rest of the tip; normalised to sum to 1, the lower spine heavier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Lean")
	FVector SpineWeights = FVector(0.5f, 0.3f, 0.2f);

	/** Critically damped spring half-life of the body tilt, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs", meta = (ClampMin = "0.01"))
	float TiltHalfLife = 0.10f;

	/** Half-life of the legs, the sink and the lag, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs", meta = (ClampMin = "0.01"))
	float LegHalfLife = 0.15f;

	/** Half-life of the arms' reach, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs", meta = (ClampMin = "0.01"))
	float ArmHalfLife = 0.06f;

	/** Half-life of the legs, sink and tilt in the hop, s (quick: the whole crouch-load is 0.15 s long). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs", meta = (ClampMin = "0.01"))
	float LaunchHalfLife = 0.04f;

	/** Half-life of the legs and body in a landing, s (quick: the feet have just hit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs", meta = (ClampMin = "0.01"))
	float LandingHalfLife = 0.05f;

	/** The jerk when the hop hands over to the line: tilt velocity added forward, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs")
	float LaunchKickDegPerSecond = 500.f;

	/** Seconds the pose takes to come in and to go out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs", meta = (ClampMin = "0.0"))
	float WeightInSeconds = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Springs", meta = (ClampMin = "0.0"))
	float WeightOutSeconds = 0.25f;

	/** At full speed the trailing leg swings this far back from the hip, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float MaxLegTrail = 30.f;

	/** The leading leg trails this much less than the other, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float LeadLegAdvance = 14.f;

	/** The whole body tip plus the leg trail never lets the feet go further back than this, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float MaxLegTotal = 75.f;

	/** Knees, bent slightly even at rest, and how much more they bend as the legs trail (degrees; per degree of trail).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float KneeBendBase = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float KneeBendPerTrail = 0.5f;

	/** The pelvis shifts back (and down) with the acceleration: cm per cm/s^2, clamped to MaxLagCm and MaxSinkCm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float LagPerAcceleration = 0.0015f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float MaxLagCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Legs")
	float MaxSinkCm = 10.f;

	/** Where the pelvis sits against the capsule centre, cm (actor frame). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arms")
	FVector PelvisFromCentre = FVector(0.f, 0.f, -10.f);

	/** The rope shoulder against the pelvis, cm (body frame, X forward, Y right: the rope hand is the left). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arms")
	FVector ShoulderFromPelvis = FVector(0.f, -18.f, 50.f);

	/** How far from the shoulder the rope hand reaches along the rope, cm (a little under the arm's length, so the
	 * elbow stays soft). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arms", meta = (ClampMin = "0.0"))
	float ArmReach = 38.f;

	/** The free (right) hand while hauled: back and out for balance, body frame, cm from the pelvis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arms")
	FVector FreeHandBack = FVector(-22.f, 52.f, 22.f);

	/** The free hand held out in front for the brace, body frame, cm from the pelvis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arms")
	FVector FreeHandBrace = FVector(52.f, 30.f, 44.f);

	/** The hands thrown out and up when falling, body frame, cm from the pelvis (Y mirrored for the left). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arms")
	FVector HandsOut = FVector(8.f, 55.f, 58.f);

	/** The head turns toward the anchor by this much of the way, and never past these. */
	UPROPERTY(
		EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Head", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeadLookWeight = 0.5f;

	/** How much of the body's tip the neck takes back so the head stays level. */
	UPROPERTY(
		EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Head", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeadStabilise = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Head")
	float MaxHeadPitch = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Head")
	float MaxHeadYaw = 40.f;

	/** The crouch-load, played while the grapple arrow is in flight (before the hop) and let go into the hop: pelvis down,
	 * hips and knees bent, the chest over. It ramps in over LoadSeconds (the arrow's flight is longer than that for any
	 * anchor past 6 m, so it is usually at full by the hop); a shorter flight shows the part of it that fits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Launch", meta = (ClampMin = "0.01"))
	float LoadSeconds = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Launch")
	float LaunchSinkCm = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Launch")
	float LaunchLeanDegrees = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Launch")
	float LaunchHipFlex = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Launch")
	float LaunchKnee = 65.f;

	/** Half-life of the legs, sink and tilt while the crouch loads, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Launch", meta = (ClampMin = "0.01"))
	float LoadHalfLife = 0.06f;

	/** Seconds after a release the legs hold the tuck before they go to the fall pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Release", meta = (ClampMin = "0.0"))
	float ReleaseSeconds = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Release")
	float TuckHipFlex = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Release")
	float TuckKnee = 75.f;

	/** The fall: downward speed at which the arms are fully out and the legs fully braced, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Fall", meta = (ClampMin = "1.0"))
	float FallFullSpeed = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Fall")
	float FallHipFlex = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Fall")
	float FallKnee = 32.f;

	/** The back arches back this much at full fall speed, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Fall")
	float FallArch = 8.f;

	/** The last this many seconds of the pull are the brace (time to arrival from the zip's progress, length and line
	 * speed); a zip shorter than ShortZipCm has none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.0"))
	float BraceSeconds = 0.3f;

	/** The brace: legs swung forward (hip flex, one leading) and the knees bent to meet the surface, degrees; the chest's
	 * tip left over, as a share of what the pull had. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive")
	float BraceHipFlexLead = 62.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive")
	float BraceHipFlexTrail = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive")
	float BraceKnee = 38.f;

	UPROPERTY(
		EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BraceTiltLeft = 0.08f;

	/** Half-life of the legs in the brace, s (the swing is 0.3 s long). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.01"))
	float BraceHalfLife = 0.035f;

	/** Zips shorter than this (cm) arrive in a soft step down, with no brace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.0"))
	float ShortZipCm = 250.f;

	/** The line is level with the surface when it climbs or falls under this many degrees; with forward held that is a
	 * slide out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.0"))
	float SlideLevelDegrees = 12.f;

	/** How long each arrival's pose runs, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.05"))
	float StepDownSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.05"))
	float BrakeSeconds = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Arrive", meta = (ClampMin = "0.05"))
	float SlideOutSeconds = 0.9f;

	/** Impact speeds (cm/s, down) at which a landing is worth a pose: light from here, heavy from the next. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Landing", meta = (ClampMin = "0.0"))
	float LightImpactSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Landing", meta = (ClampMin = "0.0"))
	float HeavyImpactSpeed = 700.f;

	/** Ground speeds (cm/s) from which a landing is a walk, a run or a sprint landing; below the first, a stand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Landing", meta = (ClampMin = "0.0"))
	float WalkLandingSpeed = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Landing", meta = (ClampMin = "0.0"))
	float RunLandingSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Landing", meta = (ClampMin = "0.0"))
	float SprintLandingSpeed = 600.f;

	/**
	 * Play rate of a heavy landing's clip (the sample's own held the crouch at the bottom for about 0.6 s; at 1.6 the
	 * hold is about 0.3 s and the rise stays smooth, the clip's own blend). Light landings play as they were made.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal Pose|Landing", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float HeavyLandingRate = 1.6f;
};

/** What the pose needs to know this frame. World space unless said; the character fills it from its movement. */
struct HAWKEYE_API FTraversalPoseInput
{
	float DeltaSeconds = 0.f;
	bool bZipping = false;
	bool bHopping = false;
	bool bRedirecting = false;
	bool bFalling = false;
	bool bGrounded = false;
	/** The first frame after a teleport: nothing carries over. */
	bool bTeleported = false;
	/** The fall loop clip plays: the fall pose shrinks to a reaction on top of it. */
	bool bFallClipPlays = false;
	/** 0..1 through the hop (Launch). */
	float HopFraction = 0.f;
	/** The zip's speed along its line. */
	float ZipLineSpeed = 0.f;
	/** 0..1 along the line and its length, cm: the time to arrival is what is left of both over the line speed. */
	float ZipProgress = 0.f;
	float ZipLength = 0.f;
	/** The grapple arrow is in the air (she is not yet zipping). */
	bool bArrowInFlight = false;
	/** The stick is held toward where she faces: a slide out rather than a two-step. */
	bool bForwardHeld = false;
	FVector Velocity = FVector::ZeroVector;
	/** Unit direction from the rope hand toward the anchor; zero when there is none. */
	FVector RopeDirection = FVector::ZeroVector;
	FQuat ActorRotation = FQuat::Identity;
};

/**
 * One frame of the body pose: rotations as the actor frame sees them (X forward, Y right, Z up; turns about those
 * axes, offsets in cm), hand targets from the capsule centre. The anim instance puts them in component space.
 */
struct HAWKEYE_API FTraversalPoseOutput
{
	ETraversalPhase Phase = ETraversalPhase::None;
	float PhaseSeconds = 0.f;
	/** 0..1 how far the pose is in; 0 means every other field is the identity. */
	float Weight = 0.f;
	/** The smoothed whole-body tip (forward, toward the right), degrees. */
	FVector2D Tilt = FVector2D::ZeroVector;
	FQuat Pelvis = FQuat::Identity;
	FVector PelvisOffset = FVector::ZeroVector;
	FQuat Spine[3] = {FQuat::Identity, FQuat::Identity, FQuat::Identity};
	FQuat Neck = FQuat::Identity;
	FQuat ThighL = FQuat::Identity;
	FQuat ThighR = FQuat::Identity;
	FQuat CalfL = FQuat::Identity;
	FQuat CalfR = FQuat::Identity;
	/** The rope (left) hand and its elbow, with how far on the IK is. */
	float LeftHandAlpha = 0.f;
	FVector LeftHand = FVector::ZeroVector;
	FVector LeftElbow = FVector::ZeroVector;
	/** The free (right) hand. */
	float RightHandAlpha = 0.f;
	FVector RightHand = FVector::ZeroVector;
	FVector RightElbow = FVector::ZeroVector;
	/** The landing's clip when one plays, the pose is then only the legs' absorb. */
	EArrivalKind Arrival = EArrivalKind::None;
	/** 0..1 how far the brace's forward reach is in (the weight included). */
	float Braced = 0.f;

	bool IsActive() const
	{
		return Weight > 0.001f;
	}
};

/** The traversal body pose's pure maths (claude-docs/gameplay-semantics.md, "Traversal body pose"; tested in
 * Hawkeye.Traversal.Pose.*). */
struct HAWKEYE_API FTraversalPoseRules
{
	/** ln 2 times 4: the critically damped spring's damping for a half-life of 1 s. */
	static float HalfLifeToDamping(float HalfLife);

	/**
	 * One exact step of a critically damped spring toward Goal with the given half-life (Holden, "Spring-It-On"):
	 * y = 2 ln 2 / h, x' = e^(-y dt) (j0 + j1 dt) + goal with j0 = x - goal and j1 = v + y j0. Does not overshoot with
	 * no velocity, and gives the same answer for one big step as many small ones.
	 */
	static void SpringStep(float& Value, float& Velocity, float Goal, float HalfLife, float DeltaSeconds);

	/** atan2(HorizontalAcceleration, Gravity) times Scale, degrees: the lean of a body that is being accelerated. */
	static float LeanDegrees(float HorizontalAcceleration, float Gravity, float Scale);

	/** The acceleration lean as (forward, right) degrees, from an acceleration in the actor frame. */
	static FVector2D AccelerationTilt(const FVector& LocalAcceleration, float Gravity, float Scale);

	/** The tip toward the rope as (forward, right) degrees: Weight times the rope's angle from vertical, toward its
	 * horizontal side. */
	static FVector2D RopeTilt(const FVector& LocalRopeDirection, float Weight);

	/** Tilt scaled down (never turned) so its length is at most MaxDegrees. */
	static FVector2D ClampTilt(const FVector2D& Tilt, float MaxDegrees);

	/**
	 * The turn that tips the actor's up toward (Forward, Right) by their length in degrees: about the axis up x
	 * direction. Positive Forward leans the head toward +X.
	 */
	static FQuat QuatFromTilt(float ForwardDegrees, float RightDegrees);

	/** The turn of Rotation made about the given actor-frame axis, scaled by Weight (0 is the identity). */
	static FQuat ScaledTurn(const FVector& Axis, float Degrees, float Weight);

	/**
	 * A turn about world axes (an actor-frame turn Local) as a bone sees it: the turn of Local's axes taken into the
	 * component's frame. ActorRotation and ComponentRotation are world rotations; the result is what an additive
	 * component-space bone modification wants.
	 */
	static FQuat ActorToComponent(const FQuat& Local, const FQuat& ActorRotation, const FQuat& ComponentRotation);

	/** Seconds-long envelope (t / a) e^(1 - t / a): rises to 1 at t = a, then falls away; 0 before 0. For a dip that
	 * recovers. */
	static float DipEnvelope(float Seconds, float Attack);

	/** exp(-((t - c) / w)^2): a short pulse at Centre (the braking steps). */
	static float Pulse(float Seconds, float Centre, float Width);

	/** Seconds to arrival: what is left of the line over its speed; a large number when the speed is nothing. */
	static float TimeToArrival(float Progress, float Length, float LineSpeed);

	/**
	 * The arrival a zip asks for: shorter than ShortZipCm a step-down; a line within SlideLevelDegrees of level (the
	 * climb or fall of the line, degrees, either way) with forward held a slide out; any other, from below a roof's edge
	 * included, the braking two-step.
	 */
	static EArrivalKind ClassifyArrival(
		float ZipLength, float LineElevationDegrees, bool bForwardHeld, const FHawkeyeTraversalPoseSettings& Settings);

	/** The length of an arrival's pose, s. */
	static float ArrivalSeconds(EArrivalKind Kind, const FHawkeyeTraversalPoseSettings& Settings);

	/**
	 * The clip a landing plays. A roll or a stumble (what the landing rules chose from the fall height) keeps itself;
	 * otherwise impact speed (cm/s, down) under LightImpactSpeed is None, under HeavyImpactSpeed Light, else Heavy,
	 * and GroundSpeed picks stand, walk, run or sprint.
	 */
	static ELandingClip ClassifyLanding(
		float ImpactSpeed, float GroundSpeed, bool bRoll, bool bStumble, const FHawkeyeTraversalPoseSettings& Settings);

	/** The play rate of a landing's clip: HeavyLandingRate for the heavy stand, walk, run and sprint, else 1. */
	static float LandingClipRate(ELandingClip Clip, const FHawkeyeTraversalPoseSettings& Settings);

	/** Impact speed of a free fall of Height cm under Gravity, cm/s: what a fall height means as speed. */
	static float ImpactSpeedFromHeight(float Height, float Gravity);

	/** Same angle wrapped to -180..180 then clamped to +- Limit. */
	static float ClampAngle(float Degrees, float Limit);
};

/**
 * The body pose over time: springs on the tilt, legs, sink and arms, the phase, and the arrival and landing envelopes.
 * Pure: Step takes the frame's input and returns the output; the anim instance owns the clock. Reset forgets everything
 * (a teleport).
 */
struct HAWKEYE_API FTraversalPoseState
{
	/** Advances DeltaSeconds and returns the pose. */
	FTraversalPoseOutput Step(const FTraversalPoseInput& Input, const FHawkeyeTraversalPoseSettings& Settings);

	/**
	 * A landing from a fall: the legs absorb it as ImpactSpeed asks, unless a clip plays (bClipPlays), when the pose
	 * only follows the clip with nothing added. Call once, as she touches down.
	 */
	void BeginLanding(
		ELandingClip Clip, float ImpactSpeed, bool bClipPlays, const FHawkeyeTraversalPoseSettings& Settings);

	void Reset();

	ETraversalPhase GetPhase() const
	{
		return Phase;
	}
	EArrivalKind GetArrival() const
	{
		return Arrival;
	}
	bool IsLanding() const
	{
		return LandingSeconds >= 0.f;
	}

	/** The channels the springs smooth. */
	enum EChannel : int32
	{
		TiltForward,
		TiltRight,
		ThighL,
		ThighR,
		CalfL,
		CalfR,
		Sink,
		Lag,
		LeftArm,
		RightArm,
		ArmsOut,
		Brace,
		Count
	};

	float GetChannel(EChannel Channel) const
	{
		return Value[Channel];
	}

private:
	struct FTargets
	{
		float Goal[Count] = {};
		/** Which half-life each channel's spring uses this frame. */
		float HalfLife[Count] = {};
		float WeightTarget = 0.f;
	};

	void UpdatePhase(
		const FTraversalPoseInput& Input, float DeltaSeconds, const FHawkeyeTraversalPoseSettings& Settings);
	void AimTargets(const FTraversalPoseInput& Input, const FHawkeyeTraversalPoseSettings& Settings,
		const FVector& LocalAcceleration, const FVector& LocalRope, FTargets& Out) const;
	void ArrivalTargets(const FHawkeyeTraversalPoseSettings& Settings, FTargets& Out) const;
	void LandingTargets(const FHawkeyeTraversalPoseSettings& Settings, FTargets& Out) const;
	FTraversalPoseOutput Compose(const FTraversalPoseInput& Input, const FHawkeyeTraversalPoseSettings& Settings,
		const FVector& LocalRope) const;

	ETraversalPhase Phase = ETraversalPhase::None;
	float PhaseSeconds = 0.f;
	float Weight = 0.f;
	float Value[Count] = {};
	float Speed[Count] = {};

	bool bWasZipping = false;
	bool bWasHopping = false;
	float LastLineSpeed = 0.f;
	float LastZipLength = 0.f;
	float LastLineElevation = 0.f;
	/** Forward was held at any time in the brace. */
	bool bHeldForward = false;
	/** The crouch-load played before this hop (it carries into the hop instead of the hop making its own). */
	bool bLoaded = false;
	FVector LastVelocity = FVector::ZeroVector;
	bool bHaveLast = false;

	EArrivalKind Arrival = EArrivalKind::None;
	float ArrivalElapsed = 0.f;

	/** Seconds into a landing's absorb, or -1 when none. */
	float LandingSeconds = -1.f;
	float LandingDepth = 0.f;
	float LandingKnee = 0.f;
	float LandingLean = 0.f;
	float LandingLength = 0.f;
	/** Seconds the pose takes to go out, overriding the setting while a clip takes over; negative for the setting. */
	float FadeOutOverride = -1.f;
};
