#pragma once

#include "CoreMinimal.h"

class ACharacter;
class UActorComponent;

/**
 * Calls into the Game Animation Sample's traversal, which is Blueprint only, by reflection.
 *
 * The sample puts it in AC_TraversalLogic, a component on SandboxCharacter_CMC (so on BP_Kate):
 *   TryTraversalAction(S_TraversalCheckInputs) -> TraversalCheckFailed, MontageSelectionFailed
 * sweeps forward on the Traversable trace channel (TraceTypeQuery3, which is our first custom
 * channel), casts what it hits to LevelBlock_Traversable, asks it for the front and back ledge
 * (GetLedgeTransforms reads its Ledge_1..4 spline components; a spline's up vector is the ledge's
 * outward normal), picks a montage from CHT_TraversalMontages_CMC and plays it with motion
 * warping in Flying mode. DoingTraversalAction is true until the montage blends out.
 * SandboxCharacter_CMC::GetTraversalCheckInputs builds the sweep from the current speed; the
 * sample's jump input called the two in a row, and we do the same from C++.
 */
namespace HawkeyeGaspTraversal
{
	/** What TryTraversalAction answered. */
	struct FResult
	{
		/** False when the functions or their parameters were not found; nothing was called. */
		bool bCalled = false;
		/** No LevelBlock_Traversable ahead, or no room: the sample did nothing. */
		bool bCheckFailed = true;
		/** Found a ledge but no montage fits the numbers. */
		bool bMontageFailed = true;

		bool Started() const { return bCalled && !bCheckFailed && !bMontageFailed; }
	};

	/** The owner's AC_TraversalLogic (any component with a TryTraversalAction function), or null. */
	HAWKEYE_API UActorComponent* FindTraversalLogic(const AActor* Owner);

	/** True while the sample's traversal montage runs (its DoingTraversalAction flag). */
	HAWKEYE_API bool IsDoingTraversal(const UActorComponent* TraversalLogic);

	/** GetTraversalCheckInputs on Character, fed straight into TryTraversalAction. */
	HAWKEYE_API FResult TryTraversal(ACharacter* Character, UActorComponent* TraversalLogic);

	/**
	 * True when Character's class has GetTraversalCheckInputs and TraversalLogic's has
	 * TryTraversalAction with an S_TraversalCheckInputs input and the two failure outputs.
	 * OutReport lists what was found, for a test or the log.
	 */
	HAWKEYE_API bool HasTraversalApi(const UClass* CharacterClass, const UClass* TraversalLogicClass, FString& OutReport);
}
