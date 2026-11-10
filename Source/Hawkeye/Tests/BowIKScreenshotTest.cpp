#include "Tests/BowIKScreenshots.h"

#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/BowIKAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/PointLight.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/AutomationCommon.h"
#include "Tests/PartnerScreenshots.h"
#include "UnrealClient.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/CombatAnimPlayback.h"
#include "Kismet/GameplayStatics.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The bow hands, close up, from a camera 2.6 m out in front and to the left of the archer so the bow
 * arm, the string hand and the face are all in view:
 *
 *   bow_hold.png          Kate on the street, bHolsterWhenIdle off: the bow in her left hand at rest, arm down
 *   bow_aim_half.png      the draw held still at half: bow hand up at the aim, string hand half way back
 *   bow_aim_full.png      held at full draw: string hand at the cheek, string drawn, arrow on it
 *   bow_aim_full_view.png the same moment through her own camera, over the shoulder
 *   clint_draw.png        Clint beside her at full draw on a point 15 m up the street
 *   archer_draw_close.png a BP_Archer spawned 5 m up the street, drawn on Kate's chest
 *
 * Draws are held still by the bow's test clock so each frame shows exactly the fraction named.
 * Every shot logs each hand's distance from its IK target and the elbow heights.
 * Hawkeye.Screenshot.BowIK runs these alone (for tuning); the Kate pass runs them after its bow shots.
 */
namespace HawkeyeBowIKShots
{
	static const TCHAR* ArcherClassPath = TEXT("/Game/Blueprints/Bosses/BP_Archer.BP_Archer_C");
	static constexpr double FrozenClock = 1000.0;

	enum class EShot : uint8
	{
		Hold,
		Half,
		Full,
		FullView,
		KateDone,
		Clint,
		ClintDone,
		Archer,
		ArcherDone,
	};

