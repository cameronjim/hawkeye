#include "Combat/HitContactRules.h"

#include "Combat/MeleeRules.h"

FHawkeyeContactPlan UHawkeyeHitContactRules::PlanContact(const FVector& Origin, const FVector& Forward,
	const FVector& ContactPoint, float ContactDistance, float MaxSlide, float MaxTurnDegrees)
{
	FHawkeyeContactPlan Plan;
	FVector Facing = Forward.GetSafeNormal2D();
	if (Facing.IsNearlyZero())
	{
		Facing = FVector::ForwardVector;
	}
	const FVector Flat = FVector(ContactPoint.X - Origin.X, ContactPoint.Y - Origin.Y, 0.f);
	const float Distance = Flat.Size();
	const FVector Toward = Distance > KINDA_SMALL_NUMBER ? Flat / Distance : Facing;

	Plan.Facing = Toward;
	Plan.TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Facing, Toward), -1.f, 1.f)));
	// Already inside the contact distance: she turns onto him but never backs off.
	Plan.Slide = FMath::Max(Distance - FMath::Max(ContactDistance, 0.f), 0.f);
	Plan.StandPoint = FVector(Origin.X, Origin.Y, Origin.Z) + Toward * Plan.Slide;

	if (Plan.Slide > FMath::Max(MaxSlide, 0.f) + KINDA_SMALL_NUMBER)
	{
		Plan.WhiffReason = FName(TEXT("too far"));
		return Plan;
	}
	if (Plan.TurnDegrees > FMath::Max(MaxTurnDegrees, 0.f) + KINDA_SMALL_NUMBER)
	{
		Plan.WhiffReason = FName(TEXT("too far round"));
		return Plan;
	}
	Plan.bReachable = true;
	return Plan;
}

EHawkeyeContactRegion UHawkeyeHitContactRules::SelectContactRegion(EHawkeyeContactRegion Preferred, bool bTargetDown)
{
	return bTargetDown ? EHawkeyeContactRegion::Torso : Preferred;
}

TArray<FName> UHawkeyeHitContactRules::RegionBones(EHawkeyeContactRegion Region)
{
	switch (Region)
	{
	case EHawkeyeContactRegion::Head:
		return { FName(TEXT("head")), FName(TEXT("neck_01")) };
	case EHawkeyeContactRegion::Legs:
		return { FName(TEXT("thigh_r")), FName(TEXT("thigh_l")), FName(TEXT("calf_r")), FName(TEXT("pelvis")) };
	default:
		return { FName(TEXT("spine_03")), FName(TEXT("spine_02")), FName(TEXT("spine_01")), FName(TEXT("pelvis")) };
	}
}

FName UHawkeyeHitContactRules::SelectRegionBone(EHawkeyeContactRegion Region, TFunctionRef<bool(FName)> HasBone)
{
	for (const FName Bone : RegionBones(Region))
	{
		if (HasBone(Bone))
		{
			return Bone;
		}
	}
	return NAME_None;
}

TArray<FName> UHawkeyeHitContactRules::LimbBones(EHawkeyeStrikeLimb Limbs)
{
	const FName HandR(TEXT("hand_r"));
	const FName HandL(TEXT("hand_l"));
	const FName FootR(TEXT("foot_r"));
	const FName FootL(TEXT("foot_l"));
	switch (Limbs)
	{
	case EHawkeyeStrikeLimb::Hands:
		return { HandR, HandL };
	case EHawkeyeStrikeLimb::Feet:
		return { FootR, FootL };
	case EHawkeyeStrikeLimb::Weapon:
		return { HandR };
	default:
		return { HandR, HandL, FootR, FootL };
	}
}

