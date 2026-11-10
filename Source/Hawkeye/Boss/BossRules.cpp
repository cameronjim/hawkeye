#include "Boss/BossRules.h"

void FBossComboClock::Reset(int32 SwingsStarted)
{
	ComboStart = SwingsStarted;
	WindedRemaining = 0.f;
}

bool FBossComboClock::Step(float DeltaSeconds, int32 SwingsStarted, bool bAttacking, const FBossMoveSet& Moves)
{
	if (WindedRemaining > 0.f)
	{
		WindedRemaining = FMath::Max(0.f, WindedRemaining - FMath::Max(DeltaSeconds, 0.f));
		// Swings started while winded (there should be none) do not count toward the next combo.
		ComboStart = SwingsStarted;
		return false;
	}
	if (Moves.WindedAfterSwings <= 0 || Moves.WindedSeconds <= 0.f || bAttacking)
	{
		return false;
	}
	if (SwingsStarted - ComboStart < Moves.WindedAfterSwings)
	{
		return false;
	}
	WindedRemaining = Moves.WindedSeconds;
	ComboStart = SwingsStarted;
	++WindedCount;
	return true;
}

int32 UBossRules::ResolvePhase(const TArray<FBossPhase>& Phases, int32 Current, float HealthFraction, bool bCrewDown, bool bDead)
{
	if (bDead || !Phases.IsValidIndex(Current))
	{
		return Current;
	}
	int32 Target = Current;
	while (Phases.IsValidIndex(Target + 1))
	{
		const FBossPhase& Next = Phases[Target + 1];
		if (Next.Entry == EBossPhaseEntry::CrewDown)
		{
			// Only the crew of the phase he is in opens the one after it.
			if (!bCrewDown || Target != Current)
			{
				break;
			}
		}
		else if (!(HealthFraction < Next.HealthThresholdPercent))
		{
			break;
		}
		++Target;
	}
	return Target;
}

bool UBossRules::IsPhaseOrderValid(const TArray<FBossPhase>& Phases)
{
	for (int32 Index = 1; Index < Phases.Num(); ++Index)
	{
		if (Phases[Index].HealthThresholdPercent > Phases[Index - 1].HealthThresholdPercent)
		{
			return false;
		}
	}
	return true;
}

bool UBossRules::IsBossDamageable(bool bPhaseInvulnerable, bool bTransitioning, bool bTransitionInvulnerable)
{
	return !bPhaseInvulnerable && !(bTransitioning && bTransitionInvulnerable);
}

float UBossRules::WindedBonusDamage(float Damage, bool bWinded, float Scale)
{
	return bWinded && Damage > 0.f && Scale > 1.f ? Damage * (Scale - 1.f) : 0.f;
}

float UBossRules::FinisherDamage(float CurrentHealth, float MaxHealth, float FinisherFraction)
{
	if (FinisherFraction <= 0.f)
	{
		return FMath::Max(CurrentHealth, 0.f);
	}
	return FMath::Clamp(MaxHealth * FinisherFraction, 0.f, FMath::Max(CurrentHealth, 0.f));
}

int32 UBossRules::CheckpointPhase(FName MeetObjective, const TArray<FName>& PhaseObjectives, const TArray<FName>& Completed)
{
	if (!MeetObjective.IsNone() && !Completed.Contains(MeetObjective))
	{
		return -1;
	}
	// The last phase whose objective is done: everything before it is behind her.
	int32 LastDone = -1;
	for (int32 Index = 0; Index < PhaseObjectives.Num(); ++Index)
	{
		if (!PhaseObjectives[Index].IsNone() && Completed.Contains(PhaseObjectives[Index]))
		{
			LastDone = Index;
		}
	}
	for (int32 Index = LastDone + 1; Index < PhaseObjectives.Num(); ++Index)
	{
		if (PhaseObjectives[Index].IsNone() || !Completed.Contains(PhaseObjectives[Index]))
		{
			return Index;
		}
	}
	return PhaseObjectives.Num();
}

