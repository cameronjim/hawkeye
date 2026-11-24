#include "Traversal/TraversalPoseRules.h"

namespace HawkeyeTraversalPoseMath
{
	static float Smooth(float Low, float High, float Value)
	{
		return FMath::SmoothStep(Low, High, Value);
	}

	/** The weights of the spine, normalised so they sum to 1 (a zero vector spreads it evenly). */
	static FVector NormalisedSpineWeights(const FVector& Weights)
	{
		const FVector Positive(FMath::Max(0., Weights.X), FMath::Max(0., Weights.Y), FMath::Max(0., Weights.Z));
		const double Sum = Positive.X + Positive.Y + Positive.Z;
		return Sum > UE_KINDA_SMALL_NUMBER ? Positive / Sum : FVector(1. / 3.);
	}

	/** The degrees an actor-frame direction is above the horizon. */
	static float ElevationDegrees(const FVector& Direction)
	{
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(static_cast<float>(Direction.Z), -1.f, 1.f)));
	}
}

float FTraversalPoseRules::HalfLifeToDamping(float HalfLife)
{
	return (4.f * UE_LN2) / FMath::Max(HalfLife, 0.0001f);
}

void FTraversalPoseRules::SpringStep(float& Value, float& Velocity, float Goal, float HalfLife, float DeltaSeconds)
{
	const float Y = HalfLifeToDamping(HalfLife) * 0.5f;
	const float J0 = Value - Goal;
	const float J1 = Velocity + J0 * Y;
	const float Decay = FMath::Exp(-Y * DeltaSeconds);
	Value = Decay * (J0 + J1 * DeltaSeconds) + Goal;
	Velocity = Decay * (Velocity - J1 * Y * DeltaSeconds);
}

float FTraversalPoseRules::LeanDegrees(float HorizontalAcceleration, float Gravity, float Scale)
{
	return FMath::RadiansToDegrees(FMath::Atan2(HorizontalAcceleration, FMath::Max(Gravity, 1.f))) * Scale;
}

FVector2D FTraversalPoseRules::AccelerationTilt(const FVector& LocalAcceleration, float Gravity, float Scale)
{
	const FVector2D Flat(LocalAcceleration.X, LocalAcceleration.Y);
	const float Size = static_cast<float>(Flat.Size());
	if (Size < UE_KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	return (Flat / Size) * LeanDegrees(Size, Gravity, Scale);
}

FVector2D FTraversalPoseRules::RopeTilt(const FVector& LocalRopeDirection, float Weight)
{
	const FVector Rope = LocalRopeDirection.GetSafeNormal();
	const FVector2D Flat(Rope.X, Rope.Y);
	const float Size = static_cast<float>(Flat.Size());
	if (Size < UE_KINDA_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	const float FromVertical = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(Rope.Z), -1.f, 1.f)));
	return (Flat / Size) * (FromVertical * Weight);
}

FVector2D FTraversalPoseRules::ClampTilt(const FVector2D& Tilt, float MaxDegrees)
{
	const float Size = static_cast<float>(Tilt.Size());
	const float Limit = FMath::Max(MaxDegrees, 0.f);
	return Size > Limit && Size > UE_KINDA_SMALL_NUMBER ? Tilt * (Limit / Size) : Tilt;
}

FQuat FTraversalPoseRules::QuatFromTilt(float ForwardDegrees, float RightDegrees)
{
	const FVector2D Tilt(ForwardDegrees, RightDegrees);
	const float Size = static_cast<float>(Tilt.Size());
	if (Size < UE_KINDA_SMALL_NUMBER)
	{
		return FQuat::Identity;
	}
	// Up turned toward the direction D by Size is a turn about up x D.
	const FVector2D Direction = Tilt / Size;
	const FVector Axis = FVector::CrossProduct(FVector::UpVector, FVector(Direction.X, Direction.Y, 0.f));
	return FQuat(Axis.GetSafeNormal(), FMath::DegreesToRadians(Size));
}