int32 UHawkeyeHitContactRules::PickStrikeLimb(const TArray<FVector>& LimbPoints, const FVector& Target)
{
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < LimbPoints.Num(); ++Index)
	{
		const float Distance = FVector::DistSquared(LimbPoints[Index], Target);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

FVector UHawkeyeHitContactRules::ComputeStrikeDirection(const FVector& LimbVelocity, const FVector& StrikerToTarget,
	float MinSpeed)
{
	FVector Line = StrikerToTarget.GetSafeNormal2D();
	if (Line.IsNearlyZero())
	{
		Line = FVector::ForwardVector;
	}
	const FVector Motion = FVector(LimbVelocity.X, LimbVelocity.Y, 0.f);
	// A limb moving away from him (the follow-through past his shoulder) does not say where the blow came from.
	if (Motion.Size() >= MinSpeed && FVector::DotProduct(Motion.GetSafeNormal(), Line) > -0.2f)
	{
		return Motion.GetSafeNormal();
	}
	return Line;
}

EHawkeyeHitDirection UHawkeyeHitContactRules::ClassifyReaction(const FVector& Forward, const FVector& StrikeDirection)
{
	// The blow travels StrikeDirection, so it came from the other way.
	return UHawkeyeMeleeRules::ClassifyHitDirection(Forward, -StrikeDirection);
}

float UHawkeyeHitContactRules::ComputeHitStopSeconds(float Weight, const FHawkeyeHitStopTuning& Tuning)
{
	return FMath::Max(FMath::Lerp(Tuning.MinSeconds, Tuning.MaxSeconds, FMath::Clamp(Weight, 0.f, 1.f)), 0.f);
}

float UHawkeyeHitContactRules::ComputeHitStopSecondsAfter(float Weight, const FHawkeyeHitStopTuning& Tuning, float SecondsSinceLastStop)
{
	const float Own = ComputeHitStopSeconds(Weight, Tuning);
	if (SecondsSinceLastStop >= 0.f && SecondsSinceLastStop < Tuning.MinGapSeconds)
	{
		return Own * FMath::Clamp(Tuning.ChainScale, 0.f, 1.f);
	}
	return Own;
}

float UHawkeyeHitContactRules::ComputeVibrationAmplitude(float Weight, const FHawkeyeWeldTuning& Tuning)
{
	const float Span = FMath::Max(1.f - Tuning.VibrationLightWeight, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp((Weight - Tuning.VibrationLightWeight) / Span, 0.f, 1.f);
	return FMath::Lerp(Tuning.VibrationLightCm, Tuning.VibrationHeavyCm, Alpha);
}

int32 UHawkeyeHitContactRules::ComputeVibrationFrames(float Weight, const FHawkeyeWeldTuning& Tuning)
{
	const float Span = FMath::Max(1.f - Tuning.VibrationLightWeight, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp((Weight - Tuning.VibrationLightWeight) / Span, 0.f, 1.f);
	const int32 Low = FMath::Max(Tuning.VibrationMinFrames, 1);
	const int32 High = FMath::Max(Tuning.VibrationMaxFrames, Low);
	return FMath::Clamp(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Low), static_cast<float>(High), Alpha)), Low, High);
}

float UHawkeyeHitContactRules::ComputeVibrationOffset(float ElapsedSeconds, float DurationSeconds, float Weight, bool bFlip,
	const FHawkeyeWeldTuning& Tuning)
{
	if (DurationSeconds <= KINDA_SMALL_NUMBER || ElapsedSeconds < 0.f || ElapsedSeconds >= DurationSeconds)
	{
		return 0.f;
	}
	const int32 Frames = ComputeVibrationFrames(Weight, Tuning);
	const float StepSeconds = DurationSeconds / static_cast<float>(Frames);
	const int32 Step = FMath::Clamp(FMath::FloorToInt(ElapsedSeconds / StepSeconds), 0, Frames - 1);
	// Each step the other side of the last and smaller: Frames steps from the full amplitude down to 1 / Frames of it.
	const float Decay = 1.f - static_cast<float>(Step) / static_cast<float>(Frames);
	const float Side = ((Step % 2 == 0) != bFlip) ? 1.f : -1.f;
	return ComputeVibrationAmplitude(Weight, Tuning) * Decay * Side;
}

float UHawkeyeHitContactRules::ComputeVictimTurn(float CurrentYawDegrees, float YawToAttackerDegrees, float MaxDegrees)
{
	const float Wanted = FRotator::NormalizeAxis(YawToAttackerDegrees - CurrentYawDegrees);
	const float Cap = FMath::Max(MaxDegrees, 0.f);
	return FMath::Clamp(Wanted, -Cap, Cap);
}

