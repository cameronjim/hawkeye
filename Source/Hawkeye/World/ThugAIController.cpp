#include "World/ThugAIController.h"

#include "Hawkeye.h"
#include "Camera/CameraComponent.h"
#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/CombatTokenSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/WeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Player/HawkeyeCharacter.h"
#include "StateTree.h"
#include "TimerManager.h"
#include "World/GrappleAnchor.h"
#include "World/ThugHearing.h"
#include "World/ThugTreeComponent.h"
#include "Settings/DifficultySubsystem.h"

namespace HawkeyeThugBrain
{
	/** Eye height above the capsule centre for line checks, cm. */
	static constexpr float EyeHeight = 60.f;
	/** A cover point this close to the one he is leaving is the same cover, cm. */
	static constexpr float SameCoverDistance = 200.f;
	/** A cover point more than this above or below him is another level (a fire escape, an awning), cm. */
	static constexpr float CoverMaxStep = 150.f;
	/** A patrol point with this tag is one he turns to face (its yaw) while he waits there. */
	static const FName PatrolFacingTag(TEXT("PatrolFacing"));
	/** A thug walking to his spot on a ring round her picks it again at most this often, seconds. */
	static constexpr float RingRepathSeconds = 0.5f;
	/**
	 * Within this of the ring's distance he is on it and ready for a token, cm: path following stops short of a
	 * goal by its acceptance radius plus his own, and a thug who counted only at the exact ring would wait forever.
	 */
	static constexpr float RingReadySlack = 150.f;
}

AThugAIController::AThugAIController()
{
	PrimaryActorTick.bCanEverTick = false;

	ThugPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("ThugPerception"));

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = SightHalfAngleDegrees;
	SightConfig->SetMaxAge(0.f);
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = HearingRange;
	HearingConfig->SetMaxAge(0.f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	ThugPerception->ConfigureSense(*SightConfig);
	ThugPerception->ConfigureSense(*HearingConfig);
	ThugPerception->SetDominantSense(SightConfig->GetSenseImplementation());

	SetPerceptionComponent(*ThugPerception);

	StateTreeComponent = CreateDefaultSubobject<UHawkeyeThugTreeComponent>(TEXT("StateTreeComponent"));
	DefaultStateTree = TSoftObjectPtr<UStateTree>(FSoftObjectPath(TEXT("/Game/Blueprints/AI/Thug/ST_Thug.ST_Thug")));
	// Started in OnPossess, once there is a pawn to read the tree from.
	StateTreeComponent->SetStartLogicAutomatically(false);
}

// --- Rules -----------------------------------------------------------------------------------------

bool AThugAIController::IsAimedAtPoint(const FVector& ViewLocation, const FVector& ViewDirection, const FVector& Point,
	float ToleranceDegrees)
{
	const FVector To = (Point - ViewLocation).GetSafeNormal();
	const FVector Forward = ViewDirection.GetSafeNormal();
	if (To.IsNearlyZero() || Forward.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(To, Forward) >= FMath::Cos(FMath::DegreesToRadians(ToleranceDegrees));
}

FVector AThugAIController::ComputeLeadAimPoint(const FVector& From, const FVector& TargetLocation,
	const FVector& TargetVelocity, float ProjectileSpeed, float GravityZ)
{
	if (ProjectileSpeed <= KINDA_SMALL_NUMBER)
	{
		return TargetLocation;
	}
	// |D + V t| = s t  ->  (V.V - s^2) t^2 + 2 (D.V) t + D.D = 0, smallest positive root.
	const FVector D = TargetLocation - From;
	const double A = FVector::DotProduct(TargetVelocity, TargetVelocity) - static_cast<double>(ProjectileSpeed) * ProjectileSpeed;
	const double B = 2.0 * FVector::DotProduct(D, TargetVelocity);
	const double C = FVector::DotProduct(D, D);
	double T = -1.0;
	if (FMath::Abs(A) < 1e-6)
	{
		T = FMath::Abs(B) > 1e-6 ? -C / B : -1.0;
	}
	else
	{
		const double Disc = B * B - 4.0 * A * C;
		if (Disc >= 0.0)
		{
			const double Root = FMath::Sqrt(Disc);
			const double T1 = (-B - Root) / (2.0 * A);
			const double T2 = (-B + Root) / (2.0 * A);
			const double Lo = FMath::Min(T1, T2);
			const double Hi = FMath::Max(T1, T2);
			T = Lo > 0.0 ? Lo : Hi;
		}
	}
	if (T <= 0.0)
	{
		return TargetLocation;
	}
	// Gravity pulls the arrow down 0.5 g t^2 over the flight; aim that much higher.
	return TargetLocation + TargetVelocity * T + FVector(0.f, 0.f, -0.5 * GravityZ * T * T);
}

EArcherRangeAction AThugAIController::ChooseArcherRangeAction(float Distance, float CloseRange, float MinRange, float MaxRange)
{
	if (Distance < CloseRange)
	{
		return EArcherRangeAction::Relocate;
	}
	if (Distance < MinRange)
	{
		return EArcherRangeAction::StepBack;
	}
	if (Distance > MaxRange)
	{
		return EArcherRangeAction::Approach;
	}
	return EArcherRangeAction::Hold;
}

bool AThugAIController::ComputeRetreatGoal(const FVector& Thug, const FVector& Player, float TriggerDistance,
	float RetreatDistance, FVector& OutGoal)
{
	const FVector Offset = Thug - Player;
	if (Offset.Size2D() >= TriggerDistance)
	{
		OutGoal = Thug;
		return false;
	}
	FVector Away = Offset.GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector(1.f, 0.f, 0.f);
	}
	OutGoal = FVector(Player.X, Player.Y, Thug.Z) + Away * RetreatDistance;
	return true;
}

// --- Possession ------------------------------------------------------------------------------------

void AThugAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (ThugPerception)
	{
		ThugPerception->OnTargetPerceptionUpdated.AddDynamic(
			this, &AThugAIController::HandleTargetPerceptionUpdated);
	}

	LastStimulusLocation = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;

	AThugCharacter* Thug = GetThug();
	if (Thug)
	{
		if (const UCharacterMovementComponent* Movement = Thug->GetCharacterMovement())
		{
			BaseWalkSpeed = Movement->MaxWalkSpeed;
		}
	}

	UStateTree* Tree = Thug && Thug->ThugStateTree ? Thug->ThugStateTree.Get() : DefaultStateTree.LoadSynchronous();
	if (Thug && StateTreeComponent)
	{
		StateTreeComponent->SetStateTree(Tree);
		StateTreeComponent->StartLogic();
	}
	if (!StateTreeComponent || !StateTreeComponent->IsRunning())
	{
		UE_LOG(LogHawkeye, Error, TEXT("%s: %s's StateTree %s did not start; he will stand still. Run Tools/Editor/create_enemies.py."),
			*GetName(), *GetNameSafe(InPawn), *GetNameSafe(Tree));
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: driving %s (StateTree %s, cover %s)."), *GetName(), *GetNameSafe(InPawn), *GetNameSafe(Tree),
		Thug && Thug->CoverQuery ? *FString::Printf(TEXT("EQS %s"), *GetNameSafe(Thug->CoverQuery)) : TEXT("C++ ring"));

	if (UWorld* World = GetWorld(); World && bThinkingEnabled)
	{
		World->GetTimerManager().SetTimer(
			ThinkTimerHandle, this, &AThugAIController::TickThink, ThinkIntervalSeconds, true);
	}
	AimStream.Initialize(GetTypeHash(GetNameSafe(InPawn)));
	UpdateTrackTimer();
}

void AThugAIController::UpdateTrackTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const AThugCharacter* Thug = GetThug();
	const bool bRanged = Thug && (Thug->IsArcher() || Thug->IsGunner());
	if (bRanged && bThinkingEnabled)
	{
		if (!World->GetTimerManager().IsTimerActive(TrackTimerHandle))
		{
			World->GetTimerManager().SetTimer(TrackTimerHandle, this, &AThugAIController::RecordTargetSample,
				HawkeyeThugAim::SampleSeconds, true);
		}
		return;
	}
	World->GetTimerManager().ClearTimer(TrackTimerHandle);
	TargetTrack.Reset();
}

void AThugAIController::RecordTargetSample()
{
	const AActor* Target = IsValid(TargetActor) ? TargetActor.Get() : FindPlayerPawn();
	if (!Target)
	{
		return;
	}
	const ACharacter* TargetCharacter = Cast<ACharacter>(Target);
	const UCharacterMovementComponent* Movement = TargetCharacter ? TargetCharacter->GetCharacterMovement() : nullptr;
	const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(Target);
	TargetTrack.AddSample(GetNowSeconds(), GetAimPointOn(Target), Target->GetVelocity(), Movement && Movement->IsFalling(),
		Kate && Kate->IsDodging());
}

