#include "Combat/ArrowEffects/ExplosiveBlast.h"

#include "Settings/HawkeyeAccessibility.h"
#include "Hawkeye.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/HealthComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/HawkeyeCharacter.h"
#include "Vfx/HawkeyeVfxMath.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/ThugCharacter.h"

AExplosiveBlast::AExplosiveBlast()
{
	Fireball = MakeVisualPart(TEXT("Fireball"), SphereMesh);
	Fireball->SetRelativeScale3D(FVector(0.01f));
	FireballCore = MakeVisualPart(TEXT("FireballCore"), SphereMesh);
	FireballCore->SetRelativeScale3D(FVector(0.01f));

	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(Root);
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->SetCastShadows(false);
	Flash->SetIntensityUnits(ELightUnits::Candelas);
	Flash->SetIntensity(0.f);
	Flash->SetAttenuationRadius(2000.f);
	Flash->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
}

void AExplosiveBlast::BeginPlay()
{
	Super::BeginPlay();
	Flash->SetLightColor(FireColor);
	UMaterialInterface* Material = LoadEffectMaterial(GlowMaterialPath);
	TintPart(Fireball, Material, FireColor, 0.f);
	TintPart(FireballCore, Material, FLinearColor(1.f, 0.85f, 0.5f), 0.f);
	FireballMaterial = Cast<UMaterialInstanceDynamic>(Fireball->GetMaterial(0));
	CoreMaterial = Cast<UMaterialInstanceDynamic>(FireballCore->GetMaterial(0));
}

float AExplosiveBlast::ComputeFalloffDamage(float MaxDamage, float Distance, float InRadius)
{
	if (InRadius <= 0.f || MaxDamage <= 0.f)
	{
		return 0.f;
	}
	return MaxDamage * FMath::Clamp(1.f - FMath::Max(Distance, 0.f) / InRadius, 0.f, 1.f);
}

void AExplosiveBlast::Activate()
{
	Super::Activate();
	FlashScale = UHawkeyeAccessibility::GetFlashScaleFor(this);
	SetActorLocation(ImpactPoint);
	ApplyBlast();
	if (SpawnEffectVfx(UHawkeyeVfxSubsystem::ExplosionEvent, ImpactPoint))
	{
		HidePlaceholders();
	}
	LeaveScorch();
	UpdateFireball();
}

void AExplosiveBlast::LeaveScorch()
{
	FHitResult Ground;
	FVector Location;
	FRotator Rotation;
	if (!Arrow || !HawkeyeVfxMath::TraceGround(GetWorld(), ImpactPoint, ScorchDropCm, Ground, this)
		|| !HawkeyeVfxMath::ComputeScorchPlacement(Ground, FMath::FRandRange(0.f, 360.f), Location, Rotation))
	{
		return;
	}
	UHawkeyeVfxSubsystem::SpawnDecal(this, Arrow->GroundDecal, Location, Rotation, Arrow->GroundDecalRadius,
		Arrow->GroundDecalSeconds, Arrow->GroundDecalSeconds * 0.25f, UHawkeyeVfxSubsystem::ScorchEvent);
}

void AExplosiveBlast::ApplyBlast()
{
	AActor* ShooterActor = GetShooter();
	TArray<AActor*> Victims;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsAlive() &&
			FVector::DistSquared(It->GetActorLocation(), ImpactPoint) < FMath::Square(Radius))
		{
			Victims.Add(*It);
		}
	}

	for (AActor* Victim : Victims)
	{
		UHealthComponent* Health = Victim->FindComponentByClass<UHealthComponent>();
		const float Distance = FVector::Dist(Victim->GetActorLocation(), ImpactPoint);
		// Her partner's blast still hurts, at PartnerDamageScale; the shooter takes her own in full.
		const float Damage = ComputeFalloffDamage(ArrowDamage, Distance, Radius)
			* (AArrowProjectile::IsPartnerOf(ShooterActor, Victim) ? PartnerDamageScale : 1.f);
		if (!Health || Damage <= 0.f)
		{
			continue;
		}
		Health->ApplyDamage(Damage, ShooterActor ? ShooterActor : this);
		++VictimCount;
		UE_LOG(LogHawkeye, Log, TEXT("%s: blast hits %s at %.0f cm for %.1f, health now %.1f."), *GetName(),
			*Victim->GetName(), Distance, Damage, Health->GetCurrentHealth());

		AThugCharacter* Thug = Cast<AThugCharacter>(Victim);
		if (Thug && Health->IsAlive())
		{
			const float Throw = ThrowSpeed * FMath::Clamp(1.f - Distance / Radius, 0.35f, 1.f);
			Thug->KnockdownFor(this, KnockdownSeconds, Throw);
		}
	}

	for (TActorIterator<AHawkeyeCharacter> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), ImpactPoint);
		if (Distance <= ShakeRadius)
		{
			It->PlayImpactShake(ShakeSeconds, ShakeAmplitude * FMath::Clamp(1.f - Distance / ShakeRadius, 0.25f, 1.f));
			It->SuppressMotionBlur(MotionBlurOffSeconds);
		}
	}
}

void AExplosiveBlast::AdvanceEffect(float DeltaSeconds)
{
	Super::AdvanceEffect(DeltaSeconds);
	if (!bActivated)
	{
		return;
	}
	UpdateFireball();
	if (Elapsed >= FlashSeconds)
	{
		Destroy();
	}
}

void AExplosiveBlast::UpdateFireball()
{
	const float Alpha = FMath::Clamp(Elapsed / FlashSeconds, 0.f, 1.f);
	// Out fast to most of the radius, then hangs and fades.
	const float Size = Radius * 0.6f * FMath::Sin(FMath::Min(Alpha * 2.5f, 1.f) * HALF_PI);
	const float Fade = 1.f - FMath::SmoothStep(0.25f, 1.f, Alpha);
	Fireball->SetRelativeLocation(FVector(0.f, 0.f, Size * 0.3f));
	Fireball->SetRelativeScale3D(FVector(FMath::Max(Size, 1.f) / 100.f));
	FireballCore->SetRelativeLocation(FVector(0.f, 0.f, Size * 0.25f));
	FireballCore->SetRelativeScale3D(FVector(FMath::Max(Size * 0.55f, 1.f) / 100.f));
	if (FireballMaterial)
	{
		FireballMaterial->SetScalarParameterValue(TEXT("Intensity"), 0.5f * Fade);
	}
	if (CoreMaterial)
	{
		CoreMaterial->SetScalarParameterValue(TEXT("Intensity"), 1.2f * Fade);
	}
	// A street lamp is under 100 cd; this is a couple of dozen of them for an instant. With the
	// Niagara fireball glowing on its own the light only has to throw the colour, not be the blast.
	Flash->SetIntensity((bEffectVfxSpawned ? BlastLightCandelas : PlaceholderLightCandelas) * Fade * Fade * FlashScale);
}

float AExplosiveBlast::GetFlashIntensity() const
{
	return Flash ? Flash->Intensity : 0.f;
}
