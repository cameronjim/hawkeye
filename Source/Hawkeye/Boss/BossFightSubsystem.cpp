#include "Boss/BossFightSubsystem.h"

#include "Hawkeye.h"
#include "Boss/BossArenaHazard.h"
#include "Boss/BossDefinition.h"
#include "Combat/BossPhaseComponent.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "NavigationSystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/DifficultySubsystem.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

const FName UBossFightSubsystem::BossTag(TEXT("Boss"));
const FName UBossFightSubsystem::CrewTag(TEXT("BossCrew"));
const FName UBossFightSubsystem::FurnitureTag(TEXT("BossFurniture"));

namespace HawkeyeBossFight
{
	static float HalfHeightOf(const APawn* Pawn)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	}

	static FVector FeetOf(const APawn* Pawn)
	{
		return Pawn->GetActorLocation() - FVector(0.f, 0.f, HalfHeightOf(Pawn));
	}

	static FString BarkKey(int32 Phase, EBossBarkEvent Event)
	{
		return FString::Printf(TEXT("%d:%s"), Phase, *UEnum::GetValueAsString(Event).RightChop(16));
	}

	/** A dodge within this of the boss, while he swings, is one he notices, cm. */
	static constexpr float DodgeNoticeDistance = 450.f;
}

UBossFightSubsystem* UBossFightSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UBossFightSubsystem>() : nullptr;
}

bool UBossFightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UBossFightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UMissionSubsystem>();
	Super::Initialize(Collection);
}

void UBossFightSubsystem::Deinitialize()
{
	OnStateChanged.Clear();
	OnPhaseStarted.Clear();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UBossFightSubsystem::HandleCampaignLoaded);
	}
	UnbindKate();
	Definition = nullptr;
	Super::Deinitialize();
}

void UBossFightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UBossFightSubsystem::HandleCampaignLoaded);
	}
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->OnMissionStarted.AddUniqueDynamic(this, &UBossFightSubsystem::HandleMissionStarted);
	}
}

TStatId UBossFightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBossFightSubsystem, STATGROUP_Tickables);
}

APawn* UBossFightSubsystem::FindKate() const
{
	if (PlayerOverride.IsValid())
	{
		return PlayerOverride.Get();
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

void UBossFightSubsystem::SetState(EBossFightState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: boss fight %s -> %s at %.1f s (phase %d)."), *GetName(), *UEnum::GetValueAsString(State),
		*UEnum::GetValueAsString(NewState), FightSeconds, PhaseIndex);
	State = NewState;
	OnStateChanged.Broadcast(NewState);
}

UBossPhaseComponent* UBossFightSubsystem::GetPhaseComponent() const
{
	return Boss.IsValid() ? Boss->FindComponentByClass<UBossPhaseComponent>() : nullptr;
}

TArray<AThugCharacter*> UBossFightSubsystem::GetCrew() const
{
	TArray<AThugCharacter*> Out;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Crew)
	{
		if (AThugCharacter* Thug = Weak.Get())
		{
			Out.Add(Thug);
		}
	}
	return Out;
}

bool UBossFightSubsystem::IsDown(const AThugCharacter* Thug) const
{
	return !Thug || Thug->IsLimp() || !Thug->GetHealthComponent() || Thug->GetHealthComponent()->IsDead();
}

// --- Starting and stopping -------------------------------------------------------------------------------

void UBossFightSubsystem::HandleMissionStarted(UMissionDefinition* Mission)
{
	UBossDefinition* Wanted = Mission ? Mission->Boss.LoadSynchronous() : nullptr;
	if (!Wanted)
	{
		if (State != EBossFightState::Idle)
		{
			StopFight();
		}
		return;
	}
	SetUpFight(Wanted);
}

bool UBossFightSubsystem::BeginChapter(const UMissionDefinition* Mission)
{
	UBossDefinition* Wanted = Mission ? Mission->Boss.LoadSynchronous() : nullptr;
	if (!Wanted)
	{
		return false;
	}
	if (Definition != Wanted || State == EBossFightState::Idle)
	{
		SetUpFight(Wanted);
	}
	APawn* Kate = FindKate();
	const FBossArena& Arena = Wanted->Arena;
	if (!Kate || Arena.PlayerStart.IsZero())
	{
		return true;
	}
	const FRotator Facing(0.f, Arena.PlayerStartYaw, 0.f);
	Kate->TeleportTo(Arena.PlayerStart + FVector(0.f, 0.f, HawkeyeBossFight::HalfHeightOf(Kate) + 2.f), Facing, false, true);
	if (AController* Controller = Kate->GetController())
	{
		// Up at the roof: the parapet and its anchor in view.
		const FVector To = Arena.ApproachAnchor - Kate->GetActorLocation();
		Controller->SetControlRotation(FRotator(FMath::Clamp(To.Rotation().Pitch, -10.f, 40.f), Arena.PlayerStartYaw, 0.f));
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: Kate put on the arena's street stand %s, yaw %.0f."), *GetName(), *Arena.PlayerStart.ToCompactString(),
		Arena.PlayerStartYaw);
	return true;
}

