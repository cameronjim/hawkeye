#include "Combat/ReturningArrowProjectile.h"

#include "Hawkeye.h"
#include "CollisionQueryParams.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "World/ArrowSwitch.h"
#include "World/ThugCharacter.h"

int32 AReturningArrowProjectile::CaughtCount = 0;
int32 AReturningArrowProjectile::SelfHitCount = 0;

namespace HawkeyeReturnArrow
{
	/** The sweep that finds bodies on the way back, cm. */
	static constexpr float BodyProbeRadius = 22.f;
	/** Sweeps one step may take going out (each body passed through starts a new one). */
	static constexpr int32 MaxSweepsPerStep = 4;

	static const FHawkeyeReturnFlight DefaultTuning;
}

AReturningArrowProjectile::AReturningArrowProjectile()
{
	if (Movement)
	{
		Movement->ProjectileGravityScale = 0.f;
		Movement->bAutoActivate = false;
	}
}

FName AReturningArrowProjectile::CaughtEvent(const UObject* InArrow)
{
	return InArrow ? FName(*FString::Printf(TEXT("arrow_caught:%s"), *InArrow->GetName())) : NAME_None;
}

FName AReturningArrowProjectile::SelfHitEvent(const UObject* InArrow)
{
	return InArrow ? FName(*FString::Printf(TEXT("arrow_self_hit:%s"), *InArrow->GetName())) : NAME_None;
}

const FHawkeyeReturnFlight& AReturningArrowProjectile::Tuning() const
{
	return Arrow ? Arrow->ReturnFlight : HawkeyeReturnArrow::DefaultTuning;
}

FVector AReturningArrowProjectile::CatcherPoint() const
{
	const AActor* Catcher = Shooter.Get();
	return Catcher ? Catcher->GetActorLocation() + FVector(0.f, 0.f, 30.f) : GetActorLocation();
}

void AReturningArrowProjectile::LaunchWithVelocity(const FVector& Velocity)
{
	Direction = Velocity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
	}
	Phase = EHawkeyeReturnPhase::Out;
	TurnReason = EHawkeyeReturnTurn::None;
	bInFlight = true;
	bStuck = false;
	OutPath.Reset();
	OutPath.Add(GetActorLocation());
	DistanceOut = SinceLastPoint = Travelled = FlightSeconds = 0.f;
	HitActors.Reset();
	LastStep = Direction;
	SetActorRotation(Direction.Rotation());
	if (Movement)
	{
		Movement->Deactivate();
	}
	StartFlightEffects();
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s loosed at %s, out along %s (up to %d bodies, %.0f cm)."), *GetName(), *GetNameSafe(Arrow),
		*GetActorLocation().ToCompactString(), *Direction.ToCompactString(), Tuning().MaxTargets, Tuning().MaxRange);
}

void AReturningArrowProjectile::Tick(float DeltaSeconds)
{
	// Not the arrow's own tick: nothing here sticks or is recovered.
	AActor::Tick(DeltaSeconds);
	AdvanceReturn(DeltaSeconds);
}

bool AReturningArrowProjectile::TryHurt(const FHitResult& Hit, const FVector& Along)
{
	AActor* Victim = Hit.GetActor();
	if (!Victim || Victim == Shooter.Get() || IsPartnerOf(Shooter.Get(), Victim)
		|| HitActors.ContainsByPredicate([Victim](const TWeakObjectPtr<AActor>& Weak) { return Weak.Get() == Victim; }))
	{
		return false;
	}
	const UHealthComponent* Health = Victim->FindComponentByClass<UHealthComponent>();
	if (!Health || !Health->IsAlive() || !UHawkeyeReturnFlightRules::CanHitMore(HitActors.Num(), Tuning().MaxTargets))
	{
		return false;
	}
	HitActors.Add(Victim);
	PlayImpactSound(Hit);
	DamageVictim(Hit, Along, ResolveHitBone(Hit, Along));
	return true;
}

