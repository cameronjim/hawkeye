#include "Chase/ChaseSubsystem.h"

#include "Hawkeye.h"
#include "HawkeyeGameMode.h"
#include "HawkeyePlayerController.h"
#include "Chase/ChaseRouteDefinition.h"
#include "Chase/ChaseRules.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dialogue/DialogueQueue.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/ParkourComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

const FName UChaseSubsystem::ThiefTag(TEXT("ChaseThief"));
const FName UChaseSubsystem::FurnitureTag(TEXT("ChaseFurniture"));

namespace HawkeyeChase
{
	/** The light over the dumpster, lm. */
	static constexpr float DumpsterLightLumens = 900.f;

	/** The beat's three moves, s (gameplay-semantics.md, "Chases": 0.7 + 1.2 + 0.9 after the 0.3 s let-go). */
	static constexpr float SnapSeconds = 0.3f;
	static constexpr float FallSeconds = 0.7f;
	static constexpr float SitSeconds = 1.2f;
	static constexpr float ClimbSeconds = 0.9f;

	static float HalfHeightOf(const APawn* Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	}

	static FVector FeetOf(const APawn* Pawn)
	{
		return Pawn->GetActorLocation() - FVector(0.f, 0.f, HalfHeightOf(Pawn));
	}
}

UChaseSubsystem* UChaseSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UChaseSubsystem>() : nullptr;
}

bool UChaseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChaseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UMissionSubsystem>();
	Super::Initialize(Collection);
}

void UChaseSubsystem::Deinitialize()
{
	OnChaseStateChanged.Clear();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UChaseSubsystem::HandleCampaignLoaded);
	}
	UnbindKate();
	Route = nullptr;
	Super::Deinitialize();
}

void UChaseSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UChaseSubsystem::HandleCampaignLoaded);
	}
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->OnMissionStarted.AddUniqueDynamic(this, &UChaseSubsystem::HandleMissionStarted);
	}
}

TStatId UChaseSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UChaseSubsystem, STATGROUP_Tickables);
}

APawn* UChaseSubsystem::FindKate() const
{
	if (PlayerOverride.IsValid())
	{
		return PlayerOverride.Get();
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

void UChaseSubsystem::SetState(EChaseState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: chase %s -> %s at %.1f s (thief %.0f, Kate %.0f of %.0f cm, lead %.0f)."), *GetName(),
		*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(NewState), ChaseSeconds, ThiefAlong, KateAlong,
		GetRouteLength(), Lead);
	State = NewState;
	OnChaseStateChanged.Broadcast(NewState);
}

bool UChaseSubsystem::IsChaseRunning() const
{
	return State == EChaseState::Running || State == EChaseState::Beat || State == EChaseState::Cornered
		|| State == EChaseState::Caught;
}

// --- Starting and stopping -------------------------------------------------------------------------------

void UChaseSubsystem::HandleMissionStarted(UMissionDefinition* Mission)
{
	// The furniture belongs to the chapter, run or not: the dumpster is there in free roam too.
	if (const UChaseRouteDefinition* Chase = Mission ? Mission->Chase.LoadSynchronous() : nullptr)
	{
		EnsureFurniture(Chase);
	}
}

bool UChaseSubsystem::BeginChapter(const UMissionDefinition* Mission)
{
	UChaseRouteDefinition* Chase = Mission ? Mission->Chase.LoadSynchronous() : nullptr;
	if (!Chase)
	{
		return false;
	}
	return StartChase(Chase, /*bPlaceKate=*/true);
}

bool UChaseSubsystem::LoadRoute(UChaseRouteDefinition* InRoute)
{
	if (!InRoute || InRoute->Waypoints.Num() < 2)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: chase %s has fewer than two waypoints; not started."), *GetName(), *GetNameSafe(InRoute));
		return false;
	}
	Route = InRoute;
	Points = InRoute->GetPoints();
	Distances = UChaseRules::Cumulative(Points);
	return true;
}

