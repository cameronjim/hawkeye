#pragma once

#include "CoreMinimal.h"
#include "Combat/BossPhaseComponent.h"
#include "Combat/WeaponDefinition.h"
#include "HawkeyePlayerController.h"
#include "Combat/Takedownable.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Player/HawkeyeCharacter.h"
#include "Settings/HawkeyeSettings.h"
#include "UI/ChallengeResultsWidget.h"
#include "UI/HawkeyeInventoryWidget.h"
#include "World/ThugCharacter.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "UObject/Script.h"
#include "HawkeyeTestUtils.generated.h"

class AGrappleAnchor;
class AThugAIController;
class UArrowDefinition;
class UBoxComponent;
class UFlashbackDefinition;
class UHealthComponent;
class UInventoryComponent;
class UMissionDefinition;
class UMissionObjective;
class UMissionTracker;

/**
 * RAII test world. Automation tests only need one when a component requires a real actor owner;
 * everything else is exercised with NewObject and no world at all.
 */
struct HAWKEYE_API FHawkeyeTestWorld
{
	FHawkeyeTestWorld();
	~FHawkeyeTestWorld();

	FHawkeyeTestWorld(const FHawkeyeTestWorld&) = delete;
	FHawkeyeTestWorld& operator=(const FHawkeyeTestWorld&) = delete;

	UWorld* Get() const { return World; }

	/** Spawns an actor of ActorClass at Location facing Forward's yaw. */
	AActor* SpawnActor(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation) const;

	/**
	 * Runs the world's clock on by Seconds without ticking anything in it. Tests step thinks and swings by hand,
	 * but rules timed on the world's clock (the attack tokens' gap) need it to move.
	 */
	void AdvanceClock(float Seconds) const;

private:
	/**
	 * A world built by hand is not a standalone game world, so AActor::GetFunctionCallspace can
	 * decide an event belongs on a remote machine and ProcessEvent silently does nothing - which
	 * makes every ITakedownable BlueprintNativeEvent return its default. This guard forces local
	 * execution for as long as the test world lives.
	 */
	FEditorScriptExecutionGuard ScriptExecutionGuard;

	UWorld* World = nullptr;
};

/**
 * One step of a thug's brain as a frame of the game runs it: his senses (AThugAIController::Think), then ST_Thug,
 * both DeltaSeconds long. Test worlds tick nothing, so the tree is ticked by hand.
 */
HAWKEYE_API void ThinkThug(AThugAIController* Brain, float DeltaSeconds);

/**
 * Counting sink for the project's dynamic multicast delegates. Dynamic delegates cannot bind
 * lambdas, so tests bind one of these and assert on the counters afterwards.
 */
UCLASS()
class HAWKEYE_API UHawkeyeTestListener : public UObject
{
	GENERATED_BODY()

public:
	// --- Health ---------------------------------------------------------------------------------
	UPROPERTY() int32 HealthChangedCount = 0;
	UPROPERTY() float LastNewHealth = 0.f;
	UPROPERTY() float LastHealthDelta = 0.f;
	UPROPERTY() int32 DeathCount = 0;

	/** Who did each hit (health going down) since the caller last emptied it; null for a fall. */
	UPROPERTY() TArray<TObjectPtr<AActor>> DamageInstigators;

	/** Who OnDeath said did it. A takedown that reports None is the bug this catches. */
	UPROPERTY() TObjectPtr<AActor> LastKiller = nullptr;

	UFUNCTION()
	void HandleHealthChanged(UHealthComponent* HealthComponent, float NewHealth, float Delta, AActor* DamageInstigator);

	UFUNCTION()
	void HandleDeath(UHealthComponent* HealthComponent, AActor* Killer);

	// --- Boss phases ----------------------------------------------------------------------------
	UPROPERTY() int32 PhaseChangedCount = 0;
	UPROPERTY() int32 LastOldPhaseIndex = INDEX_NONE;
	UPROPERTY() int32 LastNewPhaseIndex = INDEX_NONE;
	UPROPERTY() int32 TransitionFinishedCount = 0;

	/** Set this and the listener records that component's invulnerability at each phase change. */
	UPROPERTY() TObjectPtr<UHealthComponent> WatchedHealth = nullptr;

	UPROPERTY() bool bWatchedHealthInvulnerableAtPhaseChange = false;

