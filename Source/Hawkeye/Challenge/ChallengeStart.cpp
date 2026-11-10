#include "Challenge/ChallengeStart.h"

#include "Hawkeye.h"
#include "Camera/PlayerCameraManager.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/App.h"
#include "World/TimeOfDaySubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace HawkeyeChallengeStart
{
	static const TCHAR* GlowPath = TEXT("/Game/Materials/M_Emissive.M_Emissive");
	// Deep and saturated: a paler purple clips toward white on the cap long before it glows.
	static const FLinearColor Purple(0.5f, 0.06f, 1.f);
	static const FLinearColor Steel(0.035f, 0.035f, 0.04f);
	static constexpr float PedestalHeight = 100.f;
	static constexpr float PedestalDiameter = 70.f;
	static constexpr float IconHeight = 175.f;
	static constexpr float LabelHeight = 232.f;
	static constexpr int32 RingSegments = 12;
	static constexpr float StandOut = 150.f;
}

AChallengeStart::AChallengeStart()
{
	using namespace HawkeyeChallengeStart;
	PrimaryActorTick.bCanEverTick = true;
	GlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(GlowPath));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* Shape = ShapeMaterial.Succeeded() ? ShapeMaterial.Object : nullptr;
	auto MakePart = [this, Shape](const FName Name, UStaticMesh* Mesh, USceneComponent* Parent, bool bCollide)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(Mesh);
		if (Shape)
		{
			Part->SetMaterial(0, Shape);
		}
		if (!bCollide)
		{
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCastShadow(false);
		}
		Part->SetCanEverAffectNavigation(bCollide);
		return Part;
	};
	UStaticMesh* Cylinder = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;
	UStaticMesh* Cube = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;

	Pedestal = MakePart(TEXT("Pedestal"), Cylinder, Root, true);
	Pedestal->SetRelativeLocation(FVector(0.f, 0.f, PedestalHeight * 0.5f));
	Pedestal->SetRelativeScale3D(FVector(PedestalDiameter / 100.f, PedestalDiameter / 100.f, PedestalHeight / 100.f));

	Cap = MakePart(TEXT("Cap"), Cylinder, Root, false);
	Cap->SetRelativeLocation(FVector(0.f, 0.f, PedestalHeight + 2.f));
	Cap->SetRelativeScale3D(FVector((PedestalDiameter + 6.f) / 100.f, (PedestalDiameter + 6.f) / 100.f, 0.04f));

	IconSpinner = CreateDefaultSubobject<USceneComponent>(TEXT("IconSpinner"));
	IconSpinner->SetupAttachment(Root);
	IconSpinner->SetRelativeLocation(FVector(0.f, 0.f, IconHeight));

	// Archery: a 40 cm target face, three discs deep, facing +X.
	const float Radii[] = { 20.f, 13.f, 6.f };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UStaticMeshComponent* Disc = MakePart(*FString::Printf(TEXT("TargetIcon%d"), Index), Cylinder, IconSpinner, false);
		Disc->SetRelativeLocationAndRotation(FVector(1.5f * Index, 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
		Disc->SetRelativeScale3D(FVector(Radii[Index] / 50.f, Radii[Index] / 50.f, 0.03f));
		TargetIcon.Add(Disc);
	}
	// Traversal: a 44 cm ring in the YZ plane.
	for (int32 Index = 0; Index < RingSegments; ++Index)
	{
		const float Radians = 2.f * PI * Index / RingSegments;
		const FVector Offset(0.f, FMath::Cos(Radians) * 22.f, FMath::Sin(Radians) * 22.f);
		const FVector Tangent(0.f, -FMath::Sin(Radians), FMath::Cos(Radians));
		UStaticMeshComponent* Segment = MakePart(*FString::Printf(TEXT("RingIcon%02d"), Index), Cube, IconSpinner, false);
		Segment->SetRelativeLocationAndRotation(Offset, FRotationMatrix::MakeFromXY(FVector::ForwardVector, Tangent).Rotator());
		Segment->SetRelativeScale3D(FVector(0.05f, 2.f * 22.f * FMath::Sin(PI / RingSegments) * 1.1f / 100.f, 0.05f));
		RingIcon.Add(Segment);
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(22.f);
	Label->SetTextRenderColor(FColor(235, 215, 255));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Root);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, IconHeight));
	Glow->SetIntensityUnits(ELightUnits::Lumens);
	// A pool round the pedestal, not a wash over the whole roof: the emissive top carries the read.
	Glow->SetIntensity(GlowLumens);
	Glow->SetAttenuationRadius(GlowRadius);
	Glow->SetLightColor(Purple);
	Glow->SetCastShadows(false);

	EntryZone = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryZone"));
	EntryZone->SetupAttachment(Root);
	EntryZone->SetBoxExtent(FVector(170.f, 170.f, 120.f));
	EntryZone->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	EntryZone->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	EntryZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EntryZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	EntryZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	EntryZone->SetGenerateOverlapEvents(true);
	EntryZone->SetCanEverAffectNavigation(false);
}

