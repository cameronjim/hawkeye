#include "Boss/BossDefinition.h"
#include "Boss/BossFightSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Hawkeye.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Mission/ChapterSiteDefinition.h"
#include "Mission/ChapterSiteSubsystem.h"
#include "Misc/Paths.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/PartnerScreenshots.h"
#include "World/LuckyCharacter.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The portfolio still: Kate, Clint and Lucky posed together at night, no HUD, no screen messages
 * (Tools/shoot-portrait.ps1 runs it for each preset). A calm group shot, not an action frame: all three stand
 * (Lucky sits) facing the camera in their idle, the camera a medium-wide at chest height.
 *
 * The preset is -HawkeyePortrait=<a|b|c>, each of which names a chapter the launcher starts (the roof and the grotto's
 * frontage are dressed by their chapters) and where the group stands; the pieces can be overridden on the command line
 * for scouting: -HawkeyePortraitAt=x:y:z (the middle of the group's feet), -HawkeyePortraitYaw=<where the camera looks>,
 * -HawkeyePortraitDist, -HawkeyePortraitHeight, -HawkeyePortraitAim, -HawkeyePortraitFov, -HawkeyePortraitSide (the camera's shift
 * along the group's line, cm), -HawkeyePortraitLamp (lumens of the fill lamp by the camera), -HawkeyePortraitFstop (a
 * little depth of field, 0 for none), -HawkeyePortraitSpread (cm between them), -HawkeyePortraitSettle (seconds).
 * Writes Saved/Screenshots/Portrait/portrait_<name>.png. Needs a real RHI.
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject /Game/Maps/L_District_EastVillage -game -windowed -ResX=1920 -ResY=1080
 *       -HawkeyeChapter=DA_CH04_SocialClub -HawkeyePortrait=a -unattended -nosplash -log
 *       -ExecCmds="Automation RunTests Hawkeye.Screenshot.Portrait; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotPortrait, "Hawkeye.Screenshot.Portrait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyePortraitShot
{
	/** One look: where the group stands, which way the camera looks, and the lens. */
	struct FPreset
	{
		FVector At = FVector::ZeroVector;
		bool bAtArenaCentre = false;
		/** In front of the chapter site's first door, DoorOut cm out, the camera looking at it. */
		bool bAtSiteDoor = false;
		float DoorOut = 330.f;
		float CamYaw = 0.f;
		float Dist = 520.f;
		float Height = 140.f;
		/** Where the lens looks, cm above the group's feet. */
		float Aim = 95.f;
		float Fov = 42.f;
		float Side = 0.f;
		float Lamp = 900.f;
		float Fstop = 0.f;
		float Spread = 112.f;
		float Settle = 6.f;
	};

	static FPreset PresetFor(const FString& Name)
	{
		FPreset P;
		if (Name == TEXT("a"))
		{
			// The social club's roof: the string lights and the skyline behind.
			P.At = FVector(-9391.f, 10800.f, 1590.f);
			P.CamYaw = -90.f;
			P.Dist = 640.f;
			P.Height = 125.f;
			P.Fov = 42.f;
			P.Lamp = 0.f;
			P.Fstop = 3.2f;
		}
		else if (Name == TEXT("b"))
		{
			// The grotto's frontage on the street, the candy posts and lights behind them.
			P.bAtSiteDoor = true;
		}
		return P;
	}

	static bool ParseNumbers(const TCHAR* Key, TArray<float>& Out)
	{
		FString Text;
		if (!FParse::Value(FCommandLine::Get(), Key, Text))
		{
			return false;
		}
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(":"));
		Out.Reset();
		for (const FString& Part : Parts)
		{
			Out.Add(FCString::Atof(*Part));
		}
		return Out.Num() > 0;
	}

	static void Override(const TCHAR* Key, float& Value)
	{
		TArray<float> N;
		if (ParseNumbers(Key, N))
		{
			Value = N[0];
		}
	}

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

	static bool Ground(UWorld* World, const FVector& XY, float FromZ, const AActor* IgnoreA, FVector& Out)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(PortraitGround), false, IgnoreA);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, FromZ - 1500.f), ECC_Visibility, Query))
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}
}

