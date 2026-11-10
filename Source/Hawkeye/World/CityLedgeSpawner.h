#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CityLedgeSpawner.generated.h"

class AFireEscapeLanding;
class AGrappleAnchor;
class UCityLedgeData;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMesh;
struct FCityFireEscapeRecord;
struct FCityLedgeRecord;

/**
 * Spawns the district's roof-edge ledges and grapple anchors from a UCityLedgeData at load, so the
 * map does not carry five thousand saved actors (claude-docs/gameplay-semantics.md, traversal).
 *
 * BeginPlay spawns every anchor (and rebuilds every grapple component's anchor grid) and every
 * ledge within ImmediateRadius of the player, then the rest of the ledges nearest first, at most
 * FrameBudgetMs of each frame, logging both times. Each ledge is a Blueprint whose construction
 * script costs about 0.3 ms, so all 3,700 at once would hitch the load by well over a second.
 * SpawnAll() does everything at once; verify_city.py calls it. In the editor the actors appear
 * only while bSpawnInEditor is on; editor-spawned actors are transient and never saved.
 *
 * Every ledge is LedgeClass (BP_TraversableBlock) with its meshes hidden and blocking only the
 * Traversable channel (ECC_GameTraceChannel1), tagged City, CityLedge and osm:<id>. Every anchor
 * is AnchorClass (BP_GrappleAnchor) tagged City, CityAnchor and osm:<id>. In an editor world they
 * also get their generator labels (City_Ledge_<osm id>_<edge>, City_Anchor_<n>).
 *
 * Fire escapes: every FCityFireEscapeRecord is an AFireEscapeLanding (its collision, tagged City,
 * CityFireEscape, osm:<id>, floor:<n>, labelled City_FireEscape_<osm id>_<floor>) plus a LedgeClass
 * block on its outer rail (City_FireEscapeLedge_...), queued nearest first with the roof ledges.
 * Their visible bars are instances in two transient instanced-mesh components (cube and
 * cylinder, FireEscapeMaterial), all added at once; they are never saved with the map.
 *
 * Clutter: every FCityClutterGroup becomes one transient hierarchical instanced mesh (water towers,
 * HVAC boxes, chimneys, hydrants, bins, bags, scaffolding, parked cars), added at once at load. A
 * group with bCollision blocks pawns and physics but ignores the visibility, camera and traversable
 * traces, and none of them affect navigation.
 *
 * Chimney wisps: in a game world, ChimneyWispCount of the chimneys nearest the player at load get
 * a slow smoke stream (ChimneyWispVfx, NS_ChimneyWisp) ChimneyTopCm above their base, spawned on the
 * spawner's first tick rather than in BeginPlay so they stay out of the load.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API ACityLedgeSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACityLedgeSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TObjectPtr<UCityLedgeData> Data;

	/** What each ledge record spawns: the sample's LevelBlock_Traversable child. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TSubclassOf<AActor> LedgeClass;

	/** What each anchor record spawns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TSubclassOf<AGrappleAnchor> AnchorClass;

	/** What each fire-escape record spawns (its collision). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TSubclassOf<AFireEscapeLanding> FireEscapeClass;

	/** The unit cube the fire escapes' slabs, rails and posts are instances of. Engine cube when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TObjectPtr<UStaticMesh> FireEscapeCube;

	/** The unit cylinder the ladders are instances of. Engine cylinder when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TObjectPtr<UStaticMesh> FireEscapeCylinder;

	/** Black iron (M_SteelPainted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	TObjectPtr<UMaterialInterface> FireEscapeMaterial;

	/** Smoke rising from a few chimneys (NS_ChimneyWisp). None when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Effects")
	TSoftObjectPtr<UNiagaraSystem> ChimneyWispVfx;

	/** How many chimneys, nearest the player at load, smoke. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Effects", meta = (ClampMin = "0"))
	int32 ChimneyWispCount = 6;

	/** The top of a chimney above its instance origin (SM_City_Chimney is 170 cm), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Effects", meta = (ClampMin = "0.0"))
	float ChimneyTopCm = 175.f;

	/** Clutter group whose instances are chimneys. */
	static const FName ChimneyKind;

	/**
	 * The tops of the Count chimneys nearest Focus (2D), nearest first: each instance's origin plus
	 * TopCm along its up axis, scaled. Pure.
	 */
	static TArray<FVector> PickChimneyTops(const TArray<FTransform>& Chimneys, const FVector& Focus, int32 Count,
		float TopCm);

	/** Chimney wisps spawned. */
	UFUNCTION(BlueprintPure, Category = "City|Effects")
	int32 GetChimneyWispCount() const { return ChimneyWisps.Num(); }

	/** Show the spawned actors in the editor viewport (transient, never saved). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	bool bSpawnInEditor = false;

	/**
	 * At BeginPlay, ledges this close to the player (2D) spawn before the first frame, cm. 40 m is
	 * further than she can get before the next ring arrives at FrameBudgetMs a frame; 100 m cost
	 * 0.5 s of the load.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City", meta = (ClampMin = "0.0"))
	float ImmediateRadius = 4000.f;

	/** After BeginPlay, how much of each frame the remaining ledges may take, ms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City", meta = (ClampMin = "0.5"))
	float FrameBudgetMs = 4.f;

	/** Spawns every ledge and anchor that is not already out, at once. Returns how many it spawned. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "City")
	int32 SpawnAll();

	/** Destroys everything this spawner spawned. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "City")
	void DestroySpawned();

	/** Every ledge and anchor is out. */
	UFUNCTION(BlueprintPure, Category = "City")
	bool IsSpawnComplete() const;

	UFUNCTION(BlueprintPure, Category = "City")
	int32 GetSpawnedLedgeCount() const { return SpawnedLedges.Num(); }

	UFUNCTION(BlueprintPure, Category = "City")
	int32 GetSpawnedAnchorCount() const { return SpawnedAnchors.Num(); }

	/** The spawned ledges. Transient, so editor actor listings skip them; verify_city reads these. */
	UFUNCTION(BlueprintPure, Category = "City")
	TArray<AActor*> GetSpawnedLedges() const { return TArray<AActor*>(SpawnedLedges); }

	UFUNCTION(BlueprintPure, Category = "City")
	TArray<AGrappleAnchor*> GetSpawnedAnchors() const { return TArray<AGrappleAnchor*>(SpawnedAnchors); }

	UFUNCTION(BlueprintPure, Category = "City")
	int32 GetSpawnedFireEscapeCount() const { return SpawnedFireEscapes.Num(); }

	UFUNCTION(BlueprintPure, Category = "City")
	TArray<AFireEscapeLanding*> GetSpawnedFireEscapes() const { return TArray<AFireEscapeLanding*>(SpawnedFireEscapes); }

	/** The ledge blocks on the fire escapes' outer rails, in the same order as their landings. */
	UFUNCTION(BlueprintPure, Category = "City")
	TArray<AActor*> GetSpawnedFireEscapeLedges() const { return TArray<AActor*>(SpawnedFireEscapeLedges); }

	/** Clutter instances drawn, all groups together. */
	UFUNCTION(BlueprintPure, Category = "City")
	int32 GetClutterInstanceCount() const;

	/** The clutter components, one per FCityClutterGroup, in the data's order. */
	UFUNCTION(BlueprintPure, Category = "City")
	TArray<UHierarchicalInstancedStaticMeshComponent*> GetClutterComponents() const
	{
		return TArray<UHierarchicalInstancedStaticMeshComponent*>(ClutterComponents);
	}

	/** Visible fire-escape parts drawn (cube and cylinder instances together). */
	UFUNCTION(BlueprintPure, Category = "City")
	int32 GetFireEscapeInstanceCount() const;

	/** Wall time spent spawning ledges before the first frame (all of them for SpawnAll), s. */
	UFUNCTION(BlueprintPure, Category = "City")
	float GetLoadLedgeSpawnSeconds() const { return LoadLedgeSeconds; }

	/** Wall time spent spawning ledges in total, load plus the per-frame remainder, s. */
	UFUNCTION(BlueprintPure, Category = "City")
	float GetTotalLedgeSpawnSeconds() const { return TotalLedgeSeconds; }

	/** Wall time spent spawning anchors, s. */
	UFUNCTION(BlueprintPure, Category = "City")
	float GetAnchorSpawnSeconds() const { return AnchorSeconds; }

	static const FName LedgeTag;
	static const FName AnchorTag;
	static const FName FireEscapeTag;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Destroyed() override;

	/** Spawns every anchor not yet out and rebuilds the grapple grids. */
	void SpawnAnchors();

	/** Orders the ledge records nearest Focus first and resets the queue. */
	void QueueLedges(const FVector& Focus);

	/** Spawns queued ledges until the queue is empty, one is further than MaxDistance, or Deadline. */
	void SpawnQueuedLedges(float MaxDistance, double Deadline);

	AActor* SpawnLedge(const FCityLedgeRecord& Record);

	/** One queue entry: a roof ledge (index into Ledges) or a fire escape (Ledges.Num() + index). */
	void SpawnQueued(int32 Entry);

	/** A fire-escape landing's collision actor and its rail ledge. */
	AFireEscapeLanding* SpawnFireEscape(const FCityFireEscapeRecord& Record);

	/** Adds every fire escape's visible parts as instances, once. */
	void SpawnFireEscapeVisuals();

	/** One instanced mesh per clutter group, once. */
	void SpawnClutter();

	/** The chimney wisps, once, in a game world. */
	void SpawnChimneyWisps();

	/** The ledge class at Transform, trace-only, tagged; Label in an editor world. */
	AActor* SpawnLedgeActor(const FTransform& Transform, const TArray<FName>& LedgeTags, const FString& Label);

	/** Hidden, query only, blocking nothing but the Traversable channel; the height labels and the ledge splines hidden. */
	static void MakeTraceOnly(AActor* Ledge);

	/** The player pawn, else a PlayerStart, else this actor. */
	FVector FindFocus() const;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "City")
	TArray<TObjectPtr<AActor>> SpawnedLedges;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "City")
	TArray<TObjectPtr<AGrappleAnchor>> SpawnedAnchors;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "City")
	TArray<TObjectPtr<AFireEscapeLanding>> SpawnedFireEscapes;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "City")
	TArray<TObjectPtr<AActor>> SpawnedFireEscapeLedges;

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FireEscapeCubes;

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FireEscapeCylinders;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ClutterComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> ChimneyWisps;

	/** Indices into Data->Ledges, nearest first, and how far down the list spawning has got. */
	TArray<int32> LedgeQueue;
	TArray<float> LedgeQueueDistance;
	int32 LedgeQueueNext = 0;
	bool bLedgesQueued = false;

	float LoadLedgeSeconds = 0.f;
	float TotalLedgeSeconds = 0.f;
	float AnchorSeconds = 0.f;
	int32 BackgroundFrames = 0;
	bool bChimneyWispsPending = false;
	float ChimneyWaitSeconds = 0.f;
	static constexpr float ChimneyWaitLimitSeconds = 10.f;
};