	UFUNCTION()
	void HandlePhaseChanged(int32 OldPhaseIndex, int32 NewPhaseIndex, FBossPhase Phase);

	UFUNCTION()
	void HandleTransitionFinished(int32 PhaseIndex);

	// --- Weapon ---------------------------------------------------------------------------------
	UPROPERTY() int32 AmmoChangedCount = 0;
	UPROPERTY() int32 LastMagazine = 0;
	UPROPERTY() int32 LastReserve = 0;
	UPROPERTY() int32 EmptyClickCount = 0;

	UFUNCTION()
	void HandleAmmoChanged(int32 CurrentAmmo, int32 ReserveAmmo);

	UFUNCTION()
	void HandleEmptyClick();

	UPROPERTY() int32 WeaponHitCount = 0;
	UPROPERTY() TObjectPtr<AActor> LastWeaponHitActor = nullptr;
	UPROPERTY() float LastWeaponHitDamage = 0.f;

	UFUNCTION()
	void HandleWeaponHit(AActor* HitActor, float DamageDealt);

	// --- Inventory ------------------------------------------------------------------------------
	UPROPERTY() int32 InventoryChangedCount = 0;
	UPROPERTY() int32 ActiveSlotChangedCount = 0;
	UPROPERTY() int32 LastOldArrowSlot = 0;
	UPROPERTY() int32 LastNewArrowSlot = 0;

	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleActiveSlotChanged(int32 OldSlot, int32 NewSlot);

	// --- Challenge results ------------------------------------------------------------------------
	UPROPERTY() int32 ChallengeRetryCount = 0;
	UPROPERTY() int32 ChallengeLeaveCount = 0;

	UFUNCTION()
	void HandleChallengeRetry();

	UFUNCTION()
	void HandleChallengeLeave();

	// --- Bow ------------------------------------------------------------------------------------
	UPROPERTY() int32 DrawChangedCount = 0;
	UPROPERTY() float LastDrawFraction = -1.f;
	UPROPERTY() int32 ArrowFiredCount = 0;
	UPROPERTY() int32 ArrowHitCount = 0;
	UPROPERTY() float LastArrowHitDamage = 0.f;
	UPROPERTY() bool bLastArrowHitHeadshot = false;

	UFUNCTION()
	void HandleDrawChanged(float Fraction);

	UFUNCTION()
	void HandleArrowFired(UArrowDefinition* Arrow);

	UFUNCTION()
	void HandleArrowHit(AActor* HitActor, float Damage, bool bHeadshot);

	// --- Stagger --------------------------------------------------------------------------------
	UPROPERTY() int32 StaggeredCount = 0;

	UFUNCTION()
	void HandleStaggered(UHealthComponent* HealthComponent, AActor* DamageInstigator);

	// --- Takedown -------------------------------------------------------------------------------
	UPROPERTY() int32 TakedownCount = 0;
	UPROPERTY() TObjectPtr<AActor> LastTakedownTarget = nullptr;

	UFUNCTION()
	void HandleTakedownPerformed(AActor* Target);

	// --- Thug ----------------------------------------------------------------------------------
	UPROPERTY() int32 AlertStateChangedCount = 0;
	UPROPERTY() EThugAlertState LastOldAlertState = EThugAlertState::Calm;
	UPROPERTY() EThugAlertState LastNewAlertState = EThugAlertState::Calm;

	UFUNCTION()
	void HandleAlertStateChanged(EThugAlertState OldState, EThugAlertState NewState);

	// --- Mission --------------------------------------------------------------------------------
	/** Set by the test so HandleMissionStarted can assert the objectives already exist. */
	UPROPERTY() TObjectPtr<UMissionTracker> WatchedTracker = nullptr;

	UPROPERTY() int32 MissionStartedCount = 0;
	UPROPERTY() TObjectPtr<UMissionDefinition> LastStartedMission = nullptr;

	/** True when GetCurrentObjective() already returned an objective inside OnMissionStarted. */
	UPROPERTY() bool bCurrentObjectiveSetAtMissionStart = false;

	UPROPERTY() int32 ObjectiveUpdatedCount = 0;
	UPROPERTY() int32 LastObjectiveIndex = INDEX_NONE;
	UPROPERTY() TObjectPtr<UMissionObjective> LastObjective = nullptr;
	UPROPERTY() int32 MissionCompleteCount = 0;