	static TWeakObjectPtr<ACameraActor> ShotCamera;
	static TWeakObjectPtr<APointLight> FillLight;
	static TWeakObjectPtr<AThugCharacter> Archer;

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

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}

	static AHawkeyeCharacter* FindClint(UWorld* World)
	{
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			if (It->GetPartner())
			{
				return It->GetPartner();
			}
		}
		return nullptr;
	}

	/** The street spot the partner pass uses: 4 m from the PlayerStart toward the park, and along the street. */
	static bool FindStreet(UWorld* World, const TArray<const AActor*>& Ignore, FVector& OutGround, FVector& OutAlong)
	{
		TActorIterator<APlayerStart> Start(World);
		if (!Start)
		{
			return false;
		}
		const FVector Away = Start->GetActorForwardVector().GetSafeNormal2D();
		OutAlong = FVector::CrossProduct(FVector::UpVector, Away);
		const FVector Spot = Start->GetActorLocation() - Away * 400.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BowIKShotGround), false);
		Params.AddIgnoredActors(Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(Spot.X, Spot.Y, Spot.Z + 3000.f), FVector(Spot.X, Spot.Y, Spot.Z - 3000.f),
				ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return true;
	}

	static void Stand(ACharacter* Character, const FVector& Ground, float Yaw)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Character->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}

	/** The close camera: out in front of Subject and to its left, looking at its chest. */
	static void FrameFrontLeft(UWorld* World, APlayerController* PC, const ACharacter* Subject)
	{
		ACameraActor* Camera = ShotCamera.Get();
		if (!Camera)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
			ShotCamera = Camera;
		}
		if (!Camera)
		{
			return;
		}
		const FRotator Facing(0.f, Subject->GetActorRotation().Yaw, 0.f);
		const FVector Chest = Subject->GetActorLocation() + FVector(0.f, 0.f, 45.f);
		const FVector Eye = Chest + Facing.RotateVector(FVector(190.f, -180.f, 15.f));
		Camera->SetActorLocationAndRotation(Eye, (Chest - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(55.f);
		// The camera jumps between shots; with time stopped motion blur would smear the whole frame.
		Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
		Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);

		// A soft light beside the lens so hands and fingers read at night; the close shots are for
		// judging the pose, not the lighting.
		APointLight* Light = FillLight.Get();
		if (!Light)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform::Identity, Params);
			FillLight = Light;
			if (UPointLightComponent* Lamp = Light ? Light->PointLightComponent.Get() : nullptr)
			{
				Lamp->SetMobility(EComponentMobility::Movable);
				Lamp->SetIntensityUnits(ELightUnits::Lumens);
				Lamp->SetIntensity(350.f);
				Lamp->SetAttenuationRadius(700.f);
				Lamp->SetCastShadows(false);
			}
		}
		if (Light)
		{
			Light->SetActorLocation(Eye + FVector(0.f, 0.f, 60.f));
		}
	}

	/** Turns an AI-held character to face Point and keeps him there (his brain is not thinking). */
	static void FaceWhileDrawing(ACharacter* Who, const FVector& Point)
	{
		if (AAIController* Brain = Who ? Cast<AAIController>(Who->GetController()) : nullptr)
		{
			Brain->SetFocalPoint(Point);
			Brain->SetControlRotation((Point - Who->GetActorLocation()).GetSafeNormal2D().Rotation());
		}
	}

	/** Holds Bow at Fraction of its draw on the test clock, starting the draw if it is not drawing. */
	static void HoldDraw(UBowComponent* Bow, float Fraction)
	{
		const UBowDefinition* Definition = Bow ? Bow->GetBow() : nullptr;
		if (!Definition)
		{
			return;
		}
		if (!Bow->IsDrawing())
		{
			Bow->SetTestTimeSeconds(FrozenClock);
			Bow->StartDraw();
		}
		Bow->SetTestTimeSeconds(FrozenClock + Definition->FullDrawSeconds * Fraction);
	}

	static void LetDown(UBowComponent* Bow)
	{
		if (Bow)
		{
			Bow->CancelDraw();
			Bow->ClearAimOverride();
			Bow->ClearTestTime();
		}
	}

	/** Where each hand is against its target, the elbows, the bow and the string: the numbers to tune from. */
	static void Report(FAutomationTestBase* Test, const ACharacter* Who, const TCHAR* Label)
	{
		const UBowComponent* Bow = Who ? Who->FindComponentByClass<UBowComponent>() : nullptr;
		const USkeletalMeshComponent* Body = Who ? Who->GetMesh() : nullptr;
		const UHawkeyeBowIKAnimInstance* Hands = Bow ? Bow->GetHandsIKInstance() : nullptr;
		if (!Bow || !Body)
		{
			Test->AddWarning(FString::Printf(TEXT("%s: no bow or no body."), Label));
			return;
		}
		if (!Hands)
		{
			Test->AddWarning(FString::Printf(TEXT("%s: %s has no bow hands post-process running (HandsIKClass %s, post-process %s)."),
				Label, *GetNameSafe(Who), *GetNameSafe(Bow->HandsIKClass.Get()), *GetNameSafe(Body->GetPostProcessInstance())));
			return;
		}
		const FTransform Component = Body->GetComponentTransform();
		const FTransform Actor = Who->GetActorTransform();
		auto Local = [&Actor](const FVector& World) { return Actor.InverseTransformPosition(World).ToCompactString(); };
		const FVector LeftHand = Body->GetSocketLocation(TEXT("hand_l"));
		const FVector RightHand = Body->GetSocketLocation(TEXT("hand_r"));
		const UStaticMeshComponent* BowMesh = Bow->GetBowMeshComponent();
		Test->AddInfo(FString::Printf(
			TEXT("%s: %s draw %.2f, alphas bow %.2f draw %.2f, spine %.0f; hand_l %.1f cm off its target, hand_r %.1f cm off; ")
			TEXT("actor frame: hand_l %s, hand_r %s, elbow_l %s, elbow_r %s, head %s, bow %s (rot %s, top limb %s), arrow tip %s"),
			Label, *GetNameSafe(Who), Bow->GetDrawFraction(), Hands->GetBowAlpha(), Hands->GetDrawAlpha(), Hands->SpineTwist.Yaw,
			FVector::Dist(LeftHand, Component.TransformPosition(Hands->LeftHandTarget)),
			FVector::Dist(RightHand, Component.TransformPosition(Hands->RightHandTarget)),
			*Local(LeftHand), *Local(RightHand), *Local(Body->GetSocketLocation(TEXT("lowerarm_l"))),
			*Local(Body->GetSocketLocation(TEXT("lowerarm_r"))), *Local(Body->GetSocketLocation(TEXT("head"))),
			BowMesh ? *Local(BowMesh->GetComponentLocation()) : TEXT("-"),
			BowMesh ? *Actor.InverseTransformRotation(BowMesh->GetComponentQuat()).Rotator().ToCompactString() : TEXT("-"),
			BowMesh ? *Local(BowMesh->GetComponentTransform().TransformPosition(Bow->StringTip)) : TEXT("-"),
			*Local(Bow->GetNockedArrowTip())));
	}

	static AThugCharacter* SpawnArcher(UWorld* World, const FVector& Ground, float Yaw)
	{
		UClass* ArcherClass = LoadClass<AThugCharacter>(nullptr, ArcherClassPath);
		if (!ArcherClass)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const float HalfHeight = GetDefault<AThugCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		AThugCharacter* Spawned = World->SpawnActor<AThugCharacter>(ArcherClass, Ground + FVector(0.f, 0.f, HalfHeight + 2.f),
			FRotator(0.f, Yaw, 0.f), Params);
		if (Spawned)
		{
			// A model, not a fight: his brain goes so he stands and holds the draw he is given.
			if (AController* Brain = Spawned->GetController())
			{
				Brain->UnPossess();
				Brain->Destroy();
			}
		}
		return Spawned;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeBowIKShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeBowIKShot::Update()
{
	using namespace HawkeyeBowIKShots;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
	AHawkeyeCharacter* Clint = World ? FindClint(World) : nullptr;
	if (!Bow)
	{
		Test->AddError(TEXT("bow hands shots: no Kate with a bow component."));
		return true;
	}

	FVector Ground, Along;
	const bool bStreet = FindStreet(World, { Kate, Clint }, Ground, Along);
	const float Yaw = Along.Rotation().Yaw;
	switch (static_cast<EShot>(Shot))
	{
	case EShot::Hold:
		if (!bStreet || !Bow->GetBow())
		{
			Test->AddWarning(TEXT("bow_hold.png: no street spot or no bow on Kate."));
			break;
		}
		Kate->StopAim();
		Stand(Kate, Ground, Yaw);
		PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		Bow->bHolsterWhenIdle = false;
		if (Clint)
		{
			// Out of the close camera's frame.
			Stand(Clint, Ground - Along * 1500.f, Yaw);
		}
		FrameFrontLeft(World, PC, Kate);
		break;

	case EShot::Half:
		Report(Test, Kate, TEXT("bow_hold.png"));
		PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		HoldDraw(Bow, 0.5f);
		break;

	case EShot::Full:
		Report(Test, Kate, TEXT("bow_aim_half.png"));
		HoldDraw(Bow, 1.f);
		break;

	case EShot::FullView:
		Report(Test, Kate, TEXT("bow_aim_full.png"));
		PC->SetViewTarget(Kate);
		break;

	case EShot::KateDone:
		Report(Test, Kate, TEXT("bow_aim_full_view.png"));
		LetDown(Bow);
		Bow->bHolsterWhenIdle = true;
		Kate->StopAim();
		break;

	case EShot::Clint:
	{
		UBowComponent* ClintBow = Clint ? Clint->GetBowComponent() : nullptr;
		if (!bStreet || !ClintBow || !ClintBow->GetBow())
		{
			Test->AddWarning(TEXT("clint_draw.png: no partner Clint with a bow, or no street spot."));
			break;
		}
		Stand(Clint, Ground + Along * 300.f, Yaw);
		// Kate behind the camera, out of the way.
		Stand(Kate, Ground - Along * 400.f, Yaw);
		const FVector Aim = Clint->GetActorLocation() + Along * 1500.f + FVector(0.f, 0.f, 40.f);
		FaceWhileDrawing(Clint, Aim);
		ClintBow->SetAimOverride(Aim);
		HoldDraw(ClintBow, 1.f);
		FrameFrontLeft(World, PC, Clint);
		break;
	}

	case EShot::ClintDone:
		Report(Test, Clint, TEXT("clint_draw.png"));
		LetDown(Clint ? Clint->GetBowComponent() : nullptr);
		if (AAIController* Brain = Clint ? Cast<AAIController>(Clint->GetController()) : nullptr)
		{
			Brain->ClearFocus(EAIFocusPriority::Gameplay);
		}
		break;

	case EShot::Archer:
	{
		if (!bStreet)
		{
			Test->AddWarning(TEXT("archer_draw_close.png: no street spot."));
			break;
		}
		Stand(Kate, Ground, Yaw);
		AThugCharacter* Spawned = SpawnArcher(World, Ground + Along * 500.f, Yaw + 180.f);
		UBowComponent* ArcherBow = Spawned ? Spawned->GetBowComponent() : nullptr;
		if (!ArcherBow || !ArcherBow->GetBow())
		{
			Test->AddWarning(TEXT("archer_draw_close.png: could not spawn BP_Archer with his bow."));
			break;
		}
		Archer = Spawned;
		ArcherBow->SetAimOverride(Kate->GetActorLocation() + FVector(0.f, 0.f, 30.f));
		HoldDraw(ArcherBow, 1.f);
		FrameFrontLeft(World, PC, Spawned);
		break;
	}

	case EShot::ArcherDone:
		if (AThugCharacter* Spawned = Archer.Get())
		{
			Report(Test, Spawned, TEXT("archer_draw_close.png"));
			LetDown(Spawned->GetBowComponent());
			Spawned->Destroy();
		}
		Archer.Reset();
		if (ACameraActor* Camera = ShotCamera.Get())
		{
			Camera->Destroy();
		}
		ShotCamera.Reset();
		if (APointLight* Light = FillLight.Get())
		{
			Light->Destroy();
		}
		FillLight.Reset();
		PC->SetViewTarget(Kate);
		break;
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeBowIKTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeBowIKTakeShot::Update()
{
	const FString FullPath = HawkeyeBowIKShots::ShotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

void HawkeyeAddBowIKShots(FAutomationTestBase* Test)
{
	using EShot = HawkeyeBowIKShots::EShot;
	auto Shot = [Test](EShot Which, float Wait)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeBowIKShot(Test, static_cast<uint8>(Which)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Wait));
	};
	auto Take = [Test](const TCHAR* File)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeBowIKTakeShot(Test, FString(File)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	};

	Shot(EShot::Hold, 1.5f);
	Take(TEXT("bow_hold.png"));
	Shot(EShot::Half, 1.f);
	Take(TEXT("bow_aim_half.png"));
	Shot(EShot::Full, 0.8f);
	Take(TEXT("bow_aim_full.png"));
	Shot(EShot::FullView, 1.2f);
	Take(TEXT("bow_aim_full_view.png"));
	Shot(EShot::KateDone, 0.5f);
	Shot(EShot::Clint, 1.5f);
	Take(TEXT("clint_draw.png"));
	Shot(EShot::ClintDone, 0.5f);
	Shot(EShot::Archer, 1.5f);
	Take(TEXT("archer_draw_close.png"));
	Shot(EShot::ArcherDone, 0.5f);
}

/** The bow hands shots on their own, for tuning the targets without the whole Kate pass. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotBowIK, "Hawkeye.Screenshot.BowIK",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

DEFINE_LATENT_AUTOMATION_COMMAND(FHawkeyeBowIKFreeze);

bool FHawkeyeBowIKFreeze::Update()
{
	UWorld* World = HawkeyeBowIKShots::FindWorld();
	if (!World)
	{
		return true;
	}
	for (TActorIterator<AThugAIController> It(World); It; ++It)
	{
		It->SetThinkingEnabled(false);
	}
	HawkeyeFreezePartner(World);
	return true;
}

bool FHawkeyeScreenshotBowIK::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the bow hands screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeBowIKFreeze());
	HawkeyeAddBowIKShots(this);
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}


/**
 * The bow in motion (Hawkeye.Screenshot.BowDraw, Saved/Screenshots/BowDraw/): the stills above hold the
 * draw on the bow's test clock, so they cannot show what a real draw looks like while it plays. This one
 * draws on the world clock and stops time (global dilation 0.0001) at each moment named, captures, and
 * lets time go on:
 *
 *   draw_0.10 .. draw_1.00  0.1, 0.3, 0.6 and 1.0 s into a draw
 *   aim_up_30, aim_down_30  held at full draw, the camera pitched 30 degrees up, then down
 *   release_0.10            0.1 s after the release
 *   redraw_0.30             a new draw started 0.25 s after the release, 0.3 s into it
 *   strafe_0.40, _0.80      walking right while held at full draw, the camera riding along
 *   settle_0.60             0.6 s after the second release, as the bow goes away
 *
 * Each moment is two files: <name>.png from a camera in front of her and to her left (the pose), and
 * <name>_view.png through her own camera over the shoulder (what the player sees, with the reticle).
 * Every frame logs the bow clip, its slot weight, the IK alphas, each hand against its target and the
 * nocked arrow's direction against the launch direction (the arrow must look where it flies). Motion
 * plays at 0.25x between the moments so each stop lands within a few milliseconds of its time.
 */
namespace HawkeyeBowDrawShots
{
	using namespace HawkeyeBowIKShots;

	static constexpr float Crawl = 0.25f;
	static constexpr float Stopped = 0.0001f;

	enum class EAction : uint8
	{
		Setup,
		StartDraw,
		Wait,
		PitchUp,
		PitchDown,
		PitchLevel,
		Release,
		StrafeStart,
		StrafeStop,
		Done,
	};

	struct FStep
	{
		EAction Action = EAction::Wait;
		/** Game seconds after the previous step before this one runs. */
		float After = 0.f;
		/** A capture taken (time stopped) once this step has run, or null. */
		const TCHAR* Shot = nullptr;
	};

	static FString DrawShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("BowDraw") / FileName);
	}

	static float DegreesBetween(const FVector& A, const FVector& B)
	{
		const FVector NA = A.GetSafeNormal();
		const FVector NB = B.GetSafeNormal();
		return (NA.IsNearlyZero() || NB.IsNearlyZero()) ? -1.f
			: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(NA, NB), -1.f, 1.f)));
	}

	/** One line per frame: the clip and its weight, the IK, and the nocked arrow against where it would fly. */
	static void ReportMotion(FAutomationTestBase* Test, const AHawkeyeCharacter* Kate, const FString& Label, double SinceDraw)
	{
		const UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
		const USkeletalMeshComponent* Body = Kate ? Kate->GetMesh() : nullptr;
		const UHawkeyeBowIKAnimInstance* Hands = Bow ? Bow->GetHandsIKInstance() : nullptr;
		if (!Bow || !Body || !Hands)
		{
			Test->AddWarning(FString::Printf(TEXT("%s: no bow, body or bow hands post-process."), *Label));
			return;
		}
		const UAnimMontage* Clip = Bow->GetBowClip();
		const UAnimInstance* Post = Body->GetPostProcessInstance();
		const float SlotWeight = Post ? Post->GetSlotMontageGlobalWeight(HawkeyeCombatAnim::UpperBodySlot) : 0.f;
		const float Position = (Post && Clip) ? Post->Montage_GetPosition(Clip) : -1.f;
		const FVector Tip = Bow->GetNockedArrowTip();
		const FVector Nock = Body->DoesSocketExist(Bow->HandsIK.StringHandSocket) ? Body->GetSocketLocation(Bow->HandsIK.StringHandSocket)
			: Body->GetSocketLocation(TEXT("hand_r"));
		const FVector Launch = Bow->ComputeAimPoint() - Bow->GetArrowSpawnLocation();
		const FVector ArrowDir = Tip - Nock;
		const FTransform Component = Body->GetComponentTransform();
		const FRotator Control = Kate->GetControlRotation();
		Test->AddInfo(FString::Printf(
			TEXT("%s: %.2f s since the draw began; drawing %d draw %.2f; clip %s (%s) at %.2f s, UpperBody weight %.2f; ")
			TEXT("alphas bow %.2f draw %.2f clip %.2f left %.2f right %.2f; spine %.1f; hand_l %.1f cm off its target, hand_r %.1f; ")
			TEXT("arrow %.1f deg off the launch (arrow pitch %.1f, launch pitch %.1f, control pitch %.1f); ")
			TEXT("string hand %.1f cm off the arrow line, correction %.2f; aim offset %.2f at yaw %.1f pitch %.1f; speed %.0f"),
			*Label, SinceDraw, Bow->IsDrawing() ? 1 : 0, Bow->GetDrawFraction(), *GetNameSafe(Clip),
			*UEnum::GetValueAsString(Bow->GetBowClipRole()), Position, SlotWeight, Hands->GetBowAlpha(), Hands->GetDrawAlpha(),
			Hands->GetClipAlpha(), Hands->LeftArmAlpha, Hands->RightArmAlpha, Hands->SpineTwist.Yaw,
			FVector::Dist(Body->GetSocketLocation(TEXT("hand_l")), Component.TransformPosition(Hands->LeftHandTarget)),
			FVector::Dist(Body->GetSocketLocation(TEXT("hand_r")), Component.TransformPosition(Hands->RightHandTarget)),
			Bow->IsDrawing() ? DegreesBetween(ArrowDir, Launch) : -1.f, ArrowDir.Rotation().Pitch, Launch.Rotation().Pitch,
			FRotator::NormalizeAxis(Control.Pitch), Hands->GetStringOffLine(), Hands->GetStringCorrectionAlpha(),
			Hands->AimOffsetAlpha, Hands->AimOffsetYaw, Hands->AimOffsetPitch, Kate->GetVelocity().Size2D()));
		Report(Test, Kate, *Label);
	}

	static const TArray<FStep>& Steps()
	{
		static const TArray<FStep> All = {
			{ EAction::Setup, 0.f, nullptr },
			{ EAction::Wait, 1.5f, nullptr },
			{ EAction::StartDraw, 0.f, nullptr },
			{ EAction::Wait, 0.1f, TEXT("draw_0.10.png") },
			{ EAction::Wait, 0.2f, TEXT("draw_0.30.png") },
			{ EAction::Wait, 0.3f, TEXT("draw_0.60.png") },
			{ EAction::Wait, 0.4f, TEXT("draw_1.00.png") },
			{ EAction::PitchUp, 0.3f, nullptr },
			{ EAction::Wait, 0.5f, TEXT("aim_up_30.png") },
			{ EAction::PitchDown, 0.f, nullptr },
			{ EAction::Wait, 0.5f, TEXT("aim_down_30.png") },
			{ EAction::PitchLevel, 0.f, nullptr },
			{ EAction::Release, 0.8f, nullptr },
			{ EAction::Wait, 0.1f, TEXT("release_0.10.png") },
			{ EAction::StartDraw, 0.15f, nullptr },
			{ EAction::Wait, 0.3f, TEXT("redraw_0.30.png") },
			{ EAction::StrafeStart, 0.7f, nullptr },
			{ EAction::Wait, 0.4f, TEXT("strafe_0.40.png") },
			{ EAction::Wait, 0.4f, TEXT("strafe_0.80.png") },
			{ EAction::StrafeStop, 0.f, nullptr },
			{ EAction::Release, 0.2f, nullptr },
			{ EAction::Wait, 0.6f, TEXT("settle_0.60.png") },
			{ EAction::Done, 0.3f, nullptr },
		};
		return All;
	}
}

