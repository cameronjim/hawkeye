// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/VfxScreenshots.h"

#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowEffects/ArrowEffect.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "Misc/Paths.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The effect shots, queued onto Hawkeye.Screenshot.Kate after the street frame time (Kate
 * invulnerable, every placed thug frozen). All of them 30 m along East 7th Street from the
 * PlayerStart, Kate looking down the street, the effects set off through the real game path
 * (AArrowEffect::SpawnForHit with the chapter's data assets, a real arrow from her bow):
 *
 *   vfx_snowfall.png   standing still, the light snow falling round the camera
 *   vfx_footstep.png   0.9 s into a walk away from the camera: snow kicked back from her heels
 *   vfx_explosion.png  0.15 s after an explosive arrow 9 m ahead: fireball, core flash, sparks
 *   vfx_emp.png        0.25 s after an EMP 7 m ahead: the ring racing out, arcs, the screen split
 *   emp_flash.png              the EMP 0.05 s in, time stopped: the 0.14 s flash up beside the ring
 *   emp_flash_later.png        0.5 s later: the flash gone (the ring and lamp-out may remain)
 *   explosion_flash.png        the explosion 0.05 s in, time stopped: the 0.1 s core flash up
 *   explosion_flash_later.png  0.6 s later: the flash gone (smoke and scorch may remain)
 *   arrow_hit_spark.png        a standard arrow into a thug 4.5 m ahead, time stopped in the tick the spark
 *                              is asked for: the purple-white star and streaks, no blood
 *   arrow_hit_spark_later.png  0.5 s later: the spark gone (its 0.2 s star, its streaks under 0.3 s)
 *   vfx_smoke.png              2 s after a smoke arrow 9 m ahead: the cloud billowing across the street
 *   muzzle_flash.png           a gunner 6 m ahead fires past her, time stopped in the tick of the shot
 *   muzzle_flash_later.png     0.3 s after it: the 0.05 s flash and its sparks gone (the thin smoke may stay)
 *   anchor_spark.png           her own grapple bites into the nearest anchor she can target, time stopped
 *                              0.04 s after (at the bite's first tick it is all inside the anchor's block)
 *   anchor_spark_later.png     0.5 s later, mid-zip: the 0.12 s bite flash gone
 *
 * Each logs how many effects the VFX subsystem was asked for and actually spawned; the flash shots log
 * every live particle of their system by emitter, and the later shots fail if a flash is still alive.
 */
namespace HawkeyeVfxShots
{
	enum class EShot : uint8
	{
		Setup,
		Stand,
		Walk,
		StopWalk,
		Explosion,
		Emp,
		ExplosionFlash,
		EmpFlash,
		HitSparkSetup,
		HitSparkFire,
		Smoke,
		MuzzleSetup,
		MuzzleFire,
		Thaw,
		GrappleDone,
		Cleanup,
	};

	static const TCHAR* MoveActionPath = TEXT("/Game/Input/IA_Move.IA_Move");
	static const TCHAR* WeaponsPath = TEXT("/Game/Blueprints/Weapons/");
	static const TCHAR* ThugClassPath = TEXT("/Game/Blueprints/AI/BP_Thug.BP_Thug_C");

	static FVector Spot = FVector::ZeroVector;
	static float Yaw = 0.f;
	static bool bKateWasInvulnerable = false;
	static TWeakObjectPtr<AThugCharacter> Target;
	static TWeakObjectPtr<AThugCharacter> Gunner;
	static TWeakObjectPtr<AArrowProjectile> Arrow;

	/** Time as good as stopped: a flash's first capture holds the tick it was asked for. */
	static constexpr float StoppedDilation = 0.0001f;
	/**
	 * The event whose request stops time (FreezeDelay world seconds after it; FHawkeyeVfxWaitFrozen stops a
	 * delayed one), and when (world seconds) and where it came; -1 until it does.
	 */
	static FName FreezeEvent;
	static float FreezeDelay = 0.f;
	static double FrozenAt = -1.0;
	static FVector FrozenWhere = FVector::ZeroVector;
	static FDelegateHandle FreezeHook;

	static void Unfreeze(UWorld* World)
	{
		if (UHawkeyeVfxSubsystem* Vfx = World ? UHawkeyeVfxSubsystem::Find(World) : nullptr)
		{
			Vfx->OnRequested.Remove(FreezeHook);
		}
		FreezeHook.Reset();
		if (World)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
		}
	}

	/**
	 * Stops time inside the request for Event (the hook runs in the tick the effect is spawned), or Delay world
	 * seconds after it, once FHawkeyeVfxWaitFrozen sees that much has passed.
	 */
	static void FreezeOn(UWorld* World, FName Event, float Delay = 0.f)
	{
		Unfreeze(World);
		FreezeEvent = Event;
		FreezeDelay = Delay;
		FrozenAt = -1.0;
		FrozenWhere = FVector::ZeroVector;
		if (UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(World))
		{
			TWeakObjectPtr<UWorld> WeakWorld(World);
			FreezeHook = Vfx->OnRequested.AddLambda([WeakWorld](FName Requested, const FVector& At)
			{
				if (Requested == FreezeEvent && FrozenAt < 0.0 && WeakWorld.IsValid())
				{
					FrozenAt = WeakWorld->GetTimeSeconds();
					FrozenWhere = At;
					if (FreezeDelay <= 0.f)
					{
						UGameplayStatics::SetGlobalTimeDilation(WeakWorld.Get(), StoppedDilation);
					}
				}
			});
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

	static AHawkeyeCharacter* FindKate(APlayerController*& OutPC)
	{
		UWorld* World = FindWorld();
		OutPC = World ? World->GetFirstPlayerController() : nullptr;
		return OutPC ? Cast<AHawkeyeCharacter>(OutPC->GetPawn()) : nullptr;
	}

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}

	static bool Ground(UWorld* World, const FVector& XY, const AActor* Ignore, FVector& Out)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VfxShotGround), false, Ignore);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, XY.Z + 800.f), FVector(XY.X, XY.Y, XY.Z - 3000.f),
				ECC_Visibility, Params))
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}

	static FVector Along() { return FRotator(0.f, Yaw, 0.f).Vector(); }

	static void HoldMove(APlayerController* PC, const FVector2D& Value, bool bHold)
	{
		const UInputAction* Move = LoadObject<UInputAction>(nullptr, MoveActionPath);
		ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
		UEnhancedInputLocalPlayerSubsystem* Input =
			Player ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player) : nullptr;
		if (!Move || !Input)
		{
			return;
		}
		if (bHold)
		{
			Input->StartContinuousInputInjectionForAction(Move, FInputActionValue(Value), {}, {});
		}
		else
		{
			Input->StopContinuousInputInjectionForAction(Move);
		}
	}

	static void PlaceKate(AHawkeyeCharacter* Kate, APlayerController* PC, float Pitch)
	{
		const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Kate->StopAim();
		Kate->TeleportTo(Spot + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
		PC->SetViewTarget(Kate);
	}

	static UArrowDefinition* LoadArrow(const TCHAR* Name)
	{
		return LoadObject<UArrowDefinition>(nullptr, *FString::Printf(TEXT("%s%s.%s"), WeaponsPath, Name, Name));
	}

	/** Sets the arrow Name off on the ground Ahead cm down the street, as if it had landed there. */
	static bool SetOff(FAutomationTestBase* Test, AHawkeyeCharacter* Kate, const TCHAR* Name, float Ahead)
	{
		UWorld* World = Kate->GetWorld();
		UArrowDefinition* Definition = LoadArrow(Name);
		FVector Point;
		if (!Definition || !Ground(World, Spot + Along() * Ahead, Kate, Point))
		{
			Test->AddError(FString::Printf(TEXT("vfx: no %s or no ground %.0f cm ahead."), Name, Ahead));
			return false;
		}
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.ImpactPoint = Point;
		Hit.Location = Point;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = FVector::UpVector;
		Hit.TraceStart = Point + FVector(0.f, 0.f, 100.f);
		Hit.TraceEnd = Point - FVector(0.f, 0.f, 100.f);
		const AArrowEffect* Effect = AArrowEffect::SpawnForHit(World, Definition, Kate, Hit);
		Test->AddInfo(FString::Printf(TEXT("vfx: %s set off at %s (%s, niagara %s)."), Name, *Point.ToCompactString(),
			*GetNameSafe(Effect), Effect && Effect->HasEffectVfx() ? TEXT("drawn") : TEXT("NOT drawn")));
		return Effect != nullptr;
	}

	static void SetThugsThinking(UWorld* World, bool bThinking)
	{
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(bThinking);
			}
		}
	}

	static bool Setup(FAutomationTestBase* Test, AHawkeyeCharacter* Kate, APlayerController* PC)
	{
		UWorld* World = Kate->GetWorld();
		const APlayerStart* Start = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Start = *It;
			break;
		}
		if (!Start)
		{
			Test->AddError(TEXT("vfx: no PlayerStart."));
			return false;
		}
		// The PlayerStart faces the tenement row across East 7th; the street runs a quarter turn round.
		Yaw = Start->GetActorRotation().Yaw + 90.f;
		if (!Ground(World, Start->GetActorLocation() + Along() * 3000.f, Kate, Spot))
		{
			Test->AddError(TEXT("vfx: no ground 30 m down the street."));
			return false;
		}
		SetThugsThinking(World, false);
		if (UHealthComponent* Health = Kate->GetHealthComponent())
		{
			bKateWasInvulnerable = Health->IsInvulnerable();
			Health->SetInvulnerable(true);
		}
		Test->AddInfo(FString::Printf(TEXT("vfx: shots at %s looking along yaw %.0f."), *Spot.ToCompactString(), Yaw));
		return true;
	}

	static void SetUpHitSpark(FAutomationTestBase* Test, AHawkeyeCharacter* Kate, APlayerController* PC)
	{
		UWorld* World = Kate->GetWorld();
		PlaceKate(Kate, PC, -4.f);
		UClass* ThugClass = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
		FVector Point;
		if (!ThugClass || !Ground(World, Spot + Along() * 450.f, Kate, Point))
		{
			Test->AddError(TEXT("vfx: no BP_Thug or no ground for the hit spark."));
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AThugCharacter* Thug = World->SpawnActor<AThugCharacter>(ThugClass, Point + FVector(0.f, 0.f, 100.f),
			FRotator(0.f, Yaw + 180.f, 0.f), Params);
		if (Thug && !Thug->GetController())
		{
			Thug->SpawnDefaultController();
		}
		if (AThugAIController* Brain = Thug ? Cast<AThugAIController>(Thug->GetController()) : nullptr)
		{
			Brain->SetThinkingEnabled(false);
		}
		Target = Thug;
	}

	static void FireAtTarget(FAutomationTestBase* Test, AHawkeyeCharacter* Kate)
	{
		AThugCharacter* Thug = Target.Get();
		UBowComponent* Bow = Kate->FindComponentByClass<UBowComponent>();
		UArrowDefinition* Standard = LoadArrow(TEXT("DA_Arrow_Standard"));
		if (!Thug || !Bow || !Standard)
		{
			Test->AddError(TEXT("vfx: no thug, bow or standard arrow for the hit spark."));
			return;
		}
		const FVector Chest = Thug->GetActorLocation() + FVector(0.f, 0.f, 35.f);
		const FVector From = Chest - Along() * 250.f;
		TSubclassOf<AArrowProjectile> Class = Standard->ProjectileClass ? Standard->ProjectileClass
			: TSubclassOf<AArrowProjectile>(AArrowProjectile::StaticClass());
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = Kate;
		AArrowProjectile* Shot = Kate->GetWorld()->SpawnActor<AArrowProjectile>(Class, From, Along().Rotation(), Params);
		if (Shot)
		{
			Shot->InitArrow(Standard, Bow->GetBow(), 40.f, Kate, Bow);
			Shot->LaunchWithVelocity((Chest - From).GetSafeNormal() * 6000.f);
		}
		Arrow = Shot;
	}

	/** A pistol thug 6 m ahead and 1.5 m to the right, facing her, his pistol raised at a spot beside her. */
	static void SetUpMuzzle(FAutomationTestBase* Test, AHawkeyeCharacter* Kate, APlayerController* PC)
	{
		UWorld* World = Kate->GetWorld();
		PlaceKate(Kate, PC, -4.f);
		// The hit spark's thug is done with: his body would lie in this frame and take the shot.
		if (AThugCharacter* Old = Target.Get())
		{
			if (AController* Brain = Old->GetController())
			{
				Brain->Destroy();
			}
			Old->Destroy();
		}
		Target = nullptr;
		UClass* ThugClass = LoadClass<AThugCharacter>(nullptr, ThugClassPath);
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along());
		FVector Feet;
		if (!ThugClass || !Ground(World, Spot + Along() * 600.f + Side * 150.f, Kate, Feet))
		{
			Test->AddError(TEXT("muzzle_flash.png: no BP_Thug or no ground for the gunner."));
			return;
		}
		const float Facing = (Spot - Feet).Rotation().Yaw;
		const FTransform At(FRotator(0.f, Facing, 0.f), Feet + FVector(0.f, 0.f, 100.f));
		AThugCharacter* Thug = World->SpawnActorDeferred<AThugCharacter>(ThugClass, At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Thug)
		{
			Test->AddError(TEXT("muzzle_flash.png: the gunner did not spawn."));
			return;
		}
		Thug->Weapon = EThugWeapon::Pistol;
		UGameplayStatics::FinishSpawningActor(Thug, At);
		Thug->RefreshHeldWeapon();
		if (!Thug->GetController())
		{
			Thug->SpawnDefaultController();
		}
		if (AThugAIController* Brain = Cast<AThugAIController>(Thug->GetController()))
		{
			Brain->SetThinkingEnabled(false);
		}
		Gunner = Thug;
		// Past her right shoulder, so the shot crosses the frame and hits the street, not her.
		Thug->SetWeaponRaised(true, Spot + Side * 120.f + FVector(0.f, 0.f, 120.f));
	}

	static void FireMuzzle(FAutomationTestBase* Test, AHawkeyeCharacter* Kate)
	{
		AThugCharacter* Thug = Gunner.Get();
		UWeaponComponent* Weapon = Thug ? Thug->GetWeaponComponent() : nullptr;
		if (!Weapon)
		{
			Test->AddError(TEXT("muzzle_flash.png: no gunner with a weapon."));
			return;
		}
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Along());
		const FVector Aim = Spot + Side * 120.f + FVector(0.f, 0.f, 120.f);
		if (AController* Brain = Thug->GetController())
		{
			Brain->SetControlRotation((Aim - Thug->GetPawnViewLocation()).Rotation());
		}
		FreezeOn(Kate->GetWorld(), UHawkeyeVfxSubsystem::MuzzleFlashEvent);
		const bool bFired = Weapon->Fire();
		Test->AddInfo(FString::Printf(TEXT("muzzle_flash.png: the gunner fired %d, muzzle at %s."), bFired ? 1 : 0,
			*Thug->GetGlintLocation().ToCompactString()));
		if (!bFired)
		{
			Test->AddError(TEXT("muzzle_flash.png: the gunner's pistol did not fire."));
		}
	}

	static void Cleanup(FAutomationTestBase* Test, AHawkeyeCharacter* Kate, APlayerController* PC)
	{
		HoldMove(PC, FVector2D::ZeroVector, false);
		Unfreeze(Kate->GetWorld());
		for (TWeakObjectPtr<AThugCharacter>* Spawned : { &Target, &Gunner })
		{
			if (AThugCharacter* Thug = Spawned->Get())
			{
				if (AController* Brain = Thug->GetController())
				{
					Brain->Destroy();
				}
				Thug->Destroy();
			}
			*Spawned = nullptr;
		}
		if (AArrowProjectile* Shot = Arrow.Get())
		{
			Shot->Destroy();
		}
		if (UHealthComponent* Health = Kate->GetHealthComponent())
		{
			Health->SetInvulnerable(bKateWasInvulnerable);
		}
		if (const UHawkeyeVfxSubsystem* Vfx = UHawkeyeVfxSubsystem::Find(Kate))
		{
			Test->AddInfo(FString::Printf(TEXT("vfx: %d effect requests, %d spawned, this run."), Vfx->GetTotalRequests(),
				Vfx->GetSpawnCount()));
		}
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeVfxShot, FAutomationTestBase*, Test, uint8, Shot);