bool UChaseSubsystem::StartChase(UChaseRouteDefinition* InRoute, bool bPlaceKate)
{
	StopChase();
	if (!LoadRoute(InRoute))
	{
		return false;
	}
	EnsureFurniture(InRoute);
	const FChaseTuning& Tuning = Route->Tuning;
	ThiefAlong = FMath::Min(Tuning.StartLead, GetRouteLength());
	ThiefSpeed = Tuning.BaseSpeed;
	KateAlong = 0.f;
	bKateOnRoute = true;
	Lead = ThiefAlong;
	MinLead = MaxLead = Lead;
	LoseClock = ChaseSeconds = CaughtSeconds = 0.f;
	ReachedWaypoints = 0;
	bBeatDone = !Route->bHasBeat;
	bBeatPlayed = bPulledAhead = bVaulted = bWasZipping = bHaveKateFeet = false;
	SyncSegment();
	APawn* Kate = FindKate();
	if (bPlaceKate && Kate)
	{
		PlaceKate(Kate);
	}
	BindKate(Kate);
	SpawnThief();
	SetState(EChaseState::Running);
	UE_LOG(LogHawkeye, Log, TEXT("%s: chase %s started: %d waypoints, %.0f m, thief %.0f m ahead."), *GetName(), *Route->GetName(),
		Points.Num(), GetRouteLength() / 100.f, ThiefAlong / 100.f);
	return true;
}

void UChaseSubsystem::StopChase()
{
	if (AThugCharacter* Runner = Thief.Get(); Runner && !Runner->IsLimp())
	{
		if (AController* Brain = Runner->GetController())
		{
			Brain->UnPossess();
			Brain->Destroy();
		}
		Runner->Destroy();
	}
	Thief = nullptr;
	if (BeatPhase != 0)
	{
		EndBeat(Cast<AHawkeyeCharacter>(FindKate()), /*bForced=*/true);
	}
	ClearMarker();
	UnbindKate();
	SetState(EChaseState::Idle);
}

void UChaseSubsystem::EnsureFurniture(const UChaseRouteDefinition* InRoute)
{
	UWorld* World = GetWorld();
	if (!World || !InRoute || FurnishedFor.Get() == InRoute)
	{
		return;
	}
	FurnishedFor = InRoute;
	auto Place = [this, World](UStaticMesh* Mesh, const FTransform& At, const TCHAR* What) -> AActor*
	{
		if (!Mesh)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), At, Params);
		if (!Actor)
		{
			return nullptr;
		}
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->Tags.Add(FurnitureTag);
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s placed at %s."), *GetName(), What, *At.GetLocation().ToCompactString());
		return Actor;
	};
	if (InRoute->bHasBeat)
	{
		Dumpster = Place(InRoute->DumpsterMesh.LoadSynchronous(),
			FTransform(FRotator(0.f, InRoute->DumpsterYaw, 0.f), InRoute->DumpsterLocation), TEXT("the dumpster"));
		if (AStaticMeshActor* Bin = Cast<AStaticMeshActor>(Dumpster.Get()))
		{
			// The beat's camera looks into the bin; its walls must not push the lens into the paper.
			Bin->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (APointLight* Lamp = World->SpawnActor<APointLight>(InRoute->DumpsterLocation + FVector(0.f, 0.f, 380.f),
			FRotator::ZeroRotator, Params))
		{
			// A back-door light over the bin, so the beat reads at night.
			UPointLightComponent* Light = Cast<UPointLightComponent>(Lamp->GetLightComponent());
			Lamp->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			Light->SetIntensityUnits(ELightUnits::Lumens);
			Light->SetIntensity(HawkeyeChase::DumpsterLightLumens);
			Light->SetAttenuationRadius(900.f);
			Light->SetLightColor(FLinearColor(1.f, 0.72f, 0.48f));
			Light->SetCastShadows(false);
			Lamp->Tags.Add(FurnitureTag);
			Props.Add(Lamp);
		}
	}
	for (const FChaseProp& Prop : InRoute->Props)
	{
		Props.Add(Place(Prop.Mesh.LoadSynchronous(), Prop.Transform, TEXT("a roof prop")));
	}
}

AThugCharacter* UChaseSubsystem::SpawnThief()
{
	UWorld* World = GetWorld();
	UClass* Class = Route && !Route->ThiefClass.IsNull() ? Route->ThiefClass.LoadSynchronous() : nullptr;
	Class = Class ? Class : AThugCharacter::StaticClass();
	const FVector Feet = UChaseRules::PointAt(Points, Distances, ThiefAlong);
	const float HalfHeight = Class->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform At(FRotator::ZeroRotator, Feet + FVector(0.f, 0.f, HalfHeight));
	AThugCharacter* Runner = World ? World->SpawnActorDeferred<AThugCharacter>(Class, At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
	if (!Runner)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: could not spawn the thief (%s)."), *GetName(), *GetNameSafe(Class));
		return nullptr;
	}
	Runner->Weapon = EThugWeapon::Fists;
	Runner->bSkipSave = true;
	Runner->Tags.AddUnique(ThiefTag);
	Runner->FinishSpawning(At);
	if (!Cast<AThugAIController>(Runner->GetController()))
	{
		if (AController* Other = Runner->GetController())
		{
			Other->UnPossess();
			Other->Destroy();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AThugAIController* Brain = World->SpawnActor<AThugAIController>(AThugAIController::StaticClass(), At, Params))
		{
			Brain->Possess(Runner);
		}
	}
	if (AThugAIController* Brain = Cast<AThugAIController>(Runner->GetController()))
	{
		Brain->SetThinkingEnabled(false);
	}
	// Moved by the chase, not by physics: his velocity is set from where he went, for the run animation.
	Runner->GetCharacterMovement()->SetMovementMode(MOVE_Custom, 0);
	Runner->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Thief = Runner;
	LastThiefFeet = Feet;
	return Runner;
}

