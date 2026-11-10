#pragma once

#include "CoreMinimal.h"
#include "ChaseTypes.generated.h"

class UStaticMesh;

/** How the thief reaches a waypoint from the one before it (claude-docs/gameplay-semantics.md, "Chases"). */
UENUM(BlueprintType)
enum class EChaseLeg : uint8
{
	/** On his feet along the line: the only leg the rubber band speeds up or slows down. */
	Run,
	/** Over something waist high (a parapet, a vent): a short hop, 0.5 s. */
	Vault,
	/** Up onto something higher (a neighbour's roof a step up): 0.8 s. */
	Climb,
	/** A leap across a gap between roofs: an arc whose time grows with its length. */
	JumpGap,
	/** Down to a lower level (a fire escape to the street): the height at a fixed speed. */
	Drop
};

/** Where a chase stands. */
UENUM(BlueprintType)
enum class EChaseState : uint8
{
	/** No chase in this world. */
	Idle,
	/** The thief is running the route. */
	Running,
	/** The dumpster beat is playing; the lose clock is held. */
	Beat,
	/** He reached the route's end and has turned his back on her. */
	Cornered,
	/** Taken down or downed; the chase's objectives complete once its line has spoken. */
	Caught,
	/** He was too far for too long; the checkpoint reloads. */
	Lost
};

/** Which band the lead is in, for the rubber band and the "pulls ahead" line. */
UENUM(BlueprintType)
enum class EChaseLeadBand : uint8
{
	/** On his heels: he finds a little more. */
	Close,
	/** The chase as it should feel. */
	Steady,
	/** Falling behind: he eases off a little. */
	Far,
	/** Too far: the lose clock runs. */
	Losing
};

/** One point of the thief's route: his feet, and how he gets there from the point before. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChaseWaypoint
{
	GENERATED_BODY()

	/** His feet here, world space, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FVector Location = FVector::ZeroVector;

	/** How he reaches this point from the previous one. Ignored on the first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	EChaseLeg Leg = EChaseLeg::Run;

	/** Completed when Kate's progress reaches this point (less the route's ReachTolerance). None for nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FName CompletesObjective;

	/** A DT_DialogueSequences sequence played when Kate's progress reaches this point. None for nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FName Sequence;
};

/**
 * One grapple the route means Kate to make, for the scripted lap, the verifier and the reader: where she
 * stands, the anchor she fires at, and for a chain the anchor she chains to mid-zip. The game does not
 * require it; the reach model proved it can be done.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChaseGrapple
{
	GENERATED_BODY()

	/** Her feet where she fires from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FVector Stand = FVector::ZeroVector;

	/** The anchor's marker point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FVector Anchor = FVector::ZeroVector;

	/** A chain: the second anchor's marker point, fired at mid-zip. Zero for a plain grapple. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FVector ChainAnchor = FVector::ZeroVector;

	/** How far along the first line (0..1) the chain is planned from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	float ChainAt = 0.f;

	/** The botched grapple of the beat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	bool bBeat = false;

	bool IsChain() const { return !ChainAnchor.IsZero(); }
};

/** A piece of furniture the route places: a mesh at a transform (its origin on the roof). */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChaseProp
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase")
	TSoftObjectPtr<UStaticMesh> Mesh;
};

/** The numbers the chase's rules read. Defaults are the semantics doc's. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChaseTuning
{
	GENERATED_BODY()

	/** The thief's speed on Run legs at a steady lead, cm/s. Kate sprints at 700. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "100.0"))
	float BaseSpeed = 620.f;

	/** Under this lead (cm) he finds CloseBoost more speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float CloseLead = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "1.0"))
	float CloseBoost = 1.15f;

	/** From this lead (cm) he eases off, down to MinSpeedScale at LoseLead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float FarLead = 2500.f;

	/** At and past this lead (cm) the lose clock runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float LoseLead = 4500.f;

	/** The slowest he ever runs, as a fraction of BaseSpeed. He never stops. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MinSpeedScale = 0.8f;

	/** How fast his speed may change, cm/s per second, so a change never reads as a jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "1.0"))
	float SpeedChangeRate = 400.f;

	/** Seconds at LoseLead or more before the chase is lost. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.5"))
	float LoseSeconds = 6.f;

	/** How far ahead of Kate (along the route) he starts, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float StartLead = 1100.f;

	/** After the beat he is put this far ahead of her progress, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float ResumeLead = 1600.f;

	/** A load never puts him further ahead of her than this, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float RestoreMaxLead = 2200.f;

	/** Her progress looks this far back of where it was, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float BackWindow = 1500.f;

	/** ...and this far ahead (a grapple can carry her a long way), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float AheadWindow = 6000.f;

	/** Further than this (3D, cm) from every route point she is off the route. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "100.0"))
	float OffRouteDistance = 2000.f;

	/** A waypoint's objective or line comes this far before her progress reaches it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float ReachTolerance = 300.f;

	/** Longest the caught line may hold the objectives back, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chase|Tuning", meta = (ClampMin = "0.0"))
	float CaughtHoldSeconds = 4.f;
};

/** Where a running chase is, as a save holds it. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FChaseSaveState
{
	GENERATED_BODY()

	/** The route's asset path; empty when no chase was running. */
	UPROPERTY(BlueprintReadOnly, Category = "Chase")
	FString RoutePath;

	UPROPERTY(BlueprintReadOnly, Category = "Chase")
	float ThiefProgress = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Chase")
	float KateProgress = 0.f;

	/** Waypoints whose objective and line have been handled. */
	UPROPERTY(BlueprintReadOnly, Category = "Chase")
	int32 ReachedWaypoints = 0;

	/** The beat played or was passed. */
	UPROPERTY(BlueprintReadOnly, Category = "Chase")
	bool bBeatDone = false;

	/** The "pulls ahead" and "first vault" moments have played. */
	UPROPERTY(BlueprintReadOnly, Category = "Chase")
	bool bPulledAhead = false;

	UPROPERTY(BlueprintReadOnly, Category = "Chase")
	bool bVaulted = false;

	bool IsRunning() const { return !RoutePath.IsEmpty(); }
};
