#include "Combat/WeaponComponent.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/ThugCharacter.h"

UWeaponComponent::UWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	HeadBoneNames.Add(FName(TEXT("head")));
	HeadBoneNames.Add(FName(TEXT("neck_01")));
}

void UWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentAmmo = FMath::Clamp(CurrentAmmo, 0, MagazineSize);

	// Per-owner seed: two thugs firing on the same frame should miss in different directions.
	SpreadStream.Initialize(*GetNameSafe(GetOwner()));

	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}

void UWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

double UWeaponComponent::GetNowSeconds() const
{
	if (bUseTestTime)
	{
		return TestTimeOverride;
	}

	const UWorld* World = GetWorld();
	return World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
}

void UWeaponComponent::SetTestTimeSeconds(double InSeconds)
{
	bUseTestTime = true;
	TestTimeOverride = InSeconds;
}

void UWeaponComponent::SetTestRandomStream(const FRandomStream& InStream)
{
	SpreadStream = InStream;
}

void UWeaponComponent::SetAiming(bool bInAiming)
{
	// Aiming an empty hand is meaningless, and it would leave the flag set when a pickup arms us.
	bIsAiming = bInAiming && bHasWeapon;
}

FVector UWeaponComponent::ApplyConeSpread(const FVector& Direction, float SpreadDegrees, const FRandomStream& Stream)
{
	const FVector Forward = Direction.GetSafeNormal();
	if (SpreadDegrees <= 0.f || Forward.IsNearlyZero())
	{
		return Forward;
	}

	// Uniform over the spherical cap, not over the angle, so the middle of the cone isn't favoured.
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SpreadDegrees));
	const float CosTheta = FMath::Lerp(CosHalfAngle, 1.f, Stream.FRand());
	const float SinTheta = FMath::Sqrt(FMath::Max(0.f, 1.f - CosTheta * CosTheta));
	const float Phi = Stream.FRand() * 2.f * PI;

	const FVector Seed = FMath::Abs(Forward.Z) < 0.99f ? FVector::UpVector : FVector::ForwardVector;
	const FVector Right = FVector::CrossProduct(Seed, Forward).GetSafeNormal();
	const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

	const FVector Offset = (Right * FMath::Cos(Phi) + Up * FMath::Sin(Phi)) * SinTheta;
	return (Forward * CosTheta + Offset).GetSafeNormal();
}

void UWeaponComponent::GiveWeapon(int32 Magazine, int32 Reserve)
{
	// The definition-free path: "you are now holding a gun with these rounds". Tests use it,
	// and it has to win over a melee definition left in the slot.
	if (IsMelee())
	{
		ActiveDefinition = nullptr;
	}

	bHasWeapon = true;
	CurrentAmmo = FMath::Clamp(Magazine, 0, MagazineSize);
	ReserveAmmo = FMath::Max(Reserve, 0);
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}

bool UWeaponComponent::IsMelee() const
{
	return ActiveDefinition != nullptr && ActiveDefinition->bIsMelee;
}

void UWeaponComponent::ApplyStatsFromDefinition()
{
	if (!ActiveDefinition)
	{
		return;
	}

	MeleeRange = ActiveDefinition->MeleeRange;
	MeleeCooldown = ActiveDefinition->MeleeCooldown;
	bStaggerOnHit = ActiveDefinition->bStaggerOnHit;

	if (ActiveDefinition->bIsMelee)
	{
		// Fists carry no ammo, spread or reload, so holding them must not overwrite the gun
		// stats the component would go back to the moment a weapon is selected again.
		MeleeDamage = ActiveDefinition->Damage;
		return;
	}

	// The definition is the source of truth; these properties stay as the runtime copy so the
	// trace, reload and spread code does not have to null-check a data asset on every shot.
	Damage = ActiveDefinition->Damage;
	MagazineSize = FMath::Max(ActiveDefinition->MagazineSize, 1);
	FireRate = FMath::Max(ActiveDefinition->FireRate, 1.f);
	ReloadSeconds = ActiveDefinition->ReloadSeconds;
	HipSpreadDegrees = ActiveDefinition->HipSpreadDegrees;
	AimSpreadDegrees = ActiveDefinition->AimSpreadDegrees;
	HeadshotMultiplier = ActiveDefinition->HeadshotMultiplier;
	HeadBoneNames = ActiveDefinition->HeadBoneNames;
}

void UWeaponComponent::SetActiveWeapon(UWeaponDefinition* Definition, int32 Magazine, int32 Reserve)
{
	CancelReload();

	ActiveDefinition = Definition;
	ApplyStatsFromDefinition();

	// bHasWeapon is now "the active slot is a ranged weapon": fists are a weapon, but they are
	// not one that has ammo, a crosshair full of bars or a reload.
	bHasWeapon = Definition != nullptr && !Definition->bIsMelee;

	CurrentAmmo = FMath::Clamp(Magazine, 0, FMath::Max(MagazineSize, 0));
	ReserveAmmo = FMath::Max(Reserve, 0);

	if (!bHasWeapon)
	{
		bIsAiming = false;
	}

	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}