void UChaseSubsystem::PlaceKate(APawn* Kate) const
{
	const FVector Feet = Route->PlayerStart.IsZero() ? Points[0] : Route->PlayerStart;
	const FRotator Facing(0.f, Route->PlayerStartYaw, 0.f);
	Kate->TeleportTo(Feet + FVector(0.f, 0.f, HawkeyeChase::HalfHeightOf(Kate) + 2.f), Facing, false, true);
	if (AController* Controller = Kate->GetController())
	{
		Controller->SetControlRotation(FRotator(-8.f, Route->PlayerStartYaw, 0.f));
	}
	if (ACharacter* Character = Cast<ACharacter>(Kate))
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Kate);
		const float Sprint = Hawkeye ? Hawkeye->GetSprintSpeed() : 700.f;
		Movement->SetMovementMode(MOVE_Walking);
		Movement->Velocity = Facing.Vector() * Sprint;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: Kate put on the chase's start %s, yaw %.0f, at a sprint."), *GetName(), *Feet.ToCompactString(),
		Route->PlayerStartYaw);
}

void UChaseSubsystem::BindKate(APawn* Kate)
{
	UnbindKate();
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Kate);
	if (UParkourComponent* Parkour = Hawkeye ? Hawkeye->GetParkourComponent() : nullptr)
	{
		Parkour->OnParkourStarted.AddUniqueDynamic(this, &UChaseSubsystem::HandleParkourStarted);
		BoundKate = Kate;
	}
}

void UChaseSubsystem::UnbindKate()
{
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(BoundKate.Get());
	if (UParkourComponent* Parkour = Hawkeye ? Hawkeye->GetParkourComponent() : nullptr)
	{
		Parkour->OnParkourStarted.RemoveDynamic(this, &UChaseSubsystem::HandleParkourStarted);
	}
	BoundKate = nullptr;
}

void UChaseSubsystem::HandleParkourStarted(EHawkeyeParkourMove Move, EHawkeyeParkourRoute /*ParkourRoute*/)
{
	if (Move == EHawkeyeParkourMove::Vault && !bVaulted && State == EChaseState::Running && Route)
	{
		bVaulted = true;
		Say(Route->FirstVaultSequence);
	}
}

void UChaseSubsystem::Say(FName Sequence) const
{
	if (Sequence.IsNone())
	{
		return;
	}
	if (UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this))
	{
		Dialogue->PlaySequence(Sequence);
	}
}

// --- Every frame ------------------------------------------------------------------------------------------

void UChaseSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!IsChaseRunning() || !Route || DeltaTime <= 0.f)
	{
		return;
	}
	APawn* Kate = FindKate();
	ChaseSeconds += DeltaTime;
	AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Kate);
	if (State == EChaseState::Beat)
	{
		UpdateBeat(DeltaTime, Hawkeye);
	}
	else if (Kate)
	{
		UpdateKate(Kate);
		UpdateBeatTrigger(Hawkeye);
	}
	UpdateThief(DeltaTime);
	UpdateWaypoints();
	UpdateLose(DeltaTime, Kate);
	UpdateCatch(DeltaTime);
	UpdateMarker();
}

void UChaseSubsystem::UpdateKate(APawn* Kate)
{
	const FVector Feet = HawkeyeChase::FeetOf(Kate);
	float Along = KateAlong;
	bKateOnRoute = UChaseRules::Project(Points, Distances, Feet, KateAlong, Route->Tuning, Along);
	const float Moved = bHaveKateFeet ? FVector::Dist(Feet, LastKateFeet) : 0.f;
	KateAlong = UChaseRules::StepProgress(KateAlong, Along, Moved, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f);
	LastKateFeet = Feet;
	bHaveKateFeet = true;
	const AThugCharacter* Runner = Thief.Get();
	const float Straight = Runner ? FVector::Dist(Feet, LastThiefFeet) : 0.f;
	Lead = UChaseRules::Lead(ThiefAlong, KateAlong, bKateOnRoute, Straight);
	if (State == EChaseState::Running)
	{
		MinLead = FMath::Min(MinLead, Lead);
		MaxLead = FMath::Max(MaxLead, Lead);
	}
	if (!bPulledAhead && UChaseRules::Band(Lead, Route->Tuning) >= EChaseLeadBand::Far)
	{
		bPulledAhead = true;
		Say(Route->PullsAheadSequence);
	}
}

