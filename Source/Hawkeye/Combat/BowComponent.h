#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Audio/HawkeyeAudioTypes.h"
#include "Combat/BowHandIKTypes.h"
#include "Math/RandomStream.h"
#include "BowComponent.generated.h"

class AArrowProjectile;
class UAnimInstance;
class UAnimMontage;
class UCombatAnimSet;
enum class ECombatAnimRole : uint8;
class UArrowDefinition;
class UAudioComponent;
class UNiagaraSystem;
class USoundBase;
class UHawkeyeBowIKAnimInstance;
class USkeletalMeshComponent;
class UBowDefinition;
class UInventoryComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDrawChangedSignature, float, Fraction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArrowFiredSignature, UArrowDefinition*, Arrow);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnArrowHitSignature, AActor*, HitActor, float, Damage, bool, bHeadshot);

/**
 * The player's bow (claude-docs/gameplay-semantics.md, "bow and arrows"). Replaces the hitscan
 * weapon for the player; thugs keep UWeaponComponent.
 *
 * StartDraw on the fire press, ReleaseDraw on its release. The draw fraction is the hold time over
 * the bow's FullDrawSeconds; a release below MinDrawFraction cancels and spends nothing. Otherwise
 * one arrow of the active quiver slot is spent and its projectile leaves the bow hand toward the
 * point under the reticle (a camera trace finds it), at a speed, spread and damage set by the draw,
 * with PerfectBonus for a release inside the perfect window. The grapple arrow is not a projectile
 * here: releasing with it nocked hands over to UGrappleComponent::TryFire.
 *
 * Also owns the bow's look: the mesh on the back while holstered, attached to the left hand while
 * drawing, turned toward the aim as the draw builds and the string pulled back. With HandsIKClass
 * set (a post-process AnimBP built by UHawkeyeBowIKGraphBuilder) the arms do the work: IK raises the
 * bow hand to the aim and brings the string hand back to the cheek, the bow rides in the palm and
 * the string's nock follows the string hand. Without it (no AnimBP, tests) the grip blends from the
 * hand up to a point held out in front of the left shoulder instead. An arrow sits on the string
 * from the nock forward through the grip while drawing.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UBowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBowComponent();

	/** Starts drawing. False with no bow, while already drawing, or with a dead owner. */
	UFUNCTION(BlueprintCallable, Category = "Bow")
	bool StartDraw();

	/** Releases: fires if drawn past MinDrawFraction, cancels otherwise. True when an arrow (or grapple) went out. */
	UFUNCTION(BlueprintCallable, Category = "Bow")
	bool ReleaseDraw();

	/** Lets the string down without firing (sprint, a zip, a takedown). */
	UFUNCTION(BlueprintCallable, Category = "Bow")
	void CancelDraw();

	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsDrawing() const { return bDrawing; }

	/** 0..1, how far the current draw has come. 0 while not drawing. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float GetDrawFraction() const;

	/** Seconds the current draw has been held. 0 while not drawing. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float GetDrawElapsed() const;

	/**
	 * How fast the draw counts: 1 for Kate; an archer's is set from the difficulty so his full draw
	 * takes 1.5 / 1.2 / 1.0 s. GetDrawElapsed is real draw time times this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow", meta = (ClampMin = "0.1"))
	float DrawRate = 1.f;

	/**
	 * The release's bend (gameplay-semantics.md, "Bow aim assist"): a line that would miss every thug turns up
	 * to the setting's BendMaxDegrees toward the nearest chest in the magnetism cone. Kate only; never while
	 * an AI controller owns the bow (archers, Clint as the partner).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow|Aim assist")
	bool bAimAssist = false;

	/**
	 * Aim, from Start, bent by the aim assist for an arrow leaving at Speed: unchanged with the assist off, a line
	 * already on someone's body, or nobody in the cone. Public for tests.
	 */
	FVector ApplyReleaseBend(const FVector& Start, const FVector& Aim, float Speed) const;

	/** Releases the bend has turned. */
	UFUNCTION(BlueprintPure, Category = "Bow|Aim assist")
	int32 GetBentReleaseCount() const { return BentReleases; }

	/** Cone half-angle a release now would get, degrees. The bow's MaxSpread while not drawing. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	float GetCurrentSpreadDegrees() const;

	/** True while a release now would be perfect. The HUD flashes the draw bar on it. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	bool IsInPerfectWindow() const;

	/** The owner's bow, or null. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	UBowDefinition* GetBow() const;

	/** Where arrows leave from: the bow hand socket, or the owner's origin plus ArrowLaunchOffset. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	FVector GetArrowSpawnLocation() const;

	/** The world point under the reticle: a camera trace on the Weapon channel, or AimTraceDistance ahead. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	FVector ComputeAimPoint() const;

	/**
	 * Aims at Point instead of the reticle until ClearAimOverride: the AI partner has no camera to
	 * trace from, so he draws on a thug's chest this way. Same draw, spread and projectile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bow")
	void SetAimOverride(const FVector& Point);

	UFUNCTION(BlueprintCallable, Category = "Bow")
	void ClearAimOverride() { bHasAimOverride = false; }

	UFUNCTION(BlueprintPure, Category = "Bow")
	bool HasAimOverride() const { return bHasAimOverride; }

	/** Spawns Arrow's projectile at the bow hand. The grapple uses this for its own flight. */
	AArrowProjectile* SpawnArrowProjectile(UArrowDefinition* Arrow, TSubclassOf<AArrowProjectile> FallbackClass,
		const FVector& Direction) const;

	/** Called by an arrow that hit something with health. Broadcasts OnHit. */
	void NotifyArrowHit(AActor* HitActor, float Damage, bool bHeadshot);

	/** Overrides the clock, for automation tests whose components have no ticking world. */
	void SetTestTimeSeconds(double InSeconds);

	/**
	 * Back to the world clock after SetTestTimeSeconds (a screenshot that held a draw still). A follow-through
	 * still running keeps what it had left, rather than lasting until the world clock reaches the test time.
	 */
	void ClearTestTime();

	/** Replaces the spread stream so a test gets the same cone every run. */
	void SetTestRandomStream(const FRandomStream& InStream) { SpreadStream = InStream; }

	UFUNCTION(BlueprintPure, Category = "Bow|Visual")
	UStaticMeshComponent* GetBowMeshComponent() const { return BowMesh; }

	/** True while the bow is in the hand: raised, or resting there with bHolsterWhenIdle off. False on the back. */
	UFUNCTION(BlueprintPure, Category = "Bow|Visual")
	bool IsBowInHand() const;

	/** True while the bow is up at the aim: drawing, or the follow-through just after a shot. */
	UFUNCTION(BlueprintPure, Category = "Bow|Visual")
	bool IsBowRaised() const;

	/**
	 * The way the hands aim the bow from From: at the aim override for the AI, else the controller's
	 * view (the player's camera), else the actor's facing.
	 */
	FRotator ComputeHandsAimRotation(const FVector& From) const;

	/** The owner mesh's running bow hands post-process instance, or null (no HandsIKClass, no mesh). */
	UHawkeyeBowIKAnimInstance* GetHandsIKInstance() const;

	/** Sets HandsIKClass as the owner mesh's post-process AnimBP override. BeginPlay calls it. */
	void ApplyHandsIK();

	/**
	 * The clips the bow plays over locomotion: BowDraw at its own pace, BowAimIdle once it is at the
	 * anchor, BowFire on release, BowNock after it. Null, or empty roles, keep the IK-only bow.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bow|Animation")
	void SetAnimSet(UCombatAnimSet* InAnimSet) { AnimSet = InAnimSet; }

	UFUNCTION(BlueprintPure, Category = "Bow|Animation")
	UCombatAnimSet* GetAnimSet() const { return AnimSet; }

	/** True while one of the bow clips plays (not blending out): it holds the bow arm, IK fixes the string hand. */
	UFUNCTION(BlueprintPure, Category = "Bow|Animation")
	bool IsBowClipPlaying() const;

	/** The bow clip last started (it may be blending out), or null. */
	UAnimMontage* GetBowClip() const { return BowClip; }

	/** The role of GetBowClip, or None. */
	ECombatAnimRole GetBowClipRole() const { return static_cast<ECombatAnimRole>(BowClipRole); }

	/**
	 * 0..1, how much a bow clip holds the arms right now: the UpperBody slot's weight on the owner mesh's
	 * post-process instance, blends included. 0 with no clip (the IK-only bow).
	 */
	float GetBowClipWeight() const;

	/**
	 * The bow clip to move on to from Current (gameplay-semantics.md, "Combat animation clips"): BowAimIdle
	 * once a draw's BowDraw is at its end (it holds its last frame there), BowNock once a BowFire is over
	 * while not drawing, else None (stay). Pure.
	 */
	static ECombatAnimRole NextBowClip(ECombatAnimRole Current, bool bDrawing, bool bClipAtEnd);

	/** True while the bow belongs in the hand: raised, or held there by bHolsterWhenIdle off, or by a clip still blended in above ClipKeepsBowWeight. Pure. */
	static bool KeepsBowInHand(bool bRaised, bool bHolsterWhenIdle, float ClipWeight);

	/**
	 * A shot on a fire clip of ClipSeconds that blends out over BlendOutSeconds follows through for this
	 * long (capped by FollowThroughSeconds): until the clip starts to let the arms down. Pure.
	 */
	static float ComputeClipFollowThrough(float ClipSeconds, float BlendOutSeconds)
	{
		return FMath::Max(0.f, ClipSeconds - FMath::Max(0.f, BlendOutSeconds));
	}

	/** Bow clips blend out over this on a let-down, a cancel or a release too early to fire, seconds. */
	static constexpr float ClipLetDownSeconds = 0.25f;

	/** The bow stays in the hand while a bow clip is blended in above this. */
	static constexpr float ClipKeepsBowWeight = 0.05f;

	/** Every tick while drawing, and 0 on release or cancel. */
	UPROPERTY(BlueprintAssignable, Category = "Bow")
	FOnDrawChangedSignature OnDrawChanged;

	/** After an arrow leaves the bow (or the grapple is handed its shot). */
	UPROPERTY(BlueprintAssignable, Category = "Bow")
	FOnArrowFiredSignature OnArrowFired;

	/** When one of this bow's arrows lands on something with health, after the damage. */
	UPROPERTY(BlueprintAssignable, Category = "Bow")
	FOnArrowHitSignature OnHit;

	/**
	 * The bow of an owner with no quiver (an AI archer, BP_Archer): used when the owner has no
	 * UInventoryComponent. Arrows come from OwnArrow and are never spent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|AI")
	TObjectPtr<UBowDefinition> OwnBow;

	/** What an owner with no quiver shoots (DA_Arrow_Trickshot on BP_Archer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|AI")
	TObjectPtr<UArrowDefinition> OwnArrow;

	/** Arrows loosed by this bow since play began. */
	UFUNCTION(BlueprintPure, Category = "Bow")
	int32 GetArrowsLoosed() const { return ArrowsLoosed; }

	/** World position of the tip of the arrow on the string, valid while drawing. */
	UFUNCTION(BlueprintPure, Category = "Bow|Visual")
	FVector GetNockedArrowTip() const { return NockedArrowTip; }

	/** Used when the arrow definition names no projectile class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow")
	TSubclassOf<AArrowProjectile> DefaultProjectileClass;

	/** Where arrows leave from when the mesh has no hand socket, relative to the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow")
	FVector ArrowLaunchOffset = FVector(40.f, -10.f, 50.f);

	/** How far the reticle trace looks, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow", meta = (ClampMin = "100.0"))
	float AimTraceDistance = 20000.f;

	/** Walk speed multiplier while drawing (the aimed walk is slowed again). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DrawWalkSpeedMultiplier = 0.6f;

	// --- Hands ------------------------------------------------------------------------------------

	/**
	 * Post-process AnimBP that puts the hands on the bow (ABP_BowIK_Post, ABP_BowIK_Post_Thug), set as
	 * the owner mesh's post-process override at BeginPlay. It runs after the main AnimBP and reads
	 * HandsIK from here. None leaves the mesh alone and the bow uses the held-out placeholder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	TSubclassOf<UAnimInstance> HandsIKClass;

	/** Where the hands and elbows go while the bow is up, and how fast they get there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Hands")
	FBowHandIKSettings HandsIK;

	// --- Visual tuning ----------------------------------------------------------------------------

	/** Carry the bow across the back while not drawing. Off: it stays in the left hand, arm down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bow|Visual")
	bool bHolsterWhenIdle = true;

	/**
	 * Pitch of the bow resting in the hand with the arm down (bHolsterWhenIdle off), degrees: the top
	 * limb tips forward, off the arm. It takes DrawnCantDegrees of cant too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	float RestPitchDegrees = -30.f;

	/** Bone the bow hangs off while holstered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	FName HolsterBone = FName(TEXT("spine_03"));

	/** Holstered position relative to HolsterBone, in the actor's frame (X forward, Y right, Z up), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	FVector HolsterOffset = FVector(-16.f, 0.f, -6.f);

	/** Holstered tilt across the back, degrees about the actor's forward axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	float HolsterRollDegrees = 35.f;

	/** Pitch of the bow in the hand at the start of the draw, before it comes up to the aim, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	float LoweredPitchDegrees = -50.f;

	/** Cant of the drawn bow, degrees of roll; negative tips the top limb out to the left, clear of her head. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	float DrawnCantDegrees = -12.f;

	/** Grip offset from the hand socket in the aim frame (X along the aim), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	FVector HandGripOffset = FVector(0.f, 0.f, 0.f);

	/** Bone the held-out grip is measured from while drawing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	FName DrawShoulderBone = FName(TEXT("upperarm_l"));

	/** Where the grip is held at full draw, from DrawShoulderBone in the aim frame (X along the aim), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	FVector DrawnGripOffset = FVector(58.f, -10.f, -4.f);

	/** Draw fraction by which the bow has come up from the hand to the held-out grip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float RaiseByDrawFraction = 0.4f;

	/** Limb tip position in the bow mesh's frame; the string runs between (X, 0, +Z) and (X, 0, -Z), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	FVector StringTip = FVector(-12.f, 0.f, 60.f);

	/** Seconds the bow stays up in the hand after an arrow is loosed, before going back on her back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual", meta = (ClampMin = "0.0"))
	float FollowThroughSeconds = 0.6f;

	/** How far the string's nock point comes back at full draw, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual", meta = (ClampMin = "0.0"))
	float StringPullAtFullDraw = 50.f;

	/** String thickness, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual", meta = (ClampMin = "0.05"))
	float StringThickness = 0.5f;

	/** String colour, through BasicShapeMaterial's Color parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual")
	FLinearColor StringColor = FLinearColor(0.85f, 0.85f, 0.8f);

	/** Length of the arrow shown on the string while drawing, from the nock forward past the grip, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Visual", meta = (ClampMin = "1.0"))
	float NockedArrowLength = 80.f;

	// --- Audio ------------------------------------------------------------------------------------

	/** Loops while drawing; its Draw input follows the draw fraction (MS_Bow_Draw). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Audio")
	TSoftObjectPtr<USoundBase> DrawSound;

	/** The twang as an arrow leaves (MS_Bow_Release). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Audio")
	TSoftObjectPtr<USoundBase> ReleaseSound;

	/** Rides on each of this bow's arrows as it flies (MS_Arrow_Whistle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Audio")
	TSoftObjectPtr<USoundBase> WhistleSound;

	/** This bow's arrows landing in brick, concrete and steel: most of the city (MS_Arrow_Impact_Stone). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Audio")
	TSoftObjectPtr<USoundBase> ImpactStoneSound;

	/** In a water tower, a door, scaffolding (MS_Arrow_Impact_Wood). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Audio")
	TSoftObjectPtr<USoundBase> ImpactWoodSound;

	/** In anything with health (MS_Arrow_Impact_Flesh). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Audio")
	TSoftObjectPtr<USoundBase> ImpactFleshSound;

	/** An arrow or a pickup going into the quiver of this bow's owner (MS_Arrow_Pickup). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Audio")
	TSoftObjectPtr<USoundBase> PickupSound;

	/** The draw loop's float input. */
	static const FName DrawParameter;

	/** The impact sound for Surface. */
	const TSoftObjectPtr<USoundBase>& GetImpactSound(EHawkeyeArrowSurface Surface) const;

	/** Plays PickupSound on the owner. */
	void PlayPickupSound() const;

	// --- Effects ----------------------------------------------------------------------------------

	/** A tiny puff where the arrow leaves the string (NS_BowRelease). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Effects")
	TSoftObjectPtr<UNiagaraSystem> ReleaseVfx;

	/** Rides on every arrow in flight unless its definition has its own (NS_ArrowTrail). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Effects")
	TSoftObjectPtr<UNiagaraSystem> ArrowTrailVfx;

	/** An arrow into stone, brick, concrete, metal (NS_ArrowImpact_Stone), out along the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Effects")
	TSoftObjectPtr<UNiagaraSystem> ImpactStoneVfx;

	/** An arrow into wood (NS_ArrowImpact_Wood). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Effects")
	TSoftObjectPtr<UNiagaraSystem> ImpactWoodVfx;

	/** An arrow into anyone with health: a purple-white comic spark, no blood (NS_HitSpark). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bow|Effects")
	TSoftObjectPtr<UNiagaraSystem> HitSparkVfx;

	/** The impact effect for Surface: HitSparkVfx for flesh. */
	const TSoftObjectPtr<UNiagaraSystem>& GetImpactVfx(EHawkeyeArrowSurface Surface) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Test clock or world clock. */
	double GetNowSeconds() const;

	UInventoryComponent* FindInventory() const;

	/** Spends one arrow of the active slot and launches it. False when the slot is at zero. */
	bool FireArrow(float Elapsed);

	mutable int32 BentReleases = 0;

	/** The grapple arrow's release: the grapple component takes the shot. */
	bool FireGrapple(UArrowDefinition* Arrow);

	/** Ends the draw; the owner drops the forced aim. */
	void EndDraw();

	/** Plays Role's bow clip at rate 1 (its pace is the clip's own); false when there is none that plays. */
	bool PlayBowClip(ECombatAnimRole Role);

	/** Blends the bow clip out. */
	void StopBowClip(float BlendOutSeconds);

	/** Moves the clips on (NextBowClip): the held aim once the draw clip is at the anchor, the nock after the release. Called from Tick. */
	void UpdateBowClip();

	/** Creates or removes the bow and string components to match whether a bow is owned. */
	void RefreshBowVisual();

	/** Places the bow on the back or in the hand and pulls the string for this frame. */
	void UpdateBowVisual();

	/** The socket the bow is gripped by while in the hand, or HolsterBone when there is none. */
	FName FindGripSocket(const USkeletalMeshComponent& Body, bool bWithHandsIK) const;

	/** The bow in the palm with the hands IK running: turned from resting to the aim by its bow alpha. */
	void PlaceBowWithHandsIK(const USkeletalMeshComponent& Body, const UHawkeyeBowIKAnimInstance& Hands, FName Grip,
		FVector& OutLocation, FQuat& OutRotation) const;

	/** The bow without hands IK: blended up from the hand to the held-out point in the first part of the draw. */
	void PlaceBowHeldOut(const USkeletalMeshComponent& Body, FName Grip, FVector& OutLocation, FQuat& OutRotation) const;

	/** Lays the string halves and the nocked arrow for this frame from the bow's transform. */
	void PlaceStringAndArrow(const USkeletalMeshComponent& Body, const UHawkeyeBowIKAnimInstance* Hands);

	/** Lays one string half from Tip to Nock (world space). */
	void PlaceString(UStaticMeshComponent* String, const FVector& Tip, const FVector& Nock) const;

	UFUNCTION()
	void HandleInventoryChanged();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BowMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> StringUpper = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> StringLower = nullptr;

	/** The arrow on the string while drawing: a pale shaft and Kate's purple nock. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NockedShaft = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NockedNock = nullptr;

	/** The bow the visual was built for, so a new bow rebuilds it. */
	UPROPERTY(Transient)
	TObjectPtr<UBowDefinition> VisualBow = nullptr;

	/** The creak while drawing, or null. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> DrawLoop = nullptr;

	UPROPERTY(Transient)
	FRandomStream SpreadStream;

	bool bDrawing = false;
	double DrawStartSeconds = 0.0;

	/** The bow stays in the hand until this time after a shot (the follow-through). */
	double FollowThroughUntilSeconds = -1.0;

	bool bHasAimOverride = false;
	FVector AimOverridePoint = FVector::ZeroVector;

	bool bUseTestTime = false;
	double TestTimeOverride = 0.0;

	int32 ArrowsLoosed = 0;
	FVector NockedArrowTip = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UCombatAnimSet> AnimSet;

	/** The bow clip playing and where. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> BowClip;

	TWeakObjectPtr<UAnimInstance> BowClipInstance;
	uint8 BowClipRole = 0;
};