bool UBossFightSubsystem::SetUpFight(UBossDefinition* InDefinition)
{
	if (!InDefinition || InDefinition->Phases.Num() == 0)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no boss or no phases to set up."), *GetName());
		return false;
	}
	if (!UBossRules::IsPhaseOrderValid(InDefinition->GetPhaseRules()))
	{
		UE_LOG(LogHawkeye, Error, TEXT("%s: %s's phase thresholds rise; phases must be authored highest first."), *GetName(),
			*InDefinition->GetName());
		return false;
	}
	StopFight();
	Definition = InDefinition;
	PhaseIndex = INDEX_NONE;
	FightSeconds = 0.f;
	PhaseStarts.Reset();
	BarkCounts.Reset();
	BarkLastSeconds.Reset();
	BarksSpoken.Reset();
	LinesSpoken.Reset();
	EnsureFurniture();
	SpawnBoss();
	SpawnCrew(0);
	SetState(EBossFightState::Approach);
	UpdateMarker();
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s waits on roof %s with %d of his crew."), *GetName(), *Definition->BossName.ToString(),
		*Definition->Arena.RoofId.ToString(), Crew.Num());
	return true;
}

void UBossFightSubsystem::StopFight()
{
	ClearCrew();
	ClearBoss();
	ClearFurniture();
	UnbindKate();
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && Definition)
	{
		Missions->UnregisterObjectiveLocation(Definition->MeetObjective);
		for (const FName Id : Definition->GetPhaseObjectives())
		{
			Missions->UnregisterObjectiveLocation(Id);
		}
	}
	Combo = FBossComboClock();
	PhaseIndex = INDEX_NONE;
	SetState(EBossFightState::Idle);
}

void UBossFightSubsystem::ClearCrew()
{
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Crew)
	{
		if (AThugCharacter* Thug = Weak.Get())
		{
			if (AController* Brain = Thug->GetController())
			{
				Brain->UnPossess();
				Brain->Destroy();
			}
			Thug->Destroy();
		}
	}
	Crew.Reset();
	CrewWasDown.Reset();
	CrewWasParried.Reset();
	CrewWasWindingUp.Reset();
}

void UBossFightSubsystem::ClearBoss()
{
	if (AThugCharacter* Old = Boss.Get())
	{
		if (UHealthComponent* Health = Old->GetHealthComponent())
		{
			Health->OnHealthChanged.RemoveDynamic(this, &UBossFightSubsystem::HandleBossHealthChanged);
			Health->OnDeath.RemoveDynamic(this, &UBossFightSubsystem::HandleBossDeath);
		}
		if (UBossPhaseComponent* Phases = Old->FindComponentByClass<UBossPhaseComponent>())
		{
			Phases->OnPhaseChanged.RemoveDynamic(this, &UBossFightSubsystem::HandlePhaseChanged);
		}
		if (AController* Brain = Old->GetController())
		{
			Brain->UnPossess();
			Brain->Destroy();
		}
		Old->Destroy();
	}
	Boss.Reset();
}

void UBossFightSubsystem::ClearFurniture()
{
	for (const TWeakObjectPtr<AActor>& Weak : Furniture)
	{
		if (AActor* Actor = Weak.Get())
		{
			Actor->Destroy();
		}
	}
	Furniture.Reset();
	if (ABossArenaHazard* Old = Hazard.Get())
	{
		Old->Destroy();
	}
	Hazard.Reset();
}

