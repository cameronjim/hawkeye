#pragma once

#include "CoreMinimal.h"
#include "Traversal/GrappleReachWorld.h"

class UWorld;

/** One anchored roof in the audit: the JSON's "roofs" row plus what the debugger draws and gates on. */
struct HAWKEYE_API FGrappleAuditRoofRow
{
	/** Index into the scene's roofs. */
	int32 Roof = INDEX_NONE;
	FString OsmId;
	FString ActorName;
	float HeightM = 0.f;
	int32 Anchors = 0;
	/** Anchors on this roof reachable from a 10 m sidewalk spot. */
	int32 FromStreet = 0;
	/** Anchors on this roof reachable from another roof's spot. */
	int32 FromRoofs = 0;
	/** 10 m sidewalk spots within range of one of its anchors. */
	int32 StreetSpotsInRange = 0;
	/** 2.5 m sidewalk spots within range, counted only for a roof the 10 m spots do not reach. */
	int32 FineSpotsInRange = 0;
	/** Anchors reachable from a 2.5 m spot (the second pass). */
	int32 FromFineStreet = 0;
	int32 Neighbours = 0;
	int32 NeighboursReaching = 0;
	EGrappleRoofReach Reach = EGrappleRoofReach::NoAnchors;
	/** Why no 2.5 m spot reaches it ("view blocked by its own building x12, ..."), empty when reached. */
	FString Why;
};

/**
 * What an audit found, in the shape Hawkeye.Grapple.Audit writes grapple_audit.json, plus the street-facing roofs
 * nothing reaches (the commandlet's gate).
 */
struct HAWKEYE_API FGrappleAuditReport
{
	FString Summary;
	TArray<FString> Objectives;
	TArray<FString> PickerMisses;
	TArray<FString> RoofsNoStreet;
	TArray<FString> RoofsNoStreetWithSpotInRange;
	TArray<FString> RoofsNoStreetAtFineStep;
	TArray<FString> RoofsNoRoof;
	TArray<FString> NeighbourPairsMissing;
	TArray<FString> AnchorsNeverReachable;
	TArray<FString> AnchorsNeverPicked;
	/** Anchored roofs only, in scene order. */
	TArray<FGrappleAuditRoofRow> Roofs;
	/** Indices into Roofs of the street-facing roofs no sidewalk spot reaches. */
	TArray<int32> StreetFacingUnreached;
	/** A chain objective's roof the street or the objective before cannot reach. */
	bool bObjectiveChainBroken = false;
	TArray<FString> Errors;

	int32 AnchorCount = 0;
	int32 StreetSpotCount = 0;
	int32 RoofSpotCount = 0;
	double Seconds = 0.0;

	/** The report as grapple_audit.json: the test's keys and layout, plus "street_facing_unreached". */
	FString ToJson() const;

	/** Writes ToJson to Path, making the directory. */
	bool WriteJson(const FString& Path) const;

	/** The street-facing unreached roofs whose osm id is not in Allowed, as labels. */
	TArray<FString> FindGateFailures(const TSet<FString>& Allowed) const;

	/** Saved/Automation/grapple_audit.json, where the test writes it. */
	static FString GetDefaultPath();
};

/** Everything an audit run reads. MakeWorldInputs fills the world-backed parts. */
struct HAWKEYE_API FGrappleAuditInputs
{
	const FGrappleReachScene* Scene = nullptr;
	FGrappleReachProfile Profile;
	FGrappleReachOracle Oracle;
	FGrappleAuditSettings Settings;
	/** What is under a sidewalk candidate. */
	TFunction<FGrappleReachQuery::EStand(const FVector2D& Point, FVector& OutFloor)> SidewalkFloor;
	/** The top of roof Roof at Point, false when Point is on another building. */
	TFunction<bool(const FVector2D& Point, int32 Roof, FVector& OutTop)> RoofTop;
	/**
	 * Objective ids whose roofs must chain from the street, in order (Hawkeye.Grapple.Audit checks chapter 1's
	 * three). Data, not code: the commandlet takes them on its command line.
	 */
	TArray<FString> ObjectiveChain;
};

/**
 * The grapple audit with no character: the same spots, rules, neighbours and chain as Hawkeye.Grapple.Audit, over an
 * FGrappleReachScene and an oracle. The one thing it cannot check offline is the picker against the full check (that
 * needs UGrappleComponent in a game world), so "picker_misses" and "anchors_never_picked" stay empty and the summary
 * says so. Shared by the traversal debugger and UHawkeyeTraversalAuditCommandlet.
 */
struct HAWKEYE_API FGrappleAudit
{
	static FGrappleAuditReport Run(const FGrappleAuditInputs& Inputs);

	/** Inputs over World: its oracle, sidewalk floor and roof tops. Scene must outlive the run. */
	static FGrappleAuditInputs MakeWorldInputs(UWorld* World, const FGrappleReachScene& Scene, const FGrappleReachProfile& Profile);
};
