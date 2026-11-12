#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Combat/BowComponent.h"
#include "Combat/MeleeComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "World/ThugAIController.h"
#include "World/LuckyCharacter.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The bodies (claude-docs/animation.md, "Bodies"): a street thug, the heavy, Clint and Lucky, each stood on the
 * street by the PlayerStart and shot front-on to Saved/Screenshots/Bodies/ (body_thug.png, body_heavy.png,
 * body_clint.png, body_lucky.png), then a thug thrown into a ragdoll (body_thug_ragdoll.png), a thug 0.4 s into his
 * punch's wind-up with the telegraph glow on (body_thug_punch.png), a thug the frame a hit flashes him
 * (body_thug_flash.png), Clint 0.7 s into a bow draw (body_clint_draw.png) and Lucky sitting by Kate
 * (body_lucky_kate.png).
 *
 * Fails, for a character with a body look, when the look is not drawn over a hidden driver, a bone of it is more than
 * 150 cm from the actor (exploded), its hands are not well below its shoulders while he stands (a T-pose), its toes
 * point away from where he faces (turned), its head is more than 25 cm from the driver's (scaled or turned), or, in the
 * ragdoll, its pelvis is more than 25 cm from the driver's (the look not following the physics). Lucky fails when he is
 * not 40 to 95 cm tall or his head is not toward where he faces. A clone without the
 * packs has no look: the shots are still taken and the checks are skipped with a note. Needs a real RHI:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject /Game/Maps/L_District_EastVillage -game -windowed -ResX=1280 -ResY=720
 *       -TimeOfDay=Day -unattended -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.Bodies; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotBodies, "Hawkeye.Screenshot.Bodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeBodyShots
{
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");
	static const TCHAR* HeavyClassPath = TEXT("/Game/Blueprints/AI/BP_Thug_Heavy.BP_Thug_Heavy_C");

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

	static FString ShotPath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Bodies") / (Name + TEXT(".png")));
	}

	/** Where a bone of Mesh is, or false when the mesh has no such bone. */
	static bool Bone(const USkeletalMeshComponent* Mesh, const TCHAR* Name, FVector& Out)
	{
		if (!Mesh || Mesh->GetBoneIndex(FName(Name)) == INDEX_NONE)
		{
			return false;
		}
		Out = Mesh->GetBoneLocation(FName(Name));
		return true;
	}

	/**
	 * The standing checks on a character's look (see the file comment). Driver is GetMesh(), Seen the mesh people see.
	 * Returns a one-line summary for the log.
	 */
	static FString CheckStanding(FAutomationTestBase* Test, const FString& Who, const ACharacter* Character,
		const USkeletalMeshComponent* Driver, const USkeletalMeshComponent* Seen)
	{
		if (!Seen || Seen == Driver)
		{
			Test->AddInfo(FString::Printf(TEXT("%s: no body look on this clone (the packs are not imported); the mannequin is shot."), *Who));
			return TEXT("mannequin");
		}
		Test->TestFalse(*FString::Printf(TEXT("%s: the driver is hidden"), *Who), Driver->IsVisible());
		Test->TestTrue(*FString::Printf(TEXT("%s: the look is drawn"), *Who), Seen->IsVisible());
		const FVector Centre = Character->GetActorLocation();
		float Farthest = 0.f;
		for (int32 Index = 0; Index < Seen->GetNumBones(); ++Index)
		{
			Farthest = FMath::Max(Farthest, FVector::Dist(Seen->GetBoneLocation(Seen->GetBoneName(Index)), Centre));
		}
		Test->TestTrue(*FString::Printf(TEXT("%s: every bone within 150 cm of him (farthest %.0f)"), *Who, Farthest), Farthest < 150.f);

		FVector HandL(0.f), HandR(0.f), ArmL(0.f), ArmR(0.f), Foot(0.f), Ball(0.f), Head(0.f), DriverHead(0.f);
		const bool bArms = Bone(Seen, TEXT("hand_l"), HandL) && Bone(Seen, TEXT("hand_r"), HandR) && Bone(Seen, TEXT("upperarm_l"), ArmL)
			&& Bone(Seen, TEXT("upperarm_r"), ArmR);
		const float Drop = bArms ? FMath::Min(ArmL.Z - HandL.Z, ArmR.Z - HandR.Z) : 0.f;
		Test->TestTrue(*FString::Printf(TEXT("%s: hands %.0f cm below the shoulders, not a T-pose"), *Who, Drop), bArms && Drop > 30.f);
		const bool bFeet = Bone(Seen, TEXT("foot_l"), Foot) && Bone(Seen, TEXT("ball_l"), Ball);
		const float Facing = bFeet ? FVector::DotProduct((Ball - Foot).GetSafeNormal2D(), Character->GetActorForwardVector().GetSafeNormal2D()) : -1.f;
		Test->TestTrue(*FString::Printf(TEXT("%s: toes point where he faces (%.2f)"), *Who, Facing), Facing > 0.5f);
		const bool bHeads = Bone(Seen, TEXT("head"), Head) && Bone(Driver, TEXT("head"), DriverHead);
		const float HeadOff = bHeads ? FVector::Dist(Head, DriverHead) : -1.f;
		Test->TestTrue(*FString::Printf(TEXT("%s: head %.0f cm from the driver's"), *Who, HeadOff), bHeads && HeadOff < 25.f);
		const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float Top = bHeads ? Head.Z - Feet : 0.f;
		return FString::Printf(TEXT("%s on %s: head bone %.0f cm up, hands %.0f cm below the shoulders, toes %.2f along his facing, head %.0f cm from the driver's, farthest bone %.0f cm"),
			*GetNameSafe(Seen->GetSkeletalMeshAsset()), *GetNameSafe(Driver->GetSkeletalMeshAsset()), Top, Drop, Facing, HeadOff, Farthest);
	}
}