void UBossFightSubsystem::EnsureFurniture()
{
	UWorld* World = GetWorld();
	if (!World || !Definition)
	{
		return;
	}
	const FBossArena& Arena = Definition->Arena;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FBossArenaProp& Prop : Arena.Props)
	{
		UStaticMesh* Mesh = Prop.Mesh.LoadSynchronous();
		AStaticMeshActor* Actor = Mesh ? World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Prop.Transform, Params) : nullptr;
		if (!Actor)
		{
			continue;
		}
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionProfileName(Prop.bBlocks ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		// Furniture never pushes the camera into her back.
		Component->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Actor->SetActorScale3D(Prop.Transform.GetScale3D());
		Actor->Tags.Add(FurnitureTag);
		Furniture.Add(Actor);
	}
	for (const FBossArenaLight& Bulb : Arena.Lights)
	{
		APointLight* Lamp = World->SpawnActor<APointLight>(Bulb.Location, FRotator::ZeroRotator, Params);
		if (!Lamp)
		{
			continue;
		}
		UPointLightComponent* Light = Cast<UPointLightComponent>(Lamp->GetLightComponent());
		Lamp->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Bulb.Lumens);
		Light->SetAttenuationRadius(Bulb.Radius);
		Light->SetLightColor(Bulb.Color);
		Light->SetCastShadows(false);
		Lamp->Tags.Add(FurnitureTag);
		Furniture.Add(Lamp);
	}
	if (!Arena.HazardStart.IsZero() || !Arena.HazardEnd.IsZero())
	{
		ABossArenaHazard* Body = World->SpawnActor<ABossArenaHazard>(ABossArenaHazard::StaticClass(), Arena.HazardStart, FRotator::ZeroRotator, Params);
		if (Body)
		{
			Body->Configure(Arena.HazardStart, Arena.HazardEnd, Arena.HazardMesh.LoadSynchronous(), Arena.HazardLaneMaterial.LoadSynchronous(),
				Definition->Hazard);
			Body->TargetOverride = PlayerOverride;
			Body->Tags.Add(FurnitureTag);
			Hazard = Body;
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: arena dressed: %d prop(s), %d light(s), %s."), *GetName(), Arena.Props.Num(), Arena.Lights.Num(),
		Hazard.IsValid() ? TEXT("the hazard tethered") : TEXT("no hazard"));
}

AThugCharacter* UBossFightSubsystem::SpawnThug(UClass* Class, const FVector& Feet, float Yaw, EThugWeapon Weapon, float Scale) const
{
	UWorld* World = GetWorld();
	Class = Class ? Class : AThugCharacter::StaticClass();
	const float HalfHeight = Class->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * Scale;
	FVector Ground = Feet;
	if (const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr)
	{
		FNavLocation OnNav;
		if (Nav->ProjectPointToNavigation(Feet, OnNav, FVector(150.f, 150.f, 200.f)))
		{
			Ground = OnNav.Location;
		}
	}
	const FTransform At(FRotator(0.f, Yaw, 0.f), Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FVector(Scale));
	AThugCharacter* Thug = World ? World->SpawnActorDeferred<AThugCharacter>(Class, At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn) : nullptr;
	if (!Thug)
	{
		return nullptr;
	}
	Thug->Weapon = Weapon;
	// The fight is restarted from its checkpoint, never from SPUD's copy of who was standing.
	Thug->bSkipSave = true;
	Thug->FinishSpawning(At);
	if (!Cast<AThugAIController>(Thug->GetController()))
	{
		if (AController* Other = Thug->GetController())
		{
			Other->UnPossess();
			Other->Destroy();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AThugAIController* Brain = World->SpawnActor<AThugAIController>(AThugAIController::StaticClass(), At, Params))
		{
			Brain->Possess(Thug);
		}
	}
	return Thug;
}

AThugCharacter* UBossFightSubsystem::SpawnBoss()
{
	if (!Definition)
	{
		return nullptr;
	}
	ClearBoss();
	const FBossArena& Arena = Definition->Arena;
	UClass* Class = Definition->BossClass.IsNull() ? nullptr : Definition->BossClass.LoadSynchronous();
	AThugCharacter* Him = SpawnThug(Class, Arena.BossSpot, Arena.BossYaw, EThugWeapon::Fists, Definition->BodyScale);
	if (!Him)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: could not spawn %s."), *GetName(), *Definition->BossName.ToString());
		return nullptr;
	}
	Him->Tags.AddUnique(BossTag);
	Him->bRefusesTakedown = true;
	Him->ComboAttacks = Definition->Moves.Combo;
	Him->FinisherDamageFraction = Definition->Moves.FinisherDamageFraction;
	Him->bResistsKnockdown = Definition->Moves.bResistsKnockdown;
	UHealthComponent* Health = Him->GetHealthComponent();
	const float HealthScale = UDifficultySubsystem::GetScalarFor(this, EDifficultyStat::ThugHealth);
	Health->SetMaxHealth(Definition->MaxHealth * HealthScale, /*bResetCurrent=*/true);
	Health->StaggerThreshold = Definition->Moves.StaggerThreshold;
	Health->OnHealthChanged.AddUniqueDynamic(this, &UBossFightSubsystem::HandleBossHealthChanged);
	Health->OnDeath.AddUniqueDynamic(this, &UBossFightSubsystem::HandleBossDeath);
	if (AThugAIController* Brain = Cast<AThugAIController>(Him->GetController()))
	{
		Brain->MeleeCooldownSeconds = Definition->Moves.SwingCooldownSeconds;
		// He does not step back after two swings: his combo ends winded instead.
		Brain->MeleeSwingsBeforeBackOff = 1000;
		Brain->RushSpeed = Definition->Moves.RushSpeed;
	}
	// The phases ride on his health; the component is added here so a boss needs no class of his own.
	UBossPhaseComponent* Phases = NewObject<UBossPhaseComponent>(Him, TEXT("BossPhases"));
	Phases->Phases = Definition->GetPhaseRules();
	Phases->RegisterComponent();
	Phases->Bind(Health);
	Phases->OnPhaseChanged.AddUniqueDynamic(this, &UBossFightSubsystem::HandlePhaseChanged);
	SetFighting(Him, false, FindKate());
	Boss = Him;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s (%s) at %s, %.0f health, scale %.2f."), *GetName(), *Definition->BossName.ToString(), *Him->GetName(),
		*Him->GetActorLocation().ToCompactString(), Health->GetMaxHealth(), Definition->BodyScale);
	return Him;
}