void UWeaponComponent::RemoveWeapon()
{
	CancelReload();
	bHasWeapon = false;
	bIsAiming = false;
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}

bool UWeaponComponent::CanFire() const
{
	if (IsMelee())
	{
		return GetNowSeconds() - LastFireTimeSeconds >= static_cast<double>(MeleeCooldown);
	}

	if (!bHasWeapon || bIsReloading || CurrentAmmo <= 0)
	{
		return false;
	}

	return GetNowSeconds() - LastFireTimeSeconds >= GetSecondsBetweenShots();
}

float UWeaponComponent::ComputeDamageForHit(FName BoneName) const
{
	return HeadBoneNames.Contains(BoneName) ? Damage * HeadshotMultiplier : Damage;
}

void UWeaponComponent::GetMeleeViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	// From the body, not the camera: in third person the camera is metres behind the pawn, and
	// a punch swept from there would end before it reached him. APawn answers with its eye
	// height and its controller's rotation, so the swing still goes where the player looks.
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
	}
}

void UWeaponComponent::GetFireViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (AController* OwnerController = Pawn->GetController())
		{
			// A controller's view point: a thug's eyes through its AI controller. The player never
			// fires this hitscan any more; the bow shoots projectiles.
			OwnerController->GetPlayerViewPoint(OutLocation, OutRotation);
			return;
		}
	}

	Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
}

bool UWeaponComponent::Fire()
{
	if (IsMelee())
	{
		return FireMelee();
	}

	if (!bHasWeapon)
	{
		// Empty-handed: not even a dry-fire click.
		return false;
	}

	if (bIsReloading)
	{
		return false;
	}

	if (CurrentAmmo <= 0)
	{
		OnEmptyClick.Broadcast();
		return false;
	}

	if (GetNowSeconds() - LastFireTimeSeconds < GetSecondsBetweenShots())
	{
		return false;
	}

	LastFireTimeSeconds = GetNowSeconds();
	--CurrentAmmo;
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	if (const AActor* Owner = GetOwner())
	{
		UHawkeyeAudioSubsystem::PlayAt(this, FireSound, Owner->GetActorLocation(), TEXT("gunshot"));
	}

	TraceAndApplyDamage();
	return true;
}

bool UWeaponComponent::FireMelee()
{
	if (GetNowSeconds() - LastFireTimeSeconds < static_cast<double>(MeleeCooldown))
	{
		return false;
	}

	LastFireTimeSeconds = GetNowSeconds();

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		// No world to punch in. The cooldown still ran, which is what the test asserts on.
		return true;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetMeleeViewPoint(ViewLocation, ViewRotation);

	const FVector Direction = ViewRotation.Vector();
	const FVector SweepEnd = ViewLocation + Direction * MeleeRange;

	// A sphere, not a line: a fist is a wide thing and missing a thug by two centimetres is
	// not the feedback anyone wants from a punch.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HawkeyeWeaponMelee), /*bTraceComplex=*/false, Owner);
	QueryParams.AddIgnoredActor(Owner);

	FHitResult Hit;
	const bool bHitSomething = World->SweepSingleByChannel(
		Hit, ViewLocation, SweepEnd, FQuat::Identity, TraceChannel,
		FCollisionShape::MakeSphere(MeleeSweepRadius), QueryParams);

	OnWeaponFired(Hit, bHitSomething);

	AActor* HitActor = bHitSomething ? Hit.GetActor() : nullptr;
	UHealthComponent* Health = HitActor ? HitActor->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Health)
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: punch hit nothing within %.0f units."),
			*GetNameSafe(Owner), MeleeRange);
		return true;
	}

	// The heavy's shield takes a punch from the front.
	if (AThugCharacter* Thug = Cast<AThugCharacter>(HitActor);
		Thug && Thug->TryBlock(Owner, Owner->GetActorLocation() - Thug->GetActorLocation(), TEXT("punch")))
	{
		return true;
	}
	const float DamageDealt = Health->ApplyMeleeDamage(MeleeDamage, Owner, bStaggerOnHit);

	UE_LOG(LogHawkeye, Verbose, TEXT("%s: punched %s for %.1f damage."),
		*GetNameSafe(Owner), *GetNameSafe(HitActor), DamageDealt);

	OnHit.Broadcast(HitActor, DamageDealt);
	return true;
}

FVector UWeaponComponent::GetMuzzleLocation(const FVector& ViewLocation, const FVector& Direction) const
{
	if (const AThugCharacter* Thug = Cast<AThugCharacter>(GetOwner()))
	{
		return Thug->GetGlintLocation();
	}
	return ViewLocation + Direction * 50.f;
}

