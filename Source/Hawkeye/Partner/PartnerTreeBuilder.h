#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PartnerTreeBuilder.generated.h"

class UStateTree;

/**
 * Builds ST_Partner headless, so the partner's StateTree comes from a script like every other
 * asset here (Tools/Editor/create_partner.py calls it). The tree: a root that tries its children
 * in order, one state per EHawkeyePartnerMode in priority order (Revive, GoToMark, Cover, Attack,
 * Follow), each entered on a FHawkeyePartnerNeedCondition (Follow has none), each running a
 * FHawkeyePartnerModeTask, and each going back to the root to choose again when its task succeeds.
 * Schema: StateTree AI Component, so the tasks get the AIController as context.
 *
 * Editor builds only; in a cooked game the function logs an error and returns null.
 */
UCLASS()
class HAWKEYE_API UHawkeyePartnerTreeBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or rebuilds in place) the StateTree asset at PackageName (e.g.
	 * "/Game/Blueprints/AI/Partner/ST_Partner") and compiles it. Returns the tree, dirty and unsaved,
	 * or null when compiling failed (the compiler's messages are logged). The caller saves it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Partner")
	static UStateTree* BuildPartnerStateTree(const FString& PackageName);

	/** The number of states under the root of Tree's editor data, for verify scripts. -1 without one. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Partner")
	static int32 CountPartnerStates(UStateTree* Tree);
};
