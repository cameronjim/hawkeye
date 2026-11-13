#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/TakedownComponent.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Strips of the combat clips, side on at human scale, for reading by eye (claude-docs/animation.md, "Authored clips"):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -ExecCmds="Automation RunTests Hawkeye.Screenshot.Clips; Quit"
 *
 * On the district street by the PlayerStart (level ground), AI off. Each clip is played on its wearer (a BP_Thug, or Kate
 * herself), stopped at fixed times (the montage set to the time and paused) and shot from the wearer's left; the frames
 * are cropped round the body and laid side by side into Saved/Screenshots/Clips/<clip>.png, with the single frames in
 * Saved/Screenshots/Clips/Frames/. The clips the game played before (the Mixamo and Paragon reactions, Paragon's bow) are
 * shot the same way for comparison (<clip>_before.png). Then two real get-ups: a heavy knocking a thug down through his
 * knockdown clip, and a blast throwing him in the ragdoll; each is stopped (the world's time held) at fixed times after
 * the get-up starts (getup_from_clip.png, getup_from_ragdoll.png), and each must end with him on his feet, the AI free.
 * A clip that is not imported on this machine is skipped with a warning.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotClips, "Hawkeye.Screenshot.Clips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeClipShots
{
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");
	static const TCHAR* ThugClips = TEXT("/Game/Mannequin/Animations/Combat/");
	static const TCHAR* KateClips = TEXT("/Game/Characters/UEFN_Mannequin/Animations/Combat/");
	static constexpr int32 Crop = 720;
	static constexpr int32 Tile = 360;

	struct FClip
	{
		FString Name;
		bool bKate = false;
		FString Montage;
		TArray<float> Times;
		/** Where the body is in the frame: how far the lens's aim is moved back along his facing (cm; a lying body). */
		float Back = 0.f;
	};

	struct FState
	{
		TWeakObjectPtr<AHawkeyeCharacter> Kate;
		TWeakObjectPtr<AThugCharacter> Thug;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<APointLight> Light;
		FVector Feet = FVector::ZeroVector;
		FVector Along = FVector::ForwardVector;
		/** Per strip, its frames in order. */
		TMap<FString, TArray<FString>> Frames;
		double GetUpStartedAt = -1.0;
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
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Clips"));
	}

	static TArray<float> Spread(float Seconds, int32 Count)
	{
		TArray<float> Out;
		for (int32 I = 0; I < Count; ++I)
		{
			Out.Add(Seconds * I / (Count - 1));
		}
		return Out;
	}

	/** The nearest ground to Start (rings of 50 cm, up to 8 m) where every point within 150 cm is within 3 cm of its height. */
	static FVector FindLevelGround(UWorld* World, const AActor* Ignore, const FVector& Start, const FVector& Along)
	{
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ClipShotLevel), false, Ignore);
		auto Probe = [World, &Query](const FVector& XY, float FromZ, FVector& Out)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, FromZ + 200.f), FVector(XY.X, XY.Y, FromZ - 400.f),
					ECC_Visibility, Query))
			{
				return false;
			}
			Out = Hit.ImpactPoint;
			return true;
		};
		for (int32 Ring = 0; Ring <= 16; ++Ring)
		{
			for (const FVector& Direction : { -Side, Side, Along, -Along })
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
					for (const float Reach : { 40.f, 90.f, 150.f })
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

	/** The lens on the body from its left, chest high, about 4.2 m off. */
	static void Frame(FState& State, const ACharacter* Subject, float Back)
	{
		ACameraActor* Camera = State.Camera.Get();
		APlayerController* PC = Subject && Subject->GetWorld() ? Subject->GetWorld()->GetFirstPlayerController() : nullptr;
		if (!Camera || !Subject || !PC)
		{
			return;
		}
		const FVector Forward = Subject->GetActorForwardVector().GetSafeNormal2D();
		const FVector Left = -Subject->GetActorRightVector().GetSafeNormal2D();
		const FVector Ground = Subject->GetActorLocation() - FVector(0.f, 0.f, Subject->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FVector Target = Ground - Forward * Back + FVector(0.f, 0.f, 85.f);
		const FVector Eye = Target + Left * 420.f + FVector(0.f, 0.f, 10.f);
		Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(55.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
		if (APointLight* Light = State.Light.Get())
		{
			Light->SetActorLocation(Eye + FVector(0.f, 0.f, 120.f));
		}
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
			}
			// A divider and, along the top, one tick per tenth of a second is not drawn: the file names carry the times.
			for (int32 Y = 0; Y < Tile; ++Y)
			{
				Dest[Y * Strip.SizeX + T * Tile] = FColor(20, 20, 20);
			}
		}
		return FImageUtils::SaveImageByExtension(*Out, Strip);
	}

	/** Subject on stage, the other one parked out of the lens's way (hidden, high above, not falling). */
	static void PutOnStage(FState& State, ACharacter* Subject)
	{
		for (ACharacter* Who : { static_cast<ACharacter*>(State.Kate.Get()), static_cast<ACharacter*>(State.Thug.Get()) })
		{
			if (!Who)
			{
				continue;
			}
			const bool bOn = Who == Subject;
			Who->SetActorHiddenInGame(!bOn);
			if (UCharacterMovementComponent* Movement = Who->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
				if (bOn)
				{
					Movement->SetMovementMode(MOVE_Walking);
				}
				else
				{
					Movement->DisableMovement();
				}
			}
			if (!bOn)
			{
				Who->TeleportTo(State.Feet + FVector(0.f, 0.f, 3000.f) + FVector(Who == State.Thug.Get() ? 400.f : 0.f, 0.f, 0.f),
					State.Along.Rotation(), false, true);
			}
		}
	}

	/** Waits until every capture this test asked for is written. */
	static bool ShotsDone(FAutomationTestBase* Test)
	{
		return HawkeyeShots::GetOutstanding(Test) == 0;
	}

	static void AddShot(FAutomationTestBase* Test, TSharedRef<FState> State, const FString& Strip, const FString& Name)
	{
		const FString File = OutDir() / TEXT("Frames") / (Name + TEXT(".png"));
		State->Frames.FindOrAdd(Strip).Add(File);
		HawkeyeShots::Request(Test, File, /*bShowUI=*/false);
	}

	/** One clip's strip: per time, the montage set there and paused on its wearer, then the shot. */
	static void AddClip(FAutomationTestBase* Test, TSharedRef<FState> State, const FClip& Clip)
	{
		for (int32 I = 0; I < Clip.Times.Num(); ++I)
		{
			const float Time = Clip.Times[I];
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, Clip, Time, I]()
			{
				ACharacter* Subject = Clip.bKate ? static_cast<ACharacter*>(State->Kate.Get()) : State->Thug.Get();
				UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Clip.Montage);
				if (!Subject || !Montage)
				{
					if (I == 0)
					{
						Test->AddWarning(FString::Printf(TEXT("%s: %s is not on this machine; skipped."), *Clip.Name, *Clip.Montage));
					}
					return true;
				}
				PutOnStage(*State, Subject);
				Subject->TeleportTo(State->Feet + FVector(0.f, 0.f, Subject->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
					State->Along.Rotation(), false, true);
				Subject->SetActorRotation(State->Along.Rotation());
				UAnimInstance* Instance = HawkeyeCombatAnim::Play(Subject->GetMesh(), Montage);
				if (!Instance)
				{
					Test->AddError(FString::Printf(TEXT("%s: %s did not play on its wearer."), *Clip.Name, *Montage->GetName()));
					return true;
				}
				Instance->Montage_SetPosition(Montage, Time);
				Instance->Montage_Pause(Montage);
				Frame(*State, Subject, Clip.Back);
				return true;
			}));
			ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.45f));
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, Clip, Time, I]()
			{
				if (!LoadObject<UAnimMontage>(nullptr, *Clip.Montage))
				{
					return true;
				}
				AddShot(Test, State, Clip.Name, FString::Printf(TEXT("%s_%d_%.2fs"), *Clip.Name, I, Time));
				return true;
			}));
			ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Test]() { return ShotsDone(Test); }, []() { return true; }, 12.f));
		}
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State, Clip]()
		{
			ACharacter* Subject = Clip.bKate ? static_cast<ACharacter*>(State->Kate.Get()) : State->Thug.Get();
			if (UAnimInstance* Instance = Subject && Subject->GetMesh() ? HawkeyeCombatAnim::PickInstance(Subject->GetMesh(),
					LoadObject<UAnimMontage>(nullptr, *Clip.Montage)) : nullptr)
			{
				Instance->Montage_Stop(0.f);
			}
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	}

	/**
	 * A real get-up: the thug is put down (Blast: KnockdownFor at 900 cm/s, the ragdoll; else Knockdown, his knockdown clip),
	 * and once he starts getting up the world's time is held at each of Times (s after the start) for a shot.
	 */
	static void AddGetUp(FAutomationTestBase* Test, TSharedRef<FState> State, const FString& Name, bool bBlast, const TArray<float>& Times)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, bBlast]()
		{
			AThugCharacter* Thug = State->Thug.Get();
			AHawkeyeCharacter* Kate = State->Kate.Get();
			if (!Thug || !Kate)
			{
				return true;
			}
			PutOnStage(*State, Thug);
			Thug->TeleportTo(State->Feet + FVector(0.f, 0.f, Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
				State->Along.Rotation(), false, true);
			Thug->SetActorRotation(State->Along.Rotation());
			// The blow from in front of him (the throw reads Kate's place flat): Kate is parked high above that spot.
			Kate->TeleportTo(Thug->GetActorLocation() + State->Along * 150.f + FVector(0.f, 0.f, 3000.f), (-State->Along).Rotation(), false, true);
			Thug->KnockdownSeconds = 2.f;
			State->GetUpStartedAt = -1.0;
			if (bBlast)
			{
				Thug->KnockdownFor(Kate, 2.f, 900.f);
			}
			else
			{
				Thug->Knockdown(Kate);
			}
			Test->AddInfo(FString::Printf(TEXT("%s: knocked down (%s)."), bBlast ? TEXT("getup_from_ragdoll") : TEXT("getup_from_clip"),
				Thug->IsKnockdownFromClip() ? TEXT("his knockdown clip") : TEXT("the ragdoll")));
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State]()
		{
			const AThugCharacter* Thug = State->Thug.Get();
			if (!Thug || !Thug->IsGettingUp())
			{
				return false;
			}
			State->GetUpStartedAt = Thug->GetWorld()->GetTimeSeconds();
			return true;
		}, [Test, Name]()
		{
			Test->AddError(Name + TEXT(": he never started getting up."));
			return true;
		}, 10.f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, Name]()
		{
			const AThugCharacter* Thug = State->Thug.Get();
			if (Thug)
			{
				Test->AddInfo(FString::Printf(TEXT("%s: getting up by %s for %.2f s, capsule at %s facing %.0f, pelvis at %s."), *Name,
					*UEnum::GetValueAsString(Thug->GetLastGetUpRole()), Thug->GetGetUpSeconds(), *Thug->GetActorLocation().ToCompactString(),
					Thug->GetActorRotation().Yaw, *Thug->GetMesh()->GetBoneLocation(FName(TEXT("pelvis"))).ToCompactString()));
				Frame(*State, Thug, 40.f);
			}
			return true;
		}));
		for (int32 I = 0; I < Times.Num(); ++I)
		{
			const float At = Times[I];
			ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State, At]()
			{
				const AThugCharacter* Thug = State->Thug.Get();
				return !Thug || Thug->GetWorld()->GetTimeSeconds() - State->GetUpStartedAt >= At;
			}, []() { return true; }, 5.f));
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
			{
				if (UWorld* World = FindWorld())
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 0.0001f);
				}
				return true;
			}));
			ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.35f));
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, Name, I]()
			{
				const AThugCharacter* Thug = State->Thug.Get();
				const double Elapsed = Thug ? Thug->GetWorld()->GetTimeSeconds() - State->GetUpStartedAt : 0.0;
				if (Thug)
				{
					const USkeletalMeshComponent* Driver = Thug->GetMesh();
					const USkeletalMeshComponent* Seen = Thug->GetBodyMesh();
					Test->AddInfo(FString::Printf(TEXT("%s %.2f s: capsule %s, driver at %s pelvis %s, seen %s at %s pelvis %s."), *Name, Elapsed,
						*Thug->GetActorLocation().ToCompactString(), *Driver->GetComponentLocation().ToCompactString(),
						*Driver->GetBoneLocation(FName(TEXT("pelvis"))).ToCompactString(), Seen && Seen->IsVisible() ? TEXT("drawn") : TEXT("hidden"),
						Seen ? *Seen->GetComponentLocation().ToCompactString() : TEXT("-"),
						Seen ? *Seen->GetBoneLocation(FName(TEXT("pelvis"))).ToCompactString() : TEXT("-")));
				}
				AddShot(Test, State, Name, FString::Printf(TEXT("%s_%d_%.2fs"), *Name, I, Elapsed));
				return true;
			}));
			ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Test]() { return ShotsDone(Test); }, []() { return true; }, 12.f));
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
			{
				if (UWorld* World = FindWorld())
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				}
				return true;
			}));
		}
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State]()
		{
			const AThugCharacter* Thug = State->Thug.Get();
			return !Thug || !Thug->IsGettingUp();
		}, [Test, Name]()
		{
			Test->AddError(Name + TEXT(": still getting up 5 s on."));
			return true;
		}, 5.f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, Name]()
		{
			const AThugCharacter* Thug = State->Thug.Get();
			if (!Thug)
			{
				return true;
			}
			const USkeletalMeshComponent* Mesh = Thug->GetMesh();
			const float Ground = Thug->GetActorLocation().Z - Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float Pelvis = Mesh ? Mesh->GetBoneLocation(FName(TEXT("pelvis"))).Z - Ground : 0.f;
			Test->TestFalse(*(Name + TEXT(": up at the end, the AI free (not incapacitated)")), Thug->IsIncapacitated());
			Test->TestTrue(*(Name + TEXT(": he walks again")), Thug->GetCharacterMovement()->MovementMode == MOVE_Walking);
			Test->TestTrue(*FString::Printf(TEXT("%s: standing at the end (pelvis %.0f cm up)"), *Name, Pelvis), Pelvis > 70.f);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}
}

