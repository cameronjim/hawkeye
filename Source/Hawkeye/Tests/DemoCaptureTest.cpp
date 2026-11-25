#include "Async/Async.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Hawkeye.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Math/RotationMatrix.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Player/ParkourComponent.h"
#include "Tests/AutomationCommon.h"
#include "Tests/LapBot.h"
#include "Tests/PartnerScreenshots.h"
#include "UnrealClient.h"
#include "World/CityLedgeSpawner.h"
#include "World/FireEscapeLanding.h"
#include "World/GrappleAnchor.h"
#include "World/LuckyCharacter.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The demo video's scripted shots (docs/demo/shot-list.md; cut by Tools/Demo/cut.py). Each test plays one shot with
 * the lap bots and writes it as a numbered frame sequence, 1920x1080 at a fixed 60 fps, to
 * Saved/Demo/<shot>/frame_00001.png onward, plus Saved/Demo/<shot>/shot.json (frame count, size, dropped frames and
 * the frame number of every zip, landing, mantle and ring, for picking cut points). Run one shot per process with
 * Tools/demo-capture.ps1; the tests are opt-in (they skip without -HawkeyeDemo) because a shot is minutes of wall time:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject /Game/Maps/L_District_EastVillage -game -windowed -ResX=1920 -ResY=1080
 *       -benchmark -fps=60 -HawkeyeDemo -unattended -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Demo.GrappleRun; Quit"
 *
 * Hawkeye.Demo.GrappleRun: the hero block's traversal_1 route from its pedestal, up to the first fire-escape descent
 * (runs, mantles, vaults and grapple zips chained across roofs), the player's own camera behind Kate.
 * Hawkeye.Demo.Descent: the same route's fire-escape leg, hang, drop and landing, Kate put on the ring before it.
 * Hawkeye.Demo.Lucky: Kate jogs down the street from the PlayerStart with Lucky trotting after her, then stops and he sits.
 * Hawkeye.Demo.Establish: a night pan round the block from a rooftop height, a scripted camera, Kate out of frame.
 *
 * The frames: the world runs a fixed 1/60 s step (FApp fixed delta time, restored after) and one screenshot is taken
 * every frame through the viewport's OnScreenshotCaptured, its PNG written on a worker thread, so the world never
 * waits for a frame to be saved. The game thread sleeps when the writers are more than MaxInFlight frames behind.
 * Every shot is HUD-free (a capture with bShowUI false). A frame the engine did not draw is counted as dropped and fails
 * the test: a hole in the sequence would be a hitch in the video. Knobs on the command line: -DemoFrames=<n> (the cap),
 * -DemoPanHeight=<cm>, -DemoPanRadius=<cm>, -DemoPanRate=<deg/s>.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoGrappleRun, "Hawkeye.Demo.GrappleRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoDescent, "Hawkeye.Demo.Descent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoLucky, "Hawkeye.Demo.Lucky",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDemoEstablish, "Hawkeye.Demo.Establish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HawkeyeDemo
{
	static const TCHAR* MovePath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* SprintPath = TEXT("/Game/Input/IA_Sprint.IA_Sprint");
	static const TCHAR* JumpPath = TEXT("/Game/Input/IA_Jump.IA_Jump");
	static const TCHAR* CrouchPath = TEXT("/Game/Input/IA_Crouch.IA_Crouch");
	static const TCHAR* GrapplePath = TEXT("/Game/Input/IA_Grapple.IA_Grapple");
	static const TCHAR* BowAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate");
	static const TCHAR* StandardAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard");
	static const TCHAR* GrappleAssetPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple");
	static const FName RouteId(TEXT("traversal_1"));

	static constexpr int32 Fps = 60;
	static constexpr float StepSeconds = 1.f / 60.f;
	static constexpr int32 WidthWanted = 1920;
	static constexpr int32 HeightWanted = 1080;
	/** Ring centres sit this far above the floor (create_challenges.py CHECKPOINT_UP). */
	static constexpr float RingUp = 110.f;
	/** Frames not captured before the first one, so the camera arm and the snow settle. */
	static constexpr int32 SettleFrames = 90;
	/** Frames kept after the route's last ring, for the landing to finish. */
	static constexpr int32 TailFrames = 90;
	/** Writers this far behind and the game thread waits. */
	static constexpr int32 MaxInFlight = 12;
	/** A fall at least this fast (cm/s, downward) is a heavy landing in the events. A guess; see shot-list.md. */
	static constexpr float HeavyFallSpeed = 800.f;
	/** The shot ends here whatever it is doing: the script's own timeout is 8 minutes. */
	static constexpr double WallBudgetSeconds = 420.0;

	enum class EKind : uint8
	{
		Route,
		Descent,
		Lucky,
		Establish
	};

	struct FShot
	{
		FString Name;
		EKind Kind = EKind::Route;
		bool bNight = true;
		int32 MaxFrames = 900;
	};

	static UWorld* FindWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	static FString ShotDir(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Demo") / Name);
	}

	static UEnhancedInputLocalPlayerSubsystem* InputOf(APlayerController* PC)
	{
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		return Player ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
	}

	static const UInputAction* Action(const TCHAR* Path)
	{
		return LoadObject<UInputAction>(nullptr, Path);
	}

	static void Tap(APlayerController* PC, const TCHAR* Path)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* A = Action(Path);
		if (Input && A)
		{
			Input->InjectInputForAction(A, FInputActionValue(true), {}, {});
		}
	}

	static void Hold(APlayerController* PC, const TCHAR* Path, bool bHold)
	{
		UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC);
		const UInputAction* A = Action(Path);
		if (!Input || !A)
		{
			return;
		}
		if (bHold)
		{
			Input->StartContinuousInputInjectionForAction(A, FInputActionValue(true), {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(A);
		}
	}

	static FVector2D MoveTowards(const FVector& WorldDirection, float CameraYaw)
	{
		const float Angle = FMath::DegreesToRadians(WorldDirection.Rotation().Yaw - CameraYaw);
		return FVector2D(FMath::Sin(Angle), FMath::Cos(Angle));
	}

	static FVector Feet(const ACharacter* Character)
	{
		return Character->GetActorLocation() - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	using HawkeyeLapBot::AimAt;

	static AChallengeStart* FindStart(UWorld* World)
	{
		for (TActorIterator<AChallengeStart> It(World); It; ++It)
		{
			if (It->Definition && It->Definition->Id == RouteId)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool IsSpawnDone(UWorld* World)
	{
		TActorIterator<ACityLedgeSpawner> It(World);
		return !It || It->IsSpawnComplete();
	}

	/** The anchor whose landing point is under a grapple ring (within 80 cm), or null. */
	static AGrappleAnchor* AnchorForRing(UWorld* World, const FVector& RingCentre)
	{
		const FVector RingFeet = RingCentre - FVector(0.f, 0.f, RingUp);
		AGrappleAnchor* Best = nullptr;
		float BestDistance = 80.f;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetLandingLocation(), RingFeet);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}

	/** The quiver the chapter grants, in case the district's mission did not. */
	static void EnsureQuiver(FAutomationTestBase* Test, UInventoryComponent* Inventory)
	{
		if (Inventory->HasBow() && Inventory->GetArrowCount(1) >= 20)
		{
			return;
		}
		Test->AddWarning(TEXT("Kate had no bow or too few arrows; granting DA_Bow_Kate, 30 standard and 6 grapple."));
		FHawkeyeQuiverSlot Standard;
		Standard.Arrow = LoadObject<UArrowDefinition>(nullptr, StandardAssetPath);
		Standard.Count = 30;
		FHawkeyeQuiverSlot Grapple;
		Grapple.Arrow = LoadObject<UArrowDefinition>(nullptr, GrappleAssetPath);
		Grapple.Count = 60;
		Inventory->ApplyStartingQuiver(LoadObject<UBowDefinition>(nullptr, BowAssetPath), { Standard, Grapple });
	}

	/** Kate healthy and up, on her feet at Location (feet) facing Yaw, the camera behind her. */
	static void PutKate(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& FeetAt, float Yaw)
	{
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->TeleportTo(FeetAt + FVector(0.f, 0.f, HalfHeight + 5.f), FRotator(0.f, Yaw, 0.f));
		PC->SetControlRotation(FRotator(-10.f, Yaw, 0.f));
		PC->SetViewTarget(Kate);
		if (Kate->IsDowned())
		{
			Kate->ReviveFromDown(1.f);
		}
		if (!Kate->GetHealthComponent()->IsAlive())
		{
			Kate->GetHealthComponent()->Revive(Kate->GetHealthComponent()->GetMaxHealth());
		}
		Kate->GetHealthComponent()->Heal(1000.f);
		PC->SetPause(false);
	}

	/**
	 * One screenshot per frame, off the game thread. The viewport broadcasts OnScreenshotCaptured inside the draw of the
	 * frame that asked; the pixels are copied and the PNG is written on the thread pool. Pending holds the paths asked for
	 * and not yet drawn, in order.
	 */
	struct FFrameSink
	{
		FDelegateHandle Handle;
		TArray<FString> Pending;
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> InFlight = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> Failed = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
		FIntPoint Size = FIntPoint::ZeroValue;
		int32 Taken = 0;

		void Start()
		{
			Handle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FFrameSink::OnCaptured);
		}

		void Stop()
		{
			if (Handle.IsValid())
			{
				UGameViewportClient::OnScreenshotCaptured().Remove(Handle);
				Handle.Reset();
			}
		}

		/** False when the previous request has not been drawn yet (the caller skips the frame and counts it). */
		bool Request(const FString& Path)
		{
			if (FScreenshotRequest::IsScreenshotRequested())
			{
				return false;
			}
			Pending.Add(Path);
			FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI=*/false, /*bAddUniqueSuffix=*/false);
			return true;
		}

		void OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
		{
			if (Pending.IsEmpty())
			{
				return;
			}
			const FString Path = Pending[0];
			Pending.RemoveAt(0);
			Size = FIntPoint(Width, Height);
			++Taken;
			InFlight->Increment();
			TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> Busy = InFlight;
			TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> Bad = Failed;
			Async(EAsyncExecution::ThreadPool, [Width, Height, Pixels = Colors, Path, Busy, Bad]() mutable
			{
				// The read-back can leave alpha at 0, which some players show as see-through.
				for (FColor& Pixel : Pixels)
				{
					Pixel.A = 255;
				}
				const FImageView Image(Pixels.GetData(), Width, Height);
				if (!FImageUtils::SaveImageByExtension(*Path, Image))
				{
					Bad->Increment();
				}
				Busy->Decrement();
			});
		}

		int32 Outstanding() const { return InFlight->GetValue(); }
	};

	enum class EPhase : uint8
	{
		WaitWorld,
		Prepare,
		Settle,
		Capture,
		Drain,
		Done
	};
}

