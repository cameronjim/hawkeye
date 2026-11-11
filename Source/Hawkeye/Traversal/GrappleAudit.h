#pragma once

#include "CoreMinimal.h"
#include "Traversal/GrappleReachWorld.h"

class AActor;
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
	/** Walls facing a street in the open (FGrappleReachQuery::CountStreetWalls); 0 inside a block or behind buildings. */
	int32 StreetWalls = 0;
	/** Roofs touching this one within the walkable rise (walked or mantled onto). */
	int32 Walkable = 0;
	EGrappleRoofReach Reach = EGrappleRoofReach::NoAnchors;
	/** Why no 2.5 m spot reaches it ("view blocked by its own building x12, ..."), empty when reached. */
	FString Why;
};

/** One anchor in the audit, over every spot: the JSON's "anchors" row, for diffing two audits of one map. */
struct HAWKEYE_API FGrappleAuditAnchorRow
{
	FString Name;
	/** The roof's osm id, empty for an anchor on no roof. */
	FString RoofOsmId;
	int32 InRange = 0;
	int32 Seen = 0;
	int32 Clear = 0;
	int32 Reach = 0;
	/** Reachable pairs the picker marked dead on (0 without a picker). */
	int32 Picked = 0;
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
	/** Every anchor, in scene order. */
	TArray<FGrappleAuditAnchorRow> AnchorRows;
	/** Indices into Roofs of the street-facing roofs no sidewalk spot reaches. */
	TArray<int32> StreetFacingUnreached;
	/**
	 * The anchored roofs the street does not reach that are not street-facing, each with its class (no sidewalk spot in
	 * range, or no wall on a street) and how it is reached instead (from another roof's spots, walked onto, or not):
	 * listed, never failed.
	 */
	TArray<FString> RoofsOffTheStreet;
	/** A chain objective's roof the street or the objective before cannot reach. */
	bool bObjectiveChainBroken = false;
	TArray<FString> Errors;

	int32 AnchorCount = 0;
	int32 StreetSpotCount = 0;
	int32 RoofSpotCount = 0;
	/** Spot-anchor pairs in range, seen, zip-clear and reachable. */
	int64 PairsInRange = 0;
	int64 SeenPairs = 0;
	int64 ClearPairs = 0;
	int64 ReachablePairs = 0;
	/** Only with a picker (FGrappleAuditInputs::Picker): reachable pairs the picker marks dead on. */
	bool bPickerChecked = false;
	int64 PickedPairs = 0;
	/** Dead-on views where the picker and the full check disagree; views aimed 6.3 degrees off and those that disagree. */
	int64 PickerDisagreements = 0;
	int64 RoughViews = 0;
	int64 RoughMismatches = 0;
	/** "dead on, the picker disagreed with the full check on ...", empty without a picker. */
	FString PickerSummary;
	int32 NeighbourPairs = 0;
	int32 NeighbourPairsNotReaching = 0;
	int32 NeighbourPairsOutOfRange = 0;
	double Seconds = 0.0;

	/**
	 * The report as grapple_audit.json: the keys and layout Hawkeye.Grapple.Audit has always written, plus
	 * "street_facing_unreached" and "anchors" (one row per anchor, for diffing two audits).
	 */
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

	/**
	 * Chain objectives that may instead be reached down a fire escape on a roof the chain gets to and up from the
	 * street (find_arrow: the street between it and cross_block is wider than the grapple's range). Any other link must
	 * be made on the roofs.
	 */
	TSet<FString> StreetLinkObjectives;

	/**
	 * Optional: the game's own picker (UGrappleComponent::SelectBestAnchorFrom) standing at Spot with the camera at Lens
	 * looking along Forward. Returns the index into the scene's anchors it marks, or INDEX_NONE; OutGreyed is the one it
	 * greys out (INDEX_NONE for none) and OutReason why. With it, every reachable pair also checks that the picker agrees
	 * with the full check (every anchor through every rule, then FGrappleReachQuery::ChoosePick), dead on and 6.3 degrees
	 * off each way, and the report's picker fields are filled. Needs a character in a game world, so only the test has one.
	 */
	TFunction<int32(const FGrappleReachSpot& Spot, const FVector& Lens, const FVector& Forward, int32& OutGreyed, FString& OutReason)> Picker;
};

/**
 * The grapple audit: sidewalk and roof spots, every spot against every anchor in range, the second pass at 2.5 m, the
 * neighbours across a gap and the objective chain, over an FGrappleReachScene and an oracle. Hawkeye.Grapple.Audit runs
 * it in the game world with Kate's own traces and picker; the traversal debugger and UHawkeyeTraversalAuditCommandlet
 * run it in an editor world with FGrappleReachWorld's oracle and no picker, so there "picker_misses" and
 * "anchors_never_picked" stay empty and the summary says the picker was not checked.
 */
struct HAWKEYE_API FGrappleAudit
{
	static FGrappleAuditReport Run(const FGrappleAuditInputs& Inputs);

	/**
	 * Inputs over World: its oracle, sidewalk floor and roof tops. Scene must outlive the run. Ignore (a character
	 * standing in the world) is left out of the sidewalk and roof-top traces.
	 */
	static FGrappleAuditInputs MakeWorldInputs(UWorld* World, const FGrappleReachScene& Scene, const FGrappleReachProfile& Profile,
		const AActor* Ignore = nullptr);
};
