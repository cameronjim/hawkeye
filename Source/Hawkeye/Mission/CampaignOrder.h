#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CampaignOrder.generated.h"

class UMissionDefinition;

/**
 * The campaign's chapter order and which chapters are open, pure (claude-docs/gameplay-semantics.md, "Bosses",
 * Campaign order). The order is the chain of UMissionDefinition::NextChapter from the district's starting chapter;
 * chapter select in the safehouse lists it. Hawkeye.Campaign.* tests call it directly.
 */
UCLASS()
class HAWKEYE_API UHawkeyeCampaignOrder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** First, then its NextChapter, and so on; stops at a missing link, a chapter already listed, or MaxChapters. */
	static TArray<UMissionDefinition*> Build(UMissionDefinition* First, int32 MaxChapters = 32);

	/**
	 * A chapter is open: one at or before the chapter in progress, or the one right after it once it is complete.
	 * With no chapter in progress (or one not in the order) only the first is.
	 */
	static bool IsOpen(int32 Index, int32 CurrentIndex, bool bCurrentComplete, int32 Count);

	/** Chapter select may start it: open, and not the chapter already in progress. */
	static bool CanStart(int32 Index, int32 CurrentIndex, bool bCurrentComplete, int32 Count);
};
