#include "Combat/MeleeRules.h"

#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

EHawkeyeHitDirection UHawkeyeMeleeRules::ClassifyHitDirection(const FVector& Forward, const FVector& ToAttacker)
{
	const FVector Facing = Forward.GetSafeNormal2D();
	const FVector To = ToAttacker.GetSafeNormal2D();
	if (Facing.IsNearlyZero() || To.IsNearlyZero())
	{
		return EHawkeyeHitDirection::Front;
	}
	const float Ahead = FVector::DotProduct(Facing, To);
	const float Cos45 = UE_INV_SQRT_2;
	if (Ahead >= Cos45 - KINDA_SMALL_NUMBER)
	{
		return EHawkeyeHitDirection::Front;
	}
	if (Ahead <= -Cos45 + KINDA_SMALL_NUMBER)
	{
		return EHawkeyeHitDirection::Back;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
	return FVector::DotProduct(Right, To) >= 0.f ? EHawkeyeHitDirection::Right : EHawkeyeHitDirection::Left;
}

FVector UHawkeyeMeleeRules::ComputeLeanDirection(const FVector& Forward, EHawkeyeHitDirection Side)
{
	const FVector Facing = Forward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Forward.GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Facing);
	switch (Side)
	{
	case EHawkeyeHitDirection::Back:
		return Facing;
	case EHawkeyeHitDirection::Left:
		return Right;
	case EHawkeyeHitDirection::Right:
		return -Right;
	default:
		return -Facing;
	}
}

FRotator UHawkeyeMeleeRules::ComputeHitLeanRotation(const FVector& LeanDirection, float Alpha, float Degrees)
{
	const FVector Lean = LeanDirection.GetSafeNormal2D();
	if (Lean.IsNearlyZero() || Alpha <= 0.f || Degrees == 0.f)
	{
		return FRotator::ZeroRotator;
	}
	const FVector Axis = FVector::CrossProduct(FVector::UpVector, Lean).GetSafeNormal();
	return FQuat(Axis, FMath::DegreesToRadians(Degrees * FMath::Clamp(Alpha, 0.f, 1.f))).Rotator();
}

int32 UHawkeyeMeleeRules::SelectSoftLockIndex(const FVector& Origin, const FVector& Forward, const TArray<FVector>& Candidates,
	float Range, float AngleDegrees, float MaxHeight)
{
	const FVector Facing = Forward.GetSafeNormal2D();
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(AngleDegrees, 0.f, 180.f)));
	int32 Best = INDEX_NONE;
	float BestDistance = Range;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FVector To = Candidates[Index] - Origin;
		const float Distance = To.Size2D();
		if (Distance > BestDistance || FMath::Abs(To.Z) > MaxHeight)
		{
			continue;
		}
		if (Distance > KINDA_SMALL_NUMBER && FVector::DotProduct(To.GetSafeNormal2D(), Facing) < MinDot - KINDA_SMALL_NUMBER)
		{
			continue;
		}
		Best = Index;
		BestDistance = Distance;
	}
	return Best;
}

int32 UHawkeyeMeleeRules::SelectMeleeAssistIndex(const FVector& Origin, const FVector& AimDirection,
	const TArray<FVector>& Candidates, float Range, float HalfAngleDegrees, float MaxHeight)
{
	const FVector Aim = AimDirection.GetSafeNormal2D();
	const float HalfAngle = FMath::Clamp(HalfAngleDegrees, 1.f, 180.f);
	const float SafeRange = FMath::Max(Range, 1.f);
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FVector To = Candidates[Index] - Origin;
		const float Distance = To.Size2D();
		if (Distance > Range || FMath::Abs(To.Z) > MaxHeight)
		{
			continue;
		}
		float Angle = 0.f;
		if (Distance > KINDA_SMALL_NUMBER && !Aim.IsNearlyZero())
		{
			const float Dot = FMath::Clamp(FVector::DotProduct(To.GetSafeNormal2D(), Aim), -1.f, 1.f);
			Angle = FMath::RadiansToDegrees(FMath::Acos(Dot));
		}
		if (Angle > HalfAngle + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const float Score = Distance / SafeRange + Angle / HalfAngle;
		if (Score < BestScore)
		{
			Best = Index;
			BestScore = Score;
		}
	}
	return Best;
}