void UBossFightSubsystem::SpawnCrew(int32 Index)
{
	ClearCrew();
	if (!Definition || !Definition->Phases.IsValidIndex(Index))
	{
		return;
	}
	const APawn* Kate = FindKate();
	for (const FBossCrewMember& Member : Definition->Phases[Index].Crew)
	{
		UClass* Class = Member.ThugClass.IsNull() ? nullptr : Member.ThugClass.LoadSynchronous();
		const FVector Look = (Kate ? Kate->GetActorLocation() : Definition->Arena.Centre) - Member.Location;
		AThugCharacter* Thug = SpawnThug(Class, Member.Location, Look.Rotation().Yaw, Member.Weapon, 1.f);
		if (!Thug)
		{
			continue;
		}
		Thug->Tags.AddUnique(CrewTag);
		SetFighting(Thug, false, nullptr);
		Crew.Add(Thug);
		CrewWasDown.Add(false);
		CrewWasParried.Add(false);
		CrewWasWindingUp.Add(false);
	}
}

void UBossFightSubsystem::SetFighting(AThugCharacter* Thug, bool bFight, APawn* Kate) const
{
	AThugAIController* Brain = Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr;
	if (!Brain)
	{
		return;
	}
	Brain->SetThinkingEnabled(bFight);
	if (bFight && Kate)
	{
		Brain->SetTarget(Kate);
		Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
		Thug->SetAlertState(EThugAlertState::Alerted);
	}
	else if (!bFight)
	{
		Brain->StopMovement();
	}
}

void UBossFightSubsystem::FaceKate(AThugCharacter* Thug, const APawn* Kate) const
{
	if (!Thug || !Kate || IsDown(Thug))
	{
		return;
	}
	const FVector To = (Kate->GetActorLocation() - Thug->GetActorLocation()).GetSafeNormal2D();
	if (!To.IsNearlyZero())
	{
		Thug->SetActorRotation(FMath::RInterpTo(Thug->GetActorRotation(), FRotator(0.f, To.Rotation().Yaw, 0.f), GetWorld()->GetDeltaSeconds(), 4.f));
	}
}

// --- The fight -------------------------------------------------------------------------------------------

void UBossFightSubsystem::BeginIntro()
{
	IntroSeconds = 0.f;
	SetState(EBossFightState::Intro);
	if (!bSkipLines && !Definition->PreFightSequence.IsNone())
	{
		if (UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this))
		{
			Dialogue->PlaySequence(Definition->PreFightSequence);
		}
	}
}

void UBossFightSubsystem::StartFight()
{
	FightSeconds = 0.f;
	PhaseStarts.Reset();
	SetState(EBossFightState::Fight);
	StartPhase(0, /*bRestore=*/false);
}

void UBossFightSubsystem::StartPhase(int32 Index, bool bRestore)
{
	if (!Definition || !Definition->Phases.IsValidIndex(Index))
	{
		return;
	}
	const FBossPhaseDefinition& Phase = Definition->Phases[Index];
	APawn* Kate = FindKate();
	PhaseIndex = Index;
	PhaseSeconds = 0.f;
	PhaseStarts.Add(FightSeconds);
	bBossWasParried = false;
	// The first phase's crew stands on the roof from the approach; a later phase's arrive now.
	if (Index != 0 || Crew.Num() == 0 || bRestore)
	{
		SpawnCrew(Index);
	}
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Crew)
	{
		SetFighting(Weak.Get(), true, Kate);
	}
	if (AThugCharacter* Him = Boss.Get())
	{
		SetFighting(Him, Phase.bBossFights, Kate);
		const AThugAIController* Brain = Cast<AThugAIController>(Him->GetController());
		Combo.Reset(Brain ? Brain->GetSwingsStarted() : 0);
	}
	if (ABossArenaHazard* Body = Hazard.Get())
	{
		if (Phase.bHazardActive && !Body->IsLoose())
		{
			Body->TargetOverride = PlayerOverride;
			Body->SetLoose(true);
		}
	}
	GrantSlot = 0;
	GrantCount = 0;
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Kate);
	if (UInventoryComponent* Inventory = Hawkeye ? Hawkeye->GetInventoryComponent() : nullptr)
	{
		for (const FHawkeyeArrowGrant& Grant : Phase.ArrowGrants)
		{
			if (UArrowDefinition* Arrow = Grant.Arrow.LoadSynchronous())
			{
				Inventory->AddArrows(Arrow, Grant.Count);
				GrantSlot = Arrow->Slot;
				GrantCount = Inventory->GetArrowCount(GrantSlot);
			}
		}
	}
	Say(Phase.StartLines);
	UpdateMarker();
	UE_LOG(LogHawkeye, Log, TEXT("%s: phase %d ('%s') %s at %.1f s: %d crew, boss %s, hazard %s."), *GetName(), Index,
		*Phase.Phase.PhaseName.ToString(), bRestore ? TEXT("restarts") : TEXT("starts"), FightSeconds, Crew.Num(),
		Phase.bBossFights ? TEXT("fights") : TEXT("watches"), Phase.bHazardActive ? TEXT("loose") : TEXT("tethered"));
	OnPhaseStarted.Broadcast(Index);
}

