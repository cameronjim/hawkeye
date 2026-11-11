#pragma once

#include "CoreMinimal.h"

/**
 * The grapple's tuning, one place for the numbers (claude-docs/gameplay-semantics.md, "Grapple arrow").
 * UGrappleComponent's UPROPERTY defaults and FGrappleReachRules both start from these, so the traversal
 * debugger, the audit commandlet and the component never drift apart. BP_Kate may still override the
 * component's values; the tools read them back from the pawn's class default (FGrappleReachWorld).
 */
namespace GrappleReachDefaults
{
	inline constexpr float Range = 2500.f;
	inline constexpr float MinRange = 300.f;
	inline constexpr float ConeDegrees = 30.f;
	inline constexpr float PickTieDegrees = 0.5f;
	inline constexpr float ScreenEdgeMarginDegrees = 2.f;
	inline constexpr float ViewFieldOfView = 90.f;
	inline constexpr float ViewAspectRatio = 16.f / 9.f;
	inline constexpr float SightTolerance = 60.f;
	inline constexpr float SupportRadius = 150.f;
	inline constexpr float ZipLaunchHeight = 120.f;
	inline constexpr float ZipStartIgnoreRadius = 250.f;
	/** Kate's capsule when no character is at hand (the CMC sample's 34 x 88); the tools read the real one. */
	inline constexpr float CapsuleRadius = 34.f;
	inline constexpr float CapsuleHalfHeight = 88.f;
	/** A standing spot puts the capsule this far above the floor, as a teleport does, so the floor is found. */
	inline constexpr float StandClearance = 2.f;
}

/**
 * The rules one grapple check needs, as plain numbers: range, cone, screen, sight and the zip's
 * allowances. UGrappleComponent::MakeReachRules fills one from a live or default component.
 */
struct HAWKEYE_API FGrappleReachRules
{
	float Range = GrappleReachDefaults::Range;
	float MinRange = GrappleReachDefaults::MinRange;
	float ConeDegrees = GrappleReachDefaults::ConeDegrees;
	float PickTieDegrees = GrappleReachDefaults::PickTieDegrees;
	float ScreenEdgeMarginDegrees = GrappleReachDefaults::ScreenEdgeMarginDegrees;
	float ViewFieldOfView = GrappleReachDefaults::ViewFieldOfView;
	float ViewAspectRatio = GrappleReachDefaults::ViewAspectRatio;
	float SightTolerance = GrappleReachDefaults::SightTolerance;
	float SupportRadius = GrappleReachDefaults::SupportRadius;
	float ZipLaunchHeight = GrappleReachDefaults::ZipLaunchHeight;
	float ZipStartIgnoreRadius = GrappleReachDefaults::ZipStartIgnoreRadius;
	float CapsuleRadius = GrappleReachDefaults::CapsuleRadius;
	float CapsuleHalfHeight = GrappleReachDefaults::CapsuleHalfHeight;

	/** How far from the start the capsule centre must be before the start supports count again. */
	float GetStartReleaseDistance() const { return ZipStartIgnoreRadius + CapsuleRadius; }
};

/**
 * The hip camera as the audit predicts it (HawkeyeGrappleView::PredictLens in the tests): the pivot at the
 * capsule centre plus PivotOffset (the boom's relative location), the pitch clamped, and per pitch the arm's
 * length, its socket offset and the pivot's world-up lift. Targets is optional: without it the lens is the
 * pivot itself, which is still a usable "eye" for the editor's reach-from-here.
 */
struct HAWKEYE_API FGrappleLensModel
{
	FVector PivotOffset = FVector::ZeroVector;
	float PitchMin = -89.f;
	float PitchMax = 89.f;

	/** Pitch in degrees, up positive, to the arm length, socket offset (arm space) and pivot lift. */
	TFunction<void(float PitchDegrees, float& OutArmLength, FVector& OutSocketOffset, float& OutPivotLift)> Targets;

	float ClampPitch(float PitchDegrees) const { return FMath::Clamp(PitchDegrees, PitchMin, PitchMax); }
};

