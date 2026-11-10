#include "Tests/MeleeScreenshots.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/BowComponent.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/CombatReadability.h"
#include "Combat/CombatTokenSubsystem.h"
#include "Combat/FinisherComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/PartnerScreenshots.h"
#include "Tests/VfxScreenshots.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The melee shots (Saved/Screenshots/Kate/), run in Hawkeye.Screenshot.Kate after the fair-fight shots
 * and on their own as Hawkeye.Screenshot.Melee. On the street by the PlayerStart, against a bat thug
 * the pass spawns (BP_Thug) and removes, every other thug frozen, Kate invulnerable:
 *
 *   combo_x3.png      her own camera, three lights chained into him, each pressed the moment the last one
 *                     lands (so it goes as soon as the chain lets it): the counter at x3 just after the third
 *   parry_flash.png   side on, a tap of V into his bat's wind-up: the purple ring between them, him staggering
 *   finisher_mid.png  her own camera 0.3 s into a finisher on him (staggered): time at 0.5, the lens pushed in
 *   hit_lean.png      from in front of him, 0.1 s after she hits him from his left: his upper body leant right
 *   strike_pose.png   side on, a light at the moment it strikes (its hit window opening, or 0.1 s in without
 *                     a clip): her right hand out at chest height
 *   heavy_strike.png  side on, her heavy (held V) at the moment its hit window opens: an AM_Heavy_ clip (the
 *                     Roundhouse, her only heavy in the set since 2026-09-29)
 *   heavy_strike_2.png the same, her next heavy, taken only while her set has a second heavy: the next
 *                     variant, never the same clip
 *   kick.png          side on, the Kick role (AM_Kick_*) at the moment its hit window opens
 *   telegraph_glyph.png her own camera, a hurt fists thug 130 cm in front of her 0.3 s into his 0.6 s wind-up:
 *                     his health bar just over his head (his head bone plus 20 cm), the parry line 4 px over
 *                     it and the red-orange telegraph "!" 4 px over that, grown and pulsing
 *   thug_punch_2.png  side on, his next fists swing (the second variant) 0.3 s into its wind-up
 *   thug_punch.png    side on, the one after (the first variant again) 0.3 s in: the two clips' wind-ups
 *   fight_camera.png  her own camera, two alerted fists thugs 3 to 4.5 m in front of her on the street: the
 *                     fight camera all the way in (boom 70 cm longer, lens tipped 4 degrees down)
 *   fight_camera_wall.png the same with her back 150 cm from a building: the probe pulls the longer boom in,
 *                     and she must still be in frame and drawn (the report gives the arm, the lens and where
 *                     she and the thugs land on screen; she hidden or off screen fails the test)
 *   target_ring.png   her own camera, 0.1 s into a light at a hurt bat thug 170 cm in front of her: the assist's
 *                     purple ring at his feet, his health bar drawn whiter (the marker not on him fails it)
 *   hit_spark.png     the same swing, time stopped in the tick the light lands: NS_MeleeSpark on his capsule
 *                     toward her at chest height, under his hit flash (no spark within 2 s fails it)
 *   token_ring.png    from above and behind her, four alerted fists thugs who came at her from 7 m 5 s before:
 *                     one holding her melee token (stepping in or swinging), the others waiting on the ring 300 cm
 *                     out, spread round her and facing her; the report gives each one's distance, bearing, facing
 *                     and speed (gameplay-semantics.md, "Attack tokens")
 *   knockdown_dust.png her own camera, 0.2 s (world time) after the thud of a bat thug her heavy knocked down
 *                     (1.5 s after he goes over): NS_KnockdownDust off the ground under his pelvis (no knockdown
 *                     or no puff fails it); the report then says when his pelvis really reached the ground
 *
 * The heavies and the kick fail the test when another montage plays (the second heavy: the first's clip
 * again), or when her pelvis at the capture is more than 15 cm below its height standing just before the press,
 * or more than 20 cm above it (read from the mesh's pelvis bone). The drop is the check that matters (a clip
 * with the hips' height on its root knelt everyone); the rise allows a kick's standing leg straightening from
 * her bent-knee idle (the roundhouse: +16 cm, the standing foot flat on the ground).
 *
 * The world is slowed to a crawl for each capture that is about a moment (all but the finisher, whose
 * own slow motion is the subject) and put back after.
 */
namespace HawkeyeMeleeShots
{
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");
	static constexpr float CrawlDilation = 0.02f;

	enum class EShot : uint8
	{
		Setup,
		ComboSetup,
		ParrySetup,
		ParrySwing,
		Parry,
		FinisherSetup,
		Finisher,
		LeanSetup,
		LeanHit,
		StrikeSetup,
		Strike,
		HeavySetup,
		Heavy,
		KickSetup,
		Kick,
		TelegraphSetup,
		TelegraphSwing,
		PunchSetup,
		FightSetup,
		FightWallSetup,
		RingSetup,
		RingSwing,
		SparkResume,
		KnockSetup,
		KnockHeavy,
		TokenRingSetup,
		Crawl,
		Uncrawl,
		Cleanup,
	};

	static TWeakObjectPtr<AThugCharacter> Foe;
	/** The fight camera shots' second thug (Foe is the first). */
	static TWeakObjectPtr<AThugCharacter> SecondFoe;
	/** token_ring.png's four, thinking. */
	static TArray<TWeakObjectPtr<AThugCharacter>> RingFoes;
	static TArray<TWeakObjectPtr<AThugCharacter>> Spawned;
	static TWeakObjectPtr<ACameraActor> ShotCamera;
	static TWeakObjectPtr<APointLight> FillLight;
	static FVector StreetFeet = FVector::ZeroVector;
	static FVector Along = FVector::ForwardVector;
	static FTransform KateStart;
	static bool bKateWasInvulnerable = false;
	static bool bHaveStreet = false;

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

	static bool Ground(UWorld* World, const FVector& XY, float FromZ, const TArray<const AActor*>& Ignore, FVector& Out)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeShotGround), false);
		Params.AddIgnoredActors(Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, FromZ - 6000.f), ECC_Visibility, Params))
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}

	static void Stand(ACharacter* Character, const FVector& Feet, float Yaw)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(Feet + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		Character->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}

	/** A camera at Eye looking at Target, with a soft fill light over the lens (the street is at night). */
	static void Frame(UWorld* World, APlayerController* PC, const FVector& Eye, const FVector& Target, float Fov)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACameraActor* Camera = ShotCamera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
			ShotCamera = Camera;
		}
		APointLight* Light = FillLight.Get();
		if (!Light)
		{
			Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform::Identity, Params);
			FillLight = Light;
			if (UPointLightComponent* Lamp = Light ? Light->PointLightComponent.Get() : nullptr)
			{
				Lamp->SetMobility(EComponentMobility::Movable);
				Lamp->SetIntensityUnits(ELightUnits::Lumens);
				Lamp->SetAttenuationRadius(900.f);
				Lamp->SetCastShadows(false);
			}
		}
		if (!Camera)
		{
			return;
		}
		Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(Fov);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
		if (Light)
		{
			Light->SetActorLocation(Eye + FVector(0.f, 0.f, 80.f));
			Light->PointLightComponent->SetIntensity(400.f);
		}
	}

	/** Back to her own camera, looking from YawOffset degrees round her facing, a little down. */
	static void OwnCamera(APlayerController* PC, AHawkeyeCharacter* Kate, float YawOffset)
	{
		PC->SetViewTarget(Kate);
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw + YawOffset, 0.f));
		if (APointLight* Light = FillLight.Get())
		{
			// Her own camera sits in the street lamps' light; only a little fill.
			Light->SetActorLocation(Kate->GetActorLocation() + FVector(0.f, 0.f, 250.f));
			Light->PointLightComponent->SetIntensity(80.f);
		}
	}

	/** A fresh thug from BP_Thug (a bat unless told otherwise), not thinking, at Feet facing Yaw. */
	static AThugCharacter* SpawnFoe(UWorld* World, const FVector& Feet, float Yaw, EThugWeapon Weapon = EThugWeapon::Bat)
	{
		UClass* ThugClass = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
		if (!ThugClass)
		{
			ThugClass = AThugCharacter::StaticClass();
		}
		const FTransform At(FRotator(0.f, Yaw, 0.f), Feet + FVector(0.f, 0.f, 100.f));
		AThugCharacter* Thug = World->SpawnActorDeferred<AThugCharacter>(ThugClass, At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Thug)
		{
			return nullptr;
		}
		Thug->Weapon = Weapon;
		UGameplayStatics::FinishSpawningActor(Thug, At);
		Thug->RefreshHeldWeapon();
		if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
		{
			Brain->SetThinkingEnabled(false);
		}
		Stand(Thug, Feet, Yaw);
		Spawned.Add(Thug);
		return Thug;
	}

	/** Kate on the street spot facing along it, and a fresh foe Distance in front of her facing her. */
	static bool FaceOff(UWorld* World, AHawkeyeCharacter* Kate, float Distance, float Yaw = 0.f,
		EThugWeapon Weapon = EThugWeapon::Bat)
	{
		if (!bHaveStreet)
		{
			return false;
		}
		const FVector Facing = FRotator(0.f, Along.Rotation().Yaw + Yaw, 0.f).Vector();
		Stand(Kate, StreetFeet, Facing.Rotation().Yaw);
		// The fight camera's pair too, so no thug from an earlier shot stands in this one's frame.
		for (TWeakObjectPtr<AThugCharacter>* Old : { &Foe, &SecondFoe })
		{
			if (AThugCharacter* Thug = Old->Get())
			{
				Thug->Destroy();
			}
			*Old = nullptr;
		}
		FVector FoeFeet;
		if (!Ground(World, StreetFeet + Facing * Distance, StreetFeet.Z + 200.f, { Kate }, FoeFeet))
		{
			FoeFeet = StreetFeet + Facing * Distance;
		}
		Foe = SpawnFoe(World, FoeFeet, (-Facing).Rotation().Yaw, Weapon);
		return Foe.IsValid();
	}

	/**
	 * Two alerted fists thugs in front of Kate (at Feet, facing Facing): one 320 cm out 25 degrees to her
	 * left, one 440 cm out 20 degrees to her right, both facing her. Old ones are removed.
	 */
	static bool StandPair(UWorld* World, AHawkeyeCharacter* Kate, const FVector& Feet, const FVector& Facing)
	{
		for (TWeakObjectPtr<AThugCharacter>* Old : { &Foe, &SecondFoe })
		{
			if (AThugCharacter* Thug = Old->Get())
			{
				Thug->Destroy();
			}
			*Old = nullptr;
		}
		Stand(Kate, Feet, Facing.Rotation().Yaw);
		const struct { float Distance; float Yaw; } Spots[] = { { 320.f, -25.f }, { 440.f, 20.f } };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FVector Out = FRotator(0.f, Facing.Rotation().Yaw + Spots[Index].Yaw, 0.f).Vector() * Spots[Index].Distance;
			FVector At;
			if (!Ground(World, Feet + Out, Feet.Z + 200.f, { Kate }, At))
			{
				At = Feet + Out;
			}
			AThugCharacter* Thug = SpawnFoe(World, At, (Feet - At).Rotation().Yaw, EThugWeapon::Fists);
			if (!Thug)
			{
				return false;
			}
			Thug->SetAlertState(EThugAlertState::Alerted);
			(Index == 0 ? Foe : SecondFoe) = Thug;
		}
		return true;
	}

	/** The nearest building face to the street spot within 15 m (a camera-blocking wall), its point and flat normal. */
	static bool FindWall(UWorld* World, const AHawkeyeCharacter* Kate, FVector& OutPoint, FVector& OutNormal)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeShotWall), false, Kate);
		for (const TWeakObjectPtr<AThugCharacter>& Weak : Spawned)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		const FVector Chest = StreetFeet + FVector(0.f, 0.f, 150.f);
		float Best = BIG_NUMBER;
		for (int32 Step = 0; Step < 24; ++Step)
		{
			const FVector Direction = FRotator(0.f, Step * 15.f, 0.f).Vector();
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Chest, Chest + Direction * 1500.f, ECC_Camera, Params)
				&& FMath::Abs(Hit.ImpactNormal.Z) < 0.3f && Hit.Distance < Best)
			{
				Best = Hit.Distance;
				OutPoint = Hit.ImpactPoint;
				OutNormal = Hit.ImpactNormal.GetSafeNormal2D();
			}
		}
		return Best < BIG_NUMBER;
	}

	/** Where Point lands on screen, as a fraction of the view from its centre (x right, y down), or "off screen". */
	static FString ScreenOffset(APlayerController* PC, const FVector& Point, bool* bOutOnScreen = nullptr)
	{
		FVector2D Screen = FVector2D::ZeroVector;
		int32 Width = 0;
		int32 Height = 0;
		PC->GetViewportSize(Width, Height);
		const bool bProjected = Width > 0 && Height > 0 && PC->ProjectWorldLocationToScreen(Point, Screen, false);
		const bool bOn = bProjected && Screen.X >= 0.f && Screen.Y >= 0.f && Screen.X <= Width && Screen.Y <= Height;
		if (bOutOnScreen)
		{
			*bOutOnScreen = bOn;
		}
		return bProjected ? FString::Printf(TEXT("(%+.2f, %+.2f)"), Screen.X / Width - 0.5f, Screen.Y / Height - 0.5f)
			: FString(TEXT("off screen"));
	}

	static void SetCrawl(UWorld* World, bool bCrawl)
	{
		UGameplayStatics::SetGlobalTimeDilation(World, bCrawl ? CrawlDilation : 1.f);
	}

	static FString Vec(const FVector& V) { return V.ToCompactString(); }

	/** Time as good as stopped: the spark's capture holds the frame the light landed. */
	static constexpr float StoppedDilation = 0.0001f;
	/** Set while hit_spark.png waits for the light to land; stops time in the tick the spark is asked for. */
	static FDelegateHandle SparkHook;
	static bool bSparkFrozen = false;
	static FVector SparkAt = FVector::ZeroVector;
	/** knockdown_dust.png: the world time he went down, and his feet's height standing just before. */
	static double KnockedAtWorld = -1.0;
	static float KnockFeetZ = 0.f;
	/** Where the thud's dust was asked for (zero until it is). */
	static FVector DustAt = FVector::ZeroVector;
	static FDelegateHandle DustHook;

	static void UnhookSpark(UWorld* World)
	{
		if (UHawkeyeVfxSubsystem* Vfx = World ? UHawkeyeVfxSubsystem::Find(World) : nullptr)
		{
			Vfx->OnRequested.Remove(SparkHook);
		}
		SparkHook.Reset();
	}

	/** Point's height on screen, px from the top (y down), or -1 off the view. */
	static float ScreenY(APlayerController* PC, const FVector& Point)
	{
		FVector2D Screen;
		return PC->ProjectWorldLocationToScreen(Point, Screen, false) ? Screen.Y : -1.f;
	}

	/** Her pelvis above her feet standing, cm, read just before the heavy or the kick is pressed. */
	static float StandingPelvis = 0.f;
	static constexpr float PelvisDropTolerance = 15.f;
	static constexpr float PelvisRiseTolerance = 20.f;
	static const TCHAR* HeavyMontagePrefix = TEXT("AM_Heavy_");
	/** The clip heavy_strike.png caught; heavy_strike_2.png must catch another. */
	static FString FirstHeavyMontage;
	/** Her set has a second heavy (heavy_strike_2.png is taken only then). */
	static bool bHaveSecondHeavy = true;
	static const TCHAR* KickMontagePrefix = TEXT("AM_Kick_");

	/** The pelvis bone above the bottom of the capsule, cm. */
	static float PelvisHeight(const ACharacter* Character)
	{
		const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
		if (!Body || Body->GetBoneIndex(TEXT("pelvis")) == INDEX_NONE)
		{
			return 0.f;
		}
		const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		return Body->GetBoneLocation(TEXT("pelvis")).Z - Feet;
	}

	/** Side on to her, like strike_pose.png, centred Ahead cm in front of her, the lens Back cm out. */
	static void FrameSideOn(UWorld* World, APlayerController* PC, const AHawkeyeCharacter* Kate, float Ahead, float Back)
	{
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		const FVector Chest = Kate->GetActorLocation() + Along * Ahead + FVector(0.f, 0.f, 25.f);
		Frame(World, PC, Chest + Side * Back + FVector(0.f, 0.f, 10.f), Chest - FVector(0.f, 0.f, 15.f), 50.f);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeMeleeShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeMeleeShot::Update()
{
	using namespace HawkeyeMeleeShots;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (!World || !Kate)
	{
		Test->AddError(TEXT("Melee shots: no world or no Kate."));
		return true;
	}
	AThugCharacter* Thug = Foe.Get();

	switch (static_cast<EShot>(Shot))
	{
	case EShot::Setup:
	{
		Spawned.Reset();
		Foe = nullptr;
		KateStart = Kate->GetActorTransform();
		bKateWasInvulnerable = Kate->GetHealthComponent()->IsInvulnerable();
		Kate->GetHealthComponent()->SetInvulnerable(true);
		Kate->StopAim();
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(false);
			}
		}
		HawkeyeFreezePartner(World);
		bHaveStreet = false;
		TActorIterator<APlayerStart> Start(World);
		if (Start)
		{
			const FVector Away = Start->GetActorForwardVector().GetSafeNormal2D();
			Along = FVector::CrossProduct(FVector::UpVector, Away);
			bHaveStreet = Ground(World, Start->GetActorLocation() - Away * 400.f, Start->GetActorLocation().Z + 3000.f, { Kate },
				StreetFeet);
			// Clint well out of the way.
			for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
			{
				if (AHawkeyeCharacter* Clint = It->GetPartner())
				{
					Stand(Clint, StreetFeet - Along * 1500.f, Along.Rotation().Yaw);
				}
			}
		}
		if (!bHaveStreet)
		{
			Test->AddWarning(TEXT("Melee shots: no street by the PlayerStart."));
		}
		break;
	}

	case EShot::ComboSetup:
		if (!FaceOff(World, Kate, 110.f))
		{
			Test->AddWarning(TEXT("combo_x3.png: could not stand a thug in front of her."));
			break;
		}
		OwnCamera(PC, Kate, 35.f);
		break;

	case EShot::ParrySetup:
	{
		if (!FaceOff(World, Kate, 130.f))
		{
			Test->AddWarning(TEXT("parry_flash.png: could not stand a thug in front of her."));
			break;
		}
		// "In front" is her view: the control rotation, whatever camera the shot looks through.
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw, 0.f));
		// Three-quarters from behind her, as she sees it: the ring faces along the line between them.
		const FVector Mid = (Foe->GetActorLocation() + Kate->GetActorLocation()) * 0.5f;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		Frame(World, PC, Mid - Along * 300.f + Side * 260.f + FVector(0.f, 0.f, 70.f), Mid + FVector(0.f, 0.f, 25.f), 55.f);
		break;
	}

	case EShot::ParrySwing:
		if (Thug)
		{
			Thug->GetMeleeComponent()->StartAttack(Thug->GetMeleeAttack());
		}
		break;

	case EShot::Parry:
	{
		const bool bParried = Kate->TryParry();
		Test->AddInfo(FString::Printf(TEXT("parry_flash.png: parried %d (%d in all); %s staggered %.2f s, swinging %d."),
			bParried ? 1 : 0, Kate->GetParryCount(), *GetNameSafe(Thug), Thug ? Thug->GetStaggerRemaining() : 0.f,
			Thug && Thug->GetMeleeComponent()->IsAttacking() ? 1 : 0));
		if (!bParried)
		{
			Test->AddWarning(TEXT("parry_flash.png: the parry did not take."));
		}
		break;
	}

	case EShot::FinisherSetup:
		if (!FaceOff(World, Kate, 140.f, 0.f) || !Foe.IsValid())
		{
			Test->AddWarning(TEXT("finisher_mid.png: could not stand a thug in front of her."));
			break;
		}
		Foe->Parried(Kate, 2.f);
		OwnCamera(PC, Kate, 40.f);
		break;

	case EShot::Finisher:
		if (!Kate->TryFinisher())
		{
			Test->AddWarning(TEXT("finisher_mid.png: the finisher did not start."));
		}
		break;

	case EShot::LeanSetup:
	{
		// She stands at his left; he faces along the street; the camera is in front of him.
		if (!FaceOff(World, Kate, 110.f, 90.f) || !Foe.IsValid())
		{
			Test->AddWarning(TEXT("hit_lean.png: could not stand a thug beside her."));
			break;
		}
		AThugCharacter* Him = Foe.Get();
		Him->SetActorRotation(FRotator(0.f, Kate->GetActorRotation().Yaw - 90.f, 0.f));
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw, 0.f));
		const FVector Front = Him->GetActorForwardVector().GetSafeNormal2D();
		// Square on from in front of him, far enough back for both of them.
		const FVector Between = (Him->GetActorLocation() + Kate->GetActorLocation()) * 0.5f;
		Frame(World, PC, Between + Front * 420.f + FVector(0.f, 0.f, 20.f), Between + FVector(0.f, 0.f, 10.f), 45.f);
		break;
	}

	case EShot::LeanHit:
		if (Thug)
		{
			Thug->GetHealthComponent()->ApplyDamage(1.f, Kate);
		}
		break;

	case EShot::StrikeSetup:
	{
		if (!FaceOff(World, Kate, 220.f))
		{
			Test->AddWarning(TEXT("strike_pose.png: no street spot."));
			break;
		}
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw, 0.f));
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
		const FVector Chest = Kate->GetActorLocation() + Along * 35.f + FVector(0.f, 0.f, 35.f);
		Frame(World, PC, Chest + Side * 380.f + FVector(0.f, 0.f, 10.f), Chest - FVector(0.f, 0.f, 15.f), 50.f);
		break;
	}

	case EShot::Strike:
		Kate->StartLightAttack();
		break;

	case EShot::HeavySetup:
		bHaveSecondHeavy = Kate->GetCombatAnimSet() && Kate->GetCombatAnimSet()->GetVariantCount(ECombatAnimRole::Heavy) > 1;
		// Far enough that the soft lock leaves him be: the clip's own root motion, no warp toward him.
		if (!FaceOff(World, Kate, 450.f))
		{
			Test->AddWarning(TEXT("heavy_strike.png: no street spot."));
			break;
		}
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw, 0.f));
		FrameSideOn(World, PC, Kate, 45.f, 400.f);
		break;

	case EShot::Heavy:
		StandingPelvis = PelvisHeight(Kate);
		if (!Kate->StartHeavyAttack())
		{
			Test->AddError(TEXT("heavy_strike.png: the heavy did not start."));
		}
		break;

	case EShot::KickSetup:
		if (!FaceOff(World, Kate, 450.f))
		{
			Test->AddWarning(TEXT("kick.png: no street spot."));
			break;
		}
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw, 0.f));
		FrameSideOn(World, PC, Kate, 45.f, 400.f);
		break;

	case EShot::Kick:
	{
		// Nothing in the game presses the Kick role on its own (it stands in for a missing Light3), so the
		// shot swings it straight through her melee component: a light's numbers with the Kick clip.
		StandingPelvis = PelvisHeight(Kate);
		FHawkeyeMeleeAttack Kick;
		Kick.Name = FName(TEXT("kick"));
		Kick.AnimRole = ECombatAnimRole::Kick;
		Kick.Damage = 0.f;
		Kick.WindupSeconds = 0.1f;
		Kick.RecoverSeconds = 0.2f;
		Kate->GetMeleeComponent()->SetNextAttackDirection(Kate->GetActorForwardVector());
		if (!Kate->GetMeleeComponent()->StartAttack(Kick))
		{
			Test->AddError(TEXT("kick.png: the kick did not start."));
		}
		break;
	}

	case EShot::TelegraphSetup:
		// A hurt fists thug square in front of her, inside the parry's 250 cm: his bar, the parry line and the
		// glyph stacked over his head.
		if (!FaceOff(World, Kate, 130.f, 0.f, EThugWeapon::Fists))
		{
			Test->AddWarning(TEXT("telegraph_glyph.png: could not stand a thug in front of her."));
			break;
		}
		if (Foe.IsValid())
		{
			Foe->GetHealthComponent()->ApplyDamage(20.f, Kate);
		}
		OwnCamera(PC, Kate, 20.f);
		break;

	case EShot::TelegraphSwing:
		if (Thug && !Thug->GetMeleeComponent()->StartAttack(Thug->GetMeleeAttack()))
		{
			Test->AddWarning(TEXT("telegraph_glyph.png: his swing did not start."));
		}
		break;

	case EShot::PunchSetup:
		// Side on to the pair of them, the same thug: his next swings are the next clip variants.
		if (Thug)
		{
			PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw, 0.f));
			FrameSideOn(World, PC, Kate, 65.f, 420.f);
		}
		break;

	case EShot::FightSetup:
		if (!bHaveStreet || !StandPair(World, Kate, StreetFeet, Along))
		{
			Test->AddWarning(TEXT("fight_camera.png: could not stand two thugs in the street."));
			break;
		}
		OwnCamera(PC, Kate, 0.f);
		break;

	case EShot::FightWallSetup:
	{
		// Her back to the nearest building, 150 cm off it, facing out: the boom points into the wall.
		FVector Wall;
		FVector Normal;
		FVector Feet;
		if (!bHaveStreet || !FindWall(World, Kate, Wall, Normal)
			|| !Ground(World, Wall + Normal * 150.f, StreetFeet.Z + 200.f, { Kate }, Feet)
			|| !StandPair(World, Kate, Feet, Normal))
		{
			Test->AddWarning(TEXT("fight_camera_wall.png: no building face near the street spot."));
			break;
		}
		OwnCamera(PC, Kate, 0.f);
		Test->AddInfo(FString::Printf(TEXT("fight_camera_wall.png: Kate %.0f cm off the wall at %s, facing away from it."),
			FVector::Dist2D(Feet, Wall), *Wall.ToCompactString()));
		break;
	}

	case EShot::RingSetup:
		// A hurt bat thug inside the assist's 350 cm: his bar shows, and the swing below is at him.
		if (!FaceOff(World, Kate, 170.f) || !Foe.IsValid())
		{
			Test->AddWarning(TEXT("target_ring.png: could not stand a thug in front of her."));
			break;
		}
		Foe->GetHealthComponent()->ApplyDamage(20.f, Kate);
		OwnCamera(PC, Kate, 30.f);
		break;

	case EShot::RingSwing:
	{
		// Time stops in the tick her light lands: the hook runs inside the spark's own request.
		UnhookSpark(World);
		bSparkFrozen = false;
		SparkAt = FVector::ZeroVector;
		if (UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(World))
		{
			TWeakObjectPtr<UWorld> WeakWorld(World);
			SparkHook = Vfx->OnRequested.AddLambda([WeakWorld](FName Event, const FVector& At)
			{
				if (Event == UHawkeyeVfxSubsystem::MeleeSparkEvent && !bSparkFrozen && WeakWorld.IsValid())
				{
					bSparkFrozen = true;
					SparkAt = At;
					UGameplayStatics::SetGlobalTimeDilation(WeakWorld.Get(), StoppedDilation);
				}
			});
		}
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw + 30.f, 0.f));
		if (!Kate->StartLightAttack())
		{
			Test->AddError(TEXT("target_ring.png: her light did not start."));
		}
		break;
	}

	case EShot::SparkResume:
		// Back to full speed for the light to land, unless it already has (and stopped time).
		if (!bSparkFrozen)
		{
			SetCrawl(World, false);
		}
		break;

	case EShot::KnockSetup:
		if (!FaceOff(World, Kate, 170.f) || !Foe.IsValid())
		{
			Test->AddWarning(TEXT("knockdown_dust.png: could not stand a thug in front of her."));
			break;
		}
		OwnCamera(PC, Kate, 45.f);
		break;

	case EShot::KnockHeavy:
		DustAt = FVector::ZeroVector;
		if (UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(World))
		{
			Vfx->OnRequested.Remove(DustHook);
			DustHook = Vfx->OnRequested.AddLambda([](FName Event, const FVector& At)
			{
				if (Event == UHawkeyeVfxSubsystem::KnockdownDustEvent)
				{
					DustAt = At;
				}
			});
		}
		KnockedAtWorld = -1.0;
		KnockFeetZ = Thug ? Thug->GetActorLocation().Z - Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		PC->SetControlRotation(FRotator(-10.f, Kate->GetActorRotation().Yaw + 45.f, 0.f));
		if (!Kate->StartHeavyAttack())
		{
			Test->AddError(TEXT("knockdown_dust.png: her heavy did not start."));
		}
		break;

	case EShot::Crawl:
		SetCrawl(World, true);
		break;

	case EShot::Uncrawl:
		SetCrawl(World, false);
		break;

	case EShot::TokenRingSetup:
	{
		for (TWeakObjectPtr<AThugCharacter>* Old : { &Foe, &SecondFoe })
		{
			if (AThugCharacter* Made = Old->Get())
			{
				Made->Destroy();
			}
			*Old = nullptr;
		}
		Stand(Kate, StreetFeet, Along.Rotation().Yaw);
		RingFoes.Reset();
		for (const float Yaw : { -35.f, 35.f, 145.f, -145.f })
		{
			const FVector Out = FRotator(0.f, Along.Rotation().Yaw + Yaw, 0.f).Vector() * 700.f;
			FVector At;
			if (!Ground(World, StreetFeet + Out, StreetFeet.Z + 200.f, { Kate }, At))
			{
				At = StreetFeet + Out;
			}
			AThugCharacter* Made = SpawnFoe(World, At, (StreetFeet - At).Rotation().Yaw, EThugWeapon::Fists);
			AThugAIController* Brain = Made ? Cast<AThugAIController>(Made->GetController()) : nullptr;
			if (!Brain)
			{
				continue;
			}
			// Behind her he cannot see her: the shot is about where they stand, not about losing her.
			Brain->LoseTargetSeconds = 60.f;
			Brain->SetThinkingEnabled(true);
			Brain->SetTarget(Kate);
			Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
			RingFoes.Add(Made);
		}
		const FVector Chest = StreetFeet + FVector(0.f, 0.f, 100.f);
		Frame(World, PC, Chest - Along * 520.f + FVector(0.f, 0.f, 820.f), Chest, 70.f);
		break;
	}

	case EShot::Cleanup:
		UnhookSpark(World);
		if (UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(World))
		{
			Vfx->OnRequested.Remove(DustHook);
		}
		DustHook.Reset();
		SetCrawl(World, false);
		PC->SetViewTarget(Kate);
		if (ACameraActor* Camera = ShotCamera.Get())
		{
			Camera->Destroy();
		}
		if (APointLight* Light = FillLight.Get())
		{
			Light->Destroy();
		}
		for (const TWeakObjectPtr<AThugCharacter>& Weak : Spawned)
		{
			if (AThugCharacter* Made = Weak.Get())
			{
				Made->Destroy();
			}
		}
		Spawned.Reset();
		RingFoes.Reset();
		Foe = nullptr;
		SecondFoe = nullptr;
		Kate->TeleportTo(KateStart.GetLocation(), KateStart.Rotator(), false, true);
		Kate->GetHealthComponent()->Heal(1000.f);
		Kate->GetHealthComponent()->SetInvulnerable(bKateWasInvulnerable);
		break;
	}
	return true;
}

