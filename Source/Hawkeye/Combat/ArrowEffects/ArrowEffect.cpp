#include "Combat/ArrowEffects/ArrowEffect.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Combat/ArrowEffects/BolaEffect.h"
#include "Combat/ArrowEffects/EmpPulse.h"
#include "Combat/ArrowEffects/ExplosiveBlast.h"
#include "Combat/ArrowEffects/HeldEffect.h"
#include "Combat/ArrowEffects/SmokeCloud.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Vfx/HawkeyeVfxSubsystem.h"

const TCHAR* const AArrowEffect::FxMaterialPath = TEXT("/Game/Blueprints/Weapons/M_ArrowFx.M_ArrowFx");
const TCHAR* const AArrowEffect::GlowMaterialPath = TEXT("/Game/Blueprints/Weapons/M_ArrowGlow.M_ArrowGlow");

AArrowEffect::AArrowEffect()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Shape(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	SphereMesh = Sphere.Succeeded() ? Sphere.Object : nullptr;
	CubeMesh = Cube.Succeeded() ? Cube.Object : nullptr;
	CylinderMesh = Cylinder.Succeeded() ? Cylinder.Object : nullptr;
	ShapeMaterial = Shape.Succeeded() ? Shape.Object : nullptr;
}

TSubclassOf<AArrowEffect> AArrowEffect::GetDefaultClassFor(EArrowHitEffect Effect)
{
	switch (Effect)
	{
		case EArrowHitEffect::Putty:
			return AHeldEffect::StaticClass();
		case EArrowHitEffect::Bola:
			return ABolaEffect::StaticClass();
		case EArrowHitEffect::Smoke:
			return ASmokeCloud::StaticClass();
		case EArrowHitEffect::EMP:
			return AEmpPulse::StaticClass();
		case EArrowHitEffect::Explosive:
			return AExplosiveBlast::StaticClass();
		default:
			return nullptr;
	}
}

AArrowEffect* AArrowEffect::SpawnForHit(
	UWorld* World, const UArrowDefinition* Arrow, AActor* Shooter, const FHitResult& Hit)
{
	if (!World || !Arrow)
	{
		return nullptr;
	}
	const TSubclassOf<AArrowEffect> Class =
		Arrow->EffectClass ? Arrow->EffectClass : GetDefaultClassFor(Arrow->OnHitEffect);
	if (!Class)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Shooter;
	Params.Instigator = Cast<APawn>(Shooter);
	const FVector Point = Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : FVector(Hit.TraceEnd);
	AArrowEffect* Effect = World->SpawnActor<AArrowEffect>(Class, Point, FRotator::ZeroRotator, Params);
	if (!Effect)
	{
		return nullptr;
	}
	Effect->InitEffect(Arrow, Shooter, Hit);
	Effect->Activate();
	return Effect;
}

void AArrowEffect::InitEffect(const UArrowDefinition* InArrow, AActor* InShooter, const FHitResult& InHit)
{
	Arrow = InArrow;
	Shooter = InShooter;
	HitActor = InHit.GetActor();
	ImpactPoint = InHit.bBlockingHit ? FVector(InHit.ImpactPoint) : GetActorLocation();
	ImpactNormal = InHit.bBlockingHit && !FVector(InHit.ImpactNormal).IsNearlyZero() ? FVector(InHit.ImpactNormal)
																					 : FVector::UpVector;
	HitBone = InHit.BoneName;
	ArrowDamage = InArrow ? InArrow->Damage : 0.f;
	SetActorLocation(ImpactPoint);
}

void AArrowEffect::Activate()
{
	bActivated = true;
	Elapsed = 0.f;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s landed at %s (hit %s)."), *GetName(), *GetNameSafe(Arrow),
		*ImpactPoint.ToCompactString(), *GetNameSafe(HitActor.Get()));
	if (Arrow && Arrow->bEffectSoundFollowsEffect)
	{
		UHawkeyeAudioSubsystem::PlayAttached(Arrow->EffectSound, Root, TEXT("trick arrow effect"));
	}
	else if (Arrow)
	{
		UHawkeyeAudioSubsystem::PlayAt(this, Arrow->EffectSound, ImpactPoint, TEXT("trick arrow effect"));
	}
}

void AArrowEffect::AdvanceEffect(float DeltaSeconds)
{
	if (bActivated)
	{
		Elapsed += DeltaSeconds;
	}
}

void AArrowEffect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AdvanceEffect(DeltaSeconds);
}

UNiagaraComponent* AArrowEffect::SpawnEffectVfx(FName Event, const FVector& Location)
{
	const TSoftObjectPtr<UNiagaraSystem> None;
	// The trick effects are authored Z-up (smoke rises, the EMP ring lies flat): Z along the normal.
	UNiagaraComponent* Spawned = UHawkeyeVfxSubsystem::SpawnAt(this, Arrow ? Arrow->EffectVfx : None, Location,
		FRotationMatrix::MakeFromZ(ImpactNormal).Rotator(), Event);
	bEffectVfxSpawned = bEffectVfxSpawned || Spawned != nullptr;
	return Spawned;
}

void AArrowEffect::HidePlaceholders()
{
	TInlineComponentArray<UStaticMeshComponent*> Parts(this);
	for (UStaticMeshComponent* Part : Parts)
	{
		Part->SetVisibility(false);
	}
}

UStaticMeshComponent* AArrowEffect::MakeVisualPart(const TCHAR* Name, UStaticMesh* Mesh)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(Root);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(false);
	Part->SetGenerateOverlapEvents(false);
	if (Mesh)
	{
		Part->SetStaticMesh(Mesh);
	}
	if (ShapeMaterial)
	{
		Part->SetMaterial(0, ShapeMaterial);
	}
	return Part;
}

UMaterialInterface* AArrowEffect::LoadEffectMaterial(const TCHAR* Path)
{
	// Synchronous: the effect has to look right on the frame it appears, in the standalone game
	// too (claude-docs/testing.md, 2b), where nothing has pulled the material in beforehand.
	if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Loaded;
	}
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

void AArrowEffect::TintPart(
	UStaticMeshComponent* Part, UMaterialInterface* Material, const FLinearColor& Color, float Opacity)
{
	if (!Part)
	{
		return;
	}
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	if (UMaterialInstanceDynamic* Instance = Part->CreateDynamicMaterialInstance(0))
	{
		Instance->SetVectorParameterValue(TEXT("Color"), Color);
		Instance->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Instance->SetScalarParameterValue(TEXT("Intensity"), Opacity);
	}
}
