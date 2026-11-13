#include "Traversal/GrappleTraces.h"

#include "Engine/OverlapResult.h"
#include "Engine/World.h"

namespace HawkeyeGrappleTraces
{
	bool HasLineOfSight(const UWorld& World, const FVector& Lens, const FVector& Marker, float SightTolerance,
		const FCollisionQueryParams& Params, FHitResult* OutHit)
	{
		FHitResult Hit;
		if (!World.LineTraceSingleByChannel(Hit, Lens, Marker, ECC_Visibility, Params))
		{
			return true;
		}
		if (Hit.Distance >= FVector::Dist(Lens, Marker) - SightTolerance)
		{
			return true;
		}
		if (OutHit)
		{
			*OutHit = Hit;
		}
		return false;
	}

	static void AddStaticOverlaps(const UWorld& World, const FVector& Centre, float Radius, const FCollisionQueryParams& Params,
		TArray<AActor*>& OutActors)
	{
		TArray<FOverlapResult> Overlaps;
		World.OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
			FCollisionShape::MakeSphere(Radius), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (AActor* Actor = Overlap.GetActor())
			{
				OutActors.AddUnique(Actor);
			}
		}
	}

	void FindAnchorSupports(const UWorld& World, const FVector& Landing, const FVector& AnchorOrigin, float ProbeDepth,
		float SupportRadius, const FCollisionQueryParams& Params, TArray<AActor*>& OutSupports)
	{
		FHitResult Hit;
		if (World.LineTraceSingleByChannel(Hit, Landing + FVector(0.f, 0.f, 50.f), Landing - FVector(0.f, 0.f, ProbeDepth),
				ECC_WorldStatic, Params)
			&& Hit.GetActor())
		{
			OutSupports.AddUnique(Hit.GetActor());
		}
		AddStaticOverlaps(World, AnchorOrigin, SupportRadius, Params, OutSupports);
	}

	void FindStartSupports(const UWorld& World, const FVector& Start, float CapsuleHalfHeight, float IgnoreRadius,
		const FCollisionQueryParams& Params, TArray<AActor*>& OutSupports)
	{
		FHitResult Hit;
		if (World.LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, CapsuleHalfHeight + 50.f), ECC_WorldStatic,
				Params)
			&& Hit.GetActor())
		{
			OutSupports.AddUnique(Hit.GetActor());
		}
		AddStaticOverlaps(World, Start, IgnoreRadius, Params, OutSupports);
	}

	bool IsZipClear(const UWorld& World, const FVector& Launch, const FVector& ReleasePoint, const FVector& End,
		const FCollisionShape& Capsule, ECollisionChannel ObjectType, const FCollisionResponseParams& Responses,
		const FCollisionQueryParams& Params, const TArray<AActor*>& AnchorSupports, const TArray<AActor*>& StartSupports,
		FHitResult* OutHit)
	{
		auto Sweep = [&](const FVector& A, const FVector& B, bool bIgnoreStart) -> bool
		{
			if (A.Equals(B, 0.1f))
			{
				return true;
			}
			FCollisionQueryParams SweepParams = Params;
			SweepParams.AddIgnoredActors(AnchorSupports);
			if (bIgnoreStart)
			{
				SweepParams.AddIgnoredActors(StartSupports);
			}
			FHitResult Hit;
			if (World.SweepSingleByChannel(Hit, A, B, FQuat::Identity, ObjectType, Capsule, SweepParams, Responses))
			{
				if (OutHit)
				{
					*OutHit = Hit;
				}
				return false;
			}
			return true;
		};
		return Sweep(Launch, ReleasePoint, true) && Sweep(ReleasePoint, End, false);
	}
}
