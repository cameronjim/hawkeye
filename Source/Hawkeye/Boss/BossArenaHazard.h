#pragma once

#include "CoreMinimal.h"
#include "Boss/BossTypes.h"
#include "GameFramework/Actor.h"
#include "BossArenaHazard.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A boss arena's moving hazard (claude-docs/gameplay-semantics.md, "Bosses"): a big body that sits tethered at one end
 * of a lane until it comes loose, then sweeps the lane back and forth on UBossRules::HazardStateAt's clock, a lit
 * strip on the floor and a red light telegraphing every sweep. It hurts Kate only while it moves and only in the lane,
 * once per HitCooldownSeconds, and shoves her out of the way. Spawned by UBossFightSubsystem from the arena's data;
 * the body is whatever mesh the arena names (chapter 4: the inflatable Santa).
 */
UCLASS()
class HAWKEYE_API ABossArenaHazard : public AActor
{
	GENERATED_BODY()

public:
	ABossArenaHazard();

	/** The lane, its body and its warning strip, and the clock's tuning. Puts it at the lane's start, tethered. */
	void Configure(const FVector& InStart, const FVector& InEnd, UStaticMesh* BodyMesh, UMaterialInterface* LaneMaterial,
		const FBossHazardTuning& InTuning);

	/** Comes loose: the clock starts from 0 (or from SecondsIn, a restore). */
	void SetLoose(bool bInLoose, float SecondsIn = 0.f);

	/** Stops where it is (the fight is over): no more sweeps, no more hits, the body stays put. */
	void Settle();

	/** One step of the clock and the hit check (Tick calls it; tests call it). */
	void AdvanceHazard(float DeltaSeconds);

	bool IsLoose() const { return bLoose; }
	const FBossHazardState& GetState() const { return State; }
	float GetSeconds() const { return Seconds; }
	int32 GetHitCount() const { return HitCount; }
	float GetDamageDealt() const { return DamageDealt; }
	/** Seconds of the telegraph that came before the sweep that last hit her (0 before any hit). */
	float GetTelegraphBeforeLastHit() const { return TelegraphBeforeLastHit; }
	FVector GetBodyLocation() const;
	FVector GetLaneStart() const { return Start; }
	FVector GetLaneEnd() const { return End; }
	const FBossHazardTuning& GetTuning() const { return Tuning; }

	/** Whom it can hurt: this pawn when set (tests), else the first player's. */
	TWeakObjectPtr<APawn> TargetOverride;

	/** Holds its clock where it is (a scripted beat): nothing moves, nothing is hit, the lane goes dark. */
	void SetHeld(bool bInHeld) { bHeld = bInHeld; }
	bool IsHeld() const { return bHeld; }

	/** Pinned for PinFor seconds (a putty arrow): its clock stops as SetHeld's does, then runs on from where it was. */
	void Pin(float PinFor);
	bool IsPinned() const { return PinRemaining > 0.f; }
	float GetPinRemaining() const { return PinRemaining; }
	int32 GetPinCount() const { return PinCount; }

	/** A prop it shoves along the lane when its sweep reaches it (FBossArenaProp::bHazardPushes), Radius its half size. */
	void AddPushable(AActor* Prop, float Radius);
	/** Times it has shoved a prop on. */
	int32 GetPushCount() const { return PushCount; }

	/**
	 * Its sweep reaches Pawn now: it sweeps (not held, not pinned), Pawn's feet are in the lane by UBossRules::HazardHits
	 * and within the height Kate's hit uses. For a victim other than Kate (the boss), with that victim's own cooldown.
	 */
	bool Reaches(const APawn* Pawn, float SecondsSinceLastHit) const;

	/** +1 while the sweep goes start to end (and resting there), -1 coming back. */
	float GetSweepDirection() const { return State.Sweeps % 2 == 0 ? 1.f : -1.f; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Hazard")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Hazard")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Hazard")
	TObjectPtr<UStaticMeshComponent> Lane;

	UPROPERTY(VisibleAnywhere, Category = "Hazard")
	TObjectPtr<UPointLightComponent> WarningLight;

	/** The lit strip's mesh (the engine cube), scaled to the lane. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LaneMesh = nullptr;

private:
	APawn* FindTarget() const;
	void PlaceBody(float DeltaSeconds);
	void CheckHit(APawn* Target);
	void PushProps();
	/** Pawn's feet from the body: across the lane, along it, and up. */
	void Offsets(const APawn* Pawn, float& OutAcross, float& OutAlong, float& OutUp) const;

	struct FPushable
	{
		TWeakObjectPtr<AActor> Actor;
		float Radius = 30.f;
	};
	TArray<FPushable> Pushables;
	bool bHeld = false;
	float PinRemaining = 0.f;
	int32 PinCount = 0;
	int32 PushCount = 0;

	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FBossHazardTuning Tuning;
	FBossHazardState State;
	bool bLoose = false;
	float Seconds = 0.f;
	float SinceLastHit = 1000.f;
	int32 HitCount = 0;
	float DamageDealt = 0.f;
	float TelegraphBeforeLastHit = 0.f;
	float TelegraphSeenThisCycle = 0.f;
	float WobbleTime = 0.f;
};
