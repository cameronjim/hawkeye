#include "Combat/ArrowEffects/HeldEffect.h"

#include "Hawkeye.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/HealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

AHeldEffect::AHeldEffect()
{
	Blob = MakeVisualPart(TEXT("Blob"), SphereMesh);
	Splat = MakeVisualPart(TEXT("Splat"), SphereMesh);
	Splat2 = MakeVisualPart(TEXT("Splat2"), SphereMesh);
	for (UStaticMeshComponent* Part : { Blob.Get(), Splat.Get(), Splat2.Get() })
	{
		Part->SetVisibility(false);
	}
}

void AHeldEffect::ShowBlob(bool bWithSplats)
{
	Blob->SetVisibility(true);
	Splat->SetVisibility(bWithSplats);
	Splat2->SetVisibility(bWithSplats);
}

void AHeldEffect::BeginPlay()
{
	Super::BeginPlay();
	// Solid goo with a faint glow of its own: the nock's material takes the same Color parameter.
	UMaterialInterface* Material = LoadEffectMaterial(AArrowProjectile::DefaultNockMaterialPath);
	for (UStaticMeshComponent* Part : { Blob.Get(), Splat.Get(), Splat2.Get() })
	{
		TintPart(Part, Material, PuttyColor);
	}
	// Squashed against his chest: goo rather than a ball. The gobs sit on a shoulder and a hip.
	Blob->SetRelativeScale3D(FVector(BlobSize * 0.55f / 100.f, BlobSize / 100.f, BlobSize * 0.8f / 100.f));
	Splat->SetRelativeLocation(FVector(-4.f, 24.f, 22.f));
	Splat->SetRelativeScale3D(FVector(BlobSize * 0.4f / 100.f));
	Splat2->SetRelativeLocation(FVector(-8.f, -20.f, -50.f));
	Splat2->SetRelativeScale3D(FVector(BlobSize * 0.35f / 100.f));
}

void AHeldEffect::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Release();
	Super::EndPlay(EndPlayReason);
}

AThugCharacter* AHeldEffect::FindThugNear(UWorld* World, const FVector& Point, float Radius)
{
	if (!World)
	{
		return nullptr;
	}
	AThugCharacter* Best = nullptr;
	float BestDistanceSq = FMath::Square(Radius);
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (It->IsLimp() || (Health && !Health->IsAlive()))
		{
			continue;
		}
		// From his feet to his head: putty on the pavement at his feet holds him as surely as putty
		// on the wall beside his shoulder.
		const FVector Closest = FMath::ClosestPointOnSegment(
			Point, It->GetActorLocation() - FVector(0.f, 0.f, 90.f), It->GetActorLocation() + FVector(0.f, 0.f, 80.f));
		const float DistanceSq = FVector::DistSquared(Closest, Point);
		if (DistanceSq <= BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = *It;
		}
	}
	return Best;
}

void AHeldEffect::Activate()
{
	Super::Activate();
	SpawnEffectVfx(UHawkeyeVfxSubsystem::PuttySplatEvent, ImpactPoint);

	AThugCharacter* Thug = Cast<AThugCharacter>(GetHitActor());
	if (!Thug || Thug->IsLimp())
	{
		Thug = FindThugNear(GetWorld(), ImpactPoint, HoldRadius);
	}
	if (!Thug)
	{
		BeginSurfaceBlob();
		return;
	}

	// Already stuck: the fresh putty tops him up rather than stacking a second hold.
	for (TActorIterator<AHeldEffect> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->GetTarget() == Thug && It->GetPhase() != EHeldPhase::Done)
		{
			It->RestartHold();
			UE_LOG(LogHawkeye, Log, TEXT("%s: %s already held by %s; hold restarted."), *GetName(), *Thug->GetName(),
				*It->GetName());
			Phase = EHeldPhase::Done;
			Destroy();
			return;
		}
	}

	Target = Thug;
	Phase = EHeldPhase::Stagger;
	PhaseRemaining = Thug->StaggerSeconds;
	UE_LOG(LogHawkeye, Log, TEXT("%s: putty on %s (%.0f cm from the impact): stagger %.2f s, then held %.1f s."),
		*GetName(), *Thug->GetName(), FVector::Dist(Thug->GetActorLocation(), ImpactPoint), PhaseRemaining,
		HoldSeconds);
	if (PhaseRemaining <= 0.f)
	{
		BeginHold();
	}
}

void AHeldEffect::RestartHold()
{
	if (Phase == EHeldPhase::Held)
	{
		PhaseRemaining = HoldSeconds;
	}
}

void AHeldEffect::BeginHold()
{
	AThugCharacter* Thug = Target.Get();
	if (!Thug || Thug->IsLimp())
	{
		Phase = EHeldPhase::Done;
		Destroy();
		return;
	}

	Phase = EHeldPhase::Held;
	PhaseRemaining = HoldSeconds;
	bHolding = true;
	if (UCharacterMovementComponent* Movement = Thug->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
	{
		Brain->SetHeld(true);
	}

	AttachToComponent(Thug->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(BlobOffset);
	ShowBlob(true);
}

void AHeldEffect::Release()
{
	if (!bHolding)
	{
		return;
	}
	bHolding = false;
	AThugCharacter* Thug = Target.Get();
	if (!Thug)
	{
		return;
	}
	if (!Thug->IsLimp() && !Thug->IsKnockedDown())
	{
		if (UCharacterMovementComponent* Movement = Thug->GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
	{
		Brain->SetHeld(false);
	}
}

void AHeldEffect::BeginSurfaceBlob()
{
	Phase = EHeldPhase::SurfaceBlob;
	PhaseRemaining = SurfaceBlobSeconds;
	// Half sunk into the wall, flattened along its normal.
	SetActorLocationAndRotation(ImpactPoint, FRotationMatrix::MakeFromZ(ImpactNormal).Rotator());
	Blob->SetRelativeScale3D(FVector(BlobSize / 100.f, BlobSize / 100.f, BlobSize * 0.45f / 100.f));
	ShowBlob(false);
	if (USceneComponent* Surface = GetHitActor() ? GetHitActor()->GetRootComponent() : nullptr)
	{
		AttachToComponent(Surface, FAttachmentTransformRules::KeepWorldTransform);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: putty blob on %s for %.0f s, nobody within %.0f cm."), *GetName(),
		*GetNameSafe(GetHitActor()), SurfaceBlobSeconds, HoldRadius);
}

void AHeldEffect::AdvanceEffect(float DeltaSeconds)
{
	Super::AdvanceEffect(DeltaSeconds);
	if (Phase == EHeldPhase::None || Phase == EHeldPhase::Done)
	{
		return;
	}

	PhaseRemaining -= DeltaSeconds;
	if (Phase == EHeldPhase::Held)
	{
		const AThugCharacter* Thug = Target.Get();
		if (!Thug || Thug->IsLimp())
		{
			PhaseRemaining = 0.f;
		}
	}
	if (PhaseRemaining > 0.f)
	{
		return;
	}

	switch (Phase)
	{
		case EHeldPhase::Stagger:
			BeginHold();
			break;
		case EHeldPhase::Held:
			UE_LOG(LogHawkeye, Log, TEXT("%s: putty lets go of %s."), *GetName(), *GetNameSafe(Target.Get()));
			Release();
			Phase = EHeldPhase::Done;
			Destroy();
			break;
		case EHeldPhase::SurfaceBlob:
			Phase = EHeldPhase::Done;
			Destroy();
			break;
		default:
			break;
	}
}