namespace HawkeyeClipShots
{
	/**
	 * The stealth takedown for real: a fresh, calm BP_Thug with his back to Kate a metre off, Kate's takedown, and the
	 * world held at each of Times after it starts, both of them in the lens from the side.
	 */
	static void AddTakedown(FAutomationTestBase* Test, TSharedRef<FState> State, const TArray<float>& Times, bool bPaired, const FString& Name)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State]()
		{
			UWorld* World = FindWorld();
			AHawkeyeCharacter* Kate = State->Kate.Get();
			UClass* Class = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
			if (!World || !Kate || !Class)
			{
				return true;
			}
			if (AThugCharacter* Old = State->Thug.Get())
			{
				Old->Destroy();
			}
			State->Thug = nullptr;
			const FTransform At(State->Along.Rotation(), State->Feet + FVector(0.f, 0.f, 100.f));
			AThugCharacter* Victim = World->SpawnActorDeferred<AThugCharacter>(Class, At, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Victim)
			{
				return true;
			}
			Victim->AutoPossessAI = EAutoPossessAI::Disabled;
			Victim->FinishSpawning(At);
			Victim->TeleportTo(State->Feet + FVector(0.f, 0.f, Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
				State->Along.Rotation(), false, true);
			State->Thug = nullptr;
			PutOnStage(*State, Kate);
			State->Thug = Victim;
			Kate->TeleportTo(Victim->GetActorLocation() - State->Along * 100.f + FVector(0.f, 0.f, 4.f), State->Along.Rotation(), false, true);
			Kate->SetActorRotation(State->Along.Rotation());
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.6f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, bPaired]()
		{
			AHawkeyeCharacter* Kate = State->Kate.Get();
			UTakedownComponent* Takedown = Kate ? Kate->GetTakedownComponent() : nullptr;
			if (!Takedown)
			{
				return true;
			}
			if (AThugCharacter* Victim = State->Thug.Get())
			{
				// Put her a metre behind him now (where he stands after settling), facing his way.
				const FVector Back = -Victim->GetActorForwardVector().GetSafeNormal2D();
				Kate->TeleportTo(Victim->GetActorLocation() + Back * 100.f + FVector(0.f, 0.f, 4.f), Victim->GetActorRotation(), false, true);
				Kate->SetActorRotation(FRotator(0.f, Victim->GetActorRotation().Yaw, 0.f));
			}
			if (!bPaired)
			{
				// Today's takedown, for comparison: no clips for it.
				Takedown->SetAnimSet(nullptr);
			}
			if (const AThugCharacter* Victim = State->Thug.Get())
			{
				Test->AddInfo(FString::Printf(TEXT("takedown: him %s (tag %d, %.0f cm), valid %d, found %s."), *Victim->GetName(),
					Victim->ActorHasTag(FName(TEXT("Thug"))) ? 1 : 0, FVector::Dist(Kate->GetActorLocation(), Victim->GetActorLocation()),
					Takedown->IsValidTakedownTarget(Victim, Kate->GetActorLocation()) ? 1 : 0, *GetNameSafe(Takedown->FindTakedownTarget())));
			}
			Test->TestTrue(TEXT("takedown: it starts"), Takedown->TryTakedown());
			Kate->ApplyCombatAnimSet();
			Test->AddInfo(FString::Printf(TEXT("takedown: %s, locked %.2f s."), Takedown->GetLastVariant() == EHawkeyeTakedownVariant::Paired
				? TEXT("paired") : TEXT("plain"), Takedown->GetLockSeconds()));
			State->GetUpStartedAt = Kate->GetWorld()->GetTimeSeconds();
			if (AThugCharacter* Victim = State->Thug.Get())
			{
				Frame(*State, Victim, 70.f);
			}
			return true;
		}));
		for (int32 I = 0; I < Times.Num(); ++I)
		{
			const float At = Times[I];
			ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([State, At]()
			{
				UWorld* World = FindWorld();
				return !World || World->GetTimeSeconds() - State->GetUpStartedAt >= At;
			}, []() { return true; }, 5.f));
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
			{
				if (UWorld* World = FindWorld())
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 0.0001f);
				}
				return true;
			}));
			ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.35f));
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State, I, Name]()
			{
				UWorld* World = FindWorld();
				const double Elapsed = World ? World->GetTimeSeconds() - State->GetUpStartedAt : 0.0;
				AddShot(Test, State, Name, FString::Printf(TEXT("%s_%d_%.2fs"), *Name, I, Elapsed));
				return true;
			}));
			ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Test]() { return ShotsDone(Test); }, []() { return true; }, 12.f));
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
			{
				if (UWorld* World = FindWorld())
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				}
				return true;
			}));
		}
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, State]()
		{
			const AHawkeyeCharacter* Kate = State->Kate.Get();
			const AThugCharacter* Victim = State->Thug.Get();
			Test->TestFalse(TEXT("takedown: her control is back at the end"), Kate && Kate->IsLockedOutByTakedown());
			Test->TestTrue(TEXT("takedown: he is down (dead, a ragdoll at rest)"), Victim && !Victim->GetHealthComponent()->IsAlive()
				&& Victim->IsRagdolling());
			return true;
		}));
	}
}