/**
 * Three lights chained into the foe the way a player who knows the rhythm would: the first at once,
 * each next one the moment the last one lands (Kate's combo counter going up), which the chain holds
 * until the clip's combo window opens, or the 0.35 s timer runs without a clip. Done when the third
 * has landed (or on the timeout). Logs when each was pressed and when it landed.
 */
class FHawkeyeMeleeComboChain : public IAutomationLatentCommand
{
public:
	FHawkeyeMeleeComboChain(FAutomationTestBase* InTest, int32 InPresses, float InTimeout)
		: Test(InTest), Presses(InPresses), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		using namespace HawkeyeMeleeShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!World || !Kate || !Foe.IsValid())
		{
			Test->AddError(TEXT("combo_x3.png: no world, no Kate or no foe for the chain."));
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		if (Start < 0.0)
		{
			Start = Now;
			BaseCount = Kate->GetComboCount();
		}
		const float Elapsed = static_cast<float>(Now - Start);
		const int32 Count = Kate->GetComboCount() - BaseCount;
		if (Count > Landed)
		{
			Landed = Count;
			Log += FString::Printf(TEXT(" landed %d at %.2f s;"), Count, Elapsed);
		}
		if (Landed >= Presses)
		{
			Test->AddInfo(FString::Printf(TEXT("combo_x3.png: chain of %d:%s"), Presses, *Log));
			return true;
		}
		// The next light goes once the last one has landed; a press it will not take yet is tried again.
		if (Pressed == Landed && Pressed < Presses && Kate->StartLightAttack())
		{
			++Pressed;
			Log += FString::Printf(TEXT(" pressed %d at %.2f s (%s);"), Pressed, Elapsed,
				*Kate->GetMeleeComponent()->GetCurrentAttack().Name.ToString());
		}
		if (Elapsed >= Timeout)
		{
			Test->AddInfo(FString::Printf(TEXT("combo_x3.png: chain timed out after %.1f s:%s"), Timeout, *Log));
			return true;
		}
		return false;
	}

