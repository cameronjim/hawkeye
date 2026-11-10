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
#include "Components/StateTreeAIComponent.h"
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
#include "Settings/DifficultySubsystem.h"

namespace HawkeyeThugBrain
{
	/** Eye height above the capsule centre for line checks, cm. */
	static constexpr float EyeHeight = 60.f;
	/** A cover point this close to the one he is leaving is the same cover, cm. */
	static constexpr float SameCoverDistance = 200.f;
	/** Close enough to a move goal to count as there, cm. */
	static constexpr float ArriveDistance = 90.f;
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

	StateTreeComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeComponent"));
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

	if (Thug && Thug->ThugStateTree && StateTreeComponent)
	{
		StateTreeComponent->SetStateTree(Thug->ThugStateTree);
		StateTreeComponent->StartLogic();
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: driving %s (%s, cover %s)."), *GetName(), *GetNameSafe(InPawn),
		IsUsingStateTree() ? *FString::Printf(TEXT("StateTree %s"), *GetNameSafe(Thug ? Thug->ThugStateTree : nullptr))
			: TEXT("C++ mode selection, no StateTree"),
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

bool AThugAIController::IsUsingStateTree() const
{
	return StateTreeComponent && StateTreeComponent->IsRunning();
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
	bPatrolWaiting = false;
	PatrolWaitElapsed = 0.f;
	LookPitch = 0.f;

	bBackingOff = false;
	SwingsSinceBackOff = 0;
	ReleaseAttackToken();

	CancelBurst(TEXT("alert state changed"));
	GunnerPhase = EGunnerPhase::Open;
	bHasCover = false;
	ShotsSinceCover = 0;
	BurstPauseRemaining = 0.f;
	bRetreating = false;

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
	if (!Thug || HasNeed(EThugMode::Stunned))
	{
		if (!IsUsingStateTree())
		{
			RunMode(EThugMode::Stunned, DeltaSeconds);
		}
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

	if (!IsUsingStateTree())
	{
		RunMode(ChooseMode(), DeltaSeconds);
	}
}

bool AThugAIController::HasNeed(EThugMode Mode) const
{
	const AThugCharacter* Thug = GetThug();
	const UHealthComponent* Health = Thug ? Thug->GetHealthComponent() : nullptr;
	const bool bStunned = !Thug || !bThinkingEnabled || bHeld || Thug->IsIncapacitated() || Thug->IsLimp()
		|| (Health && !Health->IsAlive());
	if (Mode == EThugMode::Stunned)
	{
		return bStunned;
	}
	if (bStunned)
	{
		return false;
	}
	const bool bAlerted = Thug->IsAlerted();
	const bool bEngaged = bAlerted && IsValid(TargetActor) && !bBlinded;
	switch (Mode)
	{
	case EThugMode::Reposition:
		if (!bEngaged && !bZipping)
		{
			return false;
		}
		if (Thug->IsArcher())
		{
			return bZipping;
		}
		if (Thug->IsGunner())
		{
			return bRetreating
				|| FVector::Dist2D(TargetActor->GetActorLocation(), Thug->GetActorLocation()) < RetreatTriggerDistance;
		}
		return false;
	case EThugMode::Cover:
		return bEngaged && Thug->IsGunner() && GunnerPhase == EGunnerPhase::Covering && bHasCover;
	case EThugMode::Attack:
		return bAlerted;
	case EThugMode::Investigate:
		return Thug->GetAlertState() == EThugAlertState::Suspicious;
	case EThugMode::Patrol:
	default:
		return true;
	}
}

EThugMode AThugAIController::ChooseMode() const
{
	static const EThugMode Order[] = {
		EThugMode::Stunned, EThugMode::Reposition, EThugMode::Cover, EThugMode::Attack, EThugMode::Investigate,
	};
	for (const EThugMode Mode : Order)
	{
		if (HasNeed(Mode))
		{
			return Mode;
		}
	}
	return EThugMode::Patrol;
}

void AThugAIController::EnterMode(EThugMode NewMode)
{
	const EThugMode Old = CurrentMode;
	CurrentMode = NewMode;
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: mode %s -> %s."), *GetName(), *UEnum::GetValueAsString(Old),
		*UEnum::GetValueAsString(NewMode));
	if (NewMode == EThugMode::Stunned)
	{
		CancelBurst(TEXT("stunned"));
		CancelArcherDraw(TEXT("stunned"));
	}
	if (Old == EThugMode::Attack && NewMode != EThugMode::Attack)
	{
		CancelArcherDraw(TEXT("left the attack"));
	}
	// Staggered, knocked down, held, dead, or off to cover or a zip: his turn is over.
	if (NewMode != EThugMode::Attack)
	{
		ReleaseAttackToken();
	}
}

void AThugAIController::RunMode(EThugMode Mode, float DeltaSeconds)
{
	if (Mode != CurrentMode)
	{
		EnterMode(Mode);
	}
	switch (Mode)
	{
	case EThugMode::Stunned:
		break;
	case EThugMode::Reposition:
		if (const AThugCharacter* Thug = GetThug(); Thug && Thug->IsArcher())
		{
			TickArcherZip(DeltaSeconds);
		}
		else
		{
			TickGunnerRetreat(DeltaSeconds);
		}
		break;
	case EThugMode::Cover:
		TickGunnerCover(DeltaSeconds);
		break;
	case EThugMode::Attack:
		TickAlerted(DeltaSeconds);
		break;
	case EThugMode::Investigate:
		TickSuspicious(DeltaSeconds);
		break;
	case EThugMode::Patrol:
	default:
		TickCalm(DeltaSeconds);
		break;
	}
}

// --- Calm and Suspicious ---------------------------------------------------------------------------

void AThugAIController::TickCalm(float DeltaSeconds)
{
	const AThugCharacter* Thug = GetThug();
	if (!Thug || Thug->PatrolPoints.Num() == 0)
	{
		return;
	}

	if (bPatrolWaiting)
	{
		// A point that says which way to look (a gunner over a hall): he turns to it while he waits.
		const AActor* Point = Thug->PatrolPoints.IsValidIndex(PatrolIndex) ? Thug->PatrolPoints[PatrolIndex].Get() : nullptr;
		if (Point && Point->ActorHasTag(HawkeyeThugBrain::PatrolFacingTag))
		{
			FaceTarget(GetPawn(), Point->GetActorForwardVector());
			LookPitch = Point->GetActorRotation().Pitch;
		}
		PatrolWaitElapsed += DeltaSeconds;
		if (PatrolWaitElapsed < Thug->PatrolWaitSeconds)
		{
			return;
		}

		bPatrolWaiting = false;
		PatrolWaitElapsed = 0.f;
		LookPitch = 0.f;
		PatrolIndex = (PatrolIndex + 1) % Thug->PatrolPoints.Num();
	}

	AdvancePatrol();
}

void AThugAIController::AdvancePatrol()
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !Thug->PatrolPoints.IsValidIndex(PatrolIndex))
	{
		return;
	}