float UBossRules::CheckpointHealthFraction(const TArray<FBossPhase>& Phases, int32 PhaseIndex)
{
	// The lowest threshold a health entry at or before this phase set: a crew phase after a health one keeps it.
	float Fraction = 1.f;
	for (int32 Index = 1; Index <= PhaseIndex && Phases.IsValidIndex(Index); ++Index)
	{
		if (Phases[Index].Entry == EBossPhaseEntry::HealthBelow)
		{
			Fraction = FMath::Min(Fraction, Phases[Index].HealthThresholdPercent);
		}
	}
	return FMath::Clamp(Fraction, 0.f, 1.f);
}

FBossHazardState UBossRules::HazardStateAt(float Seconds, const FBossHazardTuning& Tuning)
{
	FBossHazardState State;
	const float T = FMath::Max(Seconds, 0.f) - Tuning.FirstDelaySeconds;
	if (T < 0.f)
	{
		State.Phase = EBossHazardPhase::Waiting;
		State.Alpha = Tuning.FirstDelaySeconds > 0.f ? FMath::Clamp(Seconds / Tuning.FirstDelaySeconds, 0.f, 1.f) : 1.f;
		return State;
	}
	const float Telegraph = FMath::Max(Tuning.TelegraphSeconds, 0.01f);
	const float Sweep = FMath::Max(Tuning.SweepSeconds, 0.01f);
	const float Rest = FMath::Max(Tuning.RestSeconds, 0.f);
	const float Cycle = Telegraph + Sweep + Rest;
	const int32 Sweeps = FMath::FloorToInt(T / Cycle);
	const float In = T - Sweeps * Cycle;
	// Even cycles go start to end; odd ones come back. It rests where its last sweep ended.
	const float From = (Sweeps % 2 == 0) ? 0.f : 1.f;
	const float To = 1.f - From;
	State.Sweeps = Sweeps;
	if (In < Telegraph)
	{
		State.Phase = EBossHazardPhase::Telegraph;
		State.Alpha = In / Telegraph;
		State.Along = From;
	}
	else if (In < Telegraph + Sweep)
	{
		State.Phase = EBossHazardPhase::Sweep;
		State.Alpha = (In - Telegraph) / Sweep;
		State.Along = FMath::Lerp(From, To, FMath::SmoothStep(0.f, 1.f, State.Alpha));
	}
	else
	{
		State.Phase = EBossHazardPhase::Rest;
		State.Alpha = Rest > 0.f ? (In - Telegraph - Sweep) / Rest : 1.f;
		State.Along = To;
		State.Sweeps = Sweeps + 1;
	}
	return State;
}

bool UBossRules::HazardHits(EBossHazardPhase Phase, float DistanceAcross, float DistanceAlong, float SecondsSinceLastHit,
	const FBossHazardTuning& Tuning)
{
	return Phase == EBossHazardPhase::Sweep && FMath::Abs(DistanceAcross) < Tuning.HalfWidth
		&& FMath::Abs(DistanceAlong) < Tuning.HalfLength && SecondsSinceLastHit >= Tuning.HitCooldownSeconds;
}

bool UBossRules::CanBark(bool bOnce, int32 TimesSpoken, float SecondsSinceLast, float CooldownSeconds)
{
	if (TimesSpoken <= 0)
	{
		return true;
	}
	return !bOnce && SecondsSinceLast >= CooldownSeconds;
}

bool UBossRules::IsIntroOver(float IntroSeconds, bool bDialogueBusy, bool bSomeoneHit, float IntroMaxSeconds)
{
	return bSomeoneHit || IntroSeconds >= IntroMaxSeconds || (IntroSeconds >= IntroMinSeconds && !bDialogueBusy);
}

bool UBossRules::IsOnRoof(const TArray<FVector2D>& Outline, float RoofZ, const FVector& Feet)
{
	if (Outline.Num() < 3 || Feet.Z < RoofZ - RoofBelowTolerance || Feet.Z > RoofZ + RoofAboveTolerance)
	{
		return false;
	}
	bool bInside = false;
	const FVector2D P(Feet.X, Feet.Y);
	for (int32 I = 0, J = Outline.Num() - 1; I < Outline.Num(); J = I++)
	{
		const FVector2D& A = Outline[I];
		const FVector2D& B = Outline[J];
		if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
		{
			bInside = !bInside;
		}
	}
	return bInside;
}
