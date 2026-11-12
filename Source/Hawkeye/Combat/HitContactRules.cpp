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
	if (SecondsSinceLastStop >= 0.f && SecondsSinceLastStop < Tuning.MinGapSeconds)
	{
		return 0.f;
	}
	return ComputeHitStopSeconds(Weight, Tuning);
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
	OutBone = (BoneAlong * Tuning.BoneSpeed * Share).GetClampedToMaxSize(Tuning.MaxBoneSpeed);
	OutBody = (Flat * Tuning.BodySpeed * Share + FVector(0.f, 0.f, bArrow ? 0.f : Tuning.Lift * Share))
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