FHawkeyeRangedAim AThugAIController::ComputeRangedAim(const FVector& From, float ProjectileSpeed, float BaseConeDegrees)
{
	FHawkeyeRangedAim Out;
	const AActor* Target = TargetActor.Get();
	if (!IsValid(Target))
	{
		Out.AimPoint = From + (GetPawn() ? GetPawn()->GetActorForwardVector() : FVector::ForwardVector) * 1000.f;
		return Out;
	}
	// Always at least the look he is taking now, so a thug with no history still aims.
	RecordTargetSample();
	float Radius = 34.f;
	float HalfHeight = 88.f;
	Target->GetSimpleCollisionCylinder(Radius, HalfHeight);
	const UWorld* World = GetWorld();
	const FHawkeyeRangedShot Shot = HawkeyeThugAim::BuildShot(TargetTrack, GetNowSeconds(), From, GetAimPointOn(Target),
		Target->GetVelocity(), UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::RangedTrackingLagSeconds), ProjectileSpeed,
		World ? World->GetGravityZ() : -980.f, BaseConeDegrees,
		UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::RangedConeScale), Radius);
	Out = HawkeyeThugAim::ComputeShot(Shot, AimStream);
	if (Out.bForcedMiss)
	{
		++ForcedMisses;
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s jinked (a sharp turn or a dodge in the last %.1f s) at %.0f cm: this shot goes wide."),
			*GetNameSafe(GetPawn()), *GetNameSafe(Target), HawkeyeThugAim::ErraticWindowSeconds, FVector::Dist(From, Shot.ChestNow));
	}
	else
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: aims %.2f s behind %s, cone %.1f deg."), *GetNameSafe(GetPawn()),
			Shot.TrackingLagSeconds, *GetNameSafe(Target), Out.ConeDegrees);
	}
	return Out;
}

void AThugAIController::SetThinkingEnabled(bool bEnabled)
{
	if (bThinkingEnabled == bEnabled)
	{
		return;
	}
	bThinkingEnabled = bEnabled;

	UWorld* World = GetWorld();
	if (!bEnabled)
	{
		StopMovement();
		CancelBurst(TEXT("thinking off"));
		CancelArcherDraw(TEXT("thinking off"));
		ReleaseAttackToken();
		if (World)
		{
			World->GetTimerManager().ClearTimer(ThinkTimerHandle);
		}
		UpdateTrackTimer();
		return;
	}
	if (World && GetPawn())
	{
		World->GetTimerManager().SetTimer(
			ThinkTimerHandle, this, &AThugAIController::TickThink, ThinkIntervalSeconds, true);
	}
	UpdateTrackTimer();
	ResyncPerception();
}

void AThugAIController::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Me = GetPawn();
	if (!Me)
	{
		Super::GetActorEyesViewPoint(OutLocation, OutRotation);
		return;
	}
	OutLocation = Me->GetPawnViewLocation();
	OutRotation = GetAlertState() == EThugAlertState::Alerted ? GetControlRotation() : FRotator(LookPitch, Me->GetActorRotation().Yaw, 0.f);
}

void AThugAIController::ResyncPerception()
{
	APawn* Player = FindPlayerPawn();
	FActorPerceptionBlueprintInfo Info;
	if (!Player || !ThugPerception || !bThinkingEnabled || bPacified || !ThugPerception->GetActorsPerception(Player, Info))
	{
		return;
	}
	for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
	{
		if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Stimulus.WasSuccessfullySensed())
		{
			HandleTargetPerceptionUpdated(Player, Stimulus);
		}
	}
}

void AThugAIController::OnUnPossess()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimerHandle);
		World->GetTimerManager().ClearTimer(ArcherHoldTimerHandle);
		World->GetTimerManager().ClearTimer(TrackTimerHandle);
	}
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(TEXT("Unpossessed"));
	}
	ReleaseAttackToken();

	if (ThugPerception)
	{
		ThugPerception->OnTargetPerceptionUpdated.RemoveDynamic(
			this, &AThugAIController::HandleTargetPerceptionUpdated);
	}

	Super::OnUnPossess();
}

void AThugAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimerHandle);
		World->GetTimerManager().ClearTimer(ArcherHoldTimerHandle);
		World->GetTimerManager().ClearTimer(TrackTimerHandle);
	}
	ReleaseAttackToken();

	Super::EndPlay(EndPlayReason);
}

AThugCharacter* AThugAIController::GetThug() const
{
	return Cast<AThugCharacter>(GetPawn());
}

EThugAlertState AThugAIController::GetAlertState() const
{
	const AThugCharacter* Thug = GetThug();
	return Thug ? Thug->GetAlertState() : EThugAlertState::Calm;
}

bool AThugAIController::IsInState(FName State) const
{
	return StateTreeComponent && StateTreeComponent->IsStateActive(State);
}

EThugMode AThugAIController::GetMode() const
{
	const UEnum* Modes = StaticEnum<EThugMode>();
	for (int32 Index = 0; Index < Modes->NumEnums() - 1; ++Index)
	{
		if (IsInState(FName(*Modes->GetNameStringByIndex(Index))))
		{
			return static_cast<EThugMode>(Modes->GetValueByIndex(Index));
		}
	}
	return EThugMode::Stunned;
}

bool AThugAIController::IsStandingOff() const
{
	return IsInState(HawkeyeThugStates::StandOff);
}

EGunnerPhase AThugAIController::GetGunnerPhase() const
{
	if (IsInState(HawkeyeThugStates::Peek))
	{
		return EGunnerPhase::Peeking;
	}
	return IsInState(HawkeyeThugStates::Cover) ? EGunnerPhase::Covering : EGunnerPhase::Open;
}

bool AThugAIController::IsRetreating() const
{
	return IsInState(HawkeyeThugStates::BackAway);
}

bool AThugAIController::IsZipping() const
{
	return IsInState(HawkeyeThugStates::ArcherZip);
}

double AThugAIController::GetNowSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

int32 AThugAIController::GetArrowsLoosed() const
{
	const AThugCharacter* Thug = GetThug();
	const UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	return Bow ? Bow->GetArrowsLoosed() : 0;
}

void AThugAIController::SetState(EThugAlertState NewState)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || Thug->GetAlertState() == NewState)
	{
		return;
	}

	Thug->SetAlertState(NewState);

	// Each state starts from a clean slate; a re-entry must not inherit the last one's clocks.
	InvestigateElapsed = 0.f;
	SeenSeconds = 0.f;
	UnseenSeconds = 0.f;
	LookPitch = 0.f;

	SwingsSinceBackOff = 0;
	ReleaseAttackToken();

	CancelBurst(TEXT("alert state changed"));
	bHasCover = false;
	ShotsSinceCover = 0;
	BurstPauseRemaining = 0.f;

	if (NewState == EThugAlertState::Alerted)
	{
		// The squad hears about it after SquadAlertDelay (claude-docs/gameplay-semantics.md).
		bSquadAlertPending = true;
		SquadAlertRemaining = SquadAlertDelay;
	}
	else
	{
		bSquadAlertPending = false;
		CancelArcherDraw(TEXT("no longer alerted"));
		StopMovement();
	}
	ApplyMoveSpeed();
}

void AThugAIController::ApplyMoveSpeed()
{
	AThugCharacter* Thug = GetThug();
	UCharacterMovementComponent* Movement = Thug ? Thug->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	// The heavy never rushes: he walks at his own pace behind the shield.
	const bool bRush = Thug->IsAlerted() && !Thug->IsGunner() && !Thug->IsArcher() && !Thug->IsHeavy();
	Movement->MaxWalkSpeed = bRush ? RushSpeed : BaseWalkSpeed;
}

// --- Senses ----------------------------------------------------------------------------------------

void AThugAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!bThinkingEnabled || !Actor || !Actor->IsA<APawn>() || !Cast<APawn>(Actor)->IsPlayerControlled())
	{
		return;
	}

	const bool bIsSight = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();
	// The archer's eyes are his own check (ArcherAggroRange, any direction); perception's cone is not.
	const AThugCharacter* Thug = GetThug();
	if (bIsSight && Thug && Thug->IsArcher())
	{
		return;
	}
	// Crouched in the smoke she is not there, to eyes or ears.
	bool bSensed = Stimulus.WasSuccessfullySensed() && !IsHiddenInSmoke(Actor);
	// Perception hears through anything; walls and closed doors stop it here (HawkeyeThugHearing).
	if (!bIsSight && bSensed && GetPawn())
	{
		const float Range = (HearingConfig ? HearingConfig->HearingRange : HearingRange) * Stimulus.Strength;
		const FVector Ear = GetPawn()->GetActorLocation() + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight);
		const HawkeyeThugHearing::FHeardNoise Heard = HawkeyeThugHearing::Hear(GetWorld(), Stimulus.StimulusLocation, Ear, Range,
			{ GetPawn(), Actor });
		if (!Heard.bHeard)
		{
			UE_LOG(LogHawkeye, Verbose, TEXT("%s: a noise %.0f cm off does not carry to him (path %.0f cm, %d closed door(s), range %.0f)."),
				*GetNameSafe(GetPawn()), FVector::Dist(Stimulus.StimulusLocation, Ear), Heard.PathLength, Heard.ClosedDoors, Range);
			return;
		}
	}
	if (bSensed)
	{
		TargetActor = Actor;
	}

	ReportStimulus(
		bIsSight ? EStimulusKind::Sight : EStimulusKind::Hearing,
		Stimulus.StimulusLocation,
		bSensed,
		Stimulus.Strength);
}

