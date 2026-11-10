#include "World/ThugHearing.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "World/DoorActor.h"

namespace HawkeyeThugHearing
{
	bool IsAirClear(UWorld* World, const FVector& A, const FVector& B, const TArray<const AActor*>& Ignore)
	{
		if (!World)
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ThugHearing), false);
		for (const AActor* Actor : Ignore)
		{
			Params.AddIgnoredActor(Actor);
		}
		FCollisionObjectQueryParams Solid;
		Solid.AddObjectTypesToQuery(ECC_WorldStatic);
		Solid.AddObjectTypesToQuery(ECC_WorldDynamic);
		return !World->LineTraceTestByObjectType(A, B, Solid, Params);
	}

	int32 CountClosedDoorsOnPath(UWorld* World, const TArray<FVector>& Points)
	{
		if (!World || Points.Num() < 2)
		{
			return 0;
		}
		int32 Count = 0;
		for (TActorIterator<ADoorActor> It(World); It; ++It)
		{
			if (It->IsOpen())
			{
				continue;
			}
			const FVector Threshold = It->GetActorLocation();
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				const FVector& A = Points[Index - 1];
				const FVector& B = Points[Index];
				// Same floor: the segment passes within a storey of the threshold.
				if (FMath::Min(A.Z, B.Z) > Threshold.Z + 200.f || FMath::Max(A.Z, B.Z) < Threshold.Z - 100.f)
				{
					continue;
				}
				const FVector Closest = FMath::ClosestPointOnSegment(FVector(Threshold.X, Threshold.Y, 0.f),
					FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f));
				if (FVector::Dist2D(Closest, Threshold) <= DoorPassDistance)
				{
					++Count;
					break;
				}
			}
		}
		return Count;
	}

	FHeardNoise Hear(UWorld* World, const FVector& Noise, const FVector& Ear, float Range, const TArray<const AActor*>& Ignore)
	{
		FHeardNoise Out;
		if (!World || Range <= 0.f || FVector::Dist(Noise, Ear) > Range)
		{
			return Out;
		}
		const FVector Rise(0.f, 0.f, OverheadRise);
		if (IsAirClear(World, Noise, Ear, Ignore) || IsAirClear(World, Noise + Rise, Ear + Rise, Ignore))
		{
			Out.bHeard = true;
			Out.bDirect = true;
			return Out;
		}
		// Round by the doorways: the way a person would walk it.
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
		if (!NavData)
		{
			return Out;
		}
		FNavLocation From;
		FNavLocation To;
		const FVector Extent(100.f, 100.f, 200.f);
		if (!Nav->ProjectPointToNavigation(Noise, From, Extent) || !Nav->ProjectPointToNavigation(Ear - FVector(0.f, 0.f, 120.f), To, Extent))
		{
			return Out;
		}
		const FPathFindingQuery Query(nullptr, *NavData, From.Location, To.Location);
		const FPathFindingResult Result = Nav->FindPathSync(Query);
		if (!Result.IsSuccessful() || Result.IsPartial() || !Result.Path.IsValid())
		{
			return Out;
		}
		TArray<FVector> Points;
		for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
		{
			Points.Add(Point.Location);
		}
		Out.PathLength = Result.Path->GetLength();
		Out.ClosedDoors = CountClosedDoorsOnPath(World, Points);
		Out.bHeard = Out.PathLength <= Range * (Out.ClosedDoors > 0 ? ClosedDoorFactor : 1.f);
		return Out;
	}
}
