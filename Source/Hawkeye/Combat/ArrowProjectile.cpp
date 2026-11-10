#include "Combat/ArrowProjectile.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioMath.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Challenge/ChallengeTarget.h"
#include "Components/AudioComponent.h"
#include "CollisionQueryParams.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "HawkeyePlayerController.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/ArrowSwitch.h"
#include "World/ThugCharacter.h"

namespace HawkeyeArrow
{
	/** How far back along the flight path the bone probe starts, and how far past the impact it reaches. */
	static constexpr float BoneProbeBack = 100.f;
	static constexpr float BoneProbeAhead = 150.f;

	/** The body walk starts this far before the capsule impact and reaches BoneProbeAhead past it, cm. */
	static constexpr float BodyWalkBack = 30.f;
	/** A point this close to a shape counts as inside it, cm. */
	static constexpr float BodyContact = 0.5f;
	/** The walk's smallest step and most steps. */
	static constexpr float BodyMinStep = 1.f;
	static constexpr int32 BodyMaxSteps = 128;
}

bool AArrowProjectile::IsPartnerOf(const AActor* Shooter, const AActor* Victim)
{
	return Shooter && Victim && Shooter != Victim && Shooter->IsA<AHawkeyeCharacter>() && Victim->IsA<AHawkeyeCharacter>();
}

USkeletalMeshComponent* AArrowProjectile::FindBodyMesh(const AActor* Actor)
{
	// The body people see, so a stuck arrow sits in her surface and moves with it (the hidden driver's shapes
	// are the mannequin's). Its physics asset gives the shapes; it has no collision of its own.
	if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Actor))
	{
		USkeletalMeshComponent* Seen = Hawkeye->GetBodyMesh();
		if (Seen && Seen->GetSkeletalMeshAsset() && Seen->GetPhysicsAsset())
		{
			return Seen;
		}
	}
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh(); Mesh && Mesh->GetSkeletalMeshAsset())
		{
			return Mesh;
		}
	}
	return Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

bool AArrowProjectile::FindBodyEntry(const USkeletalMeshComponent& Body, const FVector& Start, const FVector& Direction,
	float Length, FVector& OutPoint, FName& OutBone, bool& bOutEntered)
{
	bOutEntered = false;
	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsNearlyZero() || !Body.GetSkeletalMeshAsset())
	{
		return false;
	}

	struct FShape
	{
		const USkeletalBodySetup* Setup;
		FTransform BoneToWorld;
		FName Bone;
	};
	TArray<FShape, TInlineAllocator<32>> Shapes;
	if (const UPhysicsAsset* Physics = Body.GetPhysicsAsset())
	{
		for (const TObjectPtr<USkeletalBodySetup>& Setup : Physics->SkeletalBodySetups)
		{
			const int32 BoneIndex = Setup ? Body.GetBoneIndex(Setup->BoneName) : INDEX_NONE;
			if (BoneIndex != INDEX_NONE && Setup->AggGeom.GetElementCount() > 0)
			{
				Shapes.Add({ Setup.Get(), Body.GetBoneTransform(BoneIndex), Setup->BoneName });
			}
		}
	}

	if (Shapes.Num() > 0)
	{
		// Sphere tracing: step along the line by the distance to the nearest shape, which never steps over one.
		float Along = 0.f;
		float NearestDistance = TNumericLimits<float>::Max();
		FVector NearestOnLine = Start;
		int32 NearestShape = INDEX_NONE;
		for (int32 Step = 0; Step < HawkeyeArrow::BodyMaxSteps && Along <= Length; ++Step)
		{
			const FVector Point = Start + Dir * Along;
			float StepDistance = TNumericLimits<float>::Max();
			int32 StepShape = INDEX_NONE;
			for (int32 Index = 0; Index < Shapes.Num(); ++Index)
			{
				const float Distance = Shapes[Index].Setup->GetShortestDistanceToPoint(Point, Shapes[Index].BoneToWorld);
				if (Distance >= 0.f && Distance < StepDistance)
				{
					StepDistance = Distance;
					StepShape = Index;
				}
			}
			if (StepShape == INDEX_NONE)
			{
				break;
			}
			if (StepDistance <= HawkeyeArrow::BodyContact)
			{
				OutPoint = Point;
				OutBone = Shapes[StepShape].Bone;
				bOutEntered = true;
				return true;
			}
			if (StepDistance < NearestDistance)
			{
				NearestDistance = StepDistance;
				NearestOnLine = Point;
				NearestShape = StepShape;
			}
			Along += FMath::Max(StepDistance, HawkeyeArrow::BodyMinStep);
		}
		if (NearestShape != INDEX_NONE)
		{
			// Through the capsule without touching the body: into the nearest shape, where it is nearest the line.
			FVector Closest;
			FVector Normal;
			Shapes[NearestShape].Setup->GetClosestPointAndNormal(NearestOnLine, Shapes[NearestShape].BoneToWorld, Closest, Normal);
			OutPoint = Closest;
			OutBone = Shapes[NearestShape].Bone;
			return true;
		}
	}

	// No physics asset: the bone nearest the line, at the line's closest approach to it.
	const int32 BoneCount = Body.GetNumBones();
	float Best = TNumericLimits<float>::Max();
	for (int32 BoneIndex = 1; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FVector BoneLocation = Body.GetBoneLocation(Body.GetBoneName(BoneIndex));
		const FVector OnLine = FMath::ClosestPointOnSegment(BoneLocation, Start, Start + Dir * Length);
		const float Distance = FVector::Dist(BoneLocation, OnLine);
		if (Distance < Best)
		{
			Best = Distance;
			OutPoint = OnLine;
			OutBone = Body.GetBoneName(BoneIndex);
		}
	}
	return Best < TNumericLimits<float>::Max();
}

