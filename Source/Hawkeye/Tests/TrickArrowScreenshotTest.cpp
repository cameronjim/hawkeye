#include "Camera/CameraComponent.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "Combat/ArrowEffects/ArrowEffectsSubsystem.h"
#include "Combat/ArrowEffects/EmpPulse.h"
#include "Combat/ArrowEffects/ExplosiveBlast.h"
#include "Combat/ArrowEffects/HeldEffect.h"
#include "Combat/ArrowEffects/SmokeCloud.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/SpotLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The trick arrows on the East Village block, through Kate's own camera, into
 * Saved/Screenshots/Kate/ alongside the rest of the Kate pass:
 *
 *   putty_hold.png   a thug 7 m down the street hit by a putty arrow, stuck with the blob on him
 *   smoke_cloud.png  a smoke arrow's cloud 9 m ahead of her on the street
 *   emp_dark.png     an EMP arrow under the street lamp nearest the street spot: that lamp dark,
 *                    the ring going out, the lamps further along still lit
 *   explosive.png    mid-flash, a thug thrown by an explosive arrow at his feet
 *   quiver_wheel.png the radial quiver open, putty highlighted
 *
 * The arrows are real AArrowProjectiles of the chapter's definitions, launched from her bow hand
 * at full-draw speed, so the impact and the effect run exactly as in play. The district's thugs are
 * frozen. Needs a real RHI; run from the standalone game (claude-docs/testing.md section 2b):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.KateTrickArrows; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotKateTrickArrows, "Hawkeye.Screenshot.KateTrickArrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeTrickShots
{
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");
	static const TCHAR* WeaponsPath = TEXT("/Game/Blueprints/Weapons/");
	static const FName ParkTag(TEXT("CityPark"));
	static constexpr float StreetOffsetFromPark = 450.f;
	static constexpr float ArrowSpeed = 6000.f;

	enum class EShot : uint8
	{
		Freeze,
		PuttyFire,
		PuttyCamera,
		SmokeFire,
		EmpFire,
		ExplosiveSetup,
		ExplosiveFire,
		WheelOpen,
		WheelClose,
		Cleanup,
	};

	static TWeakObjectPtr<AThugCharacter> Target;

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
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}

	static UArrowDefinition* LoadArrow(const TCHAR* Name)
	{
		const FString Path = FString(WeaponsPath) + Name + TEXT(".") + Name;
		return LoadObject<UArrowDefinition>(nullptr, *Path);
	}

	/** The street beside the park on the side nearest the start, and the way along it. */
	static bool FindStreetSpot(UWorld* World, FVector& OutSpot, FVector& OutAlong)
	{
		TActorIterator<APlayerStart> Start(World);
		FBox Park(ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(ParkTag))
			{
				Park = It->GetComponentsBoundingBox();
				break;
			}
		}
		if (!Start || !Park.IsValid)
		{
			return false;
		}
		const FVector Edge = Park.GetClosestPointTo(Start->GetActorLocation());
		FVector Away = (Start->GetActorLocation() - Edge).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector(0.f, 1.f, 0.f);
		}
		OutSpot = Edge + Away * StreetOffsetFromPark;
		OutAlong = FVector::CrossProduct(FVector::UpVector, Away);
		return true;
	}

	static bool FindGround(UWorld* World, const FVector& XY, float FromZ, const AActor* Ignore, FVector& OutGround)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TrickShotGround), false, Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(
				Hit, FVector(XY.X, XY.Y, FromZ), FVector(XY.X, XY.Y, -5000.f), ECC_Visibility, Params))
		{
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return true;
	}

	/** Stands Kate on Ground facing Yaw, the camera pitched Pitch, looking through her own camera. */
	static void PlaceKate(AHawkeyeCharacter* Kate, APlayerController* PC, const FVector& Ground, float Yaw, float Pitch)
	{
		Kate->StopAim();
		Kate->TeleportTo(Ground + FVector(0.f, 0.f, Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
			FRotator(0.f, Yaw, 0.f), false, true);
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
		PC->SetViewTarget(Kate);
	}

	/** Kate at the street spot facing along it. False (with a warning) when the district has no park. */
	static bool PlaceAtStreetSpot(FAutomationTestBase* Test, UWorld* World, AHawkeyeCharacter* Kate,
		APlayerController* PC, FVector& OutGround, FVector& OutAlong)
	{
		FVector Spot;
		if (!FindStreetSpot(World, Spot, OutAlong) || !FindGround(World, Spot, 3000.f, Kate, OutGround))
		{
			Test->AddWarning(TEXT("No street spot beside the park."));
			return false;
		}
		PlaceKate(Kate, PC, OutGround, OutAlong.Rotation().Yaw, -10.f);
		return true;
	}

	static AThugCharacter* SpawnThug(UWorld* World, const FVector& Ground, float Yaw)
	{
		UClass* ThugClass = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const float HalfHeight = GetDefault<AThugCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		AThugCharacter* Thug = World->SpawnActor<AThugCharacter>(ThugClass ? ThugClass : AThugCharacter::StaticClass(),
			Ground + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), Params);
		if (AThugAIController* Brain = Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr)
		{
			// A target, not a fight: he stands there.
			Brain->SetThinkingEnabled(false);
		}
		return Thug;
	}

	/** Launches one of Arrow from Kate's bow hand at Aim, at full-draw speed, as a release would. */
	static AArrowProjectile* Loose(
		FAutomationTestBase* Test, AHawkeyeCharacter* Kate, UArrowDefinition* Arrow, const FVector& Aim)
	{
		UBowComponent* Bow = Kate->GetBowComponent();
		if (!Bow || !Arrow)
		{
			Test->AddWarning(
				FString::Printf(TEXT("Cannot loose %s: no bow or no arrow definition."), *GetNameSafe(Arrow)));
			return nullptr;
		}
		const FVector From = Bow->GetArrowSpawnLocation();
		const FVector Direction = (Aim - From).GetSafeNormal();
		AArrowProjectile* Projectile = Bow->SpawnArrowProjectile(Arrow, AArrowProjectile::StaticClass(), Direction);
		if (!Projectile)
		{
			Test->AddWarning(FString::Printf(TEXT("Could not spawn %s."), *Arrow->GetName()));
			return nullptr;
		}
		Projectile->InitArrow(Arrow, Bow->GetBow(), Arrow->Damage, Kate, Bow);
		Projectile->LaunchWithVelocity(Direction * ArrowSpeed);
		Test->AddInfo(FString::Printf(TEXT("Loosed %s from %s at %s (%.0f cm)."), *Arrow->GetName(),
			*From.ToCompactString(), *Aim.ToCompactString(), FVector::Dist(From, Aim)));
		return Projectile;
	}

	/** Points the camera from where it is now at Point. */
	static void LookAt(APlayerController* PC, AHawkeyeCharacter* Kate, const FVector& Point)
	{
		const FVector Lens = Kate->GetFollowCamera()->GetComponentLocation();
		const FRotator Rotation = (Point - Lens).Rotation();
		PC->SetControlRotation(FRotator(Rotation.Pitch, Rotation.Yaw, 0.f));
	}

	static void DestroyAll(UWorld* World)
	{
		for (TActorIterator<AArrowEffect> It(World); It; ++It)
		{
			It->Destroy();
		}
		for (TActorIterator<AArrowProjectile> It(World); It; ++It)
		{
			It->Destroy();
		}
		if (AThugCharacter* Thug = Target.Get())
		{
			Thug->Destroy();
		}
		Target.Reset();
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeTrickShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeTrickShot::Update()
{
	using namespace HawkeyeTrickShots;
	UWorld* World = FindWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UInventoryComponent* Inventory = Kate ? Kate->GetInventoryComponent() : nullptr;
	if (!Kate || !Inventory)
	{
		Test->AddError(TEXT("No Kate with an inventory."));
		return true;
	}

	FVector Ground, Along;
	switch (static_cast<EShot>(Shot))
	{
		case EShot::Freeze:
			for (TActorIterator<AThugAIController> It(World); It; ++It)
			{
				It->SetThinkingEnabled(false);
			}
			Kate->GetHealthComponent()->SetInvulnerable(true);
			Test->AddInfo(FString::Printf(TEXT("Quiver: putty %d, bola %d, smoke %d, EMP %d, explosive %d."),
				Inventory->GetArrowCount(3), Inventory->GetArrowCount(4), Inventory->GetArrowCount(5),
				Inventory->GetArrowCount(6), Inventory->GetArrowCount(7)));
			if (Inventory->GetArrowCount(3) != 2 || Inventory->GetArrowCount(6) != 1)
			{
				Test->AddWarning(TEXT("Kate does not carry CH01's trick arrows; DA_CH01_Rooftops should grant them."));
			}
			break;

		case EShot::PuttyFire:
		{
			if (!PlaceAtStreetSpot(Test, World, Kate, PC, Ground, Along))
			{
				break;
			}
			FVector ThugGround;
			if (!FindGround(World, Ground + Along * 700.f, Ground.Z + 400.f, Kate, ThugGround))
			{
				Test->AddWarning(TEXT("putty_hold.png: no ground for the thug."));
				break;
			}
			Target = SpawnThug(World, ThugGround, Along.Rotation().Yaw + 180.f);
			if (AThugCharacter* Thug = Target.Get())
			{
				LookAt(PC, Kate, Thug->GetActorLocation());
				Loose(
					Test, Kate, LoadArrow(TEXT("DA_Arrow_Putty")), Thug->GetActorLocation() + FVector(0.f, 0.f, 35.f));
			}
			break;
		}

		case EShot::PuttyCamera:
		{
			AThugCharacter* Thug = Target.Get();
			const AHeldEffect* Held = nullptr;
			for (TActorIterator<AHeldEffect> It(World); It; ++It)
			{
				Held = *It;
			}
			const AThugAIController* Brain = Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr;
			Test->AddInfo(FString::Printf(TEXT("putty_hold: effect %s phase %d on %s; brain held %d; health %.0f."),
				*GetNameSafe(Held), Held ? static_cast<int32>(Held->GetPhase()) : -1,
				*GetNameSafe(Held ? Held->GetTarget() : nullptr), Brain && Brain->IsHeld() ? 1 : 0,
				Thug ? Thug->GetHealthComponent()->GetCurrentHealth() : -1.f));
			if (!Held || Held->GetPhase() != EHeldPhase::Held)
			{
				Test->AddWarning(TEXT("putty_hold.png: the thug is not held."));
			}
			if (!Thug)
			{
				break;
			}
			// Close, in front of him and off to one side, so the blob on his chest is the subject.
			const FVector Front = Thug->GetActorForwardVector().GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Front);
			FVector Stand;
			if (FindGround(World, Thug->GetActorLocation() + Front * 170.f - Side * 120.f,
					Thug->GetActorLocation().Z + 200.f, Kate, Stand))
			{
				// Looking a little left of him, so the over-the-shoulder camera has Kate on the left of
				// the frame and him clear on the right.
				const float Yaw = (Thug->GetActorLocation() - Stand).GetSafeNormal2D().Rotation().Yaw;
				PlaceKate(Kate, PC, Stand, Yaw, -6.f);
				PC->SetControlRotation(FRotator(-6.f, Yaw - 22.f, 0.f));
				Test->AddInfo(FString::Printf(TEXT("putty_hold: Kate at %s, %.0f cm from him."),
					*Stand.ToCompactString(), FVector::Dist2D(Stand, Thug->GetActorLocation())));
			}
			break;
		}

		case EShot::SmokeFire:
		{
			DestroyAll(World);
			if (!PlaceAtStreetSpot(Test, World, Kate, PC, Ground, Along))
			{
				break;
			}
			FVector Landing;
			if (FindGround(World, Ground + Along * 1300.f, Ground.Z + 400.f, Kate, Landing))
			{
				Loose(Test, Kate, LoadArrow(TEXT("DA_Arrow_Smoke")), Landing);
			}
			break;
		}

		case EShot::EmpFire:
		{
			DestroyAll(World);
			if (!PlaceAtStreetSpot(Test, World, Kate, PC, Ground, Along))
			{
				break;
			}
			// The lamp nearest the street spot, seen from 14 m back along the pavement it stands on.
			ASpotLight* Lamp = nullptr;
			float Best = TNumericLimits<float>::Max();
			for (TActorIterator<ASpotLight> It(World); It; ++It)
			{
				const float Distance = FVector::Dist2D(It->GetActorLocation(), Ground);
				if (It->ActorHasTag(UArrowEffectsSubsystem::LampTag) && Distance < Best)
				{
					Best = Distance;
					Lamp = *It;
				}
			}
			FVector LampGround, Stand;
			if (!Lamp ||
				!FindGround(World, Lamp->GetActorLocation(), Lamp->GetActorLocation().Z - 100.f, Kate, LampGround) ||
				!FindGround(World, LampGround - Along * 1400.f, LampGround.Z + 400.f, Kate, Stand))
			{
				Test->AddWarning(TEXT("emp_dark.png: no CityLamp near the street spot."));
				break;
			}
			PlaceKate(Kate, PC, Stand, Along.Rotation().Yaw, -6.f);
			LookAt(PC, Kate, LampGround + FVector(0.f, 0.f, 250.f));
			// At the lamp's pole a metre up: a flat shot at the road either skims into a kerb short of the
			// lamp or sails past it.
			FVector Aim = LampGround + FVector(0.f, 0.f, 100.f);
			float PoleDistance = 300.f;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				FVector Centre, Extent;
				It->GetActorBounds(false, Centre, Extent);
				const float Distance = FVector::Dist2D(Centre, Lamp->GetActorLocation());
				if (It->ActorHasTag(UArrowEffectsSubsystem::LampTag) && *It != Lamp && Extent.Z > 250.f &&
					Distance < PoleDistance)
				{
					PoleDistance = Distance;
					Aim = FVector(Centre.X, Centre.Y, Centre.Z - Extent.Z + 130.f);
				}
			}
			Loose(Test, Kate, LoadArrow(TEXT("DA_Arrow_EMP")), Aim);
			Test->AddInfo(FString::Printf(TEXT("emp_dark: lamp %s at %s, Kate at %s."), *Lamp->GetName(),
				*Lamp->GetActorLocation().ToCompactString(), *Stand.ToCompactString()));
			break;
		}

		case EShot::ExplosiveSetup:
		{
			DestroyAll(World);
			if (!PlaceAtStreetSpot(Test, World, Kate, PC, Ground, Along))
			{
				break;
			}
			Inventory->AddArrows(LoadArrow(TEXT("DA_Arrow_Explosive")), 1);
			FVector ThugGround;
			if (FindGround(World, Ground + Along * 1000.f, Ground.Z + 400.f, Kate, ThugGround))
			{
				Target = SpawnThug(World, ThugGround, Along.Rotation().Yaw + 150.f);
			}
			break;
		}

		case EShot::ExplosiveFire:
			if (AThugCharacter* Thug = Target.Get())
			{
				// At his belt: a low shot along the street skims into the kerb before it reaches his feet.
				LookAt(PC, Kate, Thug->GetActorLocation() + FVector(0.f, 0.f, 80.f));
				Loose(Test, Kate, LoadArrow(TEXT("DA_Arrow_Explosive")),
					Thug->GetActorLocation() - FVector(0.f, 0.f, 20.f));
			}
			break;

		case EShot::WheelOpen:
			DestroyAll(World);
			if (PlaceAtStreetSpot(Test, World, Kate, PC, Ground, Along))
			{
				Kate->OpenQuiverWheel();
				Kate->SetQuiverWheelCursor(FVector2D(100.f, 0.f));
				Test->AddInfo(FString::Printf(TEXT("quiver_wheel: open %d, highlight %d, time x%.2f."),
					Kate->IsQuiverWheelOpen() ? 1 : 0, Kate->GetQuiverWheelHighlight(),
					World->GetWorldSettings()->TimeDilation));
			}
			break;

		case EShot::WheelClose:
			Kate->CloseQuiverWheel(true);
			Test->AddInfo(
				FString::Printf(TEXT("quiver_wheel: closed, slot %d nocked."), Inventory->GetActiveArrowSlot()));
			Inventory->SelectArrowSlot(1);
			break;

		case EShot::Cleanup:
			DestroyAll(World);
			Kate->GetHealthComponent()->SetInvulnerable(false);
			break;
	}
	return true;
}