	/**
	 * Set this and OnMissionComplete clears it, which is what AHawkeyeGameMode does for real.
	 * It lets the "nothing carries between missions" rule be tested without a game mode.
	 */
	UPROPERTY() TObjectPtr<UInventoryComponent> InventoryToClearOnMissionComplete = nullptr;

	UPROPERTY() int32 FlashbackRequestedCount = 0;

	/** Set when OnFlashbackRequested arrives while MissionCompleteCount is already 1. */
	UPROPERTY() bool bFlashbackFollowedMissionComplete = false;

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	UFUNCTION()
	void HandleMissionComplete(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleFlashbackRequested(UFlashbackDefinition* Flashback);

	// --- Settings -------------------------------------------------------------------------------
	UPROPERTY() int32 SettingsChangedCount = 0;
	UPROPERTY() float LastLookSensitivity = 0.f;

	UFUNCTION()
	void HandleSettingsChanged(FHawkeyeSettings Settings);

	// --- Flashback widget -----------------------------------------------------------------------
	UPROPERTY() int32 FlashbackFinishedCount = 0;

	UFUNCTION()
	void HandleFlashbackFinished(UFlashbackDefinition* Flashback);

	// --- Grapple --------------------------------------------------------------------------------
	UPROPERTY() int32 GrappleLandedCount = 0;
	UPROPERTY() int32 GrappleCancelledCount = 0;
	UPROPERTY() TObjectPtr<AGrappleAnchor> LastGrappleAnchor = nullptr;

	UFUNCTION()
	void HandleGrappleLanded(AGrappleAnchor* Anchor);

	UFUNCTION()
	void HandleGrappleCancelled(AGrappleAnchor* Anchor);

	// --- Melee ----------------------------------------------------------------------------------
	UPROPERTY() int32 MeleeWindupCount = 0;
	UPROPERTY() FName LastMeleeWindupName;
	UPROPERTY() int32 MeleeLandedCount = 0;
	UPROPERTY() TObjectPtr<AActor> LastMeleeHitActor = nullptr;
	UPROPERTY() float LastMeleeDamage = 0.f;

	/** Set by the test: the health it reads when the wind-up fires, to prove no damage came first. */
	UPROPERTY() TObjectPtr<UHealthComponent> WatchedMeleeVictim = nullptr;
	UPROPERTY() float VictimHealthAtWindup = -1.f;

	UFUNCTION()
	void HandleMeleeWindup(FName AttackName, float WindupSeconds);

	UFUNCTION()
	void HandleMeleeLanded(AActor* HitActor, float DamageDealt, FName AttackName);
};

/**
 * Kate with her protected aim state opened up. Sprinting and the FOV blend are driven by the
 * input handlers and Tick in the real game, neither of which a headless test can run.
 */
UCLASS()
class HAWKEYE_API AHawkeyeAimTestCharacter : public AHawkeyeCharacter
{
	GENERATED_BODY()

public:
	/** Stands in for Input_SprintStarted / Input_SprintCompleted. */
	void TestSetSprinting(bool bInSprinting);

	/** Stands in for one Tick of the camera blend (arm length, shoulder offset, FOV). */
	void TestTickAim(float DeltaSeconds) { UpdateCamera(DeltaSeconds); }

	/** Stands in for Input_Move having reported a stick of this size, held this long. */
	void TestSetMoveInput(float Magnitude, float HeldSeconds);

	/** Stands in for one Tick of the sprint toggle, the stick pushed or centred this frame. */
	void TestTickSprintToggle(float DeltaSeconds, bool bStickPushed)
	{
		bStickPushedThisFrame = bStickPushed;
		UpdateSprintToggle(DeltaSeconds);
	}

	/** Stands in for a settings change of the sprint button alone. */
	void TestSetSprintMode(EHawkeyeSprintMode Mode) { SprintMode = Mode; }

	/** Stands in for one Tick of the slide. */
	void TestTickSlide(float DeltaSeconds) { UpdateSlide(DeltaSeconds); }

	/** Stands in for Landed() after a fall of FallHeight. */
	void TestApplyLanding(float FallHeight) { ApplyLanding(FallHeight); }