class FHawkeyeDemoRunner : public IAutomationLatentCommand
{
public:
	FHawkeyeDemoRunner(FAutomationTestBase* InTest, const HawkeyeDemo::FShot& InShot) : Test(InTest), Shot(InShot) {}

	virtual bool Update() override;

private:
	void Prepare(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate);
	void Note(const FString& What);
	void Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate);

	/** The per-frame bots. Each returns true when the shot's script is over. */
	bool StepRoute(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate);
	bool StepLucky(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate);
	bool StepEstablish(UWorld* World, APlayerController* PC);
	void StepChase(UWorld* World, AHawkeyeCharacter* Kate);
	void StepProfile(UWorld* World, AHawkeyeCharacter* Kate);
	void WatchEvents(AHawkeyeCharacter* Kate);

	// The route's legs (the traversal challenge lap's bot, without the challenge).
	bool Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive);
	void StopMoving(APlayerController* PC);
	void EaseCamera(APlayerController* PC, const FVector& Toward);
	void RunLeg(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring);
	void GrappleLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring);
	void DescentLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring);
	void BeginLeg(int32 NewLeg);

	FAutomationTestBase* Test;
	HawkeyeDemo::FShot Shot;
	HawkeyeDemo::EPhase Phase = HawkeyeDemo::EPhase::WaitWorld;
	HawkeyeDemo::FFrameSink Sink;

	double PhaseStartSeconds = -1.0;
	double WallStart = 0.0;
	int32 FramesWaited = 0;
	int32 FrameIndex = 0;
	int32 Dropped = 0;
	uint64 LastFrameCounter = 0;
	int32 TailLeft = -1;
	int32 DrainLeft = 4;
	bool bFailed = false;
	FString FailReason;
	TArray<TPair<int32, FString>> Events;

	// What to put back.
	bool bHadFixedStep = false;
	double PreviousFixedDelta = 0.0;
	bool bTimeOfDaySet = false;

	// The stage.
	FVector StartFeet = FVector::ZeroVector;
	FVector Along = FVector::ForwardVector;
	TWeakObjectPtr<ACameraActor> PanCamera;
	TWeakObjectPtr<ALuckyCharacter> Dog;
	TArray<TWeakObjectPtr<ALuckyCharacter>> Spawned;

	// Route.
	TWeakObjectPtr<AChallengeStart> Start;
	int32 Leg = INDEX_NONE;
	int32 EndLeg = 0;
	bool bMoveInjecting = false;
	bool bSprintHeld = false;
	float StuckFor = 0.f;
	bool bParkourSeen = false;
	TWeakObjectPtr<AGrappleAnchor> Anchor;
	int32 AimFrames = 0;
	int32 PressedFrame = -1000;
	bool bZipped = false;
	int32 DescentPhase = 0;
	FVector DescentStand = FVector::ZeroVector;
	FVector DescentOut = FVector::ForwardVector;
	int32 DescentFrame = 0;
	int32 Retries = 0;
	bool bPushing = false;
	int32 LegFrames = 0;

	// Events.
	bool bWasZipping = false;
	bool bWasBusy = false;
	bool bWasOnGround = true;
	float PeakFall = 0.f;

	// The demo's own chase camera (the player's boom folds in on a parapet; see StepChase).
	FVector ChaseHeading = FVector::ForwardVector;
	FVector ChaseOffset = FVector::ZeroVector;
	FVector ChaseAim = FVector::ZeroVector;
	bool bChaseStarted = false;
	float ProfileSide = 1.f;

	// Lucky.
	FVector RunDirection = FVector::ForwardVector;
	ELuckyState LastLucky = ELuckyState::Idle;
};