bool FHawkeyeScreenshotClips::RunTest(const FString& Parameters)
{
	using namespace HawkeyeClipShots;
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the clip strips. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	TSharedRef<FState> State = MakeShared<FState>();
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!Kate)
		{
			AddError(TEXT("Clip strips: no player."));
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
		State->Kate = Kate;
		State->Feet = Kate->GetActorLocation() - FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		if (TActorIterator<APlayerStart> Start(World); Start)
		{
			State->Along = Start->GetActorForwardVector().GetSafeNormal2D();
			const FVector From = Start->GetActorLocation() + FVector(0.f, 0.f, 200.f);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(ClipShotGround), false, Kate);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Query))
			{
				// The PlayerStart sits by the kerb: the nearest spot where everything within 1.5 m is level (a foot off the step
				// read as a stumble).
				State->Feet = FindLevelGround(World, Kate, Hit.ImpactPoint, State->Along);
			}
		}
		if (UCharacterMovementComponent* Movement = Kate->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		if (Camera)
		{
			Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
			Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
		}
		State->Camera = Camera;
		APointLight* Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform::Identity, Params);
		if (UPointLightComponent* Lamp = Light ? Light->PointLightComponent.Get() : nullptr)
		{
			Lamp->SetMobility(EComponentMobility::Movable);
			Lamp->SetIntensityUnits(ELightUnits::Lumens);
			Lamp->SetIntensity(600.f);
			Lamp->SetAttenuationRadius(1000.f);
			Lamp->SetCastShadows(false);
		}
		State->Light = Light;
		if (UClass* Class = LoadClass<AThugCharacter>(nullptr, ThugClassPath))
		{
			const FTransform At(State->Along.Rotation(), State->Feet + FVector(600.f, 0.f, 200.f));
			if (AThugCharacter* Thug = World->SpawnActorDeferred<AThugCharacter>(Class, At, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				Thug->AutoPossessAI = EAutoPossessAI::Disabled;
				Thug->FinishSpawning(At);
				State->Thug = Thug;
			}
		}
		AddInfo(FString::Printf(TEXT("Clip strips on %s facing %s."), *State->Feet.ToCompactString(), *State->Along.Rotation().ToCompactString()));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	const FString T(ThugClips);
	const FString K(KateClips);
	TArray<FClip> Clips = {
		{TEXT("thug_getup_back"), false, T + TEXT("AM_GetUp_Authored"), {0.f, 0.25f, 0.5f, 0.75f, 0.95f, 1.15f, 1.35f, 1.6f}, 40.f},
		{TEXT("thug_getup_front"), false, T + TEXT("AM_GetUpFront_Authored"), {0.f, 0.3f, 0.55f, 0.8f, 1.0f, 1.2f, 1.45f, 1.8f}, 0.f},
		{TEXT("thug_hit_front"), false, T + TEXT("AM_HitFront_Authored"), {0.f, 0.05f, 0.1f, 0.17f, 0.25f, 0.34f, 0.44f, 0.53f}, 0.f},
		{TEXT("thug_hit_left"), false, T + TEXT("AM_HitLeft_Authored"), {0.f, 0.05f, 0.1f, 0.17f, 0.25f, 0.34f, 0.44f, 0.53f}, 0.f},
		{TEXT("thug_hit_right"), false, T + TEXT("AM_HitRight_Authored"), {0.f, 0.05f, 0.1f, 0.17f, 0.25f, 0.34f, 0.44f, 0.53f}, 0.f},
		{TEXT("thug_hit_heavy"), false, T + TEXT("AM_HitHeavy_Authored"), {0.f, 0.06f, 0.13f, 0.22f, 0.32f, 0.44f, 0.58f, 0.73f}, 0.f},
		{TEXT("thug_hit_front_before"), false, T + TEXT("AM_HitFront_PunchToFace"), Spread(1.0f, 8), 0.f},
		{TEXT("thug_hit_left_before"), false, T + TEXT("AM_HitLeft_Sparrow"), Spread(0.6f, 8), 0.f},
		{TEXT("thug_knockdown_before"), false, T + TEXT("AM_Knockdown_KnockedOut"), Spread(1.8f, 8), 40.f},
		{TEXT("kate_hit_front"), true, K + TEXT("AM_HitFront_Authored"), {0.f, 0.05f, 0.1f, 0.17f, 0.25f, 0.34f, 0.44f, 0.53f}, 0.f},
		{TEXT("kate_hit_left"), true, K + TEXT("AM_HitLeft_Authored"), {0.f, 0.05f, 0.1f, 0.17f, 0.25f, 0.34f, 0.44f, 0.53f}, 0.f},
		{TEXT("kate_hit_right"), true, K + TEXT("AM_HitRight_Authored"), {0.f, 0.05f, 0.1f, 0.17f, 0.25f, 0.34f, 0.44f, 0.53f}, 0.f},
		{TEXT("kate_hit_heavy"), true, K + TEXT("AM_HitHeavy_Authored"), {0.f, 0.06f, 0.13f, 0.22f, 0.32f, 0.44f, 0.58f, 0.73f}, 0.f},
		{TEXT("kate_hit_front_before"), true, K + TEXT("AM_HitFront_Sparrow"), Spread(0.6f, 8), 0.f},
		{TEXT("kate_hit_left_before"), true, K + TEXT("AM_HitLeft_Sparrow"), Spread(0.6f, 8), 0.f},
		{TEXT("kate_bow_draw"), true, K + TEXT("AM_BowDraw_Authored"), Spread(0.35f, 6), 0.f},
		{TEXT("kate_bow_fire"), true, K + TEXT("AM_BowFire_Authored"), Spread(0.5f, 7), 0.f},
		{TEXT("kate_bow_draw_before"), true, K + TEXT("AM_BowDraw_Sparrow"), Spread(0.5f, 6), 0.f},
		{TEXT("kate_bow_fire_before"), true, K + TEXT("AM_BowFire_Sparrow"), Spread(0.43f, 7), 0.f},
	};
	const FString Only = FParse::Param(FCommandLine::Get(), TEXT("ClipShotsGetUpOnly")) ? TEXT("none") : FString();
	for (const FClip& Clip : Clips)
	{
		if (Only.IsEmpty())
		{
			AddClip(this, State, Clip);
		}
	}
	AddGetUp(this, State, TEXT("getup_from_clip"), false, {0.02f, 0.25f, 0.5f, 0.8f, 1.1f, 1.4f, 1.62f});
	AddGetUp(this, State, TEXT("getup_from_ragdoll"), true, {0.02f, 0.25f, 0.5f, 0.8f, 1.1f, 1.4f, 1.62f});
	AddTakedown(this, State, {0.0f, 0.2f, 0.45f, 0.7f, 0.95f, 1.2f, 1.45f, 1.75f}, false, TEXT("takedown_before"));
	AddTakedown(this, State, {0.0f, 0.2f, 0.45f, 0.7f, 0.95f, 1.2f, 1.45f, 1.75f}, true, TEXT("takedown_paired"));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
	{
		for (const TPair<FString, TArray<FString>>& Strip : State->Frames)
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
		if (AThugCharacter* Thug = State->Thug.Get())
		{
			Thug->Destroy();
		}
		return true;
	}));
	return true;
}

#endif