	AActor* Point = Thug->PatrolPoints[PatrolIndex];
	if (!IsValid(Point))
	{
		PatrolIndex = (PatrolIndex + 1) % FMath::Max(Thug->PatrolPoints.Num(), 1);
		return;
	}

	const EPathFollowingStatus::Type Status = GetMoveStatus();
	if (Status == EPathFollowingStatus::Moving)
	{
		return;
	}

	const float DistanceSq = FVector::DistSquared2D(Thug->GetActorLocation(), Point->GetActorLocation());
	if (DistanceSq <= FMath::Square(120.f))
	{
		bPatrolWaiting = true;
		PatrolWaitElapsed = 0.f;
		return;
	}

	RequestMoveToActor(Point, /*AcceptanceRadius=*/60.f);
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

void AThugAIController::TickSuspicious(float DeltaSeconds)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug)
	{
		return;
	}
	// An archer holds his roof: he turns to the noise instead of walking off to it.
	if (Thug->IsArcher())
	{
		FaceTarget(Thug, LastStimulusLocation - Thug->GetActorLocation());
	}
	else if (GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		return;
	}
	else if (FVector::DistSquared2D(Thug->GetActorLocation(), LastStimulusLocation) > FMath::Square(120.f))
	{
		// A noise he cannot walk to (on the roof over him, in the air where she zipped) he watches from here.
		if (RequestMoveToLocation(LastStimulusLocation, /*AcceptanceRadius=*/60.f, WideNavSnap, TEXT("the noise")))
		{
			return;
		}
		FaceTarget(Thug, LastStimulusLocation - Thug->GetActorLocation());
	}

	// Standing on the noise (or facing it) with nothing to show for it.
	InvestigateElapsed += DeltaSeconds;
	if (InvestigateElapsed >= InvestigateSeconds)
	{
		SetState(EThugAlertState::Calm);
	}
}