void UChaseSubsystem::SyncSegment()
{
	ThiefSegment = 0;
	LegElapsed = 0.f;
	for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
	{
		if (ThiefAlong >= Distances[Index])
		{
			ThiefSegment = Index;
		}
	}
	if (ThiefAlong >= GetRouteLength())
	{
		ThiefSegment = Points.Num() - 1;
		return;
	}
	const FChaseWaypoint& Next = Route->Waypoints[ThiefSegment + 1];
	if (Next.Leg != EChaseLeg::Run)
	{
		const float Span = FMath::Max(Distances[ThiefSegment + 1] - Distances[ThiefSegment], 1.f);
		const FVector From = Points[ThiefSegment];
		const FVector To = Points[ThiefSegment + 1];
		const float Seconds = UChaseRules::LegSeconds(Next.Leg, FVector::Dist2D(From, To), From.Z - To.Z);
		LegElapsed = Seconds * FMath::Clamp((ThiefAlong - Distances[ThiefSegment]) / Span, 0.f, 1.f);
	}
}

void UChaseSubsystem::UpdateThief(float DeltaSeconds)
{
	AThugCharacter* Runner = Thief.Get();
	if (!Runner || Runner->IsLimp() || !Runner->GetHealthComponent()->IsAlive())
	{
		return;
	}
	// Never a fight: he only runs (gameplay-semantics.md, "Chases"). Until he is cornered he cannot be stopped.
	if (Runner->GetAlertState() == EThugAlertState::Alerted)
	{
		Runner->SetAlertState(EThugAlertState::Calm);
	}
	Runner->bRefusesTakedown = !UChaseRules::IsCatchable(State == EChaseState::Cornered || ThiefSegment >= Points.Num() - 1);
	if (State == EChaseState::Cornered || State == EChaseState::Caught || ThiefSegment >= Points.Num() - 1)
	{
		const APawn* Kate = FindKate();
		const float Yaw = Kate ? UChaseRules::CorneredYaw(LastThiefFeet, Kate->GetActorLocation()) : Runner->GetActorRotation().Yaw;
		MoveThief(LastThiefFeet, Yaw, DeltaSeconds);
		if (State == EChaseState::Running)
		{
			SetState(EChaseState::Cornered);
			Say(Route->CorneredSequence);
		}
		return;
	}
	const FChaseWaypoint& Next = Route->Waypoints[ThiefSegment + 1];
	const FVector From = Points[ThiefSegment];
	const FVector To = Points[ThiefSegment + 1];
	const float Start = Distances[ThiefSegment];
	const float End = Distances[ThiefSegment + 1];
	FVector Feet;
	if (Next.Leg == EChaseLeg::Run)
	{
		ThiefSpeed = UChaseRules::StepSpeed(ThiefSpeed, UChaseRules::TargetSpeed(Lead, Route->Tuning), Route->Tuning.SpeedChangeRate,
			DeltaSeconds);
		ThiefAlong = FMath::Min(ThiefAlong + ThiefSpeed * DeltaSeconds, End);
		Feet = UChaseRules::PointAt(Points, Distances, ThiefAlong);
	}
	else
	{
		const float Seconds = FMath::Max(UChaseRules::LegSeconds(Next.Leg, FVector::Dist2D(From, To), From.Z - To.Z), 0.05f);
		LegElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(LegElapsed / Seconds, 0.f, 1.f);
		ThiefAlong = FMath::Lerp(Start, End, Alpha);
		Feet = UChaseRules::LegPosition(Next.Leg, From, To, Alpha);
	}
	if (ThiefAlong >= End - KINDA_SMALL_NUMBER)
	{
		ThiefAlong = End;
		++ThiefSegment;
		LegElapsed = 0.f;
	}
	const FVector Along = (To - From) * FVector(1.f, 1.f, 0.f);
	MoveThief(Feet, Along.IsNearlyZero() ? Runner->GetActorRotation().Yaw : Along.Rotation().Yaw, DeltaSeconds);
}