/** One step per frame group: stand a subject, frame it, let it settle, shoot it, next. */
class FHawkeyeBodyShots : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeBodyShots(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeBodyShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!World || !PC || !Kate)
		{
			Test->AddError(TEXT("Body shots: no world, player controller or Kate."));
			return true;
		}
		if (HawkeyeShots::GetOutstanding(Test) > 0)
		{
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (Step == 0)
		{
			Setup(World, Kate);
			Step = 1;
			return false;
		}
		if (Step > NumSubjects * 2)
		{
			Cleanup();
			return true;
		}
		// Odd steps stand the subject and frame it; even steps shoot it once it has settled.
		const int32 Subject = (Step - 1) / 2;
		if (Step % 2 == 1)
		{
			Stand(World, PC, Subject);
			StepStart = Now;
			++Step;
			return false;
		}
		if (Now - StepStart < SettleSeconds(Subject))
		{
			return false;
		}
		// A moment shot (the punch, the flash, the draw) starts its action, waits its lead, then holds time on him.
		if (ActionLead(Subject) >= 0.0)
		{
			if (ActionStart < 0.0)
			{
				StartAction(Subject);
				ActionStart = Now;
				return false;
			}
			if (Now - ActionStart < ActionLead(Subject))
			{
				return false;
			}
			if (Current.IsValid())
			{
				Current->CustomTimeDilation = 0.001f;
			}
		}
		ActionStart = -1.0;
		Shoot(Subject);
		++Step;
		return false;
	}

