#include "Challenge/ChallengeTarget.h"

#include "Challenge/ChallengeRules.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "UObject/ConstructorHelpers.h"
#include "World/TimeOfDaySubsystem.h"

namespace HawkeyeChallengeTarget
{
	static const FLinearColor OuterColor(0.86f, 0.82f, 0.72f);
	static const FLinearColor RingColor(0.72f, 0.05f, 0.04f);
	static const FLinearColor BullseyeColor(1.f, 0.72f, 0.08f);
	static const FLinearColor PostColor(0.05f, 0.05f, 0.055f);
	static const FLinearColor DownColor(0.25f, 0.24f, 0.22f);
	static constexpr float PostDiameter = 5.f;
	static constexpr float FaceThickness = 4.f;
	static constexpr float RingThickness = 1.f;
	/** Degrees a hit face tips back, and how long it takes, s. */
	static constexpr float FallDegrees = 80.f;
	static constexpr float FallSeconds = 0.3f;
}

AChallengeTarget::AChallengeTarget()
{
	PrimaryActorTick.bCanEverTick = true;
	FaceMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_Emissive.M_Emissive")));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	Slider = CreateDefaultSubobject<USceneComponent>(TEXT("Slider"));
	Slider->SetupAttachment(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UStaticMesh* Cylinder = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;
	UMaterialInterface* Material = ShapeMaterial.Succeeded() ? ShapeMaterial.Object : nullptr;
	auto MakePart = [this, Cylinder, Material](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(Cylinder);
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		return Part;
	};

	Post = MakePart(TEXT("Post"), Slider);
	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Slider);
	FacePivot = CreateDefaultSubobject<USceneComponent>(TEXT("FacePivot"));
	FacePivot->SetupAttachment(Hinge);
	OuterDisc = MakePart(TEXT("OuterDisc"), FacePivot);
	RingDisc = MakePart(TEXT("RingDisc"), FacePivot);
	BullseyeDisc = MakePart(TEXT("BullseyeDisc"), FacePivot);

	// Arrows (WorldDynamic sweeps) and the bow's aim trace on the Weapon channel stop on the face;
	// nothing else does, so a target never blocks a run or the camera.
	OuterDisc->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OuterDisc->SetCollisionObjectType(ECC_WorldDynamic);
	OuterDisc->SetCollisionResponseToAllChannels(ECR_Ignore);
	OuterDisc->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	OuterDisc->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	OuterDisc->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);

	LayoutParts();
}

void AChallengeTarget::LayoutParts()
{
	using namespace HawkeyeChallengeTarget;
	const float PostHeight = FMath::Max(FaceHeight - OuterRadius, 1.f);
	Post->SetRelativeLocation(FVector(-FaceThickness, 0.f, PostHeight * 0.5f));
	Post->SetRelativeScale3D(FVector(PostDiameter / 100.f, PostDiameter / 100.f, PostHeight / 100.f));
	Hinge->SetRelativeLocationAndRotation(FVector(0.f, 0.f, PostHeight), FRotator(FacePitch, 0.f, 0.f));
	FacePivot->SetRelativeLocation(FVector(0.f, 0.f, OuterRadius));

	// The engine cylinder is 100 cm across and 100 cm along Z; pitched 90 its axis is the face's X.
	auto Disc = [](UStaticMeshComponent* Part, float Radius, float Thickness, float FrontX)
	{
		Part->SetRelativeLocationAndRotation(FVector(FrontX - Thickness * 0.5f, 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
		Part->SetRelativeScale3D(FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, Thickness / 100.f));
	};
	Disc(OuterDisc, OuterRadius, FaceThickness, FaceThickness * 0.5f);
	Disc(RingDisc, RingRadius, RingThickness, FaceThickness * 0.5f + RingThickness);
	Disc(BullseyeDisc, BullseyeRadius, RingThickness, FaceThickness * 0.5f + RingThickness * 2.f);
}

void AChallengeTarget::Tint(UStaticMeshComponent* Part, const FLinearColor& Color, float Glow)
{
	if (UMaterialInstanceDynamic* Material = Part ? Part->CreateDynamicMaterialInstance(0) : nullptr)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		if (Glow >= 0.f)
		{
			// The night's value; the time of day scales it (3x by day) and keeps it scaled.
			UTimeOfDaySubsystem::SetGlow(Part, Material, Glow);
		}
	}
}