void FHawkeyeDemoRunner::Note(const FString& What)
{
	Events.Emplace(FrameIndex, What);
	UE_LOG(LogHawkeye, Log, TEXT("Demo %s frame %d: %s"), *Shot.Name, FrameIndex, *What);
}

/** The player's view eased to look along the way she goes, a little down, so a zip is seen from behind, not from the aim. */
void FHawkeyeDemoRunner::EaseCamera(APlayerController* PC, const FVector& Toward)
{
	if (Toward.Size2D() < 250.f)
	{
		return; // Arrived: the direction is noise, keep the view.
	}
	const FRotator Control = PC->GetControlRotation();
	const float Yaw = Toward.GetSafeNormal2D().Rotation().Yaw;
	const float MaxTurn = 150.f * HawkeyeDemo::StepSeconds;
	const float NewYaw = Control.Yaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(Control.Yaw, Yaw), -MaxTurn, MaxTurn);
	const float NewPitch = Control.Pitch + FMath::Clamp(FMath::FindDeltaAngleDegrees(Control.Pitch, -12.f), -MaxTurn, MaxTurn);
	PC->SetControlRotation(FRotator(NewPitch, NewYaw, 0.f));
}

void FHawkeyeDemoRunner::StopMoving(APlayerController* PC)
{
	using namespace HawkeyeDemo;
	if (bSprintHeld)
	{
		Hold(PC, SprintPath, false);
		bSprintHeld = false;
	}
	if (bMoveInjecting)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC))
		{
			Input->StopContinuousInputInjectionForAction(Action(MovePath));
		}
		bMoveInjecting = false;
	}
}

bool FHawkeyeDemoRunner::Steer(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Target, bool bSprint, float Arrive)
{
	using namespace HawkeyeDemo;
	const FVector To = (Target - Kate->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	if (To.Size() <= Arrive)
	{
		return true;
	}
	const FVector Direction = To.GetSafeNormal();
	const FRotator Control = PC->GetControlRotation();
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(Control.Yaw, Direction.Rotation().Yaw);
	const float MaxTurn = 540.f * StepSeconds;
	const float NewYaw = Control.Yaw + FMath::Clamp(DeltaYaw, -MaxTurn, MaxTurn);
	PC->SetControlRotation(FRotator(-10.f, NewYaw, 0.f));
	const FVector2D Value = MoveTowards(Direction, NewYaw);
	if (UEnhancedInputLocalPlayerSubsystem* Input = InputOf(PC))
	{
		const UInputAction* Move = Action(MovePath);
		if (!bMoveInjecting)
		{
			Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(Value), {}, {});
			bMoveInjecting = true;
		}
		else
		{
			Input->UpdateValueOfContinuousInputInjectionForAction(Move, FInputActionValue(Value));
		}
	}
	if (bSprint != bSprintHeld)
	{
		Hold(PC, SprintPath, bSprint);
		bSprintHeld = bSprint;
	}
	return false;
}

void FHawkeyeDemoRunner::BeginLeg(int32 NewLeg)
{
	Leg = NewLeg;
	LegFrames = 0;
	StuckFor = 0.f;
	Anchor = nullptr;
	PressedFrame = -1000;
	bZipped = false;
	DescentPhase = 0;
	Retries = 0;
	bPushing = false;
	AimFrames = 0;
	const UChallengeDefinition* Definition = Start.IsValid() ? Start->Definition.Get() : nullptr;
	if (Definition && Definition->CheckpointLegs.IsValidIndex(Leg))
	{
		Note(FString::Printf(TEXT("leg_%d_%s"), Leg + 1, *UEnum::GetValueAsString(Definition->CheckpointLegs[Leg])));
	}
}

void FHawkeyeDemoRunner::RunLeg(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring)
{
	using namespace HawkeyeDemo;
	const UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (Parkour->IsBusy())
	{
		bParkourSeen = true;
		return;
	}
	bParkourSeen = false;
	Steer(PC, Kate, Ring, true, 0.f);
	const bool bStopped = Kate->GetCharacterMovement()->IsMovingOnGround() && Kate->GetVelocity().Size2D() < 80.f;
	StuckFor = bStopped ? StuckFor + StepSeconds : 0.f;
	if (StuckFor > 0.6f)
	{
		// A parapet the sprint did not take on its own: the jump key's mantle or vault.
		StuckFor = 0.f;
		Tap(PC, JumpPath);
		Note(TEXT("jump_at_parapet"));
	}
}

