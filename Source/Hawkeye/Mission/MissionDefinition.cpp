#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"

FPrimaryAssetId UMissionDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Mission"), GetFName());
}
