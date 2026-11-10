#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimEnums.h"
#include "Components/ActorComponent.h"
#include "Player/HawkeyeMovementTypes.h"
#include "ParkourComponent.generated.h"

class ACharacter;
struct FHawkeyeStrikePoseSample;
class UAnimMontage;
class UAnimSequenceBase;
class UCharacterMovementComponent;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnParkourStartedSignature, EHawkeyeParkourMove, Move, EHawkeyeParkourRoute, Route);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnParkourFinishedSignature, EHawkeyeParkourMove, Move);

/** A stretch of one of the sample's traversal clips that poses the body for a move. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeParkourClip
{
	GENERATED_BODY()

	/** Played in the full-body slot with root motion ignored; the capsule follows its root motion, warped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour")
	TSoftObjectPtr<UAnimSequenceBase> Sequence;

	/** Clip time the move starts at, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour", meta = (ClampMin = "0.0"))
	float StartTime = 0.f;

	/** Clip time the move ends at, s. The stretch is played faster or slower to fit the move's duration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour", meta = (ClampMin = "0.0"))
	float EndTime = 1.f;
};

/**
 * Vault, mantle and ledge grab (claude-docs/gameplay-semantics.md, traversal).
 *
 * A few traces ahead of the character find the front face, the top, whether the capsule fits on
 * (or just over) the top, and whether there is a back edge with a floor beyond it; ChooseMove
 * turns the numbers into a move. Sprinting into an obstacle within AutoTriggerDistance starts it
 * on its own; the jump key starts it at any speed. Vault and mantle go to the Game Animation
 * Sample's traversal first (GaspTraversal.h) when the owner carries it; when the sample finds no
 * traversable block or no montage fits, or the owner has no sample, this component moves the
 * capsule itself: along the root motion of a sample clip (or a plain arc when there is no
 * animation), warped so it starts where the character is and ends on the far side, on top, or
 * at the hang point. Input is locked exactly as long as the move lasts.
 *
 * The ledge grab has no sample equivalent: the character jumps to a hang (the sample's 2.5 m
 * climb clip paused where the hands reach the edge), then the jump key climbs and crouch drops.
 * Falling past a ledge between CatchMinHeight and CatchMaxHeight above the feet catches it.
 *
 * The jump key is forgiving (2026-09-28): it probes a fan of rays around the stick direction (or
 * the facing), reaches ManualTriggerDistance, and takes tops from JumpMinHeight. When nothing fits
 * and a plain jump starts, the late catch re-probes every tick for LateCatchSeconds and takes a
 * mantle or a ledge the moment one fits; a press during a move is buffered for JumpBufferSeconds
 * and fires when the move ends.
 *
 * The way down: standing within DropToHangReach of an edge with more than DropToHangMinDrop
 * beyond it (a roof edge over its parapet, a fire-escape landing over its rail, or any walkable
 * edge), TryDropToHang goes over it to the same hang on its outer face. A drop from that hang
 * catches the next ledge below, so a fire escape comes down as hang, drop, catch, drop.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UParkourComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParkourComponent();

	/**
	 * The move for an obstacle Height cm above the feet: vault at VaultMinHeight to VaultMaxHeight
	 * when clear beyond (else mantle if there is somewhere to stand), mantle up to MantleMaxHeight
	 * with a standing surface, ledge grab up to LedgeMaxHeight, otherwise none.
	 */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	EHawkeyeParkourMove ChooseMove(float Height, bool bClearBeyond, bool bStandingSurface) const;

	/**
	 * ChooseMove for a probed obstacle. A vault whose far floor is more than MaxAutoVaultDrop
	 * (auto) or MaxVaultDrop (jump key) below the feet counts as not clear: sprinting at a parapet
	 * never throws the character off the roof.
	 */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	EHawkeyeParkourMove ChooseMoveFor(const FHawkeyeParkourObstacle& Obstacle, bool bAuto) const;

	/** Probes up to MaxDistance cm ahead of the capsule. False when there is no usable front face. */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool DetectObstacle(float MaxDistance, FHawkeyeParkourObstacle& OutObstacle) const;

	/** DetectObstacle for faces and tops up to MaxHeight above the feet (the catch looks higher). */
	bool DetectObstacleUpTo(float MaxDistance, float MaxHeight, FHawkeyeParkourObstacle& OutObstacle) const;

	/** The ledge the last drop let go of is still ignored (the fall has not ended). */
	bool IsIgnoringDroppedLedge() const { return bIgnoreDroppedLedge; }

	/** DetectObstacleUpTo along Direction (flattened) instead of the facing. */
	bool DetectObstacleAlong(const FVector& Direction, float MaxDistance, float MaxHeight, FHawkeyeParkourObstacle& OutObstacle,
		float LowestProbe = -1.f) const;

	/**
	 * The jump key's probe: JumpFanRays rays spread JumpFanDegrees either side of Direction, each
	 * Reach ahead of the capsule. The ray nearest the middle whose obstacle takes a move wins; with
	 * bLateCatch the airborne rules apply (mantle when the top is in reach, else a ledge in the
	 * catch window, auto vault limits). False with OutWhyNot saying why nothing did.
	 */
	bool ProbeJumpFan(const FVector& Direction, float Reach, bool bLateCatch, FHawkeyeParkourObstacle& OutObstacle,
		EHawkeyeParkourMove& OutMove, FString& OutWhyNot, FHawkeyeParkourObstacle* OutRoofEdge = nullptr) const;

	/**
	 * A parapet at a roof edge (the roof-edge guard's case): a thin top (a back edge found) from the jump key's
	 * lowest up to RoofEdgeGuardMaxHeight, with more than RoofEdgeGuardDrop below the feet just past it.
	 */
	bool IsRoofEdgeParapet(const FHawkeyeParkourObstacle& Obstacle) const;

	/** Where the capsule can stand on Parapet's own top (its middle), for the guard's mantle. False when it does not fit. */
	bool FindParapetStand(const FHawkeyeParkourObstacle& Parapet, FVector& OutStandPoint) const;

	/** The last guarded press mantled onto the parapet (true) or became a plain jump held short of it (false). */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsGuardMantle() const { return bGuardMantle; }

	/** The double tap's hop over the parapet is running. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsGuardLeap() const { return bGuardLeap; }

	/** A guarded plain jump is in the air, its speed toward the drop held at zero. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsGuardFlight() const { return bGuardFlight; }

	/** Standing on a parapet top the guard put her on: walking off it is allowed only back toward the roof. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsPerchedOnParapet() const { return bPerched; }

	/** The guard's state for one tick: the double-tap clock, the held flight, the perch. TickComponent calls it. */
	void TickRoofEdgeGuard(float DeltaSeconds);

	/** Where the jump key probes: the movement input when the stick is pushed, else the facing. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	FVector GetJumpProbeDirection() const;

	/** Probe and start whatever fits: bAuto is the sprint trigger, false the jump key (the fan). */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool TryParkour(bool bAuto);

	/** The jump key along Direction: the fan, logging at Log why nothing started. */
	bool TryJumpParkour(const FVector& Direction);

	/** Why the last jump press or late catch did not turn into a move; empty after one that did. */
	const FString& GetLastJumpRefusal() const { return LastJumpRefusal; }

	/** A plain jump has started: re-probe every tick for LateCatchSeconds (the late catch). */
	void ArmLateCatch();

	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsLateCatchArmed() const { return LateCatchRemaining > 0.f; }

	/** One late-catch probe from the air; true when it started a move. */
	bool TryLateCatch();

	/** Jump pressed during a move: it fires when the move ends, if that is within JumpBufferSeconds. */
	void BufferJump();

	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsJumpBuffered() const { return JumpBufferRemaining > 0.f; }

	/** The jump buffer and the late catch for one tick. TickComponent calls it; a test calls it directly. */
	void TickJumpAssist(float DeltaSeconds);

	/** Starts Move over Obstacle with this component moving the capsule (no sample traversal). */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool StartMove(EHawkeyeParkourMove Move, const FHawkeyeParkourObstacle& Obstacle);

	/** Moves the current move DeltaSeconds on. Tick calls it; a test with no ticking calls it directly. */
	void AdvanceMove(float DeltaSeconds);

	/** From a hang: up onto the top. False when not hanging or there is nowhere to stand. */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool ClimbFromHang();

	/**
	 * From a hang: let go and fall. The same ledge is not caught again for DropRegrabSeconds, nor
	 * at all until the fall ends; any other ledge in the catch window is.
	 */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool DropFromHang();

	/**
	 * The edge Direction points at, if there is one to hang from: within DropToHangReach of the
	 * capsule, either a thin top at most MaxLipHeight high (a parapet or a rail) or the floor
	 * simply ending, with more than DropToHangMinDrop beyond it and room to hang on its outer
	 * face. WallNormal points out of that face (away from the character), LedgePoint is its top
	 * edge, LandingDrop the drop beyond, StandPoint where the character stands now.
	 */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool FindDropEdge(const FVector& Direction, FHawkeyeParkourObstacle& OutEdge) const;

	/**
	 * On the ground at an edge along Direction: over it to the hang (crouch moving toward an edge,
	 * or the jump key with the edge behind). False when there is no edge to hang from.
	 */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool TryDropToHang(const FVector& Direction, const FString& Trigger);

	/** Falling with a ledge in reach: catch it. Tick calls this while airborne. */
	UFUNCTION(BlueprintCallable, Category = "Parkour")
	bool TryCatchLedge();

	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsHanging() const { return bHanging; }

	// --- The hang vocabulary (2026-09-29; ParkourHang.cpp) ---------------------------------------
	// Shimmy along the ledge, turn its corners, leap sideways or hop back to another, and the auto
	// climb. There is no hang timeout: she hangs until a press or the stick moves her.

	/** This frame's stick while hanging, as a flat world direction (camera relative); its length is how far it is pushed. */
	void SetHangInput(const FVector& WorldInput);

	/** The hang for one tick: shimmy, a corner, the auto climb, the hands. TickComponent calls it; a test calls it directly. */
	void AdvanceHang(float DeltaSeconds);

	/** The jump key while hanging: the stick to one side leaps, the stick back hops round, otherwise she climbs. */
	bool JumpFromHang();

	/** The ledge line the hang is on (valid while hanging). */
	const FHawkeyeLedgeLine& GetHangLine() const { return HangLine; }

	/** Where the capsule is along the hang's ledge line, cm from its origin. */
	float GetHangAlong() const;

	/** Why the last hang input came to nothing more (a shimmy stopped, a leap refused); empty after one that worked. */
	const FString& GetLastHangRefusal() const { return LastHangRefusal; }

	/** Moving along the ledge this tick. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsShimmying() const { return bShimmying; }

	/** The hang's top is a parapet with more than RoofEdgeGuardDrop below its far side: the stick does not climb it. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsHangTopGuarded() const { return bHangTopGuarded; }

	/** How long the stick has been held up (toward the wall) in this hang, s. */
	float GetHangUpHeldSeconds() const { return HangUpHeld; }

	/**
	 * The hands on the edge while hanging, hand over hand as she shimmies, into Pose (actor frame) when no strike
	 * holds them, and the body's sway into the lean when it is stronger. False when the hang does not own them.
	 */
	bool GetHangArmPose(FHawkeyeStrikePoseSample& InOutPose, FVector& InOutLeanDirection, float& InOutLeanAlpha) const;

	/** Where the hands are along the edge from the capsule, cm (left, right); for the log and the tests. */
	FVector2D GetHangHandOffsets() const;

	/** The hang camera: the shoulder offset goes to the open side of the wall. AHawkeyeCharacter::UpdateCamera calls it. */
	void ApplyHangCamera(FHawkeyeCameraTargets& InOutTargets, float DeltaSeconds);

	/** The control yaw that keeps the camera off the wall: within MaxDegrees of looking straight at it. Pure. */
	static float ComputeHangCameraYaw(float ControlYaw, const FVector& WallNormal, float MaxDegrees);

	/**
	 * Which shoulder the lens sits over while hanging: +1 the right (the default), -1 the left when the camera's
	 * right points into the wall. Between the two thresholds CurrentSide is kept. Pure.
	 */
	static float ComputeHangSocketSide(float ControlYaw, const FVector& WallNormal, float CurrentSide);

	/**
	 * The ledge line Ledge (a probed obstacle, its LedgePoint on the edge) lies on: the traversable ledge splines
	 * near it (Ledge_1..4 on the spawned city ledges, fire-escape rails and the sample's blocks) joined end to end,
	 * then carried on along the wall as far as a probe finds the same edge. False when there is no edge there.
	 */
	bool FindLedgeLine(const FHawkeyeParkourObstacle& Ledge, FHawkeyeLedgeLine& OutLine) const;

	/** A top edge at EdgePoint facing Normal about TopZ high: top, face, open air in front of it and on it. */
	bool IsHangLedgeAt(const FVector& EdgePoint, const FVector& Normal, float TopZ, float* OutTopZ = nullptr,
		AActor** OutActor = nullptr, FString* OutWhyNot = nullptr) const;

	/** A move this component is driving (not the sample's). */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsPerformingMove() const { return ActiveMove != EHawkeyeParkourMove::None; }

	/** The sample's traversal montage is running. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsSampleTraversalActive() const;

	/** Any traversal in progress: a move, a hang, or the sample's traversal. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsBusy() const { return IsPerformingMove() || bHanging || IsSampleTraversalActive(); }

	/**
	 * Movement input is ignored: during a move of ours and while hanging. The sample's traversal
	 * leaves input alone, because its montages blend out early on movement input.
	 */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	bool IsLockingInput() const { return IsPerformingMove() || bHanging; }

	UFUNCTION(BlueprintPure, Category = "Parkour")
	EHawkeyeParkourMove GetActiveMove() const { return ActiveMove; }

	UFUNCTION(BlueprintPure, Category = "Parkour")
	EHawkeyeParkourMove GetLastMove() const { return LastMove; }

	UFUNCTION(BlueprintPure, Category = "Parkour")
	EHawkeyeParkourRoute GetLastRoute() const { return LastRoute; }

	UFUNCTION(BlueprintPure, Category = "Parkour")
	FHawkeyeParkourObstacle GetLastObstacle() const { return LastObstacle; }

	/** How long this component takes over Move, s. */
	UFUNCTION(BlueprintPure, Category = "Parkour")
	float GetMoveSeconds(EHawkeyeParkourMove Move) const;

	UPROPERTY(BlueprintAssignable, Category = "Parkour")
	FOnParkourStartedSignature OnParkourStarted;

	/** The effort of a vault, mantle, grab or climb: a short low grunt (MS_Vault_Grunt). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Audio")
	TSoftObjectPtr<USoundBase> EffortSound;

	/** Plays EffortSound for the moves that take effort (not a drop to a hang). */
	void PlayEffortSound(EHawkeyeParkourMove Move) const;

	UPROPERTY(BlueprintAssignable, Category = "Parkour")
	FOnParkourFinishedSignature OnParkourFinished;

	// --- Heights (above the feet), cm ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Heights", meta = (ClampMin = "0.0"))
	float VaultMinHeight = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Heights", meta = (ClampMin = "0.0"))
	float VaultMaxHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Heights", meta = (ClampMin = "0.0"))
	float MantleMaxHeight = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Heights", meta = (ClampMin = "0.0"))
	float LedgeMaxHeight = 260.f;

	/** Falling, a ledge at least this far above the feet (and at most CatchMaxHeight) is caught. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Heights", meta = (ClampMin = "0.0"))
	float CatchMinHeight = 150.f;

	/**
	 * Falling, a ledge up to this far above the feet is caught. One floor (330 cm) so a drop from
	 * one fire-escape landing reaches the next one down even when a frame is late.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Heights", meta = (ClampMin = "0.0"))
	float CatchMaxHeight = 330.f;

	// --- Detection ------------------------------------------------------------------------------

	/** Sprinting, an obstacle this close to the capsule starts a move by itself, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float AutoTriggerDistance = 120.f;

	/** The jump key looks this far ahead of the capsule, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float ManualTriggerDistance = 180.f;

	/** The jump key takes tops from this high (the auto trigger from VaultMinHeight), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float JumpMinHeight = 40.f;

	/** The jump key's fan reaches this far either side of the stick direction, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float JumpFanDegrees = 35.f;

	/** Rays in the jump key's fan, odd so one is straight ahead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "1"))
	int32 JumpFanRays = 7;

	/** After a plain jump, the probe keeps running this long for a mantle or a ledge, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float LateCatchSeconds = 0.8f;

	/** The late catch looks this far ahead of the capsule, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float LateCatchReach = 100.f;

	/** A jump press during a move fires when the move ends, if it ends within this long, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float JumpBufferSeconds = 0.3f;

	/** Falling, a wall this close to the capsule can be caught, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float CatchDistance = 60.f;

	/** Ground speed below which sprinting into something does not auto-trigger, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float AutoMinSpeed = 300.f;

	/** A back edge further than this from the front face means the obstacle is not vaultable, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float MaxVaultDepth = 120.f;

	/** Auto vaults refuse a far floor this far below the feet, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float MaxAutoVaultDrop = 150.f;

	/** Jump-key vaults refuse a far floor this far below the feet (the roll height), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float MaxVaultDrop = 400.f;

	/** A thin top (a parapet) can be climbed over onto a floor at most this far below it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float ClimbOverMaxDrop = 120.f;

	/** An edge this close to the capsule (along the probe) can be dropped to a hang from, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float DropToHangReach = 60.f;

	/** Only an edge with more than this below it is one to hang from (less is a step down), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float DropToHangMinDrop = 150.f;

	/** The tallest lip at an edge that still counts as one (a 90 cm parapet, a 98 cm rail), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float MaxLipHeight = 130.f;

	/** The deepest lip that still counts (a 30 cm parapet); anything deeper is a raised floor, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection", meta = (ClampMin = "0.0"))
	float MaxLipDepth = 60.f;

	/** Vault and mantle try the Game Animation Sample's traversal first when the owner has it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Detection")
	bool bUseSampleTraversal = true;

	/**
	 * The roof-edge guard (2026-09-29, a 1679 cm fall in Cameron's log): a jump press moving toward a parapet with
	 * more than this below it on the far side does not launch her over it, cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Roof edge", meta = (ClampMin = "0.0"))
	float RoofEdgeGuardDrop = 400.f;

	/** The tallest parapet the guard looks at (a plain jump clears nothing taller), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Roof edge", meta = (ClampMin = "0.0"))
	float RoofEdgeGuardMaxHeight = 160.f;

	/** Moving toward the parapet: this fast at it, or the stick pushed within 60 degrees of it, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Roof edge", meta = (ClampMin = "0.0"))
	float RoofEdgeGuardMinSpeed = 50.f;

	/** A second press this soon after the guarded one means "I know": she leaps, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Roof edge", meta = (ClampMin = "0.0"))
	float DoubleTapSeconds = 0.3f;

	/** The double tap's leap: at least this fast toward the drop, cm/s (or her speed before the press, if more). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Roof edge", meta = (ClampMin = "0.0"))
	float GuardLeapSpeed = 450.f;

	// --- Timing, s ------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Timing", meta = (ClampMin = "0.05"))
	float VaultSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Timing", meta = (ClampMin = "0.05"))
	float MantleSeconds = 0.8f;

	/** Standing jump up to the hang. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Timing", meta = (ClampMin = "0.05"))
	float GrabSeconds = 0.35f;

	/** Falling onto the hang. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Timing", meta = (ClampMin = "0.05"))
	float CatchSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Timing", meta = (ClampMin = "0.05"))
	float ClimbSeconds = 1.f;

	/** Standing at an edge, over it and down to the hang. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Timing", meta = (ClampMin = "0.05"))
	float DropToHangSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Timing", meta = (ClampMin = "0.0"))
	float DropRegrabSeconds = 0.6f;

	// --- Hang -----------------------------------------------------------------------------------

	/**
	 * Feet below the ledge top while hanging, cm. The 2.5 m climb clip has them 151 cm down when the
	 * wrists reach the edge; 145 puts the hands over the top in the -game renders.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang")
	float HangBelowLedge = 145.f;

	/** Capsule centre out from the ledge edge while hanging, cm. More than the capsule radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang")
	float HangBackFromEdge = 36.f;

	/** Shimmy speed along the ledge with the stick fully to one side, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float ShimmySpeed = 120.f;

	/** The capsule stops this far short of a ledge's end, so both hands stay on it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float ShimmyEndMargin = 25.f;

	/** A ledge round a corner counts when its end is within this of the one she is on, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float CornerReach = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.05"))
	float CornerSeconds = 0.4f;

	/** A sideways leap reaches a hang this far along from the one she is on, cm (centre to centre). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float HangLeapReach = 250.f;

	/** The ledge a leap goes to has its top within this of the one she is on, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float HangLeapHeightTolerance = 40.f;

	/** A leap of HangLeapReach takes this long; a shorter one less (down to 0.3 s), s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.05"))
	float HangLeapSeconds = 0.45f;

	/** The hop back grabs a face behind her within this of the capsule, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float HangHopReach = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.05"))
	float HangHopSeconds = 0.5f;

	/** With nothing behind, the hop back is a controlled drop away from the wall at this speed, cm/s out and up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang")
	FVector2D HangHopDropVelocity = FVector2D(250.f, 150.f);

	/** The stick held up (toward the wall) this long climbs, when there is somewhere to stand and no roof-edge guard, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float AutoClimbHoldSeconds = 0.5f;

	/** Half the gap between the hands on the edge, cm (when the pose's own hands cannot be read). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float HangHandSpread = 22.f;

	/** How far one hand reaches ahead of its rest place when it steps, and how long a step takes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float HandStride = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.05"))
	float HandStepSeconds = 0.22f;

	/** While hanging the camera stays within this of looking straight at the wall (never behind its plane), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float HangCameraMaxYaw = 65.f;

	/** How fast the camera eases back inside that limit, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Hang", meta = (ClampMin = "0.0"))
	float HangCameraYawSpeed = 6.f;

	// --- Clips ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Clips")
	FHawkeyeParkourClip VaultClip;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Clips")
	FHawkeyeParkourClip MantleClip;

	/** Standing jump to the hang; its EndTime is the hang pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Clips")
	FHawkeyeParkourClip GrabClip;

	/** The last moment before the hang pose, for a catch while falling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Clips")
	FHawkeyeParkourClip CatchClip;

	/** From the hang pose to standing on top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Clips")
	FHawkeyeParkourClip ClimbClip;

	/** The montage slot the clips play in: the sample AnimBP's full-body slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parkour|Clips")
	FName ClipSlot = TEXT("DefaultSlot");

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Sprinting into something: TryParkour(true). */
	void TryAutoParkour();

	/** ChooseMove with the lowest accepted top at MinHeight (below VaultMinHeight a vault or mantle still fits). */
	EHawkeyeParkourMove ChooseMoveFrom(float MinHeight, float Height, bool bClearBeyond, bool bStandingSurface) const;

	/** Why Obstacle takes no move, for the log. */
	FString DescribeRefusal(const FHawkeyeParkourObstacle& Obstacle, bool bLateCatch) const;

	/** Starts Move over Obstacle: the sample for a vault or mantle from the ground, else ours. */
	bool StartChosenMove(EHawkeyeParkourMove Move, const FHawkeyeParkourObstacle& Obstacle, const TCHAR* Trigger);

	void CancelLateCatch(const TCHAR* Why);

	/** Nearest near-vertical face within Reach of the capsule axis, probed at several heights up to MaxHeight. */
	bool FindFrontFace(const FVector& Feet, const FVector& Forward, float Reach, float MaxHeight, FHitResult& OutHit,
		float LowestProbe = -1.f) const;

	/** The top surface just behind the front face, with open air above it. */
	bool FindTop(const FHitResult& Face, const FVector& Normal, const FVector& Feet, float MaxHeight, FVector& OutTop) const;

	/** Back edge, far floor and whether the capsule fits there. */
	void ProbeBeyond(const FVector& Feet, FHawkeyeParkourObstacle& Obstacle) const;

	/** Room for the capsule on the top, or over a thin top onto a floor close below. */
	void ProbeStanding(FHawkeyeParkourObstacle& Obstacle) const;

	/** No blocking geometry overlaps the capsule with its centre at Centre. */
	bool CapsuleFits(const FVector& Centre) const;

	/** What stops the capsule fitting at Centre, for the refusal log. */
	FString DescribeBlocker(const FVector& Centre) const;

	bool TraceLine(const FVector& From, const FVector& To, FHitResult& OutHit) const;

	/** The sample's TryTraversalAction; true when it started a montage. */
	bool TryStartSampleTraversal();

	/** Common start: face the wall, Flying, clip, state. Rise < 0 means End.Z - Start.Z. */
	bool BeginMove(EHawkeyeParkourMove Move, const FVector& End, float Rise, float Seconds, const FHawkeyeParkourClip& Clip);

	/** Where the capsule is Alpha of the way through the current move. */
	FVector ComputeMoveLocation(float Alpha) const;

	/** Forward (0..1 of the way) and up (in units of the rise) at Alpha, from the clip or an arc. */
	void ComputeMoveShape(float Alpha, float& OutForward, float& OutUp) const;

	/** Clip root motion from its start to Time, in the actor's frame: X forward, Z up. */
	FVector ClipRootOffset(float Time) const;

	void FinishMove();
	void EnterHang();
	FVector HangLocationFor(const FHawkeyeParkourObstacle& Obstacle) const;

	/** Starts Clip on the body (root motion ignored) so its stretch lasts Seconds. False without an AnimBP. */
	bool PlayClip(const FHawkeyeParkourClip& Clip, float Seconds);
	void StopClip(float BlendOutSeconds);
	void RestoreRootMotionMode();

	ACharacter* GetCharacter() const;
	UCharacterMovementComponent* GetMovement() const;

	/** The bottom of the capsule, world z. */
	float GetFeetZ() const;

	// --- Runtime state --------------------------------------------------------------------------

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Parkour")
	EHawkeyeParkourMove ActiveMove = EHawkeyeParkourMove::None;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Parkour")
	bool bHanging = false;

	UPROPERTY(Transient)
	EHawkeyeParkourMove LastMove = EHawkeyeParkourMove::None;

	UPROPERTY(Transient)
	EHawkeyeParkourRoute LastRoute = EHawkeyeParkourRoute::None;

	UPROPERTY(Transient)
	FHawkeyeParkourObstacle LastObstacle;

	/** The ledge being hung from (or jumped to). */
	UPROPERTY(Transient)
	FHawkeyeParkourObstacle HangObstacle;

	/** The clip posing the current move; null means the plain arc. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> MoveClip;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMontage;

	FVector MoveStart = FVector::ZeroVector;
	FVector MoveEnd = FVector::ZeroVector;
	float MoveRise = 0.f;
	float MoveElapsed = 0.f;
	float MoveSeconds = 0.f;
	float ClipStart = 0.f;
	float ClipEnd = 0.f;
	/** The clip's root motion over the stretch: forward at the end, peak rise, rise at the end. */
	float ClipForwardTotal = 0.f;
	float ClipPeakUp = 0.f;
	float ClipEndUp = 0.f;
	float RegrabCooldown = 0.f;
	/** The ledge the last drop let go of: not caught again until the fall ends. */
	FVector DroppedLedgePoint = FVector::ZeroVector;
	bool bIgnoreDroppedLedge = false;
	/** Hop over the lip during a drop to hang, cm above a straight line. */
	float DropHop = 0.f;
	bool bRootMotionOverridden = false;
	TEnumAsByte<ERootMotionMode::Type> SavedRootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
	TWeakObjectPtr<UActorComponent> SampleTraversal;
	bool bLoggedSampleApi = false;

	/** The late catch: seconds left, seconds since armed, and whether she has left the ground yet. */
	float LateCatchRemaining = 0.f;
	float LateCatchElapsed = 0.f;
	/** Feet height when the late catch was armed: a top it takes must be JumpMinHeight above that. */
	float LateCatchTakeOffZ = 0.f;
	bool bLateCatchAirborne = false;
	float JumpBufferRemaining = 0.f;
	FString LastJumpRefusal;

	// The roof-edge guard.
	/** Seconds since the guarded press; below 0 when there was none (or the double-tap window is spent). */
	float GuardPressAge = -1.f;
	/** Toward the drop (into the parapet's front face), flat, and the leap's speed along it. */
	FVector GuardDirection = FVector::ZeroVector;
	float GuardSpeed = 0.f;
	bool bGuardMantle = false;
	bool bGuardFlight = false;
	/** The double tap's hop over the top is running; at its end she is let go toward the drop. */
	bool bGuardLeap = false;
	/** The parapet the last guarded press met. */
	FHawkeyeParkourObstacle GuardParapet;
	bool bGuardFlightAirborne = false;
	float GuardFlightElapsed = 0.f;
	/** On the parapet top: the way back to the roof, the top's height, and bCanWalkOffLedges as it was. */
	bool bPerched = false;
	FVector PerchRoofDirection = FVector::ZeroVector;
	float PerchTopZ = 0.f;
	bool bPerchSavedWalkOff = true;

	/** The guarded press: mantle onto the parapet top, or arm a plain jump held short of it. True when a move started. */
	bool GuardRoofEdge(const FHawkeyeParkourObstacle& Parapet);
	/** The double tap: out of the guard's mantle (or the held jump) and over the parapet. */
	void LeapFromGuard(const TCHAR* Why);
	void BeginPerch();
	void EndPerch(const TCHAR* Why);
	/** Moving toward Direction: by velocity, or by the stick. */
	bool IsMovingToward(const FVector& Direction) const;

	// The hang vocabulary (ParkourHang.cpp).
	/** A move from one hang to another (corner, leap, hop): it ends in the hang, and its path is its own. */
	static bool IsHangTransfer(EHawkeyeParkourMove Move);
	/** The hang is set up on HangObstacle: its ledge line, the far side, the hands. EnterHang calls it. */
	void BeginHangState();
	/** Out of the hang (a climb, a drop, a transfer): the hands let go of the edge. */
	void EndHangState();
	/** The capsule's hang location at Distance along HangLine. */
	FVector HangLocationAt(float Distance) const;
	/** Along the hang line by up to Step toward Sign; the distance moved. Pushing into an end tries a corner. */
	float Shimmy(float Sign, float Step);
	/** At the end of the line toward Sign (or at a wall across it): onto the ledge round the corner. */
	bool TryTurnCorner(float Sign, bool bBlockedAcross, const FVector& BlockPoint);
	/** How far the edge through EdgePoint runs along Direction, up to MaxDistance, cm. */
	float FindLedgeRun(const FVector& EdgePoint, const FVector& Direction, const FVector& Normal, float TopZ, float MaxDistance,
		float Step = 20.f, FString* OutWhyEnds = nullptr) const;
	/** The shrunk capsule swept between two hang locations; true when something is in the way. */
	bool SweepHang(const FVector& From, const FVector& To, FHitResult& OutHit) const;
	/** A probed ledge for a hang at EdgePoint: its far side and where to stand, measured from the hang's feet. */
	FHawkeyeParkourObstacle ProbeHangLedge(const FVector& EdgePoint, const FVector& Normal, AActor* Actor) const;
	/** HangObstacle's standing surface and far side where she is now (after a shimmy). */
	void RefreshHangLedge();
	/** The stick held up long enough: climbs, unless there is nowhere to stand or the roof-edge guard holds. */
	void TryAutoClimb();
	/** The sideways leap: a ledge at the same height along Sign, past where the shimmy would stop. */
	bool FindLeapTarget(float Sign, FHawkeyeParkourObstacle& OutTarget, float& OutDistance, FString& OutWhyNot) const;
	/** The hop back: a face behind her within HangHopReach with a top she can hang from. */
	bool FindHopTarget(FHawkeyeParkourObstacle& OutTarget, FString& OutWhyNot) const;
	/** Starts a transfer to Target's hang over Seconds, turning by TurnDegrees; bArc swings round Pivot. */
	bool BeginHangTransfer(EHawkeyeParkourMove Move, const FHawkeyeParkourObstacle& Target, float Seconds, float Rise,
		float TurnDegrees, bool bArc, const FVector& Pivot);
	/** Where the capsule is Alpha through a hang transfer. */
	FVector ComputeHangTransferLocation(float Alpha) const;
	/** The yaw a turning move has at Alpha, and the camera carried round with her. */
	void ApplyMoveTurn(float Alpha);
	/** The hands' hand-over-hand state for one tick, after the capsule moved Moved cm along the line. */
	void AdvanceHangHands(float DeltaSeconds, float Moved, float Sign);
	/** Eases the control yaw back inside HangCameraMaxYaw of the wall. */
	void KeepHangCameraOffTheWall(float DeltaSeconds);
	/** The stick this frame (zero when no input came in this frame or the last). */
	FVector GetFreshHangInput() const;

	UPROPERTY(Transient)
	FHawkeyeLedgeLine HangLine;
	FVector HangInput = FVector::ZeroVector;
	uint64 HangInputFrame = 0;
	bool bHangInputSet = false;
	float HangUpHeld = 0.f;
	bool bShimmying = false;
	bool bShimmyStopLogged = false;
	bool bAutoClimbRefusalLogged = false;
	FString LastHangRefusal;
	/** The standing surface was measured at this distance along the line; a shimmy past it re-measures. */
	float HangProbedAlong = 0.f;
	bool bHangTopGuarded = false;
	/** Below the top on its far side, cm (the roof-edge guard's measure from a hang). */
	float HangFarSideDrop = 0.f;
	/** Where the hang began, and its stand point, for a re-probe that finds nothing where the first one did. */
	float HangStartAlong = 0.f;
	FVector HangStartStand = FVector::ZeroVector;
	bool bHangStartStand = false;

	// The hands: where each is along the line (world, planted), the one stepping, the pose's own rest places.
	float HandAlong[2] = { 0.f, 0.f };
	int32 SteppingHand = INDEX_NONE;
	float StepFrom = 0.f;
	float StepTo = 0.f;
	float StepElapsed = 0.f;
	float HandsAlpha = 0.f;
	FVector HandRest[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bHandRestKnown = false;
	float SwayAlpha = 0.f;
	FVector SwayDirection = FVector::ZeroVector;

	// A turning move (a corner, the hop): the yaw it starts at and turns by, and the corner's swing.
	bool bMoveTurns = false;
	float MoveYawStart = 0.f;
	float MoveYawDelta = 0.f;
	float MoveYawApplied = 0.f;
	bool bMoveArc = false;
	FVector MovePivot = FVector::ZeroVector;
	/** The hang transfer's target, which becomes HangObstacle when it ends. */
	FHawkeyeParkourObstacle TransferTarget;

	/** The lens's shoulder: +1 right, -1 left, eased between. */
	float HangCameraSide = 1.f;
	float HangCameraSideTarget = 1.f;
};
