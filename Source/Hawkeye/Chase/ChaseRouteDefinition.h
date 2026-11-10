#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Chase/ChaseTypes.h"
#include "ChaseRouteDefinition.generated.h"

class AThugCharacter;
class UNiagaraSystem;
class UStaticMesh;

/**
 * One chase: the thief's route and everything placed along it (claude-docs/gameplay-semantics.md, "Chases").
 * Planned from the district's OpenStreetMap records by Tools/Editor/create_chase.py, so every point sits on
 * a real roof or pavement and every grapple it expects has been checked against the reach model. A chapter
 * names one on UMissionDefinition::Chase; a later chapter reuses the machinery with another asset.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UChaseRouteDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The thief's route, first point where he starts running. At least two. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase")
	TArray<FChaseWaypoint> Waypoints;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase")
	FChaseTuning Tuning;

	/** Kate's feet when the chase opens; she faces along PlayerStartYaw at her sprint speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Start")
	FVector PlayerStart = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Start")
	float PlayerStartYaw = 0.f;

	/** Who runs. Thinking off, tagged ChaseThief and Thug (so the takedown takes him), never saved by SPUD. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Thief")
	TSoftClassPtr<AThugCharacter> ThiefClass;

	/** Objectives whose marker follows the thief while the chase runs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Objectives")
	TArray<FName> ThiefObjectives;

	/** Completed by the catch, after every waypoint objective still open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Objectives")
	FName CatchObjective;

	/** The HUD toast's heading when the chase is lost, before the checkpoint loads. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Objectives")
	FText LostHeading = NSLOCTEXT("Hawkeye", "ChaseLost", "[He got away]");

	// --- Lines: DT_DialogueSequences sequences, each played once a campaign. None plays nothing. ----------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Lines")
	FName FirstVaultSequence;

	/** The first time the lead goes past the tuning's FarLead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Lines")
	FName PullsAheadSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Lines")
	FName BotchSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Lines")
	FName DumpsterSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Lines")
	FName ClimbOutSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Lines")
	FName CorneredSequence;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Lines")
	FName CaughtSequence;

	// --- The beat: the botched grapple into the dumpster ---------------------------------------------------

	/** The route has a beat. Everything below is read only when it does. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	bool bHasBeat = false;

	/** The zone a zip must start in: a box, centre and half size (cm), turned by BeatZoneYaw. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	FVector BeatZoneCentre = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	FVector BeatZoneExtent = FVector(300.f, 300.f, 200.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	float BeatZoneYaw = 0.f;

	/** The anchor the botched zip is fired at (its marker point). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	FVector BeatAnchor = FVector::ZeroVector;

	/** The waypoint where the thief leaps the gap: the beat counts as passed once her progress is beyond it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	int32 BeatWaypoint = INDEX_NONE;

	/** The dumpster's origin (the middle of its base, on the ground) and yaw (its length along X). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	FVector DumpsterLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	float DumpsterYaw = 0.f;

	/** Her feet in the dumpster, in the paper. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	FVector BeatLanding = FVector::ZeroVector;

	/** Her feet on the ground beside it once she has climbed out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	FVector BeatExit = FVector::ZeroVector;

	/** The dumpster, built by create_chase.py. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	TSoftObjectPtr<UStaticMesh> DumpsterMesh;

	/** The burst of wrapping paper where she lands. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Beat")
	TSoftObjectPtr<UNiagaraSystem> PaperVfx;

	// --- Furniture and what the route expects of Kate --------------------------------------------------------

	/**
	 * Roof clutter the route runs over, placed by the planner: the vent boxes she vaults on the opening run and,
	 * where no neighbour stands a step up, the stair bulkhead she mantles. Spawned when the chapter starts.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Props")
	TArray<FChaseProp> Props;

	/** The grapples the route expects, in order (the first, the chain, the beat). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Kate")
	TArray<FChaseGrapple> Grapples;

	/** The OpenStreetMap ids of the roofs the route crosses, in order, for the reader. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Chase|Kate")
	TArray<FName> RoofIds;

	/** The waypoints' locations, in order. */
	TArray<FVector> GetPoints() const;

	/** Every objective the route names: the waypoints' in order, then CatchObjective. */
	TArray<FName> GetObjectives() const;

	/** Whether Feet is inside the beat's zone. */
	bool IsInBeatZone(const FVector& Feet) const;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface
};
