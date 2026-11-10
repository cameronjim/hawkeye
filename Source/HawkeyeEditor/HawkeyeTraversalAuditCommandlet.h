#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HawkeyeTraversalAuditCommandlet.generated.h"

/**
 * The grapple audit headless, as a pipeline gate (claude-docs/tools-traversal-debugger.md):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -run=HawkeyeTraversalAudit -map=/Game/Maps/L_District_EastVillage
 *       [-out=<json>] [-chain=reach_roof,cross_block,find_arrow] [-allowfile=<txt>] [-pawn=<class path>] [-nospawn]
 *
 * Loads the map in the editor world, runs FGrappleAudit through FTraversalAuditRunner (the traversal debugger's own
 * path), writes grapple_audit.json (Saved/Automation by default) and returns 0 when every street-facing roof is
 * reachable from the street (or listed in the allow file, one osm id per line, # comments) and the objective chain
 * holds; 1 when not; 2 when it could not run (no map, no city data).
 */
UCLASS()
class UHawkeyeTraversalAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UHawkeyeTraversalAuditCommandlet(const FObjectInitializer& ObjectInitializer);

	virtual int32 Main(const FString& Params) override;

	/** The osm ids in an allow file: one per line, the first word, blank lines and # comments skipped. */
	static TSet<FString> ParseAllowList(const TArray<FString>& Lines);
};