// --- Alerted ---------------------------------------------------------------------------------------

void AThugAIController::TickAlerted(float DeltaSeconds)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug)
	{
		return;
	}

	// Alerted by a noise he has not yet put a body to: stay alerted, but nothing to shoot at.
	if (!IsValid(TargetActor))
	{
		ReleaseAttackToken();
		return;
	}

	// Blind in the smoke: he stands and waits for it to clear rather than swinging or shooting at
	// where she was. Losing the target on the usual clock is what sends him looking.
	if (bBlinded)
	{
		StopMovement();
		CancelBurst(TEXT("blinded"));
		CancelArcherDraw(TEXT("blinded"));
		ReleaseAttackToken();
		return;
	}

	const FVector ToTarget = TargetActor->GetActorLocation() - Thug->GetActorLocation();

	// She is down: nobody piles on (claude-docs/gameplay-semantics.md, "Health and damage").
	if (IsTargetDown(TargetActor))
	{
		TickStandOff(DeltaSeconds, ToTarget);
		return;
	}
	bStandingOff = false;

	// The weapon traces along the control rotation, so the aim always points at him. The body
	// does not follow it (see AThugCharacter's movement setup): it faces where it is walking, and
	// only squares up once he has stopped.
	FRotator FacingRotation = ToTarget.Rotation();
	FacingRotation.Roll = 0.f;
	SetControlRotation(FacingRotation);

	if (Thug->IsGunner())
	{
		TickGunner(DeltaSeconds, ToTarget);
	}
	else if (Thug->IsArcher())
	{
		TickArcher(DeltaSeconds, ToTarget);
	}
	else
	{
		TickMeleeRush(DeltaSeconds, ToTarget);
	}
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