void FHawkeyeDemoRunner::GrappleLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring)
{
	using namespace HawkeyeDemo;
	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	if (bZipped)
	{
		if (Anchor.IsValid())
		{
			EaseCamera(PC, Anchor->GetLandingLocation() - Kate->GetActorLocation());
		}
		if (Grapple->IsZipping() || Grapple->IsArrowInFlight() || !Kate->GetCharacterMovement()->IsMovingOnGround())
		{
			return;
		}
		// Landed: into the ring if the zip did not put her through it.
		Steer(PC, Kate, Ring, false, 0.f);
		return;
	}
	if (!Anchor.IsValid())
	{
		Anchor = AnchorForRing(World, Ring);
		if (!Anchor.IsValid())
		{
			bFailed = true;
			FailReason = FString::Printf(TEXT("no anchor under ring %d"), Leg + 1);
			return;
		}
		AimFrames = 0;
	}
	if (Grapple->IsZipping() || Grapple->IsArrowInFlight())
	{
		bZipped = true;
		StopMoving(PC);
		return;
	}
	if (LegFrames - PressedFrame < 24)
	{
		if (LegFrames - PressedFrame > 8)
		{
			EaseCamera(PC, Anchor->GetLandingLocation() - Kate->GetActorLocation());
		}
		return;
	}
	++AimFrames;
	StopMoving(PC);
	AimAt(PC, Kate, Anchor->GetMarkerLocation());
	Grapple->RefreshTarget();
	if (Grapple->GetTargetAnchor() == Anchor.Get() && Grapple->CanChain())
	{
		PressedFrame = LegFrames;
		Tap(PC, GrapplePath);
		return;
	}
	if (AimFrames > Fps)
	{
		// Not targeted from here: a step each second, toward it (back when it is steeply overhead), then to either side.
		const FVector ToMarker = Anchor->GetMarkerLocation() - Kate->GetActorLocation();
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(ToMarker.Z, FMath::Max(ToMarker.Size2D(), 1.f)));
		const FVector AlongTo = ToMarker.GetSafeNormal2D();
		const FVector Side(-AlongTo.Y, AlongTo.X, 0.f);
		const int32 Try = ((AimFrames - Fps) / Fps) % 3;
		const FVector Step = Try == 0 ? AlongTo * (Pitch > 50.f ? -200.f : 200.f) : Side * (Try == 1 ? 200.f : -200.f);
		Steer(PC, Kate, Kate->GetActorLocation() + Step, false, 0.f);
		if (AimFrames > 6 * Fps)
		{
			bFailed = true;
			FailReason = FString::Printf(TEXT("ring %d: %s never became the grapple target (%.0f cm away)"), Leg + 1,
				*Anchor->GetName(), FVector::Dist(Kate->GetActorLocation(), Anchor->GetMarkerLocation()));
		}
	}
}

void FHawkeyeDemoRunner::DescentLeg(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Ring)
{
	using namespace HawkeyeDemo;
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	const float FeetZ = Feet(Kate).Z;
	++DescentFrame;
	if (DescentPhase == 0)
	{
		// The fire escape whose foot is the street ring: its top landing.
		const AFireEscapeLanding* Top = nullptr;
		for (TActorIterator<AFireEscapeLanding> It(World); It; ++It)
		{
			const FVector Out = It->GetActorRightVector().GetSafeNormal2D();
			const FVector Foot = It->GetActorLocation() + Out * 140.f;
			if (FVector::Dist2D(Foot, Ring) < 400.f && (!Top || It->GetRecord().Floor > Top->GetRecord().Floor))
			{
				Top = *It;
			}
		}
		if (!Top)
		{
			bFailed = true;
			FailReason = TEXT("no fire escape under the descent ring");
			return;
		}
		DescentOut = Top->GetActorRightVector().GetSafeNormal2D();
		DescentStand = FVector(Top->GetActorLocation().X, Top->GetActorLocation().Y, FeetZ) - DescentOut * 110.f;
		DescentPhase = 1;
		DescentFrame = 0;
	}
	if (DescentPhase == 1)
	{
		if (Steer(PC, Kate, DescentStand, false, 40.f) || DescentFrame > 8 * Fps)
		{
			DescentPhase = 2;
			DescentFrame = 0;
		}
	}
	else if (DescentPhase == 2)
	{
		// Push toward the edge, then crouch: the drop to hang.
		Steer(PC, Kate, FVector(DescentStand.X, DescentStand.Y, FeetZ) + DescentOut * 1000.f, false, 1.f);
		if (Parkour->IsBusy())
		{
			StopMoving(PC);
			bPushing = false;
			DescentPhase = 3;
			DescentFrame = 0;
		}
		else if (DescentFrame > Fps / 4 && !bPushing)
		{
			bPushing = true;
			Tap(PC, CrouchPath);
		}
		else if (DescentFrame > Fps)
		{
			bPushing = false;
			DescentFrame = 0;
			if (++Retries >= 4)
			{
				bFailed = true;
				FailReason = FString::Printf(TEXT("crouch at the edge found no drop to hang at %s"), *Kate->GetActorLocation().ToCompactString());
			}
		}
	}
	else if (DescentPhase == 3)
	{
		if (Parkour->IsHanging())
		{
			if (DescentFrame > Fps * 3 / 10)
			{
				Tap(PC, CrouchPath);
				DescentPhase = 4;
				DescentFrame = 0;
			}
		}
		else if (!Parkour->IsBusy() && DescentFrame > 2 * Fps)
		{
			// Over the edge without a hang: treat it as a drop.
			DescentPhase = 4;
			DescentFrame = 0;
		}
	}
	else if (DescentPhase == 4 && DescentFrame > 9)
	{
		if (Parkour->IsBusy())
		{
			if (Parkour->IsHanging() || Parkour->GetActiveMove() == EHawkeyeParkourMove::LedgeGrab)
			{
				DescentPhase = 3;
				DescentFrame = 0;
			}
		}
		else if (Kate->GetCharacterMovement()->IsMovingOnGround() && DescentFrame > 12)
		{
			if (FeetZ - Ring.Z > 0.f)
			{
				// On a landing: crouch at its rail again.
				DescentStand = FVector(Kate->GetActorLocation().X, Kate->GetActorLocation().Y, FeetZ);
				Retries = 0;
				DescentPhase = 2;
				DescentFrame = 0;
			}
			else
			{
				DescentPhase = 5;
			}
		}
		else if (DescentFrame > 5 * Fps)
		{
			bFailed = true;
			FailReason = FString::Printf(TEXT("still in the air 5 s after a drop at %s"), *Kate->GetActorLocation().ToCompactString());
		}
	}
	else if (DescentPhase == 5)
	{
		// On the street: into the ring.
		Steer(PC, Kate, Ring, false, 0.f);
	}
}

bool FHawkeyeDemoRunner::StepRoute(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeDemo;
	const UChallengeDefinition* Definition = Start.IsValid() ? Start->Definition.Get() : nullptr;
	if (!Definition || Leg >= EndLeg)
	{
		StopMoving(PC);
		return true;
	}
	++LegFrames;
	const FVector Ring = Definition->Checkpoints[Leg].GetLocation();
	const EChallengeLeg How = Definition->CheckpointLegs.IsValidIndex(Leg) ? Definition->CheckpointLegs[Leg] : EChallengeLeg::Run;
	switch (How)
	{
	case EChallengeLeg::Grapple:
		GrappleLeg(World, PC, Kate, Ring);
		break;
	case EChallengeLeg::Descent:
		DescentLeg(World, PC, Kate, Ring);
		break;
	default:
		RunLeg(PC, Kate, Ring);
		break;
	}
	if (FVector::Dist(Kate->GetActorLocation(), Ring) <= Definition->CheckpointRadius)
	{
		Note(FString::Printf(TEXT("ring_%d"), Leg + 1));
		if (Leg + 1 >= EndLeg)
		{
			StopMoving(PC);
			Leg = EndLeg;
			return true;
		}
		BeginLeg(Leg + 1);
	}
	else if (LegFrames > 30 * Fps && !bFailed)
	{
		bFailed = true;
		FailReason = FString::Printf(TEXT("ring %d took over 30 s (Kate at %s, %s)"), Leg + 1, *Kate->GetActorLocation().ToCompactString(),
			*Kate->GetMovementDebugText());
	}
	return bFailed;
}

