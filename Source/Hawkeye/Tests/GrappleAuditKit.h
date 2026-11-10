#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Player/HawkeyeCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Where Kate's hip camera sits when a player turns it to put Target in the middle of the screen, with her
 * capsule centre at Centre: the spring arm's own sums (pivot = capsule + boom offset + the look-up pivot lift;
 * the lens an arm's length back along the view and the socket offset out to the right and up; a probe sweep
 * on the boom's channel pulls it in), the pitch clamped, iterated three times because the lens moves with the
 * pitch. Shared by Hawkeye.Grapple.Audit and the laps that plan a grapple from a spot Kate is not standing on.
 */
namespace HawkeyeGrappleView
{
	inline void PredictLens(const AHawkeyeCharacter* Kate, const FVector& Centre, const FVector& Target, FVector& OutLens,
		FVector& OutForward)
	{
		const USpringArmComponent* Boom = Kate ? Kate->GetCameraBoom() : nullptr;
		const UWorld* World = Kate ? Kate->GetWorld() : nullptr;
		const FVector BoomOffset = Boom ? Boom->GetRelativeLocation() : FVector::ZeroVector;
		const FVector Base = Centre + BoomOffset;
		FRotator Rot = (Target - Base).Rotation();
		FVector Lens = Base;
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			Rot = FRotator(Kate ? Kate->ClampCameraPitch(Rot.Pitch) : Rot.Pitch, Rot.Yaw, 0.f);
			const FHawkeyeCameraTargets Targets = Kate ? Kate->ComputeCameraTargets(false, Rot.Pitch) : FHawkeyeCameraTargets();
			const FVector Origin = Base + FVector(0.f, 0.f, Targets.PivotLift);
			const FVector Desired = Origin - Rot.Vector() * Targets.ArmLength + FRotationMatrix(Rot).TransformVector(Targets.SocketOffset);
			Lens = Desired;
			if (World && Boom && Boom->bDoCollisionTest)
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleViewProbe), false, Kate);
				FHitResult Hit;
				if (World->SweepSingleByChannel(Hit, Origin, Desired, FQuat::Identity, Boom->ProbeChannel,
						FCollisionShape::MakeSphere(Boom->ProbeSize), Params))
				{
					Lens = Hit.Location;
				}
			}
			Rot = (Target - Lens).Rotation();
		}
		Rot = FRotator(Kate ? Kate->ClampCameraPitch(Rot.Pitch) : Rot.Pitch, Rot.Yaw, 0.f);
		OutLens = Lens;
		OutForward = Rot.Vector();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