void AThugAIController::TickGunner(float DeltaSeconds, const FVector& ToTarget)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug)
	{
		return;
	}
	BurstPauseRemaining = FMath::Max(0.f, BurstPauseRemaining - DeltaSeconds);
	if (Burst.IsActive())
	{
		RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
	}

	if (ToTarget.Size2D() > EngageRange && GunnerPhase != EGunnerPhase::Peeking)
	{
		CancelBurst(TEXT("out of range"));
		RequestMoveToActor(TargetActor, /*AcceptanceRadius=*/EngageRange * 0.8f);
		return;
	}

	// Stepping out to the peek spot first.
	if (GunnerPhase == EGunnerPhase::Peeking && !Burst.IsActive()
		&& FVector::Dist2D(Thug->GetActorLocation(), PeekPoint) > HawkeyeThugBrain::ArriveDistance)
	{
		if (GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			RequestMoveToLocation(PeekPoint, 40.f);
		}
		return;
	}

	StopMovement();
	FaceTarget(Thug, ToTarget);
	const FVector Chest = GetAimPointOn(TargetActor);

	// Aimed at while out in the open: he ducks, if there is anywhere to duck to. A peek is committed.
	if (bAimedAt && GunnerPhase == EGunnerPhase::Open && BeginCover(TEXT("aimed at")))
	{
		return;
	}

	if (!Burst.IsActive())
	{
		if (IsJammed())
		{
			if (!bLoggedJamHold)
			{
				bLoggedJamHold = true;
				UE_LOG(LogHawkeye, Log, TEXT("%s: jammed, holds fire (%.1f s left)."), *Thug->GetName(), JamRemaining);
			}
			Thug->SetWeaponRaised(false, Chest);
			return;
		}
		if (BurstPauseRemaining > 0.f)
		{
			return;
		}
		// His turn on her ranged budget (gameplay-semantics.md, "Attack tokens"); without it he holds his aim.
		const FHawkeyeTokenGrant Token = RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
		if (!Token.bGranted)
		{
			return;
		}
		Burst.Start(Token.MinTelegraphSeconds);
		Thug->SetWeaponRaised(true, Chest);
		Thug->SetTelegraphGlint(true);
		UE_LOG(LogHawkeye, Log, TEXT("%s: raises his pistol at %s from %.0f cm: %.1f s telegraph, then %d shots."),
			*Thug->GetName(), *GetNameSafe(TargetActor), ToTarget.Size(), Burst.GetTelegraphSeconds(), Burst.ShotsPerBurst);
		return;
	}

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
		return;
	}

	// The burst is over.
	ReleaseAttackToken();
	Thug->SetWeaponRaised(false, Chest);
	BurstPauseRemaining = BurstPauseSeconds;
	const bool bWasPeeking = GunnerPhase == EGunnerPhase::Peeking;
	if (ShotsSinceCover >= ShotsBeforeCover)
	{
		if (bWasPeeking && bHasCover)
		{
			// Back behind the same cover.
			GunnerPhase = EGunnerPhase::Covering;
			HideElapsed = 0.f;
			RequestMoveToLocation(CoverPoint, 40.f, WideNavSnap, TEXT("cover"));
			UE_LOG(LogHawkeye, Log, TEXT("%s: back into cover after %d shots."), *Thug->GetName(), ShotsSinceCover);
			ShotsSinceCover = 0;
		}
		else if (!BeginCover(TEXT("fired a burst")))
		{
			ShotsSinceCover = 0;
		}
	}
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
	HideElapsed = 0.f;
	ShotsSinceCover = 0;
	GunnerPhase = EGunnerPhase::Covering;
	RequestMoveToLocation(CoverPoint, 40.f, WideNavSnap, TEXT("cover"));
	UE_LOG(LogHawkeye, Log, TEXT("%s: takes cover (%s) at %s, %.0f cm away, out of %s's line."), *Thug->GetName(), Why,
		*CoverPoint.ToCompactString(), FVector::Dist2D(CoverPoint, Thug->GetActorLocation()), *GetNameSafe(TargetActor));
	return true;
}

void AThugAIController::TickGunnerCover(float DeltaSeconds)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		return;
	}
	CoverElapsed += DeltaSeconds;
	Thug->SetWeaponRaised(false, FVector::ZeroVector);
	if (FVector::Dist2D(Thug->GetActorLocation(), CoverPoint) > HawkeyeThugBrain::ArriveDistance)
	{
		if (GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			RequestMoveToLocation(CoverPoint, 40.f, WideNavSnap, TEXT("cover"));
		}
		// Never stuck on the way: after the relocation time he gives up on this one.
		if (CoverElapsed < RelocateSeconds)
		{
			return;
		}
	}
	FaceTarget(Thug, TargetActor->GetActorLocation() - Thug->GetActorLocation());
	HideElapsed += DeltaSeconds;
	if (HideElapsed < CoverHideSeconds)
	{
		return;
	}
	// Every RelocateSeconds a new cover point; otherwise a peek from this one.
	if (CoverElapsed >= RelocateSeconds)
	{
		const FVector Old = CoverPoint;
		if (BeginCover(TEXT("relocating"), &Old))
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: relocates after %.1f s at %s."), *Thug->GetName(), RelocateSeconds,
				*Old.ToCompactString());
			return;
		}
		CoverElapsed = 0.f;
	}
	PeekPoint = FindPeekPoint();
	GunnerPhase = EGunnerPhase::Peeking;
	HideElapsed = 0.f;
	RequestMoveToLocation(PeekPoint, 40.f);
	UE_LOG(LogHawkeye, Log, TEXT("%s: peeks from cover at %s."), *Thug->GetName(), *PeekPoint.ToCompactString());
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

