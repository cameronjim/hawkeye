#include "Combat/ReturnFlight.h"

bool UHawkeyeReturnFlightRules::CanHitMore(int32 HitsSoFar, int32 MaxTargets)
{
	return HitsSoFar < FMath::Max(MaxTargets, 1);
}

EHawkeyeReturnTurn UHawkeyeReturnFlightRules::TurnBack(float DistanceOut, float MaxRange, int32 HitsSoFar, int32 MaxTargets, bool bHitWall,
	bool bHitSwitch)
{
	if (bHitSwitch)
	{
		return EHawkeyeReturnTurn::Switch;
	}
	if (bHitWall)
	{
		return EHawkeyeReturnTurn::Wall;
	}
	if (!CanHitMore(HitsSoFar, MaxTargets))
	{
		return EHawkeyeReturnTurn::Targets;
	}
	return DistanceOut >= MaxRange ? EHawkeyeReturnTurn::Range : EHawkeyeReturnTurn::None;
}

float UHawkeyeReturnFlightRules::ReturnLength(const TArray<FVector>& OutPath, const FVector& Catcher)
{
	if (OutPath.Num() == 0)
	{
		return 0.f;
	}
	float Length = 0.f;
	for (int32 Index = OutPath.Num() - 1; Index > 0; --Index)
	{
		Length += FVector::Dist(OutPath[Index], OutPath[Index - 1]);
	}
	return Length + FVector::Dist(OutPath[0], Catcher);
}

FVector UHawkeyeReturnFlightRules::PointAlongReturn(const TArray<FVector>& OutPath, const FVector& Catcher, float Travelled)
{
	if (OutPath.Num() == 0)
	{
		return Catcher;
	}
	float Left = FMath::Max(Travelled, 0.f);
	FVector From = OutPath.Last();
	for (int32 Index = OutPath.Num() - 2; Index >= -1; --Index)
	{
		const FVector To = Index >= 0 ? OutPath[Index] : Catcher;
		const float Leg = FVector::Dist(From, To);
		if (Left <= Leg)
		{
			return Leg > KINDA_SMALL_NUMBER ? FMath::Lerp(From, To, Left / Leg) : To;
		}
		Left -= Leg;
		From = To;
	}
	return Catcher;
}

bool UHawkeyeReturnFlightRules::IsCaught(const FVector& Velocity, const FVector& Facing, bool bBusy, float ConeDegrees)
{
	if (bBusy)
	{
		return false;
	}
	const FVector Coming = (-Velocity).GetSafeNormal2D();
	const FVector Ahead = Facing.GetSafeNormal2D();
	if (Coming.IsNearlyZero() || Ahead.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(Coming, Ahead) >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(ConeDegrees, 0.f, 180.f))) - KINDA_SMALL_NUMBER;
}

float UHawkeyeReturnFlightRules::SelfHitDamage(float Damage, float CurrentHealth)
{
	return FMath::Max(FMath::Min(Damage, CurrentHealth - 1.f), 0.f);
}
