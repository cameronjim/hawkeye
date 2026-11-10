#include "Playtest/PlaytestPhotoMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "InputKeyEventArgs.h"
#include "Playtest/PhotoModeWidget.h"
#include "Playtest/PlaytestSubsystem.h"
#include "UI/HawkeyeHudWidget.h"

namespace
{
	/** A stick with a radial dead zone of 0.2, rescaled so just past it is just above 0. */
	FVector2D DeadZoned(float X, float Y)
	{
		const FVector2D Raw(X, Y);
		const double Size = Raw.Size();
		if (Size < 0.2)
		{
			return FVector2D::ZeroVector;
		}
		return Raw / Size * FMath::Min(1.0, (Size - 0.2) / 0.8);
	}
}

float UPlaytestPhotoMode::StepFov(float InFov, float Delta)
{
	return FMath::Clamp(InFov + Delta, MinFov, MaxFov);
}

FRotator UPlaytestPhotoMode::StepRotation(const FRotator& View, float YawDelta, float PitchDelta)
{
	return FRotator(FMath::Clamp(FRotator::NormalizeAxis(View.Pitch + PitchDelta), -89.f, 89.f),
		FRotator::NormalizeAxis(View.Yaw + YawDelta), 0.f);
}

FVector UPlaytestPhotoMode::StepLocation(const FVector& From, const FRotator& View, const FVector& InAxes, float Speed,
	float DeltaSeconds, const FVector& Anchor, float Radius)
{
	const FVector Forward = View.Vector();
	const FVector Right = FRotator(0.f, View.Yaw, 0.f).RotateVector(FVector::RightVector);
	const FVector Axes = InAxes.BoundToCube(1.f);
	const FVector To = From + (Forward * Axes.X + Right * Axes.Y + FVector::UpVector * Axes.Z) * Speed * DeltaSeconds;
	const FVector Offset = To - Anchor;
	return Offset.SizeSquared() > FMath::Square(Radius) ? Anchor + Offset.GetSafeNormal() * Radius : To;
}

bool UPlaytestPhotoMode::Enter(APlayerController* PC)
{
	if (bActive || !PC || !PC->IsLocalController() || !PC->PlayerCameraManager || !PC->GetWorld())
	{
		return false;
	}
	UWorld* World = PC->GetWorld();
	const FMinimalViewInfo View = PC->PlayerCameraManager->GetCameraCacheView();
	Location = StartLocation = View.Location;
	Rotation = StartRotation = FRotator(View.Rotation.Pitch, View.Rotation.Yaw, 0.f);
	Fov = StartFov = StepFov(View.FOV, 0.f);

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = World->SpawnActor<ACameraActor>(Location, Rotation, Params);
	if (!Camera)
	{
		return false;
	}
	UCameraComponent* Lens = Camera->GetCameraComponent();
	Lens->bConstrainAspectRatio = false;
	Lens->SetFieldOfView(Fov);
	PreviousViewTarget = PC->GetViewTarget();
	// The same look as the game camera: its post process comes along.
	if (const UCameraComponent* From = PreviousViewTarget.IsValid() ? PreviousViewTarget->FindComponentByClass<UCameraComponent>() : nullptr)
	{
		Lens->PostProcessSettings = From->PostProcessSettings;
		Lens->PostProcessBlendWeight = From->PostProcessBlendWeight;
	}

	Owner = PC;
	PC->SetPause(true);
	PC->SetViewTarget(Camera);
	PC->PlayerCameraManager->UpdateCamera(0.f);
	PC->FlushPressedKeys();
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;

	const AHawkeyePlayerController* HawkeyePC = Cast<AHawkeyePlayerController>(PC);
	bGamepad = HawkeyePC && HawkeyePC->IsUsingGamepad();
	Hud = HawkeyePC ? HawkeyePC->GetHawkeyeHud() : nullptr;
	if (Hud)
	{
		HudVisibility = uint8(Hud->GetVisibility());
		Hud->SetVisibility(ESlateVisibility::Collapsed);
	}
	Overlay = CreateWidget<UPhotoModeWidget>(PC, UPhotoModeWidget::StaticClass());
	if (Overlay)
	{
		Overlay->AddToViewport(60);
		Overlay->SetState(Fov, bGamepad);
	}

	Held.Reset();
	Axes.Reset();
	MouseDelta = FVector2D::ZeroVector;
	WheelNotches = 0.f;
	bFastToggle = false;
	ShotsTaken = 0;
	Status.Reset();
	LastTickSeconds = FPlatformTime::Seconds();
	bActive = true;
	UE_LOG(LogHawkeye, Log, TEXT("Photo mode: on at (%.0f,%.0f,%.0f) yaw %.0f, FOV %.0f."), Location.X, Location.Y, Location.Z,
		Rotation.Yaw, Fov);
	return true;
}