private:
	FAutomationTestBase* Test;
	int32 Presses;
	float Timeout;
	int32 Pressed = 0;
	int32 Landed = 0;
	int32 BaseCount = 0;
	double Start = -1.0;
	FString Log;
};

/** Waits (up to Timeout) until Kate's swing has struck: its hit window opened (a clip) or its wind-up ran out. */
class FHawkeyeMeleeWaitForStrike : public IAutomationLatentCommand
{
public:
	FHawkeyeMeleeWaitForStrike(FAutomationTestBase* InTest, float InTimeout, FString InLabel = TEXT("strike_pose.png"))
		: Test(InTest), Timeout(InTimeout), Label(MoveTemp(InLabel)) {}

	virtual bool Update() override
	{
		using namespace HawkeyeMeleeShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		const UMeleeComponent* Melee = Kate ? Kate->GetMeleeComponent() : nullptr;
		if (!Melee)
		{
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		if (Start < 0.0)
		{
			Start = Now;
		}
		if (Melee->IsWindingUp() && Now - Start < Timeout)
		{
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("%s: struck %.2f s after the press (%s)."), *Label, Now - Start,
			Melee->IsHitFromNotify() ? TEXT("the clip's hit window") : TEXT("the wind-up timer")));
		return true;
	}

private:
	FAutomationTestBase* Test;
	float Timeout;
	FString Label;
	double Start = -1.0;
};