void UChaseSubsystem::MoveThief(const FVector& Feet, float Yaw, float DeltaSeconds)
{
	AThugCharacter* Runner = Thief.Get();
	if (!Runner)
	{
		return;
	}
	const float HalfHeight = HawkeyeChase::HalfHeightOf(Runner);
	Runner->SetActorLocationAndRotation(Feet + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Yaw, 0.f), false, nullptr,
		ETeleportType::TeleportPhysics);
	UCharacterMovementComponent* Movement = Runner->GetCharacterMovement();
	if (Movement->MovementMode != MOVE_Custom)
	{
		Movement->SetMovementMode(MOVE_Custom, 0);
	}
	Movement->Velocity = DeltaSeconds > 0.f ? (Feet - LastThiefFeet) / DeltaSeconds : FVector::ZeroVector;
	LastThiefFeet = Feet;
}

void UChaseSubsystem::UpdateWaypoints()
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	while (ReachedWaypoints < Route->Waypoints.Num() && UChaseRules::HasReached(KateAlong, Distances[ReachedWaypoints], Route->Tuning))
	{
		const FChaseWaypoint& Point = Route->Waypoints[ReachedWaypoints];
		++ReachedWaypoints;
		const UMissionObjective* Objective = Tracker && !Point.CompletesObjective.IsNone()
			? Tracker->FindObjective(Point.CompletesObjective) : nullptr;
		if (Objective && !Objective->IsCompleted())
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: Kate reached waypoint %d (%.0f cm): %s complete."), *GetName(), ReachedWaypoints - 1,
				Distances[ReachedWaypoints - 1], *Point.CompletesObjective.ToString());
			Missions->CompleteObjective(Point.CompletesObjective);
		}
		Say(Point.Sequence);
	}
	if (!bBeatDone && Route->BeatWaypoint >= 0 && State == EChaseState::Running
		&& UChaseRules::HasReached(KateAlong, GetWaypointProgress(Route->BeatWaypoint) + Route->Tuning.ReachTolerance, Route->Tuning))
	{
		bBeatDone = true;
		UE_LOG(LogHawkeye, Log, TEXT("%s: Kate is past the beat's gap without it (%.0f cm along): the beat will not play."), *GetName(),
			KateAlong);
	}
}

void UChaseSubsystem::UpdateLose(float DeltaSeconds, const APawn* Kate)
{
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Kate);
	const bool bHeld = State != EChaseState::Running || (Hawkeye && Hawkeye->IsDowned());
	LoseClock = UChaseRules::StepLoseClock(LoseClock, Lead, DeltaSeconds, bHeld, Route->Tuning);
	if (UChaseRules::IsLost(LoseClock, Route->Tuning))
	{
		Lose();
	}
}

void UChaseSubsystem::UpdateCatch(float DeltaSeconds)
{
	const AThugCharacter* Runner = Thief.Get();
	if (State != EChaseState::Caught)
	{
		const AThugAIController* Brain = Runner ? Cast<AThugAIController>(Runner->GetController()) : nullptr;
		const bool bCaught = Runner && UChaseRules::IsCaught(false, Runner->IsLimp(), Runner->IsKnockedDown() || (Brain && Brain->IsHeld()),
			!Runner->GetHealthComponent()->IsAlive());
		if (!bCaught || State == EChaseState::Beat || !UChaseRules::IsCatchable(State == EChaseState::Cornered))
		{
			return;
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: the thief is caught after %.1f s (%s; lead %.0f to %.0f cm over the chase)."), *GetName(),
			ChaseSeconds, Runner->IsLimp() ? TEXT("taken down") : TEXT("downed"), MinLead, MaxLead);
		SetState(EChaseState::Caught);
		CaughtSeconds = 0.f;
		Say(Route->CaughtSequence);
		return;
	}
	CaughtSeconds += DeltaSeconds;
	const UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this);
	const bool bSpoken = CaughtSeconds >= 0.5f && (!Dialogue || !Dialogue->IsBusy());
	if (bSkipCaughtHold || bSpoken || CaughtSeconds >= Route->Tuning.CaughtHoldSeconds)
	{
		CompleteCatch();
	}
}

void UChaseSubsystem::CompleteCatch()
{
	ClearMarker();
	UnbindKate();
	SetState(EChaseState::Idle);
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	for (const FName Id : Route->GetObjectives())
	{
		const UMissionObjective* Objective = Tracker ? Tracker->FindObjective(Id) : nullptr;
		if (Objective && !Objective->IsCompleted())
		{
			Missions->CompleteObjective(Id);
		}
	}
}

