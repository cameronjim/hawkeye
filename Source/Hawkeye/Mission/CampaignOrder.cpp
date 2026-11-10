#include "Mission/CampaignOrder.h"

#include "Mission/MissionDefinition.h"

TArray<UMissionDefinition*> UHawkeyeCampaignOrder::Build(UMissionDefinition* First, int32 MaxChapters)
{
	TArray<UMissionDefinition*> Order;
	for (UMissionDefinition* Chapter = First; Chapter && Order.Num() < MaxChapters && !Order.Contains(Chapter);
		Chapter = Chapter->NextChapter.LoadSynchronous())
	{
		Order.Add(Chapter);
	}
	return Order;
}

bool UHawkeyeCampaignOrder::IsOpen(int32 Index, int32 CurrentIndex, bool bCurrentComplete, int32 Count)
{
	if (Index < 0 || Index >= Count)
	{
		return false;
	}
	if (CurrentIndex < 0 || CurrentIndex >= Count)
	{
		return Index == 0;
	}
	return Index <= CurrentIndex || (Index == CurrentIndex + 1 && bCurrentComplete);
}

bool UHawkeyeCampaignOrder::CanStart(int32 Index, int32 CurrentIndex, bool bCurrentComplete, int32 Count)
{
	return IsOpen(Index, CurrentIndex, bCurrentComplete, Count) && !(Index == CurrentIndex && !bCurrentComplete);
}