void AThugAIController::TickGunnerRetreat(float DeltaSeconds)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		bRetreating = false;
		return;
	}
	const FVector Player = TargetActor->GetActorLocation();
	if (!bRetreating)
	{
		FVector Goal;
		if (!ComputeRetreatGoal(Thug->GetActorLocation(), Player, RetreatTriggerDistance, RetreatToDistance, Goal))
		{
			return;
		}
		// Somewhere he can stand: on a roof the straight line may run off the edge.
		FNavLocation OnNav;
		const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
		if (Nav && Nav->ProjectPointToNavigation(Goal, OnNav, FVector(200.f, 200.f, 250.f)))
		{
			Goal = OnNav.Location;
		}
		CancelBurst(TEXT("backing off"));
		GunnerPhase = EGunnerPhase::Open;
		bHasCover = false;
		bRetreating = true;
		RetreatGoal = Goal;
		RetreatElapsed = 0.f;
		RequestMoveToLocation(RetreatGoal, 40.f);
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s closed to %.0f cm; backs off to %.0f cm (%s)."), *Thug->GetName(),
			*GetNameSafe(TargetActor), FVector::Dist2D(Player, Thug->GetActorLocation()), RetreatToDistance,
			*RetreatGoal.ToCompactString());
		return;
	}
	RetreatElapsed += DeltaSeconds;
	const float Distance = FVector::Dist2D(Player, Thug->GetActorLocation());
	const bool bArrived = FVector::Dist2D(Thug->GetActorLocation(), RetreatGoal) <= HawkeyeThugBrain::ArriveDistance;
	if (Distance >= RetreatToDistance - 50.f || bArrived || RetreatElapsed >= RetreatMaxSeconds)
	{
		bRetreating = false;
		StopMovement();
		UE_LOG(LogHawkeye, Log, TEXT("%s: done backing off, %.0f cm from %s."), *Thug->GetName(), Distance,
			*GetNameSafe(TargetActor));
	}
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