void UBossFightSubsystem::HandlePhaseChanged(int32 OldPhaseIndex, int32 NewPhaseIndex, FBossPhase /*Phase*/)
{
	if (!Definition || State != EBossFightState::Fight)
	{
		return;
	}
	// Every phase a big hit skipped is behind her too.
	for (int32 Index = FMath::Max(OldPhaseIndex, 0); Index < NewPhaseIndex && Definition->Phases.IsValidIndex(Index); ++Index)
	{
		Say(Definition->Phases[Index].EndLines);
		// Completed before the next phase grants anything: the objective's autosave is the next phase's checkpoint.
		CompleteObjective(Definition->Phases[Index].ObjectiveId);
	}
	StartPhase(NewPhaseIndex, /*bRestore=*/false);
}

void UBossFightSubsystem::HandleBossHealthChanged(UHealthComponent* Health, float /*NewHealth*/, float Delta, AActor* DamageInstigator)
{
	if (bApplyingBonus || !Definition || !Health || Delta >= 0.f || !Health->IsAlive())
	{
		return;
	}
	const float Bonus = UBossRules::WindedBonusDamage(-Delta, Combo.IsWinded(), Definition->Moves.WindedDamageScale);
	if (Bonus > 0.f)
	{
		TGuardValue<bool> Guard(bApplyingBonus, true);
		Health->ApplyDamage(Bonus, DamageInstigator);
	}
}

void UBossFightSubsystem::HandleBossDeath(UHealthComponent* /*Health*/, AActor* Killer)
{
	if (!Definition || (State != EBossFightState::Fight && State != EBossFightState::Intro && State != EBossFightState::Approach))
	{
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s is down (by %s) in phase %d at %.1f s."), *GetName(), *Definition->BossName.ToString(),
		*GetNameSafe(Killer), PhaseIndex, FightSeconds);
	if (ABossArenaHazard* Body = Hazard.Get())
	{
		Body->Settle();
	}
	if (Definition->Phases.IsValidIndex(PhaseIndex))
	{
		Say(Definition->Phases[PhaseIndex].EndLines);
	}
	DefeatedSeconds = 0.f;
	SetState(EBossFightState::Defeated);
	if (!bSkipLines && !Definition->AfterSequence.IsNone())
	{
		if (UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this))
		{
			Dialogue->PlaySequence(Definition->AfterSequence);
		}
	}
}

void UBossFightSubsystem::HandleKateHealthChanged(UHealthComponent* /*Health*/, float /*NewHealth*/, float Delta, AActor* DamageInstigator)
{
	if (Delta < 0.f && State == EBossFightState::Fight && DamageInstigator && DamageInstigator == Boss.Get())
	{
		Bark(EBossBarkEvent::BossHitsPlayer);
	}
}

void UBossFightSubsystem::HandleFinisherStruck(AActor* Target)
{
	if (State != EBossFightState::Fight || !Target)
	{
		return;
	}
	if (Target == Boss.Get())
	{
		Bark(EBossBarkEvent::FinisherOnBoss);
		// One finisher a window: the next has to be earned by another combo.
		Combo.WindedRemaining = 0.f;
		Boss->CloseFinisherWindow();
	}
}

void UBossFightSubsystem::BindKate(APawn* Kate)
{
	if (BoundKate.Get() == Kate)
	{
		return;
	}
	UnbindKate();
	if (!Kate)
	{
		return;
	}
	BoundKate = Kate;
	if (UHealthComponent* Health = Kate->FindComponentByClass<UHealthComponent>())
	{
		Health->OnHealthChanged.AddUniqueDynamic(this, &UBossFightSubsystem::HandleKateHealthChanged);
	}
	if (UFinisherComponent* Finisher = Kate->FindComponentByClass<UFinisherComponent>())
	{
		Finisher->OnFinisherStruck.AddUniqueDynamic(this, &UBossFightSubsystem::HandleFinisherStruck);
	}
}