/** Runs HawkeyeBowDrawShots::Steps on the world clock, stopping time for each capture. */
class FHawkeyeBowDrawSequence : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeBowDrawSequence(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeBowDrawShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		UBowComponent* Bow = Kate ? Kate->GetBowComponent() : nullptr;
		if (!Bow || !Bow->GetBow())
		{
			Test->AddError(TEXT("bow draw shots: no Kate with a bow."));
			return true;
		}
		if (bWaitingForShot)
		{
			if (HawkeyeShots::GetOutstanding(Test) > 0)
			{
				return false;
			}
			if (!bTookView)
			{
				// Time is stopped: the same pose again, through her own camera.
				bTookView = true;
				PC->SetViewTarget(Kate);
				HawkeyeShots::Request(Test, DrawShotPath(PendingLabel.Replace(TEXT(".png"), TEXT("_view.png"))), /*bShowUI=*/true);
				return false;
			}
			bWaitingForShot = false;
			FrameFrontLeft(World, PC, Kate);
			UGameplayStatics::SetGlobalTimeDilation(World, Crawl);
			StepStart = World->GetTimeSeconds();
			++Index;
		}
		if (bStrafing)
		{
			// To her right, across the street's length she faces along.
			Kate->AddMovementInput(FVector::CrossProduct(FVector::UpVector, Along).GetSafeNormal(), 1.f);
			FrameFrontLeft(World, PC, Kate);
		}
		const TArray<FStep>& All = Steps();
		if (!All.IsValidIndex(Index))
		{
			return true;
		}
		const FStep& Step = All[Index];
		if (StepStart < 0.0)
		{
			StepStart = World->GetTimeSeconds();
		}
		if (World->GetTimeSeconds() - StepStart < Step.After)
		{
			return false;
		}
		Run(Step.Action, World, PC, Kate, Bow);
		if (Step.Shot)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, Stopped);
			PendingLabel = FString(Step.Shot);
			ReportMotion(Test, Kate, PendingLabel, DrawStart >= 0.0 ? World->GetTimeSeconds() - DrawStart : -1.0);
			// Drawing puts the view back on her camera; the pose shot is from the close one.
			FrameFrontLeft(World, PC, Kate);
			HawkeyeShots::Request(Test, DrawShotPath(PendingLabel), /*bShowUI=*/true);
			bWaitingForShot = true;
			bTookView = false;
			return false;
		}
		StepStart = World->GetTimeSeconds();
		++Index;
		return !All.IsValidIndex(Index);
	}

