#pragma once

#include "CoreMinimal.h"
#include "Traversal/GrappleReachTypes.h"

/**
 * The grapple's reach as pure functions (claude-docs/tools-traversal-debugger.md): which anchors a standing
 * point can grapple and why not, which roofs no pavement sample reaches, and the footprint geometry the audit
 * uses (roof spots, neighbours across a gap, chains of roofs). Nothing here touches a world or a UObject: the
 * questions only a world can answer (a sight line, a capsule sweep, the spring arm's probe, the floor under a
 * point) come in through an FGrappleReachOracle or a callback, so the tests drive it with fakes and the
 * traversal debugger, the audit commandlet and the game share one set of rules.
 *
 * It mirrors UGrappleComponent's targeting (range, cone, IsOnScreen, sight, IsZipClear, ChoosePick), which
 * calls the math here, and Hawkeye.Grapple.Audit's sampling, which mirrors Tools/Editor/_reach.py.
 */
struct HAWKEYE_API FGrappleReachQuery
{
	// --- The view --------------------------------------------------------------------------------

	/** Angle between ViewForward and the line from ViewLocation to Point, degrees. */
	static float AngleOffViewDegrees(const FVector& ViewLocation, const FVector& ViewForward, const FVector& Point);

	/**
	 * Point is on the screen of a view from ViewLocation along ViewForward (no roll) with the rules' field of view
	 * across and aspect ratio, ScreenEdgeMarginDegrees inside every edge (UGrappleComponent::IsOnScreen).
	 */
	static bool IsOnScreen(const FGrappleReachRules& Rules, const FVector& ViewLocation, const FVector& ViewForward,
		const FVector& Point);

	/** MinRange <= Distance <= Range. */
	static bool IsInRange(const FGrappleReachRules& Rules, float Distance);

	/**
	 * The picker's rule over anchors that pass everything (X the angle off the middle of the screen, degrees; Y the
	 * distance, cm): the one nearest the middle, or of those within TieDegrees of it the nearest to her. INDEX_NONE
	 * for none.
	 */
	static int32 ChoosePick(const TArray<FVector2f>& AnglesAndDistances, float TieDegrees);

	/**
	 * Where the hip camera sits when a player turns it to put Target in the middle of the screen, with the capsule
	 * centre at Centre: the pivot, the arm's length back along the view and the socket offset out, the probe pulling
	 * it in, iterated three times because the lens moves with the pitch (HawkeyeGrappleView::PredictLens).
	 */
	static void PredictLens(const FGrappleLensModel& Model, const FVector& Centre, const FVector& Target,
		const TFunction<FVector(const FVector&, const FVector&)>& Probe, FVector& OutLens, FVector& OutForward);

	// --- The zip ---------------------------------------------------------------------------------

	/** Where the straight line starts from a capsule centre: ZipLaunchHeight up from the ground, itself mid-air. */
	static FVector ComputeZipLaunch(const FGrappleReachRules& Rules, const FVector& Centre, bool bFromGround);

	/** Where the capsule centre ends a zip: feet on the landing point plus the stand clearance. */
	static FVector ComputeZipEnd(const FGrappleReachRules& Rules, const FVector& Landing);

	/** The capsule centre of a character standing on Floor. */
	static FVector ComputeStandingCentre(const FGrappleReachRules& Rules, const FVector& Floor);

	/**
	 * The first point on the line Launch -> End that is ReleaseDistance from From: where the geometry round the
	 * start counts again (UGrappleComponent::IsZipClear). End when the line never gets that far.
	 */
	static FVector ComputeStartReleasePoint(const FVector& From, const FVector& Launch, const FVector& End, float ReleaseDistance);

	// --- Anchors from a standing point -----------------------------------------------------------

	/**
	 * One anchor from one spot, through the checks in EGrappleReachVerdict's order. With bCheckZipWhenUnseen the
	 * zip is swept even when the view fails, so an audit can count seen and clear apart.
	 */
	static FGrappleReachEvaluation Evaluate(const FGrappleReachRules& Rules, const FGrappleLensModel& Lens,
		const FGrappleReachOracle& Oracle, const FGrappleReachSpot& Spot, const TArray<FGrappleReachAnchor>& Anchors,
		int32 AnchorIndex, bool bCheckZipWhenUnseen = false);

	/**
	 * Every anchor from one spot: "from here, which anchors can be grappled, and why not the others". Anchors out of
	 * range are answered without asking the world, so this is cheap enough for a button press over a district.
	 */
	static void EvaluateAll(const FGrappleReachRules& Rules, const FGrappleLensModel& Lens, const FGrappleReachOracle& Oracle,
		const FGrappleReachSpot& Spot, const TArray<FGrappleReachAnchor>& Anchors, TArray<FGrappleReachEvaluation>& OutEvaluations);

	/** Of the evaluations, the one the picker would mark looking along ViewForward from ViewLocation, or INDEX_NONE. */
	static int32 PickFrom(const FGrappleReachRules& Rules, const TArray<FGrappleReachAnchor>& Anchors,
		const TArray<FGrappleReachEvaluation>& Evaluations, const FVector& ViewLocation, const FVector& ViewForward);

	// --- Footprints ------------------------------------------------------------------------------

