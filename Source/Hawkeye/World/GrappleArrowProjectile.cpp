#include "World/GrappleArrowProjectile.h"

#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Player/GrappleComponent.h"
#include "World/GrappleAnchor.h"

AGrappleArrowProjectile::AGrappleArrowProjectile()
{
	// It flies its own straight line; the projectile movement and the tip's collision stay off.
	if (Collision)
	{
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (Movement)
	{
		Movement->bAutoActivate = false;
		Movement->ProjectileGravityScale = 0.f;
	}
	FletchingColor = FLinearColor(0.1f, 0.8f, 0.2f);
}

void AGrappleArrowProjectile::LaunchWithVelocity(const FVector& /*Velocity*/)
{
}

void AGrappleArrowProjectile::Launch(AGrappleAnchor* Anchor, UGrappleComponent* InGrapple)
{
	TargetAnchor = Anchor;
	Grapple = InGrapple;
	bArrived = false;
	bInFlight = true;
	if (Anchor)
	{
		SetActorRotation((Anchor->GetMarkerLocation() - GetActorLocation()).Rotation());
	}
}

void AGrappleArrowProjectile::Launch(AGrappleAnchor* Anchor, UGrappleComponent* InGrapple, float InSpeed)
{
	Launch(Anchor, InGrapple);
	if (InSpeed > 0.f)
	{
		Speed = InSpeed;
		return;
	}
	if (!IsValid(Anchor))
	{
		Destroy();
		return;
	}
	SetActorLocation(Anchor->GetMarkerLocation());
	Arrive();
}

void AGrappleArrowProjectile::Tick(float DeltaSeconds)
{
	// Not Super: the anchor, not a walk-over, takes this arrow back.
	AActor::Tick(DeltaSeconds);
	Advance(DeltaSeconds);
}

void AGrappleArrowProjectile::Advance(float DeltaSeconds)
{
	if (bArrived)
	{
		return;
	}

	const AGrappleAnchor* Anchor = TargetAnchor.Get();
	if (!IsValid(Anchor))
	{
		// The anchor went away mid-flight; the arrow is lost.
		Destroy();
		return;
	}

	const FVector Target = Anchor->GetMarkerLocation();
	const FVector ToTarget = Target - GetActorLocation();
	const float Step = Speed * DeltaSeconds;
	if (ToTarget.Size() <= Step)
	{
		SetActorLocation(Target);
		Arrive();
		return;
	}
	SetActorLocationAndRotation(GetActorLocation() + ToTarget.GetSafeNormal() * Step, ToTarget.Rotation());
}

void AGrappleArrowProjectile::Arrive()
{
	bArrived = true;
	bInFlight = false;
	bStuck = true;
	SetActorTickEnabled(false);

	AGrappleAnchor* Anchor = TargetAnchor.Get();
	if (Anchor)
	{
		const UGrappleComponent* ShooterGrapple = Grapple.Get();
		Anchor->AddStuckArrow(this, ShooterGrapple ? ShooterGrapple->GetOwner() : GetOwner());
	}
	if (UGrappleComponent* Component = Grapple.Get())
	{
		Component->HandleArrowArrived(this, Anchor);
	}
}