bool FHawkeyeDemoRunner::StepLucky(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeDemo;
	// Jog out along the street for 7 s, turning round when something stops her, then stand: he catches up and sits.
	constexpr int32 JogFrames = 5 * Fps;
	if (FrameIndex < JogFrames)
	{
		const bool bStopped = Kate->GetCharacterMovement()->IsMovingOnGround() && Kate->GetVelocity().Size2D() < 60.f;
		StuckFor = bStopped && FrameIndex > Fps ? StuckFor + StepSeconds : 0.f;
		if (StuckFor > 0.5f)
		{
			StuckFor = 0.f;
			RunDirection = -RunDirection;
			Note(TEXT("turned_round"));
		}
		Steer(PC, Kate, Kate->GetActorLocation() + RunDirection * 3000.f, false, 0.f);
	}
	else if (FrameIndex == JogFrames)
	{
		StopMoving(PC);
		Note(TEXT("kate_stops"));
	}
	if (const ALuckyCharacter* Lucky = Dog.Get())
	{
		const ELuckyState State = Lucky->GetLuckyState();
		if (FrameIndex == 0)
		{
			LastLucky = State;
		}
		if (State != LastLucky)
		{
			Note(FString::Printf(TEXT("lucky_%s"), *UEnum::GetValueAsString(State)));
			LastLucky = State;
		}
	}
	StepProfile(World, Kate);
	return false;
}

bool FHawkeyeDemoRunner::StepEstablish(UWorld* World, APlayerController* PC)
{
	using namespace HawkeyeDemo;
	ACameraActor* Camera = PanCamera.Get();
	if (!Camera)
	{
		bFailed = true;
		FailReason = TEXT("the pan camera is gone");
		return true;
	}
	float Height = 3000.f;
	float Radius = 4500.f;
	float Rate = 7.f;
	FParse::Value(FCommandLine::Get(), TEXT("DemoPanHeight="), Height);
	FParse::Value(FCommandLine::Get(), TEXT("DemoPanRadius="), Radius);
	FParse::Value(FCommandLine::Get(), TEXT("DemoPanRate="), Rate);
	const float Seconds = FrameIndex * StepSeconds;
	const FVector Centre = StartFeet + Along * 2500.f;
	const float Yaw = Along.Rotation().Yaw + 180.f - 35.f + Rate * Seconds;
	const FVector Out = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector At = Centre + Out * Radius + FVector(0.f, 0.f, Height);
	const FVector Look = Centre + FVector(0.f, 0.f, 900.f);
	Camera->SetActorLocationAndRotation(At, FRotationMatrix::MakeFromX(Look - At).Rotator());
	return false;
}

/**
 * A chase camera of the demo's own, so the body is always in the picture: behind her along the way she is going (her velocity,
 * or the zip's line), up and a little to her right, eased, and pulled in by a sphere sweep when a wall is in the way.
 * The game's camera boom folds in to nothing and hides her mesh when she lands on a parapet (a finding, see shot-list.md),
 * which would spoil the shot of the arrival; the camera's own rules are untouched.
 */
void FHawkeyeDemoRunner::StepChase(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeDemo;
	ACameraActor* Camera = PanCamera.Get();
	if (!Camera)
	{
		return;
	}
	constexpr float Arm = 340.f;
	constexpr float Up = 120.f;
	constexpr float Side = 55.f;
	const FVector Pivot = Kate->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	if (Shot.Kind == EKind::Descent)
	{
		// The fire escape is seen from the street: out in front of the wall and a little to one side, her height followed.
		const FVector Out = DescentOut.GetSafeNormal2D();
		const FVector Across = FVector::CrossProduct(FVector::UpVector, Out).GetSafeNormal();
		const FVector Wanted = Pivot + Out * 400.f + Across * 130.f + FVector(0.f, 0.f, 80.f);
		ChaseOffset = bChaseStarted ? FMath::Lerp(ChaseOffset, Wanted - Pivot, 1.f - FMath::Exp(-6.f * StepSeconds)) : Wanted - Pivot;
		bChaseStarted = true;
		const FVector Lens = Pivot + ChaseOffset;
		Camera->SetActorLocationAndRotation(Lens, FRotationMatrix::MakeFromX(Pivot + FVector(0.f, 0.f, 20.f) - Lens).Rotator());
		return;
	}
	FVector Velocity = Kate->GetVelocity();
	FVector Want = Velocity.GetSafeNormal2D();
	if (Kate->GetGrappleComponent()->IsZipping() && Velocity.Size() > 300.f)
	{
		Want = Velocity.GetSafeNormal2D();
	}
	if (!bChaseStarted)
	{
		ChaseHeading = Kate->GetActorForwardVector().GetSafeNormal2D();
		bChaseStarted = true;
	}
	if (Velocity.Size2D() > 250.f && !Want.IsNearlyZero())
	{
		const float Alpha = 1.f - FMath::Exp(-3.5f * StepSeconds);
		ChaseHeading = FMath::Lerp(ChaseHeading, Want, Alpha).GetSafeNormal2D();
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, ChaseHeading).GetSafeNormal() * -1.f;
	const FVector Desired = Pivot - ChaseHeading * Arm + FVector(0.f, 0.f, Up) + Right * Side;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoChase), false, Kate);
	FVector At = Desired;
	if (World->SweepSingleByChannel(Hit, Pivot, Desired, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(18.f), Query))
	{
		At = Pivot + (Desired - Pivot) * FMath::Max(Hit.Time, 0.3f);
	}
	// Ease what the lens sees as an offset from her, so it does not trail.
	const FVector Offset = At - Pivot;
	const float Follow = 1.f - FMath::Exp(-14.f * StepSeconds);
	ChaseOffset = FMath::Lerp(ChaseOffset.IsNearlyZero() ? Offset : ChaseOffset, Offset, Follow);
	const FVector Lens = Pivot + ChaseOffset;
	const FVector Aim = Pivot + FVector(0.f, 0.f, 45.f) + ChaseHeading * 120.f;
	ChaseAim = ChaseAim.IsNearlyZero() ? Aim : FMath::Lerp(ChaseAim, Aim, 1.f - FMath::Exp(-10.f * StepSeconds));
	Camera->SetActorLocationAndRotation(Lens, FRotationMatrix::MakeFromX(ChaseAim - Lens).Rotator());
}