void UChaseSubsystem::Lose()
{
	UE_LOG(LogHawkeye, Log, TEXT("%s: lost the thief after %.1f s (lead %.0f cm for %.1f s); loading the last checkpoint."), *GetName(),
		ChaseSeconds, Lead, LoseClock);
	const UWorld* World = GetWorld();
	if (const UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(this))
	{
		if (UHawkeyeObjectiveWidget* Toasts = Hud->GetObjectiveMarker())
		{
			const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
			const UMissionObjective* Objective = Missions ? Missions->GetCurrentObjective() : nullptr;
			Toasts->PushToast(Route->LostHeading, Objective ? Objective->Title : FText::GetEmpty());
		}
	}
	UChaseRouteDefinition* Lost = Route;
	StopChase();
	Route = Lost;
	SetState(EChaseState::Lost);
	if (AHawkeyeGameMode* Mode = World ? Cast<AHawkeyeGameMode>(World->GetAuthGameMode()) : nullptr)
	{
		Mode->ReloadLastCheckpoint(1.5f);
	}
}

void UChaseSubsystem::UpdateMarker()
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Missions || !Thief.IsValid())
	{
		return;
	}
	for (const FName Id : Route->ThiefObjectives)
	{
		Missions->RegisterObjectiveLocation(Id, LastThiefFeet + FVector(0.f, 0.f, 100.f));
	}
}

void UChaseSubsystem::ClearMarker()
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Missions || !Route)
	{
		return;
	}
	for (const FName Id : Route->ThiefObjectives)
	{
		Missions->UnregisterObjectiveLocation(Id);
	}
}

// --- The beat ---------------------------------------------------------------------------------------------

void UChaseSubsystem::FaceAlongAlley(AHawkeyeCharacter* Kate) const
{
	APlayerController* PC = Kate ? Cast<APlayerController>(Kate->GetController()) : nullptr;
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}
	// Along the alley one way or the other: whichever leaves the camera's arm more room behind her.
	const FVector Eye = Route->BeatLanding + FVector(0.f, 0.f, 160.f);
	float BestYaw = Route->DumpsterYaw;
	float BestRoom = -1.f;
	for (const float Yaw : { Route->DumpsterYaw, Route->DumpsterYaw + 180.f })
	{
		const FVector Back = Eye - FRotator(0.f, Yaw, 0.f).Vector() * 450.f;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaseBeatCamera), false, Kate);
		if (Dumpster.IsValid())
		{
			Params.AddIgnoredActor(Dumpster.Get());
		}
		const float Room = World->LineTraceSingleByChannel(Hit, Eye, Back, ECC_Camera, Params) ? Hit.Distance : 450.f;
		if (Room > BestRoom)
		{
			BestRoom = Room;
			BestYaw = Yaw;
		}
	}
	PC->SetControlRotation(FRotator(-25.f, BestYaw, 0.f));
}

bool UChaseSubsystem::IsReplay() const
{
	const UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this);
	const UDialogueQueue* Queue = Dialogue ? Dialogue->GetQueue() : nullptr;
	return Queue && !Route->BotchSequence.IsNone() && Queue->HasPlayedSequence(Route->BotchSequence);
}

void UChaseSubsystem::UpdateBeatTrigger(AHawkeyeCharacter* Kate)
{
	const UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
	if (!Grapple || bBeatDone || State != EChaseState::Running)
	{
		bWasZipping = Grapple && Grapple->IsZipping();
		return;
	}
	const bool bZipping = Grapple->IsZipping();
	const bool bStarted = bZipping && !bWasZipping;
	bWasZipping = bZipping;
	if (!bStarted)
	{
		return;
	}
	const AGrappleAnchor* Anchor = Grapple->GetZipAnchor();
	const float AnchorDistance = Anchor ? FVector::Dist(Anchor->GetActorLocation(), Route->BeatAnchor) : BIG_NUMBER;
	const FVector StartFeet = Grapple->GetZipStart() - FVector(0.f, 0.f, HawkeyeChase::HalfHeightOf(Kate));
	const bool bInZone = Route->IsInBeatZone(StartFeet) || Route->IsInBeatZone(HawkeyeChase::FeetOf(Kate));
	UE_LOG(LogHawkeye, Log, TEXT("%s: a zip started %s the beat's zone at %s, %.0f cm from its anchor."), *GetName(),
		bInZone ? TEXT("in") : TEXT("outside"), *GetNameSafe(Anchor), AnchorDistance);
	if (UChaseRules::ShouldStartBeat(bBeatDone, bStarted, bInZone, AnchorDistance))
	{
		BeginBeat(Kate);
	}
}