void AThugAIController::ReportStimulus(EStimulusKind Kind, FVector Location, bool bSuccessful, float Loudness)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || bPacified)
	{
		return;
	}

	if (Kind == EStimulusKind::Sight)
	{
		bPerceivesTarget = bSuccessful;
		bSeesTarget = bSuccessful;
		if (!bSuccessful)
		{
			return;
		}

		LastSightLocation = Location;
		bHasSightLocation = true;
		UpdateBlinded();
		if (bBlinded)
		{
			// In the cone, but through smoke: nothing to see.
			bSeesTarget = false;
			return;
		}

		LastStimulusLocation = Location;
		UnseenSeconds = 0.f;

		// A glimpse makes him suspicious; only a sustained look confirms it (see Think).
		if (Thug->GetAlertState() == EThugAlertState::Calm)
		{
			SetState(EThugAlertState::Suspicious);
		}
		return;
	}

	if (!bSuccessful)
	{
		return;
	}

	LastStimulusLocation = Location;

	// A gunshot is unambiguous; footsteps are not.
	if (Loudness >= GunshotLoudnessThreshold)
	{
		SetState(EThugAlertState::Alerted);
		UnseenSeconds = 0.f;
		return;
	}

	if (Thug->GetAlertState() == EThugAlertState::Calm)
	{
		SetState(EThugAlertState::Suspicious);
	}
}

FVector AThugAIController::GetAimPointOn(const AActor* Target) const
{
	if (!Target)
	{
		return FVector::ZeroVector;
	}
	// Crouched she is only as tall as her capsule centre: a parapet at that height covers her.
	const ACharacter* TargetCharacter = Cast<ACharacter>(Target);
	const float Height = TargetCharacter && TargetCharacter->bIsCrouched ? 0.f : ChestHeight;
	return Target->GetActorLocation() + FVector(0.f, 0.f, Height);
}

APawn* AThugAIController::FindPlayerPawn() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

bool AThugAIController::HasLineTo(const FVector& Point, const AActor* IgnoreActor) const
{
	const APawn* Me = GetPawn();
	const UWorld* World = GetWorld();
	if (!Me || !World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugLine), false, Me);
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}
	const FVector Eye = Me->GetActorLocation() + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight);
	return !World->LineTraceTestByChannel(Eye, Point, ECC_Visibility, Params);
}

void AThugAIController::UpdateArcherSight()
{
	AActor* Candidate = IsValid(TargetActor) ? TargetActor.Get() : FindPlayerPawn();
	const APawn* Me = GetPawn();
	if (!Candidate || !Me)
	{
		return;
	}
	const FVector Chest = GetAimPointOn(Candidate);
	const bool bInRange = FVector::Dist(Candidate->GetActorLocation(), Me->GetActorLocation()) <= ArcherAggroRange;
	// Indoors a calm archer is a sentry like any other: only his sight cone.
	bool bLooking = true;
	if (const AThugCharacter* Archer = GetThug(); Archer && !Archer->bArcherSeesAllRound && Archer->GetAlertState() == EThugAlertState::Calm)
	{
		const FVector Eye = Me->GetActorLocation() + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight);
		bLooking = FVector::Dist(Chest, Eye) <= SightRadius && FVector::DotProduct(Me->GetActorForwardVector(), (Chest - Eye).GetSafeNormal())
			>= FMath::Cos(FMath::DegreesToRadians(SightHalfAngleDegrees));
	}
	const bool bVisible = bInRange && bLooking && !IsHiddenInSmoke(Candidate) && HasLineTo(Chest, Candidate);
	if (bVisible)
	{
		TargetActor = Candidate;
	}
	ReportStimulus(EStimulusKind::Sight, Candidate->GetActorLocation(), bVisible);
}

void AThugAIController::UpdateAimedAt()
{
	bAimedAt = false;
	const AHawkeyeCharacter* Player = Cast<AHawkeyeCharacter>(TargetActor);
	const APawn* Me = GetPawn();
	if (!Player || !Me || !(Player->IsAiming() || Player->IsDrawingBow()))
	{
		return;
	}
	const UCameraComponent* Camera = Player->GetFollowCamera();
	const FVector View = Camera ? Camera->GetComponentLocation() : Player->GetPawnViewLocation();
	const FVector Forward = Camera ? Camera->GetForwardVector() : Player->GetControlRotation().Vector();
	bAimedAt = IsAimedAtPoint(View, Forward, Me->GetActorLocation() + FVector(0.f, 0.f, 20.f), AimedAtToleranceDegrees);
}

void AThugAIController::UpdateSquadAlert(float DeltaSeconds)
{
	if (!bSquadAlertPending)
	{
		return;
	}
	SquadAlertRemaining -= DeltaSeconds;
	if (SquadAlertRemaining > 0.f)
	{
		return;
	}
	bSquadAlertPending = false;
	AThugCharacter* Me = GetThug();
	UWorld* World = GetWorld();
	if (!Me || !World || !Me->IsAlerted())
	{
		return;
	}
	const FVector Where = IsValid(TargetActor) ? TargetActor->GetActorLocation() : LastStimulusLocation;
	const FVector MyEye = Me->GetActorLocation() + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight);
	int32 Told = 0;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		AThugCharacter* Other = *It;
		const UHealthComponent* Health = Other ? Other->GetHealthComponent() : nullptr;
		// Another crew (AlertGroup) is never told.
		if (Other == Me || !Health || !Health->IsAlive() || Other->IsLimp() || Other->IsAlerted() || Other->AlertGroup != Me->AlertGroup
			|| FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation()) > SquadAlertRadius)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugSquad), false, Me);
		Params.AddIgnoredActor(Other);
		const FVector OtherEye = Other->GetActorLocation() + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight);
		if (World->LineTraceTestByChannel(MyEye, OtherEye, ECC_Visibility, Params))
		{
			continue;
		}
		if (AThugAIController* Brain = Cast<AThugAIController>(Other->GetController()))
		{
			Brain->ReceiveSquadAlert(Where, Me);
			++Told;
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: squad alert after %.1f s: %d thug(s) within %.0f cm with a line to him."),
		*Me->GetName(), SquadAlertDelay, Told, SquadAlertRadius);
}

void AThugAIController::ReceiveSquadAlert(FVector Location, AThugCharacter* From)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || Thug->IsAlerted() || bPacified)
	{
		return;
	}
	LastStimulusLocation = Location;
	InvestigateElapsed = 0.f;
	if (Thug->GetAlertState() == EThugAlertState::Calm)
	{
		SetState(EThugAlertState::Suspicious);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: squad alert from %s, suspicious toward %s."), *Thug->GetName(), *GetNameSafe(From),
		*Location.ToCompactString());
}

void AThugAIController::SetPacified(bool bInPacified)
{
	if (bPacified == bInPacified)
	{
		return;
	}
	bPacified = bInPacified;
	if (bPacified)
	{
		bPerceivesTarget = false;
		bSeesTarget = false;
		TargetActor = nullptr;
		bSquadAlertPending = false;
		SetState(EThugAlertState::Calm);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s."), *GetNameSafe(GetPawn()), bPacified ? TEXT("calm for a challenge, ignoring the player")
		: TEXT("challenge over, senses back"));
	ResyncPerception();
}

void AThugAIController::NotifyDamaged(AActor* By)
{
	const AThugCharacter* Thug = GetThug();
	if (!Thug)
	{
		return;
	}
	if (Burst.IsActive())
	{
		CancelBurst(*FString::Printf(TEXT("hit by %s"), *GetNameSafe(By)));
	}
	if (Thug->GetBowComponent() && Thug->GetBowComponent()->IsDrawing())
	{
		CancelArcherDraw(*FString::Printf(TEXT("hit by %s"), *GetNameSafe(By)));
	}
}

// --- Think -----------------------------------------------------------------------------------------

void AThugAIController::TickThink()
{
	Think(ThinkIntervalSeconds);
}

void AThugAIController::Think(float DeltaSeconds)
{
	if (JamRemaining > 0.f)
	{
		JamRemaining = FMath::Max(0.f, JamRemaining - DeltaSeconds);
		if (JamRemaining <= 0.f)
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: pistol unjammed."), *GetName());
			bLoggedJamHold = false;
		}
	}
	UpdateSquadAlert(DeltaSeconds);

	AThugCharacter* Thug = GetThug();
	if (!Thug || Senses(EThugSense::Stunned))
	{
		return;
	}

	if (bPacified)
	{
		// A challenge nearby: nobody to see, and nothing that happened before it still counts.
		bPerceivesTarget = false;
		TargetActor = nullptr;
		SetState(EThugAlertState::Calm);
	}
	else if (Thug->IsArcher())
	{
		UpdateArcherSight();
	}
	UpdateBlinded();
	bSeesTarget = bPerceivesTarget && !bBlinded;

	if (bSeesTarget)
	{
		SeenSeconds += DeltaSeconds;
		UnseenSeconds = 0.f;
	}
	else
	{
		SeenSeconds = 0.f;
		UnseenSeconds += DeltaSeconds;
	}

	// A sustained sighting is the only thing that promotes Suspicious to Alerted.
	if (bSeesTarget && SeenSeconds >= SightConfirmSeconds)
	{
		SetState(EThugAlertState::Alerted);
	}
	if (Thug->IsAlerted() && UnseenSeconds >= LoseTargetSeconds)
	{
		SetState(EThugAlertState::Suspicious);
	}
	if (Thug->IsGunner())
	{
		UpdateAimedAt();
	}
}