float UHawkeyeHitContactRules::ComputeVictimNudge(float CurrentFlatCm, float DesiredFlatCm, float MaxCm)
{
	if (DesiredFlatCm <= 0.f)
	{
		return 0.f;
	}
	const float Cap = FMath::Max(MaxCm, 0.f);
	// Desired further than he is: he is too close and is moved away; nearer: moved toward the attacker.
	return FMath::Clamp(DesiredFlatCm - CurrentFlatCm, -Cap, Cap);
}

void UHawkeyeHitContactRules::ComputeKnockbackProfile(float DistanceCm, float SecondsLong, float Ease, int32 Samples,
	float& OutPeakSpeed, TArray<float>& OutStrengths)
{
	OutPeakSpeed = 0.f;
	OutStrengths.Reset();
	const int32 Count = FMath::Max(Samples, 2);
	const float Power = FMath::Max(Ease, 1.f);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float T = static_cast<float>(Index) / static_cast<float>(Count - 1);
		OutStrengths.Add(FMath::Pow(1.f - T, Power));
	}
	// Trapezoids on the unit time axis: the area under the straight lines between the samples.
	float Area = 0.f;
	for (int32 Index = 1; Index < Count; ++Index)
	{
		Area += 0.5f * (OutStrengths[Index - 1] + OutStrengths[Index]) / static_cast<float>(Count - 1);
	}
	if (DistanceCm > 0.f && SecondsLong > 0.f && Area > KINDA_SMALL_NUMBER)
	{
		OutPeakSpeed = DistanceCm / (SecondsLong * Area);
	}
}

FHawkeyeReactionStart UHawkeyeHitContactRules::ComputeReactionStart(float PeakSeconds, float PlayLength,
	const FHawkeyeWeldTuning& Tuning)
{
	FHawkeyeReactionStart Start;
	if (PeakSeconds >= 0.f && PeakSeconds < PlayLength)
	{
		Start.StartAtSeconds = PeakSeconds;
		Start.BlendInSeconds = 0.f;
		return Start;
	}
	Start.BlendInSeconds = FMath::Max(Tuning.UnauthoredBlendInSeconds, 0.f);
	return Start;
}

bool UHawkeyeHitContactRules::ShouldAlignVictim(bool bAlive, bool bWindingUp, bool bDown, bool bPlayerControlled)
{
	return bAlive && !bWindingUp && !bDown && !bPlayerControlled;
}

float UHawkeyeHitContactRules::ClampPhysicalBlend(float Blend, const FHawkeyeWeldTuning& Tuning)
{
	return FMath::Clamp(Blend, 0.f, Tuning.PhysicalBlendCap);
}

EHawkeyeContactRegion UHawkeyeHitContactRules::ClassifyBoneRegion(FName Bone)
{
	const FString Name = Bone.ToString();
	if (Name.StartsWith(TEXT("head")) || Name.StartsWith(TEXT("neck")))
	{
		return EHawkeyeContactRegion::Head;
	}
	if (Name.StartsWith(TEXT("pelvis")) || Name.StartsWith(TEXT("thigh")) || Name.StartsWith(TEXT("calf"))
		|| Name.StartsWith(TEXT("foot")) || Name.StartsWith(TEXT("ball")))
	{
		return EHawkeyeContactRegion::Legs;
	}
	return EHawkeyeContactRegion::Torso;
}

EHawkeyeContactRegion UHawkeyeHitContactRules::SelectRegionForHeight(float ContactHeightCm, float HeadCm, float TorsoCm,
	float LegsCm, EHawkeyeContactRegion Preferred)
{
	if (ContactHeightCm < 0.f)
	{
		return Preferred;
	}
	const float ToHead = FMath::Abs(ContactHeightCm - HeadCm);
	const float ToTorso = FMath::Abs(ContactHeightCm - TorsoCm);
	const float ToLegs = FMath::Abs(ContactHeightCm - LegsCm);
	if (ToHead <= ToTorso && ToHead <= ToLegs)
	{
		return EHawkeyeContactRegion::Head;
	}
	return ToTorso <= ToLegs ? EHawkeyeContactRegion::Torso : EHawkeyeContactRegion::Legs;
}

float UHawkeyeHitContactRules::PickStrikeHeight(float ClipHeightCm, float LimbHeightCm)
{
	if (ClipHeightCm >= 0.f)
	{
		return ClipHeightCm;
	}
	return LimbHeightCm >= 0.f ? LimbHeightCm : -1.f;
}