void UPlaytestPhotoMode::Exit()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	if (Overlay)
	{
		Overlay->RemoveFromParent();
		Overlay = nullptr;
	}
	if (Hud)
	{
		Hud->SetVisibility(ESlateVisibility(HudVisibility));
		Hud = nullptr;
	}
	APlayerController* PC = Owner.Get();
	if (PC)
	{
		AActor* Back = PreviousViewTarget.IsValid() ? PreviousViewTarget.Get() : PC->GetPawn();
		PC->SetViewTarget(Back);
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->UpdateCamera(0.f);
		}
	}
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	if (PC)
	{
		PC->FlushPressedKeys();
		PC->SetPause(false);
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("Photo mode: off (%d photos)."), ShotsTaken);
}

bool UPlaytestPhotoMode::HandleKey(const FInputKeyEventArgs& Params)
{
	if (!bActive)
	{
		return false;
	}
	const FKey& Key = Params.Key;
	if (Key.IsAnalog())
	{
		if (Key == EKeys::MouseX)
		{
			MouseDelta.X += Params.AmountDepressed;
		}
		else if (Key == EKeys::MouseY)
		{
			MouseDelta.Y += Params.AmountDepressed;
		}
		else if (Key == EKeys::MouseWheelAxis)
		{
			WheelNotches += Params.AmountDepressed;
		}
		else
		{
			Axes.Add(Key, Params.AmountDepressed);
			bGamepad = bGamepad || (Key.IsGamepadKey() && FMath::Abs(Params.AmountDepressed) > 0.5f);
		}
		return true;
	}
	if (Params.Event == IE_Pressed)
	{
		bGamepad = Key.IsGamepadKey();
		Held.Add(Key);
		if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::F11)
		{
			Exit();
		}
		else if (Key == EKeys::F12 || Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			TakeShot();
		}
		else if (Key == EKeys::R || Key == EKeys::Gamepad_FaceButton_Top)
		{
			ResetToStart();
		}
		else if (Key == EKeys::Gamepad_LeftThumbstick)
		{
			bFastToggle = !bFastToggle;
		}
	}
	else if (Params.Event == IE_Released)
	{
		Held.Remove(Key);
	}
	return true;
}

float UPlaytestPhotoMode::Axis(const FKey& Key) const
{
	const float* Value = Axes.Find(Key);
	return Value ? *Value : 0.f;
}

void UPlaytestPhotoMode::ResetToStart()
{
	Location = StartLocation;
	Rotation = StartRotation;
	Fov = StartFov;
	UE_LOG(LogHawkeye, Log, TEXT("Photo mode: back to Kate's camera."));
}

void UPlaytestPhotoMode::TakeShot()
{
	UPlaytestSubsystem* Playtest = UPlaytestSubsystem::Get(Owner.Get());
	if (!Playtest)
	{
		return;
	}
	++ShotsTaken;
	TWeakObjectPtr<UPlaytestPhotoMode> WeakThis(this);
	Playtest->TakePhoto([WeakThis](int32 Index, bool bOk)
	{
		if (UPlaytestPhotoMode* Self = WeakThis.Get())
		{
			Self->Status = bOk ? FString::Printf(TEXT("photo %d saved"), Index) : FString::Printf(TEXT("photo %d failed"), Index);
			Self->StatusUntilSeconds = FPlatformTime::Seconds() + 2.0;
		}
	});
}