const TCHAR* const AArrowProjectile::DefaultNockMaterialPath = TEXT("/Game/Blueprints/Weapons/M_ArrowNock.M_ArrowNock");

AArrowProjectile::AArrowProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	NockMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(DefaultNockMaterialPath));

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	RootComponent = Collision;
	Collision->InitSphereRadius(2.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	// World and bodies stop it. Not the Weapon trace channel: the hidden traversal ledges on every
	// roof edge block only that, and an arrow that stuck in thin air 2 cm off a facade is a bug.
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Destructible, ECR_Block);
	Collision->SetGenerateOverlapEvents(false);
	Collision->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UMaterialInterface* ShapeMaterialObject = ShapeMaterial.Succeeded() ? ShapeMaterial.Object : nullptr;
	auto MakePart = [this, ShapeMaterialObject](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Collision);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		if (Mesh)
		{
			Part->SetStaticMesh(Mesh);
		}
		if (ShapeMaterialObject)
		{
			Part->SetMaterial(0, ShapeMaterialObject);
		}
		return Part;
	};
	UStaticMesh* CylinderMesh = Cylinder.Succeeded() ? Cylinder.Object : nullptr;
	UStaticMesh* CubeMesh = Cube.Succeeded() ? Cube.Object : nullptr;
	Shaft = MakePart(TEXT("Shaft"), CylinderMesh);
	Fletching = MakePart(TEXT("Fletching"), CubeMesh);
	Fletching2 = MakePart(TEXT("Fletching2"), CubeMesh);
	Fletching3 = MakePart(TEXT("Fletching3"), CubeMesh);
	Nock = MakePart(TEXT("Nock"), CylinderMesh);
	LayoutParts();

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f;
	Movement->ProjectileGravityScale = 1.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->bAutoActivate = false;
}

void AArrowProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Again here, so a Blueprint child's ShaftLength or thickness shapes its parts too.
	LayoutParts();
}