void UChaseSubsystem::BeginBeat(AHawkeyeCharacter* Kate)
{
	SetState(EChaseState::Beat);
	BeatPhase = 1;
	BeatSeconds = BeatPhaseSeconds = 0.f;
	bBeatReplay = IsReplay();
	if (APlayerController* PC = Cast<APlayerController>(Kate->GetController()); PC && !bBeatInputIgnored)
	{
		PC->SetIgnoreMoveInput(true);
		bBeatInputIgnored = true;
	}
	Say(Route->BotchSequence);
	UE_LOG(LogHawkeye, Log, TEXT("%s: the beat: the grapple goes wrong (%s)."), *GetName(),
		bBeatReplay ? TEXT("a replay, skippable") : TEXT("first time"));
}

void UChaseSubsystem::StartBeatMove(AHawkeyeCharacter* Kate, const TArray<FVector>& Feet, float Seconds) const
{
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	FHawkeyeTraversalMove Move;
	Move.Seconds = Seconds;
	Move.Exit = EHawkeyeTraversalExit::Walk;
	const float HalfHeight = HawkeyeChase::HalfHeightOf(Kate);
	const float Yaw = Kate->GetActorRotation().Yaw;
	for (const FVector& Point : Feet)
	{
		FHawkeyeTraversalKey Key;
		Key.Location = Point + FVector(0.f, 0.f, HalfHeight);
		Key.Yaw = Yaw;
		Move.Keys.Add(Key);
	}
	if (Dumpster.IsValid())
	{
		Move.PassThrough.Add(Dumpster);
	}
	Movement->StartTraversal(Move);
}

void UChaseSubsystem::UpdateBeat(float DeltaSeconds, AHawkeyeCharacter* Kate)
{
	BeatSeconds += DeltaSeconds;
	BeatPhaseSeconds += DeltaSeconds;
	if (!Kate || UChaseRules::IsBeatStuck(BeatSeconds))
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: the beat ran %.1f s (phase %d); putting Kate on its exit."), *GetName(), BeatSeconds, BeatPhase);
		EndBeat(Kate, /*bForced=*/true);
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(Kate->GetController());
	if (PC && UChaseRules::CanSkipBeat(bBeatReplay, BeatSeconds) && BeatPhase >= 1
		&& (PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: the beat skipped at %.1f s (a replay)."), *GetName(), BeatSeconds);
		EndBeat(Kate, /*bForced=*/true);
		return;
	}
	const FVector Feet = HawkeyeChase::FeetOf(Kate);
	if (BeatPhase == 1 && BeatPhaseSeconds >= HawkeyeChase::SnapSeconds)
	{
		// The line lets go: a short drop that eases into a fall, straight into the paper.
		TArray<FVector> Path;
		const FVector Landing = Route->BeatLanding;
		for (int32 Step = 0; Step <= 10; ++Step)
		{
			const float T = Step / 10.f;
			const FVector Flat = FMath::Lerp(Feet, Landing, FMath::Sin(T * HALF_PI));
			Path.Add(FVector(Flat.X, Flat.Y, FMath::Lerp(Feet.Z, Landing.Z, T * T)));
		}
		StartBeatMove(Kate, Path, HawkeyeChase::FallSeconds);
		FaceAlongAlley(Kate);
		BeatPhase = 2;
		BeatPhaseSeconds = 0.f;
	}
	else if (BeatPhase == 2 && BeatPhaseSeconds >= HawkeyeChase::FallSeconds)
	{
		UHawkeyeVfxSubsystem::SpawnAt(this, Route->PaperVfx, Route->BeatLanding + FVector(0.f, 0.f, 60.f), FRotator(90.f, 0.f, 0.f),
			TEXT("ChasePaper"));
		Say(Route->DumpsterSequence);
		StartBeatMove(Kate, { Route->BeatLanding, Route->BeatLanding - FVector(0.f, 0.f, 15.f), Route->BeatLanding },
			HawkeyeChase::SitSeconds);
		BeatPhase = 3;
		BeatPhaseSeconds = 0.f;
	}
	else if (BeatPhase == 3 && BeatPhaseSeconds >= HawkeyeChase::SitSeconds)
	{
		const FVector Exit = Route->BeatExit;
		const FVector Rim = FMath::Lerp(Route->BeatLanding, Exit, 0.5f) + FVector(0.f, 0.f, 120.f);
		Say(Route->ClimbOutSequence);
		StartBeatMove(Kate, { Route->BeatLanding, Route->BeatLanding + FVector(0.f, 0.f, 90.f), Rim, Exit + FVector(0.f, 0.f, 30.f), Exit },
			HawkeyeChase::ClimbSeconds);
		BeatPhase = 4;
		BeatPhaseSeconds = 0.f;
	}
	else if (BeatPhase == 4 && BeatPhaseSeconds >= HawkeyeChase::ClimbSeconds + 0.05f)
	{
		EndBeat(Kate, /*bForced=*/false);
	}
}

void UChaseSubsystem::EndBeat(AHawkeyeCharacter* Kate, bool bForced)
{
	if (Kate && bForced)
	{
		Kate->TeleportTo(Route->BeatExit + FVector(0.f, 0.f, HawkeyeChase::HalfHeightOf(Kate) + 2.f), Kate->GetActorRotation(), false, true);
		Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Kate->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}
	if (APlayerController* PC = Kate ? Cast<APlayerController>(Kate->GetController()) : nullptr; PC && bBeatInputIgnored)
	{
		PC->SetIgnoreMoveInput(false);
	}
	bBeatInputIgnored = false;
	const bool bWasPlaying = BeatPhase != 0;
	BeatPhase = 0;
	bBeatDone = true;
	bBeatPlayed = bWasPlaying && !bForced;
	if (!bWasPlaying || !Route || State != EChaseState::Beat)
	{
		return;
	}
	// The one placement of the chase: the thief a fixed lead ahead of where she came down.
	KateAlong = FMath::Max(KateAlong, GetWaypointProgress(Route->BeatWaypoint));
	bHaveKateFeet = false;
	ThiefAlong = FMath::Min(KateAlong + Route->Tuning.ResumeLead, GetRouteLength());
	SyncSegment();
	LastThiefFeet = UChaseRules::PointAt(Points, Distances, ThiefAlong);
	ThiefSpeed = Route->Tuning.BaseSpeed;
	LoseClock = 0.f;
	Lead = Route->Tuning.ResumeLead;
	SetState(EChaseState::Running);
	UE_LOG(LogHawkeye, Log, TEXT("%s: the beat is over after %.1f s%s; the thief is %.0f cm ahead."), *GetName(), BeatSeconds,
		bForced ? TEXT(" (cut short)") : TEXT(""), Route->Tuning.ResumeLead);
}

// --- Save -------------------------------------------------------------------------------------------------

void UChaseSubsystem::MirrorToCampaign() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (!Campaign)
	{
		return;
	}
	const bool bRunning = Route && (State == EChaseState::Running || State == EChaseState::Beat || State == EChaseState::Cornered);
	Campaign->ChaseRoutePath = bRunning ? FSoftObjectPath(Route).ToString() : FString();
	Campaign->ChaseThiefProgress = bRunning ? ThiefAlong : 0.f;
	Campaign->ChaseKateProgress = bRunning ? KateAlong : 0.f;
	Campaign->ChaseReachedWaypoints = bRunning ? ReachedWaypoints : 0;
	Campaign->bChaseBeatDone = bRunning && bBeatDone;
	Campaign->bChasePulledAhead = bRunning && bPulledAhead;
	Campaign->bChaseVaulted = bRunning && bVaulted;
}