FQuat FTraversalPoseRules::ScaledTurn(const FVector& Axis, float Degrees, float Weight)
{
	const FVector Unit = Axis.GetSafeNormal();
	if (Unit.IsNearlyZero())
	{
		return FQuat::Identity;
	}
	return FQuat(Unit, FMath::DegreesToRadians(Degrees * Weight));
}

FQuat FTraversalPoseRules::ActorToComponent(const FQuat& Local, const FQuat& ActorRotation, const FQuat& ComponentRotation)
{
	// The same turn about world axes, then written in the component's axes.
	const FQuat World = ActorRotation * Local * ActorRotation.Inverse();
	return (ComponentRotation.Inverse() * World * ComponentRotation).GetNormalized();
}

float FTraversalPoseRules::DipEnvelope(float Seconds, float Attack)
{
	if (Seconds <= 0.f)
	{
		return 0.f;
	}
	const float X = Seconds / FMath::Max(Attack, 0.001f);
	return X * FMath::Exp(1.f - X);
}

float FTraversalPoseRules::Pulse(float Seconds, float Centre, float Width)
{
	const float X = (Seconds - Centre) / FMath::Max(Width, 0.001f);
	return FMath::Exp(-X * X);
}

EArrivalKind FTraversalPoseRules::ClassifyArrival(float LineSpeed, const FHawkeyeTraversalPoseSettings& Settings)
{
	if (LineSpeed < Settings.ArrivalStepMaxSpeed)
	{
		return EArrivalKind::StepDown;
	}
	return LineSpeed <= Settings.ArrivalBrakeMaxSpeed ? EArrivalKind::Brake : EArrivalKind::SlideOut;
}

float FTraversalPoseRules::ArrivalSeconds(EArrivalKind Kind, const FHawkeyeTraversalPoseSettings& Settings)
{
	switch (Kind)
	{
	case EArrivalKind::StepDown:
		return Settings.StepDownSeconds;
	case EArrivalKind::Brake:
		return Settings.BrakeSeconds;
	case EArrivalKind::SlideOut:
		return Settings.SlideOutSeconds;
	default:
		return 0.f;
	}
}

ELandingClip FTraversalPoseRules::ClassifyLanding(float ImpactSpeed, float GroundSpeed, bool bRoll, bool bStumble,
	const FHawkeyeTraversalPoseSettings& Settings)
{
	if (bRoll)
	{
		return ELandingClip::Roll;
	}
	if (bStumble)
	{
		return ELandingClip::Stumble;
	}
	if (ImpactSpeed < Settings.LightImpactSpeed)
	{
		return ELandingClip::None;
	}
	const bool bHeavy = ImpactSpeed >= Settings.HeavyImpactSpeed;
	if (GroundSpeed < Settings.WalkLandingSpeed)
	{
		return bHeavy ? ELandingClip::StandHeavy : ELandingClip::StandLight;
	}
	if (GroundSpeed < Settings.RunLandingSpeed)
	{
		return bHeavy ? ELandingClip::WalkHeavy : ELandingClip::WalkLight;
	}
	if (GroundSpeed < Settings.SprintLandingSpeed)
	{
		return bHeavy ? ELandingClip::RunHeavy : ELandingClip::RunLight;
	}
	return bHeavy ? ELandingClip::SprintHeavy : ELandingClip::SprintLight;
}

float FTraversalPoseRules::ImpactSpeedFromHeight(float Height, float Gravity)
{
	return FMath::Sqrt(FMath::Max(2.f * Gravity * Height, 0.f));
}

float FTraversalPoseRules::ClampAngle(float Degrees, float Limit)
{
	return FMath::Clamp(static_cast<float>(FRotator::NormalizeAxis(Degrees)), -Limit, Limit);
}

// --- The state ----------------------------------------------------------------------------------------------------