float UHawkeyeHitContactRules::ComputeNetKnockbackCm(float TunedCm, const FVector& Nudge, const FVector& Direction)
{
	const FVector Along = Direction.GetSafeNormal2D();
	const float Moved = FVector(Nudge.X, Nudge.Y, 0.f) | Along;
	return FMath::Max(TunedCm - Moved, 0.f);
}

float UHawkeyeHitContactRules::ResolveKnockbackCm(float ClipCm, bool bHeavy, const FHawkeyeWeldTuning& Tuning, float Scale)
{
	const float Tuned = bHeavy ? Tuning.KnockbackHeavyCm : Tuning.KnockbackLightCm;
	const float Base = Tuning.bClipKnockbackWins && ClipCm >= 0.f ? ClipCm : Tuned;
	return FMath::Max(Base * FMath::Max(Scale, 0.f), 0.f);
}

float UHawkeyeHitContactRules::ResolveKnockbackSeconds(bool bHeavy, const FHawkeyeWeldTuning& Tuning)
{
	return FMath::Max(bHeavy ? Tuning.KnockbackSeconds : Tuning.KnockbackLightSeconds, 0.05f);
}

FVector UHawkeyeHitContactRules::ComputeKnockbackDirection(const FVector* AttackerLocation, const FVector& VictimLocation, const FVector& Fallback)
{
	if (AttackerLocation)
	{
		const FVector Away = (VictimLocation - *AttackerLocation).GetSafeNormal2D();
		if (!Away.IsNearlyZero())
		{
			return Away;
		}
	}
	const FVector Back = Fallback.GetSafeNormal2D();
	return Back.IsNearlyZero() ? FVector(-1.f, 0.f, 0.f) : Back;
}

float UHawkeyeHitContactRules::ClampKnockbackToClearance(float DesiredCm, float ClearanceCm, float SkinCm)
{
	const float Desired = FMath::Max(DesiredCm, 0.f);
	if (ClearanceCm < 0.f)
	{
		return Desired;
	}
	return FMath::Clamp(ClearanceCm - FMath::Max(SkinCm, 0.f), 0.f, Desired);
}

bool UHawkeyeHitContactRules::ShouldHitStop(bool bAlive, bool bWindingUp)
{
	return bAlive && !bWindingUp;
}

FVector UHawkeyeHitContactRules::ComputeReactionImpulse(const FVector& Direction, float Weight, bool bGuarding,
	const FHawkeyeHitReactionTuning& Tuning)
{
	const FVector Along = Direction.GetSafeNormal().IsNearlyZero() ? FVector::ForwardVector : Direction.GetSafeNormal();
	const float Share = FMath::Lerp(Tuning.MinShare, 1.f, FMath::Clamp(Weight, 0.f, 1.f));
	return Along * Tuning.ImpulseSpeed * Share * (bGuarding ? Tuning.GuardScale : 1.f);
}