float UHawkeyeMeleeRules::ComputeGapCloseDistance(float DistanceToTarget, float StandOff, float MinLunge, float MaxClose)
{
	const float Wanted = FMath::Max(DistanceToTarget - FMath::Max(StandOff, 0.f), 0.f);
	return FMath::Max(FMath::Min(Wanted, FMath::Max(MaxClose, 0.f)), FMath::Max(MinLunge, 0.f));
}

bool UHawkeyeMeleeRules::IsAssistHitForgiven(const FVector& Origin, const FVector& SwingDirection, const FVector& Target,
	float TargetRadius, float Reach, float ReachScale, float ForgiveDegrees)
{
	const FVector To = Target - Origin;
	const float Edge = FMath::Max(To.Size2D() - FMath::Max(TargetRadius, 0.f), 0.f);
	if (Edge > Reach * ReachScale)
	{
		return false;
	}
	const FVector Swing = SwingDirection.GetSafeNormal2D();
	if (To.Size2D() <= KINDA_SMALL_NUMBER || Swing.IsNearlyZero())
	{
		return true;
	}
	return FVector::DotProduct(To.GetSafeNormal2D(), Swing)
		>= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(ForgiveDegrees, 0.f, 180.f))) - KINDA_SMALL_NUMBER;
}

bool UHawkeyeMeleeRules::IsInFrontWithin(const FVector& Origin, const FVector& Forward, const FVector& Target, float Range,
	float HalfAngleDegrees)
{
	const FVector To = Target - Origin;
	if (To.Size2D() > Range)
	{
		return false;
	}
	const FVector Facing = Forward.GetSafeNormal2D();
	if (To.Size2D() <= KINDA_SMALL_NUMBER || Facing.IsNearlyZero())
	{
		return true;
	}
	return FVector::DotProduct(To.GetSafeNormal2D(), Facing)
		>= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDegrees, 0.f, 180.f))) - KINDA_SMALL_NUMBER;
}

EHawkeyeParryKind UHawkeyeMeleeRules::ClassifyParry(const AThugCharacter* Thug)
{
	const UHealthComponent* Health = Thug ? Thug->GetHealthComponent() : nullptr;
	if (!Health || !Health->IsAlive() || Thug->IsLimp() || Thug->IsIncapacitated() || Thug->IsArcher())
	{
		return EHawkeyeParryKind::None;
	}
	if (Thug->IsGunner())
	{
		const AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController());
		return Brain && Brain->IsTelegraphing() ? EHawkeyeParryKind::Burst : EHawkeyeParryKind::None;
	}
	const UMeleeComponent* Melee = Thug->GetMeleeComponent();
	if (!Melee || !Melee->IsWindingUp() || Melee->GetCurrentAttack().WindupSeconds < MinTelegraphSeconds)
	{
		return EHawkeyeParryKind::None;
	}
	return Thug->IsHeavy() && Melee->GetCurrentAttack().KnockbackDistance > 0.f ? EHawkeyeParryKind::Bash : EHawkeyeParryKind::Swing;
}

float UHawkeyeMeleeRules::GetTelegraphElapsed(const AThugCharacter* Thug)
{
	if (!Thug)
	{
		return -1.f;
	}
	if (Thug->IsGunner())
	{
		const AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController());
		return Brain ? Brain->GetTelegraphElapsed() : -1.f;
	}
	const UMeleeComponent* Melee = Thug->GetMeleeComponent();
	if (!Melee || !Melee->IsWindingUp())
	{
		return -1.f;
	}
	return FMath::Max(0.f, Melee->GetCurrentAttack().WindupSeconds - Melee->GetPhaseRemaining());
}

bool UHawkeyeMeleeRules::IsInParryWindow(float TelegraphElapsed, float WindowDelta)
{
	return TelegraphElapsed >= 0.f && TelegraphElapsed + KINDA_SMALL_NUMBER >= FMath::Max(0.f, -WindowDelta);
}

bool UHawkeyeMeleeRules::IsFinisherTarget(const AThugCharacter* Thug, const FVector& From, float Range)
{
	const UHealthComponent* Health = Thug ? Thug->GetHealthComponent() : nullptr;
	if (!Health || !Health->IsAlive() || Thug->IsLimp() || !Thug->IsFinisherOpen())
	{
		return false;
	}
	// A knocked-down thug's capsule stays where he fell from; his body is what she reaches for.
	const FVector Where = Thug->IsKnockedDown() && Thug->GetMesh() ? Thug->GetMesh()->GetComponentLocation() : Thug->GetActorLocation();
	const FVector To = Where - From;
	return To.Size2D() <= Range && FMath::Abs(To.Z) <= 200.f;
}