	/** Stands in for one tick of the movement component (a zip's PhysZip included), with no controller. */
	void TestTickMovement(float DeltaSeconds);

	/** Stands in for the footstep tracker calling a step. */
	void TestPlayFootstep() { PlayFootstep(); }

	/** Stands in for one Tick of the hit reactions (stagger, shake, screen pulse). */
	void TestTickHitReactions(float DeltaSeconds) { UpdateHitReactions(DeltaSeconds); }

	/** Stands in for one Tick of the landing roll, stumble or dip. */
	void TestTickLanding(float DeltaSeconds) { UpdateLanding(DeltaSeconds); UpdateMaxWalkSpeed(); }

	/** Stands in for Input_Move having pointed the stick this way (world, flattened). */
	void TestSetMoveDirection(const FVector& Direction) { LastMoveWorldDirection = Direction.GetSafeNormal2D(); }

	float TestRollSeconds() const { return RollSeconds; }
	float TestStumbleSeconds() const { return StumbleSeconds; }

	/** Stands in for one Tick of the dodge (its invulnerability and cooldown). */
	void TestTickDodge(float DeltaSeconds) { UpdateDodge(DeltaSeconds); }

	/** Stands in for Input_Move having set the gait: a sprint refuses the dodge. */
	void TestSetSprintFlag(bool bSprint) { bIsSprinting = bSprint; }

	float TestWalkSpeed() const { return WalkSpeed; }
	float TestRunSpeed() const { return RunSpeed; }
	float TestSprintSpeed() const { return SprintSpeed; }
	float TestCrouchSpeed() const { return CrouchSpeed; }
	float TestSlideSeconds() const { return SlideSeconds; }
	float TestJumpHeight() const { return JumpHeight; }
	float TestAimFOV() const { return AimCamera.FieldOfView; }
	float TestHipFOV() const { return HipCamera.FieldOfView; }
	float TestAimBlendSeconds() const { return AimBlendSeconds; }

	float MaxWalkSpeed() const;

	/** Stands in for UHawkeyeSettingsSubsystem::OnSettingsChanged arriving. */
	void TestApplySettings(const FHawkeyeSettings& Settings) { HandleSettingsChanged(Settings); }

	float TestLookSensitivity() const { return LookSensitivity; }
	float TestAimLookMultiplier() const { return AimLookMultiplier; }
	bool TestHasSettingsSensitivity() const { return bHasSettingsLookSensitivity; }

	float TestStickSensitivity() const { return StickSensitivity; }
	bool TestHasSettingsStickSensitivity() const { return bHasSettingsStickSensitivity; }
	float TestStickYawDegreesPerSecond() const { return StickYawDegreesPerSecond; }
	float TestStickPitchDegreesPerSecond() const { return StickPitchDegreesPerSecond; }
	float TestAimStickRateMultiplier() const { return AimStickRateMultiplier; }
	bool TestInvertMouseY() const { return bInvertMouseY; }
	bool TestInvertStickY() const { return bInvertStickY; }

	/**
	 * Stands in for Input_Takedown having (or not having) executed a takedown this press,
	 * without a real physical sweep - the sweep and the angle/tag/veto rules are covered by
	 * Hawkeye.Takedown.*. This is only for the takedown-before-interact ordering.
	 */
	void TestSetTookDownThisPress(bool bValue) { bTookDownThisPress = bValue; }
	bool TestTookDownThisPress() const { return bTookDownThisPress; }

	/** Stands in for the gamepad's shared Y button firing Input_Interact after Input_Takedown. */
	void TestFireInputInteract() { Input_Interact(FInputActionValue()); }
};

/**
 * AHawkeyePlayerController with the flashback flag opened up, so a test can assert that Escape
 * is ignored while the slideshow owns the pause without building a UFlashbackWidget.
 */
UCLASS()
class HAWKEYE_API AHawkeyePauseTestController : public AHawkeyePlayerController
{
	GENERATED_BODY()

public:
	void TestSetFlashbackActive(bool bActive) { bFlashbackActive = bActive; }

	/** Stands in for the Escape key so a test can check what one press does without EnhancedInput. */
	void TestInputPause() { Input_Pause(FInputActionValue()); }
};

/** Minimal ITakedownable actor for takedown tests. */
UCLASS()
class HAWKEYE_API AHawkeyeTestTakedownTarget : public AActor, public ITakedownable
{
	GENERATED_BODY()

public:
	/** Mirrors a thug's AI state: thugs return false from CanBeTakenDown while Alerted. */
	UPROPERTY()
	bool bAlerted = false;