/** Waits (up to Timeout) for the moment a shot is about, then reports it. */
class FHawkeyeMeleeWait : public IAutomationLatentCommand
{
	/** token_ring.png: who holds her token, and where each thug stands and faces. Off the ring is a warning. */
	void ReportTokenRing(UWorld* World, const AHawkeyeCharacter* Kate) const
	{
		const UCombatTokenSubsystem* Tokens = UCombatTokenSubsystem::Get(World);
		const FHawkeyeTokenBudgetView View = Tokens ? Tokens->GetBudgetView(Kate, EHawkeyeAttackKind::Melee)
			: FHawkeyeTokenBudgetView();
		FString Line = FString::Printf(TEXT("token_ring.png: %d holding, %d in line, %d cooling"), View.Holders.Num(),
			View.Waiters.Num(), View.Idle.Num());
		for (const TWeakObjectPtr<AThugCharacter>& Weak : HawkeyeMeleeShots::RingFoes)
		{
			const AThugCharacter* Thug = Weak.Get();
			if (!Thug)
			{
				continue;
			}
			const FVector To = Kate->GetActorLocation() - Thug->GetActorLocation();
			const float Facing = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(Thug->GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()), -1.f, 1.f)));
			const bool bHolds = View.Holders.Contains(Thug);
			Line += FString::Printf(TEXT("; %s %s %.0f cm, %.0f deg round her, facing %.0f deg off her, %.0f cm/s"),
				*Thug->GetName(), bHolds ? TEXT("HOLDS") : TEXT("waits"), To.Size2D(),
				FRotator::NormalizeAxis((-To).Rotation().Yaw - Kate->GetActorRotation().Yaw), Facing, Thug->GetVelocity().Size2D());
			if (!bHolds && FMath::Abs(To.Size2D() - 300.f) > 120.f)
			{
				Test->AddWarning(FString::Printf(TEXT("token_ring.png: %s waits %.0f cm from her, off the 300 cm ring."),
					*Thug->GetName(), To.Size2D()));
			}
		}
		Test->AddInfo(Line);
	}