void UWeaponComponent::PlayShotEffects(const FVector& ViewLocation, const FVector& Direction, const FVector& End) const
{
	const FVector Muzzle = GetMuzzleLocation(ViewLocation, Direction);
	const FRotator Along = (End - Muzzle).IsNearlyZero() ? Direction.Rotation() : (End - Muzzle).Rotation();
	UHawkeyeVfxSubsystem::SpawnAt(this, MuzzleFlashVfx, Muzzle, Along, UHawkeyeVfxSubsystem::MuzzleFlashEvent);
	if (UNiagaraComponent* Tracer = UHawkeyeVfxSubsystem::SpawnAt(this, TracerVfx, Muzzle, Along,
			UHawkeyeVfxSubsystem::TracerEvent))
	{
		Tracer->SetVariableVec3(TEXT("User.BeamStart"), Muzzle);
		Tracer->SetVariableVec3(TEXT("User.BeamEnd"), End);
	}
}

void UWeaponComponent::TraceAndApplyDamage()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		// Ammo accounting still happened; there is simply nothing to trace against.
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetFireViewPoint(ViewLocation, ViewRotation);

	const FVector ShotDirection = ApplyConeSpread(ViewRotation.Vector(), GetCurrentSpreadDegrees(), SpreadStream);
	const FVector TraceEnd = ViewLocation + ShotDirection * Range;

	// Simple collision, not complex: a skeletal mesh's physics-asset bodies are simple shapes,
	// and a complex-only query would miss every character the bullet is aimed at.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HawkeyeWeaponFire), /*bTraceComplex=*/false, Owner);
	QueryParams.AddIgnoredActor(Owner);
	QueryParams.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	const bool bHitSomething = World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, TraceChannel, QueryParams);
	PlayShotEffects(ViewLocation, ShotDirection, bHitSomething ? FVector(Hit.ImpactPoint) : TraceEnd);

	if (bHitSomething && Hit.GetActor())
	{
		AController* InstigatorController = nullptr;
		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			InstigatorController = Pawn->GetController();
		}

		const FName HitBone = ResolveHitBone(Hit, ViewLocation, TraceEnd);
		const float DamageDealt = ComputeDamageForHit(HitBone);

		// Every playtest question about "why did that shot not count" is answerable from this line.
		UE_LOG(LogHawkeye, Verbose,
			TEXT("%s: shot hit %s (component %s, bone %s) for %.1f damage."),
			*GetNameSafe(Owner), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
			*HitBone.ToString(), DamageDealt);

		UGameplayStatics::ApplyPointDamage(
			Hit.GetActor(), DamageDealt, ShotDirection, Hit,
			InstigatorController, Owner, DamageTypeClass);

		if (Hit.GetActor()->FindComponentByClass<UHealthComponent>())
		{
			OnHit.Broadcast(Hit.GetActor(), DamageDealt);
		}
	}
	else
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: shot hit nothing within %.0f units."),
			*GetNameSafe(Owner), Range);
	}

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		DrawDebugLine(World, ViewLocation, bHitSomething ? Hit.ImpactPoint : TraceEnd,
			FColor::Red, false, TraceChannelDebugDuration, 0, 1.f);
	}
#endif

	OnWeaponFired(Hit, bHitSomething);
}

FName UWeaponComponent::ResolveHitBone(const FHitResult& Hit, const FVector& TraceStart, const FVector& TraceEnd) const
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

	// The capsule is wider than the head, so the capsule hit alone can never be a headshot.
	// Ask the body itself whether the ray passed through one of its bones.
	FHitResult BoneHit;
	FCollisionQueryParams BoneParams(SCENE_QUERY_STAT(HawkeyeWeaponBone), /*bTraceComplex=*/false);
	if (SkeletalMesh->LineTraceComponent(BoneHit, TraceStart, TraceEnd, BoneParams))
	{
		return BoneHit.BoneName;
	}

	return Hit.BoneName;
}

bool UWeaponComponent::Reload()
{
	if (!bHasWeapon || bIsReloading || ReserveAmmo <= 0 || CurrentAmmo >= MagazineSize)
	{
		return false;
	}

	bIsReloading = true;
	OnReloadStarted(ReloadSeconds);

	if (ReloadSeconds <= 0.f)
	{
		CompleteReloadNow();
		return true;
	}

	// Without a world there is no timer manager; the caller (an animation notify, or a test)
	// finishes the reload with CompleteReloadNow().
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ReloadTimerHandle, this, &UWeaponComponent::CompleteReloadNow, ReloadSeconds, false);
	}

	return true;
}

void UWeaponComponent::CompleteReloadNow()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}

	bIsReloading = false;

	const int32 Needed = MagazineSize - CurrentAmmo;
	const int32 Loaded = FMath::Min(Needed, ReserveAmmo);
	if (Loaded <= 0)
	{
		OnReloadFinished();
		return;
	}

	CurrentAmmo += Loaded;
	ReserveAmmo -= Loaded;

	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
	OnReloadFinished();
}

void UWeaponComponent::CancelReload()
{
	if (!bIsReloading)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}

	bIsReloading = false;
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: reload cancelled."), *GetNameSafe(GetOwner()));
}

void UWeaponComponent::AddAmmo(int32 Rounds)
{
	if (Rounds <= 0)
	{
		return;
	}

	ReserveAmmo += Rounds;
	OnAmmoChanged.Broadcast(CurrentAmmo, ReserveAmmo);
}
