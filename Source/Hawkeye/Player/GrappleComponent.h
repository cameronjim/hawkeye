#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/HawkeyeMovementTypes.h"
#include "GrappleComponent.generated.h"

class ACharacter;
class AGrappleAnchor;
class AGrappleArrowProjectile;
class UArrowDefinition;
class UAudioComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class UHawkeyeMovementComponent;
class UInventoryComponent;

/** What a grapple press would do at the marked anchor right now; the HUD draws the marker from it. */
UENUM(BlueprintType)
enum class EGrappleTargetState : uint8
{
	/** Nothing marked. */
	None,
	/** A press fires at the target: green diamond and the key hint. Grapple arrows are never short (no count). */
	Ready,
	/** The last grapple arrow has not arrived yet: grey diamond, no hint. */
	ArrowInFlight,
	/** Mid-zip before the chain window: no marker. */
	TooEarlyToChain,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrappleLandedSignature, AGrappleAnchor*, Anchor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrappleCancelledSignature, AGrappleAnchor*, Anchor);

/**
 * The grapple arrow (claude-docs/gameplay-semantics.md, PLANNED: traversal).
 *
 * Every RefreshSeconds it picks the best anchor: enabled, between MinRange and Range of the character, within
 * ConeDegrees of the camera forward, in line of sight of the camera; the smallest angle wins.
 * TryFire shoots a grapple arrow (the quiver slot whose arrow has OnHitEffect Grapple; it has no count,
 * so nothing is spent) through the bow at that anchor, whatever quiver slot is active (Q, or a release
 * with the grapple slot nocked); when it arrives the character
 * zips along a straight line to the anchor's landing point, with movement input off (the camera
 * still turns). The zip's movement is UHawkeyeMovementComponent's (speed, the hop, the chain's turn,
 * the velocity she leaves with); this component decides the line and what may block it. From the
 * ground the line starts at a launch point ZipLaunchHeight above where she stood, reached by a hop,
 * so a level or downward line clears her own parapet. Firing again mid-zip is allowed once the
 * progress reaches ChainMinProgress; the chain arrow travels at ChainArrowSpeed (0: it is there
 * at once) so the zip is redirected to the new anchor in the air. A zip blocked by
 * anything but the anchor's own building (the one under its landing point, and any static actor
 * within SupportRadius of the anchor) or, until she is ZipStartIgnoreRadius clear of the start,
 * the geometry round where she stood (her own roof and parapet; after a mid-air chain, also the
 * building the old line was landing on), stops and drops the character.
 * Jump or crouch mid-zip lets go (ReleaseZip). Arrows stay in the
 * anchor until the character is within RecoverRadius of it, and are tidied away then (there is no count to add them to).
 *
 * Anchors are bucketed into a GridCellSize grid the first time they are needed, so the query
 * only looks at the cells around the character however many anchors the district has.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UGrappleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGrappleComponent();

	/** The anchor a press would fire at, or null when none is valid. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	AGrappleAnchor* GetTargetAnchor() const { return TargetAnchor.Get(); }

	/**
	 * Fires a grapple arrow at the target anchor out of the bow hand. Never refused for lack of arrows (they are
	 * unlimited); refused (false) with no target, an arrow already in flight, or mid-zip before ChainMinProgress.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grapple")
	bool TryFire();

	/** Starts the zip to Anchor from where the character is now. The arrow's arrival calls this. */
	UFUNCTION(BlueprintCallable, Category = "Grapple")
	bool StartZip(AGrappleAnchor* Anchor);

	/**
	 * Lets go of the zip (jump or crouch): she falls with the zip's velocity, a jump adding the movement
	 * component's ReleaseJumpBoost. The arrow stays in the anchor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grapple")
	void ReleaseZip(bool bJump);

	/** Ready, ArrowInFlight or TooEarlyToChain for the target; None without one. TryFire fires only when Ready. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	EGrappleTargetState GetTargetState() const;

	/**
	 * An anchor nearer the middle of the view than the target (or seen with no target at all) that
	 * passes range, cone and sight but whose zip would hit something on the way: drawn as a dim
	 * grey diamond, never fired at. Null when there is none.
	 */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	AGrappleAnchor* GetBlockedAnchor() const { return BlockedAnchor.Get(); }

	/** Why GetBlockedAnchor cannot be zipped to ("the line hits FireEscapeLanding_34"). */
	const FString& GetBlockedReason() const { return BlockedReason; }

	/** hawkeye.DebugGrapple: the HUD writes the blocked reason under the grey diamond. */
	static bool IsDebugEnabled();

	/**
	 * The best anchor seen from ViewLocation looking along ViewForward: range, cone, sight, and a
	 * clear zip line from where the character is (IsZipClear). Does not change the target. The
	 * nearest-the-middle candidate that failed only the zip line comes back in OutBlocked.
	 */
	AGrappleAnchor* SelectBestAnchor(const FVector& ViewLocation, const FVector& ViewForward) const;
	AGrappleAnchor* SelectBestAnchor(const FVector& ViewLocation, const FVector& ViewForward, AGrappleAnchor*& OutBlocked,
		FString& OutBlockedReason) const;

	/**
	 * The same pick for a character whose capsule centre is at From (on the ground when bFromGround), wherever the
	 * owner really is: range, the clear zip line and the grid lookup are all from From. Hawkeye.Grapple.Audit asks
	 * it from every standing spot in the district without moving Kate.
	 */
	AGrappleAnchor* SelectBestAnchorFrom(const FVector& From, bool bFromGround, const FVector& ViewLocation,
		const FVector& ViewForward, AGrappleAnchor*& OutBlocked, FString& OutBlockedReason) const;

	/** The camera at ViewLocation sees Anchor's marker, give or take SightTolerance (the picker's sight rule). */
	bool HasLineOfSight(const AGrappleAnchor* Anchor, const FVector& ViewLocation) const;

	/**
	 * Point is on the screen of a view from ViewLocation along ViewForward (no roll) with ViewFieldOfView across
	 * and ViewAspectRatio, ScreenEdgeMarginDegrees inside every edge: a marked anchor's diamond is always drawn.
	 */
	bool IsOnScreen(const FVector& ViewLocation, const FVector& ViewForward, const FVector& Point) const;

	/**
	 * The picker's rule over anchors that pass everything (X the angle off the middle of the screen, degrees; Y the
	 * distance, cm): the one nearest the middle, or of those within TieDegrees of it the nearest to her. Returns
	 * the index, INDEX_NONE for none. Hawkeye.Grapple.Audit checks the picker against it.
	 */
	static int32 ChoosePick(const TArray<FVector2f>& AnglesAndDistances, float TieDegrees);

	/** Re-picks the target from this view. RefreshTarget passes the camera's. */
	void UpdateTarget(const FVector& ViewLocation, const FVector& ViewForward);

	/** Re-picks the target from the owner's camera. Runs every RefreshSeconds. */
	UFUNCTION(BlueprintCallable, Category = "Grapple")
	void RefreshTarget();

	/** Re-buckets every anchor in the world. Call after spawning anchors at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Grapple")
	void RebuildAnchorGrid();

	/**
	 * Tidies away the character's own arrows stuck in any anchor within RecoverRadius (grapple arrows have no
	 * count, so nothing goes back in the quiver). Returns how many.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grapple")
	int32 RecoverNearbyArrows();

	/** Arrow arrived at Anchor: start (or redirect) the zip. Called by AGrappleArrowProjectile. */
	void HandleArrowArrived(AGrappleArrowProjectile* Arrow, AGrappleAnchor* Anchor);

	/** The owner's movement component is carrying her along a zip. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	bool IsZipping() const;

	/** 0 at the start of the line, 1 at the landing point. 0 while not zipping. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	float GetZipProgress() const;

	/** False only while a zip is running and it is still too early to chain off it. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	bool CanChain() const { return !IsZipping() || GetZipProgress() >= ChainMinProgress; }

	UFUNCTION(BlueprintPure, Category = "Grapple")
	AGrappleAnchor* GetZipAnchor() const { return ZipAnchor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Grapple")
	bool IsArrowInFlight() const { return InFlightArrow.IsValid(); }

	/** Arrows fired this session. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	int32 GetUseCount() const { return UseCount; }

	/** Length of the current zip's line, cm. 0 while not zipping. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	float GetZipLength() const;

	/** Where the character's capsule centre ends a zip to Anchor. */
	FVector ComputeZipEnd(const AGrappleAnchor* Anchor) const;

	/** Where the straight line of a zip starts from a capsule centre at Start: ZipLaunchHeight up from the ground, Start itself mid-air. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	FVector ComputeZipLaunch(const FVector& Start, bool bFromGround) const;

	/**
	 * Offline, the same rules as a real zip: would a zip to Anchor from a capsule centre at From
	 * (on the ground when bFromGround) get there? Sweeps the capsule from the launch point,
	 * ignoring the start supports until ZipStartIgnoreRadius (plus the capsule radius) from From
	 * and the anchor supports all the way. OutBlocker names what stops it.
	 */
	bool IsZipClear(const FVector& From, const AGrappleAnchor* Anchor, bool bFromGround, AActor** OutBlocker = nullptr,
		const TArray<AActor*>* ExtraStartSupports = nullptr) const;

	/** In the hop before the straight line. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	bool IsHopping() const;

	/** Where the current zip's straight line starts. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	FVector GetZipLaunch() const;

	/** Where the character stood (or hung in the air) when the current zip started. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	FVector GetZipStart() const { return ZipStart; }

	/** The geometry round the start is still being ignored. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	bool IsIgnoringStartSupports() const { return IsZipping() && bStartSupportsIgnored; }

	/** A chain is still turning the direction of travel onto the new line. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	bool IsRedirecting() const;

	/** Unit direction the zip moved in on its last step; zero while not zipping. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	FVector GetZipDirection() const;

	/** Mid-air redirects (chains that took before landing) this session. */
	UFUNCTION(BlueprintPure, Category = "Grapple")
	int32 GetRedirectCount() const { return RedirectCount; }

	UPROPERTY(BlueprintAssignable, Category = "Grapple")
	FOnGrappleLandedSignature OnGrappleLanded;

	UPROPERTY(BlueprintAssignable, Category = "Grapple")
	FOnGrappleCancelledSignature OnGrappleCancelled;

	/** Farthest an anchor can be from the character, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float Range = 2500.f;

	/**
	 * Anchors closer than this are never targeted: the one just landed beside would otherwise keep
	 * the marker lit and a press would zip less than a metre. cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float MinRange = 300.f;

	/** Largest angle between the camera forward and the anchor, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float ConeDegrees = 30.f;

	/** How far along a zip a second arrow may be fired to chain, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChainMinProgress = 0.4f;

	/**
	 * Speed of an arrow fired mid-zip (a chain), cm/s; 0 means it arrives the moment it leaves the
	 * bow. At the 6000 cm/s of a standing shot a short line ended before the arrow got there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float ChainArrowSpeed = 0.f;

	/**
	 * The target must have a clear zip line (IsZipClear), so a marked anchor never drops her into a
	 * fire escape half way. Candidates are checked nearest the middle first, at most MaxClearChecks
	 * per refresh.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple")
	bool bRequireClearZip = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "1"))
	int32 MaxClearChecks = 6;

	/** An anchor this close to the middle-most one's angle (degrees) that is nearer to her is marked instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float PickTieDegrees = 0.5f;

	/** A candidate's marker must be this far inside every edge of the screen, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float ScreenEdgeMarginDegrees = 2.f;

	/** The view's horizontal field of view and width over height; RefreshTarget reads them from the camera and viewport. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grapple")
	float ViewFieldOfView = 90.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Grapple")
	float ViewAspectRatio = 16.f / 9.f;

	/** Seconds between target refreshes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.01"))
	float RefreshSeconds = 0.1f;

	/** A line-of-sight hit this close to the anchor still counts as seeing it (the parapet it sits on). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float SightTolerance = 60.f;

	/**
	 * Static geometry within this of the anchor counts as the anchor's building and does not
	 * block the zip: the line clips the parapet it sits on, and at a corner the neighbouring
	 * tenement's parapet too. cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float SupportRadius = 150.f;

	/**
	 * From the ground, the straight line starts this far above the capsule centre she stood at:
	 * her feet 120 cm up clear a 90 cm parapet, so level and downward lines work. cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float ZipLaunchHeight = 120.f;

	/**
	 * Static geometry within this of where she stood (her own roof, parapet and fire escape) does
	 * not block the zip; it counts again once the capsule is this far plus its radius away. cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float ZipStartIgnoreRadius = 250.f;

	/** Within this of an anchor, its stuck arrows go back in the quiver, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0.0"))
	float RecoverRadius = 200.f;

	/** Side of a spatial grid cell, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "100.0"))
	float GridCellSize = 2500.f;

	/** Where the arrow leaves from when the owner has no bow component, relative to the actor origin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple")
	FVector ArrowLaunchOffset = FVector(40.f, 0.f, 50.f);

	/** What TryFire spawns when the grapple arrow definition names no grapple projectile class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple")
	TSubclassOf<AGrappleArrowProjectile> ArrowClass;

	/** The whoosh as the grapple arrow leaves (MS_Grapple_Fire). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple|Audio")
	TSoftObjectPtr<USoundBase> FireSound;

	/** Loops along the line; its Speed input follows the zip (MS_Grapple_Zip). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple|Audio")
	TSoftObjectPtr<USoundBase> ZipSound;

	/** The landing on the anchor's roof (MS_Grapple_Land). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple|Audio")
	TSoftObjectPtr<USoundBase> LandSound;

	/** The line from her hand to the anchor while she zips (NS_ZipLine; User.BeamStart/BeamEnd each frame). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple|Effects")
	TSoftObjectPtr<UNiagaraSystem> ZipLineVfx;

	/** Sparks where the grapple arrow bites into the anchor (NS_AnchorSparks). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple|Effects")
	TSoftObjectPtr<UNiagaraSystem> AnchorSparksVfx;

	/** The mesh socket the zip line leaves from; the capsule's top when the mesh has none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple|Effects")
	FName ZipLineSocket = TEXT("hand_l");

	/** The zip line's component while she is on the line, else null. */
	UFUNCTION(BlueprintPure, Category = "Grapple|Effects")
	UNiagaraComponent* GetZipLineComponent() const { return ZipLine; }

	/** Where the zip line leaves her: the ZipLineSocket, or 40 cm over the capsule centre. */
	FVector GetZipLineStart() const;

	/** The zip loop's float input, 0..1. */
	static const FName SpeedParameter;

	/** The zip line system's two ends. */
	static const FName BeamStartParameter;
	static const FName BeamEndParameter;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The zip hum while on the line, or null. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ZipLoop = nullptr;

	/** The zip line while on the line, or null. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ZipLine = nullptr;

	/** Puts the zip line's ends on her hand and the anchor (spawning it on the first call of a zip). */
	void UpdateZipLine();

	/** 0..1 for the zip loop: low on the hop up to the line, full along it (scaled by the movement's ZipSpeed). */
	float ComputeZipSoundSpeed() const;

	/** True when Anchor passes range (from From) and cone; OutAngleDegrees is its angle from ViewForward. */
	bool IsAnchorValid(const AGrappleAnchor* Anchor, const FVector& From, const FVector& ViewLocation, const FVector& ViewForward,
		float& OutAngleDegrees) const;

	/** The owner's camera position and forward; the actor's eyes when it has no camera. */
	void GetViewPoint(FVector& OutLocation, FVector& OutForward) const;

	/** Anchors in the grid cells within Range of Location. */
	void GatherNearbyAnchors(const FVector& Location, TArray<AGrappleAnchor*>& OutAnchors) const;

	FIntPoint CellOf(const FVector& Location) const;

	/**
	 * The movement component's sweep hit something. A chain's swing off the line straightens onto it, a graze of
	 * the roof she left (counting again, but still under her) is ignored from then on, anything else blocks.
	 */
	EHawkeyeZipHitResponse HandleZipHit(const FHitResult& Hit, bool bSwinging, bool bArriving);

	/** After each sub-step: her own roof counts again once she is GetStartReleaseDistance() from the start. */
	void HandleZipStep();

	/** The zip is over: the building collision back, the hum and the line off, then landed or cancelled. */
	void HandleZipEnded(EHawkeyeZipEnd Reason);

	/** The owner's movement component when it can zip, else null. */
	UHawkeyeMovementComponent* GetMovement() const;

	/**
	 * The building the anchor stands on, found under its landing point, plus any static actor
	 * within SupportRadius of the anchor. The zip sweep ignores all of them.
	 */
	void FindAnchorSupports(const AGrappleAnchor* Anchor, TArray<AActor*>& OutSupports) const;

	/** The static actors within ZipStartIgnoreRadius of Start and the one under it. */
	void FindStartSupports(const FVector& Start, TArray<AActor*>& OutSupports) const;

	/** Starts or stops the capsule ignoring the current supports. */
	void SetSupportsIgnored(bool bIgnore);

	/** Stops ignoring the start supports that are not also anchor supports. */
	void ReleaseStartSupports();

	/** How far from the start the capsule centre must be before the start supports count again. */
	float GetStartReleaseDistance() const;

	ACharacter* GetCharacter() const;

	UInventoryComponent* GetInventory() const;

	/** The quiver slot holding grapple arrows, or INDEX_NONE. */
	int32 FindGrappleSlot() const;

	/** Spawns the grapple arrow at the bow hand (or ArrowLaunchOffset) aimed at Anchor. */
	AGrappleArrowProjectile* SpawnGrappleArrow(UArrowDefinition* Definition, const AGrappleAnchor* Anchor) const;

	/**
	 * A zip that ends without landing on Anchor (blocked on the way, or redirected by a chain) reels
	 * its arrow back out of the anchor; only letting go with jump or crouch leaves it there.
	 */
	void ReelBackArrow(AGrappleAnchor* Anchor, const TCHAR* Why);

	/** Only the player's pawn (or a pawn nobody controls, in a test) keeps a target; the partner's AI does not need one. */
	bool ShouldRefreshTarget() const;

	TWeakObjectPtr<AGrappleAnchor> TargetAnchor;
	TWeakObjectPtr<AGrappleAnchor> BlockedAnchor;
	FString BlockedReason;
	TWeakObjectPtr<AGrappleAnchor> ZipAnchor;
	TArray<TWeakObjectPtr<AActor>> ZipIgnoredSupports;
	TArray<TWeakObjectPtr<AActor>> ZipStartSupports;
	TWeakObjectPtr<AGrappleArrowProjectile> InFlightArrow;

	/** Built on the first query (anchors are placed with the level) and by RebuildAnchorGrid. */
	mutable TMap<FIntPoint, TArray<TWeakObjectPtr<AGrappleAnchor>>> AnchorGrid;
	mutable bool bGridBuilt = false;

	/** Buckets every anchor in the world by cell. Const so the lazy build can run from a query. */
	void BuildGrid() const;

	FVector ZipStart = FVector::ZeroVector;
	bool bStartSupportsIgnored = false;
	int32 RedirectCount = 0;
	float RefreshAccumulator = 0.f;
	int32 UseCount = 0;
};
