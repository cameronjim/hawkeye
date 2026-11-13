#pragma once

#include "CoreMinimal.h"
#include "Audio/HawkeyeAudioTypes.h"
#include "Combat/CombatReadability.h"
#include "Combat/FinisherComponent.h"
#include "Combat/MeleeCombo.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Combat/StrikePose.h"
#include "GameFramework/Character.h"
#include "ISpudObject.h"
#include "Player/BodyLook.h"
#include "Player/HawkeyeMovementTypes.h"
#include "Player/HawkeyeSprintToggle.h"
#include "Settings/HawkeyeSettings.h"
#include "HawkeyeCharacter.generated.h"

class UAnimSequence;
class AThugCharacter;
class UBowComponent;
class UCameraComponent;
class UInputAction;
class USpringArmComponent;
class UInputMappingContext;
class UHealthComponent;
class UHitReactionComponent;
class UInteractionComponent;
class UGrappleComponent;
class UHawkeyeMovementComponent;
class UInventoryComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class UParkourComponent;
class UPawnNoiseEmitterComponent;
class USoundBase;
class UTakedownComponent;
class UWeaponComponent;
struct FInputActionValue;

/**
 * The player character, in third person: a spring arm behind and above the body that probes
 * against the world, the camera on the end of it, and the body turning to face the way it moves
 * (or the camera, while aiming). Aim blends the arm in to the right shoulder.
 *
 * Movement picks a gait every frame (walk, run, sprint, crouch, slide) from how hard the stick
 * is pushed and for how long; every number is a property so BP_Kate can tune it. Landings are
 * measured from the top of the arc: a long drop rolls (moving) or stumbles (standing), a
 * controlled drop dips the camera, and a very long one costs health, never all of it.
 *
 * Body animation: when the mesh runs an AnimBP (BP_Kate: the Game Animation Sample's
 * motion-matched SandboxCharacter_CMC_ABP) the AnimBP owns the body, and the idle/walk/run clip
 * switch in LocomotionAnim.h only runs for a mesh without one. A class that carries the sample's
 * CharacterInputState (SandboxCharacter_CMC and its children) gets our gait, crouch and aim
 * written into it every update; see SyncGaspInputState.
 *
 * BP_HawkeyeCharacter is the input-wired base; BP_Kate is the playable child.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AHawkeyeCharacter : public ACharacter, public ISpudObject, public ISpudObjectCallback
{
	GENERATED_BODY()

public:
	/** Swaps in UHawkeyeMovementComponent, so the Blueprints' tuned CharacterMovement values carry over. */
	AHawkeyeCharacter(const FObjectInitializer& ObjectInitializer);

	/** The character's movement: the engine's modes plus Hawkeye's custom ones (the zip). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	UHawkeyeMovementComponent* GetHawkeyeMovement() const;

	/** The boom the camera hangs off. Rotates with the control rotation. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** The player's view, on the end of the boom. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UTakedownComponent* GetTakedownComponent() const { return TakedownComponent; }

	/** Hands, the melee fallback while no bow is owned. bHasWeapon is always false on the player. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UWeaponComponent* GetWeaponComponent() const;

	/** The bow: draw, release, arrows and the bow's look. Always present on the player. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UBowComponent* GetBowComponent() const { return BowComponent; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	/** The bow, the quiver and the keycard ring. Always present on the player. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	/** The grapple arrow: anchor targeting, firing and the zip. Always present on the player. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UGrappleComponent* GetGrappleComponent() const { return GrappleComponent; }

	/** Light and heavy strikes with the bow (V). Always present on the player. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UMeleeComponent* GetMeleeComponent() const { return MeleeComponent; }

	/**
	 * Hands CombatAnimSet to the melee and bow components and loads its clips. BeginPlay calls it; a
	 * test calls it after SetCombatAnimSet.
	 */
	void ApplyCombatAnimSet();

	/** Replaces the combat clip set (tests, or a costume with its own clips); ApplyCombatAnimSet hands it on. */
	void SetCombatAnimSet(UCombatAnimSet* InSet) { CombatAnimSet = InSet; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Animation")
	UCombatAnimSet* GetCombatAnimSet() const { return CombatAnimSet; }

	/** The finisher on F (after the takedown is tried). Always present on the player. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UFinisherComponent* GetFinisherComponent() const { return FinisherComponent; }

	/** Vault, mantle and ledge grab. Always present on the player. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	UParkourComponent* GetParkourComponent() const { return ParkourComponent; }

	/**
	 * True during any traversal move: one of ours, a hang, or the Game Animation Sample's
	 * traversal montage. Jumping, crouching and the grapple wait for it to end.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	bool IsTraversing() const;

	/** True while a grapple zip carries the character; movement input and jumping are ignored. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	bool IsZipping() const;

	/**
	 * The end of a grapple zip, through the ordinary landing path as a 0 cm landing: no damage,
	 * and no roll (the camera dip and speed cut are for falls above RollHeight only).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Falling")
	void NotifyGrappleLanded();

	/** True once GiveKeycard(KeycardId) has been called for that id. Forwards to the inventory. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	bool HasKeycard(FName KeycardId) const;

	/** Adds a keycard to the player's ring. Returns false when they already had it. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Character")
	bool GiveKeycard(FName KeycardId);

	/** Keycards picked up so far. Doors check this by id. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	TSet<FName> GetKeycards() const;

	/**
	 * Loudness the player is currently emitting, per claude-docs/gameplay-semantics.md:
	 * 1.0 sprinting, 0.4 walking, 0 crouching or airborne.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	float GetMovementNoiseLoudness() const;

	/**
	 * True while a takedown or a finisher is playing, or a scripted beat holds her (SetScriptedLock); movement,
	 * strikes, dodges and firing are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	bool IsLockedOutByTakedown() const;

	/** A scripted beat (a boss's phase change) holds her: no strikes, dodges, draws or finishers until it lets go. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Character")
	void SetScriptedLock(bool bLocked) { bScriptedLock = bLocked; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	bool IsScriptedLocked() const { return bScriptedLock; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	bool IsPerformingFinisher() const;

	/**
	 * Starts aiming: the camera blends in to AimCamera over the right shoulder, the body turns to
	 * face the camera, the speed drops to WalkSpeed, and the weapon tightens its spread cone.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Aim")
	void StartAim();

	/** Ends aiming. Safe to call when not aiming; sprinting calls it. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Aim")
	void StopAim();

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Aim")
	bool IsAiming() const { return bIsAiming; }

	/** The bow started drawing: drop the sprint and aim (camera in, strafe, slower walk) while it is held. */
	void NotifyBowDrawStarted();

	/**
	 * The aim button went down / up. Held (the default), aim lasts while the button does; toggled
	 * (FHawkeyeSettings::bToggleAim), a press raises it and the next press lowers it.
	 */
	void PressAim();
	void ReleaseAim();

	/**
	 * The bow aim assist (gameplay-semantics.md, "Bow aim assist"), each tick while aiming with a bow: the
	 * aim press's snap while it runs, else the magnetism's pull toward the chest nearest the reticle.
	 */
	void UpdateAimAssist(float DeltaSeconds);

	/** The aim press's snap: turns the view over 0.1 s onto the chest nearest the reticle in the snap cone. False with none. */
	bool BeginAimSnap();

	/** LookDelta (degrees) slowed by the magnetism near a thug's chest; bFromStick picks the pad's numbers. */
	FVector2D ApplyAimSlowdown(FVector2D LookDelta, bool bFromStick) const;

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Aim")
	bool IsAimSnapping() const { return AimSnapRemaining > 0.f; }

	/** The thug the aim assist last acted on, or null. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Aim")
	AActor* GetAimAssistTarget() const { return AimAssistTarget.Get(); }

	/**
	 * The crouch button went down / up. Toggled (the default), a press crouches and the next stands;
	 * held, she stands again when it is let go. Taps still dodge and presses at a sprint still slide.
	 */
	void PressCrouch();
	void ReleaseCrouch();

	/** Crouched, or asked to be and waiting for the movement component's next update. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	bool IsCrouchWanted() const;

	/**
	 * Stands her up for Why, which the log names when the capsule comes up. Every C++ un-crouch goes through
	 * here (or names itself before touching the movement component), so a stand-up nobody asked for shows as
	 * "unasked" with the Blueprint or C++ caller behind it.
	 */
	void EndCrouch(const TCHAR* Why);

	/** Why her last crouch ended ("crouch input", "slide start", "dodge", ...); empty before the first. */
	const FString& GetLastCrouchEndReason() const { return LastCrouchEndReason; }

	/** Crouches that have ended since play began. */
	int32 GetCrouchEndCount() const { return CrouchEndCount; }

	/** Stand-ups nobody in C++ asked for (a Blueprint or an unnamed caller): a bug, logged with a warning. */
	int32 GetUnaskedCrouchEndCount() const { return UnaskedCrouchEndCount; }

	/** Routes a Blueprint or engine UnCrouch through the reason log. */
	virtual void UnCrouch(bool bClientSimulation = false) override;

	/**
	 * Takes every setting the character uses: sensitivities, inverts, hold or toggle for aim and crouch,
	 * the shake and flash scales, and the difficulty's regen delay, fall damage, parry window and trick
	 * arrow caps. The settings subsystem's broadcast lands here; tests call it directly.
	 */
	void ApplySettings(const FHawkeyeSettings& Settings);

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Settings")
	bool IsAimToggle() const { return bToggleAim; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Settings")
	bool IsCrouchToggle() const { return bToggleCrouch; }

	/** What camera shakes are multiplied by: 1, or 0.3 with "reduce camera shake". */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Settings")
	float GetCameraShakeScale() const { return CameraShakeScale; }

	/** What the EMP's screen split and the parry ring are multiplied by: 1, or 0.3 with "reduce flashing". */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Settings")
	float GetFlashScale() const { return FlashScale; }

	/** The difficulty's multiplier on fall damage. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Settings")
	float GetFallDamageScale() const { return FallDamageScale; }

	/** The difficulty's change to the parry window, s (+ an early tap still counts, - not the telegraph's start). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Settings")
	float GetParryWindowDelta() const { return ParryWindowDelta; }

	/** Seconds an early strike tap is held waiting for a telegraph to parry (Story only). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Parry")
	float GetParryBufferRemaining() const { return ParryBufferRemaining; }

	/** The throw of the shake now running, cm, after the shake scale. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	float GetActiveShakeAmplitude() const { return ActiveShakeAmplitude; }

	/** The draw ended (released or let down): the aim goes too, unless the aim button is held. */
	void NotifyBowDrawEnded();

	/** True while the bow is being drawn. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Aim")
	bool IsDrawingBow() const;

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	/** Her top speed with sprint held, cm/s (a chase's opening starts her at it). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	float GetSprintSpeed() const { return SprintSpeed; }

	/**
	 * The sprint button went down (Shift, or L3 on a pad). Held (the keyboard's default) it sprints until the release;
	 * toggled (the pad's default, or the setting) the first press turns it on and a second turns it off
	 * (FHawkeyeSprintToggle). Input_SprintStarted calls it with the controller's last device; public for tests.
	 */
	void PressSprint(bool bFromGamepad);

	/** The sprint button came up: ends a held sprint; a toggled one keeps going. */
	void ReleaseSprint();

	/** True while a toggled sprint is on (it may be sliding, or in the air, and still on). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	bool IsSprintToggled() const { return SprintToggle.IsOn(); }

	/** Ends a toggled sprint for Why (logged at Log); a held one is left to its button. Returns whether one was on. */
	bool StopSprintToggle(EHawkeyeSprintStop Why);

	/** The sprint setting ApplySettings last took. */
	EHawkeyeSprintMode GetSprintMode() const { return SprintMode; }

	/** Current camera field of view. Exposed so a test or a Blueprint can read the blend. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Aim")
	float GetCurrentFOV() const;

	/** LookSensitivity, reduced by AimLookMultiplier while aiming. */
	UFUNCTION(BlueprintPure, Category = "Input")
	float GetEffectiveLookSensitivity() const;

	/**
	 * The yaw/pitch delta a raw look input produces. Split out of Input_Look so the sensitivity
	 * rules can be tested without an input stack.
	 */
	UFUNCTION(BlueprintPure, Category = "Input")
	FVector2D ComputeLookDelta(FVector2D RawInput, bool bAiming) const;

	/**
	 * Yaw/pitch degrees to apply this frame for a held gamepad stick position, given
	 * DeltaSeconds and whether the aim is held. Eased with value^1.5 so a light nudge is fine
	 * for small aim corrections while the stick still reaches full rate at the edge. Pure and
	 * tested so the scaling can be checked without a controller.
	 */
	UFUNCTION(BlueprintPure, Category = "Input")
	FVector2D ComputeStickLookDelta(FVector2D RawInput, float DeltaSeconds, bool bAiming) const;

	// --- Camera ---------------------------------------------------------------------------------

	/**
	 * Arm length, shoulder offset and FOV the camera settles at for this aim state and camera
	 * pitch (degrees, up positive). No blending over time. At the hip, looking up past
	 * LookUpPitchStart shortens the arm towards LookUpArmLength, lifts the socket towards
	 * LookUpSocketZ and the pivot by up to LookUpPivotLift, all reached at LookUpPitchFull, so a
	 * steep look up from the street does not drive the lens into the pavement.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	FHawkeyeCameraTargets ComputeCameraTargets(bool bAiming, float Pitch = 0.f) const;

	/** Pitch clamped to [CameraPitchMin, CameraPitchMax]. The player camera manager uses the same limits. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	float ClampCameraPitch(float Pitch) const { return FMath::Clamp(Pitch, CameraPitchMin, CameraPitchMax); }

	/** 0 at the hip, 1 fully aimed; moves over AimBlendSeconds. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	float GetAimBlendAlpha() const { return AimAlpha; }

	// --- Gait -----------------------------------------------------------------------------------

	/**
	 * Which gait these inputs ask for. Slide beats crouch beats sprint beats aim; otherwise a full
	 * stick (or any keyboard press) runs, a medium stick runs once held for RunAfterHeldSeconds,
	 * and a light one walks.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	EHawkeyeGait SelectGait(float InputMagnitude, float InputHeldSeconds, bool bWantsSprint, bool bCrouched,
		bool bSliding, bool bAiming) const;

	/** Top ground speed for a gait, cm/s. The slide starts at SlideSpeed and eases to CrouchSpeed. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	float GetGaitSpeed(EHawkeyeGait Gait) const;

	/** The gait chosen on the last update. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	EHawkeyeGait GetGait() const { return CurrentGait; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	bool IsSliding() const { return bIsSliding; }

	/**
	 * Starts a slide: only while sprinting on the ground above SlideMinSpeed. Shrinks the capsule
	 * to the crouched height for SlideSeconds, then stands back up. Returns true when it started.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Movement")
	bool StartSlide();

	// --- Falling --------------------------------------------------------------------------------

	/**
	 * Fraction of MaxHealth a fall of this height costs: 0 below FallDamageMinHeight, then
	 * FallDamageMinFraction rising to FallDamageMaxFraction at FallDamageMaxHeight and capped there.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	float ComputeFallDamageFraction(float FallHeight) const;

	/** Height of the last landing, from the top of the arc to the ground, cm. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	float GetLastFallHeight() const { return LastFallHeight; }

	/** How far below the top of the current arc the character is, cm. 0 on the ground. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	float GetCurrentFallHeight() const;

	/**
	 * True through a landing roll or stumble (a landing from above RollHeight), and for
	 * LandingRecoverSeconds after a controlled drop (stepped off an edge, let go of a hang or a
	 * zip, not a jump) from above ControlledDropDipHeight.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	bool IsRecoveringFromLanding() const { return LandingState != EHawkeyeLanding::None; }

	/** What the last landing turned into, while it lasts; None once it is over. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	EHawkeyeLanding GetLandingState() const { return LandingState; }

	/** Seconds since the current landing roll, stumble or dip began. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	float GetLandingElapsed() const { return LandingElapsed; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	bool IsRolling() const { return LandingState == EHawkeyeLanding::Roll; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	bool IsStumbling() const { return LandingState == EHawkeyeLanding::Stumble; }

	/** The first RollInputLockSeconds of a roll: move, jump, crouch and dodge input are ignored. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	bool IsLandingInputLocked() const { return IsRolling() && LandingElapsed < RollInputLockSeconds; }

	/**
	 * How much of her speed a landing leaves her: rising 0 to 1 over a stumble,
	 * LandingSpeedMultiplier through a dip, 1 otherwise (a roll is carried by its own force).
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	float GetLandingSpeedFactor() const;

	/** Camera pitch added by a roll, degrees: down to -RollCameraPitchDegrees halfway and back. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	float GetLandingCameraPitch() const;

	/** The flattened direction the current (or last) roll travels. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	FVector GetRollDirection() const { return RollDirection; }

	/** The current fall began as a controlled drop: off an edge, a hang or a zip, not a jump. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Falling")
	bool IsControlledDrop() const { return bControlledDrop; }

	// --- Debug ----------------------------------------------------------------------------------

	/**
	 * One line for the HUD: gait, ground speed, fall height. Shown when hawkeye.DebugMovement is 1. On a ledge
	 * and in a parkour move it reads the traversal instead of the gait: "hang", "hang shimmy", or the move
	 * ("corner 0.21 s", "leap", "hop", "climb"...) with how long it has run, at the speed she is actually
	 * moving (a hang is moved by hand, so the velocity reads 0), and how far along the ledge she is.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Debug")
	FString GetMovementDebugText() const;

	/**
	 * The debug line's state word for a traversal, or empty when there is none: Move (with MoveSeconds) wins
	 * over the hang, and a hang reads "hang shimmy" while bShimmying.
	 */
	static FString FormatTraversalDebugState(bool bHanging, bool bShimmying, EHawkeyeParkourMove Move, float MoveSeconds,
		bool bSampleTraversal);

	/** Reads the hawkeye.DebugMovement console variable. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Debug")
	static bool IsMovementDebugEnabled();

	/** Ends a slide before jumping: a character cannot jump while crouched. */
	virtual void Jump() override;

	// --- Game Animation Sample bridge -------------------------------------------------------------

	/**
	 * True when this class carries the Game Animation Sample's CharacterInputState struct (BP_Kate,
	 * through SandboxCharacter_CMC). Then the sandbox graph reads its locomotion inputs from it,
	 * owns MaxWalkSpeed and the rotation mode, and its own Blueprint input events are dropped.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Animation")
	bool UsesGaspLocomotion() const;

	/** True while the body mesh is animated by an AnimBP; the clip switch then leaves it alone. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Animation")
	bool IsBodyDrivenByAnimBlueprint() const;

	// --- Body look ------------------------------------------------------------------------------

	/**
	 * What she looks like over the driver mesh (see GetBodyMesh). Empty: the driver is what is drawn. BP_Kate's
	 * is her Blender-built suit (SK_Built_Kate, written by Tools/Editor/import_bodies.py), or Paragon's Sparrow
	 * (create_kate_look.py) when the built body is not on disk; a clone with neither keeps the mannequin.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Body")
	FHawkeyeBodyLook BodyLook;

	/**
	 * The mesh people see. GetMesh() is the driver: it runs the AnimBP, the montages, the bow and hang IK,
	 * root motion, the notifies and the collision, and owns the sockets gameplay measures from (and the
	 * bow attaches to). With a BodyLook in place the driver is hidden and this is the look's mesh, posed
	 * from the driver every frame; without one it is GetMesh(). Read this for anything that is about how
	 * she looks: materials, hiding her from a close camera, arrows stuck in her.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Body")
	USkeletalMeshComponent* GetBodyMesh() const;

	/** True when a BodyLook mesh is drawn over a hidden driver. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Body")
	bool HasBodyLook() const { return LookMesh != nullptr; }

	/**
	 * Puts BodyLook on (once; PostInitializeComponents calls it). Returns the look's mesh, or null when
	 * there is none or its content is not on disk (the driver then stays visible). Public for tests.
	 */
	USkeletalMeshComponent* ApplyBodyLook();

	/**
	 * Writes the current gait, crouch and aim into the sample's CharacterInputState:
	 * WantsToSprint (sprint gait), WantsToWalk (walk gait, or aiming), WantsToStrafe and
	 * WantsToAim (aiming), WantsToCrouch (crouched or sliding), plus FullMovementInput (any gait
	 * above walk). No-op on a class without the struct. Public so a test can drive it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Animation")
	void SyncGaspInputState();

	/** Reads one CharacterInputState flag back by its authored name (WantsToSprint, ...). False if absent. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Animation")
	bool GetGaspInputFlag(FName FlagName) const;

	/** How many Blueprint input bindings the last possession dropped. See DropBlueprintInputBindings. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Animation")
	int32 GetDroppedBlueprintInputBindings() const { return DroppedBlueprintInputBindings; }

	// --- Melee, dodge, hit reactions -------------------------------------------------------------

	/**
	 * A tap of V. A parry first, when a thug in front within ParryRange is telegraphing (TryParry).
	 * Otherwise the next light of the chain (GetComboAttack): 15, 15, then 25 with a 150 cm knockback,
	 * each within the combo's 0.35 s window after the last one landed; pressed during the recovery of
	 * a light that landed, it waits for the recovery. Turns her to the soft-lock target over
	 * SoftTurnSeconds. Refused mid-wind-up, mid-dodge, drawing, traversing or zipping.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Melee")
	bool StartLightAttack();

	/** V held HeavyHoldSeconds: HeavyAttack (35 after a 0.6 s wind-up, knocks a thug down). Ends the chain. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Melee")
	bool StartHeavyAttack();

	/** Chain step Step (0, 1, 2) of the light combo. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	FHawkeyeMeleeAttack GetComboAttack(int32 Step) const;

	/**
	 * The parry: a thug within ParryRange and ParryAngleDegrees of her view, telegraphing a swing or a
	 * bash (UHawkeyeMeleeRules::ClassifyParry), is staggered ParryStaggerSeconds and his swing never
	 * lands; a gunner's raised pistol loses its first shot. Hit stop, a ring and MS_Parry. False when
	 * there is nothing to parry.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Melee")
	bool TryParry();

	/** TryParry, but allowed over her own jab's wind-up: the early tap Story holds (UpdateParryBuffer). */
	bool TryParryFromBuffer();

	/** The thug a parry would meet now (and what it would meet), or null. */
	AThugCharacter* FindParryTarget(EHawkeyeParryKind& OutKind) const;

	/**
	 * True when a tap now would parry Thug: he is telegraphing something a parry answers, is in the window
	 * (the difficulty's ParryWindowDelta), within ParryRange and ParryAngleDegrees of her view and 150 cm
	 * up or down. What the telegraph glyph's parry line shows.
	 */
	bool CanParryNow(const AThugCharacter* Thug, EHawkeyeParryKind* OutKind = nullptr) const;

	/** The marker on the thug her last swing was aimed at (gameplay-semantics.md, "Combat readability"). */
	const FHawkeyeTargetMarker& GetMeleeTargetMarker() const { return MeleeTargetMarker; }

	/** How far the fight camera is in, 0 to 1 (before its smoothstep). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	float GetFightCameraAlpha() const { return FightCameraAlpha; }

	/** The fight camera's numbers (FightCamera). */
	const FHawkeyeFightCameraSettings& GetFightCameraSettings() const { return FightCamera; }

	/** Alerted thugs within the fight camera's radius at the last count. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	int32 GetEngagedThugCount() const { return EngagedThugCount; }

	/** The heavy's camera punch now, cm toward her (0 outside one). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	float GetCameraPunchOffset() const;

	/** The hit-stop her landed light (the chain's first) or heavy gives, real seconds (by its weight). */
	float GetMeleeHitStopSeconds(bool bHeavy) const;

	/** A landed blow's hit-stop: the world at Dilation for RealSeconds (ApplyTimeWarp; never cuts a finisher's slow motion). */
	void ApplyHitStop(float RealSeconds, float Dilation)
	{
		LastHitStopRealSeconds = FPlatformTime::Seconds();
		ApplyTimeWarp(RealSeconds, Dilation);
	}

	/** Real seconds since the last landed blow's hit-stop began; -1 when there has been none. */
	float GetSecondsSinceHitStop() const
	{
		return LastHitStopRealSeconds < 0.0 ? -1.f : static_cast<float>(FPlatformTime::Seconds() - LastHitStopRealSeconds);
	}

	/** The hit-stop, the physical reaction to a blow (on the body people see) and the knockout's throw. */
	UHitReactionComponent* GetHitReactionComponent() const { return HitReactionComponent; }

	/** F when no takedown is valid: the finisher on a staggered or knocked-down thug (UFinisherComponent). */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Melee")
	bool TryFinisher();

	/** The closest living thug within SoftLockRange and SoftLockAngleDegrees of the camera's forward. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	AActor* FindSoftLockTarget() const;

	/**
	 * The melee assist's pick for a strike now (gameplay-semantics.md, "Melee assist"): the best living,
	 * standing thug within MeleeAssistRange and MeleeAssistAngleDegrees of GetMeleeAssistDirection. Null if none.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	AActor* FindMeleeAssistTarget() const;

	/** Where a strike now is meant: the move stick if it was pushed within MeleeAssistInputSeconds, else the camera's forward. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	FVector GetMeleeAssistDirection() const;

	/** Tests: the stick points WorldDirection now, as Input_Move would record it. */
	void SetMoveInputDirectionForTest(const FVector& WorldDirection);

	/** Turns her to face Direction (flat) over SoftTurnSeconds. */
	void BeginSoftTurn(const FVector& Direction);

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	bool IsSoftTurning() const { return SoftTurnRemaining > 0.f; }

	/**
	 * One step of the melee flow: the combo's windows, a waiting light, the soft turn, the strike pose
	 * and the hit lean, handed to the bow-IK anim instance. Tick calls it; public for tests.
	 */
	void AdvanceMeleeFlow(float DeltaSeconds);

	const FHawkeyeComboTracker& GetCombo() const { return Combo; }

	/** Hits on the combo counter (the HUD's "x3"). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	int32 GetComboCount() const { return Combo.GetCount(); }

	/** The counter is at its bonus (x5 and up): hits do +20% and it glows. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	bool IsComboBonusActive() const { return Combo.IsBonusActive(); }

	const FHawkeyeStrikePoseClock& GetStrikePose() const { return StrikePose; }
	const FHawkeyeHitLeanClock& GetHitLean() const { return HitLean; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	int32 GetParryCount() const { return ParryCount; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Dodge")
	int32 GetPerfectDodgeCount() const { return PerfectDodgeCount; }

	/** Dodges started, every one, this life of the pawn (the laps' pressure count). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Dodge")
	int32 GetDodgeCount() const { return DodgeCount; }

	/** Hits from someone else that took health, and the health they took, this life of the pawn. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	int32 GetHitsTakenCount() const { return HitsTakenCount; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	float GetDamageTakenTotal() const { return DamageTakenTotal; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	bool IsMeleeAttacking() const;

	/**
	 * A dash of DodgeDistance over DodgeSeconds along WorldDirection (flattened), with the health
	 * component invulnerable for the first DodgeInvulnerableSeconds. Refused while sprinting,
	 * airborne, traversing, zipping, in a takedown, already dodging, or within DodgeCooldownSeconds
	 * of the last one. Ctrl tapped while moving (not sprinting) calls it with the move direction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Dodge")
	bool TryDodge(FVector WorldDirection);

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Dodge")
	bool IsDodging() const { return DodgeRemaining > 0.f; }

	/** True for the invulnerable part of a dodge. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Dodge")
	bool IsDodgeInvulnerable() const { return DodgeInvulnerableRemaining > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Dodge")
	float GetDodgeCooldownRemaining() const { return DodgeCooldownRemaining; }

	/** True for PlayerStaggerSeconds after a staggering hit (a thug's punch or bat). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Melee")
	bool IsStaggered() const { return StaggerRemaining > 0.f; }

	/**
	 * 0 at or above LowHealthThreshold of max health, rising to 1 at zero: how much of the
	 * low-health desaturation and vignette is on screen.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	float ComputeLowHealthAlpha(float HealthPercent) const;

	/** 1 the moment a hit lands on her (or a blast shakes her), 0 once the shake has run out. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	float GetHitShakeAlpha() const { return ActiveShakeSeconds > 0.f ? HitShakeRemaining / ActiveShakeSeconds : 0.f; }

	/** Shakes the camera for Seconds, the lens thrown Amplitude cm at the start. A blast nearby calls it. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Camera")
	void PlayImpactShake(float Seconds, float Amplitude);

	/**
	 * Draws the follow camera without motion blur for Seconds (the longer of this and any running),
	 * then puts its own setting back. A blast calls it with its shake.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Camera")
	void SuppressMotionBlur(float Seconds);

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	bool IsMotionBlurSuppressed() const { return MotionBlurOffRemaining > 0.f; }

	/**
	 * A brief chromatic split on her camera (ScreenPulseMaterial) for Seconds, Strength 0..1 at the
	 * start and fading out. An EMP nearby calls it. A stronger pulse running on is left alone.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Camera")
	void PlayScreenPulse(float Seconds, float Strength);

	/** The screen pulse's strength right now, 0 when none is running. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	float GetScreenPulseStrength() const;

	// --- Quiver wheel (claude-docs/gameplay-semantics.md, "trick arrows") -----------------------

	/**
	 * Opens the radial quiver: game time drops to QuiverWheelTimeDilation, look input steers the
	 * highlight instead of the camera, and the HUD shows the wheel. Holding Tab opens it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Quiver")
	void OpenQuiverWheel();

	/** Closes the wheel and restores time; with bSelect, nocks the highlighted slot if it holds arrows. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Quiver")
	void CloseQuiverWheel(bool bSelect);

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Quiver")
	bool IsQuiverWheelOpen() const { return bQuiverWheelOpen; }

	/**
	 * Points the wheel's cursor: X right, Y up, in cursor units (QuiverWheelCursorRadius is the rim).
	 * Mouse look adds to it; the right stick sets it outright.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Quiver")
	void SetQuiverWheelCursor(FVector2D Cursor);

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Quiver")
	FVector2D GetQuiverWheelCursor() const { return QuiverWheelCursor; }

	/** The slot under the cursor, 1..HawkeyeQuiverSlotCount, or 0 while the cursor is in the dead zone. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Quiver")
	int32 GetQuiverWheelHighlight() const;

	/** Tab held this long (real time) opens the wheel; released sooner it was a tap, the inventory. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Quiver", meta = (ClampMin = "0.0"))
	float QuiverWheelHoldSeconds = 0.25f;

	/** Game speed while the wheel is open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Quiver",
		meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float QuiverWheelTimeDilation = 0.2f;

	/** How far the cursor can go from the centre, in cursor units (about mouse pixels). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Quiver", meta = (ClampMin = "1.0"))
	float QuiverWheelCursorRadius = 100.f;

	/** Inside this the cursor highlights nothing, and a release keeps the slot already nocked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Quiver", meta = (ClampMin = "0.0"))
	float QuiverWheelDeadZone = 30.f;

	// --- Partner and switching (claude-docs/gameplay-semantics.md, "partner and switching") --------

	/** Who this is, for the HUD and the partner's status line: "Kate" on BP_Kate, "Clint" on BP_Clint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Character")
	FText CharacterName;

	/** CharacterName, or the actor's name when a Blueprint left it empty. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	FText GetCharacterName() const;

	/**
	 * The gait an AI controller wants while it drives this character (the partner runs to catch up,
	 * walks when close). The player's gait comes from the stick instead; this is ignored then.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Movement")
	void SetAIGait(EHawkeyeGait Gait) { AIGait = Gait; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Movement")
	EHawkeyeGait GetAIGait() const { return AIGait; }

	/**
	 * True from health reaching 0 until the partner's revive lands or she dies of it (then until the
	 * reload): on the ground, no input. Lasts at most DownedMaxSeconds.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	bool IsDowned() const { return bDowned; }

	/** The partner's revive: back up with HealthFraction of max health. Does nothing unless downed. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Health")
	void ReviveFromDown(float HealthFraction);

	/** The longest she lies downed: no revive by then and she dies (the last save loads). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Health", meta = (ClampMin = "0.5"))
	float DownedMaxSeconds = 8.f;

	/** A key press this soon into a hopeless down is ignored, so a held attack cannot skip it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Health", meta = (ClampMin = "0.0"))
	float DownedGiveUpGraceSeconds = 0.5f;

	/** Downed and the partner took the revive on: the HUD shows the ring and "[<partner> is coming]". */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	bool IsReviveExpected() const { return bDowned && bReviveExpected && !bDiedFromDown; }

	/** The partner on his way (null when no revive is coming). */
	APawn* GetReviver() const { return IsReviveExpected() ? Reviver.Get() : nullptr; }

	/** Seconds since she went down (0 when up). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	float GetDownedSeconds() const { return bDowned ? DownedSeconds : 0.f; }

	/** The share of DownedMaxSeconds left, 1 to 0: the HUD's ring. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	float GetDownedFractionLeft() const;

	/** The down ran out (or she gave up): the fade to the last save is under way. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Health")
	bool HasDiedFromDown() const { return bDiedFromDown; }

	/** Moves the down on by DeltaSeconds; at DownedMaxSeconds she dies. Tick runs it; public for tests. */
	void AdvanceDowned(float DeltaSeconds);

	/**
	 * Any key while downed with no revive coming: she dies now instead of waiting the timer out. False,
	 * and nothing happens, while a revive is coming, in the first DownedGiveUpGraceSeconds, or when up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Health")
	bool GiveUpFromDown();

	/**
	 * Why control cannot leave this character right now, or empty when it can: "mid-traversal",
	 * "mid-zip", "mid-takedown" or "down". AHawkeyePlayerController::SwitchCharacter asks this.
	 */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Character")
	FString GetSwitchBlocker() const;

	/** Lets go of everything the player was holding (sprint, aim, a draw, the wheel) when control leaves. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Character")
	void ReleaseHeldInputs();

	// --- Save (claude-docs/gameplay-semantics.md, "Save data") -----------------------------------

	//~ Begin ISpudObject interface
	/** CharacterName: Kate is spawned by the game mode, so her FName is not stable between loads. */
	virtual FString OverrideName_Implementation() const override;
	//~ End ISpudObject interface

	//~ Begin ISpudObjectCallback interface
	/** Copies health and the quiver into the SaveGame snapshot fields below. */
	virtual void SpudPreStore_Implementation(const USpudState* State) override;
	/** Applies the snapshot back to the health and inventory components. */
	virtual void SpudPostRestore_Implementation(const USpudState* State) override;
	//~ End ISpudObjectCallback interface

	/** Fills the snapshot from the components. Public so a test can check the round trip without SPUD. */
	void CaptureSaveSnapshot();

	/** Applies the snapshot to the components. Does nothing when no snapshot was ever captured. */
	void ApplySaveSnapshot();

	/** Fired when the Interact action is pressed; implement in Blueprint to drive doors, levers, pickups. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Hawkeye|Character")
	void OnInteractPressed();

	// --- Audio ------------------------------------------------------------------------------------

	/** Snow crunches, one picked at random per step, never the same one twice running (MS_Foot_Snow_0N). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Audio")
	TArray<TSoftObjectPtr<USoundBase>> FootstepSounds;

	/** Steps on a floor tagged SurfaceWood (boards, stair treads, a stage): MS_Footstep_Wood. Empty uses FootstepSounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Audio")
	TArray<TSoftObjectPtr<USoundBase>> WoodFootstepSounds;

	/** Steps on a floor tagged SurfaceCarpet: MS_Footstep_Carpet. Empty uses FootstepSounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Audio")
	TArray<TSoftObjectPtr<USoundBase>> CarpetFootstepSounds;

	/** What the floor she is standing on is, from its actor's tags (HawkeyeAudioMath::ClassifyFootstepSurface). */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Audio")
	EHawkeyeFootstepSurface GetFootstepSurface() const;

	/** The sounds a step on Surface picks from: the wood or carpet set, or FootstepSounds when that set is empty. */
	const TArray<TSoftObjectPtr<USoundBase>>& GetFootstepSoundsFor(EHawkeyeFootstepSurface Surface) const;

	/** Indoors the hip arm is capped at IndoorArmLength. BeginPlay sets it from the map's game mode. */
	UFUNCTION(BlueprintCallable, Category = "Hawkeye|Camera")
	void SetIndoorCamera(bool bIndoors) { bIndoorCamera = bIndoors; }

	UFUNCTION(BlueprintPure, Category = "Hawkeye|Camera")
	bool IsIndoorCamera() const { return bIndoorCamera; }

	/** A step every 70 cm walking, 55 cm sprinting, on the ground only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Audio")
	FHawkeyeFootstepTracker Footsteps;

	/** A landing from 40 cm or more; its Intensity input rises with the fall (MS_Land). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Audio")
	TSoftObjectPtr<USoundBase> LandSound;

	/** The landing roll going over (MS_Roll_Thump). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Audio")
	TSoftObjectPtr<USoundBase> RollSound;

	/** A hit that staggers her (MS_Melee_Stagger). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Audio")
	TSoftObjectPtr<USoundBase> StaggerSound;

	/** Snow kicked up behind each footstep (NS_FootstepSnow), on the same trigger as the sound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Effects")
	TSoftObjectPtr<UNiagaraSystem> FootstepVfx;

	/** A ring of snow round her feet on landing, scaled by the fall (NS_LandingSnow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Effects")
	TSoftObjectPtr<UNiagaraSystem> LandingVfx;

	/** The post-process material PlayScreenPulse blends in; its Intensity scalar is driven (M_PP_EmpAberration). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Effects")
	TSoftObjectPtr<UMaterialInterface> ScreenPulseMaterial;

	/** The landing sound's float input. */
	static const FName IntensityParameter;

	/** Steps taken since play began. */
	UFUNCTION(BlueprintPure, Category = "Hawkeye|Audio")
	int32 GetFootstepCount() const { return FootstepCount; }

protected:
	//~ Begin AActor interface
	virtual void PostInitializeComponents() override;
	//~ End AActor interface

	//~ Begin APawn interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PawnClientRestart() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~ End APawn interface

	//~ Begin ACharacter interface
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	//~ End ACharacter interface

	/** Adds DefaultMappingContext to the local player's Enhanced Input subsystem. */
	void AddDefaultMappingContext();

	// --- Input handlers -------------------------------------------------------------------------
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_SprintStarted(const FInputActionValue& Value);
	void Input_SprintCompleted(const FInputActionValue& Value);
	void Input_CrouchToggle(const FInputActionValue& Value);
	void Input_FirePressed(const FInputActionValue& Value);
	void Input_FireReleased(const FInputActionValue& Value);
	void Input_Reload(const FInputActionValue& Value);
	void Input_Takedown(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_AimStarted(const FInputActionValue& Value);
	void Input_AimCompleted(const FInputActionValue& Value);
	void Input_Slot1(const FInputActionValue& Value);
	void Input_Slot2(const FInputActionValue& Value);
	void Input_Slot3(const FInputActionValue& Value);
	void Input_Slot4(const FInputActionValue& Value);
	void Input_Slot5(const FInputActionValue& Value);
	void Input_Slot6(const FInputActionValue& Value);
	void Input_SlotScroll(const FInputActionValue& Value);
	void Input_Inventory(const FInputActionValue& Value);
	void Input_InventoryReleased(const FInputActionValue& Value);

	/** Opens the wheel once Tab has been held QuiverWheelHoldSeconds. */
	void UpdateQuiverWheelHold();

	/** Pushes the wheel's open state and highlight to the HUD. */
	void RefreshQuiverWheelHud() const;
	void Input_Grapple(const FInputActionValue& Value);
	void Input_CrouchReleased(const FInputActionValue& Value);
	void Input_MeleePressed(const FInputActionValue& Value);
	void Input_MeleeReleased(const FInputActionValue& Value);

	/** Starts Attack after turning to the soft-lock target, with Pose on the arms. Shared by the light and the heavy. */
	bool StartMelee(const FHawkeyeMeleeAttack& Attack, EHawkeyeStrikePose Pose);

	/** The next light of the chain, with the counter's bonus. */
	bool StartComboLight();

	/** The camera's flat forward (the body's without a controller): what "in front" means for her. */
	FVector GetViewForward() const;

	/** Slows the world to Dilation for RealSeconds (hit stop, parry, perfect dodge). */
	void ApplyTimeWarp(float RealSeconds, float Dilation);

	/** Turns the body toward the soft turn's yaw, and keeps orient-to-movement off while something owns her facing. */
	void UpdateSoftTurn(float DeltaSeconds);

	/** Hands the strike pose and the hit lean to the bow-IK anim instance. */
	void UpdateArmPoses();

	/** A dodge that starts inside a thug's telegraph: a moment of slow motion. */
	void CheckPerfectDodge();

	/** Counts the V hold; past HeavyHoldSeconds it becomes the heavy. */
	void UpdateMeleeHold(float DeltaSeconds);

	/** Counts a dodge, its invulnerability and its cooldown down. */
	void UpdateDodge(float DeltaSeconds);

	/** A Ctrl press while moving waits DodgeTapSeconds: released sooner it dodges, held it crouches. */
	void UpdateCrouchTap(float DeltaSeconds);

	/** Counts the stagger and the hit shake down. */
	void UpdateHitReactions(float DeltaSeconds);

	/** Writes the low-health desaturation and vignette onto the camera's post process. */
	void UpdateLowHealthPostProcess();

	UFUNCTION()
	void HandleStaggered(UHealthComponent* Health, AActor* DamageInstigator);

	UFUNCTION()
	void HandleHealthChanged(UHealthComponent* Health, float NewHealth, float Delta, AActor* DamageInstigator);

	UFUNCTION()
	void HandleMeleeLanded(AActor* HitActor, float DamageDealt, FName AttackName);

	UFUNCTION()
	void HandleMeleeMissed(FName AttackName);

	/** A strike clip's combo window closing lets go of the chain it held open. */
	UFUNCTION()
	void HandleComboWindowChanged(bool bOpen);

	UFUNCTION()
	void HandleFinisherStarted(AActor* Target, EHawkeyeFinisherStyle Style);

	UFUNCTION()
	void HandleFinisherStruck(AActor* Target);

	/** Time back to normal after the hit stop. */
	void EndHitStop();

	/** Picks the gait and writes its speed to CharacterMovement (walking and crouched). */
	void UpdateMaxWalkSpeed();

	/** Moves the aim blend one frame and writes arm length, socket offset and FOV. */
	void UpdateCamera(float DeltaSeconds);

	/** Counts the alerted thugs round her (every FightRecountSeconds) and moves the fight camera's blend. */
	void UpdateFightCamera(float DeltaSeconds);

	/** MeleeSparkVfx where her swing met HitActor: on his capsule's surface toward her, at chest height. */
	void SpawnMeleeSpark(const AActor* HitActor, bool bHeavy) const;

	/** Hides the body from its own camera while a wall has pulled the lens in to it. */
	void UpdateBodyVisibilityForCamera();

	/** Accumulates how long move input has been held; resets on the first frame without it. */
	void UpdateMoveInputTiming(float DeltaSeconds);

	/** Counts the slide down, steers it, and stands up at the end. */
	void UpdateSlide(float DeltaSeconds);

	/** Tracks the top of the arc while falling and counts the landing recovery down. */
	void UpdateFalling(float DeltaSeconds);

	/** Stands back up and restores friction. Safe when not sliding. */
	void EndSlide();

	/** Roll, stumble or dip, and fall damage, for a landing FallHeight below the top of the arc. */
	void ApplyLanding(float FallHeight);

	/** Walks the footstep tracker on by this frame's ground travel and plays a step when one is due. */
	void UpdateFootsteps();

	/** One of FootstepSounds at the feet, and the snow kick. */
	void PlayFootstep();

	/** FootstepVfx at the feet, thrown back and up from the way she moves. */
	void KickFootstepSnow();

	/**
	 * A roll along Direction: RollDistance over RollSeconds as a root motion force, the capsule down
	 * to RollCapsuleHalfHeight, the body tumbling once forward about the capsule centre.
	 */
	void StartRoll(const FVector& Direction);

	/** Counts the landing state on and ends it; the roll's body tumble and capsule are handled here. */
	void UpdateLanding(float DeltaSeconds);

	/** Puts the body and capsule back after a roll and clears the landing state. */
	void EndRoll();

	/** Writes one frame of the roll's tumble onto the body mesh. */
	void ApplyRollPose();

	/** Swaps the body between IdleAnim, WalkAnim, RunAnim and FallAnim. See LocomotionAnim.h. */
	void UpdateBodyLocomotion();

	/** Aiming faces the camera; otherwise the body turns towards where it moves. */
	void ApplyRotationMode();

	/**
	 * The sample's graph binds its own IA_Move, IA_Look, IA_Jump, IA_Sprint, IA_Crouch and IA_Aim
	 * events, and our actions sit at the same asset paths, so they would fire twice (and a crouch
	 * toggle would cancel itself). With the GASP bridge active every Blueprint action and key
	 * binding added after our own is removed; C++ owns input.
	 */
	void DropBlueprintInputBindings();

	/**
	 * Deactivates every camera on the actor other than FollowCamera: the sample's GameplayCamera
	 * rig (and the cine camera it spawns) and its spare CameraComponent. Our spring arm stays the
	 * view. Cheap when there is nothing to do; run on BeginPlay, possession and every tick.
	 */
	void SilenceForeignCameras();

	/**
	 * Copies the tuning properties onto the boom and CharacterMovement. Run at BeginPlay so a
	 * Blueprint child's values win over the constructor's.
	 */
	void ApplyTuningToComponents();

	// --- Components -----------------------------------------------------------------------------

	/**
	 * Turned by the control rotation; lags behind in position and rotation; probes with a sphere on
	 * the Camera channel and pulls in against walls. Length and offset are driven every frame.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** On the end of CameraBoom. The boom does the rotating, so this does not follow the pawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UTakedownComponent> TakedownComponent;

	/**
	 * Set by Input_Takedown when it executes one, consumed by Input_Interact on the very same
	 * key press. Takedown and Interact are bound to separate keyboard keys (F, E) but share the
	 * gamepad Y face button; Takedown is bound first, so this is how it wins that press instead
	 * of both firing.
	 */
	bool bTookDownThisPress = false;

	/** The frame bTookDownThisPress was set on; Input_Interact skips only a press on that frame. */
	uint64 TakedownPressFrame = 0;

	/** Hands only: the punch while no bow is owned (bHasWeapon false). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UWeaponComponent> WeaponComponent;

	/** Draw, release, arrows; the bow on her back and in her hand. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UBowComponent> BowComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UInteractionComponent> InteractionComponent;

	/** The bow, a seven-slot quiver (standard arrows always in slot 1) and the keycard ring. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UInventoryComponent> InventoryComponent;

	/** Picks the anchor under the camera, fires the grapple arrow and runs the zip. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UGrappleComponent> GrappleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFinisherComponent> FinisherComponent;

	/** Light and heavy bow strikes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UMeleeComponent> MeleeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UHitReactionComponent> HitReactionComponent;

	/** Vault, mantle, ledge grab; auto while sprinting, on the jump key at any speed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UParkourComponent> ParkourComponent;

	/** What AISense_Hearing listens to. MakeNoise routes through this. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hawkeye|Components")
	TObjectPtr<UPawnNoiseEmitterComponent> NoiseEmitter;

	// --- Input assets ---------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (ClampMin = "0"))
	int32 DefaultMappingPriority = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/**
	 * The gamepad right stick. Kept separate from LookAction (mouse) so the handler can scale by
	 * delta time and StickSensitivity: a mouse delta is already a per-frame pixel count, but a
	 * stick reports a held position and needs an explicit rate.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookStickAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> TakedownAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	/** Number key 1: standard arrows. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot1Action;

	/** Number keys 2..6: the other quiver slots. An empty slot's key does nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot2Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot3Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot4Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot5Action;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Slot6Action;

	/** Mouse wheel. Positive is the next quiver slot, negative the previous; empty slots are skipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SlotScrollAction;

	/** Tab: tap opens the read-only inventory screen (paused); hold opens the quiver wheel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InventoryAction;

	/** Q: fire a grapple arrow at the marked anchor (or chain, late in a zip), whatever slot is nocked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> GrappleAction;

	/** V: tap for a light bow strike, hold for the heavy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MeleeAction;

	// --- Melee tuning ---------------------------------------------------------------------------

	/**
	 * Her combat clips (DA_AnimSet_Kate, DA_AnimSet_Clint). Empty roles, or none at all, keep the
	 * procedural strikes, dash and reactions (claude-docs/gameplay-semantics.md, "Combat animation clips").
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Animation")
	TObjectPtr<UCombatAnimSet> CombatAnimSet;

	/** The tap: 15 damage, lands 0.1 s in, 0.3 s in all, staggers, 20 cm lunge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee")
	FHawkeyeMeleeAttack LightAttack;

	/** The hold: 35 damage after a 0.6 s wind-up, knocks a thug down, 40 cm lunge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee")
	FHawkeyeMeleeAttack HeavyAttack;

	/** The chain's second light: 15, like the first, a longer lunge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee")
	FHawkeyeMeleeAttack ComboFollowAttack;

	/** The chain's third light: 25 and a 150 cm knockback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee")
	FHawkeyeMeleeAttack ComboFinishAttack;

	/** The chain window, the counter's reset and its bonus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee")
	FHawkeyeComboTracker Combo;

	/**
	 * A combo ender (the chain's third light, or a heavy) landing on a thug opens a finisher on him for
	 * this long, s. Ordinary hits never do; knockdowns and parry staggers are open on their own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float ComboEnderFinisherSeconds = 1.f;

	/** Seconds the soft lock takes to turn her to the target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float SoftTurnSeconds = 0.1f;

	/** A telegraphing thug this close and this far off her view is parried by a tap of V, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Parry", meta = (ClampMin = "0.0"))
	float ParryRange = 250.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Parry", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ParryAngleDegrees = 70.f;

	/** How long a parried thug is staggered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Parry", meta = (ClampMin = "0.0"))
	float ParryStaggerSeconds = 1.5f;

	/** Real seconds of hit stop on a parry: four frames at 60. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Parry", meta = (ClampMin = "0.0"))
	float ParryHitStopSeconds = 0.067f;

	/** The parry's ring (NS_ParryRing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Parry")
	TSoftObjectPtr<UNiagaraSystem> ParryVfx;

	/** The parry's ring of metal (MS_Parry). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Parry")
	TSoftObjectPtr<USoundBase> ParrySound;

	/** A dodge started while a thug this close is in a telegraph is a perfect dodge, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.0"))
	float PerfectDodgeRange = 300.f;

	/** The perfect dodge's slow motion: real seconds, and how slow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.0"))
	float PerfectDodgeSeconds = 0.1f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float PerfectDodgeDilation = 0.3f;

	/** The arms on a strike (no attack clips): where the hands go and how fast. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee")
	FHawkeyeStrikePoseClock StrikePose;

	/** The lean away from a hit on her. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee")
	FHawkeyeHitLeanClock HitLean;

	/**
	 * A light pressed this close to the end of any swing's recovery (a heavy's included) is kept and goes as the swing ends,
	 * s. (Inside a light chain's window it is kept whenever pressed, as before.)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float LightBufferSeconds = 0.3f;

	/** V held this long becomes the heavy; let go sooner and it was a tap. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float HeavyHoldSeconds = 0.4f;

	/** A thug this close and this far off the camera's forward is who a swing turns to face. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float SoftLockRange = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SoftLockAngleDegrees = 60.f;

	/** The melee assist: a strike picks the best thug this close (flat, cm)... */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee|Assist", meta = (ClampMin = "0.0"))
	float MeleeAssistRange = 350.f;

	/** ...and this far off the stick (or the camera), degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee|Assist", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float MeleeAssistAngleDegrees = 60.f;

	/** With nobody in the stick's or the camera's cone, the nearest thug this close is the pick, whatever the angle (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee|Assist", meta = (ClampMin = "0.0"))
	float MeleeAssistNearRange = 150.f;

	/** The stick counts as where she means to strike when it was pushed this recently, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee|Assist", meta = (ClampMin = "0.0"))
	float MeleeAssistInputSeconds = 0.15f;

	/** After the soft turn, the wind-up keeps her facing the assist target at up to this rate, degrees per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee|Assist", meta = (ClampMin = "0.0"))
	float MeleeAssistTrackDegreesPerSecond = 720.f;

	/**
	 * The world's dilation for the parry's and the perfect dodge's stop (a landed strike's hit-stop is the two bodies'
	 * own, UHitReactionComponent).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float HitStopTimeDilation = 0.1f;

	/** A landed blow of weight 1 (her heavy) pushes the lens this far in toward her and back, cm (times the camera shake scale). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float HeavyCameraPunchDistance = 2.5f;

	/** The same at weight 0; between the two by the blow's weight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float LightCameraKickDistance = 0.8f;

	/** Real seconds the punch takes, in and back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float HeavyCameraPunchSeconds = 0.1f;

	/**
	 * The spark where her strike lands (NS_MeleeSpark): small and at the contact point, so the thug's own hit
	 * flash is what reads; a heavy's is a little bigger.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hawkeye|Melee")
	TSoftObjectPtr<UNiagaraSystem> MeleeSparkVfx;

	/** The spark's scale on a light; a heavy's is HeavySparkScale. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float LightSparkScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float HeavySparkScale = 1.4f;

	/**
	 * How far her axis must be off the lens's line to the spark, cm, for it to show past her; nearer, the spark
	 * slides round his body (up to 0.9 of his radius across) to the side the lens sees.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float SparkClearance = 40.f;

	/** The marker on the assist's pick: its fade after the swing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee|Assist")
	FHawkeyeTargetMarker MeleeTargetMarker;

	// --- Dodge ----------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.0"))
	float DodgeDistance = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.05"))
	float DodgeSeconds = 0.4f;

	/** The start of the dodge during which nothing can hurt her. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.0"))
	float DodgeInvulnerableSeconds = 0.25f;

	/** From the start of one dodge to the earliest the next can start. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.0"))
	float DodgeCooldownSeconds = 0.8f;

	/** A Ctrl press released within this is a dodge; held longer (or pressed standing) it crouches. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Dodge", meta = (ClampMin = "0.0"))
	float DodgeTapSeconds = 0.2f;

	// --- Hit reactions --------------------------------------------------------------------------

	/** Seconds a staggering hit takes her movement and attacks away. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float PlayerStaggerSeconds = 0.35f;

	/** Speed she is shoved away from a staggering hit at, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Melee", meta = (ClampMin = "0.0"))
	float PlayerStaggerShove = 250.f;

	/** How long the camera shakes when a hit lands on her. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float HitShakeSeconds = 0.2f;

	/** How far the lens is thrown about by a hit, cm, at the start of the shake. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float HitShakeAmplitude = 6.f;

	/** Below this fraction of max health the picture starts to drain and close in. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Health", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthThreshold = 0.4f;

	/** Colour saturation at zero health (1 is normal). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Health", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthSaturation = 0.2f;

	/** Vignette intensity at zero health (the engine default is 0.4). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Health", meta = (ClampMin = "0.0"))
	float LowHealthVignette = 1.2f;

	// --- Movement tuning ------------------------------------------------------------------------

	/** Light stick input, and the speed while aiming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 250.f;

	/** The default on foot. Keyboard input is always full, so the keyboard runs unless sprinting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float RunSpeed = 500.f;

	/** Sprint held. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 700.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float CrouchSpeed = 200.f;

	/** Stick deflection at or above which the character runs straight away. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FullInputThreshold = 0.9f;

	/** Stick deflection below which the character always walks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LightInputThreshold = 0.4f;

	/** A medium stick breaks from walk into run after being held this long. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float RunAfterHeldSeconds = 0.2f;

	/** Apex of a standing jump, cm. JumpZVelocity is derived from it and gravity at BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float JumpHeight = 90.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JumpAirControl = 0.3f;

	/** Yaw the body turns at, degrees per second, towards movement or (aiming) the camera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float TurnRateDegrees = 720.f;

	/** Sprint input is held. Only moves the character at SprintSpeed while it is on its feet. */
	UPROPERTY(BlueprintReadOnly, Category = "Hawkeye|Movement")
	bool bIsSprinting = false;

	// --- Slide ----------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Slide", meta = (ClampMin = "0.05"))
	float SlideSeconds = 0.7f;

	/** Speed at the start of a slide; eases down to CrouchSpeed by the end. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Slide", meta = (ClampMin = "0.0"))
	float SlideSpeed = 750.f;

	/** A sprint slower than this crouches instead of sliding. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Slide", meta = (ClampMin = "0.0"))
	float SlideMinSpeed = 350.f;

	/** Ground friction while sliding; low so the slide carries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Slide", meta = (ClampMin = "0.0"))
	float SlideGroundFriction = 0.5f;

	/** Capsule half-height while crouched or sliding, cm. Applied at BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Slide", meta = (ClampMin = "10.0"))
	float CrouchedCapsuleHalfHeight = 50.f;

	/**
	 * A fall ends a crouch once it has lasted this long and dropped CrouchKeepFallHeight below its top: a
	 * shorter or shallower one (a stair's lip, a one-frame flick to falling on a landing) keeps it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float CrouchKeepFallSeconds = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Movement", meta = (ClampMin = "0.0"))
	float CrouchKeepFallHeight = 30.f;

	// --- Falling --------------------------------------------------------------------------------

	/** A landing from above this height rolls: a short speed and camera dip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float RollHeight = 400.f;

	/** How long a controlled drop's dip lasts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float LandingRecoverSeconds = 0.3f;

	/** Length of a landing roll, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.01"))
	float RollSeconds = 0.5f;

	/** How far a roll carries her along the move direction, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float RollDistance = 200.f;

	/** Capsule half-height through a roll, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "10.0"))
	float RollCapsuleHalfHeight = 50.f;

	/** How far the camera pitches down at the middle of a roll, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float RollCameraPitchDegrees = 8.f;

	/** Input is ignored for this much of the roll, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float RollInputLockSeconds = 0.35f;

	/** Landing with move input held, or faster than this, counts as moving: a roll, not a stumble. cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float RollMinSpeed = 150.f;

	/** The body turns once over in this much of the roll, then straightens, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.01"))
	float RollTumbleSeconds = 0.4f;

	/** Height of the body through the tumble as a fraction of its own, so the tuck clears the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float RollTuckScale = 0.55f;

	/** Landing standing still from above RollHeight: speed climbs from 0 back to a run over this, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.01"))
	float StumbleSeconds = 0.4f;

	/** A controlled drop (not a jump) landing from above this height gets the landing dip too, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float ControlledDropDipHeight = 150.f;

	/** Speed multiplier for the length of a controlled drop's dip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LandingSpeedMultiplier = 0.5f;

	/** How far the camera drops at the bottom of a controlled drop's dip, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float LandingCameraDip = 30.f;

	/** Falls shorter than this cost nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float FallDamageMinHeight = 900.f;

	/** Falls from here up cost FallDamageMaxFraction; about the tallest tenement on the block. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0"))
	float FallDamageMaxHeight = 2500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallDamageMinFraction = 0.1f;

	/** The cap. A fall never kills either way: damage stops one point short of the last. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Falling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallDamageMaxFraction = 0.6f;

	// --- Camera ---------------------------------------------------------------------------------

	/** Where the camera sits while not aiming: behind, above and a little right, so the body sits left of centre. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera")
	FHawkeyeCameraTargets HipCamera = { 350.f, FVector(0.f, 70.f, 60.f), 90.f };

	/**
	 * Where it sits while aiming: in close, well out over the right shoulder and a little high, so her
	 * raised bow arm sits left of centre and the reticle area stays clear.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera")
	FHawkeyeCameraTargets AimCamera = { 180.f, FVector(0.f, 70.f, 65.f), 70.f };

	/**
	 * Indoors (a map whose game mode sets bInteriorCamera) the hip arm is never longer than this, so
	 * the lens stays in the room rather than pressing into the wall behind her. Aiming is unchanged.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float IndoorArmLength = 250.f;

	/** Set at BeginPlay from the game mode (AHawkeyeGameMode::WantsInteriorCamera), or by SetIndoorCamera. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Camera")
	bool bIndoorCamera = false;

	/** Seconds the camera takes to travel the whole way between HipCamera and AimCamera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float AimBlendSeconds = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float CameraLagSpeed = 10.f;

	/**
	 * The fight camera: with two or more alerted thugs within 6 m the hip boom lengthens 70 cm and the lens
	 * tips down 4 degrees over 0.5 s, back over 1 s once the fight thins out. Never while aiming; a
	 * finisher's push-in takes over from it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera")
	FHawkeyeFightCameraSettings FightCamera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float CameraRotationLagSpeed = 12.f;

	/** Camera pitch (up positive) above which the hip arm starts to shorten and lift, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float LookUpPitchStart = 20.f;

	/** Camera pitch at which the hip arm is fully LookUpArmLength and LookUpSocketZ, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float LookUpPitchFull = 60.f;

	/** Hip arm length when looking steeply up, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float LookUpArmLength = 220.f;

	/** Hip socket height when looking steeply up, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera")
	float LookUpSocketZ = 110.f;

	/**
	 * World-up lift of the arm's pivot when looking steeply up, cm, blended in with the other two.
	 * The socket offset turns with the arm, so at 50 degrees and more it hardly raises the lens;
	 * this keeps the lens off the pavement all the way to CameraPitchMax.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera")
	float LookUpPivotLift = 150.f;

	/** Lowest the camera can look, degrees (down is negative). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float CameraPitchMin = -70.f;

	/** Highest the camera can look, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float CameraPitchMax = 75.f;

	/** Hands CameraPitchMin/Max to the player camera manager, which clamps the look input with them. */
	void ApplyCameraPitchLimits();

	/** Radius of the sphere the arm sweeps to find walls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float CameraProbeSize = 12.f;

	/**
	 * When a wall pulls the lens closer than this to the arm's pivot, the body is hidden from the
	 * player's view (it still casts its shadow). Otherwise backing into a tenement fills the
	 * screen with the inside of Kate's shoulder.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Camera", meta = (ClampMin = "0.0"))
	float CameraHideBodyDistance = 100.f;

	/** The look's mesh while BodyLook is on; null otherwise. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> LookMesh;

	// --- Look -----------------------------------------------------------------------------------

	/**
	 * Multiplier on the raw Look input. The mouse mapping is 1 degree per unit, which is far
	 * too fast to hold an aim; this is the one number to change when the mouse feels wrong.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.02"))
	float LookSensitivity = 0.2f;

	/**
	 * The sensitivity UHawkeyeSettingsSubsystem last handed us. The subsystem wins whenever one
	 * exists; LookSensitivity above is only the fallback for a world without a game instance,
	 * which is every automation test.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Input")
	float SettingsLookSensitivity = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Input")
	bool bHasSettingsLookSensitivity = false;

	/** LookSensitivity is multiplied by this again while aiming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float AimLookMultiplier = 0.7f;

	/** Gamepad stick full-deflection yaw rate, before StickSensitivity and the aim halving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "1.0"))
	float StickYawDegreesPerSecond = 180.f;

	/** Gamepad stick full-deflection pitch rate, before StickSensitivity and the aim halving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "1.0"))
	float StickPitchDegreesPerSecond = 120.f;

	/** Aiming halves the stick look rate, the same as it slows the mouse via AimLookMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float AimStickRateMultiplier = 0.5f;

	/**
	 * The multiplier UHawkeyeSettingsSubsystem last handed us for StickSensitivity. Only the
	 * fallback for a world without a game instance, same as SettingsLookSensitivity above.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Input")
	float SettingsStickSensitivity = 1.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Input")
	bool bHasSettingsStickSensitivity = false;

	/** Fallback StickSensitivity for a world with no settings subsystem (every automation test). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.2", ClampMax = "3.0"))
	float StickSensitivity = 1.f;

	/**
	 * On, mouse look pitch is flipped: pushing the mouse forward looks down. Matches
	 * FHawkeyeSettings::bInvertMouseY once a settings subsystem exists; false (not inverted) is
	 * the fallback for a world without one, same as LookSensitivity above.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bInvertMouseY = false;

	/** On, gamepad stick look pitch is flipped: pushing the stick up looks down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bInvertStickY = false;

	/** Reads the current sensitivity out of the settings subsystem and subscribes to changes. */
	void BindToSettingsSubsystem();

	void UnbindFromSettingsSubsystem();

	UFUNCTION()
	void HandleSettingsChanged(FHawkeyeSettings Settings);

	/** Aim and crouch by press or by hold (FHawkeyeSettings). */
	bool bToggleAim = false;
	bool bToggleCrouch = true;

	/** The sprint button's setting, and the toggle's state while a press toggled it. */
	EHawkeyeSprintMode SprintMode = EHawkeyeSprintMode::Default;
	FHawkeyeSprintToggle SprintToggle;

	/** Moves the toggle a frame: the stick centred past its 0.6 s turns it off. */
	void UpdateSprintToggle(float DeltaSeconds);

	/** The move stick was off centre this frame, whether or not the move was locked out (a zip, a vault). */
	bool bStickPushedThisFrame = false;

	/** The accessibility scales and the difficulty's numbers ApplySettings last took. */
	float CameraShakeScale = 1.f;
	float FlashScale = 1.f;
	float FallDamageScale = 1.f;
	float ParryWindowDelta = 0.f;

	/** Story's early parry: a tap that met no telegraph waits this long for one to start. */
	float ParryBufferRemaining = 0.f;

	/** Set while the buffered early tap tries its parry, so her own jab's wind-up does not refuse it. */
	bool bParryOverOwnWindup = false;

	/** Spends a waiting early tap on a telegraph that has just started. */
	void UpdateParryBuffer(float DeltaSeconds);

	/** Gamepad right stick: Value is a held -1..1 position, not a per-frame delta like the mouse. */
	void Input_LookStick(const FInputActionValue& Value);

	// --- Aim ------------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Hawkeye|Aim")
	bool bIsAiming = false;

	/** The aim button is held (not just the bow forcing the aim). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Aim")
	bool bAimInputHeld = false;

	/** Walk speed multiplier while the bow is drawn, on top of the aimed walk. Non-sample characters only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hawkeye|Aim", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DrawWalkSpeedMultiplier = 0.6f;

	// --- Animation ------------------------------------------------------------------------------

	/** Body animation while standing still. The same sequence the thugs use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Animation")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Animation")
	TObjectPtr<UAnimSequence> WalkAnim;

	/** Played above RunAnimSpeedThreshold. Falls back to WalkAnim when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Animation")
	TObjectPtr<UAnimSequence> RunAnim;

	/** Played while falling. Falls back to the ground animation when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Animation")
	TObjectPtr<UAnimSequence> FallAnim;

	/** WalkAnim above this much ground speed, IdleAnim below it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Animation", meta = (ClampMin = "0.0"))
	float WalkAnimSpeedThreshold = 20.f;

	/** RunAnim above this much ground speed; between walk and run speed so each reads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hawkeye|Animation", meta = (ClampMin = "0.0"))
	float RunAnimSpeedThreshold = 375.f;

	// --- Noise ----------------------------------------------------------------------------------

	/** Seconds between movement noise events while moving. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Noise", meta = (ClampMin = "0.01"))
	float NoiseIntervalSeconds = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Noise", meta = (ClampMin = "0.0"))
	float SprintNoiseLoudness = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hawkeye|Noise", meta = (ClampMin = "0.0"))
	float WalkNoiseLoudness = 0.4f;

	/** Timer body: emits one movement noise event if the player is making any. */
	void EmitMovementNoise();

	UFUNCTION()
	void HandleDeath(UHealthComponent* Health, AActor* Killer);

	/** Asks this character's partner for the once-a-fight revive. True when he took it on (she is down). */
	bool TryPartnerRevive(AActor* Killer);

	/** Goes down: bReviveExpected says whether a partner is coming. Starts the DownedMaxSeconds clock. */
	void EnterDowned(bool bInReviveExpected, APawn* InReviver);

	/** The down is over without a revive: logged, then the game mode fades to the last save. */
	void DieFromDown(const TCHAR* Why);

	// Saved by SPUD with the actor (UPROPERTY SaveGame). Components are not saved by SPUD, so the
	// health and quiver are mirrored here just before a store and applied just after a restore.

	/** Health at the save; negative when nothing was captured. */
	UPROPERTY(SaveGame)
	float SavedHealth = -1.f;

	/** The carried bow as a soft object path, empty for none. */
	UPROPERTY(SaveGame)
	FString SavedBowPath;

	/** Each filled quiver slot's arrow asset path, parallel to SavedArrowCounts. */
	UPROPERTY(SaveGame)
	TArray<FString> SavedArrowPaths;

	UPROPERTY(SaveGame)
	TArray<int32> SavedArrowCounts;

	UPROPERTY(SaveGame)
	int32 SavedActiveArrowSlot = 1;

	/** The keycard ring (the toy sacks a chapter asks for are keycards too), so a load keeps what she carries. */
	UPROPERTY(SaveGame)
	TArray<FName> SavedKeycards;

	/** Set while waiting for the partner's revive. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Health")
	bool bDowned = false;
	/** SetScriptedLock: a beat holds her input. */
	bool bScriptedLock = false;

	/** The down's clock and whether anyone is coming; see IsReviveExpected. */
	float DownedSeconds = 0.f;
	bool bReviveExpected = false;
	bool bDiedFromDown = false;
	TWeakObjectPtr<APawn> Reviver;

	/** See SetAIGait. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Movement")
	EHawkeyeGait AIGait = EHawkeyeGait::Run;

	// --- Runtime state --------------------------------------------------------------------------

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Movement")
	EHawkeyeGait CurrentGait = EHawkeyeGait::Walk;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Slide")
	bool bIsSliding = false;

	/** Seconds of slide left. */
	UPROPERTY(Transient)
	float SlideRemaining = 0.f;

	/** Ground direction the slide travels in, fixed when it starts. */
	UPROPERTY(Transient)
	FVector SlideDirection = FVector::ForwardVector;

	/** GroundFriction before the slide lowered it. */
	UPROPERTY(Transient)
	float PreSlideGroundFriction = 8.f;

	/** Length of the last move input, 0..1. Keyboard is always 1. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Movement")
	float MoveInputMagnitude = 0.f;

	/** How long move input has been held without a break. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Movement")
	float MoveInputHeldSeconds = 0.f;

	/** Set by Input_Move, cleared by the next Tick: how the tick knows the stick was let go. */
	bool bMoveInputThisFrame = false;

	/** Highest capsule Z since the character last left the ground. */
	UPROPERTY(Transient)
	float FallApexZ = 0.f;

	/** Seconds in the air since she last left the ground (the crouch's real-fall test). */
	float FallSeconds = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Hawkeye|Falling")
	float LastFallHeight = 0.f;

	UPROPERTY(Transient)
	EHawkeyeLanding LandingState = EHawkeyeLanding::None;

	UPROPERTY(Transient)
	float LandingElapsed = 0.f;

	UPROPERTY(Transient)
	FVector RollDirection = FVector::ZeroVector;

	/** The roll crouched the capsule (she was standing), so it stands her back up. */
	bool bRollOwnsCrouch = false;

	/** The crouched half-height before the roll set its own. */
	float PreRollCrouchedHalfHeight = 0.f;

	/** The body's mount on the capsule once crouched, which the tumble is built from. */
	FVector RollMeshBaseLocation = FVector::ZeroVector;
	FQuat RollMeshBaseRotation = FQuat::Identity;
	FVector RollMeshBaseScale = FVector::OneVector;

	/** Set when a fall starts without a jump; cleared on landing. */
	UPROPERTY(Transient)
	bool bControlledDrop = false;

	/** Aim blend, 0 hip to 1 aimed, moved at 1 / AimBlendSeconds. */
	UPROPERTY(Transient)
	float AimAlpha = 0.f;

	// --- Combat state ---------------------------------------------------------------------------

	UPROPERTY(Transient)
	float DodgeRemaining = 0.f;

	UPROPERTY(Transient)
	float DodgeInvulnerableRemaining = 0.f;

	UPROPERTY(Transient)
	float DodgeCooldownRemaining = 0.f;

	UPROPERTY(Transient)
	float StaggerRemaining = 0.f;

	UPROPERTY(Transient)
	float HitShakeRemaining = 0.f;

	/** SuppressMotionBlur: seconds left, and the camera's own motion blur override to put back. */
	float MotionBlurOffRemaining = 0.f;
	bool bSavedMotionBlurOverride = false;
	float SavedMotionBlurAmount = 0.f;

	/** Length and throw of the shake now running: a hit's, or a blast's from PlayImpactShake. */
	UPROPERTY(Transient)
	float ActiveShakeSeconds = 0.2f;

	/** The screen pulse now running: seconds left, its length and its starting strength. */
	float ScreenPulseRemaining = 0.f;
	float ScreenPulseSeconds = 0.f;
	float ScreenPulseStart = 0.f;

	/** The pulse material's instance on the follow camera, made on the first pulse. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenPulseInstance = nullptr;

	/** Fades the screen pulse and pushes it to the camera. */
	void UpdateScreenPulse(float DeltaSeconds);

	UPROPERTY(Transient)
	float ActiveShakeAmplitude = 6.f;

	/** Tab is down and has not yet become the wheel. */
	bool bInventoryKeyHeld = false;
	double InventoryKeyDownRealSeconds = 0.0;
	bool bQuiverWheelOpen = false;
	FVector2D QuiverWheelCursor = FVector2D::ZeroVector;

	/** The world direction of the last move input, for the dodge. */
	UPROPERTY(Transient)
	FVector LastMoveWorldDirection = FVector::ZeroVector;

	/** Game time LastMoveWorldDirection was last set; below 0 before any move input. */
	double LastMoveInputSeconds = -1.0;

	/** The aim snap: from and to, and how long is left. */
	FRotator AimSnapFrom = FRotator::ZeroRotator;
	FRotator AimSnapTo = FRotator::ZeroRotator;
	float AimSnapRemaining = 0.f;

	/** The last look input came from the stick (the pad's magnetism), not the mouse. */
	bool bLookFromStick = false;

	TWeakObjectPtr<AActor> AimAssistTarget;

	/** Where the reticle looks from and along: the player's view, or the eyes with no player controller. */
	void GetAimAssistView(FVector& OutLocation, FRotator& OutRotation) const;

private:
	FTimerHandle NoiseTimerHandle;
	FTimerHandle HitStopTimerHandle;

	/** FPlatformTime of the last landed blow's hit-stop, -1 for none (stops never stack). */
	double LastHitStopRealSeconds = -1.0;

	/** The debug line's traversal speed: how fast the actor really moves, smoothed (a hang has no velocity). */
	void UpdateMovementDebugSample(float DeltaSeconds);
	FVector DebugLastLocation = FVector::ZeroVector;
	bool bDebugHasLocation = false;
	float DebugTraversalSpeed = 0.f;
	EHawkeyeParkourMove DebugMoveSeen = EHawkeyeParkourMove::None;
	float DebugMoveSeconds = 0.f;

	/** Where the feet were last frame, for the footstep distance. */
	FVector LastFootLocation = FVector::ZeroVector;
	bool bHasFootLocation = false;
	int32 LastFootstepIndex = INDEX_NONE;
	int32 FootstepCount = 0;

	/** V is held and has not yet become a heavy. */
	bool bMeleeHeld = false;
	float MeleeHeldSeconds = 0.f;

	/** A light pressed in the recovery of a light that landed, waiting for it to end. */
	bool bLightBuffered = false;

	/** The swing now going: a light of this chain step, or (false) a heavy. */
	bool bSwingIsLight = false;
	int32 SwingStep = 0;

	/** The soft turn: the yaw she is turning to and the seconds left. */
	float SoftTurnYaw = 0.f;
	float SoftTurnRemaining = 0.f;

	/** Orient-to-movement was switched off because something owns her facing. */
	bool bFacingLocked = false;

	/** The dodge running was aimed at a soft-lock target, so she keeps facing it. */
	bool bDodgeFacesTarget = false;

	int32 ParryCount = 0;
	int32 PerfectDodgeCount = 0;
	int32 DodgeCount = 0;
	int32 HitsTakenCount = 0;
	float DamageTakenTotal = 0.f;

	/** A Ctrl press while moving, waiting to find out whether it is a tap (dodge) or a hold (crouch). */
	bool bCrouchTapPending = false;
	float CrouchTapHeldSeconds = 0.f;

	/** Why the crouch is being ended, set before the movement component stands her up; logged by OnEndCrouch. */
	FString PendingCrouchEndReason;
	bool bPendingCrouchEndUnasked = false;
	FString LastCrouchEndReason;
	int32 CrouchEndCount = 0;
	int32 UnaskedCrouchEndCount = 0;

	/** The dodge turned invulnerability on, so the dodge is what turns it off. */
	bool bDodgeOwnsInvulnerability = false;

	/** The low-health alpha last written, so the post process is only touched on a change. */
	float LastLowHealthAlpha = 0.f;

	/** Action bindings SetupPlayerInputComponent made; anything after them came from a Blueprint. */
	int32 NativeActionBindingCount = INDEX_NONE;

	int32 DroppedBlueprintInputBindings = 0;

	/** Logged once per actor: that the sample's graph, not our gait speeds, sets MaxWalkSpeed. */
	bool bLoggedGaspSpeedOwnership = false;

	/** Logged once per actor, the first time a foreign camera had to be switched off. */
	bool bLoggedForeignCamera = false;

	/** Set while StopAim is letting the bow down, so the draw's own end does not re-enter it. */
	bool bStoppingAim = false;

	/** The fight camera's blend, 0 to 1, and the alerted thugs round her at the last count. */
	float FightCameraAlpha = 0.f;
	int32 EngagedThugCount = 0;
	float FightRecountRemaining = 0.f;

	/** Real seconds into the heavy's camera punch; below 0 when none is running. */
	float CameraPunchElapsed = -1.f;

	/** How far the current kick pushes the lens, cm, by the blow's weight. */
	float CameraKickDistance = 0.f;

	/** The fight is recounted this often, s. */
	static constexpr float FightRecountSeconds = 0.1f;

	/** Whichever of IdleAnim / WalkAnim the body is playing, so Tick only re-plays on a change. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentLocomotionAnim;
};
