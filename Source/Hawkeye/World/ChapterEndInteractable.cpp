#include "World/ChapterEndInteractable.h"

#include "Hawkeye.h"
#include "Combat/HealthComponent.h"
#include "EngineUtils.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "World/ThugCharacter.h"
#include "HawkeyePlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"

AChapterEndInteractable::AChapterEndInteractable()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Prop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Prop"));
	Prop->SetupAttachment(Root);
	Prop->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Prop->SetCanEverAffectNavigation(false);

	PropAccent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropAccent"));
	PropAccent->SetupAttachment(Prop);
	PropAccent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PropAccent->SetCanEverAffectNavigation(false);

	CloseUpCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CloseUpCamera"));
	CloseUpCamera->SetupAttachment(Root);
	CloseUpCamera->SetFieldOfView(50.f);

	InteractZone = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractZone"));
	InteractZone->SetupAttachment(Root);
	InteractZone->SetBoxExtent(FVector(200.f, 200.f, 200.f));
	InteractZone->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	InteractZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractZone->SetGenerateOverlapEvents(true);
	InteractZone->SetCanEverAffectNavigation(false);
}

void AChapterEndInteractable::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLayout();
}

void AChapterEndInteractable::ApplyLayout()
{
	Prop->SetRelativeRotation(PropRotation);
	const FVector Look = CloseUpLookAt - CloseUpOffset;
	CloseUpCamera->SetRelativeLocationAndRotation(CloseUpOffset, Look.IsNearlyZero() ? FRotator::ZeroRotator : Look.Rotation());
	InteractZone->SetRelativeLocation(InteractZoneOffset);
	InteractZone->SetBoxExtent(InteractZoneExtent);
}

void AChapterEndInteractable::BeginPlay()
{
	Super::BeginPlay();
	// Placed actors do not rerun construction in a game; the layout comes from the properties again.
	ApplyLayout();
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && !ObjectiveId.IsNone())
	{
		Missions->RegisterObjectiveLocation(ObjectiveId, GetActorLocation());
	}
}

void AChapterEndInteractable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && !ObjectiveId.IsNone())
	{
		Missions->UnregisterObjectiveLocation(ObjectiveId);
	}
	Super::EndPlay(EndPlayReason);
}

bool AChapterEndInteractable::CanInteract_Implementation(AActor* Interactor) const
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	if (bUsed || !Pawn || !Pawn->IsPlayerControlled())
	{
		return false;
	}
	// Down, she examines nothing: the sequence would run with her on the ground and its autosave refused.
	if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Pawn);
		Hawkeye && (Hawkeye->IsDowned() || (Hawkeye->GetHealthComponent() && !Hawkeye->GetHealthComponent()->IsAlive())))
	{
		return false;
	}
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	const UMissionObjective* Objective = Tracker ? Tracker->FindObjective(ObjectiveId) : nullptr;
	if (!Objective || Objective->IsCompleted())
	{
		return false;
	}
	if (!RequiredObjective.IsNone())
	{
		const UMissionObjective* Before = Tracker->FindObjective(RequiredObjective);
		if (!Before || !Before->IsCompleted())
		{
			return false;
		}
	}
	// Not with a thug alerted: the end sequence and the return from its scene would put her back in front of
	// whoever is still shooting (the ArcherPair guards CH01's arrow). Clint's fight clock does not hold it.
	for (TActorIterator<AThugCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetAlertState() == EThugAlertState::Alerted && It->GetHealthComponent() && It->GetHealthComponent()->IsAlive())
		{
			return false;
		}
	}
	if (RequiredKeycards.Num() > 0)
	{
		const UInventoryComponent* Inventory = Pawn->FindComponentByClass<UInventoryComponent>();
		for (const FName Keycard : RequiredKeycards)
		{
			if (!Inventory || !Inventory->HasKeycard(Keycard))
			{
				return false;
			}
		}
	}
	return !bRequireCurrentObjective || Tracker->GetCurrentObjective() == Objective;
}

void AChapterEndInteractable::SetPropMesh(UStaticMesh* Mesh)
{
	Prop->SetStaticMesh(Mesh);
}

FText AChapterEndInteractable::GetInteractPrompt_Implementation() const
{
	return Prompt.IsEmpty() ? NSLOCTEXT("Hawkeye", "ChapterEndPrompt", "[Examine]") : Prompt;
}

void AChapterEndInteractable::Interact_Implementation(AActor* Interactor)
{
	if (!IInteractable::Execute_CanInteract(this, Interactor))
	{
		return;
	}
	bUsed = true;
	const APawn* Pawn = Cast<APawn>(Interactor);
	AHawkeyePlayerController* PC = Pawn ? Cast<AHawkeyePlayerController>(Pawn->GetController()) : nullptr;
	UE_LOG(LogHawkeye, Log, TEXT("%s: examined by %s; completing %s."), *GetName(), *GetNameSafe(Interactor), *ObjectiveId.ToString());

	// The close-up first, so the mission's end sequence sees it running and waits for it.
	if (PC)
	{
		PC->PlayCloseUp(this, CloseUpSeconds, CloseUpBlendSeconds);
	}
	OnExamined(Interactor);
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->CompleteObjective(ObjectiveId);
	}
}

void AChapterEndInteractable::BeginPush()
{
	CameraStart = CloseUpCamera->GetRelativeLocation();
	PushElapsed = 0.f;
	bPushing = true;
	SetActorTickEnabled(true);
}

void AChapterEndInteractable::EndPush()
{
	bPushing = false;
	SetActorTickEnabled(false);
	CloseUpCamera->SetRelativeLocation(CameraStart);
}

void AChapterEndInteractable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPushing)
	{
		return;
	}
	PushElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(PushElapsed / FMath::Max(CloseUpSeconds, 0.1f), 0.f, 1.f);
	const FVector Forward = CloseUpCamera->GetRelativeRotation().Vector();
	CloseUpCamera->SetRelativeLocation(CameraStart + Forward * PushDistance * FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.f));
}