bool FHawkeyeVfxShot::Update()
{
	using namespace HawkeyeVfxShots;
	APlayerController* PC = nullptr;
	AHawkeyeCharacter* Kate = FindKate(PC);
	if (!Kate)
	{
		Test->AddError(TEXT("vfx: no Kate."));
		return true;
	}
	switch (static_cast<EShot>(Shot))
	{
	case EShot::Setup:
		Setup(Test, Kate, PC);
		break;
	case EShot::Stand:
		PlaceKate(Kate, PC, -6.f);
		break;
	case EShot::Walk:
		PlaceKate(Kate, PC, -12.f);
		HoldMove(PC, FVector2D(0.f, 1.f), true);
		break;
	case EShot::StopWalk:
		HoldMove(PC, FVector2D::ZeroVector, false);
		break;
	case EShot::Explosion:
		PlaceKate(Kate, PC, -4.f);
		FreezeOn(Kate->GetWorld(), UHawkeyeVfxSubsystem::ExplosionEvent, 0.15f);
		SetOff(Test, Kate, TEXT("DA_Arrow_Explosive"), 900.f);
		break;
	case EShot::Emp:
		PlaceKate(Kate, PC, -10.f);
		SetOff(Test, Kate, TEXT("DA_Arrow_EMP"), 700.f);
		break;
	case EShot::ExplosionFlash:
		PlaceKate(Kate, PC, -4.f);
		FreezeOn(Kate->GetWorld(), UHawkeyeVfxSubsystem::ExplosionEvent, 0.05f);
		SetOff(Test, Kate, TEXT("DA_Arrow_Explosive"), 900.f);
		break;
	case EShot::EmpFlash:
		PlaceKate(Kate, PC, -10.f);
		FreezeOn(Kate->GetWorld(), UHawkeyeVfxSubsystem::EmpPulseEvent, 0.05f);
		SetOff(Test, Kate, TEXT("DA_Arrow_EMP"), 700.f);
		break;
	case EShot::HitSparkSetup:
		SetUpHitSpark(Test, Kate, PC);
		break;
	case EShot::HitSparkFire:
		FreezeOn(Kate->GetWorld(), UHawkeyeVfxSubsystem::HitSparkEvent);
		FireAtTarget(Test, Kate);
		break;
	case EShot::MuzzleSetup:
		SetUpMuzzle(Test, Kate, PC);
		break;
	case EShot::MuzzleFire:
		FireMuzzle(Test, Kate);
		break;
	case EShot::Thaw:
		Unfreeze(Kate->GetWorld());
		break;
	case EShot::GrappleDone:
		if (UGrappleComponent* Grapple = Kate->GetGrappleComponent(); Grapple && Grapple->IsZipping())
		{
			Grapple->ReleaseZip(/*bJump=*/false);
		}
		PlaceKate(Kate, PC, -6.f);
		break;
	case EShot::Smoke:
		PlaceKate(Kate, PC, -4.f);
		SetOff(Test, Kate, TEXT("DA_Arrow_Smoke"), 900.f);
		break;
	case EShot::Cleanup:
		Cleanup(Test, Kate, PC);
		break;
	}
	return true;
}

