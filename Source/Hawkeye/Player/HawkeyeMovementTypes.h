#pragma once

#include "CoreMinimal.h"
#include "HawkeyeMovementTypes.generated.h"

class AActor;

/**
 * How the player is moving across the ground. Each gait has its own top speed on
 * AHawkeyeCharacter; the character picks one from the input every frame (SelectGait).
 */
UENUM(BlueprintType)
enum class EHawkeyeGait : uint8
{
	/** Light stick input, or aiming. */
	Walk,
	/** The default: keyboard input, a full stick, or any stick held for a moment. */
	Run,
	/** Sprint held while moving. */
	Sprint,
	/** Crouched and not sliding. */
	Crouch,
	/** Crouch pressed mid-sprint: a short, fast, low slide. */
	Slide,
};

/** The traversal move an obstacle calls for (claude-docs/gameplay-semantics.md, traversal). */
UENUM(BlueprintType)
enum class EHawkeyeParkourMove : uint8
{
	/** Nothing to do: too low, too high, or no room. A jump press jumps. */
	None,
	/** 60 to 110 cm with room on the far side: over it and down. */
	Vault,
	/** Up to 200 cm with somewhere to stand on (or just over) the top. */
	Mantle,
	/** 200 to 260 cm: jump, hang from the edge, then climb up or drop. */
	LedgeGrab,
	/** Hanging to standing on top. Only ever started from a hang. */
	Climb,
	/** Standing at a roof or landing edge: over it and down to the hang on its outer face. */
	DropToHang,
	/** Hanging, shimmied into a ledge's end with another round an outside or inside corner: 0.4 s onto it. */
	HangCorner,
	/** Hanging, jump with the stick to one side: a sideways leap to another ledge at the same height. */
	HangLeap,
	/** Hanging, jump with the stick back: turn round and hop to a ledge behind her. */
	HangHop,
};

/** What a landing turned into. See AHawkeyeCharacter::ApplyLanding. */
UENUM(BlueprintType)
enum class EHawkeyeLanding : uint8
{
	None,
	/** A controlled drop from above ControlledDropDipHeight: a short camera dip and speed cut. */
	Dip,
	/** Above RollHeight while moving: a forward roll along the move direction. */
	Roll,
	/** Above RollHeight standing still: speed builds from 0 back to a run. */
	Stumble,
};

/** Who is moving the body through a traversal move. */
UENUM(BlueprintType)
enum class EHawkeyeParkourRoute : uint8
{
	None,
	/** The Game Animation Sample's AC_TraversalLogic (motion-warped montage, its own detection). */
	SampleTraversal,
	/** UParkourComponent's own move, run by UHawkeyeMovementComponent's traversal mode. */
	Procedural,
};

/** What the parkour probe found in front of the character. Heights are above the character's feet. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeParkourObstacle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	bool bFound = false;

	/** Where the probe met the obstacle's front face. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector WallPoint = FVector::ZeroVector;

	/** Horizontal, out of the front face, back towards the character. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector WallNormal = FVector::ZeroVector;

	/** The top front edge: on the top surface, directly above WallPoint. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector LedgePoint = FVector::ZeroVector;

	/** Top of the obstacle above the feet, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	float Height = 0.f;

	/** Horizontal gap between the capsule's surface and the front face, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	float Distance = 0.f;

	/** Front face to back edge, cm; 0 when no back edge was found within reach. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	float Depth = 0.f;

	/** A back edge within vault reach and a floor beyond it the capsule fits on. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	bool bClearBeyond = false;

	/** Feet location on the far side (valid with bClearBeyond). */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector LandingPoint = FVector::ZeroVector;

	/**
	 * How far below the feet the ground just past the back edge is, cm, whether or not she could land there; the
	 * whole probe depth (800) when there is no ground at all. 0 without a back edge. The roof-edge guard reads it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	float FarSideDrop = 0.f;

	/** How far the far-side floor is below the feet now, cm (negative: higher). */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	float LandingDrop = 0.f;

	/** The capsule fits on the top, or just over a thin top (a parapet) onto a floor close below it. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	bool bStandingSurface = false;

	/** Feet location a mantle or climb ends at (valid with bStandingSurface). */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector StandPoint = FVector::ZeroVector;

	/** The actor that owns the front face. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	TObjectPtr<AActor> Actor = nullptr;

	/** Why bClearBeyond is false (no back edge, no floor, the landing blocked), for the refusal log. */
	FString BeyondWhyNot;

	/** Why the top is no standing surface when it is flat there (something in the way), for the refusal log. */
	FString StandWhyNot;
};