public:
	FHawkeyeMeleeWait(FAutomationTestBase* InTest, FString InLabel, float InTimeout)
		: Test(InTest), Label(MoveTemp(InLabel)), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		using namespace HawkeyeMeleeShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!World || !Kate)
		{
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		if (Start < 0.0)
		{
			Start = Now;
		}
		const bool bTimedOut = Now - Start >= Timeout;
		const AThugCharacter* Thug = Foe.Get();
		const USkeletalMeshComponent* Body = Kate->GetMesh();
		const UHawkeyeBowIKAnimInstance* Hands = Kate->GetBowComponent() ? Kate->GetBowComponent()->GetHandsIKInstance() : nullptr;
		const USpringArmComponent* Arm = Kate->GetCameraBoom();

		if (Label == TEXT("finisher_mid.png"))
		{
			const UFinisherComponent* Finisher = Kate->GetFinisherComponent();
			const bool bReady = Finisher && Finisher->IsPerformingFinisher() && Finisher->IsSlowing() && Finisher->GetElapsed() >= 0.3f;
			if (!bReady && !bTimedOut)
			{
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("finisher_mid.png: %.2f s in, slowing %d, time dilation %.2f, camera push %.2f (arm %.0f cm), ")
				TEXT("invulnerable %d, locked %d, thug alive %d."),
				Finisher ? Finisher->GetElapsed() : 0.f, Finisher && Finisher->IsSlowing() ? 1 : 0,
				World->GetWorldSettings()->TimeDilation, Finisher ? Finisher->GetCameraPushAlpha() : 0.f,
				Arm ? Arm->TargetArmLength : 0.f, Kate->GetHealthComponent()->IsInvulnerable() ? 1 : 0,
				Kate->IsLockedOutByTakedown() ? 1 : 0, Thug && Thug->GetHealthComponent()->IsAlive() ? 1 : 0));
			if (!bReady)
			{
				Test->AddWarning(TEXT("finisher_mid.png: never caught the finisher in its slow motion."));
			}
			return true;
		}
		if (Label == TEXT("combo_x3.png"))
		{
			Test->AddInfo(FString::Printf(TEXT("combo_x3.png: counter x%d, chain next step %d, swing %s, thug health %.0f."),
				Kate->GetComboCount(), Kate->GetCombo().GetNextStep(), *Kate->GetMeleeComponent()->GetCurrentAttack().Name.ToString(),
				Thug ? Thug->GetHealthComponent()->GetCurrentHealth() : -1.f));
			if (Kate->GetComboCount() != 3)
			{
				// With a Light1 clip the chain is the clip's to time; the inputs above follow it, so a
				// short count is the clips' windows not letting a chain through.
				const FString Message = FString::Printf(TEXT("combo_x3.png: the counter reads x%d, not x3."), Kate->GetComboCount());
				if (UCombatAnimSet::Resolve(Kate->GetCombatAnimSet(), ECombatAnimRole::Light1))
				{
					Test->AddError(Message);
				}
				else
				{
					Test->AddWarning(Message);
				}
			}
			return true;
		}
		if (Label == TEXT("hit_lean.png"))
		{
			const UHawkeyeBowIKAnimInstance* ThugHands = Thug && Thug->GetBowComponent() ? Thug->GetBowComponent()->GetHandsIKInstance() : nullptr;
			Test->AddInfo(FString::Printf(TEXT("hit_lean.png: hit from %s, lean alpha %.2f toward %s; thug graph %s, HitLean %s."),
				Thug ? *UEnum::GetValueAsString(Thug->GetLastHitDirection()) : TEXT("-"), Thug ? Thug->GetHitLean().GetAlpha() : 0.f,
				Thug ? *Vec(Thug->GetHitLean().GetDirection()) : TEXT("-"), ThugHands ? *ThugHands->GetClass()->GetName() : TEXT("none"),
				ThugHands ? *ThugHands->HitLean.ToCompactString() : TEXT("-")));
			if (!ThugHands)
			{
				Test->AddWarning(TEXT("hit_lean.png: the thug runs no bow-IK graph, so nothing leans him."));
			}
			return true;
		}
		if (Label == TEXT("heavy_strike.png") || Label == TEXT("heavy_strike_2.png") || Label == TEXT("kick.png"))
		{
			const bool bHeavy = Label != TEXT("kick.png");
			const bool bSecondHeavy = Label == TEXT("heavy_strike_2.png");
			const UMeleeComponent* Melee = Kate->GetMeleeComponent();
			const FString Name = GetNameSafe(Melee ? Melee->GetCurrentMontage() : nullptr);
			const float Pelvis = PelvisHeight(Kate);
			const float Feet = Kate->GetActorLocation().Z - Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector Head = Body ? Body->GetBoneLocation(TEXT("head")) : FVector::ZeroVector;
			// Each limb as (forward, right, up) from her feet, in her actor's frame; and the way her hips face.
			auto Limb = [&](const TCHAR* Bone)
			{
				const FVector At = Body ? Body->GetBoneLocation(Bone) : Kate->GetActorLocation();
				const FVector Local = Kate->GetActorTransform().InverseTransformVectorNoScale(At - Kate->GetActorLocation());
				return FString::Printf(TEXT("%s %.0f/%.0f/%.0f"), Bone, Local.X, Local.Y, At.Z - Feet);
			};
			const FVector HipsAcross = Body ? Body->GetBoneLocation(TEXT("thigh_r")) - Body->GetBoneLocation(TEXT("thigh_l")) : FVector::RightVector;
			const FVector HipsFacing = FVector::CrossProduct(HipsAcross.GetSafeNormal2D(), FVector::UpVector);
			const float HipsYaw = FMath::FindDeltaAngleDegrees(Kate->GetActorRotation().Yaw, HipsFacing.Rotation().Yaw);
			Test->AddInfo(FString::Printf(TEXT("%s: montage %s, hit window open %d; pelvis %.0f cm up (standing %.0f), head %.0f cm up; ")
				TEXT("hips face %.0f deg off her facing; %s, %s, %s, %s (forward/right/up cm)."),
				*Label, *Name, Melee && Melee->IsHitWindowOpen() ? 1 : 0, Pelvis, StandingPelvis, Head.Z - Feet, HipsYaw,
				*Limb(TEXT("hand_r")), *Limb(TEXT("hand_l")), *Limb(TEXT("foot_r")), *Limb(TEXT("foot_l"))));
			const bool bRightClip = Name.StartsWith(bHeavy ? HeavyMontagePrefix : KickMontagePrefix)
				&& !(bSecondHeavy && bHaveSecondHeavy && Name == FirstHeavyMontage);
			if (!bRightClip)
			{
				Test->AddError(FString::Printf(TEXT("%s: plays %s, not %s."), *Label, *Name,
					bSecondHeavy ? *FString::Printf(TEXT("an AM_Heavy_ clip other than %s"), *FirstHeavyMontage)
						: bHeavy ? TEXT("an AM_Heavy_ clip") : TEXT("an AM_Kick_ clip")));
			}
			if (Label == TEXT("heavy_strike.png"))
			{
				FirstHeavyMontage = Name;
			}
			const float Off = Pelvis - StandingPelvis;
			if (Off < -PelvisDropTolerance || Off > PelvisRiseTolerance)
			{
				Test->AddError(FString::Printf(TEXT("%s: her pelvis is %.0f cm up, %.0f cm off her standing %.0f (allowed %.0f down, %.0f up)."),
					*Label, Pelvis, Off, StandingPelvis, PelvisDropTolerance, PelvisRiseTolerance));
			}
			return true;
		}
		if (Label == TEXT("telegraph_glyph.png") || Label == TEXT("thug_punch.png") || Label == TEXT("thug_punch_2.png"))
		{
			const UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
			const float Elapsed = Thug ? UHawkeyeMeleeRules::GetTelegraphElapsed(Thug) : -1.f;
			const float Windup = Melee ? Melee->GetCurrentAttack().WindupSeconds : 0.f;
			const USkeletalMeshComponent* His = Thug ? Thug->GetMesh() : nullptr;
			auto Limb = [&](const TCHAR* Bone)
			{
				if (!His || His->GetBoneIndex(Bone) == INDEX_NONE)
				{
					return FString::Printf(TEXT("%s -"), Bone);
				}
				const FVector At = His->GetBoneLocation(Bone);
				const FVector Local = Thug->GetActorTransform().InverseTransformVectorNoScale(At - Thug->GetActorLocation());
				const float Feet = Thug->GetActorLocation().Z - Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				return FString::Printf(TEXT("%s %.0f/%.0f/%.0f"), Bone, Local.X, Local.Y, At.Z - Feet);
			};
			const bool bParry = Thug && Kate->CanParryNow(Thug);
			Test->AddInfo(FString::Printf(TEXT("%s: %s plays %s (variant %d), %.2f s into his %.2f s telegraph, winding up %d, parry now %d, ")
				TEXT("glyph scale %.2f; %s, %s (forward/right/up cm)."),
				*Label, *GetNameSafe(Thug), *GetNameSafe(Melee ? Melee->GetCurrentMontage() : nullptr),
				Melee ? Melee->GetPickedVariantIndex() + 1 : 0, Elapsed, Windup, Melee && Melee->IsWindingUp() ? 1 : 0, bParry ? 1 : 0,
				HawkeyeCombatReadability::ComputeTelegraphLook(Elapsed, Windup).Scale, *Limb(TEXT("hand_r")), *Limb(TEXT("hand_l"))));
			if (!Melee || !Melee->IsWindingUp())
			{
				Test->AddError(FString::Printf(TEXT("%s: he is not winding up."), *Label));
			}
			if (Label == TEXT("telegraph_glyph.png") && Thug && His && His->GetBoneIndex(TEXT("head")) != INDEX_NONE)
			{
				// Screen px (y down) from the top of his head (the head bone plus 12 cm) up to where the stack sits.
				const float HeadTop = ScreenY(PC, His->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, 12.f));
				const bool bBar = Thug->GetHealthBarAlpha(FVector::Dist(Thug->GetActorLocation(), Kate->GetActorLocation())) > 0.f;
				Test->AddInfo(FString::Printf(TEXT("telegraph_glyph.png: the stack's anchor (his bar's bottom) is %.0f px over the top ")
					TEXT("of his head (the capsule anchor the bar had is %.0f px over it); bar showing %d, %.0f%% health."),
					HeadTop - ScreenY(PC, Thug->GetGlyphLocation()), HeadTop - ScreenY(PC, Thug->GetOverheadLocation()), bBar ? 1 : 0,
					Thug->GetHealthComponent()->GetHealthPercent() * 100.f));
				if (!bBar)
				{
					Test->AddError(TEXT("telegraph_glyph.png: the hurt thug's bar should show under the glyph."));
				}
			}
			if (Label == TEXT("telegraph_glyph.png") && !bParry)
			{
				Test->AddError(TEXT("telegraph_glyph.png: a thug 130 cm in front in his wind-up should be parryable (the line under the glyph)."));
			}
			return true;
		}
		if (Label == TEXT("fight_camera.png") || Label == TEXT("fight_camera_wall.png"))
		{
			const UCameraComponent* Lens = Kate->GetFollowCamera();
			const float LensDistance = Lens && Arm ? FVector::Dist(Lens->GetComponentLocation(), Arm->GetComponentLocation()) : 0.f;
			bool bHeadOn = false;
			bool bFeetOn = false;
			const FVector Feet = Kate->GetActorLocation() - FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			// The top of her head (the head bone plus a little) and her feet.
			const bool bHasHead = Body && Body->GetBoneIndex(TEXT("head")) != INDEX_NONE;
			const FVector HeadTop = bHasHead ? Body->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, 12.f) : Feet + FVector(0.f, 0.f, 170.f);
			const FString Head = ScreenOffset(PC, HeadTop, &bHeadOn);
			const FString Toes = ScreenOffset(PC, Feet, &bFeetOn);
			const AThugCharacter* Second = SecondFoe.Get();
			const bool bHidden = Body && (Body->bOwnerNoSee || !Body->IsVisible());
			Test->AddInfo(FString::Printf(TEXT("%s: %d alerted thugs engaged, fight alpha %.2f; boom target %.0f cm (hip %.0f), lens %.0f cm ")
				TEXT("from the pivot, lens pitch %.1f deg; Kate hidden %d, her head %s, feet %s; thugs %s and %s."),
				*Label, Kate->GetEngagedThugCount(), Kate->GetFightCameraAlpha(), Arm ? Arm->TargetArmLength : 0.f,
				Kate->ComputeCameraTargets(false).ArmLength, LensDistance, Lens ? Lens->GetRelativeRotation().Pitch : 0.f,
				bHidden ? 1 : 0, *Head, *Toes, Thug ? *ScreenOffset(PC, Thug->GetActorLocation()) : TEXT("-"),
				Second ? *ScreenOffset(PC, Second->GetActorLocation()) : TEXT("-")));
			if (Kate->GetFightCameraAlpha() < 0.99f)
			{
				Test->AddError(FString::Printf(TEXT("%s: the fight camera is only %.2f in."), *Label, Kate->GetFightCameraAlpha()));
			}
			if (bHidden || !bHeadOn || !bFeetOn)
			{
				Test->AddError(FString::Printf(TEXT("%s: Kate is not framed (hidden %d, head on screen %d, feet on screen %d)."), *Label,
					bHidden ? 1 : 0, bHeadOn ? 1 : 0, bFeetOn ? 1 : 0));
			}
			return true;
		}
		if (Label == TEXT("token_ring.png"))
		{
			ReportTokenRing(World, Kate);
			return true;
		}
		if (Label == TEXT("target_ring.png"))
		{
			const FHawkeyeTargetMarker& Marker = Kate->GetMeleeTargetMarker();
			const float FromHer = Thug ? FVector::Dist(Thug->GetActorLocation(), Kate->GetActorLocation()) : 0.f;
			const float BarAlpha = Thug ? Thug->GetHealthBarAlpha(FromHer) : 0.f;
			const FVector Feet = Thug ? Thug->GetActorLocation() - FVector(0.f, 0.f, Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())
				: FVector::ZeroVector;
			Test->AddInfo(FString::Printf(TEXT("target_ring.png: marker on %s at %.2f; her swing %s winding up %d, landed %d; ")
				TEXT("his bar %.2f at %.0f%% health, %.0f cm from her; his feet at %s on screen."),
				*GetNameSafe(Marker.GetTarget()), Marker.GetAlpha(), *Kate->GetMeleeComponent()->GetCurrentAttack().Name.ToString(),
				Kate->GetMeleeComponent()->IsWindingUp() ? 1 : 0, bSparkFrozen ? 1 : 0, BarAlpha,
				Thug ? Thug->GetHealthComponent()->GetHealthPercent() * 100.f : 0.f, FromHer, Thug ? *ScreenOffset(PC, Feet) : TEXT("-")));
			if (!Thug || Marker.GetTarget() != Thug || Marker.GetAlpha() < 0.99f)
			{
				Test->AddError(TEXT("target_ring.png: the marker is not full on the thug she swung at."));
			}
			if (BarAlpha <= 0.f)
			{
				Test->AddError(TEXT("target_ring.png: his health bar is not showing."));
			}
			return true;
		}
		if (Label == TEXT("strike_pose.png"))
		{
			const FVector Hand = Body ? Body->GetSocketLocation(TEXT("hand_r")) : FVector::ZeroVector;
			const FVector Target = Kate->GetActorTransform().TransformPosition(Kate->GetStrikePose().Settings.LightHand);
			Test->AddInfo(FString::Printf(TEXT("strike_pose.png: pose %s alpha %.2f; arm alpha %.2f; hand_r %.0f cm in front, %.0f cm up, ")
				TEXT("%.1f cm from the target."),
				*UEnum::GetValueAsString(Kate->GetStrikePose().GetPose()), Kate->GetStrikePose().GetAlpha(), Hands ? Hands->RightArmAlpha : -1.f,
				FVector::DotProduct(Hand - Kate->GetActorLocation(), Kate->GetActorForwardVector()), Hand.Z - Kate->GetActorLocation().Z,
				FVector::Dist(Hand, Target)));
			if (!Hands)
			{
				Test->AddWarning(TEXT("strike_pose.png: Kate runs no bow-IK graph."));
			}
			return true;
		}
		return true;
	}

