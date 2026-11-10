#include "World/HawkeyeNavigationLibrary.h"

#include "Hawkeye.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

bool UHawkeyeNavigationLibrary::BuildNavigationNow(UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
	if (!NavSystem)
	{
		return false;
	}

	// An editor world adds AsyncLoadLock at load and waits for a core ticker to remove it; a
	// commandlet never ticks, so Build() would refuse forever without this.
	NavSystem->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	NavSystem->Build();

	const bool bBuilt = NavSystem->GetDefaultNavDataInstance() != nullptr;
	UE_LOG(LogHawkeye, Log, TEXT("%s: navigation built (%s)."), *GetNameSafe(World),
		bBuilt ? *GetNameSafe(NavSystem->GetDefaultNavDataInstance()) : TEXT("no nav data"));
	return bBuilt;
}

bool UHawkeyeNavigationLibrary::ProjectToNavigation(UObject* WorldContext, FVector Point, FVector Extent, FVector& OutProjected)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
	FNavLocation Location;
	if (!NavSystem || !NavSystem->ProjectPointToNavigation(Point, Location, Extent))
	{
		OutProjected = Point;
		return false;
	}
	OutProjected = Location.Location;
	return true;
}

float UHawkeyeNavigationLibrary::FindPathLength(UObject* WorldContext, FVector From, FVector To)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
	if (!NavSystem)
	{
		return -1.f;
	}
	const FVector Extent(100.f, 100.f, 250.f);
	FNavLocation Start;
	FNavLocation End;
	if (!NavSystem->ProjectPointToNavigation(From, Start, Extent) || !NavSystem->ProjectPointToNavigation(To, End, Extent))
	{
		return -1.f;
	}
	const UNavigationPath* Path = NavSystem->FindPathToLocationSynchronously(World, Start.Location, End.Location);
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		return -1.f;
	}
	return Path->GetPathLength();
}