/**
 * The ledge a hang is on, as a straight line along its top edge (claude-docs/gameplay-semantics.md, traversal,
 * "Hang"): where along it the hands can go. Origin is on the edge at the top's height; Along is her right while
 * she hangs facing the wall; MinAlong and MaxAlong are the edge's ends, cm along it from Origin.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeLedgeLine
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector Origin = FVector::ZeroVector;

	/** Horizontal unit vector along the edge: Up x (-Normal), her right as she faces the wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector Along = FVector::RightVector;

	/** Horizontal, out of the face (away from the wall). */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	FVector Normal = -FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	float MinAlong = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	float MaxAlong = 0.f;

	/** The ledge splines it came from and how many were joined end to end; 0 when it was probed along the wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Parkour")
	int32 DataSegments = 0;

	/** Where it came from, for the log: "Ledge_1 on City_Ledge_way/123_2 (+1 more)" or "probed along the wall". */
	FString Source;

	/** What ends it at MinAlong and at MaxAlong ("the top runs out", "Chimney_3 on the top"), for the log. */
	FString MinEndWhy;
	FString MaxEndWhy;

	FVector PointAt(float Distance) const { return Origin + Along * Distance; }

	float AlongOf(const FVector& Point) const
	{
		return static_cast<float>(FVector::DotProduct(FVector(Point.X - Origin.X, Point.Y - Origin.Y, 0.f), Along));
	}

	float Length() const { return MaxAlong - MinAlong; }
};

/** Where the spring arm and lens want to be for one aim state. The camera blends between two of these. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeCameraTargets
{
	GENERATED_BODY()

	FHawkeyeCameraTargets() = default;

	FHawkeyeCameraTargets(float InArmLength, const FVector& InSocketOffset, float InFieldOfView, float InPivotLift = 0.f)
		: ArmLength(InArmLength), SocketOffset(InSocketOffset), FieldOfView(InFieldOfView), PivotLift(InPivotLift)
	{
	}

	/** Spring arm length, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float ArmLength = 350.f;

	/** Offset of the camera at the end of the arm, in the arm's space (Y is right, Z is up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector SocketOffset = FVector(0.f, 0.f, 60.f);

	/** Horizontal field of view, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float FieldOfView = 90.f;

	/** World-up lift of the arm's pivot, cm (the spring arm's TargetOffset.Z). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float PivotLift = 0.f;

	/** Straight blend between two targets; Alpha 0 is A, 1 is B. */
	static FHawkeyeCameraTargets Lerp(const FHawkeyeCameraTargets& A, const FHawkeyeCameraTargets& B, float Alpha)
	{
		FHawkeyeCameraTargets Out;
		Out.ArmLength = FMath::Lerp(A.ArmLength, B.ArmLength, Alpha);
		Out.SocketOffset = FMath::Lerp(A.SocketOffset, B.SocketOffset, Alpha);
		Out.FieldOfView = FMath::Lerp(A.FieldOfView, B.FieldOfView, Alpha);
		Out.PivotLift = FMath::Lerp(A.PivotLift, B.PivotLift, Alpha);
		return Out;
	}
};

/** UHawkeyeMovementComponent's custom movement modes (MOVE_Custom's sub-mode). */
UENUM(BlueprintType)
enum class EHawkeyeCustomMovement : uint8
{
	None,
	/** A grapple zip: a hop, then a straight line to the anchor's landing point (PhysZip). */
	Zip,
	/** A parkour move or a hang (PhysTraversal). */
	Traversal,
};