/** Lucky's shot: a camera to the side of the two of them, so Kate and the dog are both in the picture. */
void FHawkeyeDemoRunner::StepProfile(UWorld* World, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeDemo;
	ACameraActor* Camera = PanCamera.Get();
	if (!Camera)
	{
		return;
	}
	const FVector KateAt = Kate->GetActorLocation();
	const FVector Mid = Dog.IsValid() ? (KateAt + Dog->GetActorLocation()) * 0.5f : KateAt;
	if (Kate->GetVelocity().Size2D() > 150.f)
	{
		ChaseHeading = FMath::Lerp(ChaseHeading, Kate->GetVelocity().GetSafeNormal2D(), 1.f - FMath::Exp(-2.f * StepSeconds)).GetSafeNormal2D();
	}
	if (!bChaseStarted)
	{
		ChaseHeading = RunDirection;
		bChaseStarted = true;
	}
	const FVector Across = FVector::CrossProduct(FVector::UpVector, ChaseHeading).GetSafeNormal();
	const FVector Pivot = Mid + FVector(0.f, 0.f, 70.f);
	FVector Wanted = Pivot;
	bool bFound = false;
	for (const float Sign : { ProfileSide, -ProfileSide })
	{
		const FVector Try = Pivot + Across * Sign * 520.f + ChaseHeading * 120.f + FVector(0.f, 0.f, 50.f);
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoProfile), false, Kate);
		if (!World->SweepSingleByChannel(Hit, Pivot, Try, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(25.f), Query))
		{
			Wanted = Try;
			ProfileSide = Sign;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		Wanted = Pivot - ChaseHeading * 450.f + FVector(0.f, 0.f, 120.f);
	}
	ChaseOffset = ChaseOffset.IsNearlyZero() ? Wanted - Pivot : FMath::Lerp(ChaseOffset, Wanted - Pivot, 1.f - FMath::Exp(-4.f * StepSeconds));
	const FVector Lens = Pivot + ChaseOffset;
	ChaseAim = ChaseAim.IsNearlyZero() ? Pivot : FMath::Lerp(ChaseAim, Pivot, 1.f - FMath::Exp(-8.f * StepSeconds));
	Camera->SetActorLocationAndRotation(Lens, FRotationMatrix::MakeFromX(ChaseAim - Lens).Rotator());
}

void FHawkeyeDemoRunner::WatchEvents(AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeDemo;
	const bool bZipping = Kate->GetGrappleComponent()->IsZipping();
	if (bZipping != bWasZipping)
	{
		Note(bZipping ? TEXT("zip_start") : TEXT("zip_end"));
		bWasZipping = bZipping;
	}
	const UParkourComponent* Parkour = Kate->GetParkourComponent();
	const bool bBusy = Parkour->IsBusy();
	if (bBusy && !bWasBusy)
	{
		FString Move = UEnum::GetValueAsString(Parkour->GetActiveMove());
		Move.RemoveFromStart(TEXT("EHawkeyeParkourMove::"));
		Note(TEXT("parkour_") + Move);
	}
	bWasBusy = bBusy;
	const bool bGround = Kate->GetCharacterMovement()->IsMovingOnGround();
	if (!bGround)
	{
		PeakFall = FMath::Max(PeakFall, -Kate->GetVelocity().Z);
	}
	else if (!bWasOnGround)
	{
		if (PeakFall >= 300.f)
		{
			Note(FString::Printf(TEXT("%s_%.0f"), PeakFall >= HeavyFallSpeed ? TEXT("land_heavy") : TEXT("land"), PeakFall));
		}
		PeakFall = 0.f;
	}
	bWasOnGround = bGround;
}

