#include "World/DoorActor.h"

#include "Hawkeye.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Mission/MissionTracker.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Navigation/NavLinkProxy.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"
#include "Player/HawkeyeCharacter.h"
#include "TimerManager.h"
#include "World/ThugCharacter.h"

ADoorActor::ADoorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// A bare scene component is the root, so the actor's origin is the middle of the threshold
	// and both meshes keep the relative heights authored here. See the DoorRoot comment.
	DoorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorRoot"));
	SetRootComponent(DoorRoot);
	DoorRoot->SetMobility(EComponentMobility::Static);

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(DoorRoot);
	FrameMesh->SetMobility(EComponentMobility::Static);
	FrameMesh->SetRelativeLocation(FVector(0.f, 0.f, FrameHeight * 0.5f));
	// Scenery only. A frame wide enough to be a surround is also wide enough to plug the
	// doorway, and a blocking one swallowed the interaction sweep and the player with it.
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FrameMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(DoorRoot);
	// Movable and blocking: the leaf is what stops the player walking through, and the whole
	// slab from the floor to LeafHeight is what the interaction sweep finds.
	DoorMesh->SetMobility(EComponentMobility::Movable);
	DoorMesh->SetRelativeLocation(FVector(0.f, 0.f, LeafHeight * 0.5f));
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionResponseToAllChannels(ECR_Block);

	InteractZone = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractZone"));
	InteractZone->SetupAttachment(DoorRoot);
	InteractZone->SetBoxExtent(FVector(130.f, 70.f, 100.f));
	InteractZone->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	InteractZone->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	InteractZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractZone->SetGenerateOverlapEvents(true);
	InteractZone->SetCanEverAffectNavigation(false);
}

FVector ADoorActor::GetLeafWorldCentre() const
{
	return DoorMesh ? DoorMesh->GetComponentLocation() : GetActorLocation();
}

void ADoorActor::BeginPlay()
{
	Super::BeginPlay();

	if (DoorMesh)
	{
		ClosedRelativeLocation = DoorMesh->GetRelativeLocation();
		ClosedRelativeRotation = DoorMesh->GetRelativeRotation();
	}
	if (InteractZone)
	{
		InteractZone->OnComponentBeginOverlap.AddDynamic(this, &ADoorActor::HandleZoneBeginOverlap);
	}
	UpdateNavLink();
	if (!OpensOnObjective.IsNone())
	{
		if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
		{
			Missions->OnObjectiveUpdated.AddUniqueDynamic(this, &ADoorActor::HandleObjectiveUpdated);
		}
		if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
		{
			Save->OnCampaignLoaded.AddUniqueDynamic(this, &ADoorActor::HandleCampaignLoaded);
		}
		CheckOpensOnObjective();
	}
}

void ADoorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &ADoorActor::HandleCampaignLoaded);
	}
	Super::EndPlay(EndPlayReason);
}

bool ADoorActor::CheckOpensOnObjective()
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	const UMissionObjective* Objective = Tracker && !OpensOnObjective.IsNone() ? Tracker->FindObjective(OpensOnObjective) : nullptr;
	if (!Objective || !Objective->IsCompleted() || bOpen)
	{
		return false;
	}
	bLocked = false;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s is done; opening."), *GetName(), *OpensOnObjective.ToString());
	return OpenNow(nullptr);
}

void ADoorActor::HandleObjectiveUpdated(UMissionObjective* Objective, int32 /*ObjectiveIndex*/)
{
	if (Objective && Objective->ObjectiveId == OpensOnObjective)
	{
		CheckOpensOnObjective();
	}
}

void ADoorActor::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		CheckOpensOnObjective();
	}
}