void UChaseSubsystem::RestoreFromCampaign()
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (!Campaign || Campaign->ChaseRoutePath.IsEmpty())
	{
		return;
	}
	UChaseRouteDefinition* Saved = Cast<UChaseRouteDefinition>(FSoftObjectPath(Campaign->ChaseRoutePath).TryLoad());
	if (!Saved || !StartChase(Saved, /*bPlaceKate=*/false))
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: the save's chase %s could not be picked up."), *GetName(), *Campaign->ChaseRoutePath);
		return;
	}
	KateAlong = Campaign->ChaseKateProgress;
	ThiefAlong = FMath::Min(UChaseRules::RestoredThiefProgress(Campaign->ChaseThiefProgress, KateAlong, Route->Tuning), GetRouteLength());
	ReachedWaypoints = Campaign->ChaseReachedWaypoints;
	bBeatDone = Campaign->bChaseBeatDone || !Route->bHasBeat;
	bPulledAhead = Campaign->bChasePulledAhead;
	bVaulted = Campaign->bChaseVaulted;
	LoseClock = 0.f;
	SyncSegment();
	LastThiefFeet = UChaseRules::PointAt(Points, Distances, ThiefAlong);
	Lead = FMath::Max(ThiefAlong - KateAlong, 0.f);
	MinLead = MaxLead = Lead;
	MoveThief(LastThiefFeet, Thief.IsValid() ? Thief->GetActorRotation().Yaw : 0.f, 0.f);
	UE_LOG(LogHawkeye, Log, TEXT("%s: chase picked up from the save: thief %.0f (saved %.0f), Kate %.0f of %.0f cm, %d waypoint(s) reached."),
		*GetName(), ThiefAlong, Campaign->ChaseThiefProgress, KateAlong, GetRouteLength(), ReachedWaypoints);
}

void UChaseSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		RestoreFromCampaign();
	}
}