private:
	FAutomationTestBase* Test;
	FString Label;
	float Timeout;
	double Start = -1.0;
};

/**
 * Waits (up to Timeout) for the light to land: the spark hook stops time in that tick. Reports where the spark
 * went against him and on screen, then leaves time stopped for the capture (crawling if it never came).
 */
class FHawkeyeMeleeWaitForSpark : public IAutomationLatentCommand
{
public:
	FHawkeyeMeleeWaitForSpark(FAutomationTestBase* InTest, float InTimeout) : Test(InTest), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		using namespace HawkeyeMeleeShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (!World || !Kate)
		{
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		if (Start < 0.0)
		{
			Start = Now;
		}
		if (!bSparkFrozen && Now - Start < Timeout)
		{
			return false;
		}
		UnhookSpark(World);
		if (!bSparkFrozen)
		{
			SetCrawl(World, true);
			Test->AddError(FString::Printf(TEXT("hit_spark.png: no melee spark within %.1f s (the light missed)."), Timeout));
			return true;
		}
		const AThugCharacter* Thug = Foe.Get();
		const UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(World);
		const FVector Camera = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		const float Feet = Thug ? Thug->GetActorLocation().Z - Thug->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
		Test->AddInfo(FString::Printf(TEXT("hit_spark.png: the spark %.2f s after the press at %s, %.0f cm from his capsule's axis, ")
			TEXT("%.0f cm over his feet, %.0f cm from the lens, %s on screen; %d melee sparks asked for, time dilation %.4f; ")
			TEXT("his health %.0f."),
			Now - Start, *Vec(SparkAt), Thug ? FVector::Dist2D(SparkAt, Thug->GetActorLocation()) : 0.f, SparkAt.Z - Feet,
			FVector::Dist(SparkAt, Camera), *ScreenOffset(PC, SparkAt), Vfx ? Vfx->GetRequestCount(UHawkeyeVfxSubsystem::MeleeSparkEvent) : -1,
			World->GetWorldSettings()->TimeDilation, Thug ? Thug->GetHealthComponent()->GetCurrentHealth() : -1.f));
		return true;
	}

private:
	FAutomationTestBase* Test;
	float Timeout;
	double Start = -1.0;
};