void AChallengeStart::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshLook();
}

void AChallengeStart::BeginPlay()
{
	Super::BeginPlay();
	RefreshLook();
}

void AChallengeStart::RefreshLook()
{
	using namespace HawkeyeChallengeStart;
	const bool bArchery = !Definition || Definition->Type == EChallengeType::Archery;
	for (UStaticMeshComponent* Part : TargetIcon)
	{
		Part->SetVisibility(bArchery);
	}
	for (UStaticMeshComponent* Part : RingIcon)
	{
		Part->SetVisibility(!bArchery);
	}
	Label->SetText(Definition ? Definition->GetDisplayName() : NSLOCTEXT("Hawkeye", "ChallengeUnset", "[Challenge]"));

	if (UMaterialInstanceDynamic* Body = Pedestal->CreateDynamicMaterialInstance(0))
	{
		Body->SetVectorParameterValue(TEXT("Color"), Steel);
	}
	// The glow only in a game that draws: the editor placing a pedestal, and headless tests, never load it.
	// M_Emissive per part: the cap and a ring icon purple, a target icon in its three ring colours.
	UWorld* World = GetWorld();
	UMaterialInterface* Emissive = World && World->IsGameWorld() && FApp::CanEverRender() && !GlowMaterial.IsNull()
		? GlowMaterial.LoadSynchronous() : nullptr;
	// The night's values; the time of day scales them (GlowScale, 3x by day) and relights them when it changes.
	auto Light = [Emissive](UStaticMeshComponent* Part, const FLinearColor& Color, float Intensity)
	{
		if (Emissive)
		{
			Part->SetMaterial(0, Emissive);
		}
		if (UMaterialInstanceDynamic* Material = Part->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			UTimeOfDaySubsystem::SetGlow(Part, Material, Intensity);
		}
	};
	Light(Cap, Purple, CapGlow);
	if (bArchery && TargetIcon.Num() == 3)
	{
		const FLinearColor Colors[] = { FLinearColor(0.86f, 0.82f, 0.72f), FLinearColor(0.72f, 0.05f, 0.04f), FLinearColor(1.f, 0.72f, 0.08f) };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Light(TargetIcon[Index], Colors[Index], IconGlow);
		}
		return;
	}
	for (UStaticMeshComponent* Part : RingIcon)
	{
		Light(Part, Purple, IconGlow * 2.f);
	}
}

void AChallengeStart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	IconSpinner->AddLocalRotation(FRotator(0.f, IconSpinDegreesPerSecond * DeltaSeconds, 0.f));
	// The name turns to face the camera so it reads from any side.
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
		Label->SetWorldRotation(FRotator(0.f, ToCamera.Rotation().Yaw, 0.f));
	}
}

FVector AChallengeStart::GetStandLocation() const
{
	return GetActorLocation() + GetActorForwardVector().GetSafeNormal2D() * HawkeyeChallengeStart::StandOut;
}

bool AChallengeStart::CanInteract_Implementation(AActor* Interactor) const
{
	const APawn* Pawn = Cast<APawn>(Interactor);
	const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	return Definition && Pawn && Pawn->IsPlayerControlled() && Challenges && !Challenges->IsRunning();
}

FText AChallengeStart::GetInteractPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Hawkeye", "ChallengePrompt", "[E] Start {0}"),
		Definition ? Definition->GetDisplayName() : FText::GetEmpty());
}

void AChallengeStart::Interact_Implementation(AActor* Interactor)
{
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	APawn* Pawn = Cast<APawn>(Interactor);
	if (!Challenges || !Pawn)
	{
		return;
	}
	if (!Challenges->StartChallenge(this, Pawn))
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s could not start %s."), *GetName(), *Pawn->GetName(), *GetNameSafe(Definition));
	}
}