	static bool IsInsideRing(const FVector2D& Point, const TArray<FVector2D>& Ring);
	static double SegmentDistance(const FVector2D& Point, const FVector2D& A, const FVector2D& B);
	/** Distance from Point to the ring's outline; OutClosest is the nearest point on it. */
	static double ClosestOnRing(const FVector2D& Point, const TArray<FVector2D>& Ring, FVector2D& OutClosest);
	/** Shortest distance between two outlines (vertex to edge both ways). */
	static double RingGap(const TArray<FVector2D>& A, const TArray<FVector2D>& B);
	/** The area-weighted centroid; false for a ring with no area. OutWinding is +1 counter-clockwise, -1 clockwise. */
	static bool RingCentroid(const TArray<FVector2D>& Ring, FVector2D& OutCentroid, float& OutWinding);

	/**
	 * Where on a roof a player stands for the audit: its centroid and the middle of every edge at least RoofSpotEdge
	 * long, RoofSpotInset in, keeping only the points inside the ring.
	 */
	static void ComputeRoofSpotPoints(const TArray<FVector2D>& Ring, const FGrappleAuditSettings& Settings, TArray<FVector2D>& OutPoints);

	// --- Neighbours and chains -------------------------------------------------------------------

	/**
	 * The closest points of roofs A and B see each other over no third building standing as high as the lower of the
	 * two, less a metre (generate_city.GrappleReach.across).
	 */
	static bool FacesAcross(const TArray<FGrappleReachRoof>& Roofs, int32 A, int32 B, const FGrappleAuditSettings& Settings);

	/**
	 * Per roof: its neighbours across a street or gap (TouchingGap < gap <= NeighbourGap and FacesAcross) and the
	 * touching roofs within WalkableRise, which are walked or mantled onto.
	 */
	static void FindNeighbours(const TArray<FGrappleReachRoof>& Roofs, const FGrappleAuditSettings& Settings,
		TArray<TArray<int32>>& OutNeighbours, TArray<TArray<int32>>& OutWalkable);

	/**
	 * Some spot in FromSpots is within the grapple's range (less 100 cm) of the top of roof To's walls: a pair the
	 * grapple could connect at all.
	 */
	static bool IsPairInRange(const FGrappleReachRules& Rules, const FGrappleAuditSettings& Settings,
		const TArray<FVector>& FromSpots, const FGrappleReachRoof& To);

	/**
	 * Hops from roof From to roof To by grapples and walks: Walkable per roof, and ReachedFrom[R] the roofs some spot
	 * of which reaches an anchor on R. -1 when there is no chain. OutVisited, when given, is every roof reached.
	 */
	static int32 FindChainHops(int32 From, int32 To, const TArray<TArray<int32>>& Walkable,
		const TArray<TSet<int32>>& ReachedFrom, TSet<int32>* OutVisited = nullptr);

	// --- The street ------------------------------------------------------------------------------

	/** What the floor under a sidewalk candidate is. */
	enum class EStand : uint8
	{
		/** Nothing under it: off the map, stop backing off. */
		NoGround,
		/** A building or something high: back off towards the centre line. */
		Blocked,
		/** A sidewalk: stand here. */
		Stand,
	};

	/**
	 * Sidewalk spots every Step along every street centre line, both sides, at the sidewalk's middle or backed off
	 * towards the centre line until the floor is a pavement; one per half-step cell, inside Bounds. FloorAt answers
	 * what is under a point and where the floor is. Returns capsule centres.
	 */
	static void GatherSidewalkSpots(const TArray<FGrappleReachStreet>& Streets, const FBox2D& Bounds, float Step,
		const FGrappleReachRules& Rules, const FGrappleAuditSettings& Settings,
		TFunctionRef<EStand(const FVector2D& Point, FVector& OutFloor)> FloorAt, TArray<FVector>& OutCentres);

	// --- Roofs the street cannot reach -----------------------------------------------------------

	/**
	 * Which roofs have no anchor reachable from any of StreetSpots, and why: one EGrappleRoofReach per roof. Every
	 * street spot within range of a roof's anchors is tried until one reaches.
	 */
	static void ClassifyRoofsFromStreet(const FGrappleReachRules& Rules, const FGrappleLensModel& Lens,
		const FGrappleReachOracle& Oracle, const FGrappleAuditSettings& Settings, const TArray<FGrappleReachRoof>& Roofs,
		const TArray<FGrappleReachAnchor>& Anchors, const TArray<FVector>& StreetSpots, TArray<EGrappleRoofReach>& OutReach);
};

/**
 * Anchors bucketed by a square cell, so a query only looks at the cells round a point. Pure: holds indices into
 * the caller's anchor array.
 */
struct HAWKEYE_API FGrappleAnchorGrid
{
	void Build(const TArray<FGrappleReachAnchor>& Anchors, float InCellSize);

	/** Indices of the anchors in the cells within Radius of Location (a superset of those within Radius). */
	void Gather(const FVector& Location, float Radius, TArray<int32>& OutIndices) const;

	float GetCellSize() const { return CellSize; }

private:
	FIntPoint CellOf(const FVector& Location) const;

	float CellSize = 2500.f;
	TMap<FIntPoint, TArray<int32>> Cells;
};