void AChallengeTarget::InitTarget(int32 InIndex, const FChallengeTargetSpawn& Spawn, float InOuterRadius, float InRingRadius,
	float InBullseyeRadius)
{
	using namespace HawkeyeChallengeTarget;
	TargetIndex = InIndex;
	OuterRadius = FMath::Max(InOuterRadius, 1.f);
	RingRadius = FMath::Clamp(InRingRadius, 0.5f, OuterRadius);
	BullseyeRadius = FMath::Clamp(InBullseyeRadius, 0.25f, RingRadius);
	const FRotator Rotation = Spawn.Transform.Rotator();
	FacePitch = Rotation.Pitch;
	SetActorLocationAndRotation(Spawn.Transform.GetLocation(), FRotator(0.f, Rotation.Yaw, 0.f));
	bMoving = Spawn.bMoving && Spawn.TrackLength > 0.f && Spawn.TrackSpeed > 0.f;
	TrackLength = Spawn.TrackLength;
	TrackSpeed = Spawn.TrackSpeed;
	TrackOffset = 0.f;
	TrackDirection = 1.f;
	bDown = false;
	FallAlpha = 0.f;
	LayoutParts();
	// Synchronously, the first target of a run spawns with it; never in a headless test.
	UMaterialInterface* Glow = FApp::CanEverRender() && !FaceMaterial.IsNull() ? FaceMaterial.LoadSynchronous() : nullptr;
	for (UStaticMeshComponent* Disc : { OuterDisc.Get(), RingDisc.Get(), BullseyeDisc.Get() })
	{
		if (Glow)
		{
			Disc->SetMaterial(0, Glow);
		}
	}
	bGlowing = Glow != nullptr;
	const float Intensity = Glow ? FaceGlow : -1.f;
	Tint(OuterDisc, OuterColor, Intensity);
	Tint(RingDisc, RingColor, Intensity);
	Tint(BullseyeDisc, BullseyeColor, Intensity);
	Tint(Post, PostColor);
}

FVector AChallengeTarget::GetFaceCentre() const
{
	return FacePivot ? FacePivot->GetComponentLocation() + GetFaceNormal() * HawkeyeChallengeTarget::FaceThickness * 0.5f
		: GetActorLocation();
}

FVector AChallengeTarget::GetFaceNormal() const
{
	return FacePivot ? FacePivot->GetForwardVector() : GetActorForwardVector();
}

void AChallengeTarget::HandleArrowHit(const FVector& ImpactPoint, AActor* Shooter)
{
	if (bDown)
	{
		return;
	}
	const float Radial = UChallengeRules::RadialDistanceOnFace(ImpactPoint, GetFaceCentre(), GetFaceNormal());
	if (UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this))
	{
		Challenges->ReportTargetHit(this, Radial, Shooter);
	}
}

void AChallengeTarget::KnockDown(int32 Points)
{
	if (bDown)
	{
		return;
	}
	bDown = true;
	SlideVelocity = FVector::ZeroVector;
	Root->ComponentVelocity = FVector::ZeroVector;
	// No more hits once it is scored: arrows fly past it into whatever is behind.
	OuterDisc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Scored: the glow goes out, so what is left to shoot stands out.
	const float Dim = bGlowing ? FaceGlow * 0.1f : -1.f;
	Tint(OuterDisc, Points > 0 ? HawkeyeChallengeTarget::OuterColor : HawkeyeChallengeTarget::DownColor, Dim);
	Tint(RingDisc, HawkeyeChallengeTarget::RingColor, Dim);
	Tint(BullseyeDisc, HawkeyeChallengeTarget::BullseyeColor, Dim);
}

void AChallengeTarget::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TArray<AActor*> Stuck;
	GetAttachedActors(Stuck, true, true);
	for (AActor* Arrow : Stuck)
	{
		if (IsValid(Arrow))
		{
			Arrow->Destroy();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AChallengeTarget::Tick(float DeltaSeconds)
{
	using namespace HawkeyeChallengeTarget;
	Super::Tick(DeltaSeconds);
	if (bDown)
	{
		if (FallAlpha < 1.f)
		{
			FallAlpha = FMath::Min(FallAlpha + DeltaSeconds / FallSeconds, 1.f);
			const float Eased = 1.f - FMath::Square(1.f - FallAlpha);
			Hinge->SetRelativeRotation(FRotator(FacePitch + FallDegrees * Eased, 0.f, 0.f));
		}
		return;
	}
	if (!bMoving)
	{
		return;
	}
	const float Half = TrackLength * 0.5f;
	TrackOffset += TrackDirection * TrackSpeed * DeltaSeconds;
	if (FMath::Abs(TrackOffset) >= Half)
	{
		TrackOffset = FMath::Clamp(TrackOffset, -Half, Half);
		TrackDirection = -TrackDirection;
	}
	Slider->SetRelativeLocation(FVector(0.f, TrackOffset, 0.f));
	SlideVelocity = GetActorRightVector() * TrackDirection * TrackSpeed;
	Root->ComponentVelocity = SlideVelocity;
}
