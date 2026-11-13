#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Player/HawkeyeCharacter.h"
#include "Traversal/GrappleReachQuery.h"
#include "Traversal/GrappleReachWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Where Kate's hip camera sits when a player turns it to put Target in the middle of the screen, with her capsule
 * centre at Centre. The math is FGrappleReachQuery::PredictLens on her profile (FGrappleReachWorld::MakeProfile);
 * this adds the spring arm's probe sweep, which in a test ignores Kate herself. Shared by the laps that plan a
 * grapple from a spot Kate is not standing on.
 */
namespace HawkeyeGrappleView
{
	/** The probe for FGrappleReachQuery::PredictLens: the boom's sweep on its channel, ignoring Kate; none when off. */
	inline TFunction<FVector(const FVector&, const FVector&)> MakeLensProbe(const AHawkeyeCharacter* Kate,
		const FGrappleReachProfile& Profile)
	{
		if (!Kate || !Profile.bProbeLens || !Kate->GetCameraBoom())
		{
			return nullptr;
		}
		TWeakObjectPtr<const AHawkeyeCharacter> WeakKate(Kate);
		const TEnumAsByte<ECollisionChannel> Channel = Profile.ProbeChannel;
		const float Size = Profile.ProbeSize;
		return [WeakKate, Channel, Size](const FVector& Origin, const FVector& Desired) -> FVector
		{
			const AHawkeyeCharacter* Live = WeakKate.Get();
			const UWorld* World = Live ? Live->GetWorld() : nullptr;
			if (!World)
			{
				return Desired;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleViewProbe), false, Live);
			FHitResult Hit;
			return World->SweepSingleByChannel(Hit, Origin, Desired, FQuat::Identity, Channel,
					   FCollisionShape::MakeSphere(Size), Params)
				? FVector(Hit.Location)
				: Desired;
		};
	}

	/** Kate's predicted lens and view direction for Target from Centre, for one-off calls (builds her profile). */
	inline void PredictLens(const AHawkeyeCharacter* Kate, const FVector& Centre, const FVector& Target, FVector& OutLens,
		FVector& OutForward)
	{
		const FGrappleReachProfile Profile = FGrappleReachWorld::MakeProfile(Kate);
		FGrappleReachQuery::PredictLens(Profile.Lens, Centre, Target, MakeLensProbe(Kate, Profile), OutLens, OutForward);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
