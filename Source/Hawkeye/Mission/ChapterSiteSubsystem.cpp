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
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/TargetPoint.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Mission/ChapterMaps.h"
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
#include "TimerManager.h"
#include "World/ArrowSwitch.h"
#include "World/ChapterEndInteractable.h"
#include "World/InteriorEntrance.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

// --- Rules ---------------------------------------------------------------------------------------------------

bool UHawkeyeChapterSiteRules::ShouldFireTrigger(bool bInside, bool bOnlyUnseen, bool bAnyAlerted, bool bAlreadyFired)
{
	return bInside && !bAlreadyFired && !(bOnlyUnseen && bAnyAlerted);
}

bool UHawkeyeChapterSiteRules::ShouldMarkSpotted(bool bCurrentIsStealth, bool bAlreadySpotted, int32 AlertedThugs)
{
	return bCurrentIsStealth && !bAlreadySpotted && AlertedThugs > 0;
}

FName UHawkeyeChapterSiteRules::SpottedEvent(FName ObjectiveId)
{
	return FName(*FString::Printf(TEXT("spotted:%s"), *ObjectiveId.ToString()));
}

// --- Subsystem -----------------------------------------------------------------------------------------------

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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WatchTimer);
	}
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
	InWorld.GetTimerManager().SetTimer(WatchTimer, this, &UChapterSiteSubsystem::Watch, 0.2f, true);
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
	// The district's site in the district, an interior's in that interior (gameplay-semantics.md, "Chapter interiors").
	UChapterSiteDefinition* Wanted = UHawkeyeChapterMaps::FindSiteFor(Mission, GetWorld());
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
		Examine->RequiredObjective = Spec.RequiredObjective;
		Examine->SetPropMesh(Spec.Mesh.LoadSynchronous());
		Examine->Tags.Add(SiteTag);
		Examine->FinishSpawning(At);
		Examines.Add(Examine);
	}

	for (const FChapterSiteReturnPoint& Spec : Definition->ReturnPoints)
	{
		ATargetPoint* Point = World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), Spec.Location + FVector(0.f, 0.f, 100.f),
			FRotator(0.f, Spec.Yaw, 0.f), Params);
		if (!Point)
		{
			continue;
		}
		// An interior's doors find where to come out by this tag (FHawkeyeSceneReturn::SelectReturnPoint).
		Point->Tags.Add(Spec.Label);
		Point->Tags.Add(SiteTag);
		ReturnPoints.Add(Point);
	}
	for (const FChapterSiteInteriorDoor& Spec : Definition->InteriorDoors)
	{
		const FTransform At(FRotator(0.f, Spec.Yaw, 0.f), Spec.Location);
		AInteriorEntrance* Door = World->SpawnActorDeferred<AInteriorEntrance>(AInteriorEntrance::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Door)
		{
			continue;
		}
		Door->Interior = Spec.Interior;
		Door->ReturnPointLabel = Spec.ReturnPointLabel;
		Door->DisplayName = Spec.DisplayName;
		Door->Tags.Add(SiteTag);
		if (!Spec.Tag.IsNone())
		{
			Door->Tags.Add(Spec.Tag);
		}
		Door->FinishSpawning(At);
		Doors.Add(Door);
	}
	for (const FChapterSiteSwitch& Spec : Definition->Switches)
	{
		const FTransform At(FRotator(0.f, Spec.Yaw, 0.f), Spec.Location);
		AArrowSwitch* Switch = World->SpawnActorDeferred<AArrowSwitch>(AArrowSwitch::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Switch)
		{
			continue;
		}
		Switch->SwitchId = Spec.Id;
		Switch->NoiseLoudness = Spec.NoiseLoudness;
		Switch->bReturningArrowsOnly = Spec.bReturningArrowsOnly;
		Switch->Tags.Add(SiteTag);
		Switch->FinishSpawning(At);
		Switch->SetMesh(Spec.Mesh.LoadSynchronous());
		Switches.Add(Switch);
	}

	if (Definition->CrewAlertObjective.IsNone())
	{
		AlertCrew();
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: site %s dressed: %d prop(s) and light(s), %d volume(s), %d crew, %d pickup(s), %d examine(s), %d door(s), %d return point(s), %d trigger(s), %d switch(es)%s."), *GetName(),
		*Definition->GetName(), Props.Num(), Volumes.Num(), Crew.Num(), Pickups.Num(), Examines.Num(), Doors.Num(), ReturnPoints.Num(),
		Definition->Triggers.Num(), Switches.Num(), Player ? TEXT("") : TEXT(" (no player yet)"));
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
	DestroyAll(Doors);
	DestroyAll(Switches);
	DestroyAll(ReturnPoints);
	bCrewAlerted = false;
	if (Definition)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: site %s cleared."), *GetName(), *Definition->GetName());
	}
	Definition = nullptr;
}

bool UChapterSiteSubsystem::BeginChapter(const UMissionDefinition* Mission)
{
	UChapterSiteDefinition* Wanted = UHawkeyeChapterMaps::FindSiteFor(Mission, GetWorld());
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
TArray<AInteriorEntrance*> UChapterSiteSubsystem::GetInteriorDoors() const { return HawkeyeSiteLive(Doors); }
TArray<AArrowSwitch*> UChapterSiteSubsystem::GetSwitches() const { return HawkeyeSiteLive(Switches); }
TArray<AActor*> UChapterSiteSubsystem::GetReturnPoints() const { return HawkeyeSiteLive(ReturnPoints); }

// --- The watch -----------------------------------------------------------------------------------------------

void UChapterSiteSubsystem::Watch()
{
	UWorld* World = GetWorld();
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	if (!World || !Tracker || !Missions->GetCurrentMission())
	{
		return;
	}
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (Save && Save->IsLoading())
	{
		return;
	}
	int32 Alerted = 0;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		Alerted += It->GetAlertState() == EThugAlertState::Alerted && Health && Health->IsAlive() && !It->IsLimp() ? 1 : 0;
	}
	// The current objective, if it is a stealth one, is spotted the moment a thug has her.
	if (UMissionObjective* Current = Tracker->GetCurrentObjective();
		Current && UHawkeyeChapterSiteRules::ShouldMarkSpotted(Current->IsStealth(), Current->bSpotted, Alerted))
	{
		const FName Id = Current->ObjectiveId;
		if (Tracker->MarkObjectiveSpotted(Id))
		{
			UChapterEventSubsystem::NotifyEvent(this, UHawkeyeChapterSiteRules::SpottedEvent(Id));
		}
	}
	const APawn* Kate = FindPlayer();
	UChapterEventSubsystem* Events = UChapterEventSubsystem::Get(this);
	if (!Definition || !Kate || !Events)
	{
		return;
	}
	const FVector At = Kate->GetActorLocation();
	for (const FChapterSiteTrigger& Trigger : Definition->Triggers)
	{
		const bool bInside = FBox::BuildAABB(Trigger.Location, Trigger.Extent).IsInsideOrOn(At);
		if (UHawkeyeChapterSiteRules::ShouldFireTrigger(bInside, Trigger.bOnlyUnseen, Alerted > 0, Events->GetFired().Contains(Trigger.Event)))
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: trigger %s at %s."), *GetName(), *Trigger.Event.ToString(), *At.ToCompactString());
			Events->Notify(Trigger.Event);
		}
	}
}