void AThugAIController::TickArcher(float DeltaSeconds, const FVector& ToTarget)
{
	AThugCharacter* Thug = GetThug();
	UBowComponent* Bow = Thug ? Thug->GetBowComponent() : nullptr;
	const UBowDefinition* BowDef = Bow ? Bow->GetBow() : nullptr;
	if (!Bow || !BowDef)
	{
		return;
	}
	if (Bow->IsDrawing())
	{
		RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
	}
	// Holding a draw on where she went: the hold timer watches for her, nothing else moves him.
	if (ArcherHold.IsHolding())
	{
		return;
	}
	const double Now = GetNowSeconds();
	const float Distance = ToTarget.Size2D();

	// Just loosed: he stands there, bow down, open to a shot back.
	if (ArcherHold.IsLoose())
	{
		StopMovement();
		FaceTarget(Thug, ToTarget);
		return;
	}

	// The band: inside ArcherCloseRange he leaves the roof; out past the far edge he looks for a nearer one.
	const EArcherRangeAction Action = ChooseArcherRangeAction(Distance, ArcherCloseRange, ArcherMinRange, ArcherMaxRange);
	const bool bCanRelocate = Now - LastRelocateSeconds >= ArcherRelocateCooldownSeconds;
	if ((Action == EArcherRangeAction::Relocate || (Action == EArcherRangeAction::Approach && Distance > ArcherAggroRange))
		&& bCanRelocate && StartArcherRelocation(Action == EArcherRangeAction::Relocate ? TEXT("she closed in") : TEXT("out of range")))
	{
		return;
	}
	if (Action == EArcherRangeAction::StepBack && !Bow->IsDrawing() && GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		const FVector Away = -ToTarget.GetSafeNormal2D();
		FNavLocation Spot;
		const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld());
		const float Step = FMath::Min(ArcherMinRange - Distance + 100.f, 400.f);
		if (Nav && Nav->ProjectPointToNavigation(Thug->GetActorLocation() + Away * Step, Spot, FVector(50.f, 50.f, 150.f))
			&& FVector::Dist2D(Spot.Location, Thug->GetActorLocation()) > 100.f)
		{
			RequestMoveToLocation(Spot.Location, 30.f);
			UE_LOG(LogHawkeye, Verbose, TEXT("%s: steps back %.0f cm to keep %.0f cm."), *Thug->GetName(), Step, ArcherMinRange);
			return;
		}
	}
	if (GetMoveStatus() == EPathFollowingStatus::Moving && !Bow->IsDrawing())
	{
		return;
	}
	StopMovement();
	FaceTarget(Thug, ToTarget);

	// Lead on her velocity, at the chest, allowing for the drop.
	const FVector Chest = GetAimPointOn(TargetActor);
	const UWorld* World = GetWorld();
	const float GravityZ = World ? World->GetGravityZ() : -980.f;
	const FVector Aim = ComputeLeadAimPoint(Bow->GetArrowSpawnLocation(), Chest, TargetActor->GetVelocity(),
		BowDef->MaxSpeed, GravityZ);
	FRotator AimRotation = (Aim - Thug->GetPawnViewLocation()).Rotation();
	AimRotation.Roll = 0.f;
	SetControlRotation(AimRotation);
	Bow->SetAimOverride(Aim);

	const bool bLine = HasLineTo(Chest, TargetActor);
	if (!Bow->IsDrawing())
	{
		if (!bLine || Now - LastArrowSeconds < NextArcherGapSeconds)
		{
			return;
		}
		const FHawkeyeTokenGrant Token = RequestAttackToken(EHawkeyeAttackKind::Ranged, /*bReady=*/true);
		if (!Token.bGranted)
		{
			return;
		}
		// The difficulty sets how long the draw takes (from off her screen, at least the token's minimum); the
		// bow's own numbers stay as they are.
		const float DrawSeconds = FMath::Max(UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ArcherDrawSeconds),
			Token.MinTelegraphSeconds);
		Bow->DrawRate = ComputeArcherDrawRate(BowDef->FullDrawSeconds, DrawSeconds);
		if (!Bow->StartDraw())
		{
			ReleaseAttackToken();
			return;
		}
		Thug->SetTelegraphGlint(true);
		UE_LOG(LogHawkeye, Log, TEXT("%s: the bowstring creaks: drawing on %s at %.0f cm (%.1f s)."), *Thug->GetName(),
			*GetNameSafe(TargetActor), ToTarget.Size(), DrawSeconds);
		return;
	}
	if (!bLine)
	{
		BeginArcherHold(Aim);
		return;
	}
	if (Bow->GetDrawElapsed() + KINDA_SMALL_NUMBER >= BowDef->FullDrawSeconds)
	{
		// The shot itself: where he saw her a lag ago, led on, scattered by how hard she is to follow.
		const FHawkeyeRangedAim Shot = ComputeRangedAim(Bow->GetArrowSpawnLocation(), BowDef->MaxSpeed, ArcherAimConeDegrees);
		LooseArcherDraw(Shot.AimPoint, *FString::Printf(TEXT("%s, %.0f cm off her chest, cone %.1f deg, her speed %.0f cm/s"),
			Shot.bForcedMiss ? TEXT("wide on purpose") : TEXT("leading"), FVector::Dist(Shot.AimPoint, Chest), Shot.ConeDegrees,
			TargetActor->GetVelocity().Size()));
	}
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
	if (!BowDef || !Bow->IsDrawing() || !IsValid(TargetActor) || bBlinded || HasNeed(EThugMode::Stunned))
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

bool AThugAIController::StartArcherRelocation(const TCHAR* Why)
{
	AThugCharacter* Thug = GetThug();
	UWorld* World = GetWorld();
	if (!Thug || !World || !IsValid(TargetActor))
	{
		return false;
	}
	const FVector Player = TargetActor->GetActorLocation();
	const FVector Chest = GetAimPointOn(TargetActor);
	const float HalfHeight = Thug->GetCapsuleComponent() ? Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;
	AGrappleAnchor* Best = nullptr;
	FVector BestLanding = FVector::ZeroVector;
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
			BestLanding = Landing;
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
	CancelArcherDraw(TEXT("relocating"));
	StopMovement();
	bZipping = true;
	ZipFrom = Thug->GetActorLocation();
	ZipTo = BestLanding;
	ZipElapsed = 0.f;
	ZipSeconds = FMath::Max(FVector::Dist(ZipFrom, ZipTo) / ArcherZipSpeed, 0.1f);
	if (UCharacterMovementComponent* Movement = Thug->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Flying);
	}
	// Teleport-zip: a straight kinematic line, no grapple component on a thug. Logged as such.
	UE_LOG(LogHawkeye, Log, TEXT("%s: relocates (%s): teleport-zip to anchor %s, %.0f cm in %.2f s, landing %.0f cm from %s."),
		*Thug->GetName(), Why, *Best->GetName(), FVector::Dist(ZipFrom, ZipTo), ZipSeconds,
		FVector::Dist2D(ZipTo, Player), *GetNameSafe(TargetActor));
	return true;
}

