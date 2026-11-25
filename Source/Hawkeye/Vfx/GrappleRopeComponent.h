#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GrappleRopeComponent.generated.h"

/**
 * The grapple rope as the player sees it: a chain of thin cylinders (the engine's own, one instance per piece) laid along the
 * points the rope's state machine (FGrappleRope) gives it, in world space. Lit by the scene like any mesh, not emissive:
 * M_GrappleRope is braided nylon grey with a slight sheen and at most a very faint rim (Tools/Editor/create_grapple_rope.py).
 * Without that material the engine's basic shape material, tinted the same grey, stands in.
 *
 * World-absolute: the component sits at the origin and every instance is a world transform, so nothing it is attached to
 * moves it. A UGrappleComponent makes one per rope it has up (a chain's old rope is still whipping away as the new one pays out).
 */
UCLASS(ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UGrappleRopeComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UGrappleRopeComponent();

	/** The grey the rope is drawn in when M_GrappleRope is not there (and its Color parameter otherwise), linear. */
	static const FLinearColor RopeColor;

	/** Where M_GrappleRope lives. */
	static const TCHAR* const MaterialPath;

	/**
	 * Lays the pieces along Points (at least two), ThicknessCm wide, and shows the rope. Adds or removes instances to match.
	 * Cheap enough for every frame: one batch transform update.
	 */
	void SetPoints(const TArray<FVector>& Points, float ThicknessCm);

	/** Takes the rope off the screen (the instances stay, so the next SetPoints costs nothing). */
	void Hide();

	/** How many pieces are drawn now (tests). */
	int32 GetPieceCount() const
	{
		return bShown ? PieceCount : 0;
	}

	/** The mesh and material, loaded the first time they are needed. False when the engine cylinder is missing. */
	bool EnsureLook();

private:
	bool bLookTried = false;
	bool bShown = false;
	int32 PieceCount = 0;

	TArray<FTransform> Scratch;
};
