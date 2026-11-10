#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CityLedgeData.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** One hidden traversable ledge block along a roof edge: BP_TraversableBlock at Transform. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCityLedgeRecord
{
	GENERATED_BODY()

	/** The block's bottom corner, turned so local X runs along the edge, scaled into a slab. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FTransform Transform;

	/** Which edge of the footprint (after ring cleaning) this is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	int32 EdgeIndex = 0;

	/** The building's OpenStreetMap id, e.g. way/123456. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FString OsmId;
};

/** One grapple anchor on a roof: BP_GrappleAnchor at Transform, its landing point at LandingOffset. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCityAnchorRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FTransform Transform;

	/** The LandingPoint component's relative location. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FVector LandingOffset = FVector(60.f, 0.f, 0.f);

	/** The generator's stable anchor number (City_Anchor_<Index>). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	int32 Index = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FString OsmId;
};

/**
 * One fire-escape landing on a tenement's street facade (claude-docs/gameplay-semantics.md,
 * traversal): AFireEscapeLanding at Transform plus a traversable ledge on its outer top rail.
 * Transform's origin is on the facade line at the middle of the landing, at the slab top; local
 * +X runs along the facade and +Y points out of it. AFireEscapeLanding::BuildParts turns the
 * numbers into the slab, rails and ladder.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCityFireEscapeRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FTransform Transform;

	/** Slab length along the facade, depth out of it, thickness, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FVector SlabSize = FVector(240.f, 90.f, 8.f);

	/** Gap between the facade and the slab, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	float FacadeGap = 5.f;

	/** Rail height above the slab top, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	float RailHeight = 90.f;

	/** Rail thickness (its collision is this thick; the visible bars are thinner), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	float RailThickness = 6.f;

	/** From this slab's top down to the slab top of the landing below; 0 for the lowest landing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	float LadderDrop = 0.f;

	/** Which end of the landing the ladder down is at: +1 the +X end, -1 the -X end (zig-zag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	float LadderSide = 1.f;

	/** 1 for the second floor (330 cm up), counting up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	int32 Floor = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FString OsmId;
};

/**
 * One kind of street or rooftop clutter (water towers, HVAC boxes, hydrants, parked cars...):
 * every instance of one mesh in one material, drawn by ACityLedgeSpawner as one hierarchical
 * instanced mesh. Tools/Editor/generate_city.py decides where they go (rules in its docstring).
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCityClutterGroup
{
	GENERATED_BODY()

	/** What this is, e.g. WaterTower, ParkedCar_Black; also the component's name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FName Kind;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TObjectPtr<UMaterialInterface> Material;

	/** Blocks pawns and physics (not the visibility, camera or traversable traces). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	bool bCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	bool bCastShadow = true;

	/** World transforms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TArray<FTransform> Instances;
};

/**
 * The generated district's many small actors, as data instead of saved actors: every roof-edge
 * traversable ledge, every grapple anchor, every fire-escape landing and the clutter. Tools/Editor/generate_city.py writes it
 * (DA_EastVillage_CityProps) and ACityLedgeSpawner spawns the actors at load. Saved as actors
 * they made the district map 72 MB.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UCityLedgeData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TArray<FCityLedgeRecord> Ledges;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TArray<FCityAnchorRecord> Anchors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TArray<FCityFireEscapeRecord> FireEscapes;

	/** Street and rooftop clutter, one instanced mesh per group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TArray<FCityClutterGroup> Clutter;

	/** Hash of the generator inputs that produced the arrays; a rerun with the same hash saves nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FString SourceHash;
};