/**
 * Waits for the foe to be knocked down, then Delay seconds of world time more, and slows the world to a crawl
 * for the capture. Reports the thud and the dust; neither, or no knockdown within Timeout (real), fails it.
 */
class FHawkeyeMeleeWaitForKnockdown : public IAutomationLatentCommand
{
public:
	/** InDelay: seconds past his thud (the delay it was scheduled with) to catch the puff. */
	FHawkeyeMeleeWaitForKnockdown(FAutomationTestBase* InTest, float InDelay, float InTimeout)
		: Test(InTest), Delay(InDelay), Timeout(InTimeout) {}

	virtual bool Update() override
	{
		using namespace HawkeyeMeleeShots;
		UWorld* World = FindWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const AThugCharacter* Thug = Foe.Get();
		if (!World || !PC || !Thug)
		{
			Test->AddError(TEXT("knockdown_dust.png: no world or no foe."));
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		if (Start < 0.0)
		{
			Start = Now;
		}
		if (KnockedAt < 0.0 && Thug->IsKnockedDown())
		{
			KnockedAt = World->GetTimeSeconds();
			KnockedAtWorld = KnockedAt;
		}
		// Just after his thud: Delay past the delay it was scheduled with (until he goes over, the ragdoll's).
		const float After = (KnockedAt < 0.0 ? Thug->GroundThudDelay : Thug->GetScheduledThudDelay()) + Delay;
		const bool bTimedOut = Now - Start >= Timeout;
		if (!bTimedOut && (KnockedAt < 0.0 || World->GetTimeSeconds() - KnockedAt < After))
		{
			return false;
		}
		SetCrawl(World, true);
		if (KnockedAt < 0.0)
		{
			Test->AddError(FString::Printf(TEXT("knockdown_dust.png: her heavy did not knock him down within %.1f s."), Timeout));
			return true;
		}
		const UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(World);
		const int32 Dust = Vfx ? Vfx->GetRequestCount(UHawkeyeVfxSubsystem::KnockdownDustEvent) : 0;
		const USkeletalMeshComponent* Body = Thug->GetMesh();
		const FVector Pelvis = Body && Body->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE ? Body->GetBoneLocation(TEXT("pelvis"))
			: Thug->GetActorLocation();
		const FVector Camera = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		Test->AddInfo(FString::Printf(TEXT("knockdown_dust.png: %.2f s (world) after he went down; thuds %d, dust puffs asked for %d; ")
			TEXT("his pelvis at %s, %.0f cm from the lens, %s on screen; the dust at %s (%.0f cm over his standing feet), %s on screen; ")
			TEXT("his health %.0f."),
			World->GetTimeSeconds() - KnockedAt, Thug->GetGroundThudCount(), Dust, *Vec(Pelvis), FVector::Dist(Pelvis, Camera),
			*ScreenOffset(PC, Pelvis), *Vec(DustAt), DustAt.Z - KnockFeetZ, *ScreenOffset(PC, DustAt),
			Thug->GetHealthComponent()->GetCurrentHealth()));
		if (Thug->GetGroundThudCount() < 1 || Dust < 1)
		{
			Test->AddError(TEXT("knockdown_dust.png: no thud or no dust 0.5 s after the knockdown."));
		}
		return true;
	}

private:
	FAutomationTestBase* Test;
	float Delay;
	float Timeout;
	double Start = -1.0;
	double KnockedAt = -1.0;
};

/**
 * After knockdown_dust.png, for Seconds of world time: when his pelvis first came within 35 cm of the ground
 * he stood on, against his thud's delay (the thud should land with the body, not before it).
 */
class FHawkeyeMeleeTrackFall : public IAutomationLatentCommand
{
public:
	FHawkeyeMeleeTrackFall(FAutomationTestBase* InTest, float InSeconds) : Test(InTest), Seconds(InSeconds) {}

