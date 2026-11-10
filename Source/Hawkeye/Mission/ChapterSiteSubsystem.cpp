#include "Mission/ChapterSiteSubsystem.h"

#include "Hawkeye.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Mission/ObjectiveTriggerVolume.h"
#include "Mission/ThugGroupObjective.h"
#include "NavigationSystem.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "World/ChapterEndInteractable.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

const FName UChapterSiteSubsystem::CrewTag(TEXT("SiteCrew"));
const FName UChapterSiteSubsystem::SiteTag(TEXT("ChapterSite"));

UChapterSiteSubsystem* UChapterSiteSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UChapterSiteSubsystem>() : nullptr;
}

bool UChapterSiteSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChapterSiteSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UMissionSubsystem>();
	Super::Initialize(Collection);
}

void UChapterSiteSubsystem::Deinitialize()
{
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UChapterSiteSubsystem::HandleCampaignLoaded);
	}
	Definition = nullptr;
	Super::Deinitialize();
}

void UChapterSiteSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	BindMissions();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UChapterSiteSubsystem::HandleCampaignLoaded);
	}
	// The map's game mode may have started its chapter before this bound.
	if (const UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && Missions->GetCurrentMission() && !Definition)
	{
		HandleMissionStarted(Missions->GetCurrentMission());
	}
}

void UChapterSiteSubsystem::BindMissions()
{
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->OnMissionStarted.AddUniqueDynamic(this, &UChapterSiteSubsystem::HandleMissionStarted);
		Missions->OnObjectiveUpdated.AddUniqueDynamic(this, &UChapterSiteSubsystem::HandleObjectiveUpdated);
	}
}

APawn* UChapterSiteSubsystem::FindPlayer() const
{
	if (PlayerOverride.IsValid())
	{
		return PlayerOverride.Get();
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

bool UChapterSiteSubsystem::IsObjectiveDone(FName ObjectiveId) const
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	const UMissionObjective* Objective = Tracker && !ObjectiveId.IsNone() ? Tracker->FindObjective(ObjectiveId) : nullptr;
	return Objective && Objective->IsCompleted();
}

void UChapterSiteSubsystem::HandleMissionStarted(UMissionDefinition* Mission)
{
	UChapterSiteDefinition* Wanted = Mission ? Mission->Site.LoadSynchronous() : nullptr;
	if (!Wanted)
	{
		ClearSite();
		return;
	}
	SpawnSite(Wanted);
}

void UChapterSiteSubsystem::HandleObjectiveUpdated(UMissionObjective* Objective, int32 /*ObjectiveIndex*/)
{
	if (Definition && Objective && Objective->IsCompleted() && !Definition->CrewAlertObjective.IsNone()
		&& Objective->ObjectiveId == Definition->CrewAlertObjective)
	{
		AlertCrew();
	}
}

void UChapterSiteSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		Reconcile();
	}
}

// --- Spawning ------------------------------------------------------------------------------------------------

AThugCharacter* UChapterSiteSubsystem::SpawnThug(UClass* Class, const FVector& Feet, float Yaw, EThugWeapon Weapon)
{
	UWorld* World = GetWorld();
	Class = Class ? Class : AThugCharacter::StaticClass();
	const float HalfHeight = Class->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Ground = Feet;
	if (const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr)
	{
		FNavLocation OnNav;
		if (Nav->ProjectPointToNavigation(Feet, OnNav, FVector(150.f, 150.f, 200.f)))
		{
			Ground = OnNav.Location;
		}
	}
	const FTransform At(FRotator(0.f, Yaw, 0.f), Ground + FVector(0.f, 0.f, HalfHeight + 2.f));
	AThugCharacter* Thug = World ? World->SpawnActorDeferred<AThugCharacter>(Class, At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn) : nullptr;
	if (!Thug)
	{
		return nullptr;
	}
	Thug->Weapon = Weapon;
	// The site is rebuilt from the chapter's objectives on a load, never from SPUD's copy of who was standing.
	Thug->bSkipSave = true;
	Thug->Tags.AddUnique(CrewTag);
	Thug->Tags.AddUnique(SiteTag);
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
	if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
	{
		Brain->SetThinkingEnabled(false);
	}
	return Thug;
}

