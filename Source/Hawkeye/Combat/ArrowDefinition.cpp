#include "Combat/ArrowDefinition.h"

FText UArrowDefinition::GetDisplayNameOrAssetName() const
{
	return DisplayName.IsEmpty() ? FText::FromName(GetFName()) : DisplayName;
}

FText UArrowDefinition::GetShortNameOrDisplayName() const
{
	return ShortName.IsEmpty() ? GetDisplayNameOrAssetName() : ShortName;
}

FPrimaryAssetId UArrowDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Arrow"), GetFName());
}