/** Waits until the hit spark's arrow has landed (or MaxSeconds). */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeVfxWaitArrow, FAutomationTestBase*, Test, float, MaxSeconds);

bool FHawkeyeVfxWaitArrow::Update()
{
	const AArrowProjectile* Shot = HawkeyeVfxShots::Arrow.Get();
	if (Shot && Shot->IsStuck())
	{
		Test->AddInfo(FString::Printf(TEXT("vfx: the arrow landed in %s."), *GetNameSafe(Shot->GetStuckInActor())));
		return true;
	}
	if (!Shot || FPlatformTime::Seconds() - StartTime > MaxSeconds)
	{
		Test->AddWarning(TEXT("vfx: the hit spark's arrow never landed."));
		return true;
	}
	return false;
}

/**
 * Logs every live particle of System by emitter, how long after the frozen event, and fails the test when
 * bMustBeGone and any of the Gone emitters (comma separated; every emitter when empty) still has one.
 */
class FHawkeyeVfxReportFlash : public IAutomationLatentCommand
{
public:
	FHawkeyeVfxReportFlash(FAutomationTestBase* InTest, const TCHAR* InLabel, const TCHAR* InSystem, const TCHAR* InGone, bool bInMustBeGone)
		: Test(InTest), Label(InLabel), System(InSystem), Gone(InGone), bMustBeGone(bInMustBeGone)
	{
	}