void FTraversalPoseState::Reset()
{
	*this = FTraversalPoseState();
}

void FTraversalPoseState::BeginLanding(ELandingClip Clip, float ImpactSpeed, bool bClipPlays,
	const FHawkeyeTraversalPoseSettings& Settings)
{
	if (bClipPlays)
	{
		// The clip is the body now; the pose leaves quickly so the two do not fight.
		LandingSeconds = -1.f;
		FadeOutOverride = 0.1f;
		return;
	}
	// How hard she hit, 0 at the light threshold to 1 at twice the heavy one.
	const float Hardness = FMath::Clamp(
		(ImpactSpeed - Settings.LightImpactSpeed) / FMath::Max(Settings.HeavyImpactSpeed * 2.f - Settings.LightImpactSpeed, 1.f),
		0.f, 1.f);
	const bool bHeavy = Clip == ELandingClip::StandHeavy || Clip == ELandingClip::WalkHeavy || Clip == ELandingClip::RunHeavy
		|| Clip == ELandingClip::SprintHeavy || Clip == ELandingClip::Stumble || Clip == ELandingClip::Roll;
	if (Clip == ELandingClip::None)
	{
		LandingSeconds = -1.f;
		return;
	}
	LandingSeconds = 0.f;
	LandingDepth = FMath::Lerp(5.f, bHeavy ? 26.f : 12.f, Hardness);
	LandingKnee = FMath::Lerp(14.f, bHeavy ? 70.f : 38.f, Hardness);
	LandingLean = FMath::Lerp(4.f, bHeavy ? 16.f : 8.f, Hardness);
	LandingLength = bHeavy ? 0.6f : 0.4f;
	FadeOutOverride = -1.f;
}

void FTraversalPoseState::UpdatePhase(const FTraversalPoseInput& Input, float DeltaSeconds,
	const FHawkeyeTraversalPoseSettings& Settings)
{
	if (Input.bZipping)
	{
		const ETraversalPhase Next = Input.bHopping ? ETraversalPhase::Launch
			: (Input.bRedirecting ? ETraversalPhase::Redirect : ETraversalPhase::Pull);
		if (Next != Phase)
		{
			Phase = Next;
			PhaseSeconds = 0.f;
		}
		else
		{
			PhaseSeconds += DeltaSeconds;
		}
		if (bWasHopping && !Input.bHopping)
		{
			// The hop hands over to the line: the jerk. The tilt spring takes it and settles.
			Speed[TiltForward] += Settings.LaunchKickDegPerSecond;
		}
		Arrival = EArrivalKind::None;
		ArrivalElapsed = 0.f;
		LandingSeconds = -1.f;
		FadeOutOverride = -1.f;
		LastLineSpeed = Input.ZipLineSpeed;
	}
	else if (bWasZipping)
	{
		PhaseSeconds = 0.f;
		if (Input.bGrounded && !Input.bFalling)
		{
			Phase = ETraversalPhase::Arrive;
			Arrival = FTraversalPoseRules::ClassifyArrival(LastLineSpeed, Settings);
			ArrivalElapsed = 0.f;
		}
		else
		{
			Phase = ETraversalPhase::Release;
		}
	}
	else
	{
		PhaseSeconds += DeltaSeconds;
		switch (Phase)
		{
		case ETraversalPhase::Arrive:
			ArrivalElapsed += DeltaSeconds;
			if (ArrivalElapsed >= FTraversalPoseRules::ArrivalSeconds(Arrival, Settings) || Input.bFalling)
			{
				Phase = ETraversalPhase::None;
				Arrival = EArrivalKind::None;
			}
			break;
		case ETraversalPhase::Release:
			if (!Input.bFalling)
			{
				Phase = ETraversalPhase::None;
			}
			else if (PhaseSeconds >= Settings.ReleaseSeconds)
			{
				Phase = ETraversalPhase::Fall;
				PhaseSeconds = 0.f;
			}
			break;
		case ETraversalPhase::Fall:
			if (!Input.bFalling)
			{
				Phase = ETraversalPhase::None;
			}
			break;
		default:
			break;
		}
	}
	if (LandingSeconds >= 0.f)
	{
		LandingSeconds += DeltaSeconds;
		if (LandingSeconds >= LandingLength)
		{
			LandingSeconds = -1.f;
		}
	}
	bWasZipping = Input.bZipping;
	bWasHopping = Input.bZipping && Input.bHopping;
}