private:
	static constexpr int32 NumSubjects = 9;
	enum ESubject : int32 { ThugShot, HeavyShot, ClintShot, LuckyShot, RagdollShot, PunchShot, FlashShot, DrawShot, LuckyKateShot };

	static double SettleSeconds(int32 Subject)
	{
		// The ragdoll needs time to fall; the others only Lumen and the exposure. The punch, the flash and the draw are
		// started at the end of their settle (Shoot), so the pose is the moment asked for.
		return Subject == RagdollShot ? 1.6 : 2.5;
	}

	/** Seconds from the action to the shot, or -1 for a subject with no action. */
	static double ActionLead(int32 Subject)
	{
		switch (Subject)
		{
		case PunchShot:
			return 0.4;   // of the fists' 0.6 s wind-up: the arm back, the telegraph on
		case FlashShot:
			return 0.02;  // the frame after the hit
		case DrawShot:
			return 0.7;
		default:
			return -1.0;
		}
	}

	void StartAction(int32 Subject)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(Current.Get());
		if (Subject == PunchShot && Thug && Thug->GetMeleeComponent())
		{
			const bool bStarted = Thug->GetMeleeComponent()->StartAttack(Thug->GetMeleeAttack(0));
			Test->TestTrue(TEXT("body_thug_punch: the swing started"), bStarted);
		}
		else if (Subject == FlashShot && Thug)
		{
			Thug->FlashHit();
		}
		else if (AHawkeyeCharacter* Clint = Cast<AHawkeyeCharacter>(Current.Get()); Subject == DrawShot && Clint && Clint->GetBowComponent())
		{
			const bool bDrawing = Clint->GetBowComponent()->StartDraw();
			Test->TestTrue(TEXT("body_clint_draw: the draw started"), bDrawing);
		}
	}

	void Setup(UWorld* World, AHawkeyeCharacter* Kate)
	{
		using namespace HawkeyeBodyShots;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(false);
			}
		}
		HawkeyeFreezePartner(World);
		for (TActorIterator<ALuckyCharacter> It(World); It; ++It)
		{
			It->SetThinkingEnabled(false);
		}
		// The street at the PlayerStart (Kate may have loaded anywhere, a roof included).
		Feet = Kate->GetActorLocation() - FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		Along = Kate->GetActorForwardVector().GetSafeNormal2D();
		if (TActorIterator<APlayerStart> Start(World); Start)
		{
			Along = Start->GetActorForwardVector().GetSafeNormal2D();
			const FVector From = Start->GetActorLocation() + FVector(0.f, 0.f, 200.f);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(BodyShotGround), false, Kate);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, From, From - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Query))
			{
				Feet = Hit.ImpactPoint;
			}
		}
		KateRef = Kate;
		// Kate steps out of the frame (and out of everyone's sight).
		Kate->SetActorHiddenInGame(true);
		Kate->SetActorEnableCollision(false);
		if (UCharacterMovementComponent* Movement = Kate->GetCharacterMovement())
		{
			Movement->DisableMovement();
		}
		Kate->SetActorLocation(Kate->GetActorLocation() - Along * 1200.f + FVector(0.f, 0.f, 800.f));

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		if (Camera)
		{
			// The ragdoll's shot moves the camera the frame before it is taken; no smear from the jump.
			Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
			Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
		}
		Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform::Identity, Params);
		if (UPointLightComponent* Lamp = Light ? Light->PointLightComponent.Get() : nullptr)
		{
			Lamp->SetMobility(EComponentMobility::Movable);
			Lamp->SetIntensityUnits(ELightUnits::Lumens);
			Lamp->SetIntensity(500.f);
			Lamp->SetAttenuationRadius(900.f);
			Lamp->SetCastShadows(false);
		}
		Test->AddInfo(FString::Printf(TEXT("Bodies: the subjects stand at %s facing %s."), *Feet.ToCompactString(),
			*Along.Rotation().ToCompactString()));
	}

	/** A thug of Class at the spot, facing the camera, not thinking. */
	AThugCharacter* SpawnThug(UWorld* World, const TCHAR* ClassPath)
	{
		UClass* Class = LoadClass<AThugCharacter>(nullptr, ClassPath);
		if (!Class)
		{
			Test->AddError(FString::Printf(TEXT("Body shots: no %s."), ClassPath));
			return nullptr;
		}
		const FTransform At(Along.Rotation(), Feet + FVector(0.f, 0.f, 100.f));
		AThugCharacter* Thug = World->SpawnActorDeferred<AThugCharacter>(Class, At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Thug)
		{
			return nullptr;
		}
		Thug->AutoPossessAI = EAutoPossessAI::Disabled;
		Thug->FinishSpawning(At);
		Thug->TeleportTo(Feet + FVector(0.f, 0.f, Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), Along.Rotation(),
			false, true);
		Spawned.Add(Thug);
		return Thug;
	}

	void Frame(APlayerController* PC, const FVector& Target, float Distance, float Fov)
	{
		// Front-on and a little to his right, from chest height.
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		const FVector Eye = Target + Along * Distance + Side * (Distance * 0.25f) + FVector(0.f, 0.f, Distance * 0.08f);
		if (Camera)
		{
			Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(Fov);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			PC->SetViewTarget(Camera);
		}
		if (Light)
		{
			Light->SetActorLocation(Eye + FVector(0.f, 0.f, 80.f));
		}
	}

	/** The district's Clint (benched in act 1), brought onto the spot; null when there is none. */
	AHawkeyeCharacter* BringClint(UWorld* World, const FVector& At)
	{
		for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
		{
			bWasBenched = It->IsBenched();
			It->SetBenched(false);
			Partner = *It;
			It->SetThinkingEnabled(false);
			It->StopMovement();
			if (AHawkeyeCharacter* Clint = It->GetPartner())
			{
				if (UCharacterMovementComponent* Movement = Clint->GetCharacterMovement())
				{
					Movement->StopMovementImmediately();
				}
				Clint->TeleportTo(At + FVector(0.f, 0.f, Clint->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
					Along.Rotation(), false, true);
				Clint->SetActorRotation(Along.Rotation());
				return Clint;
			}
		}
		return nullptr;
	}

	/** Lucky on the spot At, facing the camera, not thinking, in State: the district's own, else a new one. */
	ALuckyCharacter* BringLucky(UWorld* World, const FVector& At, const FRotator& Facing, ELuckyState State)
	{
		ALuckyCharacter* Dog = nullptr;
		for (TActorIterator<ALuckyCharacter> It(World); It && !Dog; ++It)
		{
			Dog = *It;
		}
		if (!Dog)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Dog = World->SpawnActor<ALuckyCharacter>(ALuckyCharacter::StaticClass(), FTransform(Facing, At + FVector(0.f, 0.f, 40.f)), Params);
			if (Dog)
			{
				Spawned.Add(Dog);
			}
		}
		if (!Dog)
		{
			return nullptr;
		}
		Dog->SetThinkingEnabled(false);
		Dog->TeleportTo(At + FVector(0.f, 0.f, Dog->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f), Facing, false, true);
		Dog->SetActorRotation(Facing);
		Dog->SetLuckyState(State);
		return Dog;
	}

	void Stand(UWorld* World, APlayerController* PC, int32 Subject)
	{
		Clear();
		const FVector Chest = Feet + FVector(0.f, 0.f, 110.f);
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		switch (Subject)
		{
		case ThugShot:
		case FlashShot:
			Current = SpawnThug(World, HawkeyeBodyShots::ThugClassPath);
			Frame(PC, Chest - FVector(0.f, 0.f, 25.f), 430.f, 50.f);
			break;
		case PunchShot:
			// From his left, so the swing's arm reads against the street.
			Current = SpawnThug(World, HawkeyeBodyShots::ThugClassPath);
			Frame(PC, Chest - FVector(0.f, 0.f, 25.f), 430.f, 50.f);
			if (Camera)
			{
				const FVector Eye = Chest - FVector(0.f, 0.f, 20.f) + Along * 180.f - Side * 250.f + FVector(0.f, 0.f, 20.f);
				Camera->SetActorLocationAndRotation(Eye, (Chest - FVector(0.f, 0.f, 20.f) - Eye).Rotation());
			}
			break;
		case HeavyShot:
			Current = SpawnThug(World, HawkeyeBodyShots::HeavyClassPath);
			Frame(PC, Chest - FVector(0.f, 0.f, 25.f), 430.f, 50.f);
			break;
		case ClintShot:
		case DrawShot:
			Current = BringClint(World, Feet);
			if (!Current.IsValid())
			{
				Test->AddWarning(TEXT("body_clint.png: no Clint in the district."));
			}
			Frame(PC, Chest - FVector(0.f, 0.f, 25.f), 430.f, 50.f);
			break;
		case LuckyShot:
			if (ALuckyCharacter* Dog = BringLucky(World, Feet, Along.Rotation(), ELuckyState::Idle))
			{
				Current = Dog;
			}
			Frame(PC, Feet + FVector(0.f, 0.f, 40.f), 280.f, 50.f);
			break;
		case LuckyKateShot:
		{
			// Kate back in the frame, Lucky sat at her left, both facing the camera.
			if (KateRef.IsValid())
			{
				KateRef->SetActorHiddenInGame(false);
				KateRef->TeleportTo(Feet + Side * 45.f + FVector(0.f, 0.f, KateRef->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
					Along.Rotation(), false, true);
				KateRef->SetActorRotation(Along.Rotation());
				// Her own camera is not the view: stop her tick so its close-up hide cannot hide her from this one.
				KateRef->SetActorTickEnabled(false);
				if (USkeletalMeshComponent* Body = KateRef->GetBodyMesh())
				{
					Body->SetOwnerNoSee(false);
				}
				if (AController* Controller = KateRef->GetController())
				{
					Controller->SetControlRotation(Along.Rotation());
				}
			}
			if (ALuckyCharacter* Dog = BringLucky(World, Feet - Side * 45.f, Along.Rotation(), ELuckyState::Sit))
			{
				Current = Dog;
			}
			Frame(PC, Feet + FVector(0.f, 0.f, 80.f), 330.f, 50.f);
			break;
		}
		default:
		{
			AThugCharacter* Thug = SpawnThug(World, HawkeyeBodyShots::ThugClassPath);
			Current = Thug;
			if (Thug)
			{
				// A blast's throw: past the clip's launch, so always the ragdoll.
				Thug->KnockdownFor(nullptr, 4.f, Thug->KnockdownLaunchSpeed * 1.4f);
			}
			Frame(PC, Feet + FVector(0.f, 0.f, 50.f) - Along * 150.f, 420.f, 55.f);
			break;
		}
		}
	}

	/** Lucky's checks: his size, and his head toward where he faces. */
	void CheckLucky(const FString& Name)
	{
		const ALuckyCharacter* Dog = Cast<ALuckyCharacter>(Current.Get());
		const USkeletalMeshComponent* Body = Dog ? Dog->GetMesh() : nullptr;
		if (!Body || !Body->GetSkeletalMeshAsset())
		{
			Test->AddInfo(Name + TEXT(": no SK_Lucky on this clone (the packs are not imported); the empty street is shot."));
			return;
		}
		const FBoxSphereBounds Bounds = Body->Bounds;
		const float Tall = Bounds.BoxExtent.Z * 2.f;
		FVector Head(0.f), Pelvis(0.f);
		const bool bBones = HawkeyeBodyShots::Bone(Body, TEXT("head"), Head) && HawkeyeBodyShots::Bone(Body, TEXT("pelvis"), Pelvis);
		const float Facing = bBones ? FVector::DotProduct((Head - Pelvis).GetSafeNormal2D(), Dog->GetActorForwardVector().GetSafeNormal2D()) : -1.f;
		// Against his capsule's bottom, which the movement keeps on whatever is under him (the kerb, the road).
		const float Ground = Bounds.Origin.Z - Bounds.BoxExtent.Z
			- (Dog->GetActorLocation().Z - Dog->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		Test->AddInfo(FString::Printf(TEXT("%s: %s %.0f cm tall, %.0f x %.0f cm, head %.2f along his facing, lowest point %.0f cm off his capsule's bottom, state %d, clip %s."),
			*Name, *GetNameSafe(Body->GetSkeletalMeshAsset()), Tall, Bounds.BoxExtent.X * 2.f, Bounds.BoxExtent.Y * 2.f, Facing, Ground,
			static_cast<int32>(Dog->GetLuckyState()), *GetNameSafe(Body->GetSingleNodeInstance() ? Body->GetSingleNodeInstance()->GetAnimationAsset() : nullptr)));
		Test->TestTrue(*FString::Printf(TEXT("%s: Lucky stands 40 to 95 cm tall (%.0f)"), *Name, Tall), Tall > 40.f && Tall < 95.f);
		Test->TestTrue(*FString::Printf(TEXT("%s: his head is toward where he faces (%.2f)"), *Name, Facing), Facing > 0.5f);
		Test->TestTrue(*FString::Printf(TEXT("%s: his feet are on the ground (%.0f cm off)"), *Name, Ground), FMath::Abs(Ground) < 15.f);
	}

	void Shoot(int32 Subject)
	{
		using namespace HawkeyeBodyShots;
		static const TCHAR* Names[NumSubjects] = {TEXT("body_thug"), TEXT("body_heavy"), TEXT("body_clint"), TEXT("body_lucky"),
			TEXT("body_thug_ragdoll"), TEXT("body_thug_punch"), TEXT("body_thug_flash"), TEXT("body_clint_draw"), TEXT("body_lucky_kate")};
		ACharacter* Character = Cast<ACharacter>(Current.Get());
		if (Subject == LuckyShot || Subject == LuckyKateShot)
		{
			CheckLucky(FString(Names[Subject]) + TEXT(".png"));
		}
		else if (Subject >= PunchShot)
		{
			ShootMoment(Subject, Names[Subject]);
		}
		else if (Character)
		{
			const USkeletalMeshComponent* Driver = Character->GetMesh();
			const USkeletalMeshComponent* Seen = Driver;
			if (const AThugCharacter* Thug = Cast<AThugCharacter>(Character))
			{
				Seen = Thug->GetBodyMesh();
			}
			else if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Character))
			{
				Seen = Hawkeye->GetBodyMesh();
			}
			if (Subject < RagdollShot)
			{
				Test->AddInfo(FString::Printf(TEXT("%s.png: %s"), Names[Subject],
					*CheckStanding(Test, Names[Subject], Character, Driver, Seen)));
			}
			else
			{
				// Framed where he came down.
				FVector Down(0.f);
				if (APlayerController* PC = Character->GetWorld()->GetFirstPlayerController(); PC && Bone(Driver, TEXT("pelvis"), Down))
				{
					Frame(PC, Down, 300.f, 55.f);
				}
				const AThugCharacter* Thug = Cast<AThugCharacter>(Character);
				Test->TestTrue(TEXT("body_thug_ragdoll: he is a ragdoll"), Thug && Thug->IsRagdolling());
				FVector Pelvis(0.f), DriverPelvis(0.f);
				if (Seen != Driver && Bone(Seen, TEXT("pelvis"), Pelvis) && Bone(Driver, TEXT("pelvis"), DriverPelvis))
				{
					const float Off = FVector::Dist(Pelvis, DriverPelvis);
					Test->AddInfo(FString::Printf(TEXT("body_thug_ragdoll.png: the look's pelvis %.0f cm from the ragdoll's, %.0f cm up."),
						Off, Pelvis.Z - Feet.Z));
					Test->TestTrue(*FString::Printf(TEXT("body_thug_ragdoll: the look follows the ragdoll (pelvis %.0f cm off)"), Off), Off < 25.f);
				}
				else
				{
					Test->AddInfo(TEXT("body_thug_ragdoll.png: no body look on this clone; the mannequin's ragdoll is shot."));
				}
			}
		}
		HawkeyeShots::Request(Test, ShotPath(Names[Subject]), /*bShowUI=*/false);
	}

	/** The punch, the flash and the draw: what state he was caught in, and that the look is what is drawn. */
	void ShootMoment(int32 Subject, const TCHAR* Name)
	{
		ACharacter* Character = Cast<ACharacter>(Current.Get());
		if (!Character)
		{
			Test->AddWarning(FString::Printf(TEXT("%s.png: nobody to shoot."), Name));
			return;
		}
		if (const AThugCharacter* Thug = Cast<AThugCharacter>(Character))
		{
			const UMeleeComponent* Melee = Thug->GetMeleeComponent();
			const bool bWinding = Melee && Melee->IsWindingUp();
			Test->AddInfo(FString::Printf(TEXT("%s.png: %s, winding up %d, hit flash %.2f, look %s."), Name, *GetNameSafe(Thug->GetBodyMesh()->GetSkeletalMeshAsset()),
				bWinding ? 1 : 0, Thug->GetHitFlashAlpha(), Thug->GetBodyMesh() != Thug->GetMesh() ? TEXT("drawn") : TEXT("none (mannequin)")));
			if (Subject == PunchShot)
			{
				Test->TestTrue(FString::Printf(TEXT("%s: caught in the wind-up (the telegraph glow is on)"), Name), bWinding);
			}
			else
			{
				Test->TestTrue(FString::Printf(TEXT("%s: caught in the flash (%.2f)"), Name, Thug->GetHitFlashAlpha()), Thug->GetHitFlashAlpha() > 0.5f);
			}
		}
		else if (const AHawkeyeCharacter* Clint = Cast<AHawkeyeCharacter>(Character))
		{
			const UBowComponent* Bow = Clint->GetBowComponent();
			Test->AddInfo(FString::Printf(TEXT("%s.png: %s, drawing %d, draw %.2f."), Name, *GetNameSafe(Clint->GetBodyMesh()->GetSkeletalMeshAsset()),
				Bow && Bow->IsDrawing() ? 1 : 0, Bow ? Bow->GetDrawFraction() : 0.f));
		}
	}

	void Clear()
	{
		if (AHawkeyeCharacter* Clint = Cast<AHawkeyeCharacter>(Current.Get()))
		{
			if (UBowComponent* Bow = Clint->GetBowComponent(); Bow && Bow->IsDrawing())
			{
				Bow->CancelDraw();
			}
		}
		if (Current.IsValid())
		{
			Current->CustomTimeDilation = 1.f;
		}
		if (ALuckyCharacter* Dog = Cast<ALuckyCharacter>(Current.Get()))
		{
			// The district's dog is put back out of the frame (thinking stays off until the test ends).
			Dog->TeleportTo(Feet - Along * 1200.f + FVector(0.f, 0.f, 60.f), Along.Rotation(), false, true);
		}
		if (KateRef.IsValid() && !KateRef->IsHidden())
		{
			KateRef->SetActorHiddenInGame(true);
			KateRef->SetActorTickEnabled(true);
			KateRef->TeleportTo(KateRef->GetActorLocation() - Along * 1200.f + FVector(0.f, 0.f, 800.f), Along.Rotation(), false, true);
		}
		// Clint is the district's own; he is walked off the spot and benched again, not destroyed.
		if (AHawkeyeCharacter* Clint = Cast<AHawkeyeCharacter>(Current.Get()))
		{
			Clint->TeleportTo(Feet - Along * 1500.f + FVector(0.f, 0.f, 120.f), Along.Rotation(), false, true);
		}
		if (Partner.IsValid())
		{
			Partner->SetBenched(bWasBenched);
			Partner = nullptr;
		}
		for (const TWeakObjectPtr<AActor>& Actor : Spawned)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}
		Spawned.Reset();
		Current = nullptr;
	}

	void Cleanup()
	{
		Clear();
		if (Camera)
		{
			Camera->Destroy();
		}
		if (Light)
		{
			Light->Destroy();
		}
	}

	FAutomationTestBase* Test = nullptr;
	int32 Step = 0;
	double StepStart = 0.0;
	FVector Feet = FVector::ZeroVector;
	FVector Along = FVector::ForwardVector;
	ACameraActor* Camera = nullptr;
	APointLight* Light = nullptr;
	TWeakObjectPtr<AActor> Current;
	TWeakObjectPtr<AHawkeyeCharacter> KateRef;
	double ActionStart = -1.0;
	TWeakObjectPtr<AHawkeyePartnerController> Partner;
	bool bWasBenched = false;
	TArray<TWeakObjectPtr<AActor>> Spawned;
};

bool FHawkeyeScreenshotBodies::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the body screenshots. Re-run without -nullrhi to capture them."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeBodyShots(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