	virtual bool Update() override
	{
		using namespace HawkeyeVfxShots;
		UWorld* World = FindWorld();
		if (!World)
		{
			return true;
		}
		TArray<FString> Names;
		Gone.ParseIntoArray(Names, TEXT(","), true);
		int32 Left = 0;
		if (Names.IsEmpty())
		{
			const FString Live = HawkeyeDescribeLiveVfx(World, *System, nullptr, Left);
			Report(World, Live, Left);
			return true;
		}
		FString Live;
		for (const FString& Name : Names)
		{
			int32 Count = 0;
			Live = HawkeyeDescribeLiveVfx(World, *System, *Name, Count);
			Left += Count;
		}
		Report(World, Live, Left);
		return true;
	}

private:
	void Report(UWorld* World, const FString& Live, int32 Left)
	{
		using namespace HawkeyeVfxShots;
		const double Since = HawkeyeVfxShots::FrozenAt >= 0.0 ? World->GetTimeSeconds() - HawkeyeVfxShots::FrozenAt : -1.0;
		Test->AddInfo(FString::Printf(TEXT("%s: %.3f s after the %s at %s (time dilation %.4f); %s"), *Label, Since,
			*FreezeEvent.ToString(), *FrozenWhere.ToCompactString(), UGameplayStatics::GetGlobalTimeDilation(World), *Live));
		if (HawkeyeVfxShots::FrozenAt < 0.0)
		{
			Test->AddError(FString::Printf(TEXT("%s: the %s was never asked for."), *Label, *FreezeEvent.ToString()));
		}
		else if (bMustBeGone && Left > 0)
		{
			Test->AddError(FString::Printf(TEXT("%s: %d particles of %s (%s) still alive %.2f s after it."), *Label, Left, *System,
				Gone.IsEmpty() ? TEXT("any emitter") : *Gone, Since));
		}
	}