bool AThugAIController::Senses(EThugSense Sense) const
{
	const AThugCharacter* Thug = GetThug();
	const UHealthComponent* Health = Thug ? Thug->GetHealthComponent() : nullptr;
	if (!Thug)
	{
		return Sense == EThugSense::Stunned;
	}
	const float Distance = IsValid(TargetActor) ? GetToTarget().Size2D() : TNumericLimits<float>::Max();
	switch (Sense)
	{
	case EThugSense::Stunned:
		return !bThinkingEnabled || bHeld || Thug->IsIncapacitated() || Thug->IsLimp() || (Health && !Health->IsAlive());
	case EThugSense::Calm:
		return Thug->GetAlertState() == EThugAlertState::Calm;
	case EThugSense::Suspicious:
		return Thug->GetAlertState() == EThugAlertState::Suspicious;
	case EThugSense::Alerted:
		return Thug->IsAlerted();
	case EThugSense::Engaged:
		return Thug->IsAlerted() && IsValid(TargetActor) && !bBlinded;
	case EThugSense::TargetDown:
		return IsTargetDown(TargetActor);
	case EThugSense::Gunner:
		return Thug->IsGunner();
	case EThugSense::Archer:
		return Thug->IsArcher();
	case EThugSense::OutOfRange:
		return Distance > EngageRange;
	case EThugSense::Crowded:
		return Distance < (Thug->IsArcher() ? ArcherCloseRange : RetreatTriggerDistance);
	case EThugSense::RoofWanted:
		return Thug->IsArcher() && CanArcherRelocate() && (Distance < ArcherCloseRange || Distance > ArcherAggroRange);
	case EThugSense::AimedAt:
		return bAimedAt;
	case EThugSense::CoverOwed:
		return ShotsSinceCover >= ShotsBeforeCover;
	case EThugSense::CanSearchCover:
		return GetNowSeconds() >= NextCoverSearchSeconds;
	case EThugSense::HasCover:
		return bHasCover;
	case EThugSense::CoverStale:
		return CoverElapsed >= RelocateSeconds;
	case EThugSense::HoldsToken:
	{
		const UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(this);
		return Tokens && Tokens->HoldsToken(Thug);
	}
	case EThugSense::BackOffOwed:
		return SwingsSinceBackOff >= MeleeSwingsBeforeBackOff;
	}
	return false;
}

// --- Calm and Suspicious ---------------------------------------------------------------------------

bool AThugAIController::HasPatrolRoute() const
{
	const AThugCharacter* Thug = GetThug();
	return Thug && Thug->PatrolPoints.Num() > 0;
}

void AThugAIController::LookFromPatrolPoint()
{
	const AThugCharacter* Thug = GetThug();
	const AActor* Point = Thug && Thug->PatrolPoints.IsValidIndex(PatrolIndex) ? Thug->PatrolPoints[PatrolIndex].Get() : nullptr;
	if (Point && Point->ActorHasTag(HawkeyeThugBrain::PatrolFacingTag))
	{
		FaceTarget(GetPawn(), Point->GetActorForwardVector());
		LookPitch = Point->GetActorRotation().Pitch;
	}
}

void AThugAIController::NextPatrolPoint()
{
	const AThugCharacter* Thug = GetThug();
	LookPitch = 0.f;
	PatrolIndex = (PatrolIndex + 1) % FMath::Max(Thug ? Thug->PatrolPoints.Num() : 1, 1);
}

bool AThugAIController::WalkToPatrolPoint()
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !Thug->PatrolPoints.IsValidIndex(PatrolIndex))
	{
		return false;
	}

	AActor* Point = Thug->PatrolPoints[PatrolIndex];
	if (!IsValid(Point))
	{
		NextPatrolPoint();
		return false;
	}

	if (GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		return false;
	}

	if (FVector::DistSquared2D(Thug->GetActorLocation(), Point->GetActorLocation()) <= FMath::Square(120.f))
	{
		return true;
	}

	RequestMoveToActor(Point, /*AcceptanceRadius=*/60.f);
	return false;
}

bool AThugAIController::RequestMoveToActor(AActor* Goal, float AcceptanceRadius)
{
	if (!IsValid(Goal))
	{
		return false;
	}
	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem || !NavSystem->GetDefaultNavDataInstance())
	{
		const EPathFollowingRequestResult::Type Result = MoveToActor(Goal, AcceptanceRadius);
		ReportMoveResult(Result, GetNameSafe(Goal));
		return Result != EPathFollowingRequestResult::Failed;
	}
	// Following her needs her on the navmesh. Hanging off a ledge, mid-zip, on a sill or down against a
	// wall she is not, and the follow fails: he walks to the nearest spot he can stand on under her instead.
	if (MoveToActor(Goal, AcceptanceRadius) != EPathFollowingRequestResult::Failed)
	{
		return true;
	}
	return RequestMoveToLocation(Goal->GetActorLocation(), AcceptanceRadius, WideNavSnap, *GetNameSafe(Goal));
}

bool AThugAIController::RequestMoveToLocation(const FVector& Goal, float AcceptanceRadius, const FVector& SnapExtent,
	const TCHAR* GoalName)
{
	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem || !NavSystem->GetDefaultNavDataInstance())
	{
		const EPathFollowingRequestResult::Type Result = MoveToLocation(Goal, AcceptanceRadius);
		ReportMoveResult(Result, Goal.ToCompactString());
		return Result != EPathFollowingRequestResult::Failed;
	}
	// A goal just off the navmesh (where she landed a zip by a parapet, a ledge) is walked to its nearest
	// navigable point instead of failing: the mesh stops short of roof edges and walls by the agent radius.
	FNavLocation OnNav;
	if (!NavSystem->ProjectPointToNavigation(Goal, OnNav, SnapExtent))
	{
		HoldPosition(Goal, *FString::Printf(TEXT("no navmesh within %.0f cm"), SnapExtent.X), GoalName);
		return false;
	}
	if (MoveToLocation(OnNav.Location, AcceptanceRadius) == EPathFollowingRequestResult::Failed)
	{
		HoldPosition(Goal, TEXT("no path from where he stands"), GoalName);
		return false;
	}
	return true;
}

void AThugAIController::HoldPosition(const FVector& Goal, const TCHAR* Why, const TCHAR* GoalName)
{
	StopMovement();
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: holds position instead of moving to %s (%s): %s."), *GetName(),
		GoalName ? GoalName : TEXT("a point"), *Goal.ToCompactString(), Why);
}

bool AThugAIController::IsReachable(const FVector& Point) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const APawn* Me = GetPawn();
	if (!NavSystem || !NavSystem->GetDefaultNavDataInstance() || !Me)
	{
		return true;
	}
	const UNavigationPath* Path = NavSystem->FindPathToLocationSynchronously(World, Me->GetNavAgentLocation(),
		Point, const_cast<APawn*>(Me));
	return Path && Path->IsValid() && !Path->IsPartial();
}

void AThugAIController::ReportMoveResult(EPathFollowingRequestResult::Type Result, const FString& GoalDescription)
{
	if (Result != EPathFollowingRequestResult::Failed || bLoggedMoveFailure)
	{
		return;
	}
	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (NavSystem && !NavSystem->GetDefaultNavDataInstance() && GetNowSeconds() < NavigationGraceSeconds)
	{
		return;
	}
	bLoggedMoveFailure = true;

	// A world with no navigation system at all is a unit test's bare world, not a level: nothing to fix.
	if (!NavSystem)
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: cannot move to %s: there is no navigation system in this world."),
			*GetName(), *GoalDescription);
		return;
	}
	UE_LOG(LogHawkeye, Warning, TEXT("%s: cannot move to %s: no navigation data exists - the level needs a "
		"NavMeshBoundsVolume and RuntimeGeneration=Dynamic in DefaultEngine.ini."), *GetName(), *GoalDescription);
}

void AThugAIController::AdvanceInvestigation(float DeltaSeconds)
{
	InvestigateElapsed += DeltaSeconds;
	if (InvestigateElapsed >= InvestigateSeconds)
	{
		SetState(EThugAlertState::Calm);
	}
}

// --- The fight -------------------------------------------------------------------------------------

FVector AThugAIController::GetToTarget() const
{
	const APawn* Me = GetPawn();
	return Me && IsValid(TargetActor) ? TargetActor->GetActorLocation() - Me->GetActorLocation() : FVector::ZeroVector;
}

void AThugAIController::HoldStillFacingTarget()
{
	StopMovement();
	FaceTarget(GetPawn(), GetToTarget());
}

