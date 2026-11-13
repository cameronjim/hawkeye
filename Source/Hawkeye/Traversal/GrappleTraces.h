#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/EngineTypes.h"

class AActor;
class UWorld;

/**
 * The grapple's world queries, in one place: the sight line to an anchor, the actors a zip may pass through at
 * either end, and the zip's capsule sweep. UGrappleComponent (the game) and FGrappleReachWorld's oracle (the
 * traversal audit, the editor's debugger) both call these, each with its own ignore list in Params: the
 * component ignores its owner and the anchor actor; the oracle ignores the chapter's see-through actors and the
 * spawned anchor. Nothing here decides a rule's numbers; callers pass them in.
 */
namespace HawkeyeGrappleTraces
{
	/**
	 * True when nothing on ECC_Visibility blocks the line from Lens to Marker short of SightTolerance (an anchor
	 * sits on a parapet; a hit on the stone right under it still means it is in view). OutHit holds the blocker.
	 */
	HAWKEYE_API bool HasLineOfSight(const UWorld& World, const FVector& Lens, const FVector& Marker, float SightTolerance,
		const FCollisionQueryParams& Params, FHitResult* OutHit = nullptr);

	/**
	 * The anchor's supports, which never block its zip: the static actor under its landing point (a trace from 50 cm
	 * above down ProbeDepth) and every static actor within SupportRadius of the anchor's origin.
	 */
	HAWKEYE_API void FindAnchorSupports(const UWorld& World, const FVector& Landing, const FVector& AnchorOrigin,
		float ProbeDepth, float SupportRadius, const FCollisionQueryParams& Params, TArray<AActor*>& OutSupports);

	/**
	 * The start's supports, ignored until the capsule is clear of them: what she stands on (a trace down her
	 * half-height plus 50 cm) and every static actor within IgnoreRadius of Start.
	 */
	HAWKEYE_API void FindStartSupports(const UWorld& World, const FVector& Start, float CapsuleHalfHeight,
		float IgnoreRadius, const FCollisionQueryParams& Params, TArray<AActor*>& OutSupports);

	/**
	 * The zip's two capsule sweeps: Launch to ReleasePoint ignoring the start's supports, then on to End. Both
	 * ignore the anchor's supports and whatever Params already ignores. OutHit holds the blocker when it fails.
	 */
	HAWKEYE_API bool IsZipClear(const UWorld& World, const FVector& Launch, const FVector& ReleasePoint, const FVector& End,
		const FCollisionShape& Capsule, ECollisionChannel ObjectType, const FCollisionResponseParams& Responses,
		const FCollisionQueryParams& Params, const TArray<AActor*>& AnchorSupports, const TArray<AActor*>& StartSupports,
		FHitResult* OutHit = nullptr);
}
