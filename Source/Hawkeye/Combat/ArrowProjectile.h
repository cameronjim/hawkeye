#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArrowProjectile.generated.h"

class UArrowDefinition;
class UAudioComponent;
class UBowComponent;
class UBowDefinition;
class UNiagaraComponent;
class UProjectileMovementComponent;
class USkeletalMeshComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * An arrow in flight, then stuck in whatever it hit (claude-docs/gameplay-semantics.md, "bow
 * and arrows"). A projectile with gravity, launched at the speed the draw earned and turned along
 * its velocity every tick. The collision sphere at the tip sweeps as WorldDynamic, blocking world
 * geometry and pawns but not the hidden traversal ledges, which answer only the Weapon trace
 * channel; the hit is then refined against the victim's physics bodies to find the bone, which is
 * where the headshot multiplier comes from.
 *
 * On hit: ApplyPointDamage, a stagger through the victim's health component, the bow's OnHit;
 * then it embeds, attached to the component (and bone) it hit, and stays for StuckLifeSeconds
 * or until a recoverable arrow is walked over within RecoverRadius. A trick arrow also spawns its
 * AArrowEffect at the impact; the explosive's damage is the blast's alone, and the blast destroys it.
 *
 * BP_Arrow_Standard is this class; BP_Arrow_Grapple is AGrappleArrowProjectile.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AArrowProjectile : public AActor
{
	GENERATED_BODY()

public:
	AArrowProjectile();

	/** How long a stuck arrow stays in the world, seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "0.0"))
	float StuckLifeSeconds = 30.f;

	/** A recoverable stuck arrow goes back in the quiver when the player is this close, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "0.0"))
	float RecoverRadius = 150.f;

	/** How far the tip sinks past the surface it hit, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "0.0"))
	float EmbedDepth = 12.f;

	/** How far the tip sinks into a body (Kate, a thug) past the surface of the physics shape it entered, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "0.0"))
	float BodyEmbedDepth = 5.f;

	/**
	 * Where an arrow flying from Start along Direction meets Body (gameplay-semantics.md, "Arrows in bodies"):
	 * the first point on the line, up to Length on, inside one of its physics asset's shapes at the current
	 * bone transforms (bEntered), else the point of the nearest shape to the line, else (no physics asset) the
	 * line's closest approach to the nearest bone. False when Body has neither. Pure but for reading the pose.
	 */
	static bool FindBodyEntry(const USkeletalMeshComponent& Body, const FVector& Start, const FVector& Direction, float Length,
		FVector& OutPoint, FName& OutBone, bool& bOutEntered);

	/**
	 * True when Victim is the other of the Kate and Clint pair from Shooter (both player characters, not the same
	 * one): partner fire (gameplay-semantics.md, "Arrows in bodies"). Their arrows fly through each other.
	 */
	static bool IsPartnerOf(const AActor* Shooter, const AActor* Victim);

	/** The skeletal mesh an arrow sticks into on Actor: a character's own mesh, else the first one it has. */
	static USkeletalMeshComponent* FindBodyMesh(const AActor* Actor);

	/** Length of the shaft behind the tip, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "1.0"))
	float ShaftLength = 100.f;

	/** Shaft thickness, cm. Thick enough that an arrow stuck in a wall reads from behind. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow", meta = (ClampMin = "0.1"))
	float ShaftThickness = 2.5f;

	/** Shaft colour: light wood, so it reads against dark walls and tracksuits. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	FLinearColor ShaftColor = FLinearColor(0.6f, 0.55f, 0.45f);

	/** Nock colour: Kate's purple on every arrow she shoots, whatever its fletching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	FLinearColor NockColor = FLinearColor(0.45f, 0.1f, 0.75f);

	/**
	 * The nock's material: a Color parameter that also glows (M_ArrowNock, built by
	 * Tools/Editor/create_weapon_data.py). The plain shape material when it is not there.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	TSoftObjectPtr<UMaterialInterface> NockMaterial;

	/** Where NockMaterial points by default; the bow's nocked arrow uses the same one. */
	static const TCHAR* const DefaultNockMaterialPath;

	/**
	 * Sets what this arrow is and who shot it. Damage is the release damage (draw and perfect bonus
	 * already applied); Bow supplies the headshot rule; Source hears about the hit.
	 */
	void InitArrow(UArrowDefinition* InArrow, UBowDefinition* InBow, float InDamage, AActor* InShooter,
		UBowComponent* InSource);

	/** Sends the arrow off along Velocity (cm/s), gravity on. */
	UFUNCTION(BlueprintCallable, Category = "Arrow")
	virtual void LaunchWithVelocity(const FVector& Velocity);

	/** Runs the projectile movement DeltaSeconds on. The engine tick does this; a test with no ticking calls it. */
	void AdvanceFlight(float DeltaSeconds);

	/** Damages, embeds and reports a blocking hit. The movement component calls it; public for tests. */
	UFUNCTION()
	void HandleImpact(const FHitResult& Hit);

	/**
	 * Hands the arrow back to Collector's quiver when it is stuck, recoverable and within
	 * RecoverRadius, and destroys it. Returns true when it was recovered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arrow")
	bool TryRecoverBy(AActor* Collector);

	UFUNCTION(BlueprintPure, Category = "Arrow")
	bool IsStuck() const { return bStuck; }

	UFUNCTION(BlueprintPure, Category = "Arrow")
	bool IsInFlight() const { return bInFlight; }

	UFUNCTION(BlueprintPure, Category = "Arrow")
	UArrowDefinition* GetArrowDefinition() const { return Arrow; }

	UFUNCTION(BlueprintPure, Category = "Arrow")
	float GetDamage() const { return Damage; }

	/** The actor the arrow is stuck in, or null. */
	UFUNCTION(BlueprintPure, Category = "Arrow")
	AActor* GetStuckInActor() const { return StuckIn.Get(); }

	UFUNCTION(BlueprintPure, Category = "Arrow")
	UProjectileMovementComponent* GetProjectileMovement() const { return Movement; }

protected:
	virtual void Tick(float DeltaSeconds) override;

	/** The bone the arrow really passed through, found against the victim's physics bodies. */
	FName ResolveHitBone(const FHitResult& Hit, const FVector& Direction) const;

	/** Point damage, stagger and the bow's OnHit for a hit on something with health. */
	void DamageVictim(const FHitResult& Hit, const FVector& Direction, FName Bone);

	/** Spawns the arrow's trick effect (putty, bola, smoke, EMP, explosive) at the impact. */
	void SpawnHitEffect(const FHitResult& Hit);

	/**
	 * Stops, sinks EmbedDepth past the impact and attaches to the hit bone, or else the hit component. With a
	 * body entry (FindBodyEntry) it goes BodyEmbedDepth into that point instead and onto EntryBone.
	 */
	void Embed(const FHitResult& Hit, const FVector& Direction, FName Bone, const FVector* EntryPoint = nullptr,
		FName EntryBone = NAME_None);

	/** Turns the arrow to face along its velocity. */
	void FaceVelocity();

	/** Tip. Root and the only thing that collides. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<USphereComponent> Collision;

	/** Thin cylinder trailing ShaftLength behind the tip. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> Shaft;

	/** Three vanes at the tail, 120 degrees apart: flat cubes standing out from the shaft. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> Fletching;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> Fletching2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> Fletching3;

	/** The purple nock on the very end. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UStaticMeshComponent> Nock;

	virtual void PostInitializeComponents() override;

	/** Sizes and places the shaft, vanes and nock from ShaftLength and ShaftThickness. */
	void LayoutParts();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arrow")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(Transient)
	TObjectPtr<UArrowDefinition> Arrow = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBowDefinition> Bow = nullptr;

	TWeakObjectPtr<AActor> Shooter;
	TWeakObjectPtr<UBowComponent> Source;
	TWeakObjectPtr<AActor> StuckIn;

	/** The whistle riding on the arrow; cut off when it lands. */
	TWeakObjectPtr<UAudioComponent> Whistle;

	/** The source bow's impact sound and effect for what Hit struck, at the impact. */
	void PlayImpactSound(const FHitResult& Hit) const;

	/** The trail riding on the arrow in flight; let go (to fade) when it lands. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Trail = nullptr;

	float Damage = 0.f;
	bool bInFlight = false;
	bool bStuck = false;

	/** Fletching colour; purple for Kate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Arrow")
	FLinearColor FletchingColor = FLinearColor(0.45f, 0.1f, 0.75f);
};