void UBossFightSubsystem::UnbindKate()
{
	if (APawn* Kate = BoundKate.Get())
	{
		if (UHealthComponent* Health = Kate->FindComponentByClass<UHealthComponent>())
		{
			Health->OnHealthChanged.RemoveDynamic(this, &UBossFightSubsystem::HandleKateHealthChanged);
		}
		if (UFinisherComponent* Finisher = Kate->FindComponentByClass<UFinisherComponent>())
		{
			Finisher->OnFinisherStruck.RemoveDynamic(this, &UBossFightSubsystem::HandleFinisherStruck);
		}
	}
	BoundKate.Reset();
}

void UBossFightSubsystem::Tick(float DeltaTime)
{
	if (State == EBossFightState::Idle || State == EBossFightState::Done || !Definition)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World || World->IsPaused())
	{
		return;
	}
	APawn* Kate = FindKate();
	BindKate(Kate);
	switch (State)
	{
	case EBossFightState::Approach:
		UpdateApproach(Kate);
		break;
	case EBossFightState::Intro:
		UpdateIntro(DeltaTime, Kate);
		break;
	case EBossFightState::Fight:
		UpdateFight(DeltaTime, Kate);
		break;
	case EBossFightState::Defeated:
		UpdateDefeated(DeltaTime);
		break;
	default:
		break;
	}
}

void UBossFightSubsystem::UpdateApproach(APawn* Kate)
{
	FaceKate(Boss.Get(), Kate);
	if (const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this); Save && Save->IsLoading())
	{
		return;
	}
	if (!Kate || !UBossRules::IsOnRoof(Definition->Arena.Outline, Definition->Arena.RoofZ, HawkeyeBossFight::FeetOf(Kate)))
	{
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: Kate is on the roof at %s."), *GetName(), *Kate->GetActorLocation().ToCompactString());
	CompleteObjective(Definition->MeetObjective);
	BeginIntro();
}

void UBossFightSubsystem::UpdateIntro(float DeltaSeconds, APawn* Kate)
{
	IntroSeconds += DeltaSeconds;
	FaceKate(Boss.Get(), Kate);
	bool bSomeoneHit = false;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Crew)
	{
		const AThugCharacter* Thug = Weak.Get();
		const UHealthComponent* Health = Thug ? Thug->GetHealthComponent() : nullptr;
		bSomeoneHit |= !Health || Health->GetCurrentHealth() < Health->GetMaxHealth();
	}
	const UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this);
	const bool bBusy = !bSkipLines && Dialogue && Dialogue->IsBusy();
	if (bSkipLines || UBossRules::IsIntroOver(IntroSeconds, bBusy, bSomeoneHit, Definition->IntroMaxSeconds))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: the intro ends after %.1f s (%s)."), *GetName(), IntroSeconds,
			bSomeoneHit ? TEXT("she hit someone") : (bBusy ? TEXT("too long") : TEXT("the lines are done")));
		StartFight();
	}
}

void UBossFightSubsystem::UpdateFight(float DeltaSeconds, APawn* Kate)
{
	FightSeconds += DeltaSeconds;
	PhaseSeconds += DeltaSeconds;
	if (!Definition->Phases.IsValidIndex(PhaseIndex))
	{
		return;
	}
	const FBossPhaseDefinition& Phase = Definition->Phases[PhaseIndex];
	AThugCharacter* Him = Boss.Get();
	if (Him && !Phase.bBossFights)
	{
		FaceKate(Him, Kate);
	}
	// The boss's combo and the winded window after it.
	if (Him && Phase.bBossFights && !IsDown(Him))
	{
		const AThugAIController* Brain = Cast<AThugAIController>(Him->GetController());
		const UMeleeComponent* Melee = Him->GetMeleeComponent();
		const bool bWasWinded = Combo.IsWinded();
		if (Combo.Step(DeltaSeconds, Brain ? Brain->GetSwingsStarted() : 0, Melee && Melee->IsAttacking(), Definition->Moves))
		{
			Him->OpenFinisherWindow(Definition->Moves.WindedSeconds);
			UE_LOG(LogHawkeye, Log, TEXT("%s: %s is winded for %.1f s (the %d time) at %.1f s."), *GetName(), *Him->GetName(),
				Definition->Moves.WindedSeconds, Combo.WindedCount, FightSeconds);
			Bark(EBossBarkEvent::Winded);
		}
		if (Combo.IsWinded())
		{
			// He stands there getting his breath back, whatever hits him.
			Him->HoldStagger(Combo.WindedRemaining);
		}
		else if (bWasWinded)
		{
			Him->CloseFinisherWindow();
		}
	}
	// The crew: who went down, and when the last one does, the next phase.
	bool bAllDown = Crew.Num() > 0;
	for (int32 Index = 0; Index < Crew.Num(); ++Index)
	{
		const bool bDown = IsDown(Crew[Index].Get());
		if (bDown && !CrewWasDown[Index])
		{
			CrewWasDown[Index] = true;
			Bark(EBossBarkEvent::CrewMemberDown);
		}
		bAllDown &= bDown;
	}
	UpdateBarks(Kate);
	if (bAllDown && Phase.Crew.Num() > 0)
	{
		if (UBossPhaseComponent* Phases = GetPhaseComponent())
		{
			Phases->NotifyCrewDown();
		}
	}
	UpdateMarker();
}