	FAutomationTestBase* Test;
	FString Label;
	FString System;
	FString Gone;
	bool bMustBeGone;
};

/** Waits until the frozen event has come and time has stopped (or MaxSeconds of real time). */
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FHawkeyeVfxWaitFrozen, FAutomationTestBase*, Test, FString, Label, float, MaxSeconds);

bool FHawkeyeVfxWaitFrozen::Update()
{
	using namespace HawkeyeVfxShots;
	UWorld* World = FindWorld();
	if (FrozenAt >= 0.0 && World)
	{
		if (FreezeDelay <= 0.f)
		{
			return true;
		}
		if (World->GetTimeSeconds() - FrozenAt >= FreezeDelay)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, StoppedDilation);
			return true;
		}
	}
	if (FPlatformTime::Seconds() - StartTime > MaxSeconds)
	{
		Test->AddError(FString::Printf(TEXT("%s: no %s within %.0f s."), *Label, *HawkeyeVfxShots::FreezeEvent.ToString(), MaxSeconds));
		return true;
	}
	return false;
}

/**
 * Stands Kate below the anchors nearest the effect spot in turn (out from each facade, as far back as the
 * grapple's reach allows), aims her camera at it, and fires the grapple at the first one it targets, with
 * time set to stop in the tick the anchor sparks are asked for.
 */
