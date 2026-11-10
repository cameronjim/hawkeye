#include "Crime/CrimeLoot.h"

#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACrimeLoot::ACrimeLoot()
{
	PrimaryActorTick.bCanEverTick = false;
	Bag = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bag"));
	SetRootComponent(Bag);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded())
	{
		Bag->SetStaticMesh(CubeFinder.Object);
	}
	if (ShapeMaterial.Succeeded())
	{
		Bag->SetMaterial(0, ShapeMaterial.Object);
	}
	Bag->SetMobility(EComponentMobility::Movable);
	Bag->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bag->SetCanEverAffectNavigation(false);
	Bag->SetGenerateOverlapEvents(false);
	// The engine cube is 100 cm a side.
	Bag->SetWorldScale3D(BagSize / 100.f);
}

void ACrimeLoot::BeginPlay()
{
	Super::BeginPlay();
	Bag->SetWorldScale3D(BagSize / 100.f);
	RefreshLook();
}

void ACrimeLoot::SetCarrier(AActor* InCarrier)
{
	if (InCarrier)
	{
		Carrier = InCarrier;
		AttachToActor(InCarrier, FAttachmentTransformRules::KeepWorldTransform);
		SetActorRelativeLocation(CarryOffset);
		SetActorRelativeRotation(FRotator::ZeroRotator);
		RefreshLook();
		return;
	}
	Carrier = nullptr;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	// On whatever is under it: the street, a roof.
	FVector Rest = GetActorLocation();
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrimeLootDrop), false, this);
		if (World->LineTraceSingleByChannel(Hit, Rest + FVector(0.f, 0.f, 50.f), Rest - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
		{
			Rest = Hit.ImpactPoint + FVector(0.f, 0.f, BagSize.Z * 0.5f);
		}
	}
	SetActorLocationAndRotation(Rest, FRotator(0.f, GetActorRotation().Yaw, 0.f));
	RefreshLook();
}

void ACrimeLoot::Recover()
{
	if (bRecovered)
	{
		return;
	}
	bRecovered = true;
	if (Carrier.IsValid())
	{
		SetCarrier(nullptr);
	}
	SetActorHiddenInGame(true);
}

void ACrimeLoot::RefreshLook()
{
	if (UMaterialInstanceDynamic* Material = Bag->CreateDynamicMaterialInstance(0))
	{
		// BasicShapeMaterial has only a Color; a dropped bag is lifted toward the purple so it reads.
		const FLinearColor Tint = IsCarried() ? BagColor : FMath::Lerp(BagColor, DroppedGlow, 0.35f);
		Material->SetVectorParameterValue(TEXT("Color"), Tint);
	}
}