void AArrowProjectile::LayoutParts()
{
	// The engine cylinder is 100 cm along Z about its centre; pitched 90 it lies along X, back from
	// the tip at the root.
	const float Radius = ShaftThickness * 0.5f;
	if (Shaft)
	{
		Shaft->SetRelativeLocationAndRotation(FVector(-ShaftLength * 0.5f, 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
		Shaft->SetRelativeScale3D(FVector(ShaftThickness / 100.f, ShaftThickness / 100.f, ShaftLength / 100.f));
	}
	// Vanes 12 cm long and 4 cm tall, 3 mm thin, standing out from the shaft 120 degrees apart.
	const float VaneHeight = 4.f;
	const float VaneX = -ShaftLength + 9.f;
	UStaticMeshComponent* const Vanes[] = { Fletching.Get(), Fletching2.Get(), Fletching3.Get() };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UStaticMeshComponent* Vane = Vanes[Index];
		if (!Vane)
		{
			continue;
		}
		const FRotator Roll(0.f, 0.f, 120.f * Index);
		Vane->SetRelativeLocationAndRotation(FVector(VaneX, 0.f, 0.f) + Roll.RotateVector(FVector(0.f, 0.f, Radius + VaneHeight * 0.5f)), Roll);
		Vane->SetRelativeScale3D(FVector(0.12f, 0.003f, VaneHeight / 100.f));
	}
	if (Nock)
	{
		Nock->SetRelativeLocationAndRotation(FVector(-ShaftLength - 1.f, 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
		Nock->SetRelativeScale3D(FVector((ShaftThickness + 0.8f) / 100.f, (ShaftThickness + 0.8f) / 100.f, 0.03f));
	}
}

void AArrowProjectile::InitArrow(UArrowDefinition* InArrow, UBowDefinition* InBow, float InDamage, AActor* InShooter,
	UBowComponent* InSource)
{
	Arrow = InArrow;
	Bow = InBow;
	Damage = InDamage;
	Shooter = InShooter;
	Source = InSource;
	SetOwner(InShooter);
	SetInstigator(Cast<APawn>(InShooter));

	if (InShooter && Collision)
	{
		// It leaves from her hand, inside her own capsule.
		Collision->IgnoreActorWhenMoving(InShooter, true);
		// And never stops in her partner: Clint's arrow at a thug who dies before it lands flies on past Kate.
		if (UWorld* World = GetWorld(); World && InShooter->IsA<AHawkeyeCharacter>())
		{
			for (TActorIterator<AHawkeyeCharacter> It(World); It; ++It)
			{
				if (IsPartnerOf(InShooter, *It))
				{
					Collision->IgnoreActorWhenMoving(*It, true);
				}
			}
		}
	}

	// Pale shaft so it reads against a dark jacket, coloured fletching, Kate's purple nock (glowing
	// when M_ArrowNock is there). BasicShapeMaterial and M_ArrowNock both take a Color parameter.
	if (Nock && !NockMaterial.IsNull())
	{
		if (UMaterialInterface* Glow = NockMaterial.LoadSynchronous())
		{
			Nock->SetMaterial(0, Glow);
		}
	}
	// An arrow type can carry its own colours: Trickshot's are black with purple vanes.
	const bool bOwnColors = InArrow && InArrow->bOverrideColors;
	const FLinearColor Shafts = bOwnColors ? InArrow->ShaftColor : ShaftColor;
	const FLinearColor Vanes = bOwnColors ? InArrow->FletchingColor : FletchingColor;
	const FLinearColor Nocks = bOwnColors ? InArrow->NockColor : NockColor;
	const TPair<UStaticMeshComponent*, FLinearColor> Tints[] = {
		{ Shaft.Get(), Shafts },
		{ Fletching.Get(), Vanes },
		{ Fletching2.Get(), Vanes },
		{ Fletching3.Get(), Vanes },
		{ Nock.Get(), Nocks },
	};
	for (const TPair<UStaticMeshComponent*, FLinearColor>& Tint : Tints)
	{
		if (UMaterialInstanceDynamic* Material = Tint.Key ? Tint.Key->CreateDynamicMaterialInstance(0) : nullptr)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Tint.Value);
		}
	}
}

void AArrowProjectile::LaunchWithVelocity(const FVector& Velocity)
{
	if (!Movement || !Collision)
	{
		return;
	}
	bInFlight = true;
	bStuck = false;
	SetActorRotation(Velocity.Rotation());
	Movement->SetUpdatedComponent(Collision);
	Movement->Velocity = Velocity;
	Movement->Activate(true);
	Movement->UpdateComponentVelocity();
	if (!Movement->OnProjectileStop.IsAlreadyBound(this, &AArrowProjectile::HandleImpact))
	{
		Movement->OnProjectileStop.AddDynamic(this, &AArrowProjectile::HandleImpact);
	}
	StartFlightEffects();
}

void AArrowProjectile::StartFlightEffects()
{
	const UBowComponent* SourceBow = Source.Get();
	if (SourceBow)
	{
		Whistle = UHawkeyeAudioSubsystem::PlayAttached(SourceBow->WhistleSound, Collision, TEXT("arrow whistle"));
	}
	// A trick arrow's own trail (the bola's whirl), else the bow's faint streak.
	const bool bOwnTrail = Arrow && !Arrow->FlightVfx.IsNull();
	if (!Trail && (bOwnTrail || SourceBow))
	{
		Trail = UHawkeyeVfxSubsystem::SpawnAttached(bOwnTrail ? Arrow->FlightVfx : SourceBow->ArrowTrailVfx, Collision,
			bOwnTrail ? UHawkeyeVfxSubsystem::TrickEffectEvent : UHawkeyeVfxSubsystem::ArrowTrailEvent);
	}
}

void AArrowProjectile::StopFlightEffects()
{
	if (UAudioComponent* Air = Whistle.Get())
	{
		Air->Stop();
	}
	UHawkeyeVfxSubsystem::Release(Trail);
}

void AArrowProjectile::PlayImpactSound(const FHitResult& Hit) const
{
	const UBowComponent* SourceBow = Source.Get();
	if (!SourceBow)
	{
		return;
	}
	const AActor* HitActor = Hit.GetActor();
	const EHawkeyeArrowSurface Surface = HawkeyeAudioMath::ClassifyArrowSurface(
		HitActor && HitActor->FindComponentByClass<UHealthComponent>(), GetNameSafe(HitActor));
	UHawkeyeAudioSubsystem::PlayAt(this, SourceBow->GetImpactSound(Surface), Hit.ImpactPoint, TEXT("arrow impact"));
	// Out of the surface: the systems throw along their X.
	const FVector Normal = FVector(Hit.ImpactNormal).IsNearlyZero() ? -GetActorForwardVector() : FVector(Hit.ImpactNormal);
	UHawkeyeVfxSubsystem::SpawnAt(this, SourceBow->GetImpactVfx(Surface), Hit.ImpactPoint, Normal.Rotation(),
		Surface == EHawkeyeArrowSurface::Flesh ? UHawkeyeVfxSubsystem::HitSparkEvent : UHawkeyeVfxSubsystem::ArrowImpactEvent);
}

void AArrowProjectile::AdvanceFlight(float DeltaSeconds)
{
	if (bInFlight && Movement && Movement->IsActive())
	{
		Movement->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}
	FaceVelocity();
}

void AArrowProjectile::FaceVelocity()
{
	if (bInFlight && Movement && !Movement->Velocity.IsNearlyZero())
	{
		SetActorRotation(Movement->Velocity.Rotation());
	}
}

void AArrowProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bInFlight)
	{
		// The movement component already turns the root; this keeps the rule explicit and holds
		// for a subclass that moves itself.
		FaceVelocity();
		return;
	}

	if (bStuck && Arrow && Arrow->bRecoverable)
	{
		const UWorld* World = GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		TryRecoverBy(PC ? PC->GetPawn() : nullptr);
	}
}

