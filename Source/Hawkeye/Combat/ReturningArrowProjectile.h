#pragma once

#include "CoreMinimal.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/ReturnFlight.h"
#include "ReturningArrowProjectile.generated.h"

/** Where a returning arrow is. */
UENUM(BlueprintType)
enum class EHawkeyeReturnPhase : uint8
{
	/** On the bow, not loosed. */
	Idle,
	Out,
	Back,
	/** Caught, or it hit her; gone. */
	Home
};

/**
 * A returning arrow in flight (the boomerang; claude-docs/gameplay-semantics.md, "Trick arrows", returning arrows): it
 * moves itself (no projectile movement, no drop) by its definition's FHawkeyeReturnFlight. Out: straight, sweeping, every
 * body it meets hurt and staggered (UHawkeyeReturnFlightRules::CanHitMore) and passed through; a wall, a switch, its
 * last target or its range turns it (TurnBack), and the path it flew is remembered. Back: along that path, then to her,
 * through anything solid, hurting bodies not hit yet while it can. At her: caught when she faces it and is not busy
 * (IsCaught: the chapter event "arrow_caught:<asset>"), else it hits her: a stagger and SelfHitDamage, which never takes
 * her under 1 health ("arrow_self_hit:<asset>"). Either way it goes back into her quiver.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AReturningArrowProjectile : public AArrowProjectile
{
	GENERATED_BODY()

public:
	AReturningArrowProjectile();

	virtual void LaunchWithVelocity(const FVector& Velocity) override;

	/** Moves the flight DeltaSeconds on; Tick calls it, a test with no ticking does. */
	void AdvanceReturn(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Arrow|Return")
	EHawkeyeReturnPhase GetReturnPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "Arrow|Return")
	int32 GetHits() const { return HitActors.Num(); }

	/** Why it turned (None while still out). */
	UFUNCTION(BlueprintPure, Category = "Arrow|Return")
	EHawkeyeReturnTurn GetTurnReason() const { return TurnReason; }

	/** How it ended: caught (true) or it hit her (false). Valid once home. */
	UFUNCTION(BlueprintPure, Category = "Arrow|Return")
	bool WasCaught() const { return bCaught; }

	const TArray<FVector>& GetOutPath() const { return OutPath; }

	/** "arrow_caught:<asset>" and "arrow_self_hit:<asset>". */
	static FName CaughtEvent(const UObject* Arrow);
	static FName SelfHitEvent(const UObject* Arrow);

	/** Every returning arrow that came home since the process started, caught or not (the lap reads these). */
	static int32 CaughtCount;
	static int32 SelfHitCount;

protected:
	virtual void Tick(float DeltaSeconds) override;

	const FHawkeyeReturnFlight& Tuning() const;

	/** Her chest, the way back's end. */
	FVector CatcherPoint() const;

	/** A body (something with health, alive, not her): hurt it if the flight still can and it has not been hurt. */
	bool TryHurt(const FHitResult& Hit, const FVector& Direction);

	void Turn(EHawkeyeReturnTurn Why);
	void ArriveHome();

	EHawkeyeReturnPhase Phase = EHawkeyeReturnPhase::Idle;
	EHawkeyeReturnTurn TurnReason = EHawkeyeReturnTurn::None;
	FVector Direction = FVector::ForwardVector;
	TArray<FVector> OutPath;
	float DistanceOut = 0.f;
	float SinceLastPoint = 0.f;
	float Travelled = 0.f;
	float FlightSeconds = 0.f;
	FVector LastStep = FVector::ForwardVector;
	TArray<TWeakObjectPtr<AActor>> HitActors;
	bool bCaught = false;
};