void AThugAIController::AimAtTarget()
{
	// The weapon traces along the control rotation, so the aim always points at her. The body does not follow
	// it (see AThugCharacter's movement setup): it faces where it is walking, and squares up once he has stopped.
	if (!Senses(EThugSense::Engaged) || IsTargetDown(TargetActor))
	{
		return;
	}
	FRotator FacingRotation = GetToTarget().Rotation();
	FacingRotation.Roll = 0.f;
	SetControlRotation(FacingRotation);
}

void AThugAIController::StandDown(const TCHAR* Why)
{
	CancelBurst(Why);
	CancelArcherDraw(Why);
	ReleaseAttackToken();
}

// --- Gunner ----------------------------------------------------------------------------------------

void AThugAIController::CancelBurst(const TCHAR* Why)
{
	AThugCharacter* Thug = GetThug();
	if (Burst.IsActive())
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: burst broken (%s) after %d shot(s)."), *GetNameSafe(Thug), Why,
			Burst.GetShotsFired());
		ReleaseAttackToken();
	}
	Burst.Cancel();
	bDeflectNextShot = false;
	if (Thug)
	{
		Thug->SetTelegraphGlint(false);
		if (Thug->IsGunner())
		{
			Thug->SetWeaponRaised(false, FVector::ZeroVector);
		}
	}
}

bool AThugAIController::DeflectFirstShot()
{
	if (!Burst.IsTelegraphing())
	{
		return false;
	}
	bDeflectNextShot = true;
	UE_LOG(LogHawkeye, Log, TEXT("%s: raised pistol parried; the first shot will be deflected."), *GetNameSafe(GetPawn()));
	return true;
}

void AThugAIController::TickGunnerClocks(float DeltaSeconds)
{
	BurstPauseRemaining = FMath::Max(0.f, BurstPauseRemaining - DeltaSeconds);
	if (Burst.IsActive())
	{
		RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
	}
}

void AThugAIController::HoldFireJammed()
{
	AThugCharacter* Thug = GetThug();
	if (!Thug)
	{
		return;
	}
	if (!bLoggedJamHold)
	{
		bLoggedJamHold = true;
		UE_LOG(LogHawkeye, Log, TEXT("%s: jammed, holds fire (%.1f s left)."), *Thug->GetName(), JamRemaining);
	}
	Thug->SetWeaponRaised(false, GetAimPointOn(TargetActor));
}

bool AThugAIController::StartBurst()
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		return false;
	}
	// His turn on her ranged budget (gameplay-semantics.md, "Attack tokens"); without it he holds his aim.
	const FHawkeyeTokenGrant Token = RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
	if (!Token.bGranted)
	{
		return false;
	}
	Burst.Start(Token.MinTelegraphSeconds);
	Thug->SetWeaponRaised(true, GetAimPointOn(TargetActor));
	Thug->SetTelegraphGlint(true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: raises his pistol at %s from %.0f cm: %.1f s telegraph, then %d shots."),
		*Thug->GetName(), *GetNameSafe(TargetActor), GetToTarget().Size(), Burst.GetTelegraphSeconds(), Burst.ShotsPerBurst);
	return true;
}

bool AThugAIController::AdvanceBurst(float DeltaSeconds)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		return false;
	}
	const FVector Chest = GetAimPointOn(TargetActor);
	Thug->SetWeaponRaised(true, Chest);
	const int32 Shots = Burst.Advance(DeltaSeconds);
	if (!Burst.IsTelegraphing())
	{
		Thug->SetTelegraphGlint(false);
	}
	for (int32 Shot = 0; Shot < Shots; ++Shot)
	{
		FireAtTarget();
		++ShotsSinceCover;
	}
	if (Burst.IsActive())
	{
		return false;
	}
	ReleaseAttackToken();
	Thug->SetWeaponRaised(false, Chest);
	BurstPauseRemaining = BurstPauseSeconds;
	return true;
}

bool AThugAIController::BeginCover(const TCHAR* Why, const FVector* Avoid)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		return false;
	}
	// Searching is dozens of traces; a failed search waits a second before the next.
	const double Now = GetNowSeconds();
	if (Now < NextCoverSearchSeconds)
	{
		return false;
	}
	FVector Point;
	if (!FindCoverPoint(TargetActor->GetActorLocation(), Point, Avoid))
	{
		NextCoverSearchSeconds = Now + 1.0;
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: no cover within %.0f cm (%s); stays out."), *Thug->GetName(),
			CoverSearchRadius, Why);
		return false;
	}
	CancelBurst(Why);
	bHasCover = true;
	CoverPoint = Point;
	CoverElapsed = 0.f;
	ShotsSinceCover = 0;
	RequestMoveToLocation(CoverPoint, 40.f, WideNavSnap, TEXT("cover"));
	UE_LOG(LogHawkeye, Log, TEXT("%s: takes cover (%s) at %s, %.0f cm away, out of %s's line."), *Thug->GetName(), Why,
		*CoverPoint.ToCompactString(), FVector::Dist2D(CoverPoint, Thug->GetActorLocation()), *GetNameSafe(TargetActor));
	return true;
}

void AThugAIController::HoldCover(float DeltaSeconds)
{
	CoverElapsed += DeltaSeconds;
	if (AThugCharacter* Thug = GetThug())
	{
		Thug->SetWeaponRaised(false, FVector::ZeroVector);
	}
}

FVector AThugAIController::FindPeekPoint() const
{
	UWorld* World = GetWorld();
	const APawn* Me = GetPawn();
	if (!World || !Me || !IsValid(TargetActor))
	{
		return CoverPoint;
	}
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
	const FVector Chest = GetAimPointOn(TargetActor);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugPeek), false, Me);
	Params.AddIgnoredActor(TargetActor);
	for (const float Radius : { 100.f, 180.f, 260.f })
	{
		for (int32 Step = 0; Step < 8; ++Step)
		{
			FVector Candidate = CoverPoint + FRotator(0.f, Step * 45.f, 0.f).Vector() * Radius;
			FNavLocation OnNav;
			if (Nav && Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(60.f, 60.f, 200.f)))
			{
				Candidate = OnNav.Location + FVector(0.f, 0.f, 90.f);
			}
			if (!World->LineTraceTestByChannel(Candidate + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight), Chest,
					ECC_Visibility, Params))
			{
				return Candidate;
			}
		}
	}
	return CoverPoint;
}

bool AThugAIController::BeginBackAway()
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		return false;
	}
	const FVector Player = TargetActor->GetActorLocation();
	FVector Goal;
	if (!ComputeRetreatGoal(Thug->GetActorLocation(), Player, RetreatTriggerDistance, RetreatToDistance, Goal))
	{
		return false;
	}
	// Somewhere he can stand: on a roof the straight line may run off the edge.
	FNavLocation OnNav;
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (Nav && Nav->ProjectPointToNavigation(Goal, OnNav, FVector(200.f, 200.f, 250.f)))
	{
		Goal = OnNav.Location;
	}
	CancelBurst(TEXT("backing off"));
	bHasCover = false;
	RetreatGoal = Goal;
	RequestMoveToLocation(RetreatGoal, 40.f);
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s closed to %.0f cm; backs off to %.0f cm (%s)."), *Thug->GetName(),
		*GetNameSafe(TargetActor), FVector::Dist2D(Player, Thug->GetActorLocation()), RetreatToDistance,
		*RetreatGoal.ToCompactString());
	return true;
}

bool AThugAIController::FinishBackAway(float Elapsed)
{
	const AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		return true;
	}
	const float Distance = FVector::Dist2D(TargetActor->GetActorLocation(), Thug->GetActorLocation());
	const bool bArrived = FVector::Dist2D(Thug->GetActorLocation(), RetreatGoal) <= ArriveDistance;
	if (Distance < RetreatToDistance - 50.f && !bArrived && Elapsed < RetreatMaxSeconds)
	{
		return false;
	}
	StopMovement();
	UE_LOG(LogHawkeye, Log, TEXT("%s: done backing off, %.0f cm from %s."), *Thug->GetName(), Distance,
		*GetNameSafe(TargetActor));
	return true;
}

bool AThugAIController::FindCoverPoint(const FVector& Threat, FVector& OutPoint, const FVector* Avoid) const
{
	if (FindCoverPointEqs(OutPoint, Avoid))
	{
		return true;
	}
	return FindCoverPointRing(Threat, OutPoint, Avoid);
}