void AThugAIController::TickArcherZip(float DeltaSeconds)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !bZipping)
	{
		bZipping = false;
		return;
	}
	ZipElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(ZipElapsed / ZipSeconds, 0.f, 1.f);
	Thug->SetActorLocation(FMath::Lerp(ZipFrom, ZipTo, Alpha), false, nullptr, ETeleportType::TeleportPhysics);
	if (Alpha < 1.f)
	{
		return;
	}
	bZipping = false;
	if (UCharacterMovementComponent* Movement = Thug->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: landed at %s."), *Thug->GetName(), *ZipTo.ToCompactString());
}

// --- Melee -----------------------------------------------------------------------------------------

void AThugAIController::TickMeleeRush(float DeltaSeconds, const FVector& ToTarget)
{
	AThugCharacter* Thug = GetThug();
	UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
	if (!Melee)
	{
		return;
	}
	ApplyMoveSpeed();

	const bool bSwinging = Melee->IsAttacking();
	if (bWasSwinging && !bSwinging)
	{
		FinishSwing(ToTarget);
	}
	bWasSwinging = bSwinging;
	MeleeCooldownRemaining = FMath::Max(0.f, MeleeCooldownRemaining - DeltaSeconds);

	if (bBackingOff)
	{
		BackOffElapsed += DeltaSeconds;
		if (GetMoveStatus() != EPathFollowingStatus::Moving || BackOffElapsed >= MeleeBackOffMaxSeconds)
		{
			bBackingOff = false;
			SwingsSinceBackOff = 0;
		}
		return;
	}

	// Only the thug holding her melee token swings (gameplay-semantics.md, "Attack tokens"); the rest hold their
	// places on the ring round her. Off the ring or in his cooldown he is not ready, so never granted.
	const float Distance = ToTarget.Size2D();
	const bool bReady = !bSwinging && MeleeCooldownRemaining <= 0.f
		&& Distance <= MeleeWaitRingDistance + HawkeyeThugBrain::RingReadySlack;
	const FHawkeyeTokenGrant Token = RequestAttackToken(EHawkeyeAttackKind::Melee, bReady);
	if (!Token.bGranted && !bSwinging)
	{
		WaitOnRing(DeltaSeconds, ToTarget);
		return;
	}
	if (!bSwinging && Distance <= MeleeEngageRange)
	{
		StartSwing(Token, Distance);
	}
	else if (!bSwinging && (StepInElapsed += DeltaSeconds) >= MeleeStepInMaxSeconds)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s stayed out of reach %.1f s after his turn came; he gives it back."),
			*Thug->GetName(), *GetNameSafe(TargetActor), MeleeStepInMaxSeconds);
		StepInElapsed = 0.f;
		ReleaseAttackToken();
		return;
	}

	// Keep closing through the wind-up; stop and square up once he is in reach.
	if (Distance > MeleeCloseDistance)
	{
		RequestMoveToActor(TargetActor, /*AcceptanceRadius=*/MeleeCloseDistance * 0.6f);
		return;
	}
	StopMovement();
	FaceTarget(Thug, ToTarget);
}

void AThugAIController::WaitOnRing(float DeltaSeconds, const FVector& ToTarget)
{
	StepInElapsed = 0.f;
	const UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(this);
	const float Slot = Tokens ? Tokens->GetRingSlotYaw(GetPawn(), TargetActor, MeleeRingSeparationDegrees)
		: (-ToTarget).Rotation().Yaw;
	CircleTarget(DeltaSeconds, ToTarget, MeleeWaitRingDistance, Slot, TEXT("his place on the ring"));
}