FName AArrowProjectile::ResolveHitBone(const FHitResult& Hit, const FVector& Direction) const
{
	if (!Hit.BoneName.IsNone())
	{
		return Hit.BoneName;
	}

	const AActor* HitActor = Hit.GetActor();
	USkeletalMeshComponent* SkeletalMesh = HitActor ? HitActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset() || !SkeletalMesh->GetPhysicsAsset())
	{
		return Hit.BoneName;
	}

	// The capsule is what the sweep met and it is wider than the head; ask the body's physics
	// shapes (what the Weapon channel meets on a mesh) which bone the flight line really crosses.
	const FVector Start = Hit.ImpactPoint - Direction * HawkeyeArrow::BoneProbeBack;
	const FVector End = Hit.ImpactPoint + Direction * HawkeyeArrow::BoneProbeAhead;
	FHitResult BoneHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeArrowBone), /*bTraceComplex=*/false);
	if (SkeletalMesh->LineTraceComponent(BoneHit, Start, End, Params))
	{
		return BoneHit.BoneName;
	}
	return Hit.BoneName;
}

void AArrowProjectile::DamageVictim(const FHitResult& Hit, const FVector& Direction, FName Bone)
{
	AActor* Victim = Hit.GetActor();
	UHealthComponent* Health = Victim ? Victim->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Health || !Health->IsAlive())
	{
		return;
	}

	const bool bHeadshot = Bow && Bow->IsHeadBone(Bone);
	const float Dealt = Bow ? Bow->ApplyHeadshot(Damage, Bone) : Damage;

	AActor* ShooterActor = Shooter.Get();
	const APawn* ShooterPawn = Cast<APawn>(ShooterActor);
	UGameplayStatics::ApplyPointDamage(Victim, Dealt, Direction, Hit,
		ShooterPawn ? ShooterPawn->GetController() : nullptr, ShooterActor ? ShooterActor : this, nullptr);

	// A surviving victim flinches through the same path a punch uses.
	if (Health->IsAlive())
	{
		Health->Stagger(ShooterActor);
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s: %s hit %s (bone %s) for %.1f%s, health now %.1f."), *GetNameSafe(ShooterActor),
		*GetNameSafe(Arrow), *GetNameSafe(Victim), *Bone.ToString(), Dealt, bHeadshot ? TEXT(" (headshot)") : TEXT(""),
		Health->GetCurrentHealth());

	if (UBowComponent* SourceBow = Source.Get())
	{
		SourceBow->NotifyArrowHit(Victim, Dealt, bHeadshot);
	}
}

