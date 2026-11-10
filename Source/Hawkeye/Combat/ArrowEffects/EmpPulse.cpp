#include "Combat/ArrowEffects/EmpPulse.h"

#include "Hawkeye.h"
#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/HawkeyeCharacter.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

AEmpPulse::AEmpPulse()
{
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		UStaticMeshComponent* Segment = MakeVisualPart(*FString::Printf(TEXT("RingSegment%d"), Index), CubeMesh);
		Segment->SetRelativeScale3D(FVector(0.01f));
		Segments.Add(Segment);
	}

	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(Root);
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->SetCastShadows(false);
	Flash->SetIntensityUnits(ELightUnits::Candelas);
	Flash->SetIntensity(0.f);
	Flash->SetAttenuationRadius(900.f);
	Flash->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
}

void AEmpPulse::BeginPlay()
{
	Super::BeginPlay();
	Flash->SetLightColor(PulseColor);
	UMaterialInterface* Material = LoadEffectMaterial(GlowMaterialPath);
	SegmentMaterials.Reset();
	for (UStaticMeshComponent* Segment : Segments)
	{
		TintPart(Segment, Material, PulseColor, 0.f);
		SegmentMaterials.Add(Segment ? Cast<UMaterialInstanceDynamic>(Segment->GetMaterial(0)) : nullptr);
	}
}

int32 AEmpPulse::JamGunners()
{
	int32 Jammed = 0;
	for (TActorIterator<AThugCharacter> It(GetWorld()); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (!It->IsGunner() || It->IsLimp() || (Health && !Health->IsAlive()) ||
			FVector::DistSquared(It->GetActorLocation(), ImpactPoint) > FMath::Square(Radius))
		{
			continue;
		}
		if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
		{
			Brain->Jam(JamSeconds);
			++Jammed;
		}
	}
	return Jammed;
}

void AEmpPulse::Activate()
{
	Super::Activate();
	// Lamps are measured across the ground: their lights and heads sit 7 m up, which a sphere from
	// a pulse on the pavement would never reach.
	if (UArrowEffectsSubsystem* Effects = UArrowEffectsSubsystem::Get(this))
	{
		LampsAffected = Effects->DisableLampsInRadius(ImpactPoint, Radius, LampOffSeconds);
	}
	GunnersJammed = JamGunners();
	UE_LOG(LogHawkeye, Log,
		TEXT("%s: EMP %.0f cm at %s: %d lamp actor(s) dark for %.0f s, %d gunner(s) jammed for %.0f s."), *GetName(),
		Radius, *ImpactPoint.ToCompactString(), LampsAffected, LampOffSeconds, GunnersJammed, JamSeconds);
	SetActorLocation(ImpactPoint + FVector(0.f, 0.f, 10.f));
	if (SpawnEffectVfx(UHawkeyeVfxSubsystem::EmpPulseEvent, ImpactPoint + FVector(0.f, 0.f, 10.f)))
	{
		HidePlaceholders();
	}
	PulseScreens();
	UpdateRing();
}

void AEmpPulse::PulseScreens() const
{
	// Every player's own screen, stronger the nearer the pulse.
	for (TActorIterator<AHawkeyeCharacter> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), ImpactPoint);
		if (It->IsPlayerControlled() && Distance <= ScreenPulseRadius)
		{
			It->PlayScreenPulse(ScreenPulseSeconds, FMath::Lerp(1.f, 0.35f, Distance / FMath::Max(ScreenPulseRadius, 1.f)));
		}
	}
}

void AEmpPulse::AdvanceEffect(float DeltaSeconds)
{
	Super::AdvanceEffect(DeltaSeconds);
	if (!bActivated)
	{
		return;
	}
	UpdateRing();
	if (Elapsed >= RingSeconds)
	{
		Destroy();
	}
}

void AEmpPulse::UpdateRing()
{
	const float Alpha = FMath::Clamp(Elapsed / RingSeconds, 0.f, 1.f);
	// Fast out, easing to a stop at the edge of the radius.
	const float RingRadius = Radius * (1.f - FMath::Square(1.f - Alpha));
	const float Brightness = 1.f - Alpha * Alpha;
	const float Circumference = 2.f * PI * FMath::Max(RingRadius, 1.f);
	const float SegmentLength = Circumference / SegmentCount * 0.7f;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		UStaticMeshComponent* Segment = Segments[Index];
		if (!Segment)
		{
			continue;
		}
		const float Angle = 2.f * PI * Index / SegmentCount;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		// Each segment a flat bar along the ring's tangent; the engine cube is 100 cm on a side.
		Segment->SetRelativeLocationAndRotation(
			Out * RingRadius, FRotator(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f));
		Segment->SetRelativeScale3D(FVector(SegmentLength / 100.f, 0.3f, 0.12f));
		if (SegmentMaterials.IsValidIndex(Index) && SegmentMaterials[Index])
		{
			SegmentMaterials[Index]->SetScalarParameterValue(TEXT("Intensity"), 3.f * Brightness);
		}
	}
	if (Flash)
	{
		// A blink, not a floodlight: the point of the shot is that it goes dark.
		Flash->SetIntensity(400.f * FMath::Max(0.f, 1.f - Alpha * 4.f));
	}
}
