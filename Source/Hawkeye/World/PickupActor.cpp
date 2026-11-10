#include "World/PickupActor.h"

#include "Hawkeye.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Mission/MissionSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"

APickupActor::APickupActor()
{
	// Ticks for the hover, bob and spin that make a pickup look like one.
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	// Blocks the interaction sweep (ECC_Visibility) but never the player walking into it.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetMobility(EComponentMobility::Movable);
	// It spins and bobs every frame; if it counted for navigation the navmesh under it would never stop rebuilding.
	Mesh->SetCanEverAffectNavigation(false);

	// The parts carry the silhouette and, with the root mesh cleared, the interaction sweep.
	const TCHAR* PartNames[] = { TEXT("Part1"), TEXT("Part2"), TEXT("Part3"), TEXT("Part4") };
	TObjectPtr<UStaticMeshComponent>* PartSlots[] = { &Part1, &Part2, &Part3, &Part4 };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartNames[Index]);
		Part->SetupAttachment(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCastShadow(false);
		Part->SetCanEverAffectNavigation(false);

		*PartSlots[Index] = Part;
	}

	PickupZone = CreateDefaultSubobject<USphereComponent>(TEXT("PickupZone"));
	PickupZone->SetupAttachment(Mesh);
	PickupZone->SetSphereRadius(110.f);
	PickupZone->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	PickupZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupZone->SetGenerateOverlapEvents(true);
	PickupZone->SetCanEverAffectNavigation(false);
}

TArray<UStaticMeshComponent*> APickupActor::GetParts() const
{
	return { Part1, Part2, Part3, Part4 };
}

void APickupActor::BeginPlay()
{
	Super::BeginPlay();

	RestLocation = GetActorLocation();

	// A stable per-actor seed: two pickups dropped by the same guard must not bob together.
	BobPhase = FMath::Fmod(static_cast<float>(GetUniqueID()) * 0.37f, 2.f * PI);
}

void APickupActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	BobElapsed += DeltaSeconds;

	const float Bob = FMath::Sin(BobElapsed * BobHz * 2.f * PI + BobPhase) * BobAmplitude;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, HoverHeight + Bob));
	AddActorWorldRotation(FRotator(0.f, SpinDegreesPerSecond * DeltaSeconds, 0.f));
}

bool APickupActor::CanInteract_Implementation(AActor* Interactor) const
{
	return !bCarried && Cast<AHawkeyeCharacter>(Interactor) != nullptr;
}

void APickupActor::SetCarried(bool bInCarried)
{
	bCarried = bInCarried;
	SetActorHiddenInGame(bCarried);
	SetActorEnableCollision(!bCarried);
	SetActorTickEnabled(!bCarried);
}

void APickupActor::DropAt(FVector Where)
{
	SetCarried(false);
	RestLocation = Where;
	SetActorLocation(RestLocation + FVector(0.f, 0.f, HoverHeight));
	UE_LOG(LogHawkeye, Log, TEXT("%s dropped at %s."), *GetName(), *Where.ToCompactString());
}

FText APickupActor::GetInteractPrompt_Implementation() const
{
	if (!PromptOverride.IsEmpty())
	{
		return PromptOverride;
	}

	switch (PickupType)
	{
	case EPickupType::Bow:
		return NSLOCTEXT("Hawkeye", "PickupBow", "[E] Take the bow");
	case EPickupType::Keycard:
		return NSLOCTEXT("Hawkeye", "PickupKeycard", "[E] Take the keycard");
	case EPickupType::Arrows:
	default:
		return NSLOCTEXT("Hawkeye", "PickupArrows", "[E] Take the arrows");
	}
}

void APickupActor::Interact_Implementation(AActor* Interactor)
{
	ApplyTo(Interactor);
}

bool APickupActor::ApplyTo(AActor* Interactor)
{
	AHawkeyeCharacter* Character = Cast<AHawkeyeCharacter>(Interactor);
	if (!Character || bCarried)
	{
		return false;
	}

	UInventoryComponent* Inventory = Character->GetInventoryComponent();

	switch (PickupType)
	{
	case EPickupType::Bow:
	{
		UBowDefinition* Definition = Bow.IsNull() ? nullptr : Bow.LoadSynchronous();
		if (!Inventory || !Definition)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: a bow pickup with no bow set, or a pawn with no inventory."), *GetName());
			return false;
		}
		Inventory->GiveBow(Definition);
		break;
	}
	case EPickupType::Keycard:
		Character->GiveKeycard(KeycardId);
		break;
	case EPickupType::Arrows:
	{
		UArrowDefinition* Definition = Arrow.IsNull() ? nullptr : Arrow.LoadSynchronous();
		if (!Definition && Inventory)
		{
			Definition = Inventory->GetActiveArrow();
		}
		if (!Inventory || !Definition)
		{
			return false;
		}
		Inventory->AddArrows(Definition, ArrowCount);
		break;
	}
	}

	if (!CompletesObjectiveId.IsNone())
	{
		if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
		{
			Missions->CompleteObjective(CompletesObjectiveId);
		}
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s picked up %s."), *Character->GetName(), *GetName());

	if (const UBowComponent* CollectorBow = Interactor ? Interactor->FindComponentByClass<UBowComponent>() : nullptr)
	{
		CollectorBow->PlayPickupSound();
	}
	OnPickedUp(Interactor);
	Destroy();
	return true;
}