/** How a traversal move leaves the mode when its time is up. */
UENUM()
enum class EHawkeyeTraversalExit : uint8
{
	/** Stays in the mode, held still at the path's end: a hang. */
	Hold,
	/** Walking on the floor under the end, or Falling when there is none. */
	Walk,
	/** Falling. */
	Fall,
};

/** Why UHawkeyeMovementComponent::OnTraversalEnded fired. */
UENUM()
enum class EHawkeyeTraversalEnd : uint8
{
	/** The move's time ran out and it ended in a hold. */
	Held,
	/** The move's time ran out and it left the mode (Walk or Fall). */
	Exited,
	/** ReleaseTraversal let go. */
	Released,
	/** Something else changed the movement mode. */
	Interrupted,
};

/** One point of a traversal path: the capsule centre and the facing (yaw in degrees, unwound so turns interpolate). */
USTRUCT()
struct HAWKEYE_API FHawkeyeTraversalKey
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	float Yaw = 0.f;
};

/** A parkour move as UParkourComponent hands it to UHawkeyeMovementComponent::StartTraversal. */
USTRUCT()
struct HAWKEYE_API FHawkeyeTraversalMove
{
	GENERATED_BODY()

	/** Evenly spaced in time, the first where she starts and the last where she ends. Empty: root motion moves her. */
	TArray<FHawkeyeTraversalKey> Keys;

	/** How long the move lasts, s. */
	float Seconds = 0.f;

	/** What the sweep passes through, for this move only: the obstacle it climbs over, the walls of a hang. */
	TArray<TWeakObjectPtr<AActor>> PassThrough;

	EHawkeyeTraversalExit Exit = EHawkeyeTraversalExit::Walk;

	/** The velocity she leaves with (Walk or Fall). */
	FVector ExitVelocity = FVector::ZeroVector;

	/** Where the path is at Alpha (0..1 of the move's time), interpolated between its keys. */
	FHawkeyeTraversalKey Sample(float Alpha) const
	{
		if (Keys.Num() < 2)
		{
			return Keys.Num() == 1 ? Keys[0] : FHawkeyeTraversalKey();
		}
		const float Scaled = FMath::Clamp(Alpha, 0.f, 1.f) * (Keys.Num() - 1);
		const int32 Index = FMath::Min(FMath::FloorToInt32(Scaled), Keys.Num() - 2);
		const float Blend = Scaled - Index;
		FHawkeyeTraversalKey Out;
		Out.Location = FMath::Lerp(Keys[Index].Location, Keys[Index + 1].Location, Blend);
		Out.Yaw = FMath::Lerp(Keys[Index].Yaw, Keys[Index + 1].Yaw, Blend);
		return Out;
	}
};

/** How a zip ended; UHawkeyeMovementComponent::OnZipEnded passes it to the grapple. */
UENUM(BlueprintType)
enum class EHawkeyeZipEnd : uint8
{
	/** On the landing point: Walking (Falling with no floor there), carrying some of the speed. */
	Arrived,
	/** Something on the line the grapple would not let her through: Falling, no velocity. */
	Blocked,
	/** Let go with jump or crouch: Falling with the zip's velocity. */
	Released,
	/** Something else changed the movement mode. */
	Interrupted,
};

/** What the grapple says about a hit on the zip's sweep (UHawkeyeMovementComponent::OnZipHit). */
UENUM()
enum class EHawkeyeZipHitResponse : uint8
{
	/** The zip is blocked. */
	Stop,
	/** The grapple stopped colliding with what was hit: sweep the rest of the step again. */
	Retry,
	/** A chain's turn clipped something off the line: finish the turn and sweep along the line itself. */
	Straighten,
};

/** The straight line a zip runs along, as the grapple works it out. */
USTRUCT()
struct HAWKEYE_API FHawkeyeZipLine
{
	GENERATED_BODY()

	/** Where the straight line starts (the capsule centre). */
	FVector Launch = FVector::ZeroVector;

	/** The capsule centre on the anchor's landing point. */
	FVector End = FVector::ZeroVector;

	/** Ease up from where she is to Launch first (a zip from the ground). */
	bool bHop = false;
};
