#include "Animation/AnimInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/BowComponent.h"
#include "Combat/BowIKAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "Traversal/TraversalPoseRules.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Strips of Kate's grapple trips and landings, side on and from the front three-quarter, with the body pose off and on
 * (claude-docs/animation.md, "Traversal body pose"; gameplay-semantics.md, "Traversal body pose"):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -ExecCmds="Automation RunTests Hawkeye.Screenshot.Traversal; Quit"
 *
 * On the district street by the PlayerStart (level ground, AI off) Kate is moved along a line by the zip movement
 * itself (the line cleared by a capsule sweep, found by turning the heading away from walls), or dropped from a height.
 * Each scenario runs twice, hawkeye.ZipPose 0 and 1, and the world's time is held at fixed times for a shot from each
 * view; the frames are laid side by side into Saved/Screenshots/Traversal/<scenario>_<side|three_quarter>_<off|on>.png,
 * the single frames in Saved/Screenshots/Traversal/Frames/. A yellow line is drawn from her rope hand to the anchor.
 *
 * Scenarios: launch (the crouch-load while the arrow is in the air, then the hop and the jerk), pull (the line),
 * redirect (a chain's turn), tail_run (a sprint, then stopped dead: the ponytail's swing) and cut (a teleport mid-run), release (let go into the fall and the landing after), brace (the last 0.3 s of a long
 * climb), arrive_brake, arrive_slide and arrive_step (the three arrival classes), land_light, land_heavy and land_roll.
 * With the pose on it also checks, in the log and as errors: the phase the pose is in at each key moment, the body
 * tipped mid-pull, and that her drawn hand is on the rope hand's target (within 45 cm; a warning past 25).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotTraversal, "Hawkeye.Screenshot.Traversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeTraversalShots
{
	static constexpr int32 Crop = 720;
	static constexpr int32 Tile = 360;
	static constexpr double HoldSettle = 0.35;
	static constexpr double ViewSettle = 0.25;

	struct FStage
	{
		TWeakObjectPtr<AHawkeyeCharacter> Kate;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<APointLight> Light;
		FVector Feet = FVector::ZeroVector;
		FVector Along = FVector::ForwardVector;
		float DefaultZipSpeed = 1800.f;
		/** The flat direction a run scenario goes along (clear of walls for the run's length). */
		FVector RunHeading = FVector::ForwardVector;
		/** Per strip name, its frames in order. */
		TMap<FString, TArray<FString>> Frames;
	};

	enum class EPhase : uint8
	{
		Setup,
		Running,
		Freeze,
		SettleSide,
		WaitSide,
		SettleQuarter,
		WaitQuarter,
		Cleanup,
		Done
	};

	/** One scenario in one mode. */
	struct FScenario
	{
		FString Name;
		/** Seconds relative to the reference moment. */
		TArray<float> Times;
		/** Positions her and starts the motion; may schedule events (seconds after the start). */
		TFunction<void(FStage&, UWorld&)> Start;
		/** Fires once when the world has run this long since Start. */
		TArray<TPair<double, TFunction<void(FStage&, UWorld&)>>> Events;
		/** Called every frame while the scenario runs, with the seconds since Start (a run's stick input). */
		TFunction<void(FStage&, UWorld&, double)> PerTick;
		/** The reference moment is this long after Start (>= 0), or, when negative, the moment the zip ends / she
		 * touches down. */
		double Reference = 0.0;
		bool bReferenceIsZipEnd = false;
		bool bReferenceIsTouchdown = false;
		/** The reference moment is when the pose enters the brace. */
		bool bReferenceIsBrace = false;
		/** The shot is from the player's own camera behind her (no stage camera), one view. */
		bool bPlayerCamera = false;
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

	static FString OutDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Traversal"));
	}

	static void SetPoseEnabled(bool bOn)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("hawkeye.ZipPose")))
		{
			Variable->Set(bOn ? 1 : 0, ECVF_SetByCode);
		}
	}

	/** The nearest ground to Start where everything within 150 cm is within 3 cm of its height (the clip strips' test).
	 */
	static FVector FindLevelGround(UWorld* World, const AActor* Ignore, const FVector& Start, const FVector& Along)
	{
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TraversalShotLevel), false, Ignore);
		auto Probe = [World, &Query](const FVector& XY, float FromZ, FVector& Out)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(
					Hit, FVector(XY.X, XY.Y, FromZ + 200.f), FVector(XY.X, XY.Y, FromZ - 400.f), ECC_Visibility, Query))
			{
				return false;
			}
			Out = Hit.ImpactPoint;
			return true;
		};
		for (int32 Ring = 0; Ring <= 16; ++Ring)
		{
			for (const FVector& Direction : {-Side, Side, Along, -Along})
			{
				FVector Centre;
				if (!Probe(Start + Direction * (Ring * 50.f), Start.Z, Centre))
				{
					continue;
				}
				bool bLevel = true;
				for (int32 Spoke = 0; Spoke < 12 && bLevel; ++Spoke)
				{
					const FVector Out = FRotator(0.f, Spoke * 30.f, 0.f).Vector();
					for (const float Reach : {40.f, 90.f, 150.f})
					{
						FVector At;
						if (!Probe(Centre + Out * Reach, Centre.Z, At) || FMath::Abs(At.Z - Centre.Z) > 3.f)
						{
							bLevel = false;
							break;
						}
					}
				}
				if (bLevel)
				{
					return Centre;
				}
				if (Ring == 0)
				{
					break;
				}
			}
		}
		return Start;
	}

	/**
	 * The far end of a line from From along Heading (flat), Length long and Rise up, that Kate's capsule can sweep; the
	 * heading is turned in steps of 25 degrees to either side and the line shortened when a wall is in the way.
	 */
	static FVector FindClearEnd(
		UWorld& World, const ACharacter& Kate, const FVector& From, const FVector& Heading, float Length, float Rise)
	{
		const UCapsuleComponent* Capsule = Kate.GetCapsuleComponent();
		const FCollisionShape Shape =
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TraversalShotLine), false, &Kate);
		FVector Best = From + Heading.GetSafeNormal2D() * Length * 0.5f + FVector(0.f, 0.f, Rise * 0.5f);
		float BestScore = -1.f;
		for (const float Scale : {1.f, 0.8f, 0.6f})
		{
			for (int32 Turn = 0; Turn <= 8; ++Turn)
			{
				const float Angle = (Turn % 2 == 0 ? 1.f : -1.f) * 25.f * ((Turn + 1) / 2);
				const FVector Direction = FRotator(0.f, Angle, 0.f).RotateVector(Heading.GetSafeNormal2D());
				const FVector End = From + Direction * Length * Scale + FVector(0.f, 0.f, Rise * Scale);
				FHitResult Hit;
				const bool bBlocked =
					World.SweepSingleByChannel(Hit, From, End, FQuat::Identity, ECC_Pawn, Shape, Query);
				if (!bBlocked)
				{
					return End;
				}
				const float Score = Scale * 100.f - FMath::Abs(Angle) * 0.1f;
				if (Score > BestScore)
				{
					BestScore = Score;
					Best = From + (End - From) * FMath::Max(0.2f, Hit.Time - 0.1f);
				}
			}
		}
		return Best;
	}

	/** Kate on the ground at the stage, still, facing Along, any zip or fall over. */
	static void StandKate(FStage& Stage, UWorld& World)
	{
		AHawkeyeCharacter* Kate = Stage.Kate.Get();
		UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
		if (!Kate || !Movement)
		{
			return;
		}
		if (Movement->IsZipping())
		{
			Movement->ReleaseZip(false);
		}
		Movement->ZipSpeed = Stage.DefaultZipSpeed;
		Kate->ClearTraversalRopeTarget();
		Kate->ClearTraversalOverrides();
		Kate->TeleportTo(
			Stage.Feet + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
			Stage.Along.Rotation(), false, true);
		Kate->SetActorRotation(Stage.Along.Rotation());
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
		Kate->GetTraversalPoseState().Reset();
	}

	static float HalfHeight(const AHawkeyeCharacter& Kate)
	{
		return Kate.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** Starts a zip along a line with the rope toward its far end. */
	static void ZipAlong(FStage& Stage, const FVector& Launch, const FVector& End, bool bHop)
	{
		AHawkeyeCharacter* Kate = Stage.Kate.Get();
		UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
		if (!Movement)
		{
			return;
		}
		FHawkeyeZipLine Line;
		Line.Launch = Launch;
		Line.End = End;
		Line.bHop = bHop;
		Kate->SetTraversalRopeTarget(End + FVector(0.f, 0.f, 60.f));
		Movement->StartZip(Line);
	}

	/** The scenarios, in the order they run. */
	static TArray<FScenario> MakeScenarios()
	{
		TArray<FScenario> Out;
		const auto GroundLaunch = [](FStage& Stage, UWorld& World, float Length, float Rise,
									  float YawDegrees) -> FVector
		{
			AHawkeyeCharacter* Kate = Stage.Kate.Get();
			StandKate(Stage, World);
			const FVector Centre = Kate->GetActorLocation();
			const FVector Launch = Kate->GetGrappleComponent()->ComputeZipLaunch(Centre, /*bFromGround=*/true);
			const FVector Heading = FRotator(0.f, YawDegrees, 0.f).RotateVector(Stage.Along);
			const FVector End = FindClearEnd(World, *Kate, Launch, Heading, Length, Rise);
			ZipAlong(Stage, Launch, End, /*bHop=*/true);
			return End;
		};
		{
			// The arrow is in the air for 0.35 s (the crouch-load), then the hop starts, the line takes tension and the
			// jerk follows. Times are from the hop's start.
			FScenario S;
			S.Name = TEXT("launch");
			S.Times = {-0.3f, -0.18f, -0.06f, 0.f, 0.06f, 0.12f, 0.2f, 0.32f};
			S.Reference = 0.35;
			S.Start = [](FStage& Stage, UWorld& World)
			{
				StandKate(Stage, World);
				Stage.Kate->SetTraversalArrowInFlight(true);
			};
			S.Events.Add({0.35, [GroundLaunch](FStage& Stage, UWorld& World)
				{
					GroundLaunch(Stage, World, 1500.f, 700.f, 0.f);
					Stage.Kate->SetTraversalArrowInFlight(false);
				}});
			Out.Add(S);
		}
		{
			FScenario S;
			S.Name = TEXT("pull");
			S.Times = {0.25f, 0.33f, 0.41f, 0.49f, 0.57f, 0.65f, 0.73f, 0.81f};
			S.Start = [GroundLaunch](FStage& Stage, UWorld& World) { GroundLaunch(Stage, World, 2400.f, 900.f, 0.f); };
			Out.Add(S);
		}
		{
			// A chain 0.55 s in, at full speed: the line swings 70 degrees to the left and a little less steeply up.
			FScenario S;
			S.Name = TEXT("redirect");
			S.Times = {-0.1f, 0.f, 0.04f, 0.08f, 0.12f, 0.2f, 0.35f, 0.5f};
			S.Reference = 0.55;
			S.Start = [GroundLaunch](FStage& Stage, UWorld& World) { GroundLaunch(Stage, World, 2200.f, 700.f, 30.f); };
			S.Events.Add({0.55, [](FStage& Stage, UWorld& World)
				{
					AHawkeyeCharacter* Kate = Stage.Kate.Get();
					if (!Kate || !Kate->GetHawkeyeMovement()->IsZipping())
					{
						return;
					}
					const FVector Now = Kate->GetActorLocation();
					const FVector Heading =
						FRotator(0.f, -70.f, 0.f).RotateVector(Kate->GetVelocity().GetSafeNormal2D());
					const FVector End = FindClearEnd(World, *Kate, Now, Heading, 1500.f, 350.f);
					ZipAlong(Stage, Now, End, /*bHop=*/false);
				}});
			Out.Add(S);
		}
		{
			// Let go with a jump 0.6 s in: the tuck, the fall, and the landing it ends in.
			FScenario S;
			S.Name = TEXT("release");
			S.Times = {0.f, 0.1f, 0.2f, 0.3f, 0.45f, 0.6f, 0.8f, 1.0f};
			S.Reference = 0.6;
			S.Start = [GroundLaunch](FStage& Stage, UWorld& World) { GroundLaunch(Stage, World, 2200.f, 900.f, 0.f); };
			S.Events.Add({0.6, [](FStage& Stage, UWorld&)
				{
					if (AHawkeyeCharacter* Kate = Stage.Kate.Get())
					{
						Kate->GetHawkeyeMovement()->ReleaseZip(/*bJump=*/true);
					}
				}});
			Out.Add(S);
		}
		// A zip ends on the stage's floor from a start Length away, Rise above it (the line's climb), with or without
		// forward held. Zip speed is the movement's own: it reaches the top speed by the end whichever the line.
		const auto Arrival = [&Out](const TCHAR* Name, float Length, float Rise, bool bForward, bool bFromBrace)
		{
			FScenario S;
			S.Name = Name;
			if (bFromBrace)
			{
				S.Times = {0.f, 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.36f};
				S.bReferenceIsBrace = true;
			}
			else
			{
				S.Times = {0.f, 0.06f, 0.12f, 0.2f, 0.3f, 0.42f, 0.6f, 0.8f};
				S.bReferenceIsZipEnd = true;
			}
			S.Start = [Length, Rise, bForward](FStage& Stage, UWorld& World)
			{
				AHawkeyeCharacter* Kate = Stage.Kate.Get();
				UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
				if (!Movement)
				{
					return;
				}
				StandKate(Stage, World);
				const FVector End = Stage.Feet + FVector(0.f, 0.f, HalfHeight(*Kate) + 2.f);
				const FVector From = FindClearEnd(World, *Kate, End, -Stage.Along, Length, Rise);
				Kate->TeleportTo(From, Stage.Along.Rotation(), false, true);
				Movement->StopMovementImmediately();
				Movement->SetMovementMode(MOVE_Falling);
				Kate->SetTraversalForwardHeld(bForward);
				// The momentum she comes in with is the line's own.
				ZipAlong(Stage, From, End, /*bHop=*/false);
			};
			Out.Add(S);
		};
		Arrival(TEXT("brace"), 1600.f, 550.f, false, /*bFromBrace=*/true);
		Arrival(TEXT("arrive_brake"), 1600.f, 550.f, false, false);
		Arrival(TEXT("arrive_slide"), 1600.f, 0.f, true, false);
		Arrival(TEXT("arrive_step"), 200.f, 40.f, false, false);
		// From the player's own camera: mid-pull and on the arrival, looking where she goes.
		{
			FScenario S;
			S.Name = TEXT("player_pull");
			S.Times = {0.45f, 0.65f};
			S.bPlayerCamera = true;
			S.Start = [GroundLaunch](FStage& Stage, UWorld& World) { GroundLaunch(Stage, World, 2400.f, 900.f, 0.f); };
			Out.Add(S);
		}
		{
			FScenario S;
			S.Name = TEXT("player_arrive");
			S.Times = {0.0f, 0.12f, 0.3f};
			S.bReferenceIsZipEnd = true;
			S.bPlayerCamera = true;
			S.Start = [](FStage& Stage, UWorld& World)
			{
				AHawkeyeCharacter* Kate = Stage.Kate.Get();
				UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
				if (!Movement)
				{
					return;
				}
				StandKate(Stage, World);
				const FVector End = Stage.Feet + FVector(0.f, 0.f, HalfHeight(*Kate) + 2.f);
				const FVector From = FindClearEnd(World, *Kate, End, -Stage.Along, 1600.f, 550.f);
				Kate->TeleportTo(From, Stage.Along.Rotation(), false, true);
				Movement->StopMovementImmediately();
				Movement->SetMovementMode(MOVE_Falling);
				ZipAlong(Stage, From, End, /*bHop=*/false);
			};
			Out.Add(S);
		}
		const auto Landing = [&Out](const TCHAR* Name, float Drop, float ForwardSpeed)
		{
			FScenario S;
			S.Name = Name;
			S.Times = {0.f, 0.1f, 0.2f, 0.3f, 0.4f, 0.55f, 0.75f, 1.0f};
			S.bReferenceIsTouchdown = true;
			S.Start = [Drop, ForwardSpeed](FStage& Stage, UWorld& World)
			{
				AHawkeyeCharacter* Kate = Stage.Kate.Get();
				UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
				if (!Movement)
				{
					return;
				}
				StandKate(Stage, World);
				const FVector At = Stage.Feet + FVector(0.f, 0.f, HalfHeight(*Kate) + Drop);
				Kate->TeleportTo(At, Stage.Along.Rotation(), false, true);
				Movement->SetMovementMode(MOVE_Falling);
				Movement->Velocity = Stage.Along * ForwardSpeed;
			};
			Out.Add(S);
		};
		Landing(TEXT("land_light"), 170.f, 0.f);
		Landing(TEXT("land_heavy"), 330.f, 320.f);
		Landing(TEXT("land_roll"), 650.f, 450.f);
		// Running on level ground for RunSeconds (sprinting, stick held along RunHeading), then the stick is let go and
		// she is stopped dead (tail_run), or she is cut to the start of the run with the stick still held (cut).
		const auto RunStart = [](FStage& Stage, UWorld& World)
		{
			AHawkeyeCharacter* Kate = Stage.Kate.Get();
			StandKate(Stage, World);
			const FVector Centre = Kate->GetActorLocation();
			const FVector End = FindClearEnd(World, *Kate, Centre, Stage.Along, 1500.f, 0.f);
			Stage.RunHeading = (End - Centre).GetSafeNormal2D();
			Kate->SetActorRotation(Stage.RunHeading.Rotation());
			Kate->PressSprint(false);
		};
		{
			FScenario S;
			S.Name = TEXT("tail_run");
			S.Times = {0.9f, 1.3f, 1.62f, 1.7f, 1.8f, 1.95f, 2.2f, 2.7f};
			S.Start = RunStart;
			S.PerTick = [](FStage& Stage, UWorld& World, double Elapsed)
			{
				AHawkeyeCharacter* Kate = Stage.Kate.Get();
				if (!Kate)
				{
					return;
				}
				if (Elapsed < 1.6)
				{
					Kate->AddMovementInput(Stage.RunHeading, 1.f);
				}
				else
				{
					Kate->ReleaseSprint();
					Kate->GetHawkeyeMovement()->StopMovementImmediately();
				}
			};
			Out.Add(S);
		}
		{
			FScenario S;
			S.Name = TEXT("cut");
			S.Times = {0.8f, 0.98f, 1.03f, 1.08f, 1.15f, 1.3f, 1.6f, 2.1f};
			S.Start = RunStart;
			S.PerTick = [](FStage& Stage, UWorld& World, double Elapsed)
			{
				AHawkeyeCharacter* Kate = Stage.Kate.Get();
				if (!Kate)
				{
					return;
				}
				Kate->AddMovementInput(Stage.RunHeading, 1.f);
			};
			S.Events.Add({1.0, [](FStage& Stage, UWorld& World)
				{
					if (AHawkeyeCharacter* Kate = Stage.Kate.Get())
					{
						// Twelve metres back along the run, a cut like a fast-travel or a checkpoint.
						Kate->TeleportTo(Kate->GetActorLocation() - Stage.RunHeading * 1200.f + FVector(0.f, 0.f, 3.f),
							Kate->GetActorRotation(), false, true);
						Kate->NotifyTeleported();
					}
				}});
			Out.Add(S);
		}
		return Out;
	}

	/** Lays the frames side by side, each cropped to the middle Crop square and halved. */
	static bool Stitch(const TArray<FString>& Frames, const FString& Out, FString& Why)
	{
		TArray<FImage> Tiles;
		for (const FString& File : Frames)
		{
			FImage Shot;
			if (!FImageUtils::LoadImage(*File, Shot))
			{
				Why = TEXT("could not read ") + File;
				return false;
			}
			Shot.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			const int32 Size = FMath::Min3(Crop, Shot.SizeX, Shot.SizeY);
			const int32 X0 = (Shot.SizeX - Size) / 2;
			const int32 Y0 = (Shot.SizeY - Size) / 2;
			TArray<FColor> Square;
			Square.SetNumUninitialized(Size * Size);
			const FColor* Source = reinterpret_cast<const FColor*>(Shot.RawData.GetData());
			for (int32 Y = 0; Y < Size; ++Y)
			{
				FMemory::Memcpy(&Square[Y * Size], &Source[(Y0 + Y) * Shot.SizeX + X0], Size * sizeof(FColor));
			}
			TArray<FColor> Small;
			Small.SetNumUninitialized(Tile * Tile);
			FImageUtils::ImageResize(Size, Size, Square, Tile, Tile, Small, false, true);
			FImage& Piece = Tiles.Emplace_GetRef(Tile, Tile, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			FMemory::Memcpy(Piece.RawData.GetData(), Small.GetData(), Small.Num() * sizeof(FColor));
		}
		if (Tiles.Num() == 0)
		{
			Why = TEXT("no frames");
			return false;
		}
		FImage Strip(Tile * Tiles.Num(), Tile, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FColor* Dest = reinterpret_cast<FColor*>(Strip.RawData.GetData());
		for (int32 T = 0; T < Tiles.Num(); ++T)
		{
			const FColor* Source = reinterpret_cast<const FColor*>(Tiles[T].RawData.GetData());
			for (int32 Y = 0; Y < Tile; ++Y)
			{
				FMemory::Memcpy(&Dest[Y * Strip.SizeX + T * Tile], &Source[Y * Tile], Tile * sizeof(FColor));
				Dest[Y * Strip.SizeX + T * Tile] = FColor(20, 20, 20);
			}
		}
		return FImageUtils::SaveImageByExtension(*Out, Strip);
	}

	/** One scenario in one mode, driven a frame at a time by the automation latent command that holds it. */
	class FDirector
	{
	public:
		FDirector(FAutomationTestBase* InTest, TSharedRef<FStage> InStage, const FScenario& InScenario, bool bInPoseOn)
			: Test(InTest), Stage(InStage), Scenario(InScenario), bPoseOn(bInPoseOn)
		{
		}

		/** True when the scenario has finished. */
		bool Tick()
		{
			UWorld* World = FindWorld();
			AHawkeyeCharacter* Kate = Stage->Kate.Get();
			if (!World || !Kate)
			{
				return true;
			}
			DrawRope(*World, *Kate);
			switch (Phase)
			{
				case EPhase::Setup:
					SetPoseEnabled(bPoseOn);
					Scenario.Start(*Stage, *World);
					if (Scenario.bPlayerCamera)
					{
						// Her own camera, behind her along the line.
						if (APlayerController* PC = World->GetFirstPlayerController())
						{
							PC->SetViewTarget(Kate);
							PC->SetControlRotation(FRotator(-12.f, Kate->GetActorRotation().Yaw, 0.f));
						}
					}
					StartedAt = World->GetTimeSeconds();
					bTouchedDown = false;
					Phase = EPhase::Running;
					break;
				case EPhase::Running:
					return Running(*World, *Kate);
				case EPhase::Freeze:
					UGameplayStatics::SetGlobalTimeDilation(World, 0.0001f);
					if (!Scenario.bPlayerCamera)
					{
						Frame(*Kate, /*bQuarter=*/false);
					}
					Since = FPlatformTime::Seconds();
					Phase = EPhase::SettleSide;
					break;
				case EPhase::SettleSide:
					if (FPlatformTime::Seconds() - Since >= HoldSettle)
					{
						Check(*Kate);
						Shoot(Scenario.bPlayerCamera ? TEXT("player") : TEXT("side"));
						Phase = EPhase::WaitSide;
					}
					break;
				case EPhase::WaitSide:
					if (HawkeyeShots::GetOutstanding(Test) == 0 && Scenario.bPlayerCamera)
					{
						UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
						++Hold;
						Phase = Hold >= Scenario.Times.Num() ? EPhase::Cleanup : EPhase::Running;
					}
					else if (HawkeyeShots::GetOutstanding(Test) == 0)
					{
						Frame(*Kate, /*bQuarter=*/true);
						Since = FPlatformTime::Seconds();
						Phase = EPhase::SettleQuarter;
					}
					break;
				case EPhase::SettleQuarter:
					if (FPlatformTime::Seconds() - Since >= ViewSettle)
					{
						Shoot(TEXT("three_quarter"));
						Phase = EPhase::WaitQuarter;
					}
					break;
				case EPhase::WaitQuarter:
					if (HawkeyeShots::GetOutstanding(Test) == 0)
					{
						UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
						++Hold;
						Phase = Hold >= Scenario.Times.Num() ? EPhase::Cleanup : EPhase::Running;
					}
					break;
				case EPhase::Cleanup:
					UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
					StandKate(*Stage, *World);
					Phase = EPhase::Done;
					return true;
				default:
					return true;
			}
			return false;
		}

		void ReportTimeout() const
		{
			Test->AddError(FString::Printf(TEXT("%s (pose %s): stalled in phase %d at frame %d of %d."), *Scenario.Name,
				bPoseOn ? TEXT("on") : TEXT("off"), static_cast<int32>(Phase), Hold, Scenario.Times.Num()));
			if (UWorld* World = FindWorld())
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			}
		}

	private:
		bool Running(UWorld& World, AHawkeyeCharacter& Kate)
		{
			const double Elapsed = World.GetTimeSeconds() - StartedAt;
			const UHawkeyeMovementComponent* Movement = Kate.GetHawkeyeMovement();
			if (Scenario.PerTick)
			{
				Scenario.PerTick(*Stage, World, Elapsed);
			}
			for (int32 Index = 0; Index < Scenario.Events.Num(); ++Index)
			{
				if (!FiredEvents.Contains(Index) && Elapsed >= Scenario.Events[Index].Key)
				{
					FiredEvents.Add(Index);
					Scenario.Events[Index].Value(*Stage, World);
				}
			}
			if (Movement && Movement->IsZipping())
			{
				bWasZipping = true;
			}
			if (Movement && Movement->IsMovingOnGround() && !bTouchedDown && Elapsed > 0.1)
			{
				bTouchedDown = true;
				TouchedAt = Elapsed;
			}
			if (bWasZipping && Movement && !Movement->IsZipping() && ZipEndedAt < 0.0)
			{
				ZipEndedAt = Elapsed;
			}
			if (Scenario.bReferenceIsBrace && BraceAt < 0.0 && Movement && Movement->IsZipping() &&
				FTraversalPoseRules::TimeToArrival(Movement->GetZipProgress(), Movement->GetZipLength(),
					Movement->GetZipLineSpeed()) <= Kate.GetTraversalPoseSettings().BraceSeconds)
			{
				BraceAt = Elapsed;
			}
			double Reference = Scenario.Reference;
			if (Scenario.bReferenceIsZipEnd || Scenario.bReferenceIsTouchdown || Scenario.bReferenceIsBrace)
			{
				const double At = Scenario.bReferenceIsBrace
					? BraceAt
					: (Scenario.bReferenceIsZipEnd ? ZipEndedAt : (bTouchedDown ? TouchedAt : -1.0));
				if (At < 0.0)
				{
					return TimedOut(Elapsed);
				}
				Reference = At;
			}
			if (Hold < Scenario.Times.Num() && Elapsed - Reference >= Scenario.Times[Hold])
			{
				HeldAt = Elapsed - Reference;
				Phase = EPhase::Freeze;
				return false;
			}
			return TimedOut(Elapsed);
		}

		/** Ends a scenario whose moment never came (the zip blocked, she never landed) after 12 s of its own clock. */
		bool TimedOut(double Elapsed)
		{
			if (Elapsed > 12.0)
			{
				Test->AddError(FString::Printf(TEXT("%s (pose %s): the moment for frame %d never came in 12 s."),
					*Scenario.Name, bPoseOn ? TEXT("on") : TEXT("off"), Hold));
				Phase = EPhase::Cleanup;
			}
			return false;
		}

		void DrawRope(UWorld& World, const AHawkeyeCharacter& Kate) const
		{
			const UHawkeyeMovementComponent* Movement = Kate.GetHawkeyeMovement();
			const USkeletalMeshComponent* Body = Kate.GetBodyMesh();
			if (Movement && Movement->IsZipping() && Body && Body->DoesSocketExist(FName(TEXT("hand_l"))) &&
				Kate.HasTraversalRopeTarget())
			{
				DrawDebugLine(&World, Body->GetSocketLocation(FName(TEXT("hand_l"))), Kate.GetTraversalRopeTarget(),
					FColor::Yellow, false, 0.f, 0, 2.f);
			}
		}

		void Frame(const AHawkeyeCharacter& Kate, bool bQuarter) const
		{
			ACameraActor* Camera = Stage->Camera.Get();
			APlayerController* PC = Kate.GetWorld() ? Kate.GetWorld()->GetFirstPlayerController() : nullptr;
			if (!Camera || !PC)
			{
				return;
			}
			FVector Forward = Kate.GetVelocity().GetSafeNormal2D();
			if (Forward.IsNearlyZero() || Kate.GetVelocity().Size2D() < 120.f)
			{
				Forward = Kate.GetActorForwardVector().GetSafeNormal2D();
			}
			const FVector Left = -FVector::CrossProduct(FVector::UpVector, Forward);
			const FVector Centre = Kate.GetActorLocation() + FVector(0.f, 0.f, 10.f);
			const FVector Direction = bQuarter ? (Forward * 0.75f + Left * 0.66f).GetSafeNormal() : Left;
			FVector Eye = Centre + Direction * 480.f + FVector(0.f, 0.f, bQuarter ? 50.f : 15.f);
			// A pole or a post between her and the lens: bring the lens in front of it.
			FCollisionQueryParams Block(SCENE_QUERY_STAT(TraversalShotLens), false, &Kate);
			FHitResult Hit;
			if (Kate.GetWorld()->LineTraceSingleByChannel(Hit, Centre, Eye, ECC_Visibility, Block))
			{
				Eye = Centre + (Eye - Centre) * FMath::Max(0.35f, Hit.Time - 0.05f);
			}
			Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(50.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			PC->SetViewTarget(Camera);
			if (APointLight* Light = Stage->Light.Get())
			{
				Light->SetActorLocation(Eye + FVector(0.f, 0.f, 150.f));
			}
		}

		void Shoot(const TCHAR* View)
		{
			const FString Mode = bPoseOn ? TEXT("on") : TEXT("off");
			const FString Strip = FString::Printf(TEXT("%s_%s_%s"), *Scenario.Name, View, *Mode);
			const FString File =
				OutDir() / TEXT("Frames") / FString::Printf(TEXT("%s_%d_%+.2fs.png"), *Strip, Hold, HeldAt);
			Stage->Frames.FindOrAdd(Strip).Add(File);
			HawkeyeShots::Request(Test, File, /*bShowUI=*/false);
		}

		/** What the pose is doing at this hold, logged; with the pose on, the checks that mean it is working. */
		void Check(const AHawkeyeCharacter& Kate)
		{
			const UBowComponent* Bow = Kate.GetBowComponent();
			const UHawkeyeBowIKAnimInstance* Hands = Bow ? Bow->GetHandsIKInstance() : nullptr;
			if (!Hands)
			{
				if (Hold == 0)
				{
					Test->AddWarning(FString::Printf(
						TEXT("%s: no bow hands post-process running (the pose layer rides on it)."), *Scenario.Name));
				}
				return;
			}
			const FTraversalPoseOutput& Pose = Hands->GetTraversalPose();
			Test->AddInfo(FString::Printf(
				TEXT("%s pose %s frame %d (%+.2f s): phase %s weight %.2f tilt (%.1f, %.1f) arrival %s speed %.0f"),
				*Scenario.Name, bPoseOn ? TEXT("on") : TEXT("off"), Hold, HeldAt, *UEnum::GetValueAsString(Pose.Phase),
				Pose.Weight, Pose.Tilt.X, Pose.Tilt.Y, *UEnum::GetValueAsString(Pose.Arrival),
				Kate.GetVelocity().Size()));
			if (!bPoseOn)
			{
				Test->TestFalse(*FString::Printf(TEXT("%s: pose off draws nothing (frame %d)"), *Scenario.Name, Hold),
					Pose.IsActive());
				return;
			}
			if (Scenario.Name == TEXT("pull") && Hold >= 3 && Hold <= 6)
			{
				Test->TestTrue(*FString::Printf(TEXT("pull frame %d: in the pull phase (is %s)"), Hold,
								   *UEnum::GetValueAsString(Pose.Phase)),
					Pose.Phase == ETraversalPhase::Pull);
				Test->TestTrue(*FString::Printf(TEXT("pull frame %d: the body is tipped over 8 degrees (%.1f)"), Hold,
								   Pose.Tilt.Size()),
					Pose.Tilt.Size() > 8.f);
				const USkeletalMeshComponent* Body = Kate.GetBodyMesh();
				if (Body && Body->DoesSocketExist(FName(TEXT("hand_l"))))
				{
					const FVector Target = Kate.GetActorTransform().TransformPositionNoScale(Pose.LeftHand);
					const float Off =
						static_cast<float>(FVector::Dist(Body->GetSocketLocation(FName(TEXT("hand_l"))), Target));
					if (Off > 25.f)
					{
						Test->AddWarning(FString::Printf(
							TEXT("pull frame %d: her drawn hand is %.0f cm off the rope hand's target."), Hold, Off));
					}
					Test->TestTrue(*FString::Printf(
									   TEXT("pull frame %d: her drawn hand is on the rope hand's target (%.0f cm off)"),
									   Hold, Off),
						Off < 45.f);
				}
			}
			if (Scenario.Name == TEXT("launch") && Hold == 2)
			{
				Test->TestEqual(TEXT("launch: the load phase while the arrow is in the air"),
					static_cast<int32>(Pose.Phase), static_cast<int32>(ETraversalPhase::Load));
			}
			if (Scenario.Name == TEXT("brace") && Hold == 3)
			{
				Test->TestEqual(TEXT("brace: the brace phase"), static_cast<int32>(Pose.Phase),
					static_cast<int32>(ETraversalPhase::Brace));
			}
			if (Scenario.Name == TEXT("arrive_brake") && Hold == 1)
			{
				Test->TestEqual(TEXT("arrive_brake: a long climb, forward not held, is the two-step"),
					static_cast<int32>(Pose.Arrival), static_cast<int32>(EArrivalKind::Brake));
			}
			if (Scenario.Name == TEXT("arrive_slide") && Hold == 1)
			{
				Test->TestEqual(TEXT("arrive_slide: a level line with forward held is a slide out"),
					static_cast<int32>(Pose.Arrival), static_cast<int32>(EArrivalKind::SlideOut));
			}
			if (Scenario.Name == TEXT("arrive_step") && Hold == 1)
			{
				Test->TestEqual(TEXT("arrive_step: a 2 m zip is a step-down"), static_cast<int32>(Pose.Arrival),
					static_cast<int32>(EArrivalKind::StepDown));
			}
		}

		FAutomationTestBase* Test = nullptr;
		TSharedRef<FStage> Stage;
		FScenario Scenario;
		bool bPoseOn = false;
		EPhase Phase = EPhase::Setup;
		int32 Hold = 0;
		double StartedAt = 0.0;
		double Since = 0.0;
		double HeldAt = 0.0;
		double TouchedAt = -1.0;
		double ZipEndedAt = -1.0;
		double BraceAt = -1.0;
		bool bTouchedDown = false;
		bool bWasZipping = false;
		TSet<int32> FiredEvents;
	};
}

bool FHawkeyeScreenshotTraversal::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalShots;
	if (!FApp::CanEverRender())
	{
		AddWarning(
			TEXT("Skipped (no RHI): the traversal strips. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	TSharedRef<FStage> Stage = MakeShared<FStage>();
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
		[this, Stage]()
		{
			UWorld* World = FindWorld();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
			if (!Kate || !Kate->GetHawkeyeMovement())
			{
				AddError(TEXT("Traversal strips: no player."));
				return true;
			}
			for (TActorIterator<AThugCharacter> It(World); It; ++It)
			{
				if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
				{
					Brain->SetThinkingEnabled(false);
				}
			}
			HawkeyeFreezePartner(World);
			Stage->Kate = Kate;
			Stage->DefaultZipSpeed = Kate->GetHawkeyeMovement()->ZipSpeed;
			Stage->Feet =
				Kate->GetActorLocation() - FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			Stage->Along = FVector::ForwardVector;
			if (TActorIterator<APlayerStart> Start(World); Start)
			{
				Stage->Along = Start->GetActorForwardVector().GetSafeNormal2D();
				const FVector From = Start->GetActorLocation() + FVector(0.f, 0.f, 200.f);
				FCollisionQueryParams Query(SCENE_QUERY_STAT(TraversalShotGround), false, Kate);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Query))
				{
					Stage->Feet = FindLevelGround(World, Kate, Hit.ImpactPoint, Stage->Along);
				}
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (ACameraActor* Camera =
					World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params))
			{
				Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
				Stage->Camera = Camera;
			}
			APointLight* Light =
				World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform::Identity, Params);
			if (UPointLightComponent* Lamp = Light ? Light->PointLightComponent.Get() : nullptr)
			{
				Lamp->SetMobility(EComponentMobility::Movable);
				Lamp->SetIntensityUnits(ELightUnits::Lumens);
				Lamp->SetIntensity(700.f);
				Lamp->SetAttenuationRadius(1400.f);
				Lamp->SetCastShadows(false);
			}
			Stage->Light = Light;
			AddInfo(FString::Printf(TEXT("Traversal strips on %s facing %s."), *Stage->Feet.ToCompactString(),
				*Stage->Along.Rotation().ToCompactString()));
			return true;
		}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	const TArray<FScenario> Scenarios = MakeScenarios();
	// -HawkeyeTraversalOnly=pull+tail_run runs just those scenarios (joined with +); -HawkeyeTraversalPoseOnly runs only the pose-on mode.
	FString Only;
	FParse::Value(FCommandLine::Get(), TEXT("HawkeyeTraversalOnly="), Only);
	TArray<FString> OnlyNames;
	Only.ParseIntoArray(OnlyNames, TEXT("+"));
	const bool bPoseOnlyOn = FParse::Param(FCommandLine::Get(), TEXT("HawkeyeTraversalPoseOnly"));
	for (const FScenario& Scenario : Scenarios)
	{
		if (OnlyNames.Num() > 0 && !OnlyNames.Contains(Scenario.Name))
		{
			continue;
		}
		for (const bool bPoseOn : {false, true})
		{
			if (bPoseOnlyOn && !bPoseOn)
			{
				continue;
			}
			TSharedRef<FDirector> Director = MakeShared<FDirector>(this, Stage, Scenario, bPoseOn);
			ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Director]() { return Director->Tick(); },
				[Director]()
				{
					Director->ReportTimeout();
					return true;
				},
				90.f));
			ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
		[]()
		{
			SetPoseEnabled(true);
			if (UWorld* World = FindWorld())
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			}
			return true;
		}));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this, 120.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
		[this, Stage]()
		{
			for (const TPair<FString, TArray<FString>>& Strip : Stage->Frames)
			{
				const FString Out = OutDir() / (Strip.Key + TEXT(".png"));
				FString Why;
				if (Stitch(Strip.Value, Out, Why))
				{
					AddInfo(FString::Printf(TEXT("Wrote %s (%d frames)."), *Out, Strip.Value.Num()));
				}
				else
				{
					AddError(FString::Printf(TEXT("%s: %s"), *Out, *Why));
				}
			}
			return true;
		}));
	return true;
}

#endif