class FHawkeyeVfxGrappleAnchor : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeVfxGrappleAnchor(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeVfxShots;
		APlayerController* PC = nullptr;
		AHawkeyeCharacter* Kate = FindKate(PC);
		UGrappleComponent* Grapple = Kate ? Kate->GetGrappleComponent() : nullptr;
		UWorld* World = FindWorld();
		if (!Grapple || !World || !PC)
		{
			Test->AddError(TEXT("anchor_spark.png: no Kate with a grapple."));
			return true;
		}
		if (!bListed)
		{
			List(World, Kate);
			bListed = true;
		}
		if (Index >= 0 && World->GetTimeSeconds() < AimedAt + 0.6)
		{
			return false;
		}
		if (Index >= 0)
		{
			Grapple->RefreshTarget();
			if (AGrappleAnchor* Anchor = Grapple->GetTargetAnchor())
			{
				// Stopped 0.04 s in: at the bite's first tick its flash and sparks are all still inside the anchor's
				// block, hidden from her lens; by 0.04 s the sparks are out and the 0.12 s flash is still up.
				FreezeOn(World, UHawkeyeVfxSubsystem::AnchorSparksEvent, 0.04f);
				const bool bFired = Grapple->TryFire();
				Test->AddInfo(FString::Printf(TEXT("anchor_spark.png: grapple at %s, %.0f cm from her, fired %d."), *Anchor->GetName(),
					FVector::Dist(Kate->GetActorLocation(), Anchor->GetMarkerLocation()), bFired ? 1 : 0));
				if (!bFired)
				{
					Test->AddError(TEXT("anchor_spark.png: the grapple did not fire."));
				}
				return true;
			}
		}
		if (++Index >= Candidates.Num() || Index >= 15)
		{
			Test->AddError(FString::Printf(TEXT("anchor_spark.png: none of %d anchors near the effect spot became a target."),
				Candidates.Num()));
			return true;
		}
		if (const AGrappleAnchor* Anchor = Candidates[Index].Get())
		{
			const FVector Marker = Anchor->GetMarkerLocation();
			const float ToMarker = (Marker - Stands[Index]).Rotation().Yaw;
			const float HalfHeight = Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Kate->StopAim();
			Kate->TeleportTo(Stands[Index] + FVector(0.f, 0.f, HalfHeight + 2.f), FRotator(0.f, ToMarker, 0.f), false, true);
			PC->SetViewTarget(Kate);
			// The lens hangs behind and right of her: aim it, not her, at the marker.
			FRotator Rotation = (Marker - Kate->GetActorLocation()).Rotation();
			for (int32 Pass = 0; Pass < 4; ++Pass)
			{
				const FVector Lens = Kate->GetActorLocation() + Rotation.RotateVector(FVector(-350.f, 70.f, 60.f));
				Rotation = (Marker - Lens).Rotation();
			}
			PC->SetControlRotation(FRotator(Rotation.Pitch - 6.f, Rotation.Yaw - 8.f, 0.f));
		}
		AimedAt = World->GetTimeSeconds();
		return false;
	}

