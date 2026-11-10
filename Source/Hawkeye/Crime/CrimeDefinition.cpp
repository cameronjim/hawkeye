#include "Crime/CrimeDefinition.h"

#include "Crime/CrimeRules.h"

int32 UCrimeDefinition::GetThugCount() const
{
	int32 Count = 0;
	for (const FCrimeRosterEntry& Entry : Roster)
	{
		Count += FMath::Max(Entry.Count, 0);
	}
	return Count;
}

FText UCrimeDefinition::GetDisplayName() const
{
	if (!Name.IsEmpty())
	{
		return Name;
	}
	return FText::Format(NSLOCTEXT("Hawkeye", "CrimeUnnamed", "[Crime: {0}]"), UCrimeRules::TypeText(Type));
}

FPrimaryAssetId UCrimeDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Crime"), GetFName());
}