void UBossFightSubsystem::UpdateBarks(APawn* Kate)
{
	AThugCharacter* Him = Boss.Get();
	for (int32 Index = 0; Index < Crew.Num(); ++Index)
	{
		const AThugCharacter* Thug = Crew[Index].Get();
		if (IsDown(Thug))
		{
			continue;
		}
		const bool bParried = Thug->IsParryStaggered();
		if (bParried && !CrewWasParried[Index])
		{
			Bark(EBossBarkEvent::PlayerParries);
		}
		CrewWasParried[Index] = bParried;
		const UMeleeComponent* Melee = Thug->GetMeleeComponent();
		const bool bWinding = Melee && Melee->IsWindingUp();
		if (bWinding && !CrewWasWindingUp[Index] && Thug->Weapon == EThugWeapon::Bat)
		{
			Bark(EBossBarkEvent::CrewSwings);
		}
		CrewWasWindingUp[Index] = bWinding;
	}
	if (Him && !IsDown(Him))
	{
		const bool bParried = Him->IsParryStaggered();
		if (bParried && !bBossWasParried)
		{
			Bark(EBossBarkEvent::PlayerParries);
		}
		bBossWasParried = bParried;
	}
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Kate);
	if (Hawkeye)
	{
		const bool bDodging = Hawkeye->IsDodging();
		const UMeleeComponent* Melee = Him ? Him->GetMeleeComponent() : nullptr;
		if (bDodging && !bKateWasDodging && Melee && Melee->IsAttacking()
			&& FVector::Dist2D(Him->GetActorLocation(), Hawkeye->GetActorLocation()) <= HawkeyeBossFight::DodgeNoticeDistance)
		{
			Bark(EBossBarkEvent::PlayerDodges);
		}
		bKateWasDodging = bDodging;
		if (GrantSlot > 0 && Hawkeye->GetInventoryComponent())
		{
			const int32 Now = Hawkeye->GetInventoryComponent()->GetArrowCount(GrantSlot);
			if (Now < GrantCount)
			{
				Bark(EBossBarkEvent::GrantedArrowUsed);
			}
			GrantCount = Now;
		}
	}
}

void UBossFightSubsystem::UpdateDefeated(float DeltaSeconds)
{
	DefeatedSeconds += DeltaSeconds;
	const UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this);
	const bool bBusy = !bSkipLines && Dialogue && Dialogue->IsBusy();
	if (bSkipLines || (DefeatedSeconds >= UBossRules::IntroMinSeconds && !bBusy) || DefeatedSeconds >= Definition->AfterMaxSeconds)
	{
		Finish();
	}
}

void UBossFightSubsystem::Finish()
{
	UE_LOG(LogHawkeye, Log, TEXT("%s: the fight is over after %.1f s of fighting and %.1f s of after lines."), *GetName(), FightSeconds,
		DefeatedSeconds);
	SetState(EBossFightState::Done);
	// Whatever phase he went down in, every objective the fight owns is behind her now; the last completes the chapter.
	for (int32 Index = FMath::Max(PhaseIndex, 0); Index < Definition->Phases.Num(); ++Index)
	{
		CompleteObjective(Definition->Phases[Index].ObjectiveId);
	}
}

void UBossFightSubsystem::UpdateMarker()
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Missions || !Definition)
	{
		return;
	}
	if (State == EBossFightState::Approach || State == EBossFightState::Intro)
	{
		Missions->RegisterObjectiveLocation(Definition->MeetObjective, Definition->Arena.Centre);
	}
	const AThugCharacter* Him = Boss.Get();
	for (int32 Index = 0; Index < Definition->Phases.Num(); ++Index)
	{
		const FName Id = Definition->Phases[Index].ObjectiveId;
		if (Id.IsNone())
		{
			continue;
		}
		// The crew phase points at the middle of the fight; his own at him.
		const bool bOnHim = Him && Definition->Phases[Index].bBossFights && !IsDown(Him);
		Missions->RegisterObjectiveLocation(Id, bOnHim ? Him->GetActorLocation() : Definition->Arena.Centre);
	}
}

