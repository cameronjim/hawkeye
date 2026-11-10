#include "World/ArrowSwitch.h"

#include "Hawkeye.h"
#include "Combat/ReturningArrowProjectile.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Mission/ChapterEventSubsystem.h"
#include "UObject/ConstructorHelpers.h"

AArrowSwitch::AArrowSwitch()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Box"));
	RootComponent = Box;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Box->SetStaticMesh(Cube.Object);
		// 30 x 25 x 8 cm, its face out along +X from the wall.
		Box->SetRelativeScale3D(FVector(0.08f, 0.3f, 0.25f));
	}
	Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Box->SetCanEverAffectNavigation(false);
	Box->SetMobility(EComponentMobility::Movable);

	Lamp = CreateDefaultSubobject<UPointLightComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Box);
	Lamp->SetRelativeLocation(FVector(60.f, 0.f, 0.f));
	Lamp->SetIntensityUnits(ELightUnits::Lumens);
	Lamp->SetIntensity(40.f);
	Lamp->SetAttenuationRadius(160.f);
	Lamp->SetLightColor(FLinearColor(1.f, 0.05f, 0.03f));
	Lamp->SetCastShadows(false);
}

FName AArrowSwitch::EventFor(FName Id)
{
	return FName(*FString::Printf(TEXT("switch:%s"), *Id.ToString()));
}

bool AArrowSwitch::Trips(bool bTrippedAlready, bool bReturningOnly, bool bProjectileReturns)
{
	return !bTrippedAlready && (!bReturningOnly || bProjectileReturns);
}

void AArrowSwitch::SetMesh(UStaticMesh* Mesh)
{
	if (Mesh && Box)
	{
		Box->SetStaticMesh(Mesh);
		Box->SetRelativeScale3D(FVector::OneVector);
	}
}

bool AArrowSwitch::HandleArrowHit(AActor* Projectile, AActor* Shooter)
{
	const bool bReturns = Projectile && Projectile->IsA<AReturningArrowProjectile>();
	if (!Trips(bTripped, bReturningArrowsOnly, bReturns))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s did not trip it (%s)."), *GetName(), *GetNameSafe(Projectile),
			bTripped ? TEXT("already tripped") : TEXT("it wants a returning arrow"));
		return false;
	}
	bTripped = true;
	if (Lamp)
	{
		Lamp->SetVisibility(false);
	}
	// The buzz: a noise at the box, heard (or not) as any noise is. Kate made it, as she makes her footsteps.
	if (APawn* By = Cast<APawn>(Shooter); By && NoiseLoudness > 0.f)
	{
		By->MakeNoise(NoiseLoudness, By, GetActorLocation());
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: switch %s tripped by %s (noise %.1f)."), *GetName(), *SwitchId.ToString(), *GetNameSafe(Projectile),
		NoiseLoudness);
	UChapterEventSubsystem::NotifyEvent(this, EventFor(SwitchId));
	OnTripped.Broadcast(this);
	return true;
}
