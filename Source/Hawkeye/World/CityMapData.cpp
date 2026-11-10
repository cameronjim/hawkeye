#include "World/CityMapData.h"

const TCHAR* UCityMapData::DefaultPath = TEXT("/Game/City/EastVillage/DA_EastVillage_Map.DA_EastVillage_Map");

bool UCityMapData::IsUsable() const
{
	return BoundsMax.X > BoundsMin.X && BoundsMax.Y > BoundsMin.Y && Footprints.Num() > 0;
}

const FCityMapPolygon* UCityMapData::FindFootprint(const FString& OsmId) const
{
	return Footprints.FindByPredicate([&OsmId](const FCityMapPolygon& Footprint) { return Footprint.OsmId == OsmId; });
}