void AThugAIController::StartSwing(const FHawkeyeTokenGrant& Token, float Distance)
{
	AThugCharacter* Thug = GetThug();
	UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
	if (!Melee)
	{
		return;
	}
	// Granted from off her screen, the wind-up is never shorter than the warning it has to give.
	FHawkeyeMeleeAttack Attack = ScaleAttackForDifficulty(Thug->GetMeleeAttack(SwingsStarted));
	Attack.WindupSeconds = FMath::Max(Attack.WindupSeconds, Token.MinTelegraphSeconds);
	UE_LOG(LogHawkeye, Log, TEXT("%s: telegraphs a %s swing at %s from %.0f cm (%.1f s wind-up)."), *Thug->GetName(),
		*Attack.Name.ToString(), *GetNameSafe(TargetActor), Distance, Attack.WindupSeconds);
	bWasSwinging = Melee->StartAttack(Attack);
	SwingsStarted += bWasSwinging ? 1 : 0;
	StepInElapsed = 0.f;
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

void AThugAIController::TickStandOff(float DeltaSeconds, const FVector& ToTarget)
{
	AThugCharacter* Thug = GetThug();
	if (!Thug || !IsValid(TargetActor))
	{
		return;
	}
	if (!bStandingOff)
	{
		bStandingOff = true;
		RingRepathRemaining = 0.f;
		// Round her from the side he is already on, so he steps back rather than across her.
		StandOffAngle = (-ToTarget).Rotation().Yaw;
		CancelBurst(TEXT("target down"));
		CancelArcherDraw(TEXT("target down"));
		ReleaseAttackToken();
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s is down; backing off to circle at %.0f cm."), *Thug->GetName(),
			*GetNameSafe(TargetActor), StandOffDistance);
	}
	// A swing already under way plays out (it can no longer hurt her); nothing new starts.
	const UMeleeComponent* Melee = Thug->GetMeleeComponent();
	bWasSwinging = Melee && Melee->IsAttacking();
	if (bWasSwinging)
	{
		return;
	}
	StandOffAngle = FMath::UnwindDegrees(StandOffAngle + StandOffOrbitDegreesPerSecond * DeltaSeconds);
	CircleTarget(DeltaSeconds, ToTarget, StandOffDistance, StandOffAngle, TEXT("the circle round her"));
}

void AThugAIController::CircleTarget(float DeltaSeconds, const FVector& ToTarget, float Radius, float SlotYaw,
	const TCHAR* GoalName)
{
	APawn* Me = GetPawn();
	if (!Me || !IsValid(TargetActor))
	{
		return;
	}
	if (GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		FaceTarget(Me, ToTarget);
	}
	RingRepathRemaining -= DeltaSeconds;
	const FVector Spot = TargetActor->GetActorLocation() + FRotator(0.f, SlotYaw, 0.f).Vector() * Radius;
	if (RingRepathRemaining > 0.f || FVector::Dist2D(Me->GetActorLocation(), Spot) <= HawkeyeThugBrain::ArriveDistance)
	{
		return;
	}
	RingRepathRemaining = HawkeyeThugBrain::RingRepathSeconds;
	// Round her against a wall or a parapet some of the ring is inside it: the nearest floor within 3 m.
	RequestMoveToLocation(Spot, 30.f, WideNavSnap, GoalName);
}

void AThugAIController::FinishSwing(const FVector& ToTarget)
{
	ReleaseAttackToken();
	MeleeCooldownRemaining = MeleeCooldownSeconds;
	++SwingsSinceBackOff;
	if (SwingsSinceBackOff < MeleeSwingsBeforeBackOff)
	{
		return;
	}

	const AThugCharacter* Thug = GetThug();
	if (!Thug)
	{
		return;
	}
	// Only somewhere he can stand: a step back on a roof may be into the parapet. Without a spot
	// he simply comes again after the cooldown.
	const FVector Away = -ToTarget.GetSafeNormal2D();
	FNavLocation Spot;
	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSystem || !NavSystem->ProjectPointToNavigation(
			Thug->GetActorLocation() + Away * MeleeBackOffDistance, Spot, FVector(50.f, 50.f, 150.f)))
	{
		SwingsSinceBackOff = 0;
		return;
	}
	bBackingOff = true;
	BackOffElapsed = 0.f;
	RequestMoveToLocation(Spot.Location, /*AcceptanceRadius=*/30.f);
	UE_LOG(LogHawkeye, Log, TEXT("%s: backs off %.0f cm after %d swings."), *Thug->GetName(),
		MeleeBackOffDistance, SwingsSinceBackOff);
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
