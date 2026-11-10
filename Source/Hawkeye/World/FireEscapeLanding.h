#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/CityLedgeData.h"
#include "FireEscapeLanding.generated.h"

class UBoxComponent;

/**
 * The collision of one fire-escape landing (claude-docs/gameplay-semantics.md, traversal): the
 * slab and the three rails as hidden boxes that block the character and traces but not the
 * camera and never touch the navmesh. ACityLedgeSpawner spawns one per FCityFireEscapeRecord
 * (City_FireEscape_<osm id>_<floor>) and draws every landing's visible bars itself, as instances
 * of two meshes, so a thousand landings cost two draw calls.
 *
 * The geometry is a pure function of the record so tests can check it without a world.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AFireEscapeLanding : public AActor
{
	GENERATED_BODY()

public:
	AFireEscapeLanding();

	/** Sizes and places the boxes for Record (the actor sits at Record.Transform). */
	void ApplyRecord(const FCityFireEscapeRecord& Record);

	UFUNCTION(BlueprintPure, Category = "City")
	const FCityFireEscapeRecord& GetRecord() const { return Record; }

	/**
	 * The collision boxes in the landing's own frame (origin on the facade line at the slab top,
	 * +X along the facade, +Y out): slab, outer rail, -X end rail, +X end rail.
	 */
	static void BuildCollisionBoxes(const FCityFireEscapeRecord& Record, TArray<FBox>& OutLocalBoxes);

	/**
	 * The visible parts in world space as transforms of the engine's 100 cm basic shapes: cubes
	 * (slab, top and middle rails, posts) and cylinders (the ladder's rails and rungs down to the
	 * landing below, at the LadderSide end).
	 */
	static void BuildParts(const FCityFireEscapeRecord& Record, TArray<FTransform>& OutCubes, TArray<FTransform>& OutCylinders);

	/**
	 * Where the traversable ledge block (BP_TraversableBlock, a 1 m cube with its pivot at a bottom
	 * corner and Ledge_1 along local y = 0 facing -Y) goes: along the outer rail, its Ledge_1 on
	 * the rail's outer top edge facing out of the facade, down to the slab bottom.
	 */
	static FTransform ComputeLedgeTransform(const FCityFireEscapeRecord& Record);

	/** The outer top edge of the rail in world space, its two ends. */
	static void ComputeOuterRailTop(const FCityFireEscapeRecord& Record, FVector& OutA, FVector& OutB);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "City")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "City")
	TArray<TObjectPtr<UBoxComponent>> Boxes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "City")
	FCityFireEscapeRecord Record;
};
