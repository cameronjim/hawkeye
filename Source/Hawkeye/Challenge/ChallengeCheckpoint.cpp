#include "Challenge/ChallengeCheckpoint.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/App.h"
#include "UObject/ConstructorHelpers.h"
#include "World/TimeOfDaySubsystem.h"

namespace HawkeyeChallengeCheckpoint
{
	static constexpr int32 SegmentCount = 24;
	static const TCHAR* GlowPath = TEXT("/Game/Materials/M_Emissive.M_Emissive");
	static const FLinearColor Purple(0.62f, 0.25f, 1.f);
	static const FLinearColor DimColor(0.2f, 0.12f, 0.3f);
}

AChallengeCheckpoint::AChallengeCheckpoint()
{
	using namespace HawkeyeChallengeCheckpoint;
	PrimaryActorTick.bCanEverTick = true;
	GlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(GlowPath));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	Spinner = CreateDefaultSubobject<USceneComponent>(TEXT("Spinner"));
	Spinner->SetupAttachment(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		UStaticMeshComponent* Segment = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Segment%02d"), Index));
		Segment->SetupAttachment(Spinner);
		if (CubeFinder.Succeeded())
		{
			Segment->SetStaticMesh(CubeFinder.Object);
		}
		if (ShapeMaterial.Succeeded())
		{
			Segment->SetMaterial(0, ShapeMaterial.Object);
		}
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetCanEverAffectNavigation(false);
		Segment->SetCastShadow(false);
		Segment->SetGenerateOverlapEvents(false);
		Segments.Add(Segment);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetIntensityUnits(ELightUnits::Lumens);
	Light->SetIntensity(350.f);
	Light->SetAttenuationRadius(600.f);
	Light->SetLightColor(Purple);
	Light->SetCastShadows(false);
	LayoutSegments();
	SetState(EChallengeCheckpointState::Hidden);
}

void AChallengeCheckpoint::LayoutSegments()
{
	using namespace HawkeyeChallengeCheckpoint;
	// Each segment is a cube stretched along the circle's tangent, long enough to close the gaps.
	const float Chord = 2.f * RingRadius * FMath::Sin(PI / SegmentCount) * 1.08f;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const float Angle = 360.f * Index / Segments.Num();
		const float Radians = FMath::DegreesToRadians(Angle);
		const FVector Offset(0.f, FMath::Cos(Radians) * RingRadius, FMath::Sin(Radians) * RingRadius);
		// The cube's local Y along the circle's tangent, its X along the ring's axis.
		const FVector Tangent(0.f, -FMath::Sin(Radians), FMath::Cos(Radians));
		Segments[Index]->SetRelativeLocationAndRotation(Offset, FRotationMatrix::MakeFromXY(FVector::ForwardVector, Tangent).Rotator());
		Segments[Index]->SetRelativeScale3D(FVector(TubeThickness / 100.f, Chord / 100.f, TubeThickness / 100.f));
	}
}

void AChallengeCheckpoint::BeginPlay()
{
	Super::BeginPlay();
	LayoutSegments();
	// Synchronously: a ring spawned mid-run must glow on its first frame.
	LoadedGlow = GlowMaterial.IsNull() || !FApp::CanEverRender() ? nullptr : GlowMaterial.LoadSynchronous();
	bLookReady = true;
	SetState(State);
}

void AChallengeCheckpoint::SetState(EChallengeCheckpointState NewState)
{
	using namespace HawkeyeChallengeCheckpoint;
	State = NewState;
	const bool bVisible = State == EChallengeCheckpointState::Next || State == EChallengeCheckpointState::Upcoming;
	Spinner->SetVisibility(bVisible, true);
	Light->SetVisibility(State == EChallengeCheckpointState::Next);
	// Materials only once the ring has begun play (its constructor sets the state too).
	for (UStaticMeshComponent* Segment : bLookReady ? Segments : TArray<TObjectPtr<UStaticMeshComponent>>())
	{
		if (!Segment || !Segment->GetMaterial(0))
		{
			continue;
		}
		const UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Segment->GetMaterial(0));
		if (LoadedGlow && Segment->GetMaterial(0) != LoadedGlow && (!Existing || Existing->Parent != LoadedGlow))
		{
			Segment->SetMaterial(0, LoadedGlow);
		}
		if (UMaterialInstanceDynamic* Material = Segment->CreateDynamicMaterialInstance(0))
		{
			const bool bNext = State == EChallengeCheckpointState::Next;
			Material->SetVectorParameterValue(TEXT("Color"), bNext ? Purple : DimColor);
			UTimeOfDaySubsystem::SetGlow(Segment, Material, bNext ? NextGlow : NextGlow * 0.25f);
		}
	}
	if (!IsTemplate())
	{
		SetActorTickEnabled(State == EChallengeCheckpointState::Next);
	}
}

void AChallengeCheckpoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Spinner->AddLocalRotation(FRotator(0.f, 0.f, SpinDegreesPerSecond * DeltaSeconds));
}
