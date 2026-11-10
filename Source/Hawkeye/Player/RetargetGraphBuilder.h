#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RetargetGraphBuilder.generated.h"

class UAnimBlueprint;
class UIKRetargeter;
class USkeleton;

/**
 * Builds a body look's AnimBlueprint headless (Tools/Editor/create_kate_look.py calls it): one Retarget Pose
 * From Mesh node, source the parent skeletal mesh component (the hidden driver), with the given IK Retargeter,
 * into the output pose. Epic's Game Animation Sample shows its retargeted characters the same way. Editor
 * builds only; in a cooked game the function logs an error and returns null.
 */
UCLASS()
class HAWKEYE_API UHawkeyeRetargetGraphBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or rebuilds in place) the AnimBlueprint at PackageName for Skeleton (the look mesh's) and compiles
	 * it. Returns it dirty and unsaved, or null when it did not build or compile (the reasons are logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	static UAnimBlueprint* BuildRetargetFromParent(
		const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter);
};