bool UChapterSiteSubsystem::SpawnSite(UChapterSiteDefinition* InDefinition)
{
	ClearSite();
	UWorld* World = GetWorld();
	if (!InDefinition || !World)
	{
		return false;
	}
	Definition = InDefinition;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FChapterSiteProp& Prop : Definition->Props)
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
		// Dressing never pushes the camera into her back.
		Component->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Actor->SetActorScale3D(Prop.Transform.GetScale3D());
		Actor->Tags.Add(SiteTag);
		Props.Add(Actor);
	}
	for (const FChapterSiteLight& Bulb : Definition->Lights)
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
		Lamp->Tags.Add(SiteTag);
		Props.Add(Lamp);
	}
	for (const FChapterSiteVolume& Spec : Definition->Volumes)
	{
		AObjectiveTriggerVolume* Volume = World->SpawnActorDeferred<AObjectiveTriggerVolume>(AObjectiveTriggerVolume::StaticClass(),
			FTransform(Spec.Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Volume)
		{
			continue;
		}
		Volume->ObjectiveId = Spec.ObjectiveId;
		if (UBoxComponent* Box = Cast<UBoxComponent>(Volume->GetCollisionComponent()))
		{
			Box->SetBoxExtent(Spec.Extent);
		}
		Volume->Tags.Add(SiteTag);
		Volume->FinishSpawning(FTransform(Spec.Location));
		Volumes.Add(Volume);
	}

	// The crew: still until the alert objective, counted by one group objective at their middle.
	const APawn* Player = FindPlayer();
	FVector Middle = FVector::ZeroVector;
	for (const FChapterSiteThug& Member : Definition->Crew)
	{
		UClass* Class = Member.ThugClass.IsNull() ? nullptr : Member.ThugClass.LoadSynchronous();
		if (AThugCharacter* Thug = SpawnThug(Class, Member.Location, Member.Yaw, Member.Weapon))
		{
			Crew.Add(Thug);
			Middle += Member.Location;
		}
	}
	if (Crew.Num() > 0 && !Definition->CrewObjective.IsNone())
	{
		Middle /= Crew.Num();
		AThugGroupObjective* Group = World->SpawnActorDeferred<AThugGroupObjective>(AThugGroupObjective::StaticClass(), FTransform(Middle),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Group)
		{
			Group->ObjectiveId = Definition->CrewObjective;
			Group->GroupTag = CrewTag;
			Group->Tags.Add(SiteTag);
			Group->FinishSpawning(FTransform(Middle));
			for (const TWeakObjectPtr<AThugCharacter>& Weak : Crew)
			{
				Group->RegisterThug(Weak.Get());
			}
			CrewGroup = Group;
		}
	}

	for (const FChapterSitePickup& Spec : Definition->Pickups)
	{
		UClass* Class = Spec.PickupClass.IsNull() ? nullptr : Spec.PickupClass.LoadSynchronous();
		Class = Class ? Class : APickupActor::StaticClass();
		const FTransform At(Spec.Location);
		APickupActor* Pickup = World->SpawnActorDeferred<APickupActor>(Class, At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Pickup)
		{
			continue;
		}
		Pickup->PickupType = EPickupType::Keycard;
		Pickup->KeycardId = Spec.KeycardId;
		Pickup->Tags.Add(SiteTag);
		Pickup->FinishSpawning(At);
		Pickups.Add(Pickup);
	}

	for (const FChapterSiteExamine& Spec : Definition->Examines)
	{
		const FTransform At(FRotator(0.f, Spec.Yaw, 0.f), Spec.Location);
		AChapterEndInteractable* Examine = World->SpawnActorDeferred<AChapterEndInteractable>(AChapterEndInteractable::StaticClass(), At,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Examine)
		{
			continue;
		}
		Examine->ObjectiveId = Spec.ObjectiveId;
		Examine->Prompt = Spec.Prompt;
		Examine->bRequireCurrentObjective = Spec.bRequireCurrentObjective;
		Examine->RequiredKeycards = Spec.RequiredKeycards;
		Examine->CloseUpOffset = Spec.CloseUpOffset;
		Examine->CloseUpLookAt = Spec.CloseUpLookAt;
		Examine->InteractZoneExtent = Spec.InteractZoneExtent;
		Examine->InteractZoneOffset = FVector(0.f, 0.f, Spec.InteractZoneExtent.Z * 0.5f);
		Examine->CloseUpSeconds = Spec.CloseUpSeconds;
		Examine->SetPropMesh(Spec.Mesh.LoadSynchronous());
		Examine->Tags.Add(SiteTag);
		Examine->FinishSpawning(At);
		Examines.Add(Examine);
	}

	if (Definition->CrewAlertObjective.IsNone())
	{
		AlertCrew();
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: site %s dressed: %d prop(s) and light(s), %d volume(s), %d crew, %d pickup(s), %d examine(s)%s."), *GetName(),
		*Definition->GetName(), Props.Num(), Volumes.Num(), Crew.Num(), Pickups.Num(), Examines.Num(),
		Player ? TEXT("") : TEXT(" (no player yet)"));
	return true;
}

void UChapterSiteSubsystem::ClearCrew()
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
	if (AThugGroupObjective* Group = CrewGroup.Get())
	{
		Group->Destroy();
	}
	CrewGroup.Reset();
}