/** What stopped a sight line or a zip: enough to say "view blocked by its own building" in a report. */
struct HAWKEYE_API FGrappleReachBlocker
{
	/** The actor's name, empty for nothing. */
	FString Name;
	/** "a building" for a CityBuilding, else the class name without _C; "nothing" when unknown. */
	FString Kind;
	/** The building's osm id when it is one. */
	FString OsmId;
};

/** One grapple anchor as the reach rules see it: no actor, just where it is. */
struct HAWKEYE_API FGrappleReachAnchor
{
	/** The actor origin (the base of the fitting): supports are looked for round it. */
	FVector Origin = FVector::ZeroVector;
	/** What the arrow flies at and the range is measured to (AGrappleAnchor::GetMarkerLocation). */
	FVector Marker = FVector::ZeroVector;
	/** Where her feet land (AGrappleAnchor::GetLandingLocation). */
	FVector Landing = FVector::ZeroVector;
	/** Index into the roofs, INDEX_NONE when it stands on none. */
	int32 Roof = INDEX_NONE;
	bool bEnabled = true;
	/** The generator's stable number (City_Anchor_<Index>), or the order found. */
	int32 Index = 0;
	/** City_Anchor_<Index> or the actor's name: what the reports print. */
	FString Name;
};

/** One building's roof: its footprint, height and identity. */
struct HAWKEYE_API FGrappleReachRoof
{
	FString OsmId;
	/** The building actor's name, for the reports. */
	FString ActorName;
	float HeightM = 0.f;
	/** World XY cm, not repeated at the end. */
	TArray<FVector2D> Ring;
	FBox2D Box = FBox2D(ForceInit);

	/** Recomputes Box from Ring. */
	void UpdateBox();
};

/** A standing spot: the capsule centre, and the roof it is on (INDEX_NONE on the street). */
struct HAWKEYE_API FGrappleReachSpot
{
	FVector Centre = FVector::ZeroVector;
	int32 Roof = INDEX_NONE;
};

/** One street centre line: points in world XY cm and the carriageway's width (UCityMapStreet). */
struct HAWKEYE_API FGrappleReachStreet
{
	TArray<FVector2D> Points;
	float WidthM = 12.f;
};

/**
 * The audit's sampling and the district's shared dimensions (Hawkeye.Grapple.Audit's constants, which mirror
 * generate_city / _reach.py: ANCHOR_MIN_HEIGHT_M, REACH_ROOF_SPOT_*, REACH_NEIGHBOUR_GAP, PARAPET_HEIGHT).
 */
struct HAWKEYE_API FGrappleAuditSettings
{
	/** Only roofs taller than this get anchors (generate_city.ANCHOR_MIN_HEIGHT_M), m. */
	float AnchoredHeightM = 8.f;
	/** cm between sidewalk spots in the first pass. */
	float StreetStep = 1000.f;
	/** cm between sidewalk spots in the second pass, for a roof the first misses. */
	float FineStreetStep = 250.f;
	/** cm out from the carriageway's edge (the middle of a 4 m sidewalk). */
	float SidewalkMiddle = 200.f;
	/** cm towards the centre line a spot may move to be off the roofs, in SidewalkBackOffStep steps. */
	float SidewalkBackOff = 800.f;
	float SidewalkBackOffStep = 100.f;
	/** A sidewalk spot's floor must be lower than this, cm (it is not a roof or a canopy). */
	float StreetMaxFloorZ = 300.f;
	/** The middle of every parapet this long is a roof spot, cm. */
	float RoofSpotEdge = 300.f;
	/** How far in from the parapet a roof spot stands, cm. */
	float RoofSpotInset = 100.f;
	/** Roofs this close across a street or gap are neighbours, cm. */
	float NeighbourGap = 2500.f;
	/** Closer than this the roofs share a wall, cm. */
	float TouchingGap = 100.f;
	/** A touching roof within this rise is walked or mantled onto, cm. */
	float WalkableRise = 250.f;
	/** Roofs this tall carry a parapet (generate_city.PARAPET_MIN_HEIGHT_M), m, of ParapetHeight cm. */
	float ParapetMinHeightM = 6.f;
	float ParapetHeight = 90.f;
	/** A fire escape belongs to the roof whose outline is nearest within this, cm. */
	float EscapeRoofGap = 250.f;

