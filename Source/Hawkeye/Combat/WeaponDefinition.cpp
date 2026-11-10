#include "Combat/WeaponDefinition.h"

UWeaponDefinition::UWeaponDefinition()
{
	HeadBoneNames.Add(FName(TEXT("head")));
	HeadBoneNames.Add(FName(TEXT("neck_01")));
}

FText UWeaponDefinition::GetDisplayNameOrAssetName() const
{
	return DisplayName.IsEmpty() ? FText::FromName(GetFName()) : DisplayName;
}

FText UWeaponDefinition::GetShortNameOrDisplayName() const
{
	if (!ShortName.IsEmpty())
	{
		return ShortName;
	}
	return GetDisplayNameOrAssetName();
}

FPrimaryAssetId UWeaponDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Weapon"), GetFName());
}