void UChapterSiteSubsystem::ClearSite()
{
	ClearCrew();
	auto DestroyAll = [](auto& List)
	{
		for (auto& Weak : List)
		{
			if (AActor* Actor = Weak.Get())
			{
				Actor->Destroy();
			}
		}
		List.Reset();
	};
	DestroyAll(Props);
	DestroyAll(Pickups);
	DestroyAll(Examines);
	DestroyAll(Volumes);
	bCrewAlerted = false;
	if (Definition)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: site %s cleared."), *GetName(), *Definition->GetName());
	}
	Definition = nullptr;
}

bool UChapterSiteSubsystem::BeginChapter(const UMissionDefinition* Mission)
{
	UChapterSiteDefinition* Wanted = Mission ? Mission->Site.LoadSynchronous() : nullptr;
	if (!Wanted)
	{
		return false;
	}
	if (Definition != Wanted)
	{
		SpawnSite(Wanted);
	}
	APawn* Kate = FindPlayer();
	if (!Kate || Wanted->PlayerStart.IsZero())
	{
		return false;
	}
	const float HalfHeight = Kate->GetRootComponent() ? Kate->GetRootComponent()->Bounds.BoxExtent.Z : 90.f;
	const FRotator Facing(0.f, Wanted->PlayerStartYaw, 0.f);
	Kate->TeleportTo(Wanted->PlayerStart + FVector(0.f, 0.f, HalfHeight + 2.f), Facing, false, true);
	if (AController* Controller = Kate->GetController())
	{
		Controller->SetControlRotation(FRotator(-8.f, Wanted->PlayerStartYaw, 0.f));
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: Kate put at the site's start %s, yaw %.0f."), *GetName(), *Wanted->PlayerStart.ToCompactString(),
		Wanted->PlayerStartYaw);
	return true;
}

void UChapterSiteSubsystem::AlertCrew()
{
	APawn* Kate = FindPlayer();
	bCrewAlerted = true;
	for (const TWeakObjectPtr<AThugCharacter>& Weak : Crew)
	{
		AThugCharacter* Thug = Weak.Get();
		AThugAIController* Brain = Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr;
		if (!Brain || !Thug->GetHealthComponent() || Thug->GetHealthComponent()->IsDead())
		{
			continue;
		}
		Brain->SetThinkingEnabled(true);
		if (Kate)
		{
			Brain->SetTarget(Kate);
			Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
			Thug->SetAlertState(EThugAlertState::Alerted);
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: the site's crew (%d) is alerted on %s."), *GetName(), Crew.Num(), *GetNameSafe(Kate));
}

void UChapterSiteSubsystem::Reconcile()
{
	if (!Definition)
	{
		return;
	}
	if (IsObjectiveDone(Definition->CrewObjective))
	{
		ClearCrew();
	}
	else if (!Definition->CrewAlertObjective.IsNone() && IsObjectiveDone(Definition->CrewAlertObjective))
	{
		AlertCrew();
	}
	const APawn* Kate = FindPlayer();
	const UInventoryComponent* Inventory = Kate ? Kate->FindComponentByClass<UInventoryComponent>() : nullptr;
	int32 Gone = 0;
	for (TWeakObjectPtr<APickupActor>& Weak : Pickups)
	{
		APickupActor* Pickup = Weak.Get();
		if (Pickup && Inventory && Inventory->HasKeycard(Pickup->KeycardId))
		{
			Pickup->Destroy();
			Weak.Reset();
			++Gone;
		}
	}
	Pickups.RemoveAll([](const TWeakObjectPtr<APickupActor>& Weak) { return !Weak.IsValid(); });
	UE_LOG(LogHawkeye, Log, TEXT("%s: site %s matched to the save: crew %s, %d pickup(s) already carried."), *GetName(), *Definition->GetName(),
		CrewGroup.IsValid() ? (bCrewAlerted ? TEXT("alerted") : TEXT("waiting")) : TEXT("gone"), Gone);
}

// --- Views ---------------------------------------------------------------------------------------------------

template <typename T>
static TArray<T*> HawkeyeSiteLive(const TArray<TWeakObjectPtr<T>>& List)
{
	TArray<T*> Out;
	for (const TWeakObjectPtr<T>& Weak : List)
	{
		if (T* Item = Weak.Get())
		{
			Out.Add(Item);
		}
	}
	return Out;
}

TArray<AThugCharacter*> UChapterSiteSubsystem::GetCrew() const { return HawkeyeSiteLive(Crew); }
TArray<APickupActor*> UChapterSiteSubsystem::GetPickups() const { return HawkeyeSiteLive(Pickups); }
TArray<AChapterEndInteractable*> UChapterSiteSubsystem::GetExamines() const { return HawkeyeSiteLive(Examines); }
TArray<AObjectiveTriggerVolume*> UChapterSiteSubsystem::GetVolumes() const { return HawkeyeSiteLive(Volumes); }