void FTraversalPoseState::AimTargets(const FTraversalPoseInput& Input, const FHawkeyeTraversalPoseSettings& Settings,
	const FVector& LocalAcceleration, const FVector& LocalRope, FTargets& Out) const
{
	using namespace HawkeyeTraversalPoseMath;
	const float TravelSpeed = Input.bZipping ? Input.ZipLineSpeed : static_cast<float>(Input.Velocity.Size());
	const float Speed01 = FMath::Clamp(TravelSpeed / FMath::Max(Settings.ReferenceSpeed, 1.f), 0.f, 1.f);
	Out.WeightTarget = 1.f;
	switch (Phase)
	{
	case ETraversalPhase::Launch:
	{
		// The crouch-load: peaks 60% through the hop, then lets go into the jerk. The reach starts at once.
		const float F = FMath::Clamp(Input.HopFraction, 0.f, 1.f);
		const float Load = Smooth(0.f, 0.6f, F) * (1.f - 0.6f * Smooth(0.6f, 1.f, F));
		Out.Goal[TiltForward] = Settings.LaunchLeanDegrees * Load;
		Out.Goal[Sink] = Settings.LaunchSinkCm * Load;
		Out.Goal[ThighL] = Out.Goal[ThighR] = -Settings.LaunchHipFlex * Load;
		Out.Goal[CalfL] = Out.Goal[CalfR] = Settings.LaunchKnee * Load;
		Out.Goal[LeftArm] = Smooth(0.1f, 0.7f, F);
		Out.Goal[RightArm] = 0.6f * Smooth(0.3f, 1.f, F);
		// The hop is 0.15 s, so the crouch is followed on a quick spring; the line's pull goes back to the slower ones.
		for (const EChannel Channel : { TiltForward, ThighL, ThighR, CalfL, CalfR, Sink })
		{
			Out.HalfLife[Channel] = Settings.LaunchHalfLife;
		}
		break;
	}
	case ETraversalPhase::Pull:
	case ETraversalPhase::Redirect:
	{
		const FVector2D Rope = FTraversalPoseRules::RopeTilt(LocalRope, Settings.RopeTiltWeight);
		const FVector2D Lean = FTraversalPoseRules::AccelerationTilt(LocalAcceleration, Settings.Gravity, Settings.AccelLeanStrength);
		const FVector2D Tilt = FTraversalPoseRules::ClampTilt(Rope + Lean, Settings.MaxTiltDegrees);
		Out.Goal[TiltForward] = static_cast<float>(Tilt.X);
		Out.Goal[TiltRight] = static_cast<float>(Tilt.Y);
		// The legs trail the way she travels, one leading. They never swing so far that the feet go past MaxLegTotal.
		const float Trail = FMath::Min(Settings.MaxLegTrail * Speed01, FMath::Max(0.f, Settings.MaxLegTotal - static_cast<float>(Tilt.Size())));
		Out.Goal[ThighR] = Trail;
		Out.Goal[ThighL] = Trail - Settings.LeadLegAdvance * Speed01;
		Out.Goal[CalfR] = Settings.KneeBendBase + Trail * Settings.KneeBendPerTrail;
		Out.Goal[CalfL] = Settings.KneeBendBase * 0.6f + FMath::Max(0.f, Out.Goal[ThighL]) * Settings.KneeBendPerTrail;
		// Hauled up sharply she sags below the hand; hauled forward the hips lag behind it.
		Out.Goal[Sink] = FMath::Clamp(static_cast<float>(LocalAcceleration.Z) / FMath::Max(Settings.Gravity, 1.f) * Settings.MaxSinkCm,
			-Settings.MaxSinkCm, Settings.MaxSinkCm);
		Out.Goal[Lag] = FMath::Clamp(static_cast<float>(LocalAcceleration.X) * Settings.LagPerAcceleration, -Settings.MaxLagCm,
			Settings.MaxLagCm);
		Out.Goal[LeftArm] = 1.f;
		Out.Goal[RightArm] = 1.f;
		break;
	}
	case ETraversalPhase::Release:
		// The tuck: knees up, the hands let go of the line and come out for balance.
		Out.Goal[TiltForward] = 8.f;
		Out.Goal[ThighL] = -Settings.TuckHipFlex;
		Out.Goal[ThighR] = -Settings.TuckHipFlex * 0.8f;
		Out.Goal[CalfL] = Settings.TuckKnee;
		Out.Goal[CalfR] = Settings.TuckKnee * 1.1f;
		Out.Goal[LeftArm] = 0.7f;
		Out.Goal[RightArm] = 0.7f;
		Out.Goal[ArmsOut] = 1.f;
		break;
	case ETraversalPhase::Fall:
	{
		// Reacting to how fast she is dropping: arms out and up, legs braced, the back arching.
		const float Down = FMath::Max(0.f, -static_cast<float>(Input.Velocity.Z));
		const float Fast = FMath::Clamp(Down / FMath::Max(Settings.FallFullSpeed, 1.f), 0.f, 1.f);
		const float Brace = 0.4f + 0.6f * Fast;
		// With the fall loop clip playing the legs and arms are its own; the pose is only a reaction on top.
		const float Share = Input.bFallClipPlays ? 0.3f : 1.f;
		Out.Goal[TiltForward] = -Settings.FallArch * Fast;
		Out.Goal[ThighL] = -Settings.FallHipFlex * Brace * Share;
		Out.Goal[ThighR] = -Settings.FallHipFlex * Brace * 0.6f * Share;
		Out.Goal[CalfL] = Settings.FallKnee * (1.f - 0.4f * Fast) * Share;
		Out.Goal[CalfR] = Settings.FallKnee * (1.f - 0.4f * Fast) * 1.3f * Share;
		Out.Goal[LeftArm] = (0.35f + 0.65f * Fast) * (Input.bFallClipPlays ? 0.f : 1.f);
		Out.Goal[RightArm] = (0.35f + 0.65f * Fast) * (Input.bFallClipPlays ? 0.f : 1.f);
		Out.Goal[ArmsOut] = 1.f;
		break;
	}
	default:
		Out.WeightTarget = 0.f;
		break;
	}
}