	/** The top of a roof's walls: its height plus the parapet where it has one, cm. */
	float RoofTopZ(float HeightM) const { return HeightM * 100.f + (HeightM >= ParapetMinHeightM ? ParapetHeight : 0.f); }
	bool IsAnchored(float HeightM) const { return HeightM > AnchoredHeightM; }
};

/** Why an anchor can or cannot be grappled from a standing point. The checks run in this order. */
enum class EGrappleReachVerdict : uint8
{
	/** Range, view, sight and a clear zip: a press would fire at it once it is the one nearest the middle. */
	Reachable,
	/** AGrappleAnchor::bEnabled is off. */
	Disabled,
	/** She stands on the anchor's own roof: the audit does not count those. */
	SameRoof,
	/** Inside MinRange of the capsule centre. */
	TooClose,
	/** Past Range. */
	TooFar,
	/** Even with the camera turned to it, it is outside the cone or off the screen: the pitch clamp. */
	TooSteep,
	/** The camera's line to the marker hits something short of SightTolerance. */
	SightBlocked,
	/** The capsule's sweep along the zip line hits something that is not a support. */
	ZipBlocked,
};

HAWKEYE_API const TCHAR* LexToString(EGrappleReachVerdict Verdict);

/** Where a roof stands with the street in an audit. */
enum class EGrappleRoofReach : uint8
{
	/** Some sidewalk spot reaches one of its anchors. */
	Reached,
	/** Not taller than the anchored height (generate_city.ANCHOR_MIN_HEIGHT_M): no anchors are placed there. */
	BelowAnchorHeight,
	/** Tall enough, but no anchor landed on it. */
	NoAnchors,
	/** No sidewalk spot is within the grapple's range of any of its anchors (too tall, or inside its block). */
	NoStreetInRange,
	/** Street-facing (a sidewalk spot is in range) but no spot reaches any anchor: the audit's failure. */
	Unreached,
};

HAWKEYE_API const TCHAR* LexToString(EGrappleRoofReach Reach);

/** One anchor checked from one standing spot. */
struct HAWKEYE_API FGrappleReachEvaluation
{
	int32 Anchor = INDEX_NONE;
	EGrappleReachVerdict Verdict = EGrappleReachVerdict::TooFar;
	/** Capsule centre to marker, cm. */
	float Distance = 0.f;
	/** The marker's angle off the predicted view's forward, degrees. */
	float AngleDegrees = 0.f;
	/** Passed the cone, the screen and the sight line. */
	bool bSeen = false;
	/** The zip line is clear (checked even when not seen when the caller asks for both). */
	bool bClear = false;
	FVector Lens = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	/** What blocked the sight or the zip, when the oracle says. */
	FGrappleReachBlocker Blocker;
};

/**
 * What the pure rules ask the world. The runtime fills it with traces (FGrappleReachWorld); the tests
 * fill it with fakes. HasLineOfSight and IsZipClear are required; ProbeLens may be empty.
 */
struct HAWKEYE_API FGrappleReachOracle
{
	/** The camera at Lens sees Anchor's marker, give or take the rules' SightTolerance. */
	TFunction<bool(const FVector& Lens, const FGrappleReachAnchor& Anchor, FGrappleReachBlocker* OutBlocker)> HasLineOfSight;

	/** A zip from a capsule centre at From (on the ground when bFromGround) reaches Anchor's landing. */
	TFunction<bool(const FVector& From, const FGrappleReachAnchor& Anchor, bool bFromGround, FGrappleReachBlocker* OutBlocker)> IsZipClear;

	/** The spring arm's probe: where the lens ends up going from Origin towards Desired. Returns Desired when clear. */
	TFunction<FVector(const FVector& Origin, const FVector& Desired)> ProbeLens;
};
