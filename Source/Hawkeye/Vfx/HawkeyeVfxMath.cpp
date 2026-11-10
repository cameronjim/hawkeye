#include "Vfx/HawkeyeVfxMath.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

namespace HawkeyeVfxMath
{
	FVector ComputeSnowfallOrigin(const FVector& CameraLocation, const FVector& CameraForward, const FVector& Velocity,
		float AheadCm, float HeightCm, float LeadSeconds)
	{
		const FVector Flat = FVector(CameraForward.X, CameraForward.Y, 0.f).GetSafeNormal();
		const FVector Motion = FVector(Velocity.X, Velocity.Y, 0.f) * LeadSeconds;
		return CameraLocation + Flat * AheadCm + Motion + FVector(0.f, 0.f, HeightCm);
	}

	float ComputeLandingPuffScale(float FallHeight, float MinHeight, float FullHeight)
	{
		if (FallHeight < MinHeight)
		{
			return 0.f;
		}
		const float Span = FMath::Max(FullHeight - MinHeight, 1.f);
		return FMath::Lerp(0.6f, 1.6f, FMath::Clamp((FallHeight - MinHeight) / Span, 0.f, 1.f));
	}

	FVector ComputeFootstepKickDirection(const FVector& Forward, const FVector& Velocity)
	{
		FVector Along = FVector(Velocity.X, Velocity.Y, 0.f).GetSafeNormal();
		if (Along.IsNearlyZero())
		{
			Along = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
		}
		if (Along.IsNearlyZero())
		{
			return FVector::UpVector;
		}
		// Back and up at about 40 degrees: a heel flicking snow.
		return (-Along + FVector(0.f, 0.f, 0.85f)).GetSafeNormal();
	}

	bool ComputeScorchPlacement(const FHitResult& GroundHit, float RollDegrees, FVector& OutLocation,
		FRotator& OutRotation, float MaxSlopeDegrees)
	{
		if (!GroundHit.bBlockingHit)
		{
			return false;
		}
		const FVector Normal = FVector(GroundHit.ImpactNormal).GetSafeNormal();
		if (Normal.IsNearlyZero() || Normal.Z < FMath::Cos(FMath::DegreesToRadians(MaxSlopeDegrees)))
		{
			return false;
		}
		OutLocation = GroundHit.ImpactPoint;
		// A decal projects along its X axis: point it into the ground, then spin it about that axis.
		OutRotation = (-Normal).Rotation();
		OutRotation.Roll = RollDegrees;
		return true;
	}

	bool TraceGround(const UWorld* World, const FVector& Point, float MaxDropCm, FHitResult& OutHit, const AActor* Ignore)
	{
		if (!World)
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeVfxGround), /*bTraceComplex=*/false, Ignore);
		return World->LineTraceSingleByChannel(OutHit, Point + FVector(0.f, 0.f, 50.f),
			Point - FVector(0.f, 0.f, FMath::Max(MaxDropCm, 0.f)), ECC_Visibility, Params);
	}
}
