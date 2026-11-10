#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Math/RandomStream.h"
#include "WeaponComponent.generated.h"

class UDamageType;
class UNiagaraSystem;
class USoundBase;
class UWeaponDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChangedSignature, int32, CurrentAmmo, int32, ReserveAmmo);

/** Fired instead of a shot when the trigger is pulled on an empty magazine. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEmptyClickSignature);

/** Fired when a shot lands on something that can take damage. The HUD flashes a hit marker on it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponHitSignature, AActor*, HitActor, float, DamageDealt);

/**
 * Hitscan weapon and melee swing attached to a pawn. Shots trace from the owner's view point
 * (the player camera, or a thug's eyes); punches sweep from the owner's eyes.
 *
 * The player shoots with UBowComponent and uses this only for Hands; thugs still shoot with it.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponComponent();

	/**
	 * False while the owner is holding no ranged weapon: Fire traces nothing, Reload does
	 * nothing and the HUD shows no ammo. The player's component only ever holds Hands (the
	 * melee fallback while no bow is owned), so it is always false there. Thugs start armed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	bool bHasWeapon = true;

	/**
	 * The weapon this component is currently firing. Null for a component driven by its own
	 * properties (thugs); Hands for the player, set by UInventoryComponent.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponDefinition> ActiveDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Ammo", meta = (ClampMin = "1"))
	int32 MagazineSize = 12;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 CurrentAmmo = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	int32 ReserveAmmo = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.0"))
	float Damage = 34.f;

	/** Damage multiplier applied when the hit bone is in HeadBoneNames. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "1.0"))
	float HeadshotMultiplier = 3.f;

	/** Bones that count as a head. Matches the default UE5 skeleton naming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSet<FName> HeadBoneNames;

	/** Hitscan range in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.0"))
	float Range = 10000.f;

	/** Rounds per minute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "1.0"))
	float FireRate = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.0"))
	float ReloadSeconds = 2.f;

	/** Cone half-angle applied to the shot direction while firing from the hip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Spread", meta = (ClampMin = "0.0"))
	float HipSpreadDegrees = 2.5f;

	/** Cone half-angle applied while aiming down sights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Spread", meta = (ClampMin = "0.0"))
	float AimSpreadDegrees = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<UDamageType> DamageTypeClass;

	/**
	 * Channel the hitscan traces on. Defaults to the project's "Weapon" channel, because the
	 * stock Pawn and CharacterMesh profiles ignore Visibility and a Visibility trace therefore
	 * passes straight through every character.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_GameTraceChannel1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon", meta = (ClampMin = "0.0"))
	float TraceChannelDebugDuration = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Debug")
	bool bDrawDebug = false;

	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FOnAmmoChangedSignature OnAmmoChanged;

	/** Fired when Fire() is called with an empty magazine; play the dry-fire click from this. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FOnEmptyClickSignature OnEmptyClick;

	/** Fired once per shot that lands on a damageable actor, after the damage is applied. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FOnWeaponHitSignature OnHit;

	/** Each shot fired, at the muzzle end of the owner (MS_Thug_Gunshot on the gunner). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Audio")
	TSoftObjectPtr<USoundBase> FireSound;

	/** Each shot's flash at the muzzle, along the shot (NS_MuzzleFlash on the gunner). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Effects")
	TSoftObjectPtr<UNiagaraSystem> MuzzleFlashVfx;

	/** Each shot's line from the muzzle to where it hit (NS_Tracer; User.BeamStart and BeamEnd). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Effects")
	TSoftObjectPtr<UNiagaraSystem> TracerVfx;

	/** Where shots leave from for the effects: a thug's pistol, else 50 cm out from the view point. */
	FVector GetMuzzleLocation(const FVector& ViewLocation, const FVector& Direction) const;

	/** Damage one punch does. Separate from Damage so holding fists never rewrites gun stats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Melee", meta = (ClampMin = "0.0"))
	float MeleeDamage = 15.f;

	/** Reach of the punch sweep, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Melee", meta = (ClampMin = "0.0"))
	float MeleeRange = 120.f;

	/** Seconds between punches. Replaces the fire-rate check while the active weapon is melee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Melee", meta = (ClampMin = "0.0"))
	float MeleeCooldown = 0.6f;

	/** Radius of the punch sweep, so a fist does not need pixel-accurate aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Melee", meta = (ClampMin = "1.0"))
	float MeleeSweepRadius = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Melee")
	bool bStaggerOnHit = true;

	/** Arms the owner with Magazine rounds loaded and Reserve spare, and fires OnAmmoChanged. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void GiveWeapon(int32 Magazine, int32 Reserve);

	/**
	 * Points the component at Definition and loads its ammo. UInventoryComponent hands the
	 * player's component Hands; a null definition disarms the owner.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetActiveWeapon(UWeaponDefinition* Definition, int32 Magazine, int32 Reserve);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UWeaponDefinition* GetActiveDefinition() const { return ActiveDefinition; }

	/** True while the active weapon is bare hands. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsMelee() const;

	/** Disarms the owner. Ammo is kept so a later GiveWeapon can restore it. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void RemoveWeapon();

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool HasWeapon() const { return bHasWeapon; }

	/** Fires one round if allowed. Returns true when a shot went out. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	bool Fire();

	/** Starts the reload timer. Returns false if already full, already reloading or out of reserve. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	bool Reload();

	/**
	 * Moves min(MagazineSize - CurrentAmmo, ReserveAmmo) rounds into the magazine right now and
	 * ends the reload. Called by the reload timer, by an animation notify, and by tests.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void CompleteReloadNow();

	/** Aborts an in-progress reload without moving any ammo (sprinting does this). */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void CancelReload();

	/**
	 * Tightens the shot cone while the owner is aiming down sights. Does nothing while
	 * bHasWeapon is false, so aiming empty-handed never leaves the component in an aimed state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Spread")
	void SetAiming(bool bInAiming);

	UFUNCTION(BlueprintPure, Category = "Weapon|Spread")
	bool IsAiming() const { return bIsAiming; }

	/** AimSpreadDegrees while aiming, HipSpreadDegrees otherwise. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Spread")
	float GetCurrentSpreadDegrees() const { return bIsAiming ? AimSpreadDegrees : HipSpreadDegrees; }

	/**
	 * Direction rotated by a random offset inside a cone of SpreadDegrees half-angle. Pure and
	 * stream-driven so the spread maths is testable without firing a shot.
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Spread")
	static FVector ApplyConeSpread(const FVector& Direction, float SpreadDegrees, const FRandomStream& Stream);

	/** Replaces the spread stream so a test gets the same cone offsets every run. */
	void SetTestRandomStream(const FRandomStream& InStream);

	/** Damage this weapon deals to a hit on BoneName, including the headshot multiplier. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float ComputeDamageForHit(FName BoneName) const;

	/** Fire rate, ammo and reload state all satisfied. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool CanFire() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void AddAmmo(int32 Rounds);

	/**
	 * Overrides the clock used for the fire-rate check. Automation tests use this because a
	 * component created with NewObject has no world to read GetTimeSeconds() from.
	 */
	void SetTestTimeSeconds(double InSeconds);

	/** Muzzle flash, tracer, sound, recoil. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon")
	void OnWeaponFired(const FHitResult& Hit, bool bHitSomething);

	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon")
	void OnReloadStarted(float Duration);

	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon")
	void OnReloadFinished();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Current time for the fire-rate check: the test clock if one is set, else the world clock. */
	virtual double GetNowSeconds() const;

	/** View point the shot originates from (player camera when the owner is player controlled). */
	void GetFireViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	/** Where a punch starts: the owner's eyes, looking along its view rotation. Never the camera. */
	void GetMeleeViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	/** Double precision so a test clock advanced by exactly one interval is not rejected. */
	double GetSecondsBetweenShots() const { return 60.0 / FMath::Max(static_cast<double>(FireRate), 1.0); }

	/** Runs the hitscan trace and applies damage. Skipped when the component has no world. */
	void TraceAndApplyDamage();

	/** The muzzle flash and the tracer for a shot along Direction that ended at End. */
	void PlayShotEffects(const FVector& ViewLocation, const FVector& Direction, const FVector& End) const;

	/** Copies ActiveDefinition's stats onto this component's own tuning properties. */
	void ApplyStatsFromDefinition();

	/** One punch: a short sphere sweep forward, melee damage on the first thing it meets. */
	bool FireMelee();

	/**
	 * Bone the shot should be scored against.
	 *
	 * A capsule hit carries no bone name, and the capsule is what a bullet usually meets first,
	 * so the hit is refined against the victim's skeletal mesh to find out whether the ray
	 * really went through the head. Returns Hit.BoneName when there is nothing to refine.
	 */
	FName ResolveHitBone(const FHitResult& Hit, const FVector& TraceStart, const FVector& TraceEnd) const;

private:
	bool bIsReloading = false;

	/** Runtime only: AHawkeyeCharacter drives this from the Aim input action. */
	UPROPERTY(Transient)
	bool bIsAiming = false;

	/** Seeded from the owner's name at BeginPlay so two thugs do not fire identical patterns. */
	UPROPERTY(Transient)
	FRandomStream SpreadStream;

	double LastFireTimeSeconds = TNumericLimits<double>::Lowest();
	FTimerHandle ReloadTimerHandle;

	bool bUseTestTime = false;
	double TestTimeOverride = 0.0;
};
