#pragma once

#include "CoreMinimal.h"
#include "Traversal/GrappleAudit.h"

/** One row of the traversal debugger's lists: a roof or an anchor of the scene, its text and its colour. */
struct FTraversalDebuggerRow
{
	/** Index into the scene's roofs, or INDEX_NONE for an anchor row. */
	int32 Roof = INDEX_NONE;
	/** Index into the scene's anchors, or INDEX_NONE for a roof row. */
	int32 Anchor = INDEX_NONE;
	FString Label;
	FLinearColor Color = FLinearColor::White;
};

/**
 * What the traversal debugger's tab shows, worked out with no Slate: which reach results are listed and in what order,
 * each row's text and colour, the roofs the street cannot reach, the status and summary lines. STraversalDebuggerPanel
 * only lays these out, and UTraversalDebuggerSubsystem uses the same filter for what it draws, so the logic is tested
 * headless (Hawkeye.Editor.TraversalDebugger.*) without a window.
 */
struct FTraversalDebuggerViewModel
{
	/** The list's colour for a verdict: green reachable, yellow too steep, orange sight, red zip, grey the rest. */
	static FLinearColor ColorOf(EGrappleReachVerdict Verdict);

	/** The results worth listing (in range, not on her own roof), reachable first, then by verdict and distance. */
	static TArray<FGrappleReachEvaluation> SelectReachResults(const TArray<FGrappleReachEvaluation>& All);

	/** "City_Anchor_12 on W123, 14 m, 3.2 deg: SightBlocked (StaticMeshActor_4)" per listed result. */
	static TArray<FTraversalDebuggerRow> MakeReachRows(const FGrappleReachScene& Scene, const TArray<FGrappleReachEvaluation>& Results);

	/** The audited roofs the street cannot reach: street-facing ones (the gate's) first, then those with no sidewalk in range. */
	static TArray<FTraversalDebuggerRow> MakeRoofRows(const FGrappleAuditReport* Report);

	/** The status line: the subsystem's last message, or how to start. */
	static FString MakeStatusText(bool bHasSubsystem, const FString& Status);

	/** The summary line: the last audit's summary, or empty. */
	static FString MakeSummaryText(const FGrappleAuditReport* Report);
};