void FTraversalPoseState::ArrivalTargets(const FHawkeyeTraversalPoseSettings& Settings, FTargets& Out) const
{
	const float T = ArrivalElapsed;
	Out.WeightTarget = T < FTraversalPoseRules::ArrivalSeconds(Arrival, Settings) ? 1.f : 0.f;
	for (int32 Channel = 0; Channel < Count; ++Channel)
	{
		Out.HalfLife[Channel] = Settings.LandingHalfLife;
	}
	switch (Arrival)
	{
	case EArrivalKind::StepDown:
	{
		const float Env = FTraversalPoseRules::DipEnvelope(T, 0.08f);
		Out.Goal[Sink] = 7.f * Env;
		Out.Goal[ThighL] = Out.Goal[ThighR] = -12.f * Env;
		Out.Goal[CalfL] = Out.Goal[CalfR] = 22.f * Env;
		Out.Goal[TiltForward] = 5.f * Env;
		break;
	}
	case EArrivalKind::Brake:
	{
		// A braking two-step: the knees take the speed, the near foot plants, then the other, the chest comes back.
		const float Env = FTraversalPoseRules::DipEnvelope(T, 0.1f);
		const float StepL = FTraversalPoseRules::Pulse(T, 0.12f, 0.07f);
		const float StepR = FTraversalPoseRules::Pulse(T, 0.3f, 0.08f);
		Out.Goal[Sink] = 14.f * Env;
		Out.Goal[TiltForward] = -8.f * Env;
		Out.Goal[ThighL] = -(20.f * Env + 24.f * StepL);
		Out.Goal[ThighR] = -(20.f * Env + 22.f * StepR);
		Out.Goal[CalfL] = 40.f * Env + 12.f * StepL;
		Out.Goal[CalfR] = 40.f * Env + 12.f * StepR;
		Out.Goal[LeftArm] = Out.Goal[RightArm] = 0.5f * Env;
		Out.Goal[ArmsOut] = 0.7f;
		break;
	}
	case EArrivalKind::SlideOut:
	{
		// A slide out: sat back on the heels with the legs forward and the arms out.
		const float Env = FTraversalPoseRules::DipEnvelope(T, 0.14f);
		Out.Goal[Sink] = 30.f * Env;
		Out.Goal[TiltForward] = -20.f * Env;
		Out.Goal[ThighL] = -50.f * Env;
		Out.Goal[ThighR] = -35.f * Env;
		Out.Goal[CalfL] = 60.f * Env;
		Out.Goal[CalfR] = 85.f * Env;
		Out.Goal[LeftArm] = Out.Goal[RightArm] = 0.8f * Env;
		Out.Goal[ArmsOut] = 1.f;
		break;
	}
	default:
		Out.WeightTarget = 0.f;
		break;
	}
}