bool AThugAIController::FindCoverPointEqs(FVector& OutPoint, const FVector* Avoid) const
{
	AThugCharacter* Thug = GetThug();
	UWorld* World = GetWorld();
	UEnvQueryManager* Manager = World ? UEnvQueryManager::GetCurrent(World) : nullptr;
	if (!Thug || !Thug->CoverQuery || !Manager || !IsValid(TargetActor))
	{
		return false;
	}
	// EQS_CoverPoints: a donut around him, filtered by a trace from the target (ThugTarget context),
	// scored nearest first. All matching, so the cover he is leaving can be skipped.
	FEnvQueryRequest Request(Thug->CoverQuery, Thug);
	const TSharedPtr<FEnvQueryResult> Result = Manager->RunInstantQuery(Request, EEnvQueryRunMode::AllMatching);
	if (!Result.IsValid() || !Result->IsSuccessful())
	{
		return false;
	}
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
	for (int32 Index = 0; Index < Result->Items.Num(); ++Index)
	{
		FVector Point = Result->GetItemAsLocation(Index) + FVector(0.f, 0.f, 90.f);
		// The donut is flat round him: over a gallery's rail its points hang in the air. Only floor counts.
		FNavLocation OnNav;
		if (Nav && Nav->GetDefaultNavDataInstance())
		{
			if (!Nav->ProjectPointToNavigation(Result->GetItemAsLocation(Index), OnNav, FVector(60.f, 60.f, 150.f)))
			{
				continue;
			}
			Point = OnNav.Location + FVector(0.f, 0.f, 90.f);
		}
		if ((Avoid && FVector::Dist2D(Point, *Avoid) < HawkeyeThugBrain::SameCoverDistance)
			|| FMath::Abs(Point.Z - Thug->GetActorLocation().Z) > HawkeyeThugBrain::CoverMaxStep)
		{
			continue;
		}
		// On the navmesh is not enough: behind a parapet on the next roof, past a locked door, he cannot get there.
		if (!IsReachable(Point))
		{
			continue;
		}
		OutPoint = Point;
		return true;
	}
	return false;
}

bool AThugAIController::FindCoverPointRing(const FVector& Threat, FVector& OutPoint, const FVector* Avoid) const
{
	const APawn* Me = GetPawn();
	UWorld* World = GetWorld();
	if (!Me || !World)
	{
		return false;
	}
	// The partner's EQS-shaped search, without an asset: rings of candidates, kept where the
	// shooter's line is blocked and the spot itself is open.
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugCover), false, Me);
	if (IsValid(TargetActor))
	{
		Params.AddIgnoredActor(TargetActor);
	}
	const FVector Eye = Threat + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight);
	const float Radius = Me->GetSimpleCollisionRadius() * 0.8f;
	for (const float Ring : { CoverSearchRadius * 0.4f, CoverSearchRadius * 0.7f, CoverSearchRadius })
	{
		float BestDistance = TNumericLimits<float>::Max();
		bool bFound = false;
		for (int32 Step = 0; Step < 12; ++Step)
		{
			FVector Candidate = Me->GetActorLocation() + FRotator(0.f, Step * 30.f, 0.f).Vector() * Ring;
			FNavLocation OnNav;
			if (Nav && Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(100.f, 100.f, 250.f)))
			{
				Candidate = OnNav.Location + FVector(0.f, 0.f, 90.f);
			}
			else if (Nav && Nav->GetDefaultNavDataInstance())
			{
				// Nowhere to stand: over a gallery's rail, in the air above a hall.
				continue;
			}
			if ((Avoid && FVector::Dist2D(Candidate, *Avoid) < HawkeyeThugBrain::SameCoverDistance)
				|| FMath::Abs(Candidate.Z - Me->GetActorLocation().Z) > HawkeyeThugBrain::CoverMaxStep)
			{
				continue;
			}
			// Inside a wall is not cover.
			if (World->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Visibility,
					FCollisionShape::MakeSphere(Radius), Params))
			{
				continue;
			}
			const float Distance = FVector::Dist2D(Candidate, Me->GetActorLocation());
			if (Distance >= BestDistance)
			{
				continue;
			}
			const FVector Side = FVector::CrossProduct((Candidate - Eye).GetSafeNormal2D(), FVector::UpVector) * Radius;
			if (!World->LineTraceTestByChannel(Eye, Candidate, ECC_Visibility, Params)
				|| !World->LineTraceTestByChannel(Eye, Candidate + Side, ECC_Visibility, Params)
				|| !World->LineTraceTestByChannel(Eye, Candidate - Side, ECC_Visibility, Params)
				|| !IsReachable(Candidate))
			{
				continue;
			}
			BestDistance = Distance;
			OutPoint = Candidate;
			bFound = true;
		}
		if (bFound)
		{
			return true;
		}
	}
	return false;
}

// --- Archer ----------------------------------------------------------------------------------------

void AThugAIController::CancelArcherDraw(const TCHAR* Why)
{
	AThugCharacter* Thug = GetThug();
	UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	if (!Bow || !Thug->IsArcher())
	{
		return;
	}
	ArcherHold.EndHold();
	if (Bow->IsDrawing())
	{
		Bow->CancelDraw();
		ReleaseAttackToken();
		UE_LOG(LogHawkeye, Log, TEXT("%s: draw broken (%s)."), *Thug->GetName(), Why);
		// A broken draw still costs him the cooldown before the next.
		LastArrowSeconds = GetNowSeconds();
		RollArcherGap();
	}
	Thug->SetTelegraphGlint(false);
}

bool AThugAIController::IsDrawingBow() const
{
	const AThugCharacter* Thug = GetThug();
	const UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	return Bow && Bow->IsDrawing();
}

bool AThugAIController::IsDrawFull() const
{
	const AThugCharacter* Thug = GetThug();
	const UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	const UBowDefinition* BowDef = Bow ? Bow->GetBow() : nullptr;
	return BowDef && Bow->IsDrawing() && Bow->GetDrawElapsed() + KINDA_SMALL_NUMBER >= BowDef->FullDrawSeconds;
}

bool AThugAIController::StepBackFromTarget()
{
	const AThugCharacter* Thug = GetThug();
	const float Distance = GetToTarget().Size2D();
	if (!Thug || IsDrawingBow() || GetMoveStatus() == EPathFollowingStatus::Moving
		|| ChooseArcherRangeAction(Distance, ArcherCloseRange, ArcherMinRange, ArcherMaxRange) != EArcherRangeAction::StepBack)
	{
		return false;
	}
	const FVector Away = -GetToTarget().GetSafeNormal2D();
	FNavLocation Spot;
	const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
	const float Step = FMath::Min(ArcherMinRange - Distance + 100.f, 400.f);
	if (!Nav || !Nav->ProjectPointToNavigation(Thug->GetActorLocation() + Away * Step, Spot, FVector(50.f, 50.f, 150.f))
		|| FVector::Dist2D(Spot.Location, Thug->GetActorLocation()) <= 100.f)
	{
		return false;
	}
	RequestMoveToLocation(Spot.Location, 30.f);
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: steps back %.0f cm to keep %.0f cm."), *Thug->GetName(), Step, ArcherMinRange);
	return true;
}

FVector AThugAIController::AimBow()
{
	const AThugCharacter* Thug = GetThug();
	UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	const UBowDefinition* BowDef = Bow ? Bow->GetBow() : nullptr;
	if (!BowDef || !IsValid(TargetActor))
	{
		return FVector::ZeroVector;
	}
	// Lead on her velocity, at the chest, allowing for the drop.
	const UWorld* World = GetWorld();
	const FVector Aim = ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), GetAimPointOn(TargetActor), TargetActor->GetVelocity(),
		BowDef->MaxSpeed, World ? World->GetGravityZ() : -980.f);
	FRotator AimRotation = (Aim - Thug->GetPawnViewLocation()).Rotation();
	AimRotation.Roll = 0.f;
	SetControlRotation(AimRotation);
	Bow->SetAimOverride(Aim);
	return Aim;
}

bool AThugAIController::HasLineToTarget() const
{
	return IsValid(TargetActor) && HasLineTo(GetAimPointOn(TargetActor), TargetActor);
}

bool AThugAIController::IsArcherGapOver() const
{
	return GetNowSeconds() - LastArrowSeconds >= NextArcherGapSeconds;
}

bool AThugAIController::StartArcherDraw()
{
	AThugCharacter* Thug = GetThug();
	UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	const UBowDefinition* BowDef = Bow ? Bow->GetBow() : nullptr;
	if (!BowDef)
	{
		return false;
	}
	const FHawkeyeTokenGrant Token = RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
	if (!Token.bGranted)
	{
		return false;
	}
	// The difficulty sets how long the draw takes (from off her screen, at least the token's minimum); the
	// bow's own numbers stay as they are.
	const float DrawSeconds = FMath::Max(UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ArcherDrawSeconds),
		Token.MinTelegraphSeconds);
	Bow->DrawRate = ComputeArcherDrawRate(BowDef->FullDrawSeconds, DrawSeconds);
	if (!Bow->StartDraw())
	{
		ReleaseAttackToken();
		return false;
	}
	Thug->SetTelegraphGlint(true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: the bowstring creaks: drawing on %s at %.0f cm (%.1f s)."), *Thug->GetName(),
		*GetNameSafe(TargetActor), GetToTarget().Size(), DrawSeconds);
	return true;
}

void AThugAIController::LooseAtTarget()
{
	const AThugCharacter* Thug = GetThug();
	UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	const UBowDefinition* BowDef = Bow ? Bow->GetBow() : nullptr;
	if (!BowDef || !IsValid(TargetActor))
	{
		return;
	}
	// The shot itself: where he saw her a lag ago, led on, scattered by how hard she is to follow.
	const FVector Chest = GetAimPointOn(TargetActor);
	const FHawkeyeRangedAim Shot = ComputeRangedAim(Bow->GetArrowSpawnLocation(), BowDef->MaxSpeed, ArcherAimConeDegrees);
	LooseArcherDraw(Shot.AimPoint, *FString::Printf(TEXT("%s, %.0f cm off her chest, cone %.1f deg, her speed %.0f cm/s"),
		Shot.bForcedMiss ? TEXT("wide on purpose") : TEXT("leading"), FVector::Dist(Shot.AimPoint, Chest), Shot.ConeDegrees,
		TargetActor->GetVelocity().Size()));
}

