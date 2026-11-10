#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ThugTreeBuilder.generated.h"

class UEnvQuery;
class UStateTree;

/**
 * Builds the thug brain's assets headless, the way UHawkeyePartnerTreeBuilder builds ST_Partner
 * (Tools/Editor/create_enemies.py calls both functions):
 *
 *   ST_Thug          the thug's decisions (claude-docs/architecture.md, "Thugs"). Schema: StateTree AI
 *                    Component. A root that tries its children by priority (Stunned, Reposition, Cover,
 *                    Attack, Investigate, Patrol); Attack holds a state per weapon, each a short sequence of
 *                    tasks. Conditions are FHawkeyeThugSenseCondition; transitions leave a state when a higher
 *                    one is wanted or its own reason is gone, and chain the fight's steps on completion.
 *   EQS_CoverPoints  an EnvQuery data asset: a donut of candidates round the querier (3 rings of 12,
 *                    120 to 800 cm, projected onto the navmesh), filtered by a trace from the thug's
 *                    target (UHawkeyeEnvQueryContext_ThugTarget) that must be blocked, scored nearest
 *                    first. Authored through UEnvQuery's options directly: no EQS editor graph, so the
 *                    asset shows an empty graph if opened in the editor (do not save it from there).
 *
 * Editor builds only; in a cooked game the builders log an error and return null.
 */
UCLASS()
class HAWKEYE_API UHawkeyeThugTreeBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Creates (or rebuilds in place) ST_Thug at PackageName and compiles it. Dirty and unsaved; null on failure. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Thug")
	static UStateTree* BuildThugStateTree(const FString& PackageName);

	/** Writes ST_Thug's states into Tree (possibly a transient one) and compiles it. False when it did not compile. */
	static bool FillThugStateTree(UStateTree* Tree);

	/** The number of states under the root of Tree's editor data (the six top-level states), for verify scripts. -1 without one. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Thug")
	static int32 CountThugStates(UStateTree* Tree);

	/** Tree's states as nested names, "Thug(Stunned, Reposition(BackAway, ArcherZip), ...)". Empty without editor data. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Thug")
	static FString DescribeThugStateTree(UStateTree* Tree);

	/** Creates (or rebuilds in place) EQS_CoverPoints at PackageName. Dirty and unsaved; null on failure. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Thug")
	static UEnvQuery* BuildCoverQuery(const FString& PackageName);

	/** "generator Donut, tests Trace+Distance" style summary of Query's first option, for verify scripts. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Thug")
	static FString DescribeCoverQuery(UEnvQuery* Query);

	/** Fills Query (possibly a transient one) with the cover search. Shared by the builder and the tests. */
	static void FillCoverQuery(UEnvQuery* Query, float OuterRadius);
};
