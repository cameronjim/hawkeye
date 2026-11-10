#include "Mission/ChapterMaps.h"

#include "Engine/World.h"
#include "HawkeyeGameMode.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/MissionDefinition.h"

bool UHawkeyeChapterMaps::BelongsInWorld(const FString& MapPackage, const FString& WorldPackage, bool bWorldIsInterior)
{
	if (MapPackage.IsEmpty())
	{
		return !bWorldIsInterior;
	}
	return MapPackage.Equals(WorldPackage, ESearchCase::IgnoreCase);
}

bool UHawkeyeChapterMaps::BelongsInWorld(const TSoftObjectPtr<UWorld>& Map, const UWorld* World)
{
	return BelongsInWorld(PackageOf(Map), PackageOf(World), IsInteriorWorld(World));
}

FString UHawkeyeChapterMaps::PackageOf(const UWorld* World)
{
	return World && World->GetOutermost() ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
}

FString UHawkeyeChapterMaps::PackageOf(const TSoftObjectPtr<UWorld>& Map)
{
	return Map.IsNull() ? FString() : Map.ToSoftObjectPath().GetLongPackageName();
}

bool UHawkeyeChapterMaps::IsInteriorWorld(const UWorld* World)
{
	return AHawkeyeGameMode::WantsInteriorCamera(World);
}

int32 UHawkeyeChapterMaps::PickSite(const TArray<FString>& Maps, const FString& WorldPackage, bool bWorldIsInterior)
{
	for (int32 Index = 0; Index < Maps.Num(); ++Index)
	{
		if (BelongsInWorld(Maps[Index], WorldPackage, bWorldIsInterior))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

UChapterSiteDefinition* UHawkeyeChapterMaps::FindSiteFor(const UMissionDefinition* Mission, const UWorld* World)
{
	if (!Mission)
	{
		return nullptr;
	}
	TArray<UChapterSiteDefinition*> Sites;
	if (UChapterSiteDefinition* Main = Mission->Site.LoadSynchronous())
	{
		Sites.Add(Main);
	}
	for (const TSoftObjectPtr<UChapterSiteDefinition>& Soft : Mission->InteriorSites)
	{
		if (UChapterSiteDefinition* Site = Soft.LoadSynchronous())
		{
			Sites.Add(Site);
		}
	}
	TArray<FString> Maps;
	for (const UChapterSiteDefinition* Site : Sites)
	{
		Maps.Add(PackageOf(Site->Map));
	}
	const int32 Index = PickSite(Maps, PackageOf(World), IsInteriorWorld(World));
	return Sites.IsValidIndex(Index) ? Sites[Index] : nullptr;
}

bool UHawkeyeChapterMaps::ContinuesInto(const UMissionDefinition* Mission, const FString& InteriorPackage)
{
	if (!Mission || InteriorPackage.IsEmpty())
	{
		return false;
	}
	auto Names = [&InteriorPackage](const TSoftObjectPtr<UChapterSiteDefinition>& Soft)
	{
		const UChapterSiteDefinition* Site = Soft.LoadSynchronous();
		return Site && PackageOf(Site->Map).Equals(InteriorPackage, ESearchCase::IgnoreCase);
	};
	if (Names(Mission->Site))
	{
		return true;
	}
	for (const TSoftObjectPtr<UChapterSiteDefinition>& Soft : Mission->InteriorSites)
	{
		if (Names(Soft))
		{
			return true;
		}
	}
	return false;
}