void ADoorActor::UpdateNavLink()
{
	// Shut and locked, the leaf cuts the navmesh too: a 100 cm doorway is not always eroded shut, and a strip
	// of navmesh under the leaf would carry paths into a locked room without the link.
	if (DoorMesh && DoorMesh->CanEverAffectNavigation() != !IsPassable())
	{
		DoorMesh->SetCanEverAffectNavigation(!IsPassable());
		UNavigationSystemV1::UpdateComponentInNavOctree(*DoorMesh);
		// And the tiles round the doorway are rebuilt now: a leaf leaving the octree did not always dirty them, and a door
		// opened by an objective left no path through it (the grotto's workshop).
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); Nav && HasActorBegunPlay())
		{
			Nav->AddDirtyArea(GetComponentsBoundingBox(true).ExpandBy(150.f), ENavigationDirtyFlag::All, TEXT("door passability"));
		}
	}
	if (!NavLink)
	{
		return;
	}
	// A Null link is left out of the navmesh (the vault's, from the first build at BeginPlay); a Default one
	// goes in when its tiles are rebuilt, which updating the proxy in the octree asks for.
	UClass* Want = IsPassable() ? UNavArea_Default::StaticClass() : UNavArea_Null::StaticClass();
	bool bChanged = false;
	for (FNavigationLink& Link : NavLink->PointLinks)
	{
		if (Link.GetAreaClass() != Want)
		{
			Link.SetAreaClass(Want);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		UNavigationSystemV1::UpdateActorInNavOctree(*NavLink);
		UE_LOG(LogHawkeye, Log, TEXT("%s: paths through it %s."), *GetName(), IsPassable() ? TEXT("on") : TEXT("off (locked)"));
	}
}

void ADoorActor::HandleZoneBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	const AThugCharacter* Thug = Cast<AThugCharacter>(OtherActor);
	if (bOpensForThugs && !bLocked && !bOpen && Thug && !Thug->IsLimp())
	{
		OpenNow(OtherActor);
	}
}

bool ADoorActor::CanInteract_Implementation(AActor* Interactor)  const
{
	// A locked door still answers, so the player is told what it wants; an open one is done. One an objective opens
	// answers nothing while it waits (the thing that opens it is elsewhere: the workshop's keypad).
	if (bLocked && !OpensOnObjective.IsNone())
	{
		return false;
	}
	return !bOpen && Cast<AHawkeyeCharacter>(Interactor) != nullptr;
}

FText ADoorActor::GetInteractPrompt_Implementation() const
{
	if (bOpen)
	{
		return FText::GetEmpty();
	}

	return bLocked
		? NSLOCTEXT("Hawkeye", "DoorLocked", "Locked: keycard required")
		: NSLOCTEXT("Hawkeye", "DoorOpen", "[E] Open");
}

void ADoorActor::Interact_Implementation(AActor* Interactor)
{
	TryOpen(Interactor);
}

bool ADoorActor::IsUnlockedFor(const AActor* Interactor) const
{
	if (!bLocked)
	{
		return true;
	}

	const AHawkeyeCharacter* Character = Cast<AHawkeyeCharacter>(Interactor);
	return Character && Character->HasKeycard(RequiredKeycardId);
}

bool ADoorActor::TryOpen(AActor* Interactor)
{
	if (!IsUnlockedFor(Interactor))
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: refused, '%s' keycard missing."),
			*GetName(), *RequiredKeycardId.ToString());
		OnOpenRefused(Interactor);
		return false;
	}

	return OpenNow(Interactor);
}

bool ADoorActor::OpenNow(AActor* Interactor)
{
	if (bOpen || bAnimating)
	{
		return false;
	}

	bOpen = true;
	bAnimating = true;
	SetActorTickEnabled(true);
	UpdateNavLink();

	// Once open the leaf is scenery; stop it blocking the corridor it just cleared.
	if (DoorMesh)
	{
		DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (!CompletesObjectiveId.IsNone())
	{
		if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
		{
			Missions->CompleteObjective(CompletesObjectiveId);
		}
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s opened by %s."), *GetName(), *GetNameSafe(Interactor));
	OnDoorOpened.Broadcast(Interactor);
	return true;
}

void ADoorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bAnimating)
	{
		SetActorTickEnabled(false);
		return;
	}

	OpenAlpha = FMath::Clamp(OpenAlpha + DeltaSeconds / FMath::Max(OpenSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f);
	ApplyOpenAlpha();

	if (OpenAlpha >= 1.f)
	{
		bAnimating = false;
		SetActorTickEnabled(false);
	}
}

void ADoorActor::ApplyOpenAlpha()
{
	if (!DoorMesh)
	{
		return;
	}

	if (Motion == EDoorMotion::Swing)
	{
		FRotator Rotation = ClosedRelativeRotation;
		Rotation.Yaw += OpenYawDegrees * OpenAlpha;
		DoorMesh->SetRelativeRotation(Rotation);
		return;
	}

	DoorMesh->SetRelativeLocation(ClosedRelativeLocation + FVector(0.f, SlideDistance * OpenAlpha, 0.f));
}
