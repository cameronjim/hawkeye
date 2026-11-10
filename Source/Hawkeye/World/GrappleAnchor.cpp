#include "World/GrappleAnchor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "World/GrappleArrowProjectile.h"

AGrappleAnchor::AGrappleAnchor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	// A 40 cm cube sitting on the origin. The engine cube is the one mesh the C++ can rely on, so
	// an anchor spawned without the Blueprint (tests, a hand-placed actor) is still visible.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetMobility(EComponentMobility::Static);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 20.f));
	Mesh->SetRelativeScale3D(FVector(0.4f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}

	LandingPoint = CreateDefaultSubobject<USceneComponent>(TEXT("LandingPoint"));
	LandingPoint->SetupAttachment(Root);
	LandingPoint->SetMobility(EComponentMobility::Static);
	LandingPoint->SetRelativeLocation(FVector(60.f, 0.f, 0.f));
}

FVector AGrappleAnchor::GetLandingLocation() const
{
	return LandingPoint ? LandingPoint->GetComponentLocation() : GetActorLocation();
}

FVector AGrappleAnchor::GetMarkerLocation() const
{
	return GetActorLocation() + GetActorUpVector() * MarkerHeight;
}

void AGrappleAnchor::AddStuckArrow(AGrappleArrowProjectile* Arrow, AActor* Shooter)
{
	StuckArrows.Add(Arrow);
	StuckArrowShooters.Add(Shooter);
	StuckArrowCount = StuckArrowShooters.Num();
}

int32 AGrappleAnchor::GetStuckArrowCountFor(const AActor* Shooter) const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AActor>& ArrowShooter : StuckArrowShooters)
	{
		if (!Shooter || !ArrowShooter.IsValid() || ArrowShooter.Get() == Shooter)
		{
			++Count;
		}
	}
	return Count;
}

int32 AGrappleAnchor::RecoverStuckArrows(AActor* Shooter)
{
	int32 Recovered = 0;
	for (int32 Index = StuckArrowShooters.Num() - 1; Index >= 0; --Index)
	{
		const TWeakObjectPtr<AActor> ArrowShooter = StuckArrowShooters[Index];
		if (Shooter && ArrowShooter.IsValid() && ArrowShooter.Get() != Shooter)
		{
			continue;
		}
		if (StuckArrows.IsValidIndex(Index))
		{
			if (AGrappleArrowProjectile* Arrow = StuckArrows[Index].Get(); IsValid(Arrow))
			{
				Arrow->Destroy();
			}
			StuckArrows.RemoveAt(Index);
		}
		StuckArrowShooters.RemoveAt(Index);
		++Recovered;
	}
	StuckArrowCount = StuckArrowShooters.Num();
	return Recovered;
}