float UHawkeyeHitContactRules::ComputeReactionBlend(float Elapsed, float Weight, bool bGuarding,
	const FHawkeyeHitReactionTuning& Tuning)
{
	if (Elapsed < 0.f || Elapsed >= Tuning.RecoverSeconds)
	{
		return 0.f;
	}
	const float Peak = Tuning.PeakBlend * FMath::Lerp(Tuning.MinShare, 1.f, FMath::Clamp(Weight, 0.f, 1.f))
		* (bGuarding ? Tuning.GuardScale : 1.f);
	if (Elapsed < Tuning.RiseSeconds)
	{
		return Peak * (Tuning.RiseSeconds > 0.f ? Elapsed / Tuning.RiseSeconds : 1.f);
	}
	const float Fall = FMath::Max(Tuning.RecoverSeconds - Tuning.RiseSeconds, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp((Elapsed - Tuning.RiseSeconds) / Fall, 0.f, 1.f);
	// Eased out: most of the snap back to the clip happens early, the last of it softly.
	return Peak * (1.f - FMath::SmoothStep(0.f, 1.f, Alpha));
}

void UHawkeyeHitContactRules::ComputeKnockoutImpulse(const FVector& Direction, float Weight, EHawkeyeImpactSource Source,
	const FHawkeyeKnockoutTuning& Tuning, FVector& OutBone, FVector& OutBody)
{
	const bool bArrow = Source == EHawkeyeImpactSource::Arrow;
	FVector Along = Direction.GetSafeNormal();
	if (Along.IsNearlyZero())
	{
		Along = FVector::ForwardVector;
	}
	// A strike's body throw is flat (the lift is its own); an arrow's keeps its pitch but never drives him into the floor.
	FVector Flat = Along.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		Flat = FVector::ForwardVector;
	}
	const float Share = FMath::Lerp(Tuning.MinShare, 1.f, FMath::Clamp(Weight, 0.f, 1.f)) * (bArrow ? Tuning.ArrowScale : 1.f);
	FVector BoneAlong = bArrow ? FVector(Along.X, Along.Y, FMath::Max(Along.Z, 0.f)).GetSafeNormal() : Along;
	if (BoneAlong.IsNearlyZero())
	{
		BoneAlong = Flat;
	}
	const bool bFinisher = Source == EHawkeyeImpactSource::Finisher;
	const float BoneScale = bFinisher ? Tuning.FinisherBoneScale : 1.f;
	const float LiftScale = bArrow ? 0.f : (bFinisher ? Tuning.FinisherLiftScale : 1.f);
	OutBone = (BoneAlong * Tuning.BoneSpeed * Share * BoneScale).GetClampedToMaxSize(Tuning.MaxBoneSpeed);
	OutBody = (Flat * Tuning.BodySpeed * Share + FVector(0.f, 0.f, Tuning.Lift * Share * LiftScale))
		.GetClampedToMaxSize(Tuning.MaxBodySpeed);
}

FVector2D UHawkeyeHitContactRules::ComputeKnockoutDamping(float SecondsGrounded, const FHawkeyeKnockoutTuning& Tuning)
{
	const float Alpha = SecondsGrounded < 0.f ? 0.f : FMath::SmoothStep(0.f, 1.f,
		FMath::Clamp(SecondsGrounded / FMath::Max(Tuning.GroundedBlendSeconds, 0.01f), 0.f, 1.f));
	return FVector2D(FMath::Lerp(Tuning.LinearDamping, Tuning.GroundedLinearDamping, Alpha),
		FMath::Lerp(Tuning.AngularDamping, Tuning.GroundedAngularDamping, Alpha));
}

float UHawkeyeHitContactRules::ComputeCameraKick(float Weight, float LightDistance, float HeavyDistance)
{
	return FMath::Lerp(LightDistance, HeavyDistance, FMath::Clamp(Weight, 0.f, 1.f));
}

bool UHawkeyeHitContactRules::IsLyingOnBack(const FVector& BodyFront)
{
	return BodyFront.Z > 0.f;
}

void UHawkeyeHitContactRules::ComputeGetUpAlignment(const FVector& WorldPelvis, const FVector& WorldBodyUp, const FVector& ClipPelvis,
	const FVector& ClipBodyUp, float FallbackYaw, FVector& OutLocation, float& OutYaw)
{
	const FVector WorldFlat = FVector(WorldBodyUp.X, WorldBodyUp.Y, 0.f);
	const FVector ClipFlat = FVector(ClipBodyUp.X, ClipBodyUp.Y, 0.f);
	OutYaw = FallbackYaw;
	if (WorldFlat.SizeSquared() > 1e-4f && ClipFlat.SizeSquared() > 1e-4f)
	{
		OutYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(WorldFlat.Y, WorldFlat.X) - FMath::Atan2(ClipFlat.Y, ClipFlat.X)));
	}
	const FVector Offset = FRotator(0.f, OutYaw, 0.f).RotateVector(FVector(ClipPelvis.X, ClipPelvis.Y, 0.f));
	OutLocation = FVector(WorldPelvis.X - Offset.X, WorldPelvis.Y - Offset.Y, WorldPelvis.Z);
}

EHawkeyeGetUpHit UHawkeyeHitContactRules::ResolveHitDuringGetUp(bool bGettingUp, bool bKnocksDown)
{
	if (!bGettingUp)
	{
		return EHawkeyeGetUpHit::React;
	}
	return bKnocksDown ? EHawkeyeGetUpHit::KnockDownAgain : EHawkeyeGetUpHit::Absorb;
}