void AArrowProjectile::Embed(const FHitResult& Hit, const FVector& Direction, FName Bone, const FVector* EntryPoint,
	FName EntryBone)
{
	if (Movement)
	{
		Movement->StopMovementImmediately();
		Movement->Deactivate();
	}
	if (Collision)
	{
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	const AActor* HitActor = Hit.GetActor();
	USkeletalMeshComponent* Body = FindBodyMesh(HitActor);
	if (EntryPoint && Body && !EntryBone.IsNone() && Body->GetBoneIndex(EntryBone) != INDEX_NONE)
	{
		// Into the body itself, not the capsule round it: the tip a few cm under the shape it entered, along
		// its flight, on that bone, so it moves with her and falls with his ragdoll.
		SetActorLocationAndRotation(*EntryPoint + Direction * BodyEmbedDepth, Direction.Rotation());
		AttachToComponent(Body, FAttachmentTransformRules::KeepWorldTransform, EntryBone);
		StuckIn = Hit.GetActor();
		SetLifeSpan(StuckLifeSeconds);
		UE_LOG(LogHawkeye, Log, TEXT("%s: stuck in %s on %s, %.0f cm in from the capsule's impact."), *GetNameSafe(Arrow),
			*GetNameSafe(HitActor), *EntryBone.ToString(), FVector::Dist(*EntryPoint, FVector(Hit.ImpactPoint)));
		return;
	}

	const FVector Tip = (Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : GetActorLocation()) + Direction * EmbedDepth;
	SetActorLocationAndRotation(Tip, Direction.Rotation());

	// Onto the bone when the flight line found one, so an arrow in a thug goes down with the
	// ragdoll instead of staying on the capsule he leaves standing.
	if (Body && !Bone.IsNone() && Body->GetBoneIndex(Bone) != INDEX_NONE)
	{
		AttachToComponent(Body, FAttachmentTransformRules::KeepWorldTransform, Bone);
	}
	else if (USceneComponent* HitComponent = Hit.GetComponent())
	{
		AttachToComponent(HitComponent, FAttachmentTransformRules::KeepWorldTransform);
	}
	StuckIn = Hit.GetActor();
	SetLifeSpan(StuckLifeSeconds);
}

void AArrowProjectile::HandleImpact(const FHitResult& Hit)
{
	if (bStuck)
	{
		return;
	}

	const FVector Direction = (Hit.TraceEnd - Hit.TraceStart).GetSafeNormal().IsNearlyZero()
		? GetActorForwardVector()
		: (Hit.TraceEnd - Hit.TraceStart).GetSafeNormal();

	bInFlight = false;
	bStuck = true;
	StopFlightEffects();

	// The explosive's damage is the blast's, falling off from the centre; a direct hit is not
	// also a stab for the full 80.
	const EArrowHitEffect Effect = Arrow ? Arrow->OnHitEffect : EArrowHitEffect::None;

	// The heavy's shield takes it from the front: no damage, and it sticks in the shield. A trick arrow's
	// effect still goes off (the bola still trips him, the putty still holds him).
	AThugCharacter* Thug = Cast<AThugCharacter>(Hit.GetActor());
	FVector OnShield;
	if (Thug && Effect != EArrowHitEffect::Explosive && Thug->TryBlock(Shooter.Get(), -Direction, GetNameSafe(Arrow))
		&& Thug->ComputeShieldImpact(Hit.TraceStart, Direction, OnShield))
	{
		FHitResult ShieldHit = Hit;
		// Only the tip goes in: the slab is 5 cm thick.
		ShieldHit.ImpactPoint = OnShield - Direction * FMath::Max(EmbedDepth - 4.f, 0.f);
		ShieldHit.Location = ShieldHit.ImpactPoint;
		ShieldHit.Component = Thug->GetShieldComponent();
		ShieldHit.BoneName = NAME_None;
		Embed(ShieldHit, Direction, NAME_None);
		SpawnHitEffect(Hit);
		return;
	}

	// Her partner's arrow (a hit the flight's ignore list did not catch): it sticks, and does nothing else.
	if (IsPartnerOf(Shooter.Get(), Hit.GetActor()))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s's arrow met %s, his partner: no damage, no effect."), *GetNameSafe(Arrow),
			*GetNameSafe(Shooter.Get()), *GetNameSafe(Hit.GetActor()));
		Embed(Hit, Direction, NAME_None);
		return;
	}

	// A body: walk the flight line into the mesh's own shapes (the capsule is 34 to 42 cm round her).
	FVector EntryPoint = FVector(Hit.ImpactPoint);
	FName EntryBone = NAME_None;
	bool bEntered = false;
	const USkeletalMeshComponent* BodyMesh = Hit.GetActor() && Hit.GetActor()->IsA<APawn>() ? FindBodyMesh(Hit.GetActor()) : nullptr;
	const bool bBody = BodyMesh
		&& FindBodyEntry(*BodyMesh, FVector(Hit.ImpactPoint) - Direction * HawkeyeArrow::BodyWalkBack, Direction,
			HawkeyeArrow::BodyWalkBack + HawkeyeArrow::BoneProbeAhead, EntryPoint, EntryBone, bEntered);
	// Only a real entry decides the bone that is hurt: a line that passed beside the head is no headshot.
	const FName Bone = bEntered ? EntryBone : ResolveHitBone(Hit, Direction);
	PlayImpactSound(Hit);
	if (Effect != EArrowHitEffect::Explosive)
	{
		DamageVictim(Hit, Direction, Bone);
	}
	// A challenge target scores where on its face the arrow went in.
	if (AChallengeTarget* Target = Cast<AChallengeTarget>(Hit.GetActor()))
	{
		Target->HandleArrowHit(Hit.ImpactPoint, Shooter.Get());
	}
	// A switch decides for itself whether a plain arrow trips it (most want a returning arrow's blunt head).
	if (AArrowSwitch* Switch = Cast<AArrowSwitch>(Hit.GetActor()))
	{
		Switch->HandleArrowHit(this, Shooter.Get());
	}
	Embed(Hit, Direction, Bone, bBody ? &EntryPoint : nullptr, EntryBone);
	SpawnHitEffect(Hit);
}

