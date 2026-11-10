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

bool FBossComboClock::ForceWinded(int32 SwingsStarted, float Seconds)
{
	if (WindedRemaining > 0.f || Seconds <= 0.f)
	{
		return false;
	}
	WindedRemaining = Seconds;
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

bool UBossRules::IsFinisherOpen(bool bKnockedDown, bool bParryStaggered, float ComboEnderWindow, float GrantedWindow, bool bNeedsGrantedWindow)
{
	if (GrantedWindow > 0.f)
	{
		return true;
	}
	return !bNeedsGrantedWindow && (bKnockedDown || bParryStaggered || ComboEnderWindow > 0.f);
}

bool UBossRules::IsGuarding(float GuardDamageFraction, bool bFighting, bool bWinded)
{
	return GuardDamageFraction < 1.f && bFighting && !bWinded;
}

float UBossRules::BossDamageScale(bool bInBeat, bool bGuarding, float GuardDamageFraction)
{
	if (bInBeat)
	{
		return 0.f;
	}
	return bGuarding ? FMath::Clamp(GuardDamageFraction, 0.f, 1.f) : 1.f;
}

bool UBossRules::BreaksGuard(EArrowHitEffect Effect, float Distance, const TArray<EArrowHitEffect>& Sources, float Radius, bool bGuarding)
{
	return bGuarding && Effect != EArrowHitEffect::None && Sources.Contains(Effect) && Distance <= Radius;
}

bool UBossRules::IsDecisiveFinisher(float CurrentHealth, float MaxHealth, float FinisherFraction, float DamageScale, float NextThreshold)
{
	if (CurrentHealth <= 0.f || MaxHealth <= 0.f)
	{
		return false;
	}
	const float Blow = FMath::Min(FinisherDamage(CurrentHealth, MaxHealth, FinisherFraction) * FMath::Max(DamageScale, 1.f), CurrentHealth);
	if (Blow >= CurrentHealth - KINDA_SMALL_NUMBER)
	{
		return true;
	}
	return NextThreshold >= 0.f && (CurrentHealth - Blow) / MaxHealth < NextThreshold;
}

void UBossRules::BeatCamera(const FVector& Kate, const FVector& Boss, const FBossBeat& Beat, FVector& OutLocation, FRotator& OutRotation)
{
	FVector Forward = (Boss - Kate).GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	const FVector Middle = (Kate + Boss) * 0.5f;
	OutLocation = Middle + Forward * Beat.CameraOffset.X + Right * Beat.CameraOffset.Y + FVector::UpVector * Beat.CameraOffset.Z;
	const FVector Chest(0.f, 0.f, 40.f);
	const FVector Look = FMath::Lerp(Kate, Boss, FMath::Clamp(Beat.LookAtBoss, 0.f, 1.f)) + Chest;
	// Both of them in the shot: back along the view until each is inside BeatFrameFraction of half the field of view.
	const float Limit = FMath::DegreesToRadians(FMath::Clamp(Beat.FieldOfView, 20.f, 120.f) * 0.5f * BeatFrameFraction);
	for (float Pushed = 0.f; Pushed <= BeatMaxPushBack; Pushed += 25.f)
	{
		const FVector View = (Look - OutLocation).GetSafeNormal();
		const float Worst = FMath::Max(FMath::Acos(FVector::DotProduct(View, (Kate + Chest - OutLocation).GetSafeNormal())),
			FMath::Acos(FVector::DotProduct(View, (Boss + Chest - OutLocation).GetSafeNormal())));
		if (Worst <= Limit)
		{
			break;
		}
		OutLocation -= View * 25.f;
	}
	OutRotation = (Look - OutLocation).Rotation();
	OutRotation.Roll = 0.f;
}

bool UBossRules::IsBeatOver(float Elapsed, float Seconds, bool bSkipPressed)
{
	return Elapsed >= Seconds || (bSkipPressed && Elapsed >= BeatMinSkipSeconds);
}

TArray<FName> UBossRules::UnheardLines(const TArray<FName>& Lines, const TArray<FName>& Heard)
{
	TArray<FName> Out;
	for (const FName Line : Lines)
	{
		if (!Line.IsNone() && !Heard.Contains(Line) && !Out.Contains(Line))
		{
			Out.Add(Line);
		}
	}
	return Out;
}

bool UBossRules::PinsHazard(EArrowHitEffect Effect, float DistanceToBody, float DistanceToTether, const FBossHazardTuning& Tuning)
{
	return Effect == EArrowHitEffect::Putty && Tuning.PinSeconds > 0.f
		&& FMath::Min(DistanceToBody, DistanceToTether) <= Tuning.PinRadius;
}

float UBossRules::PushedAlong(float PropAlong, float PropAcross, float PropRadius, float BodyAlong, float Direction, float LaneLength,
	const FBossHazardTuning& Tuning)
{
	const float Dir = Direction >= 0.f ? 1.f : -1.f;
	const float Ahead = (PropAlong - BodyAlong) * Dir;
	if (FMath::Abs(PropAcross) >= Tuning.HalfWidth + FMath::Max(PropRadius, 0.f) || Ahead < -0.5f * Tuning.HalfLength
		|| Ahead >= Tuning.HalfLength)
	{
		return PropAlong;
	}
	// Off the ends by a body's reach at most: where the body comes to rest it has passed the prop, so the sweep back
	// leaves it there instead of dragging it home.
	return FMath::Clamp(BodyAlong + Dir * Tuning.HalfLength, -Tuning.HalfLength, FMath::Max(LaneLength, 0.f) + Tuning.HalfLength);
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