void AReturningArrowProjectile::Turn(EHawkeyeReturnTurn Why)
{
	if (Phase != EHawkeyeReturnPhase::Out)
	{
		return;
	}
	TurnReason = Why;
	Phase = EHawkeyeReturnPhase::Back;
	if (OutPath.Num() == 0 || FVector::Dist(OutPath.Last(), GetActorLocation()) > 1.f)
	{
		OutPath.Add(GetActorLocation());
	}
	Travelled = 0.f;
	// The way back is the way it came: nothing solid in it, so it is not swept against the world.
	if (Collision)
	{
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: turns back after %.0f cm (%s), %d hit."), *GetName(), DistanceOut, *UEnum::GetValueAsString(Why),
		HitActors.Num());
}

void AReturningArrowProjectile::AdvanceReturn(float DeltaSeconds)
{
	if (Phase == EHawkeyeReturnPhase::Idle || Phase == EHawkeyeReturnPhase::Home || DeltaSeconds <= 0.f)
	{
		return;
	}
	const FHawkeyeReturnFlight& Flight = Tuning();
	FlightSeconds += DeltaSeconds;
	if (FlightSeconds > Flight.MaxFlightSeconds)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: flight over %.1f s; home where it is."), *GetName(), FlightSeconds);
		ArriveHome();
		return;
	}
	UWorld* World = GetWorld();
	if (Phase == EHawkeyeReturnPhase::Out)
	{
		float Left = Flight.Speed * DeltaSeconds;
		for (int32 Sweep = 0; Sweep < HawkeyeReturnArrow::MaxSweepsPerStep && Left > 0.1f && Phase == EHawkeyeReturnPhase::Out; ++Sweep)
		{
			const FVector From = GetActorLocation();
			FHitResult Hit;
			SetActorLocation(From + Direction * Left, /*bSweep=*/true, &Hit);
			const float Moved = FVector::Dist(From, GetActorLocation());
			Left -= Moved;
			DistanceOut += Moved;
			SinceLastPoint += Moved;
			if (SinceLastPoint >= Flight.PathStep)
			{
				OutPath.Add(GetActorLocation());
				SinceLastPoint = 0.f;
			}
			if (!Hit.bBlockingHit)
			{
				break;
			}
			AActor* Other = Hit.GetActor();
			AThugCharacter* Thug = Cast<AThugCharacter>(Other);
			if (Thug && Thug->TryBlock(Shooter.Get(), -Direction, GetNameSafe(Arrow)))
			{
				// A raised shield is a wall.
				PlayImpactSound(Hit);
				Turn(EHawkeyeReturnTurn::Wall);
				break;
			}
			const UHealthComponent* Health = Other ? Other->FindComponentByClass<UHealthComponent>() : nullptr;
			if (Other && (Health || Other->IsA<APawn>()))
			{
				// A body: hurt it if it can, and fly on through it.
				TryHurt(Hit, Direction);
				if (Collision)
				{
					Collision->IgnoreActorWhenMoving(Other, true);
				}
				if (!UHawkeyeReturnFlightRules::CanHitMore(HitActors.Num(), Flight.MaxTargets))
				{
					Turn(EHawkeyeReturnTurn::Targets);
				}
				continue;
			}
			if (AArrowSwitch* Switch = Cast<AArrowSwitch>(Other))
			{
				PlayImpactSound(Hit);
				Switch->HandleArrowHit(this, Shooter.Get());
				Turn(EHawkeyeReturnTurn::Switch);
				break;
			}
			PlayImpactSound(Hit);
			Turn(EHawkeyeReturnTurn::Wall);
		}
		if (Phase == EHawkeyeReturnPhase::Out)
		{
			const EHawkeyeReturnTurn Why = UHawkeyeReturnFlightRules::TurnBack(DistanceOut, Flight.MaxRange, HitActors.Num(), Flight.MaxTargets,
				false, false);
			if (Why != EHawkeyeReturnTurn::None)
			{
				Turn(Why);
			}
		}
		return;
	}

	// Back along the path to her.
	const FVector Catcher = CatcherPoint();
	const FVector From = GetActorLocation();
	Travelled += Flight.Speed * DeltaSeconds;
	const FVector To = UHawkeyeReturnFlightRules::PointAlongReturn(OutPath, Catcher, Travelled);
	if (World && UHawkeyeReturnFlightRules::CanHitMore(HitActors.Num(), Flight.MaxTargets) && !From.Equals(To, 0.1f))
	{
		TArray<FHitResult> Hits;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeReturnArrowBack), false, this);
		if (AActor* Kate = Shooter.Get())
		{
			Params.AddIgnoredActor(Kate);
		}
		World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(HawkeyeReturnArrow::BodyProbeRadius), Params);
		for (const FHitResult& Hit : Hits)
		{
			TryHurt(Hit, (To - From).GetSafeNormal());
		}
	}
	if (!From.Equals(To, 0.1f))
	{
		LastStep = To - From;
		SetActorLocationAndRotation(To, LastStep.Rotation());
	}
	if (FVector::Dist(To, Catcher) <= Flight.ArriveRadius || Travelled >= UHawkeyeReturnFlightRules::ReturnLength(OutPath, Catcher))
	{
		ArriveHome();
	}
}