void AArrowProjectile::SpawnHitEffect(const FHitResult& Hit)
{
	if (!Arrow || Arrow->OnHitEffect == EArrowHitEffect::None || Arrow->OnHitEffect == EArrowHitEffect::Grapple)
	{
		return;
	}
	AArrowEffect::SpawnForHit(GetWorld(), Arrow, Shooter.Get(), Hit);
	if (Arrow->OnHitEffect == EArrowHitEffect::Explosive)
	{
		// Nothing left of it to stick out of anything.
		Destroy();
	}
}

bool AArrowProjectile::TryRecoverBy(AActor* Collector)
{
	if (!bStuck || !Arrow || !Arrow->bRecoverable || !IsValid(Collector))
	{
		return false;
	}
	// One stuck in her own body is not something she pulls out on the move.
	if (StuckIn.Get() == Collector || FVector::Dist(Collector->GetActorLocation(), GetActorLocation()) > RecoverRadius)
	{
		return false;
	}
	UInventoryComponent* Inventory = Collector->FindComponentByClass<UInventoryComponent>();
	UArrowDefinition* Into = Arrow->GetRecoveredType();
	if (!Inventory || Inventory->AddArrows(Into, 1) <= 0)
	{
		// A full quiver leaves the arrow where it is.
		return false;
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s recovered %s as %s; %d in slot %d."), *GetNameSafe(Collector), *GetNameSafe(Arrow),
		*GetNameSafe(Into), Inventory->GetArrowCount(Into->Slot), Into->Slot);
	if (const UBowComponent* CollectorBow = Collector->FindComponentByClass<UBowComponent>())
	{
		CollectorBow->PlayPickupSound();
	}
	if (!Arrow->PickupToast.IsEmpty() && Inventory->NoteFirstPickup(Arrow))
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: first pickup of %s, toast \"%s\"."), *GetNameSafe(Collector), *GetNameSafe(Arrow),
			*Arrow->PickupToast.ToString());
		const APawn* Pawn = Cast<APawn>(Collector);
		if (Pawn && Pawn->IsPlayerControlled())
		{
			if (UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(Collector))
			{
				if (UHawkeyeObjectiveWidget* Toasts = Hud->GetObjectiveMarker())
				{
					Toasts->PushToast(NSLOCTEXT("Hawkeye", "ArrowPickedUp", "Picked up"), Arrow->PickupToast);
				}
			}
		}
	}
	bStuck = false;
	Destroy();
	return true;
}
