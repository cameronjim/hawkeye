#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CityMapData.generated.h"

/** One closed outline on the world map (a building footprint or the park), world XY in cm. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCityMapPolygon
{
	GENERATED_BODY()

	/** The ring, not repeated at the end, simplified by the generator to a few points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FVector2D> Points;

	/** The OpenStreetMap id, e.g. W248142338. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString OsmId;

	/** Building height in metres (0 for the park). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	float HeightM = 0.f;
};

/** One street's centre line on the world map, world XY in cm. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FCityMapStreet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FVector2D> Points;

	/** The carriageway's width, m (25 for avenues, 12 for streets by default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	float WidthM = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString Name;
};

/**
 * The generated district as a flat map: building footprints, the park, street centre lines and the
 * bounds, all in world XY centimetres (X east, Y south, so north is -Y). Tools/Editor/generate_city.py
 * writes it (DA_EastVillage_Map) from the same OpenStreetMap records the meshes come from; the world
 * map (UHawkeyeMapWidget) draws it. Nothing here is placed by hand.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UCityMapData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The district's ground rectangle's bounding box, world XY cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FVector2D BoundsMin = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FVector2D BoundsMax = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FCityMapPolygon> Footprints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FCityMapPolygon> Parks;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FCityMapStreet> Streets;

	/** Hash of the generator inputs that produced the arrays; a rerun with the same hash saves nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString SourceHash;

	/** True when the bounds have area and there is at least one footprint. */
	UFUNCTION(BlueprintPure, Category = "Map")
	bool IsUsable() const;

	/** The footprint with OsmId, or null. */
	const FCityMapPolygon* FindFootprint(const FString& OsmId) const;

	/** Where the world map data lives for the one district. */
	static const TCHAR* DefaultPath;
};