void AThugAIController::BeginArcherHold(const FVector& AimPoint)
{
	const AThugCharacter* Thug = GetThug();
	const UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	if (!Bow || !Bow->IsDrawing() || ArcherHold.IsHolding())
	{
		return;
	}
	HeldAimPoint = AimPoint;
	ArcherHold.BeginHold();
	StartArcherHoldTimer();
	UE_LOG(LogHawkeye, Log, TEXT("%s: lost his line %.2f s into the draw; holds it up to %.1f s on where %s went."),
		*Thug->GetName(), Bow->GetDrawElapsed(), ArcherHold.MaxHoldSeconds, *GetNameSafe(TargetActor));
}

void AThugAIController::RollArcherGap()
{
	NextArcherGapSeconds = ArcherShotCooldownSeconds >= 0.f
		? ArcherShotCooldownSeconds
		: AimStream.FRandRange(UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ArcherShotGapMinSeconds),
			UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ArcherShotGapMaxSeconds));
}

void AThugAIController::LooseArcherDraw(const FVector& AimPoint, const TCHAR* Why)
{
	AThugCharacter* Thug = GetThug();
	UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	if (!Bow)
	{
		return;
	}
	Thug->SetTelegraphGlint(false);
	Bow->SetAimOverride(AimPoint);
	FRotator AimRotation = (AimPoint - Thug->GetPawnViewLocation()).Rotation();
	AimRotation.Roll = 0.f;
	SetControlRotation(AimRotation);
	const bool bLoosed = Bow->ReleaseDraw();
	ReleaseAttackToken();
	if (!bLoosed)
	{
		return;
	}
	LastArrowSeconds = GetNowSeconds();
	RollArcherGap();
	ArcherHold.BeginLoose();
	StartArcherHoldTimer();
	UE_LOG(LogHawkeye, Log, TEXT("%s: looses at %s (%s); %.1f s loose window."), *Thug->GetName(), *GetNameSafe(TargetActor),
		Why, ArcherHold.LooseSeconds);
}

void AThugAIController::StartArcherHoldTimer()
{
	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(ArcherHoldTimerHandle))
	{
		return;
	}
	World->GetTimerManager().SetTimer(ArcherHoldTimerHandle, this, &AThugAIController::TickArcherHoldTimer,
		ArcherHoldTickSeconds, true);
}

void AThugAIController::TickArcherHoldTimer()
{
	TickArcherHold(ArcherHoldTickSeconds);
	if (!ArcherHold.IsHolding() && !ArcherHold.IsLoose())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ArcherHoldTimerHandle);
		}
	}
}

void AThugAIController::TickArcherHold(float DeltaSeconds)
{
	ArcherHold.AdvanceLoose(DeltaSeconds);
	if (!ArcherHold.IsHolding())
	{
		return;
	}
	AThugCharacter* Thug = GetThug();
	UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	const UBowDefinition* BowDef = Bow ? Bow->GetBow() : nullptr;
	if (!BowDef || !Bow->IsDrawing() || !IsValid(TargetActor) || bBlinded || Senses(EThugSense::Stunned))
	{
		CancelArcherDraw(TEXT("the hold ended"));
		return;
	}
	const FVector Chest = GetAimPointOn(TargetActor);
	const FVector From = Bow->GetArrowSpawnLocation();
	const bool bLine = !IsHiddenInSmoke(TargetActor) && HasLineTo(Chest, TargetActor);
	const bool bInCone = bLine && IsAimedAtPoint(From, HeldAimPoint - From, Chest, ArcherHoldConeDegrees);
	const bool bFull = Bow->GetDrawElapsed() + KINDA_SMALL_NUMBER >= BowDef->FullDrawSeconds;
	const float Held = ArcherHold.GetHoldElapsed() + DeltaSeconds;
	switch (ArcherHold.Advance(DeltaSeconds, bInCone, bFull))
	{
	case EArcherHoldStep::Fire:
	{
		const FHawkeyeRangedAim Shot = ComputeRangedAim(From, BowDef->MaxSpeed, ArcherAimConeDegrees);
		LooseArcherDraw(Shot.AimPoint, *FString::Printf(TEXT("she showed again after a %.2f s hold%s"), Held,
			Shot.bForcedMiss ? TEXT(", wide on purpose") : TEXT("")));
		break;
	}
	case EArcherHoldStep::Relax:
		CancelArcherDraw(*FString::Printf(TEXT("held %.1f s with no line, relaxes"), Held));
		break;
	default:
		break;
	}
}

bool AThugAIController::CanArcherRelocate() const
{
	return GetNowSeconds() - LastRelocateSeconds >= ArcherRelocateCooldownSeconds && !ArcherHold.IsHolding()
		&& !ArcherHold.IsLoose();
}

bool AThugAIController::FindArcherRoof(FVector& OutLanding)
{
	AThugCharacter* Thug = GetThug();
	UWorld* World = GetWorld();
	if (!Thug || !World || !IsValid(TargetActor))
	{
		return false;
	}
	const FVector Player = TargetActor->GetActorLocation();
	const FVector Chest = GetAimPointOn(TargetActor);
	const TCHAR* Why = FVector::Dist2D(Player, Thug->GetActorLocation()) < ArcherCloseRange ? TEXT("she closed in") : TEXT("out of range");
	const float HalfHeight = Thug->GetCapsuleComponent() ? Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;
	AGrappleAnchor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	const float Middle = (ArcherMinRange + ArcherMaxRange) * 0.5f;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		const FVector Landing = It->GetLandingLocation() + FVector(0.f, 0.f, HalfHeight + 2.f);
		const float ToPlayer = FVector::Dist2D(Landing, Player);
		const float FromMe = FVector::Dist(Landing, Thug->GetActorLocation());
		if (ToPlayer < ArcherMinRange || ToPlayer > ArcherMaxRange || FromMe < 600.f || FromMe > 4000.f)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ArcherAnchor), false, Thug);
		Params.AddIgnoredActor(TargetActor);
		Params.AddIgnoredActor(*It);
		if (World->LineTraceTestByChannel(Landing + FVector(0.f, 0.f, HawkeyeThugBrain::EyeHeight), Chest, ECC_Visibility, Params))
		{
			continue;
		}
		// Near the middle of the band, and not far to go.
		const float Score = FMath::Abs(ToPlayer - Middle) + FromMe * 0.25f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = *It;
			OutLanding = Landing;
		}
	}
	LastRelocateSeconds = GetNowSeconds();
	if (!Best)
	{
		if (!bLoggedNoAnchor)
		{
			bLoggedNoAnchor = true;
			UE_LOG(LogHawkeye, Log, TEXT("%s: wants to relocate (%s) but no anchor lands %.0f to %.0f cm from %s with a line."),
				*Thug->GetName(), Why, ArcherMinRange, ArcherMaxRange, *GetNameSafe(TargetActor));
		}
		return false;
	}
	ZipLanding = OutLanding;
	// Teleport-zip: a straight kinematic line, no grapple component on a thug. Logged as such.
	UE_LOG(LogHawkeye, Log, TEXT("%s: relocates (%s): teleport-zip to anchor %s, %.0f cm, landing %.0f cm from %s."),
		*Thug->GetName(), Why, *Best->GetName(), FVector::Dist(Thug->GetActorLocation(), OutLanding),
		FVector::Dist2D(OutLanding, Player), *GetNameSafe(TargetActor));
	return true;
}

void AThugAIController::BeginZip()
{
	AThugCharacter* Thug = GetThug();
	CancelArcherDraw(TEXT("relocating"));
	StopMovement();
	UCharacterMovementComponent* Movement = Thug ? Thug->GetCharacterMovement() : nullptr;
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Flying);
	}
}

void AThugAIController::EndZip()
{
	AThugCharacter* Thug = GetThug();
	UCharacterMovementComponent* Movement = Thug ? Thug->GetCharacterMovement() : nullptr;
	if (!Movement || Movement->MovementMode != MOVE_Flying)
	{
		return;
	}
	Movement->SetMovementMode(MOVE_Walking);
	UE_LOG(LogHawkeye, Log, TEXT("%s: landed at %s."), *Thug->GetName(), *Thug->GetActorLocation().ToCompactString());
}

// --- Melee -----------------------------------------------------------------------------------------

void AThugAIController::TickMeleeCooldown(float DeltaSeconds)
{
	MeleeCooldownRemaining = FMath::Max(0.f, MeleeCooldownRemaining - DeltaSeconds);
}

bool AThugAIController::IsReadyToSwing(float Distance) const
{
	// Off the ring or in his cooldown he is not ready, so never granted a token (gameplay-semantics.md, "Attack tokens").
	const AThugCharacter* Thug = GetThug();
	const UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
	return Melee && !Melee->IsAttacking() && MeleeCooldownRemaining <= 0.f
		&& Distance <= MeleeWaitRingDistance + HawkeyeThugBrain::RingReadySlack;
}

