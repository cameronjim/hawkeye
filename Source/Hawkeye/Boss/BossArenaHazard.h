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