void FTraversalPoseState::LandingTargets(const FHawkeyeTraversalPoseSettings& Settings, FTargets& Out) const
{
	const float Env = FTraversalPoseRules::DipEnvelope(LandingSeconds, 0.07f);
	Out.WeightTarget = LandingSeconds < LandingLength ? 1.f : 0.f;
	for (int32 Channel = 0; Channel < Count; ++Channel)
	{
		Out.HalfLife[Channel] = Settings.LandingHalfLife;
	}
	Out.Goal[Sink] = LandingDepth * Env;
	Out.Goal[ThighL] = Out.Goal[ThighR] = -LandingKnee * 0.5f * Env;
	Out.Goal[CalfL] = Out.Goal[CalfR] = LandingKnee * Env;
	Out.Goal[TiltForward] = LandingLean * Env;
}

FTraversalPoseOutput FTraversalPoseState::Compose(const FTraversalPoseInput& Input,
	const FHawkeyeTraversalPoseSettings& Settings, const FVector& LocalRope) const
{
	using namespace HawkeyeTraversalPoseMath;
	FTraversalPoseOutput Out;
	Out.Phase = Phase;
	Out.PhaseSeconds = PhaseSeconds;
	Out.Weight = Weight;
	Out.Arrival = Arrival;
	if (Weight <= 0.001f)
	{
		return Out;
	}
	const float W = Weight;
	const float TipForward = Value[TiltForward];
	const float TipRight = Value[TiltRight];
	Out.Tilt = FVector2D(TipForward, TipRight);

	const FVector Spread = NormalisedSpineWeights(Settings.SpineWeights);
	const float Pelvic = FMath::Clamp(Settings.PelvisTiltShare, 0.f, 1.f);
	Out.Pelvis = FTraversalPoseRules::QuatFromTilt(TipForward * Pelvic * W, TipRight * Pelvic * W);
	const float SpineShare = (1.f - Pelvic) * W;
	Out.Spine[0] = FTraversalPoseRules::QuatFromTilt(TipForward * SpineShare * static_cast<float>(Spread.X),
		TipRight * SpineShare * static_cast<float>(Spread.X));
	Out.Spine[1] = FTraversalPoseRules::QuatFromTilt(TipForward * SpineShare * static_cast<float>(Spread.Y),
		TipRight * SpineShare * static_cast<float>(Spread.Y));
	Out.Spine[2] = FTraversalPoseRules::QuatFromTilt(TipForward * SpineShare * static_cast<float>(Spread.Z),
		TipRight * SpineShare * static_cast<float>(Spread.Z));
	Out.PelvisOffset = FVector(-Value[Lag], 0.f, -Value[Sink]) * W;

	// The hips swing about the body's own right axis: the pelvis's tip carries it round.
	const FVector HipAxis = Out.Pelvis.RotateVector(FVector::RightVector);
	Out.ThighL = FTraversalPoseRules::ScaledTurn(HipAxis, Value[ThighL], W);
	Out.ThighR = FTraversalPoseRules::ScaledTurn(HipAxis, Value[ThighR], W);
	Out.CalfL = FTraversalPoseRules::ScaledTurn(HipAxis, FMath::Max(0.f, Value[CalfL]), W);
	Out.CalfR = FTraversalPoseRules::ScaledTurn(HipAxis, FMath::Max(0.f, Value[CalfR]), W);

	// The head: the tip taken back so it stays level, and a look toward the anchor while she is on the rope.
	const float OnRope = FMath::Clamp(Value[LeftArm], 0.f, 1.f) * (1.f - FMath::Clamp(Value[ArmsOut], 0.f, 1.f));
	const float LookPitch = FMath::Clamp(ElevationDegrees(LocalRope) * Settings.HeadLookWeight, 0.f, Settings.MaxHeadPitch);
	const float LookYaw = FTraversalPoseRules::ClampAngle(
		FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(LocalRope.Y), static_cast<float>(LocalRope.X))) * Settings.HeadLookWeight,
		Settings.MaxHeadYaw);
	const FQuat Level = FTraversalPoseRules::QuatFromTilt(-TipForward * Settings.HeadStabilise * W, -TipRight * Settings.HeadStabilise * W);
	// Pitching up is a turn about -Y, turning right one about +Z.
	const FQuat Look = FTraversalPoseRules::ScaledTurn(FVector::UpVector, LookYaw, OnRope * W)
		* FTraversalPoseRules::ScaledTurn(-FVector::RightVector, LookPitch, OnRope * W);
	Out.Neck = Look * Level;

	// The hands, from the capsule centre. The shoulders ride the whole tip (pelvis and spine together).
	const FQuat Body = FTraversalPoseRules::QuatFromTilt(TipForward * W, TipRight * W);
	const FVector PelvisAt = Settings.PelvisFromCentre + Out.PelvisOffset;
	const FVector LeftShoulder = PelvisAt + Body.RotateVector(Settings.ShoulderFromPelvis);
	const FVector RightShoulder = PelvisAt + Body.RotateVector(FVector(Settings.ShoulderFromPelvis.X, -Settings.ShoulderFromPelvis.Y,
		Settings.ShoulderFromPelvis.Z));
	const float Out01 = FMath::Clamp(Value[ArmsOut], 0.f, 1.f);

	const FVector RopeHand = LeftShoulder + LocalRope * Settings.ArmReach;
	const FVector RopeElbow = LeftShoulder + Body.RotateVector(FVector(-12.f, -22.f, -30.f));
	const FVector LeftOut = PelvisAt + Body.RotateVector(FVector(Settings.HandsOut.X, -Settings.HandsOut.Y, Settings.HandsOut.Z));
	const FVector LeftOutElbow = LeftShoulder + Body.RotateVector(FVector(-5.f, -30.f, 8.f));
	Out.LeftHand = FMath::Lerp(RopeHand, LeftOut, Out01);
	Out.LeftElbow = FMath::Lerp(RopeElbow, LeftOutElbow, Out01);
	Out.LeftHandAlpha = FMath::Clamp(Value[LeftArm] * W, 0.f, 1.f);

	const FVector FreeBack = PelvisAt + Body.RotateVector(Settings.FreeHandBack);
	const FVector FreeBackElbow = RightShoulder + Body.RotateVector(FVector(-22.f, 24.f, -10.f));
	const FVector RightOut = PelvisAt + Body.RotateVector(Settings.HandsOut);
	const FVector RightOutElbow = RightShoulder + Body.RotateVector(FVector(-5.f, 30.f, 8.f));
	Out.RightHand = FMath::Lerp(FreeBack, RightOut, Out01);
	Out.RightElbow = FMath::Lerp(FreeBackElbow, RightOutElbow, Out01);
	Out.RightHandAlpha = FMath::Clamp(Value[RightArm] * W, 0.f, 1.f);
	return Out;
}

