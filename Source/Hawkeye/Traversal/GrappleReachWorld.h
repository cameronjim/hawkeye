#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Traversal/GrappleReachQuery.h"
#include "World/CityLedgeData.h"

class AActor;
class ACityLedgeSpawner;
class APawn;
class UCityMapData;
class UWorld;

/**
 * Everything about the player a reach check needs from the world: the grapple's rules, the hip camera, and how
 * her capsule collides. Read off a pawn (its class default is enough), so the tools check BP_Kate's tuning
 * without spawning her.
 */
struct HAWKEYE_API FGrappleReachProfile
{
	FGrappleReachRules Rules;
	FGrappleLensModel Lens;
	/** The capsule's object type and responses: what the zip sweep collides with (UGrappleComponent::IsZipClear). */
	TEnumAsByte<ECollisionChannel> CapsuleObjectType = ECC_Pawn;
	FCollisionResponseContainer CapsuleResponses;
	/** The spring arm's probe (USpringArmComponent::bDoCollisionTest, ProbeChannel, ProbeSize). */
	bool bProbeLens = true;
	TEnumAsByte<ECollisionChannel> ProbeChannel = ECC_Camera;
	float ProbeSize = 12.f;
	/** Where it was read from, for the reports ("BP_Kate_C"). */
	FString Source;
};

/**
 * The district as the reach rules see it, built from the generated data rather than spawned actors so it works in
 * an editor world: roofs from the CityBuilding actors and DA_EastVillage_Map's footprints, anchors, ledges and fire
 * escapes from DA_EastVillage_CityProps, streets and bounds from the map.
 */
struct HAWKEYE_API FGrappleReachScene
{
	TArray<FGrappleReachRoof> Roofs;
	/** The building actor of each roof, same order. */
	TArray<TWeakObjectPtr<AActor>> RoofActors;
	TMap<FString, int32> RoofByOsm;

	TArray<FGrappleReachAnchor> Anchors;
	/** The spawned AGrappleAnchor of each anchor, same order, when the spawner has them out (sight ignores it). */
	TArray<TWeakObjectPtr<AActor>> AnchorActors;

	TArray<FCityLedgeRecord> Ledges;
	TArray<FCityFireEscapeRecord> FireEscapes;

	TArray<FGrappleReachStreet> Streets;
	FBox2D Bounds = FBox2D(ForceInit);

	/** Objective id to the osm id of the roof its volume stands on ("objective:<id>" and "osm:<id>" tags). */
	TMap<FString, FString> ObjectiveRoofs;

	/** The roof carrying OsmId; INDEX_NONE for none. */
	int32 FindRoofByOsm(const FString& OsmId) const;
};

/**
 * The world side of FGrappleReachQuery: profiles from pawns, scenes from the city data, and an oracle whose sight
 * lines, zip sweeps and lens probe are the same traces UGrappleComponent runs (same channels, same supports, same
 * allowances), with no character in the world. Game thread only.
 */
struct HAWKEYE_API FGrappleReachWorld
{
	/** The tag every generated building carries. */
	static const FName BuildingTag;

	/** The profile of Pawn (a live pawn or a class default). Falls back to the rules' defaults for anything missing. */
	static FGrappleReachProfile MakeProfile(const APawn* Pawn);

	/**
	 * The profile of the player: PawnClass when it is a Hawkeye character, else the map's game mode's default pawn,
	 * else AHawkeyeCharacter's class default.
	 */
	static FGrappleReachProfile MakePlayerProfile(const UWorld* World, TSubclassOf<APawn> PawnClass = nullptr);

	/** The pawn class the map's game mode spawns for the player, or null. */
	static TSubclassOf<APawn> FindPlayerPawnClass(const UWorld* World);

	/** The map's city props spawner, or null. */
	static ACityLedgeSpawner* FindSpawner(const UWorld* World);

	/** "<id>" from an "osm:<id>" tag, or empty. */
	static FString OsmOf(const AActor* Actor);

	/** Name, kind ("a building" or the class name) and osm id of what blocked a line. */
	static FGrappleReachBlocker DescribeBlocker(const AActor* Actor);

	/**
	 * Builds the scene from World's buildings, Map and Props. MarkerHeight is the anchor class's
	 * (AGrappleAnchor::MarkerHeight). False with OutError when the inputs are missing.
	 */
	static bool BuildScene(UWorld* World, const UCityMapData* Map, const UCityLedgeData* Props, float MarkerHeight,
		FGrappleReachScene& OutScene, FString& OutError);

	/** The first building under Point within Depth (Up above it), by a multi trace so clutter on top does not hide it. */
	static AActor* FindBuildingUnder(const UWorld* World, const FVector& Point, float Up, float Depth, FVector* OutTop = nullptr);

	/** What the floor under a sidewalk candidate is (Hawkeye.Grapple.Audit's rule: no building, lower than MaxFloorZ). */
	static FGrappleReachQuery::EStand FindSidewalkFloor(const UWorld* World, const FVector2D& Point, float MaxFloorZ, FVector& OutFloor);

	/**
	 * The oracle over World: sight on ECC_Visibility with the rules' SightTolerance, the capsule swept along the zip
	 * with the anchor's and the start's supports ignored as UGrappleComponent does, the lens probed on the arm's
	 * channel. Scene, when given, lets the sight line ignore the spawned anchor actor as the component does.
	 * The returned functions hold World weakly; they answer "blocked" once it is gone.
	 */
	static FGrappleReachOracle MakeOracle(UWorld* World, const FGrappleReachProfile& Profile, const FGrappleReachScene* Scene = nullptr);
};