	UPROPERTY()
	int32 TakedownReceivedCount = 0;

	virtual bool CanBeTakenDown_Implementation(AActor* Attacker) override { return !bAlerted; }
	virtual void OnTakedown_Implementation(AActor* Attacker) override { ++TakedownReceivedCount; }
};

/** A takedown target with a box that blocks every channel, so the takedown's sweep finds it. */
UCLASS()
class HAWKEYE_API AHawkeyeTestSolidTakedownTarget : public AHawkeyeTestTakedownTarget
{
	GENERATED_BODY()

public:
	AHawkeyeTestSolidTakedownTarget();

	UPROPERTY()
	TObjectPtr<UBoxComponent> Box;
};

/**
 * A box that blocks every channel, for line-of-sight and sweep tests. Tests never load Content,
 * and a box component needs no mesh. Size it with SetExtent.
 */
UCLASS()
class HAWKEYE_API AHawkeyeTestBlocker : public AActor
{
	GENERATED_BODY()

public:
	AHawkeyeTestBlocker();

	void SetExtent(const FVector& HalfExtent);

	UPROPERTY()
	TObjectPtr<UBoxComponent> Box;
};

/** Fires a synthetic key event without a live Slate application, for widgets whose NativeOnKeyDown/Up is the whole fix under test. */
inline FKeyEvent HawkeyeTestKeyEvent(const FKey& Key, bool bIsRepeat = false)
{
	return FKeyEvent(Key, FModifierKeysState(), 0u, bIsRepeat, 0u, 0u);
}

/** Exposes UHawkeyeInventoryWidget's protected key handling: Escape/Tab/pad close it reach it Slate normally, not through the controller. */
UCLASS()
class HAWKEYE_API UHawkeyeInventoryWidgetTestHelper : public UHawkeyeInventoryWidget
{
	GENERATED_BODY()

public:
	FReply TestKeyDown(const FKey& Key, bool bIsRepeat = false)
	{
		return NativeOnKeyDown(FGeometry(), HawkeyeTestKeyEvent(Key, bIsRepeat));
	}

	FReply TestKeyUp(const FKey& Key)
	{
		return NativeOnKeyUp(FGeometry(), HawkeyeTestKeyEvent(Key));
	}

	void TestTick(float DeltaTime) { NativeTick(FGeometry(), DeltaTime); }

	bool TestIsWheelOpenedFromHold() const { return bWheelOpenedFromHold; }

	/**
	 * NativeOnKeyDown/Tick/Up read NowSeconds off GetWorld()->GetRealTimeSeconds(), which only
	 * advances if something ticks the world - a plain FHawkeyeTestWorld never does. These three
	 * drive the same tap/hold state machine with an injected clock instead, the way the generic
	 * FHawkeyeTapHold tests (Hawkeye.Stability.PhoneTapAndHold) already do.
	 */
	void TestPressTab(float HoldSeconds, double NowSeconds)
	{
		TabHold.HoldSeconds = HoldSeconds;
		TabHold.Press(NowSeconds);
	}

	void TestTickTab(double NowSeconds)
	{
		if (TabHold.Tick(NowSeconds) == EHawkeyeTapHold::Hold)
		{
			EnterWheelFromHold();
		}
	}

	void TestReleaseTab(double NowSeconds)
	{
		const EHawkeyeTapHold Result = TabHold.Release(NowSeconds);
		if (bWheelOpenedFromHold)
		{
			FinishWheelFromHold();
		}
		else if (Result == EHawkeyeTapHold::Tap || Result == EHawkeyeTapHold::Hold)
		{
			RequestClose();
		}
	}
};

/** Exposes UChallengeResultsWidget's protected key handling: Enter/Esc should do what Retry/Leave do. */
UCLASS()
class HAWKEYE_API UChallengeResultsWidgetTestHelper : public UChallengeResultsWidget
{
	GENERATED_BODY()

public:
	FReply TestKeyDown(const FKey& Key) { return NativeOnKeyDown(FGeometry(), HawkeyeTestKeyEvent(Key)); }
};