	virtual bool Update() override
	{
		using namespace HawkeyeMeleeShots;
		UWorld* World = FindWorld();
		const AThugCharacter* Thug = Foe.Get();
		const USkeletalMeshComponent* Body = Thug ? Thug->GetMesh() : nullptr;
		if (!World || !Body || KnockedAtWorld < 0.0 || Body->GetBoneIndex(TEXT("pelvis")) == INDEX_NONE)
		{
			return true;
		}
		const float Since = static_cast<float>(World->GetTimeSeconds() - KnockedAtWorld);
		const float Pelvis = Body->GetBoneLocation(TEXT("pelvis")).Z - KnockFeetZ;
		Lowest = FMath::Min(Lowest, Pelvis);
		if (First < 0.f)
		{
			First = Since;
		}
		if (Down < 0.f && Pelvis < 35.f)
		{
			Down = Since;
		}
		if (Since < Seconds)
		{
			return false;
		}
		const FString When = Down < 0.f ? FString(TEXT("never"))
			: Down <= First ? FString::Printf(TEXT("by %.2f s (already, when watching began)"), Down)
			: FString::Printf(TEXT("at %.2f s"), Down);
		Test->AddInfo(FString::Printf(TEXT("knockdown_dust.png: his pelvis came within 35 cm of the ground %s after he went down ")
			TEXT("(lowest %.0f cm by %.1f s); the thud is at %.2f s."), *When, Lowest, Seconds, Thug->GetScheduledThudDelay()));
		return true;
	}

private:
	FAutomationTestBase* Test;
	float Seconds;
	float Down = -1.f;
	float First = -1.f;
	float Lowest = BIG_NUMBER;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeMeleeTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeMeleeTakeShot::Update()
{
	if (FileName == TEXT("heavy_strike_2.png") && !HawkeyeMeleeShots::bHaveSecondHeavy)
	{
		// One heavy in her set: no second clip to show, and no old picture of one left lying about.
		IFileManager::Get().Delete(*HawkeyeMeleeShots::ShotPath(FileName), false, false, true);
		Test->AddInfo(TEXT("heavy_strike_2.png: not taken, her set has one heavy."));
		return true;
	}
	if (FileName == TEXT("parry_flash.png"))
	{
		// 0.17 s of world time after the parry (then a crawl): the ring (0.22 to 0.3 s) still out, its 0.08 s
		// flash gone.
		int32 FlashLeft = 0;
		const FString Live = HawkeyeDescribeLiveVfx(HawkeyeMeleeShots::FindWorld(), TEXT("NS_ParryRing"), TEXT("Flash"), FlashLeft);
		Test->AddInfo(TEXT("parry_flash.png: ") + Live);
		if (FlashLeft > 0)
		{
			Test->AddError(TEXT("parry_flash.png: the parry's 0.08 s flash is still alive 0.17 s after it."));
		}
	}
	HawkeyeShots::Request(Test, HawkeyeMeleeShots::ShotPath(FileName), /*bShowUI=*/true);
	return true;
}

void HawkeyeAddMeleeShots(FAutomationTestBase* Test)
{
	using EShot = HawkeyeMeleeShots::EShot;
	auto Shot = [Test](EShot Which, float Wait)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeShot(Test, static_cast<uint8>(Which)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Wait));
	};
	auto Wait = [Test](const TCHAR* Label, float Timeout)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeWait(Test, FString(Label), Timeout));
	};
	auto Take = [Test](const TCHAR* File)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeTakeShot(Test, FString(File)));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
	};

	Shot(EShot::Setup, 0.3f);

	// Three lights, each pressed as the last one lands: the third lands about 1.0 s in with the
	// clips (about 1.4 s for the whole chain with its recovery), 0.72 s with the procedural swing.
	Shot(EShot::ComboSetup, 1.2f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeComboChain(Test, 3, 4.f));
	// Caught just after the third, once his hit flash has gone: the counter at x3, him shoved back.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.25f));
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("combo_x3.png"), 0.f);
	Take(TEXT("combo_x3.png"));
	Shot(EShot::Uncrawl, 2.5f);

	// A tap of V 0.3 s into his 0.6 s bat wind-up; the ring 0.17 s later (past the parry's hit stop).
	Shot(EShot::ParrySetup, 1.2f);
	Shot(EShot::ParrySwing, 0.3f);
	Shot(EShot::Parry, 0.17f);
	Shot(EShot::Crawl, 0.05f);
	Take(TEXT("parry_flash.png"));
	Shot(EShot::Uncrawl, 2.f);

	// The finisher on a staggered thug, caught in its slow motion.
	Shot(EShot::FinisherSetup, 1.f);
	Shot(EShot::Finisher, 0.f);
	Wait(TEXT("finisher_mid.png"), 2.f);
	Take(TEXT("finisher_mid.png"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));

	// A hit from his left: 0.1 s in, the lean at its height.
	Shot(EShot::LeanSetup, 1.2f);
	Shot(EShot::LeanHit, 0.1f);
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("hit_lean.png"), 0.f);
	Take(TEXT("hit_lean.png"));
	Shot(EShot::Uncrawl, 0.8f);

	// A light at the moment it strikes, nobody in reach so no hit stop.
	Shot(EShot::StrikeSetup, 1.2f);
	Shot(EShot::Strike, 0.f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeWaitForStrike(Test, 1.f));
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("strike_pose.png"), 0.f);
	Take(TEXT("strike_pose.png"));
	Shot(EShot::Uncrawl, 0.5f);

	// The heavy and the kick at the moment their hit windows open, nobody in reach.
	Shot(EShot::HeavySetup, 1.2f);
	Shot(EShot::Heavy, 0.f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeWaitForStrike(Test, 1.5f, TEXT("heavy_strike.png")));
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("heavy_strike.png"), 0.f);
	Take(TEXT("heavy_strike.png"));
	Shot(EShot::Uncrawl, 1.2f);

	// Her next heavy takes the next clip.
	Shot(EShot::HeavySetup, 1.2f);
	Shot(EShot::Heavy, 0.f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeWaitForStrike(Test, 1.5f, TEXT("heavy_strike_2.png")));
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("heavy_strike_2.png"), 0.f);
	Take(TEXT("heavy_strike_2.png"));
	Shot(EShot::Uncrawl, 1.2f);

	Shot(EShot::KickSetup, 1.2f);
	Shot(EShot::Kick, 0.f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeWaitForStrike(Test, 1.f, TEXT("kick.png")));
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("kick.png"), 0.f);
	Take(TEXT("kick.png"));
	Shot(EShot::Uncrawl, 1.f);

	// A fists thug's wind-up 0.3 s in, through her eyes (the glyph), then his next two swings side on.
	Shot(EShot::TelegraphSetup, 1.2f);
	Shot(EShot::TelegraphSwing, 0.3f);
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("telegraph_glyph.png"), 0.f);
	Take(TEXT("telegraph_glyph.png"));
	Shot(EShot::Uncrawl, 1.8f);
	Shot(EShot::PunchSetup, 0.3f);
	Shot(EShot::TelegraphSwing, 0.3f);
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("thug_punch_2.png"), 0.f);
	Take(TEXT("thug_punch_2.png"));
	Shot(EShot::Uncrawl, 1.8f);
	Shot(EShot::TelegraphSwing, 0.3f);
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("thug_punch.png"), 0.f);
	Take(TEXT("thug_punch.png"));
	Shot(EShot::Uncrawl, 1.8f);

	// Two alerted thugs in front of her: the fight camera in (0.5 s, plus the arm's lag), in the open and
	// with her back to a building.
	Shot(EShot::FightSetup, 1.5f);
	Wait(TEXT("fight_camera.png"), 0.f);
	Take(TEXT("fight_camera.png"));
	Shot(EShot::FightWallSetup, 1.5f);
	Wait(TEXT("fight_camera_wall.png"), 0.f);
	Take(TEXT("fight_camera_wall.png"));

	// A light at a hurt thug: the ring and the brighter bar 0.1 s in (before it lands, about 0.25 s in), then
	// the frame it lands, time stopped by the spark's own request.
	Shot(EShot::RingSetup, 1.5f);
	Shot(EShot::RingSwing, 0.1f);
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("target_ring.png"), 0.f);
	Take(TEXT("target_ring.png"));
	Shot(EShot::SparkResume, 0.f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeWaitForSpark(Test, 2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.1f));
	Take(TEXT("hit_spark.png"));
	Shot(EShot::Uncrawl, 1.5f);

	// Her heavy knocks one down; 0.2 s after his thud (1.3 s after he goes over) the puff is spreading off the ground.
	Shot(EShot::KnockSetup, 1.2f);
	Shot(EShot::KnockHeavy, 0.f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeWaitForKnockdown(Test, 0.2f, 4.f));
	Take(TEXT("knockdown_dust.png"));
	Shot(EShot::Uncrawl, 0.f);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeMeleeTrackFall(Test, 2.f));

	// Four thugs come at her from 7 m: one at a time swings, the rest wait their turn on the ring.
	Shot(EShot::TokenRingSetup, 5.f);
	Shot(EShot::Crawl, 0.05f);
	Wait(TEXT("token_ring.png"), 0.f);
	Take(TEXT("token_ring.png"));
	Shot(EShot::Uncrawl, 0.f);

	Shot(EShot::Cleanup, 0.5f);
}

/** The melee shots on their own, for tuning without the whole Kate pass. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotMelee, "Hawkeye.Screenshot.Melee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotMelee::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the melee screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddMeleeShots(this);
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