private:
	void Run(HawkeyeBowDrawShots::EAction Action, UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate, UBowComponent* Bow)
	{
		using namespace HawkeyeBowDrawShots;
		switch (Action)
		{
		case EAction::Setup:
		{
			AHawkeyeCharacter* Clint = FindClint(World);
			FVector Ground;
			if (!FindStreet(World, { Kate, Clint }, Ground, Along))
			{
				Test->AddError(TEXT("bow draw shots: no street spot."));
				return;
			}
			Yaw = Along.Rotation().Yaw;
			LetDown(Bow);
			Kate->StopAim();
			Stand(Kate, Ground, Yaw);
			PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
			if (Clint)
			{
				Stand(Clint, Ground - Along * 1500.f, Yaw);
			}
			FrameFrontLeft(World, PC, Kate);
			UGameplayStatics::SetGlobalTimeDilation(World, Crawl);
			break;
		}
		case EAction::StartDraw:
			Bow->ClearTestTime();
			if (!Bow->StartDraw())
			{
				Test->AddError(TEXT("bow draw shots: the draw did not start."));
			}
			DrawStart = World->GetTimeSeconds();
			break;
		case EAction::PitchUp:
			PC->SetControlRotation(FRotator(30.f, Yaw, 0.f));
			break;
		case EAction::PitchDown:
			PC->SetControlRotation(FRotator(-30.f, Yaw, 0.f));
			break;
		case EAction::PitchLevel:
			PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
			break;
		case EAction::Release:
			if (!Bow->ReleaseDraw())
			{
				Test->AddWarning(TEXT("bow draw shots: the release loosed nothing."));
			}
			DrawStart = -1.0;
			break;
		case EAction::StrafeStart:
			bStrafing = true;
			break;
		case EAction::StrafeStop:
			bStrafing = false;
			break;
		case EAction::Done:
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			LetDown(Bow);
			Kate->StopAim();
			if (ACameraActor* Camera = ShotCamera.Get())
			{
				Camera->Destroy();
			}
			ShotCamera.Reset();
			if (APointLight* Light = FillLight.Get())
			{
				Light->Destroy();
			}
			FillLight.Reset();
			PC->SetViewTarget(Kate);
			break;
		default:
			break;
		}
	}

	FAutomationTestBase* Test = nullptr;
	int32 Index = 0;
	double StepStart = -1.0;
	double DrawStart = -1.0;
	bool bWaitingForShot = false;
	bool bTookView = false;
	bool bStrafing = false;
	FString PendingLabel;
	FVector Along = FVector::ForwardVector;
	float Yaw = 0.f;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotBowDraw, "Hawkeye.Screenshot.BowDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotBowDraw::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the bow draw screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeBowIKFreeze());
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeBowDrawSequence(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