void FHawkeyeDemoRunner::Prepare(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeDemo;
	WallStart = FPlatformTime::Seconds();
	bHadFixedStep = FApp::UseFixedTimeStep();
	PreviousFixedDelta = FApp::GetFixedDeltaTime();
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / Fps);
	GEngine->Exec(World, Shot.bNight ? TEXT("hawkeye.TimeOfDay Night") : TEXT("hawkeye.TimeOfDay Day"));
	bTimeOfDaySet = true;

	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
		{
			Brain->SetThinkingEnabled(false);
		}
	}
	HawkeyeFreezePartner(World);
	// The challenge pedestals (their ring icons and floating labels) are not part of the picture.
	for (TActorIterator<AChallengeStart> It(World); It; ++It)
	{
		It->SetActorHiddenInGame(true);
	}
	for (TActorIterator<ALuckyCharacter> It(World); It; ++It)
	{
		It->SetThinkingEnabled(Shot.Kind == EKind::Lucky);
	}
	EnsureQuiver(Test, Kate->GetInventoryComponent());

	StartFeet = Feet(Kate);
	Along = Kate->GetActorForwardVector().GetSafeNormal2D();
	if (TActorIterator<APlayerStart> PlayerStart(World); PlayerStart)
	{
		Along = PlayerStart->GetActorForwardVector().GetSafeNormal2D();
		const FVector From = PlayerStart->GetActorLocation() + FVector(0.f, 0.f, 200.f);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoGround), false, Kate);
		FHitResult Hit;
		StartFeet = World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Query)
			? FVector(Hit.ImpactPoint) : FVector(PlayerStart->GetActorLocation());
	}

	UGameViewportClient* Client = GEngine->GameViewport;
	const FIntPoint Size = Client && Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	Test->AddInfo(FString::Printf(TEXT("Demo %s: viewport %dx%d, fixed step %d fps, %s."), *Shot.Name, Size.X, Size.Y, Fps,
		Shot.bNight ? TEXT("night") : TEXT("day")));
	if (Size.X != WidthWanted || Size.Y != HeightWanted)
	{
		Test->AddWarning(FString::Printf(TEXT("The viewport is %dx%d, not %dx%d: launch with -ResX=1920 -ResY=1080 on a display at least that big."),
			Size.X, Size.Y, WidthWanted, HeightWanted));
	}

	switch (Shot.Kind)
	{
	case EKind::Route:
	case EKind::Descent:
	{
		Start = FindStart(World);
		const UChallengeDefinition* Definition = Start.IsValid() ? Start->Definition.Get() : nullptr;
		if (!Definition || Definition->Checkpoints.Num() == 0)
		{
			bFailed = true;
			FailReason = TEXT("no City_Challenge_traversal_1 pedestal on the district");
			return;
		}
		int32 FirstDescent = Definition->Checkpoints.Num();
		for (int32 Index = 0; Index < Definition->CheckpointLegs.Num(); ++Index)
		{
			if (Definition->CheckpointLegs[Index] == EChallengeLeg::Descent)
			{
				FirstDescent = Index;
				break;
			}
		}
		if (Shot.Kind == EKind::Route)
		{
			EndLeg = FirstDescent;
			const float Yaw = Start->GetActorRotation().Yaw;
			PutKate(PC, Kate, Start->GetStandLocation(), Yaw);
			BeginLeg(0);
		}
		else
		{
			if (FirstDescent >= Definition->Checkpoints.Num())
			{
				bFailed = true;
				FailReason = TEXT("traversal_1 has no descent leg");
				return;
			}
			EndLeg = Definition->Checkpoints.Num();
			const FVector Before = FirstDescent > 0 ? Definition->Checkpoints[FirstDescent - 1].GetLocation() - FVector(0.f, 0.f, RingUp)
				: Start->GetStandLocation();
			const FVector Towards = Definition->Checkpoints[FirstDescent].GetLocation() - Before;
			PutKate(PC, Kate, Before, Towards.Rotation().Yaw);
			BeginLeg(FirstDescent);
		}
		break;
	}
	case EKind::Lucky:
	{
		RunDirection = -Along;
		PutKate(PC, Kate, StartFeet, RunDirection.Rotation().Yaw);
		Dog = nullptr;
		for (TActorIterator<ALuckyCharacter> It(World); It && !Dog.IsValid(); ++It)
		{
			Dog = *It;
		}
		if (!Dog.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Dog = World->SpawnActor<ALuckyCharacter>(ALuckyCharacter::StaticClass(), FTransform(Along.Rotation(), StartFeet + FVector(0.f, 0.f, 60.f)), Params);
		}
		if (ALuckyCharacter* Lucky = Dog.Get())
		{
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
			Lucky->TeleportTo(StartFeet - Side * 120.f + FVector(0.f, 0.f, Lucky->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f),
				Along.Rotation(), false, true);
			Lucky->SetThinkingEnabled(true);
		}
		else
		{
			bFailed = true;
			FailReason = TEXT("no Lucky in the district and none could be spawned");
		}
		break;
	}
	case EKind::Establish:
	{
		Kate->SetActorHiddenInGame(true);
		Kate->SetActorEnableCollision(false);
		if (UCharacterMovementComponent* Movement = Kate->GetCharacterMovement())
		{
			Movement->DisableMovement();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(StartFeet + FVector(0.f, 0.f, 3000.f)), Params);
		PanCamera = Camera;
		if (Camera)
		{
			PC->SetViewTarget(Camera);
		}
		else
		{
			bFailed = true;
			FailReason = TEXT("the pan camera could not be spawned");
		}
		break;
	}
	}
	if ((Shot.Kind == EKind::Route || Shot.Kind == EKind::Descent || Shot.Kind == EKind::Lucky) && !bFailed)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(Kate->GetActorLocation()), Params);
		PanCamera = Camera;
		if (Camera)
		{
			if (Shot.Kind != EKind::Lucky && Camera->GetCameraComponent())
			{
				// The shot's own camera has no follow camera's trip blur: give it the same low blur, or a zip smears the frame.
				const IConsoleVariable* Amount = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.Traversal.MotionBlur"));
				const IConsoleVariable* Max = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.Traversal.MotionBlurMax"));
				FPostProcessSettings& Settings = Camera->GetCameraComponent()->PostProcessSettings;
				Settings.bOverride_MotionBlurAmount = true;
				Settings.MotionBlurAmount = Amount ? Amount->GetFloat() : 0.12f;
				Settings.bOverride_MotionBlurMax = true;
				Settings.MotionBlurMax = Max ? Max->GetFloat() : 1.f;
			}
			PC->SetViewTarget(Camera);
			if (Shot.Kind == EKind::Route)
			{
				StepChase(World, Kate);
			}
		}
	}
	Sink.Start();
}

void FHawkeyeDemoRunner::Finish(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate)
{
	using namespace HawkeyeDemo;
	StopMoving(PC);
	Sink.Stop();
	if (bTimeOfDaySet && World)
	{
		GEngine->Exec(World, TEXT("hawkeye.TimeOfDay Saved"));
	}
	FApp::SetUseFixedTimeStep(bHadFixedStep);
	FApp::SetFixedDeltaTime(PreviousFixedDelta);
	if (Kate)
	{
		if (Shot.Kind == EKind::Establish)
		{
			Kate->SetActorHiddenInGame(false);
			Kate->SetActorEnableCollision(true);
		}
		if (PC)
		{
			PC->SetViewTarget(Kate);
		}
	}
	if (ACameraActor* Camera = PanCamera.Get())
	{
		Camera->Destroy();
	}

	const bool bWrongSize = Sink.Taken > 0 && (Sink.Size.X != WidthWanted || Sink.Size.Y != HeightWanted);
	const int32 Lost = Sink.Failed->GetValue();
	TArray<FString> EventJson;
	for (const TPair<int32, FString>& Event : Events)
	{
		EventJson.Add(FString::Printf(TEXT("    {\"frame\": %d, \"event\": \"%s\"}"), Event.Key, *Event.Value));
	}
	const FString Json = FString::Printf(TEXT("{\n  \"shot\": \"%s\",\n  \"frames\": %d,\n  \"fps\": %d,\n  \"width\": %d,\n  \"height\": %d,\n")
		TEXT("  \"first_frame\": 1,\n  \"dropped_frames\": %d,\n  \"write_failures\": %d,\n  \"completed\": %s,\n  \"fail\": \"%s\",\n  \"events\": [\n%s\n  ]\n}\n"),
		*Shot.Name, Sink.Taken, Fps, Sink.Size.X, Sink.Size.Y, Dropped, Lost, !bFailed ? TEXT("true") : TEXT("false"),
		*FailReason.ReplaceCharWithEscapedChar(), *FString::Join(EventJson, TEXT(",\n")));
	FFileHelper::SaveStringToFile(Json, *(ShotDir(Shot.Name) / TEXT("shot.json")));

	Test->AddInfo(FString::Printf(TEXT("Demo %s: %d frames (%.1f s) of %dx%d in %s, %d dropped, %d write failures, %d events."), *Shot.Name,
		Sink.Taken, Sink.Taken / float(Fps), Sink.Size.X, Sink.Size.Y, *ShotDir(Shot.Name), Dropped, Lost, Events.Num()));
	if (bFailed)
	{
		Test->AddError(FString::Printf(TEXT("Demo %s stopped at frame %d: %s."), *Shot.Name, FrameIndex, *FailReason));
	}
	if (Dropped > 0)
	{
		Test->AddError(FString::Printf(TEXT("Demo %s dropped %d frames: the sequence has holes."), *Shot.Name, Dropped));
	}
	if (Lost > 0)
	{
		Test->AddError(FString::Printf(TEXT("Demo %s: %d frames could not be written."), *Shot.Name, Lost));
	}
	if (bWrongSize)
	{
		Test->AddError(FString::Printf(TEXT("Demo %s: the frames are %dx%d, not %dx%d."), *Shot.Name, Sink.Size.X, Sink.Size.Y,
			WidthWanted, HeightWanted));
	}
	if (Sink.Taken == 0)
	{
		Test->AddError(FString::Printf(TEXT("Demo %s took no frames."), *Shot.Name));
	}
}

