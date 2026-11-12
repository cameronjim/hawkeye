#include "World/LuckyRules.h"

ELuckyState FLuckyRules::ChooseState(const FLuckyFollowSettings& Settings, ELuckyState Current, float Distance,
	float HeightGap, float KateStillSeconds)
{
	const bool bInReach = Distance <= Settings.NoticeRadius && FMath::Abs(HeightGap) <= Settings.MaxHeightGap;
	const bool bStill = KateStillSeconds >= Settings.SitDelay;
	if (!bInReach)
	{
		// Out of reach he stops where he is; a sitting dog stays sat.
		return Current == ELuckyState::Sit ? ELuckyState::Sit : ELuckyState::Idle;
	}
	const ELuckyState Pace = Distance > Settings.TrotDistance ? ELuckyState::Trot : ELuckyState::Walk;
	if (IsMoving(Current))
	{
		if (Distance > Settings.StopDistance)
		{
			return Pace;
		}
		return bStill ? ELuckyState::Sit : ELuckyState::Idle;
	}
	if (Distance > Settings.StartFollowDistance)
	{
		return Pace;
	}
	// Near her and not moving: he sits once she has stood still long enough, and gets up when she moves.
	return bStill ? ELuckyState::Sit : ELuckyState::Idle;
}

float FLuckyRules::PlayRateFor(float Speed, float ClipSpeed, float MinRate, float MaxRate)
{
	if (ClipSpeed <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	return FMath::Clamp(Speed / ClipSpeed, MinRate, MaxRate);
}