/** Waits (up to TimeoutSeconds) for an effect of EffectClass to exist, then HoldSeconds more. */
class FHawkeyeWaitTrickEffect : public IAutomationLatentCommand
{
public:
	FHawkeyeWaitTrickEffect(
		FAutomationTestBase* InTest, UClass* InEffectClass, float InHoldSeconds, float InTimeoutSeconds)
		: Test(InTest), EffectClass(InEffectClass), HoldSeconds(InHoldSeconds), TimeoutSeconds(InTimeoutSeconds)
	{
	}

	virtual bool Update() override
	{
		UWorld* World = HawkeyeTrickShots::FindWorld();
		if (!World)
		{
			return true;
		}
		const double Now = FPlatformTime::Seconds();
		if (StartTime < 0.0)
		{
			StartTime = Now;
		}
		if (SeenTime < 0.0)
		{
			for (TActorIterator<AArrowEffect> It(World); It; ++It)
			{
				if (It->IsA(EffectClass))
				{
					SeenTime = Now;
					Test->AddInfo(FString::Printf(TEXT("%s appeared after %.2f s at %s."), *It->GetName(),
						Now - StartTime, *It->GetImpactPoint().ToCompactString()));
					break;
				}
			}
			if (SeenTime < 0.0 && Now - StartTime > TimeoutSeconds)
			{
				Test->AddWarning(FString::Printf(TEXT("No %s appeared."), *GetNameSafe(EffectClass)));
				return true;
			}
			return false;
		}
		return Now - SeenTime >= HoldSeconds;
	}

private:
	FAutomationTestBase* Test;
	UClass* EffectClass;
	float HoldSeconds;
	float TimeoutSeconds;
	double StartTime = -1.0;
	double SeenTime = -1.0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeTrickTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeTrickTakeShot::Update()
{
	const FString FullPath = HawkeyeTrickShots::ShotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

bool FHawkeyeScreenshotKateTrickArrows::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTrickShots;
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the trick arrow screenshots."));
		return true;
	}

	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::Freeze)));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::PuttyFire)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitTrickEffect(this, AHeldEffect::StaticClass(), 0.8f, 2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::PuttyCamera)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickTakeShot(this, TEXT("putty_hold.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::SmokeFire)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitTrickEffect(this, ASmokeCloud::StaticClass(), 1.5f, 2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickTakeShot(this, TEXT("smoke_cloud.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::EmpFire)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitTrickEffect(this, AEmpPulse::StaticClass(), 0.45f, 2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickTakeShot(this, TEXT("emp_dark.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::ExplosiveSetup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::ExplosiveFire)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitTrickEffect(this, AExplosiveBlast::StaticClass(), 0.2f, 2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickTakeShot(this, TEXT("explosive.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::WheelOpen)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.8f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickTakeShot(this, TEXT("quiver_wheel.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::WheelClose)));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTrickShot(this, static_cast<uint8>(EShot::Cleanup)));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