bool FHawkeyeDemoRunner::Update()
{
	using namespace HawkeyeDemo;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		if (Phase == EPhase::Capture || Phase == EPhase::Settle)
		{
			bFailed = true;
			FailReason = TEXT("the world or Kate went away mid-shot");
			Finish(World, PC, Kate);
			return true;
		}
		if (!World || World->GetTimeSeconds() > 30.0)
		{
			Test->AddError(TEXT("Demo: no game world or no Kate."));
			return true;
		}
		return false;
	}
	if (PhaseStartSeconds < 0.0)
	{
		PhaseStartSeconds = World->GetTimeSeconds();
	}

	switch (Phase)
	{
	case EPhase::WaitWorld:
		if ((!IsSpawnDone(World) || !Kate->GetCharacterMovement()->IsMovingOnGround()) && World->GetTimeSeconds() - PhaseStartSeconds < 40.0)
		{
			return false;
		}
		Phase = EPhase::Prepare;
		return false;

	case EPhase::Prepare:
		Prepare(World, PC, Kate);
		if (bFailed)
		{
			Test->AddError(FString::Printf(TEXT("Demo %s: %s."), *Shot.Name, *FailReason));
			Finish(World, PC, Kate);
			return true;
		}
		IFileManager::Get().DeleteDirectory(*ShotDir(Shot.Name), false, true);
		IFileManager::Get().MakeDirectory(*ShotDir(Shot.Name), true);
		Phase = EPhase::Settle;
		return false;

	case EPhase::Settle:
		// Kate stands (the pan camera moves to its first pose) while the camera arm, the snow and the streaming settle.
		if (Shot.Kind == EKind::Establish)
		{
			StepEstablish(World, PC);
		}
		if (++FramesWaited < SettleFrames)
		{
			return false;
		}
		Phase = EPhase::Capture;
		FrameIndex = 0;
		LastFrameCounter = 0;
		return false;

	case EPhase::Capture:
		break;

	case EPhase::Drain:
		// The last request is drawn a frame or two later; the world keeps ticking until it is.
		if (--DrainLeft > 0)
		{
			return false;
		}
		for (int32 Waits = 0; Sink.Outstanding() > 0 && Waits < 12000; ++Waits)
		{
			FPlatformProcess::Sleep(0.005f);
		}
		Phase = EPhase::Done;
		Finish(World, PC, Kate);
		return true;

	case EPhase::Done:
		return true;
	}

	// Writers too far behind: wait here. The world is on a fixed step, so waiting costs the video nothing.
	for (int32 Waits = 0; Sink.Outstanding() > MaxInFlight && Waits < 6000; ++Waits)
	{
		FPlatformProcess::Sleep(0.005f);
	}

	bool bScriptDone = false;
	switch (Shot.Kind)
	{
	case EKind::Route:
	case EKind::Descent:
		bScriptDone = StepRoute(World, PC, Kate);
		StepChase(World, Kate);
		break;
	case EKind::Lucky:
		bScriptDone = StepLucky(World, PC, Kate);
		break;
	case EKind::Establish:
		bScriptDone = StepEstablish(World, PC);
		break;
	}
	WatchEvents(Kate);

	if (bScriptDone && TailLeft < 0)
	{
		TailLeft = Shot.Kind == EKind::Route || Shot.Kind == EKind::Descent ? TailFrames : 0;
	}
	if (FrameIndex >= Shot.MaxFrames)
	{
		if (!bScriptDone)
		{
			Note(TEXT("frame_cap_reached"));
		}
		TailLeft = 0;
	}
	if (FPlatformTime::Seconds() - WallStart > WallBudgetSeconds)
	{
		bFailed = true;
		FailReason = FString::Printf(TEXT("over %.0f s of wall time at frame %d"), WallBudgetSeconds, FrameIndex);
		TailLeft = 0;
	}

	if ((!bFailed || TailLeft != 0) && FrameIndex < Shot.MaxFrames)
	{
		++FrameIndex;
		const FString Path = ShotDir(Shot.Name) / FString::Printf(TEXT("frame_%05d.png"), FrameIndex);
		if (!Sink.Request(Path))
		{
			++Dropped;
		}
		// A frame the engine skipped between two requests is a hole too.
		if (LastFrameCounter != 0 && GFrameCounter > LastFrameCounter + 1)
		{
			Dropped += static_cast<int32>(GFrameCounter - LastFrameCounter - 1);
		}
		LastFrameCounter = GFrameCounter;
	}

	const bool bEnd = bFailed || (TailLeft >= 0 && --TailLeft < 0);
	if (!bEnd)
	{
		return false;
	}
	Phase = EPhase::Drain;
	return false;
}

namespace HawkeyeDemo
{
	/** The shared start of every shot: skipped unless asked for, and without a renderer. */
	static bool Begin(FAutomationTestBase* Test, const FShot& Shot)
	{
		if (!FApp::CanEverRender())
		{
			Test->AddWarning(TEXT("Skipped (no RHI): the demo shots. Run them from the standalone game (-game, no -nullrhi)."));
			return true;
		}
		if (!FParse::Param(FCommandLine::Get(), TEXT("HawkeyeDemo")))
		{
			Test->AddWarning(TEXT("Skipped (demo capture is opt-in): pass -HawkeyeDemo, see Tools/demo-capture.ps1."));
			return true;
		}
		FShot Wanted = Shot;
		int32 Cap = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("DemoFrames="), Cap) && Cap > 0)
		{
			Wanted.MaxFrames = Cap;
		}
		AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeDemoRunner(Test, Wanted));
		return true;
	}
}

bool FHawkeyeDemoGrappleRun::RunTest(const FString& Parameters)
{
	return HawkeyeDemo::Begin(this, { TEXT("grapple_run"), HawkeyeDemo::EKind::Route, true, 1200 });
}

bool FHawkeyeDemoDescent::RunTest(const FString& Parameters)
{
	return HawkeyeDemo::Begin(this, { TEXT("descent"), HawkeyeDemo::EKind::Descent, true, 900 });
}

bool FHawkeyeDemoLucky::RunTest(const FString& Parameters)
{
	return HawkeyeDemo::Begin(this, { TEXT("lucky"), HawkeyeDemo::EKind::Lucky, true, 900 });
}

bool FHawkeyeDemoEstablish::RunTest(const FString& Parameters)
{
	return HawkeyeDemo::Begin(this, { TEXT("establish"), HawkeyeDemo::EKind::Establish, true, 720 });
}

#endif
