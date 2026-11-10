#include "Audio/HawkeyeMusicRules.h"

int32 HawkeyeMusic::GetLevel(EHawkeyeMusicState State)
{
	switch (State)
	{
	case EHawkeyeMusicState::Roam: return 0;
	case EHawkeyeMusicState::Alert: return 1;
	case EHawkeyeMusicState::Fight: return 2;
	case EHawkeyeMusicState::Duel: return 3;
	default: return -1;
	}
}

EHawkeyeMusicState HawkeyeMusic::FromLevel(int32 Level)
{
	switch (FMath::Clamp(Level, 0, 3))
	{
	case 1: return EHawkeyeMusicState::Alert;
	case 2: return EHawkeyeMusicState::Fight;
	case 3: return EHawkeyeMusicState::Duel;
	default: return EHawkeyeMusicState::Roam;
	}
}

EHawkeyeMusicState HawkeyeMusic::ComputeTarget(const FHawkeyeMusicInputs& Inputs)
{
	if (Inputs.bPlayerDowned)
	{
		return EHawkeyeMusicState::Downed;
	}
	if (Inputs.AlertedStanding > 0)
	{
		// The heavy on his own is a fight, not a duel; with a crowd round him he is the centre of one.
		if (Inputs.bBossAlerted || (Inputs.bHeavyAlerted && Inputs.AlertedStanding >= 1 + HeavyDuelOthers))
		{
			return EHawkeyeMusicState::Duel;
		}
		if (Inputs.AlertedStanding >= 2 || Inputs.NearestAlertedCm <= FightRangeCm)
		{
			return EHawkeyeMusicState::Fight;
		}
		return EHawkeyeMusicState::Alert;
	}
	return Inputs.bCrimeNearby ? EHawkeyeMusicState::Alert : EHawkeyeMusicState::Roam;
}

FHawkeyeMusicLayers HawkeyeMusic::GetLayersFor(EHawkeyeMusicState State, const FHawkeyeMusicInputs& Inputs)
{
	FHawkeyeMusicLayers Layers;
	switch (State)
	{
	case EHawkeyeMusicState::Roam:
		Layers.Pad = (Inputs.bNight || Inputs.bChallengeRunning) ? RoamPadGain : 0.f;
		break;
	case EHawkeyeMusicState::Alert:
	case EHawkeyeMusicState::Fight:
	case EHawkeyeMusicState::Duel:
		// Each level keeps the one under it: a duel is a fight with the motif on top.
		Layers.Pulse = 1.f;
		Layers.Perc = GetLevel(State) >= 2 ? 1.f : 0.f;
		Layers.Motif = GetLevel(State) >= 3 ? 1.f : 0.f;
		break;
	case EHawkeyeMusicState::Downed:
		Layers.Drone = 1.f;
		break;
	case EHawkeyeMusicState::Win:
	default:
		break;
	}
	return Layers;
}

namespace
{
	float Approach(float Current, float Target, float DeltaSeconds, float InSeconds, float OutSeconds)
	{
		const float Seconds = Target > Current ? InSeconds : OutSeconds;
		const float Step = Seconds > 0.f ? FMath::Max(DeltaSeconds, 0.f) / Seconds : 1.f;
		return Target > Current ? FMath::Min(Current + Step, Target) : FMath::Max(Current - Step, Target);
	}
}

FHawkeyeMusicLayers HawkeyeMusic::AdvanceLayers(const FHawkeyeMusicLayers& Current, const FHawkeyeMusicLayers& Target,
	float DeltaSeconds, bool bFast)
{
	const float Out = bFast ? FastFadeOutSeconds : FadeOutSeconds;
	FHawkeyeMusicLayers Next;
	Next.Pad = Approach(Current.Pad, Target.Pad, DeltaSeconds, FadeInSeconds, Out);
	Next.Pulse = Approach(Current.Pulse, Target.Pulse, DeltaSeconds, FadeInSeconds, Out);
	Next.Perc = Approach(Current.Perc, Target.Perc, DeltaSeconds, FadeInSeconds, Out);
	Next.Motif = Approach(Current.Motif, Target.Motif, DeltaSeconds, FadeInSeconds, Out);
	Next.Drone = Approach(Current.Drone, Target.Drone, DeltaSeconds, FadeInSeconds, Out);
	return Next;
}

const TCHAR* HawkeyeMusic::GetStateName(EHawkeyeMusicState State)
{
	switch (State)
	{
	case EHawkeyeMusicState::Roam: return TEXT("Roam");
	case EHawkeyeMusicState::Alert: return TEXT("Alert");
	case EHawkeyeMusicState::Fight: return TEXT("Fight");
	case EHawkeyeMusicState::Duel: return TEXT("Duel");
	case EHawkeyeMusicState::Win: return TEXT("Win");
	case EHawkeyeMusicState::Downed: return TEXT("Downed");
	default: return TEXT("?");
	}
}

bool FHawkeyeMusicDirector::SetState(EHawkeyeMusicState NewState)
{
	Cooldown = 0.f;
	if (NewState == State)
	{
		return false;
	}
	Previous = State;
	State = NewState;
	return true;
}

bool FHawkeyeMusicDirector::Step(const FHawkeyeMusicInputs& Inputs, float DeltaSeconds)
{
	using namespace HawkeyeMusic;
	const float Dt = FMath::Max(DeltaSeconds, 0.f);
	const EHawkeyeMusicState Target = ComputeTarget(Inputs);

	// Downed beats everything, and ends straight into whatever the fight is.
	if (Target == EHawkeyeMusicState::Downed)
	{
		return SetState(EHawkeyeMusicState::Downed);
	}
	if (State == EHawkeyeMusicState::Downed)
	{
		return SetState(Target);
	}

	// The last one of the fight went down: the sting, whatever the level was.
	// Once per fight: someone engaged and standing again (or the fight forgotten) makes a new one to win.
	if (Inputs.EngagedStanding > 0 || Inputs.EngagedDown == 0)
	{
		bWinSpent = false;
	}
	const bool bFightWon = Inputs.EngagedDown > 0 && Inputs.EngagedStanding == 0 && !bWinSpent;
	if (GetLevel(State) > 0 && bFightWon)
	{
		WinRemaining = WinSeconds;
		bWinSpent = true;
		return SetState(EHawkeyeMusicState::Win);
	}
	if (State == EHawkeyeMusicState::Win)
	{
		// A thug alerted cuts the sting short; a crime starting nearby waits for it. Otherwise it plays out
		// and the music goes quiet.
		if (GetLevel(Target) > 0 && Inputs.AlertedStanding > 0)
		{
			return SetState(Target);
		}
		WinRemaining -= Dt;
		if (WinRemaining > 0.f)
		{
			return false;
		}
		return SetState(EHawkeyeMusicState::Roam);
	}

	const int32 Current = GetLevel(State);
	const int32 Wanted = GetLevel(Target);
	if (Wanted > Current)
	{
		return SetState(Target);
	}
	if (Wanted == Current)
	{
		Cooldown = 0.f;
		return false;
	}
	Cooldown += Dt;
	if (Cooldown + KINDA_SMALL_NUMBER < CooldownSeconds)
	{
		return false;
	}
	return SetState(FromLevel(FMath::Max(Wanted, Current - 1)));
}
