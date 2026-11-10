#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/**
 * Whether a noise reaches a thug through the building (claude-docs/gameplay-semantics.md, "Interiors").
 * The engine's hearing sense is a sphere, HearingRange x loudness, through anything; this is the walls:
 *
 *  - a clear line from the noise to his head, or one OverheadRise above both ends (over a balustrade, a
 *    counter, a parked car), and he hears it within the range;
 *  - otherwise the sound goes the way a person would walk: he hears it when the navmesh path from the
 *    noise to him is within the range, and within ClosedDoorFactor of it when that path goes through a
 *    closed door;
 *  - no path at all (another floor with no stair, a sealed room): he does not.
 *
 * Walls, floors and door leaves count (world static and world dynamic); pawns and bodies do not.
 */
namespace HawkeyeThugHearing
{
	/** How far above both ends the second line runs, cm. Under an interior ceiling from standing height. */
	static constexpr float OverheadRise = 150.f;

	/** A closed door on the way cuts the range to this share of it. */
	static constexpr float ClosedDoorFactor = 0.25f;

	/** A path passing this close to a closed door's threshold (flat) goes through it, cm. */
	static constexpr float DoorPassDistance = 80.f;

	struct FHeardNoise
	{
		bool bHeard = false;
		/** Straight through the air, no path needed. */
		bool bDirect = false;
		/** The walking path's length when the air was blocked, cm; below 0 when there was none. */
		float PathLength = -1.f;
		int32 ClosedDoors = 0;
	};

	/** Whether a noise at Noise with a hearing range of Range (HearingRange x loudness) reaches Ear. */
	HAWKEYE_API FHeardNoise Hear(UWorld* World, const FVector& Noise, const FVector& Ear, float Range,
		const TArray<const AActor*>& Ignore);

	/** True when nothing solid (world static or dynamic) is on the line from A to B. */
	HAWKEYE_API bool IsAirClear(UWorld* World, const FVector& A, const FVector& B, const TArray<const AActor*>& Ignore);

	/** The closed doors a polyline passes through. */
	HAWKEYE_API int32 CountClosedDoorsOnPath(UWorld* World, const TArray<FVector>& Points);
}
