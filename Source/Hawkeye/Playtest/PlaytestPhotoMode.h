#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "PlaytestPhotoMode.generated.h"

class ACameraActor;
class AActor;
class APlayerController;
class UPhotoModeWidget;
class UUserWidget;
struct FInputKeyEventArgs;

/**
 * Photo mode (claude-docs/gameplay-semantics.md, Controller). The world pauses, the HUD goes, and a free
 * camera starts where Kate's camera is. While it is on, every key and axis goes to it and nothing reaches
 * the game's input, so leaving it (Esc or B) hands back exactly the frame it froze: no state change, no
 * save. A shot (F12 or A) is photo_<n>.png in the playtest session folder, at the window's resolution,
 * taken with no UI.
 *
 * The world is paused, so the engine does not update the camera manager: Tick moves the camera actor and
 * updates the manager itself, from real time. The controller owns one and decides when it may start.
 */
UCLASS()
class HAWKEYE_API UPlaytestPhotoMode : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static constexpr float MinFov = 40.f;
	static constexpr float MaxFov = 110.f;
	/** How far the camera may wander from where it started, cm. */
	static constexpr float MaxRadius = 5000.f;
	static constexpr float MoveSpeed = 500.f;
	static constexpr float FastMultiplier = 4.f;
	/** Degrees per mouse count, and per second at full stick. */
	static constexpr float MouseDegreesPerCount = 0.1f;
	static constexpr float StickYawDegreesPerSecond = 120.f;
	static constexpr float StickPitchDegreesPerSecond = 80.f;
	/** Degrees of FOV per wheel notch, and per second with a bumper held. */
	static constexpr float FovPerNotch = 5.f;
	static constexpr float FovPerSecond = 40.f;

	/** Starts on PC's current view. False if it could not (no camera manager, not a local player). */
	bool Enter(APlayerController* PC);

	/** Back to the game where it was. */
	void Exit();

	bool IsActive() const { return bActive; }

	/** Every input while active. Returns true: photo mode swallows it all. */
	bool HandleKey(const FInputKeyEventArgs& Params);

	float GetFov() const { return Fov; }
	FVector GetCameraLocation() const { return Location; }
	FRotator GetCameraRotation() const { return Rotation; }
	FVector GetStartLocation() const { return StartLocation; }
	UPhotoModeWidget* GetOverlay() const { return Overlay; }

	/** Shots taken this time in photo mode. */
	int32 GetShotsTaken() const { return ShotsTaken; }

	/** FOV plus Delta, clamped to MinFov..MaxFov. Pure. */
	static float StepFov(float InFov, float Delta);

	/** View plus a yaw and pitch delta, pitch held inside +-89 and no roll. Pure. */
	static FRotator StepRotation(const FRotator& View, float YawDelta, float PitchDelta);

	/**
	 * One step of the free camera. Axes: X forward along the view, Y right, Z world up, each -1..1.
	 * The result stays within Radius of Anchor. Pure.
	 */
	static FVector StepLocation(const FVector& From, const FRotator& View, const FVector& Axes, float Speed, float DeltaSeconds,
		const FVector& Anchor, float Radius);

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

private:
	void Step(float DeltaSeconds);
	void ApplyView();
	void TakeShot();
	void ResetToStart();
	float Axis(const FKey& Key) const;
	bool IsHeld(const FKey& Key) const { return Held.Contains(Key); }

	bool bActive = false;
	TWeakObjectPtr<APlayerController> Owner;
	TWeakObjectPtr<AActor> PreviousViewTarget;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPhotoModeWidget> Overlay = nullptr;

	/** The HUD and how it was shown before, put back on Exit. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Hud = nullptr;
	uint8 HudVisibility = 0;

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float Fov = 90.f;
	FVector StartLocation = FVector::ZeroVector;
	FRotator StartRotation = FRotator::ZeroRotator;
	float StartFov = 90.f;

	TSet<FKey> Held;
	TMap<FKey, float> Axes;
	FVector2D MouseDelta = FVector2D::ZeroVector;
	float WheelNotches = 0.f;
	bool bFastToggle = false;
	bool bGamepad = false;
	int32 ShotsTaken = 0;
	double LastTickSeconds = 0.0;
	/** "photo 2 saved" on the card until StatusUntilSeconds (real time). */
	FString Status;
	double StatusUntilSeconds = 0.0;
};
