#pragma once

#include "CoreMinimal.h"
#include "BodyLook.generated.h"

class UAnimInstance;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * The mesh people see on a character whose animated mesh is a stand-in (Kate: the Game Animation
 * Sample's UEFN mannequin drives every system; Paragon's Sparrow is what is drawn). Everything is soft:
 * a clone without the look's content keeps the driver mesh visible and plays exactly as before.
 *
 * Swapping the look is a data change: another skeletal mesh, an AnimBP that retargets the driver's
 * pose onto it (Retarget Pose From Mesh, source = the parent component), its material slots and the
 * bones to hide. Tools/Editor/create_kate_look.py writes BP_Kate's.
 */
USTRUCT(BlueprintType)
struct FHawkeyeBodyLook
{
	GENERATED_BODY()

	/** The skeletal mesh drawn in place of the driver. None keeps the driver visible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Look")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Its AnimBP: a retarget of the parent (driver) component's pose onto Mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Look")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Material per slot of Mesh; an empty entry, or a slot past the end, keeps the mesh's own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Look")
	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;

	/** Bones hidden with everything under them (Sparrow's own bow and nocked arrow: Kate holds the game's bow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body Look")
	TArray<FName> HiddenBones;

	/** True when a mesh is named at all (whether or not it is on disk). */
	bool IsSet() const
	{
		return !Mesh.IsNull();
	}
};

/**
 * Applies an FHawkeyeBodyLook. The driver keeps animating, posing and colliding (every system reads it)
 * but is not drawn and casts no shadow; the look mesh is attached to it at the identity, inherits its
 * transform (scale, the roll's tumble), ticks after it and has no collision of its own.
 */
namespace HawkeyeBodyLook
{
	/** The name of the look's component on the actor. */
	HAWKEYE_API extern const FName ComponentName;

	/**
	 * The look's mesh and AnimBP class, loaded when their packages exist. False (and both null) when either
	 * is missing: a fresh clone without the Paragon content.
	 */
	HAWKEYE_API bool Resolve(const FHawkeyeBodyLook& Look, USkeletalMesh*& OutMesh, UClass*& OutAnimClass);

	/**
	 * Creates, registers and returns the look component under Driver, and hides the driver. Null (and the
	 * driver left as it was, with one log line) when the look does not resolve.
	 */
	HAWKEYE_API USkeletalMeshComponent* Apply(USkeletalMeshComponent& Driver, const FHawkeyeBodyLook& Look);
}