private:
	void List(UWorld* World, const AHawkeyeCharacter* Kate)
	{
		using namespace HawkeyeVfxShots;
		static constexpr float MaxStandBack = 1500.f;
		static constexpr float Reach = 2300.f;
		TArray<TPair<float, int32>> Order;
		TArray<TWeakObjectPtr<AGrappleAnchor>> Found;
		TArray<FVector> FoundStands;
		for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
		{
			const FVector Marker = It->GetMarkerLocation();
			const float Height = Marker.Z - Spot.Z;
			if (!It->bEnabled || FVector::Dist2D(Marker, Spot) > 8000.f || Height < 600.f || Height > 2200.f)
			{
				continue;
			}
			const float StandBack = FMath::Min(MaxStandBack, FMath::Sqrt(Reach * Reach - Height * Height));
			const FVector StandXY = Marker - It->GetActorForwardVector().GetSafeNormal2D() * StandBack;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(VfxShotAnchorStand), false, Kate);
			if (!World->LineTraceSingleByChannel(Hit, FVector(StandXY.X, StandXY.Y, Spot.Z + 300.f),
					FVector(StandXY.X, StandXY.Y, Spot.Z - 300.f), ECC_Visibility, Params)
				|| !Hit.GetActor() || Hit.GetActor()->Tags.Contains(FName(TEXT("CityBuilding"))))
			{
				continue;
			}
			Order.Emplace(FVector::Dist2D(Marker, Spot), Found.Num());
			Found.Add(*It);
			FoundStands.Add(Hit.ImpactPoint);
		}
		Order.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		for (const TPair<float, int32>& Pair : Order)
		{
			Candidates.Add(Found[Pair.Value]);
			Stands.Add(FoundStands[Pair.Value]);
		}
		Test->AddInfo(FString::Printf(TEXT("anchor_spark.png: %d anchors within 80 m of the effect spot with a street spot out front."),
			Candidates.Num()));
	}

	FAutomationTestBase* Test;
	TArray<TWeakObjectPtr<AGrappleAnchor>> Candidates;
	TArray<FVector> Stands;
	bool bListed = false;
	int32 Index = -1;
	double AimedAt = 0.0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeVfxTakeShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeVfxTakeShot::Update()
{
	const FString FullPath = HawkeyeVfxShots::ShotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

void HawkeyeAddVfxShots(FAutomationTestBase* Test)
{
	using EShot = HawkeyeVfxShots::EShot;
	auto Step = [Test](EShot Shot) { ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeVfxShot(Test, static_cast<uint8>(Shot))); };
	auto Wait = [](float Seconds) { ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Seconds)); };
	auto Take = [Test](const TCHAR* File) { ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeVfxTakeShot(Test, File)); };

	Step(EShot::Setup);
	Step(EShot::Stand);
	Wait(3.f);
	Take(TEXT("vfx_snowfall.png"));
	Wait(0.5f);

	Step(EShot::Walk);
	Wait(0.9f);
	Take(TEXT("vfx_footstep.png"));
	Wait(0.3f);
	Step(EShot::StopWalk);
	Wait(1.f);


	Step(EShot::Emp);
	Wait(0.25f);
	Take(TEXT("vfx_emp.png"));
	Wait(1.5f);

	auto Report = [Test](const TCHAR* Label, const TCHAR* System, const TCHAR* Gone, bool bMustBeGone)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeVfxReportFlash(Test, Label, System, Gone, bMustBeGone));
	};
	auto Frozen = [Test](const TCHAR* Label, float MaxSeconds)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeVfxWaitFrozen(Test, FString(Label), MaxSeconds));
		// A beat of real time (none of the world's): the stopped world ticks and draws the effect's first frame.
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
	};

	// The explosion 0.15 s in, time stopped (the world drew it as it was at that moment, whatever a capture costs).
	Step(EShot::Explosion);
	Frozen(TEXT("vfx_explosion.png"), 1.f);
	Take(TEXT("vfx_explosion.png"));
	Wait(0.3f);
	Step(EShot::Thaw);
	Wait(4.5f);

	// The EMP's and the explosion's flashes: time stops 0.05 s in (their flashes live 0.14 s and 0.1 s), then runs
	// on 0.5 s and 0.6 s. The ring, the lamp-out, the smoke and the scorch may stay; the flash sprites may not.
	Step(EShot::EmpFlash);
	Frozen(TEXT("emp_flash.png"), 1.f);
	Report(TEXT("emp_flash.png"), TEXT("NS_EmpPulse"), TEXT(""), false);
	Take(TEXT("emp_flash.png"));
	Wait(0.3f);
	Step(EShot::Thaw);
	Wait(0.5f);
	Report(TEXT("emp_flash_later.png"), TEXT("NS_EmpPulse"), TEXT("Flash"), true);
	Take(TEXT("emp_flash_later.png"));
	Wait(1.5f);

	Step(EShot::ExplosionFlash);
	Frozen(TEXT("explosion_flash.png"), 1.f);
	Report(TEXT("explosion_flash.png"), TEXT("NS_Explosion"), TEXT(""), false);
	Take(TEXT("explosion_flash.png"));
	Wait(0.3f);
	Step(EShot::Thaw);
	Wait(0.6f);
	Report(TEXT("explosion_flash_later.png"), TEXT("NS_Explosion"), TEXT("Core"), true);
	Take(TEXT("explosion_flash_later.png"));
	Wait(4.5f);

	// The arrow's spark: time stops in the tick it is asked for, then runs on for 0.5 s.
	Step(EShot::HitSparkSetup);
	Wait(1.f);
	Step(EShot::HitSparkFire);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeVfxWaitArrow(Test, 1.f));
	Frozen(TEXT("arrow_hit_spark.png"), 1.f);
	Report(TEXT("arrow_hit_spark.png"), TEXT("NS_HitSpark"), TEXT(""), false);
	Take(TEXT("arrow_hit_spark.png"));
	Wait(0.3f);
	Step(EShot::Thaw);
	Wait(0.5f);
	Report(TEXT("arrow_hit_spark_later.png"), TEXT("NS_HitSpark"), TEXT(""), true);
	Take(TEXT("arrow_hit_spark_later.png"));
	Wait(0.5f);

	Step(EShot::Smoke);
	Wait(2.f);
	Take(TEXT("vfx_smoke.png"));
	Wait(0.5f);

	// A gunner's shot: stopped in its tick, then 0.3 s on. His smoke puff (0.4 to 0.6 s) may still show.
	Step(EShot::MuzzleSetup);
	Wait(1.f);
	Step(EShot::MuzzleFire);
	Frozen(TEXT("muzzle_flash.png"), 1.f);
	Report(TEXT("muzzle_flash.png"), TEXT("NS_MuzzleFlash"), TEXT(""), false);
	Take(TEXT("muzzle_flash.png"));
	Wait(0.3f);
	Step(EShot::Thaw);
	Wait(0.3f);
	Report(TEXT("muzzle_flash_later.png"), TEXT("NS_MuzzleFlash"), TEXT("Flash,Sparks"), true);
	Take(TEXT("muzzle_flash_later.png"));
	Wait(0.5f);

	// Her grapple into an anchor: stopped as it bites, then 0.5 s on (its sparks live up to 0.5 s).
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeVfxGrappleAnchor(Test));
	Frozen(TEXT("anchor_spark.png"), 3.f);
	Report(TEXT("anchor_spark.png"), TEXT("NS_AnchorSparks"), TEXT(""), false);
	Take(TEXT("anchor_spark.png"));
	Wait(0.3f);
	Step(EShot::Thaw);
	Wait(0.5f);
	Report(TEXT("anchor_spark_later.png"), TEXT("NS_AnchorSparks"), TEXT("Bite"), true);
	Take(TEXT("anchor_spark_later.png"));
	Wait(3.f);
	Step(EShot::GrappleDone);
	Wait(1.f);
	Step(EShot::Cleanup);
}

