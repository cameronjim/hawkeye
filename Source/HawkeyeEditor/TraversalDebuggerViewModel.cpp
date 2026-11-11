#include "TraversalDebuggerViewModel.h"

FLinearColor FTraversalDebuggerViewModel::ColorOf(EGrappleReachVerdict Verdict)
{
	switch (Verdict)
	{
	case EGrappleReachVerdict::Reachable:
		return FLinearColor(0.3f, 1.f, 0.4f);
	case EGrappleReachVerdict::TooSteep:
		return FLinearColor(1.f, 1.f, 0.3f);
	case EGrappleReachVerdict::SightBlocked:
		return FLinearColor(1.f, 0.6f, 0.2f);
	case EGrappleReachVerdict::ZipBlocked:
		return FLinearColor(1.f, 0.3f, 0.3f);
	default:
		return FLinearColor(0.7f, 0.7f, 0.7f);
	}
}

TArray<FGrappleReachEvaluation> FTraversalDebuggerViewModel::SelectReachResults(const TArray<FGrappleReachEvaluation>& All)
{
	TArray<FGrappleReachEvaluation> Results = All.FilterByPredicate([](const FGrappleReachEvaluation& Result)
	{
		return Result.Verdict != EGrappleReachVerdict::TooFar && Result.Verdict != EGrappleReachVerdict::SameRoof;
	});
	// EGrappleReachVerdict is declared in check order with Reachable first.
	Results.StableSort([](const FGrappleReachEvaluation& A, const FGrappleReachEvaluation& B)
	{
		return A.Verdict != B.Verdict ? A.Verdict < B.Verdict : A.Distance < B.Distance;
	});
	return Results;
}

TArray<FTraversalDebuggerRow> FTraversalDebuggerViewModel::MakeReachRows(const FGrappleReachScene& Scene,
	const TArray<FGrappleReachEvaluation>& Results)
{
	TArray<FTraversalDebuggerRow> Rows;
	for (const FGrappleReachEvaluation& Result : Results)
	{
		if (!Scene.Anchors.IsValidIndex(Result.Anchor))
		{
			continue;
		}
		const FGrappleReachAnchor& Anchor = Scene.Anchors[Result.Anchor];
		const FString Roof = Scene.Roofs.IsValidIndex(Anchor.Roof) ? Scene.Roofs[Anchor.Roof].OsmId : FString(TEXT("no roof"));
		const FString Blocker = Result.Blocker.Name.IsEmpty() ? FString() : FString::Printf(TEXT(" (%s)"), *Result.Blocker.Name);
		FTraversalDebuggerRow& Row = Rows.AddDefaulted_GetRef();
		Row.Anchor = Result.Anchor;
		Row.Label = FString::Printf(TEXT("%s on %s, %.0f m, %.1f deg: %s%s"), *Anchor.Name, *Roof, Result.Distance / 100.f, Result.AngleDegrees,
			LexToString(Result.Verdict), *Blocker);
		Row.Color = ColorOf(Result.Verdict);
	}
	return Rows;
}

TArray<FTraversalDebuggerRow> FTraversalDebuggerViewModel::MakeRoofRows(const FGrappleAuditReport* Report)
{
	TArray<FTraversalDebuggerRow> Rows;
	if (!Report)
	{
		return Rows;
	}
	// Street-facing first: those are the ones the gate fails on.
	for (const EGrappleRoofReach Wanted : { EGrappleRoofReach::Unreached, EGrappleRoofReach::NoStreetInRange })
	{
		for (const FGrappleAuditRoofRow& Roof : Report->Roofs)
		{
			if (Roof.Reach != Wanted)
			{
				continue;
			}
			FTraversalDebuggerRow& Row = Rows.AddDefaulted_GetRef();
			Row.Roof = Roof.Roof;
			Row.Label = FString::Printf(TEXT("%s (%s, %.1f m, %d anchors, %d street spots in range): %s"), *Roof.OsmId, *Roof.ActorName,
				Roof.HeightM, Roof.Anchors, Roof.StreetSpotsInRange + Roof.FineSpotsInRange, Roof.Why.IsEmpty() ? LexToString(Roof.Reach) : *Roof.Why);
			Row.Color = Wanted == EGrappleRoofReach::Unreached ? FLinearColor(1.f, 0.35f, 0.35f) : FLinearColor(0.8f, 0.5f, 1.f);
		}
	}
	return Rows;
}

FString FTraversalDebuggerViewModel::MakeStatusText(bool bHasSubsystem, const FString& Status)
{
	if (!bHasSubsystem)
	{
		return TEXT("The traversal debugger subsystem is not running.");
	}
	return Status.IsEmpty() ? FString(TEXT("Open the district and press Refresh scene, Reach from camera or Run audit.")) : Status;
}

FString FTraversalDebuggerViewModel::MakeSummaryText(const FGrappleAuditReport* Report)
{
	return Report ? Report->Summary : FString();
}