class FHawkeyePortraitRunner : public IAutomationLatentCommand
{
public:
	explicit FHawkeyePortraitRunner(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyePortraitShot;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!World || !PC || !Kate)
		{
			Test->AddError(TEXT("Portrait: no world, player controller or Kate."));
			return true;
		}
		if (HawkeyeShots::GetOutstanding(Test) > 0)
		{
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		switch (Phase)
		{
		case 0:
			Setup(World, PC, Kate);
			Phase = 1;
			PhaseStart = Now;
			return false;
		case 1:
			// Walk Kate onto her spot, keep everything where it was put, let the lights and the temporal upscale settle.
			KeepScene(PC, Kate);
			WalkIn(Kate);
			if (!bWalking && Now - PhaseStart > P.Settle)
			{
				Phase = 2;
			}
			else if (Now - PhaseStart > P.Settle + 10.0)
			{
				Test->AddWarning(TEXT("Portrait: Kate never reached her spot; shooting anyway."));
				Phase = 2;
			}
			return false;
		case 2:
			KeepScene(PC, Kate);
			Report(Kate);
			HawkeyeShots::Request(Test, OutPath(), /*bShowUI=*/false);
			Phase = 3;
			return false;
		default:
			Cleanup();
			return true;
		}
	}

private:
	FString OutPath() const
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Portrait") / FString::Printf(TEXT("portrait_%s.png"), *Name));
	}

	void Setup(UWorld* World, APlayerController* PC, AHawkeyeCharacter* Kate)
	{
		using namespace HawkeyePortraitShot;
		if (!FParse::Value(FCommandLine::Get(), TEXT("HawkeyePortrait="), Name) || Name.IsEmpty())
		{
			Name = TEXT("a");
		}
		P = PresetFor(Name);
		if (GEngine)
		{
			GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
			GEngine->Exec(World, TEXT("hawkeye.TimeOfDay Night"));
		}
		// The roof: the arena's middle. Everything the chapter put there stays (the lights, the poles, the Santa).
		UBossFightSubsystem* Fight = UBossFightSubsystem::Get(World);
		if (Fight && Fight->GetDefinition())
		{
			const FBossArena& Arena = Fight->GetDefinition()->Arena;
			UE_LOG(LogHawkeye, Log, TEXT("Portrait: arena centre %s, roof z %.0f, radius %.0f, boss spot %s yaw %.0f, hazard %s to %s, %d props, %d lights."),
				*Arena.Centre.ToCompactString(), Arena.RoofZ, Arena.FightRadius, *Arena.BossSpot.ToCompactString(), Arena.BossYaw,
				*Arena.HazardStart.ToCompactString(), *Arena.HazardEnd.ToCompactString(), Arena.Props.Num(), Arena.Lights.Num());
			for (const FBossArenaProp& Prop : Arena.Props)
			{
				UE_LOG(LogHawkeye, Log, TEXT("Portrait: arena prop %s at %s"), *Prop.Mesh.GetAssetName(), *Prop.Transform.GetLocation().ToCompactString());
			}
			for (const FBossArenaLight& Light : Arena.Lights)
			{
				UE_LOG(LogHawkeye, Log, TEXT("Portrait: arena light at %s lumens %.0f"), *Light.Location.ToCompactString(), Light.Lumens);
			}
			if (P.bAtArenaCentre)
			{
				P.At = Arena.Centre;
			}
			// The fight is not wanted: Tomas and his crew are out of the picture and out of the way.
			TArray<AThugCharacter*> Away = Fight->GetCrew();
			if (AThugCharacter* Boss = Fight->GetBoss())
			{
				Away.Add(Boss);
			}
			for (AThugCharacter* Thug : Away)
			{
				if (Thug)
				{
					Thug->SetActorHiddenInGame(true);
					Thug->SetActorEnableCollision(false);
				}
			}
		}
		Override(TEXT("HawkeyePortraitDoorOut="), P.DoorOut);
		if (P.bAtSiteDoor)
		{
			const UChapterSiteSubsystem* Site = UChapterSiteSubsystem::Get(World);
			const UChapterSiteDefinition* Definition = Site ? Site->GetDefinition() : nullptr;
			if (Definition && Definition->InteriorDoors.Num() > 0)
			{
				const FChapterSiteInteriorDoor& Door = Definition->InteriorDoors[0];
				const FVector Out = FRotator(0.f, Door.Yaw, 0.f).Vector();
				P.At = Door.Location + Out * P.DoorOut;
				P.CamYaw = Door.Yaw + 180.f;
				UE_LOG(LogHawkeye, Log, TEXT("Portrait: the site door \"%s\" at %s faces yaw %.0f."), *Door.DisplayName.ToString(), *Door.Location.ToCompactString(), Door.Yaw);
			}
			else
			{
				Test->AddError(TEXT("Portrait: no chapter site with a door is running (start -HawkeyeChapter=DA_CH03_Grotto)."));
			}
		}
		TArray<float> N;
		if (ParseNumbers(TEXT("HawkeyePortraitAt="), N) && N.Num() >= 3)
		{
			P.At = FVector(N[0], N[1], N[2]);
		}
		Override(TEXT("HawkeyePortraitYaw="), P.CamYaw);
		Override(TEXT("HawkeyePortraitDist="), P.Dist);
		Override(TEXT("HawkeyePortraitHeight="), P.Height);
		Override(TEXT("HawkeyePortraitAim="), P.Aim);
		Override(TEXT("HawkeyePortraitFov="), P.Fov);
		Override(TEXT("HawkeyePortraitSide="), P.Side);
		Override(TEXT("HawkeyePortraitLamp="), P.Lamp);
		Override(TEXT("HawkeyePortraitFstop="), P.Fstop);
		Override(TEXT("HawkeyePortraitSpread="), P.Spread);
		Override(TEXT("HawkeyePortraitSettle="), P.Settle);

		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(false);
			}
		}
		HawkeyeFreezePartner(World);

		// The ground under the middle of the group.
		FVector Found;
		Feet = Ground(World, P.At, P.At.Z + 200.f, Kate, Found) ? Found : P.At;
		const FRotator CamLook(0.f, P.CamYaw, 0.f);
		ToCamera = -CamLook.Vector();                       // the way the group faces: back along the camera's look
		Side = FVector::CrossProduct(FVector::UpVector, ToCamera);   // the group's right (the screen's left)
		FaceYaw = ToCamera.Rotation().Yaw;

		// Kate in the middle, Clint at her right (the screen's left) a little behind, Lucky sat at her left, a little forward.
		KateFeet = Feet;
		ClintFeet = Feet + Side * P.Spread - ToCamera * 25.f;
		LuckyFeet = Feet - Side * (P.Spread * 0.95f) + ToCamera * 45.f;
		for (FVector* Spot : { &KateFeet, &ClintFeet, &LuckyFeet })
		{
			FVector G;
			if (Ground(World, *Spot, Feet.Z + 120.f, Kate, G))
			{
				*Spot = G;
			}
		}

		// Kate, walked onto her spot a metre short of it so her body turns to face (as the Bodies shots do).
		KateRef = Kate;
		Kate->SetActorHiddenInGame(false);
		Kate->SetActorEnableCollision(true);
		Kate->CustomTimeDilation = 1.f;
		float KateTurn = 0.f;
		Override(TEXT("HawkeyePortraitKateTurn="), KateTurn);
		const FRotator Facing(0.f, FaceYaw + KateTurn, 0.f);
		Kate->TeleportTo(KateFeet - Facing.Vector() * 100.f + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), Facing, false, true);
		Kate->SetActorRotation(Facing);
		if (UCharacterMovementComponent* Movement = Kate->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Walking);
		}
		if (AController* Controller = Kate->GetController())
		{
			Controller->SetControlRotation(Facing);
		}
		bWalking = true;

		// Clint, the district's own, benched in act 1: brought out, standing.
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
				Clint->SetActorHiddenInGame(false);
				const FRotator ClintFacing(0.f, FaceYaw + 14.f, 0.f);
				Clint->TeleportTo(ClintFeet + FVector(0.f, 0.f, Clint->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), ClintFacing, false, true);
				Clint->SetActorRotation(ClintFacing);
				ClintRef = Clint;
			}
			break;
		}
		if (!ClintRef.IsValid())
		{
			Test->AddError(TEXT("Portrait: no Clint in the district."));
		}

		// Lucky, the district's own or a new one, sitting.
		for (TActorIterator<ALuckyCharacter> It(World); It && !LuckyRef.IsValid(); ++It)
		{
			LuckyRef = *It;
		}
		if (!LuckyRef.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			LuckyRef = World->SpawnActor<ALuckyCharacter>(ALuckyCharacter::StaticClass(), FTransform(FRotator(0.f, FaceYaw, 0.f), LuckyFeet + FVector(0.f, 0.f, 60.f)), Params);
			bSpawnedLucky = true;
		}
		if (ALuckyCharacter* Dog = LuckyRef.Get())
		{
			Dog->SetThinkingEnabled(false);
			const FRotator DogFacing(0.f, FaceYaw - 20.f, 0.f);
			Dog->TeleportTo(LuckyFeet + FVector(0.f, 0.f, Dog->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.f), DogFacing, false, true);
			Dog->SetActorRotation(DogFacing);
			Dog->SetLuckyState(ELuckyState::Sit);
		}
		else
		{
			Test->AddError(TEXT("Portrait: no Lucky."));
		}

		// The camera, and the lamp by it that keeps faces warm.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		if (Camera)
		{
			UCameraComponent* Lens = Camera->GetCameraComponent();
			Lens->bConstrainAspectRatio = false;
			Lens->SetFieldOfView(P.Fov);
			Lens->PostProcessSettings.bOverride_MotionBlurAmount = true;
			Lens->PostProcessSettings.MotionBlurAmount = 0.f;
			if (P.Fstop > 0.f)
			{
				Lens->PostProcessSettings.bOverride_DepthOfFieldFstop = true;
				Lens->PostProcessSettings.DepthOfFieldFstop = P.Fstop;
				Lens->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
				Lens->PostProcessSettings.DepthOfFieldFocalDistance = P.Dist;
				Lens->PostProcessSettings.bOverride_DepthOfFieldSensorWidth = true;
				Lens->PostProcessSettings.DepthOfFieldSensorWidth = 36.f;
			}
		}
		Lamp = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform::Identity, Params);
		if (UPointLightComponent* Bulb = Lamp ? Lamp->PointLightComponent.Get() : nullptr)
		{
			Bulb->SetMobility(EComponentMobility::Movable);
			Bulb->SetIntensityUnits(ELightUnits::Lumens);
			Bulb->SetIntensity(P.Lamp);
			Bulb->SetLightColor(FLinearColor(1.f, 0.82f, 0.62f));
			Bulb->SetAttenuationRadius(1100.f);
			Bulb->SetSourceRadius(60.f);
			Bulb->SetCastShadows(false);
		}
		PlaceCamera(PC);
		UE_LOG(LogHawkeye, Log, TEXT("Portrait %s: group at %s, camera looks yaw %.0f from %.0f cm, lens %.0f deg, Kate %s Clint %s Lucky %s."),
			*Name, *Feet.ToCompactString(), P.CamYaw, P.Dist, P.Fov, *KateFeet.ToCompactString(), *ClintFeet.ToCompactString(), *LuckyFeet.ToCompactString());
	}

	void PlaceCamera(APlayerController* PC)
	{
		if (!Camera)
		{
			return;
		}
		const FVector CamSide = FVector::CrossProduct(FVector::UpVector, -ToCamera);   // the camera's right
		const FVector Target = Feet + FVector(0.f, 0.f, P.Aim);
		const FVector Eye = Feet + ToCamera * P.Dist + CamSide * P.Side + FVector(0.f, 0.f, P.Height);
		Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
		PC->SetViewTarget(Camera);
		if (Lamp)
		{
			Lamp->SetActorLocation(Eye + FVector(0.f, 0.f, 90.f) + CamSide * -120.f);
		}
	}

	void KeepScene(APlayerController* PC, AHawkeyeCharacter* Kate)
	{
		// Her own camera's close-up hide is for her own view.
		if (USkeletalMeshComponent* Body = Kate->GetBodyMesh())
		{
			Body->SetOwnerNoSee(false);
		}
		if (AHawkeyeCharacter* Clint = ClintRef.Get())
		{
			if (USkeletalMeshComponent* Body = Clint->GetBodyMesh())
			{
				Body->SetOwnerNoSee(false);
			}
		}
		if (PC->GetViewTarget() != Camera)
		{
			PlaceCamera(PC);
		}
	}

	void WalkIn(AHawkeyeCharacter* Kate)
	{
		if (!bWalking)
		{
			return;
		}
		const FVector Here = Kate->GetActorLocation();
		const FVector To(KateFeet.X - Here.X, KateFeet.Y - Here.Y, 0.f);
		if (To.Size() < 8.f)
		{
			bWalking = false;
			if (UCharacterMovementComponent* Movement = Kate->GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
			return;
		}
		Kate->AddMovementInput(To.GetSafeNormal(), 0.5f);
	}

	/** The standing checks, logged: a T-pose or a floating foot would show here before it shows in the picture. */
	void Report(const AHawkeyeCharacter* Kate)
	{
		auto Bone = [](const USkeletalMeshComponent* Mesh, const TCHAR* Bone, FVector& Out)
		{
			if (!Mesh || Mesh->GetBoneIndex(FName(Bone)) == INDEX_NONE)
			{
				return false;
			}
			Out = Mesh->GetBoneLocation(FName(Bone));
			return true;
		};
		const AHawkeyeCharacter* const Both[2] = { Kate, ClintRef.Get() };
		for (const AHawkeyeCharacter* Who : Both)
		{
			if (!Who)
			{
				continue;
			}
			const USkeletalMeshComponent* Seen = Who->GetBodyMesh();
			FVector HandL(0.f), HandR(0.f), ArmL(0.f), ArmR(0.f), Foot(0.f), Ball(0.f);
			const bool bArms = Bone(Seen, TEXT("hand_l"), HandL) && Bone(Seen, TEXT("hand_r"), HandR) && Bone(Seen, TEXT("upperarm_l"), ArmL) && Bone(Seen, TEXT("upperarm_r"), ArmR);
			const float Drop = bArms ? FMath::Min(ArmL.Z - HandL.Z, ArmR.Z - HandR.Z) : 0.f;
			const bool bFeet = Bone(Seen, TEXT("foot_l"), Foot) && Bone(Seen, TEXT("ball_l"), Ball);
			const float Facing = bFeet ? FVector::DotProduct((Ball - Foot).GetSafeNormal2D(), Who->GetActorForwardVector().GetSafeNormal2D()) : -1.f;
			const float Ground = Who->GetActorLocation().Z - Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FVector Lowest(0.f, 0.f, 1e9f);
			for (const TCHAR* BoneName : { TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r") })
			{
				FVector At(0.f);
				if (Bone(Seen, BoneName, At) && At.Z < Lowest.Z)
				{
					Lowest = At;
				}
			}
			UE_LOG(LogHawkeye, Log, TEXT("Portrait: %s hands %.0f cm below the shoulders, toes %.2f along her facing, feet %.0f cm above the ground, mesh %s, speed %.0f."),
				*Who->GetName(), Drop, Facing, Lowest.Z - Ground, *GetNameSafe(Seen ? Seen->GetSkeletalMeshAsset() : nullptr), Who->GetVelocity().Size());
			Test->TestTrue(*FString::Printf(TEXT("%s is not in a T-pose (hands %.0f cm below the shoulders)"), *Who->GetName(), Drop), !bArms || Drop > 30.f);
		}
		if (const ALuckyCharacter* Dog = LuckyRef.Get())
		{
			UE_LOG(LogHawkeye, Log, TEXT("Portrait: Lucky at %s state %d visible %d."), *Dog->GetActorLocation().ToCompactString(), (int32)Dog->GetLuckyState(), Dog->GetMesh()->IsVisible() ? 1 : 0);
		}
	}

	void Cleanup()
	{
		if (Partner.IsValid())
		{
			Partner->SetBenched(bWasBenched);
		}
		if (bSpawnedLucky && LuckyRef.IsValid())
		{
			LuckyRef->Destroy();
		}
		if (GEngine && KateRef.IsValid())
		{
			GEngine->Exec(KateRef->GetWorld(), TEXT("hawkeye.TimeOfDay Saved"));
		}
		if (Camera)
		{
			Camera->Destroy();
		}
		if (Lamp)
		{
			Lamp->Destroy();
		}
	}

	FAutomationTestBase* Test = nullptr;
	int32 Phase = 0;
	double PhaseStart = 0.0;
	FString Name;
	HawkeyePortraitShot::FPreset P;
	FVector Feet = FVector::ZeroVector;
	FVector ToCamera = FVector::ForwardVector;
	FVector Side = FVector::RightVector;
	float FaceYaw = 0.f;
	FVector KateFeet = FVector::ZeroVector;
	FVector ClintFeet = FVector::ZeroVector;
	FVector LuckyFeet = FVector::ZeroVector;
	bool bWalking = false;
	bool bWasBenched = false;
	bool bSpawnedLucky = false;
	ACameraActor* Camera = nullptr;
	APointLight* Lamp = nullptr;
	TWeakObjectPtr<AHawkeyeCharacter> KateRef;
	TWeakObjectPtr<AHawkeyeCharacter> ClintRef;
	TWeakObjectPtr<ALuckyCharacter> LuckyRef;
	TWeakObjectPtr<AHawkeyePartnerController> Partner;
};

bool FHawkeyeScreenshotPortrait::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the portrait. Re-run without -nullrhi to capture it."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyePortraitRunner(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
