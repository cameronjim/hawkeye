#include "World/InteriorPortal.h"

#include "Hawkeye.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace HawkeyeInteriorPortal
{
	// The engine's 1 m cube, scaled per part; sizes in cm.
	static const FVector FrameSize(10.f, 124.f, 232.f);
	static const FVector DoorSize(6.f, 100.f, 220.f);
	static const FVector BoardSize(4.f, 140.f, 34.f);
	static constexpr float BoardBottom = 238.f;

	static void PlaceCube(UStaticMeshComponent* Part, const FVector& Size, float FrontX, float BottomZ)
	{
		Part->SetRelativeScale3D(Size / 100.f);
		Part->SetRelativeLocation(FVector(FrontX - Size.X * 0.5f, 0.f, BottomZ + Size.Z * 0.5f));
	}
}

AInteriorPortal::AInteriorPortal()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cube = CubeFinder.Object;

	DoorFrame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorFrame"));
	DoorFrame->SetupAttachment(Root);
	DoorFrame->SetStaticMesh(Cube);
	DoorFrame->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	Door = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Door"));
	Door->SetupAttachment(Root);
	Door->SetStaticMesh(Cube);

	SignBoard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SignBoard"));
	SignBoard->SetupAttachment(Root);
	SignBoard->SetStaticMesh(Cube);
	SignBoard->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	SignText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("SignText"));
	SignText->SetupAttachment(Root);
	SignText->SetHorizontalAlignment(EHTA_Center);
	SignText->SetVerticalAlignment(EVRTA_TextCenter);
	SignText->SetWorldSize(22.f);
	SignText->SetTextRenderColor(FColor(245, 232, 200));
	SignText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DoorLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("DoorLight"));
	DoorLight->SetupAttachment(Root);
	DoorLight->SetIntensityUnits(ELightUnits::Lumens);
	DoorLight->SetIntensity(400.f);
	DoorLight->SetAttenuationRadius(450.f);
	DoorLight->SetLightColor(FLinearColor(1.f, 0.78f, 0.5f));
	DoorLight->SetCastShadows(false);

	EntryZone = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryZone"));
	EntryZone->SetupAttachment(Root);
	EntryZone->SetBoxExtent(FVector(90.f, 90.f, 110.f));
	EntryZone->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	EntryZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EntryZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	EntryZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	EntryZone->SetGenerateOverlapEvents(true);

	LayOutParts(3.f);
}

void AInteriorPortal::LayOutParts(float LeafFrontX)
{
	using namespace HawkeyeInteriorPortal;
	PlaceCube(DoorFrame, FrameSize, LeafFrontX - 1.f, 0.f);
	PlaceCube(Door, DoorSize, LeafFrontX, 0.f);
	PlaceCube(SignBoard, BoardSize, LeafFrontX + 1.f, BoardBottom);
	SignText->SetRelativeLocation(FVector(LeafFrontX + 2.f, 0.f, BoardBottom + BoardSize.Z * 0.5f));
	DoorLight->SetRelativeLocation(FVector(LeafFrontX + 60.f, 0.f, 250.f));
	EntryZone->SetRelativeLocation(FVector(LeafFrontX + 90.f, 0.f, 110.f));
}

void AInteriorPortal::SetSignText(const FText& Text)
{
	if (SignText)
	{
		SignText->SetText(Text);
	}
}

bool AInteriorPortal::CanInteract_Implementation(AActor* Interactor) const
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	return !bTravelling && Pawn && Pawn->IsPlayerControlled();
}

void AInteriorPortal::Interact_Implementation(AActor* Interactor)
{
	Use(Interactor);
}

bool AInteriorPortal::Use(AActor* Interactor)
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	if (bTravelling || !Pawn || !Pawn->IsPlayerControlled())
	{
		return false;
	}
	bTravelling = true;
	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FadeSeconds, FLinearColor::Black, false, /*bHoldWhenFinished=*/true);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: used by %s; fading out over %.2f s."), *GetName(), *Pawn->GetName(), FadeSeconds);
	if (FadeSeconds <= 0.f)
	{
		TravelNow();
		return true;
	}
	GetWorldTimerManager().SetTimer(TravelTimer, this, &AInteriorPortal::TravelNow, FadeSeconds, false);
	return true;
}

void AInteriorPortal::TravelNow()
{
	if (Travel())
	{
		return;
	}
	UE_LOG(LogHawkeye, Warning, TEXT("%s: could not change level; staying."), *GetName());
	bTravelling = false;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StopCameraFade();
	}
}

void AInteriorPortal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(TravelTimer);
	Super::EndPlay(EndPlayReason);
}