void AReturningArrowProjectile::ArriveHome()
{
	if (Phase == EHawkeyeReturnPhase::Home)
	{
		return;
	}
	Phase = EHawkeyeReturnPhase::Home;
	bInFlight = false;
	StopFlightEffects();
	AActor* Catcher = Shooter.Get();
	const APawn* Pawn = Cast<APawn>(Catcher);
	const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(Catcher);
	UHealthComponent* Health = Catcher ? Catcher->FindComponentByClass<UHealthComponent>() : nullptr;
	const bool bDown = !Health || !Health->IsAlive() || (Kate && Kate->IsDowned());
	const bool bBusy = bDown || (Kate && (Kate->IsLockedOutByTakedown() || Kate->IsTraversing() || Kate->IsZipping() || Kate->IsStaggered()));
	const FVector Facing = Pawn && Pawn->GetController() ? Pawn->GetControlRotation().Vector() : (Catcher ? Catcher->GetActorForwardVector() : FVector::ForwardVector);
	const FHawkeyeReturnFlight& Flight = Tuning();
	bCaught = FlightSeconds > Flight.MaxFlightSeconds || UHawkeyeReturnFlightRules::IsCaught(LastStep, Facing, bBusy, Flight.CatchConeDegrees);
	if (bCaught)
	{
		++CaughtCount;
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s caught %s (%d hit, turned: %s)."), *GetName(), *GetNameSafe(Catcher), *GetNameSafe(Arrow),
			HitActors.Num(), *UEnum::GetValueAsString(TurnReason));
		UChapterEventSubsystem::NotifyEvent(this, CaughtEvent(Arrow));
	}
	else
	{
		++SelfHitCount;
		const float Hurt = Health ? UHawkeyeReturnFlightRules::SelfHitDamage(Flight.SelfHitDamage, Health->GetCurrentHealth()) : 0.f;
		if (Health && !bDown)
		{
			if (Hurt > 0.f)
			{
				Health->ApplyDamage(Hurt, this);
			}
			Health->Stagger(this);
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s came back into %s: %.1f damage, a stagger (%d hit, turned: %s)."), *GetName(), *GetNameSafe(Arrow),
			*GetNameSafe(Catcher), Hurt, HitActors.Num(), *UEnum::GetValueAsString(TurnReason));
		UChapterEventSubsystem::NotifyEvent(this, SelfHitEvent(Arrow));
	}
	if (UInventoryComponent* Inventory = Catcher ? Catcher->FindComponentByClass<UInventoryComponent>() : nullptr; Inventory && Arrow)
	{
		Inventory->AddArrows(Arrow, 1);
	}
	SetLifeSpan(0.01f);
	SetActorHiddenInGame(true);
}