void UPlaytestPhotoMode::Step(float DeltaSeconds)
{
	const FVector2D LookStick = DeadZoned(Axis(EKeys::Gamepad_RightX), Axis(EKeys::Gamepad_RightY));
	// Eased like the game's look stick, for fine framing.
	const auto Ease = [](double V) { return FMath::Sign(V) * FMath::Pow(FMath::Abs(V), 1.5); };
	const float Yaw = float(MouseDelta.X * MouseDegreesPerCount + Ease(LookStick.X) * StickYawDegreesPerSecond * DeltaSeconds);
	const float Pitch = float(MouseDelta.Y * MouseDegreesPerCount + Ease(LookStick.Y) * StickPitchDegreesPerSecond * DeltaSeconds);
	MouseDelta = FVector2D::ZeroVector;
	Rotation = StepRotation(Rotation, Yaw, Pitch);

	const FVector2D MoveStick = DeadZoned(Axis(EKeys::Gamepad_LeftX), Axis(EKeys::Gamepad_LeftY));
	const auto Key01 = [this](const FKey& Key) { return IsHeld(Key) ? 1.f : 0.f; };
	const FVector Move(
		Key01(EKeys::W) - Key01(EKeys::S) + MoveStick.Y,
		Key01(EKeys::D) - Key01(EKeys::A) + MoveStick.X,
		Key01(EKeys::E) - Key01(EKeys::Q) + Axis(EKeys::Gamepad_RightTriggerAxis) - Axis(EKeys::Gamepad_LeftTriggerAxis));
	const bool bFast = bFastToggle || IsHeld(EKeys::LeftShift) || IsHeld(EKeys::RightShift);
	Location = StepLocation(Location, Rotation, Move, MoveSpeed * (bFast ? FastMultiplier : 1.f), DeltaSeconds, StartLocation,
		MaxRadius);

	// Wheel up and RB zoom in (a narrower view); wheel down and LB widen it.
	const float Bumpers = Key01(EKeys::Gamepad_LeftShoulder) - Key01(EKeys::Gamepad_RightShoulder);
	Fov = StepFov(Fov, -WheelNotches * FovPerNotch + Bumpers * FovPerSecond * DeltaSeconds);
	WheelNotches = 0.f;
}

void UPlaytestPhotoMode::ApplyView()
{
	APlayerController* PC = Owner.Get();
	if (!Camera || !PC || !PC->PlayerCameraManager)
	{
		return;
	}
	Camera->SetActorLocationAndRotation(Location, Rotation);
	Camera->GetCameraComponent()->SetFieldOfView(Fov);
	// The world is paused, so the engine leaves the camera manager alone: update it here.
	PC->PlayerCameraManager->UpdateCamera(0.f);
	if (Overlay)
	{
		const bool bShowStatus = FPlatformTime::Seconds() < StatusUntilSeconds;
		Overlay->SetState(Fov, bGamepad, bShowStatus ? Status : FString());
	}
}

void UPlaytestPhotoMode::Tick(float /*DeltaTime*/)
{
	const double Now = FPlatformTime::Seconds();
	const float Dt = float(FMath::Clamp(Now - LastTickSeconds, 0.0, 0.1));
	LastTickSeconds = Now;
	if (!bActive)
	{
		return;
	}
	if (!Owner.IsValid() || !Camera)
	{
		Exit();
		return;
	}
	Step(Dt);
	ApplyView();
}

TStatId UPlaytestPhotoMode::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPlaytestPhotoMode, STATGROUP_Tickables);
}

ETickableTickType UPlaytestPhotoMode::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

UWorld* UPlaytestPhotoMode::GetTickableGameObjectWorld() const
{
	return Owner.IsValid() ? Owner->GetWorld() : nullptr;
}