void UBossFightSubsystem::Say(const TArray<FName>& Lines)
{
	UDialogueSubsystem* Dialogue = bSkipLines ? nullptr : UDialogueSubsystem::Get(this);
	for (const FName Line : Lines)
	{
		LinesSpoken.Add(Line);
		if (Dialogue)
		{
			Dialogue->PlayLine(Line);
		}
	}
}

void UBossFightSubsystem::Bark(EBossBarkEvent Event)
{
	if (!Definition || !Definition->Phases.IsValidIndex(PhaseIndex))
	{
		return;
	}
	for (const FBossBark& Entry : Definition->Phases[PhaseIndex].Barks)
	{
		if (Entry.Event != Event)
		{
			continue;
		}
		const FString Key = HawkeyeBossFight::BarkKey(PhaseIndex, Event);
		const int32 Times = BarkCounts.FindRef(Key);
		const float Since = FightSeconds - BarkLastSeconds.FindRef(Key);
		if (!UBossRules::CanBark(Entry.bOnce, Times, Since, Entry.CooldownSeconds))
		{
			return;
		}
		BarkCounts.Add(Key, Times + 1);
		BarkLastSeconds.Add(Key, FightSeconds);
		BarksSpoken.Add(Key);
		UE_LOG(LogHawkeye, Log, TEXT("%s: bark %s at %.1f s."), *GetName(), *Key, FightSeconds);
		Say(Entry.Lines);
		return;
	}
}

bool UBossFightSubsystem::CompleteObjective(FName ObjectiveId) const
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (ObjectiveId.IsNone() || !Missions || !Missions->HasObjective(ObjectiveId))
	{
		return false;
	}
	const UMissionObjective* Objective = Missions->GetTracker() ? Missions->GetTracker()->FindObjective(ObjectiveId) : nullptr;
	if (Objective && Objective->IsCompleted())
	{
		return false;
	}
	return Missions->CompleteObjective(ObjectiveId);
}

// --- The save ----------------------------------------------------------------------------------------------

void UBossFightSubsystem::RestartAtPhase(int32 Index)
{
	if (!Definition)
	{
		return;
	}
	if (Index < 0)
	{
		SetUpFight(Definition);
		return;
	}
	ClearCrew();
	if (ABossArenaHazard* Body = Hazard.Get())
	{
		Body->SetLoose(false);
	}
	if (Index >= Definition->Phases.Num())
	{
		ClearBoss();
		SetState(EBossFightState::Done);
		return;
	}
	AThugCharacter* Him = (Boss.IsValid() && !IsDown(Boss.Get())) ? Boss.Get() : SpawnBoss();
	if (!Him)
	{
		return;
	}
	UHealthComponent* Health = Him->GetHealthComponent();
	const float Fraction = UBossRules::CheckpointHealthFraction(Definition->GetPhaseRules(), Index);
	// The phase first (its own invulnerability, nothing broadcast), then the health down to the checkpoint's: from
	// the checkpoint phase a drop onto its own threshold opens nothing.
	Combo = FBossComboClock();
	if (UBossPhaseComponent* Phases = GetPhaseComponent())
	{
		Phases->RestorePhase(Index);
	}
	Health->SetMaxHealth(Health->GetMaxHealth(), /*bResetCurrent=*/true);
	const float Drop = Health->GetMaxHealth() * (1.f - Fraction);
	if (Drop > 0.f && !Health->IsInvulnerable())
	{
		TGuardValue<bool> Guard(bApplyingBonus, true);
		Health->ApplyDamage(Drop, nullptr);
	}
	FightSeconds = 0.f;
	PhaseStarts.Reset();
	SetState(EBossFightState::Fight);
	StartPhase(Index, /*bRestore=*/true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: the fight restarts at phase %d with %.0f of %.0f health."), *GetName(), Index,
		Health->GetCurrentHealth(), Health->GetMaxHealth());
}

void UBossFightSubsystem::RestoreFromCampaign()
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	if (!Definition || !Tracker)
	{
		return;
	}
	const int32 Checkpoint = UBossRules::CheckpointPhase(Definition->MeetObjective, Definition->GetPhaseObjectives(),
		Tracker->GetCompletedObjectiveIds());
	UE_LOG(LogHawkeye, Log, TEXT("%s: the save puts the fight at checkpoint %d."), *GetName(), Checkpoint);
	RestartAtPhase(Checkpoint);
}

void UBossFightSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess && Definition)
	{
		RestoreFromCampaign();
	}
}
