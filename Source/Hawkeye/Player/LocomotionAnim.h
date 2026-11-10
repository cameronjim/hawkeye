#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeletalMeshComponent;

/**
 * Idle/walk switching without an AnimBP. The mannequin pack's ThirdPerson_AnimBP does not
 * compile in a headless editor, so every character here drives the sequences directly; this is
 * the one copy of that, shared by the thugs and by the player's third-person body.
 */
namespace HawkeyeLocomotion
{
	/**
	 * Plays Wanted through Mesh and writes it to Current, unless it is already playing.
	 * Returns true when the animation changed. A component with no mesh asset still updates
	 * Current: a greybox character tracks what it would be playing.
	 */
	HAWKEYE_API bool PlayIfChanged(
		USkeletalMeshComponent* Mesh, UAnimSequence* Wanted, TObjectPtr<UAnimSequence>& Current);

	/**
	 * Which way the character the mesh is drawing actually faces, in world space.
	 *
	 * The mannequin is authored facing its own +Y, so every character here mounts it at a -90
	 * degree yaw; the component's right vector is therefore the direction the body looks. Taking
	 * it from the component rather than from the actor is the point: it catches a mesh mounted at
	 * the wrong yaw, which is what "the guards walk backwards" looked like.
	 */
	HAWKEYE_API FVector GetMeshFacing(const USkeletalMeshComponent* Mesh);

	/**
	 * cos(angle) between the way a pawn is moving and the way its mesh faces: 1 walking forwards,
	 * -1 moonwalking. Returns 1 when the pawn is not moving, because a standing character cannot
	 * be facing the wrong way along a direction it does not have.
	 */
	HAWKEYE_API float GetFacingAlongVelocity(const USkeletalMeshComponent* Mesh, const FVector& Velocity);
}