FTraversalPoseOutput FTraversalPoseState::Step(const FTraversalPoseInput& Input, const FHawkeyeTraversalPoseSettings& Settings)
{
	const float Dt = FMath::Clamp(Input.DeltaSeconds, 0.f, 0.1f);
	if (Input.bTeleported)
	{
		Reset();
	}

	FVector Acceleration = FVector::ZeroVector;
	if (bHaveLast && Dt > UE_KINDA_SMALL_NUMBER)
	{
		Acceleration = (Input.Velocity - LastVelocity) / Dt;
		const double Size = Acceleration.Size();
		if (Size > Settings.MaxAcceleration && Size > UE_KINDA_SMALL_NUMBER)
		{
			Acceleration *= Settings.MaxAcceleration / Size;
		}
	}
	LastVelocity = Input.Velocity;
	bHaveLast = true;

	UpdatePhase(Input, Dt, Settings);

	const FQuat Inverse = Input.ActorRotation.Inverse();
	const FVector LocalAcceleration = Inverse.RotateVector(Acceleration);
	// Without a rope direction the body takes the rope a little up and ahead of her, which is what a launch looks like.
	const FVector LocalRope = Input.RopeDirection.IsNearlyZero() ? FVector(0.6, 0., 0.8)
		: Inverse.RotateVector(Input.RopeDirection.GetSafeNormal());

	FTargets Targets;
	for (int32 Channel = 0; Channel < Count; ++Channel)
	{
		Targets.HalfLife[Channel] = Channel <= TiltRight ? Settings.TiltHalfLife
			: (Channel >= LeftArm ? Settings.ArmHalfLife : Settings.LegHalfLife);
	}
	if (Phase == ETraversalPhase::Arrive)
	{
		ArrivalTargets(Settings, Targets);
	}
	else if (Phase != ETraversalPhase::None)
	{
		AimTargets(Input, Settings, LocalAcceleration, LocalRope, Targets);
	}
	else if (LandingSeconds >= 0.f)
	{
		LandingTargets(Settings, Targets);
	}

	for (int32 Channel = 0; Channel < Count; ++Channel)
	{
		FTraversalPoseRules::SpringStep(Value[Channel], Speed[Channel], Targets.Goal[Channel], Targets.HalfLife[Channel], Dt);
	}

	const float FadeOut = FadeOutOverride >= 0.f ? FadeOutOverride : Settings.WeightOutSeconds;
	const float Delta = Targets.WeightTarget > Weight
		? (Settings.WeightInSeconds > 0.f ? Dt / Settings.WeightInSeconds : 1.f)
		: (FadeOut > 0.f ? Dt / FadeOut : 1.f);
	Weight = FMath::FInterpConstantTo(Weight, Targets.WeightTarget, 1.f, Delta);
	if (Weight <= 0.001f && Targets.WeightTarget <= 0.f)
	{
		// Out: the springs rest, the clip's override is spent.
		Weight = 0.f;
		FadeOutOverride = -1.f;
		for (int32 Channel = 0; Channel < Count; ++Channel)
		{
			Value[Channel] = 0.f;
			Speed[Channel] = 0.f;
		}
	}
	return Compose(Input, Settings, LocalRope);
}
