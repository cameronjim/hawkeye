#include "Chase/ChaseRouteDefinition.h"

TArray<FVector> UChaseRouteDefinition::GetPoints() const
{
	TArray<FVector> Out;
	Out.Reserve(Waypoints.Num());
	for (const FChaseWaypoint& Point : Waypoints)
	{
		Out.Add(Point.Location);
	}
	return Out;
}

TArray<FName> UChaseRouteDefinition::GetObjectives() const
{
	TArray<FName> Out;
	for (const FChaseWaypoint& Point : Waypoints)
	{
		if (!Point.CompletesObjective.IsNone())
		{
			Out.AddUnique(Point.CompletesObjective);
		}
	}
	if (!CatchObjective.IsNone())
	{
		Out.AddUnique(CatchObjective);
	}
	return Out;
}

bool UChaseRouteDefinition::IsInBeatZone(const FVector& Feet) const
{
	if (!bHasBeat)
	{
		return false;
	}
	const FVector Local = FRotator(0.f, BeatZoneYaw, 0.f).UnrotateVector(Feet - BeatZoneCentre);
	return FMath::Abs(Local.X) <= BeatZoneExtent.X && FMath::Abs(Local.Y) <= BeatZoneExtent.Y
		&& FMath::Abs(Local.Z) <= BeatZoneExtent.Z;
}

FPrimaryAssetId UChaseRouteDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Chase"), GetFName());
}