void AThugAIController::WaitOnRing(float DeltaSeconds, float& RepathRemaining)
{
	const UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(this);
	const float Slot = Tokens ? Tokens->GetRingSlotYaw(GetPawn(), TargetActor, MeleeRingSeparationDegrees)
		: (-GetToTarget()).Rotation().Yaw;
	CircleTarget(DeltaSeconds, MeleeWaitRingDistance, Slot, RepathRemaining, TEXT("his place on the ring"));
}

bool AThugAIController::StartSwing(const FHawkeyeTokenGrant& Token, float Distance)
{
	AThugCharacter* Thug = GetThug();
	UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
	if (!Melee)
	{
		return false;
	}
	// Granted from off her screen, the wind-up is never shorter than the warning it has to give.
	FHawkeyeMeleeAttack Attack = ScaleAttackForDifficulty(Thug->GetMeleeAttack(SwingsStarted));
	Attack.WindupSeconds = FMath::Max(Attack.WindupSeconds, Token.MinTelegraphSeconds);
	UE_LOG(LogHawkeye, Log, TEXT("%s: telegraphs a %s swing at %s from %.0f cm (%.1f s wind-up)."), *Thug->GetName(),
		*Attack.Name.ToString(), *GetNameSafe(TargetActor), Distance, Attack.WindupSeconds);
	const bool bStarted = Melee->StartAttack(Attack);
	SwingsStarted += bStarted ? 1 : 0;
	return bStarted;
}

FHawkeyeTokenGrant AThugAIController::RequestAttackToken(EHawkeyeAttackKind Kind, bool bReady)
{
	UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(this);
	return Tokens ? Tokens->RequestToken(GetPawn(), TargetActor, Kind, bReady) : FHawkeyeTokenGrant();
}

void AThugAIController::ReleaseAttackToken()
{
	if (UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(this))
	{
		Tokens->ReleaseToken(GetPawn());
	}
}

bool AThugAIController::IsTargetDown(const AActor* Target)
{
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Target);
	const UHealthComponent* Health = Hawkeye ? Hawkeye->GetHealthComponent() : nullptr;
	return Hawkeye && (Hawkeye->IsDowned() || (Health && !Health->IsAlive()));
}

void AThugAIController::CircleTarget(float DeltaSeconds, float Radius, float SlotYaw, float& RepathRemaining,
	const TCHAR* GoalName)
{
	APawn* Me = GetPawn();
	if (!Me || !IsValid(TargetActor))
	{
		return;
	}
	if (GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		FaceTarget(Me, GetToTarget());
	}
	RepathRemaining -= DeltaSeconds;
	const FVector Spot = TargetActor->GetActorLocation() + FRotator(0.f, SlotYaw, 0.f).Vector() * Radius;
	if (RepathRemaining > 0.f || FVector::Dist2D(Me->GetActorLocation(), Spot) <= ArriveDistance)
	{
		return;
	}
	RepathRemaining = HawkeyeThugBrain::RingRepathSeconds;
	// Round her against a wall or a parapet some of the ring is inside it: the nearest floor within 3 m.
	RequestMoveToLocation(Spot, 30.f, WideNavSnap, GoalName);
}

void AThugAIController::FinishSwing()
{
	ReleaseAttackToken();
	MeleeCooldownRemaining = MeleeCooldownSeconds;
	++SwingsSinceBackOff;
}

bool AThugAIController::StartBackOff()
{
	const AThugCharacter* Thug = GetThug();
	if (!Thug)
	{
		return false;
	}
	// Only somewhere he can stand: a step back on a roof may be into the parapet. Without a spot
	// he simply comes again after the cooldown.
	const FVector Away = -GetToTarget().GetSafeNormal2D();
	FNavLocation Spot;
	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem || !NavSystem->ProjectPointToNavigation(
			Thug->GetActorLocation() + Away * MeleeBackOffDistance, Spot, FVector(50.f, 50.f, 150.f)))
	{
		EndBackOff();
		return false;
	}
	RequestMoveToLocation(Spot.Location, /*AcceptanceRadius=*/30.f);
	UE_LOG(LogHawkeye, Log, TEXT("%s: backs off %.0f cm after %d swings."), *Thug->GetName(),
		MeleeBackOffDistance, SwingsSinceBackOff);
	return true;
}

void AThugAIController::EndBackOff()
{
	SwingsSinceBackOff = 0;
}

void AThugAIController::FaceTarget(APawn* Thug, const FVector& ToTarget)
{
	// Standing still, bOrientRotationToMovement has no direction to work from, so the turn is
	// made by hand. Yaw only: a thug does not lean over to shoot down at you.
	const FVector Flat = ToTarget.GetSafeNormal2D();
	if (!Thug || Flat.IsNearlyZero())
	{
		return;
	}

	Thug->SetActorRotation(FRotator(0.f, Flat.Rotation().Yaw, 0.f));
}

void AThugAIController::FireAtTarget()
{
	AThugCharacter* Thug = GetThug();
	UWeaponComponent* Weapon = Thug ? Thug->GetWeaponComponent() : nullptr;
	if (!Weapon || !IsValid(TargetActor))
	{
		return;
	}

	if (IsJammed())
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: pistol jammed (%.1f s left), no shot."), *GetName(), JamRemaining);
		return;
	}

	if (bDeflectNextShot)
	{
		bDeflectNextShot = false;
		++DeflectedShots;
		UE_LOG(LogHawkeye, Log, TEXT("%s: first shot of the burst deflected by %s's parry."), *GetNameSafe(Thug),
			*GetNameSafe(TargetActor));
		return;
	}

	if (Weapon->CurrentAmmo <= 0)
	{
		Weapon->Reload();
		return;
	}

	// Where he saw her a lag ago, scattered by how hard she is to follow (or wide after a jink); the weapon
	// traces the control rotation.
	const FVector View = Thug->GetPawnViewLocation();
	const FHawkeyeRangedAim Shot = ComputeRangedAim(View, /*ProjectileSpeed=*/0.f, AimSpreadDegrees);
	const FVector Scattered = (Shot.AimPoint - View).GetSafeNormal();

	FRotator AimRotation = Scattered.Rotation();
	AimRotation.Roll = 0.f;
	SetControlRotation(AimRotation);

	// The difficulty scales the shot, not the weapon: the pistol keeps its own number.
	const float BaseDamage = Weapon->Damage;
	Weapon->Damage = BaseDamage * UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ThugDamage);
	Weapon->Fire();
	Weapon->Damage = BaseDamage;
}

FHawkeyeMeleeAttack AThugAIController::ScaleAttackForDifficulty(const FHawkeyeMeleeAttack& Attack) const
{
	FHawkeyeMeleeAttack Scaled = Attack;
	Scaled.Damage *= UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ThugDamage);
	return Scaled;
}

// --- Trick arrow states ---------------------------------------------------------------------------

void AThugAIController::SetHeld(bool bInHeld)
{
	if (bHeld == bInHeld)
	{
		return;
	}
	bHeld = bInHeld;
	if (bHeld)
	{
		StopMovement();
		CancelBurst(TEXT("held by putty"));
		CancelArcherDraw(TEXT("held by putty"));
		ReleaseAttackToken();
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s."), *GetName(),
		bHeld ? TEXT("held by putty, AI paused") : TEXT("putty released, AI resumes"));
}

void AThugAIController::Jam(float Seconds)
{
	if (Seconds <= JamRemaining)
	{
		return;
	}
	const bool bWasJammed = IsJammed();
	JamRemaining = Seconds;
	if (!bWasJammed)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: pistol jammed for %.1f s."), *GetName(), Seconds);
		CancelBurst(TEXT("jammed"));
	}
}

bool AThugAIController::IsHiddenInSmoke(const AActor* Actor)
{
	const ACharacter* Character = Cast<ACharacter>(Actor);
	const UArrowEffectsSubsystem* Effects = Character ? UArrowEffectsSubsystem::Get(Character) : nullptr;
	return Effects && Character->bIsCrouched && Effects->IsInsideSmoke(Character->GetActorLocation());
}

void AThugAIController::UpdateBlinded()
{
	const APawn* Me = GetPawn();
	const UArrowEffectsSubsystem* Effects = UArrowEffectsSubsystem::Get(this);
	bool bNowBlinded = false;
	if (Me && Effects && Effects->GetSmokeCount() > 0)
	{
		const FVector Eye = Me->GetPawnViewLocation();
		bNowBlinded = Effects->IsInsideSmoke(Eye);
		if (!bNowBlinded && IsValid(TargetActor))
		{
			bNowBlinded = IsHiddenInSmoke(TargetActor)
				|| Effects->IsSightBlocked(Eye, TargetActor->GetActorLocation());
		}
		else if (!bNowBlinded && bHasSightLocation)
		{
			bNowBlinded = Effects->IsSightBlocked(Eye, LastSightLocation);
		}
	}
	if (bNowBlinded != bBlinded)
	{
		bBlinded = bNowBlinded;
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s."), *GetName(),
			bBlinded ? TEXT("blinded by smoke") : TEXT("can see again"));
	}
}