FString HawkeyeDescribeLiveVfx(const UWorld* World, const TCHAR* System, const TCHAR* Emitter, int32& OutCount)
{
	OutCount = 0;
	FString Out;
	int32 Live = 0;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		const UNiagaraComponent* Component = *It;
		if (!IsValid(Component) || Component->GetWorld() != World || !Component->GetAsset()
			|| Component->GetAsset()->GetName() != System)
		{
			continue;
		}
		FNiagaraSystemInstanceControllerConstPtr Controller = Component->GetSystemInstanceController();
		const FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
		if (!Instance)
		{
			continue;
		}
		FString Parts;
		int32 Total = 0;
		for (const FNiagaraEmitterInstanceRef& EmitterInstance : Instance->GetEmitters())
		{
			const int32 Count = EmitterInstance->GetNumParticles();
			const FString Name = EmitterInstance->GetEmitterHandle().GetName().ToString();
			Total += Count;
			if (!Emitter || Name == Emitter)
			{
				OutCount += Count;
			}
			Parts += FString::Printf(TEXT("%s%s %d"), Parts.IsEmpty() ? TEXT("") : TEXT(", "), *Name, Count);
		}
		if (Total == 0 && !Component->IsActive())
		{
			// A pooled one, finished and waiting.
			continue;
		}
		++Live;
		Out += FString::Printf(TEXT("%s%s (active %d): %s"), Out.IsEmpty() ? TEXT("") : TEXT("; "), System,
			Component->IsActive() ? 1 : 0, *Parts);
	}
	return Live ? Out : FString::Printf(TEXT("no live %s"), System);
}

/** The effect shots on their own, for tuning without the whole Kate pass. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotVfx, "Hawkeye.Screenshot.Vfx",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotVfx::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("No RHI: skipping the effect screenshots."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddVfxShots(this);
	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
